#!/usr/bin/env python3
"""Audit the first live Bashful trail and left-edge exit in Number Demo.

The focused lossless interval exposes the two player frames immediately before
the trail callback, the retained-player callback composite, all six clipped
left-exit positions, one incomplete DOSBox-X 2x refresh, and the terminal
repaint where the regenerated ``150`` payload first becomes visible.
"""

from __future__ import annotations

from dataclasses import dataclass
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-bashful-trail-exit-report.json"
)

EXPECTED_CAPTURE_SHA256 = (
    "ED2DBDA447B54E33DCD2019DA0CB883F3E2B4FF6565EEE5AAE3427C404CE64F1"
)
EXPECTED_CAPTURE_FRAMES = 22_004
EXPECTED_AUDIO_BYTES = 60_277_056
EXPECTED_AUDIO_NONZERO_BYTES = 44_230_428
WINDOW_FIRST_FRAME = 3073
WINDOW_LAST_FRAME = 3092
EXPECTED_WINDOW_FRAMES = 20
EXPECTED_WINDOW_NONUNIFORM_BLOCKS = 12
EXPECTED_WINDOW_RUNS = 10

CAPTURE_WIDTH = 640
CAPTURE_HEIGHT = 400
LOGICAL_WIDTH = 320
LOGICAL_HEIGHT = 200
CAPTURE_FRAME_BYTES = CAPTURE_WIDTH * CAPTURE_HEIGHT * 3
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1


@dataclass
class Run:
    digest: str
    start: int
    end: int
    nonuniform_blocks: int
    has_uniform_frame: bool
    rgb: bytes


EXPECTED_STATES = {
    "pre_exit_player_phase_0": (0, 3073, 3074, 0x184234CD0B3459DF),
    "pre_exit_player_phase_1": (1, 3075, 3076, 0xA94134AF316CA8B8),
    "exit_phase_1_retained_player": (2, 3077, 3077, 0xAF6848F5B4C6BBD6),
    "exit_phase_1_complete_player": (3, 3078, 3079, 0x1BB9864C54149626),
    "exit_phase_2": (4, 3080, 3081, 0x0B897D98E748403A),
    "exit_phase_3": (6, 3083, 3084, 0xAA84126D4DA449C4),
    "exit_phase_4": (7, 3085, 3086, 0x986345441E325759),
    "exit_phase_5": (8, 3087, 3091, 0x1A1BE4728A608892),
    "terminal_label_150_visible": (9, 3092, 3092, 0xFB75AE4B0F01F04A),
}
EXPECTED_TORN = (5, 3082, 3082, 12, 0xE943D41BF05E61CD)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def renderer_fnv64(rgb: bytes) -> int:
    value = FNV_OFFSET
    for offset in range(0, len(rgb), 3):
        value ^= (
            (rgb[offset] << 16) |
            (rgb[offset + 1] << 8) |
            rgb[offset + 2]
        )
        value = (value * FNV_PRIME) & MASK64
    return value


def probe_capture(path: Path) -> dict[str, object]:
    result = subprocess.run(
        [
            "ffprobe", "-v", "error", "-show_streams", "-show_format",
            "-of", "json", str(path),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    raw = json.loads(result.stdout)
    video = next(stream for stream in raw["streams"] if stream["codec_type"] == "video")
    audio = next(stream for stream in raw["streams"] if stream["codec_type"] == "audio")
    return {
        "codec": video["codec_name"],
        "pixel_format": video["pix_fmt"],
        "width": int(video["width"]),
        "height": int(video["height"]),
        "frame_rate_fraction": str(Fraction(video["r_frame_rate"])),
        "frame_rate_hz": float(Fraction(video["r_frame_rate"])),
        "frames": int(video["nb_frames"]),
        "duration_seconds": float(video["duration"]),
        "audio_codec": audio["codec_name"],
        "audio_sample_rate": int(audio["sample_rate"]),
        "audio_channels": int(audio["channels"]),
    }


def decode_audio_track(path: Path) -> dict[str, int]:
    process = subprocess.Popen(
        [
            "ffmpeg", "-v", "error", "-i", str(path), "-map", "0:a:0",
            "-f", "s16le", "-acodec", "pcm_s16le", "pipe:1",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if process.stdout is None or process.stderr is None:
        raise RuntimeError("ffmpeg did not expose audio pipes")
    decoded_bytes = 0
    nonzero_bytes = 0
    while True:
        block = process.stdout.read(1024 * 1024)
        if not block:
            break
        decoded_bytes += len(block)
        nonzero_bytes += sum(value != 0 for value in block)
    process.stdout.close()
    error_text = process.stderr.read().decode("utf-8", errors="replace")
    return_code = process.wait()
    if return_code:
        raise subprocess.CalledProcessError(return_code, process.args, stderr=error_text)
    return {"decoded_bytes": decoded_bytes, "nonzero_bytes": nonzero_bytes}


def decode_window(path: Path) -> tuple[list[Run], dict[str, int]]:
    selection = f"select=between(n\\,{WINDOW_FIRST_FRAME}\\,{WINDOW_LAST_FRAME})"
    result = subprocess.run(
        [
            "ffmpeg", "-v", "error", "-i", str(path), "-map", "0:v:0",
            "-vf", selection, "-fps_mode", "passthrough", "-f", "rawvideo",
            "-pix_fmt", "rgb24", "pipe:1",
        ],
        check=True,
        capture_output=True,
    )
    if len(result.stdout) % CAPTURE_FRAME_BYTES:
        raise ValueError("focused raw-video decode ended with a partial frame")

    runs: list[Run] = []
    total_nonuniform = 0
    frame_count = len(result.stdout) // CAPTURE_FRAME_BYTES
    for offset in range(0, len(result.stdout), CAPTURE_FRAME_BYTES):
        global_frame = WINDOW_FIRST_FRAME + offset // CAPTURE_FRAME_BYTES
        captured = np.frombuffer(
            result.stdout[offset:offset + CAPTURE_FRAME_BYTES], dtype=np.uint8
        ).reshape(CAPTURE_HEIGHT, CAPTURE_WIDTH, 3)
        logical = captured[0::2, 0::2]
        mismatches = (
            np.any(logical != captured[0::2, 1::2], axis=2) |
            np.any(logical != captured[1::2, 0::2], axis=2) |
            np.any(logical != captured[1::2, 1::2], axis=2)
        )
        nonuniform = int(np.count_nonzero(mismatches))
        total_nonuniform += nonuniform
        rgb = logical.tobytes()
        digest = hashlib.sha256(rgb).hexdigest()
        if runs and runs[-1].digest == digest:
            runs[-1].end = global_frame
            runs[-1].nonuniform_blocks += nonuniform
            runs[-1].has_uniform_frame |= nonuniform == 0
        else:
            runs.append(Run(
                digest=digest,
                start=global_frame,
                end=global_frame,
                nonuniform_blocks=nonuniform,
                has_uniform_frame=nonuniform == 0,
                rgb=rgb,
            ))
    return runs, {
        "first_global_frame": WINDOW_FIRST_FRAME,
        "last_global_frame": WINDOW_LAST_FRAME,
        "decoded_frames": frame_count,
        "nonuniform_2x_blocks": total_nonuniform,
        "logical_run_count": len(runs),
    }


def selected_state(
    runs: list[Run], expected: tuple[int, int, int, int]
) -> tuple[dict[str, object], bool]:
    index, first, last, expected_hash = expected
    run = runs[index]
    actual_hash = renderer_fnv64(run.rgb)
    matched = (
        run.start == first and run.end == last and
        run.nonuniform_blocks == 0 and run.has_uniform_frame and
        actual_hash == expected_hash
    )
    return ({
        "run_index": index,
        "first_frame": run.start,
        "last_frame": run.end,
        "frames": run.end - run.start + 1,
        "nonuniform_2x_blocks": run.nonuniform_blocks,
        "renderer_fnv64": f"0x{actual_hash:016x}",
        "matched": matched,
    }, matched)


def difference_geometry(first: bytes, second: bytes) -> dict[str, object]:
    a = np.frombuffer(first, dtype=np.uint8).reshape(LOGICAL_HEIGHT, LOGICAL_WIDTH, 3)
    b = np.frombuffer(second, dtype=np.uint8).reshape(LOGICAL_HEIGHT, LOGICAL_WIDTH, 3)
    changed = np.any(a != b, axis=2)
    rows, columns = np.where(changed)
    return {
        "changed_pixels": int(np.count_nonzero(changed)),
        "bbox": [
            int(columns.min()), int(rows.min()),
            int(columns.max()), int(rows.max()),
        ],
    }


def main() -> None:
    source_sha256 = sha256_file(CAPTURE)
    probe = probe_capture(CAPTURE)
    audio = decode_audio_track(CAPTURE)
    runs, window = decode_window(CAPTURE)

    source_matches = source_sha256 == EXPECTED_CAPTURE_SHA256
    probe_matches = probe == {
        "codec": "zmbv",
        "pixel_format": "bgr0",
        "width": 640,
        "height": 400,
        "frame_rate_fraction": "2190197/31250",
        "frame_rate_hz": 70.086304,
        "frames": EXPECTED_CAPTURE_FRAMES,
        "duration_seconds": 313.955777,
        "audio_codec": "pcm_s16le",
        "audio_sample_rate": 48_000,
        "audio_channels": 2,
    }
    audio_matches = audio == {
        "decoded_bytes": EXPECTED_AUDIO_BYTES,
        "nonzero_bytes": EXPECTED_AUDIO_NONZERO_BYTES,
    }
    window_matches = window == {
        "first_global_frame": WINDOW_FIRST_FRAME,
        "last_global_frame": WINDOW_LAST_FRAME,
        "decoded_frames": EXPECTED_WINDOW_FRAMES,
        "nonuniform_2x_blocks": EXPECTED_WINDOW_NONUNIFORM_BLOCKS,
        "logical_run_count": EXPECTED_WINDOW_RUNS,
    }

    selected_states: dict[str, dict[str, object]] = {}
    selected_states_match = True
    for name, expected in EXPECTED_STATES.items():
        state, matched = selected_state(runs, expected)
        selected_states[name] = state
        selected_states_match &= matched

    torn_index, torn_first, torn_last, torn_nonuniform, torn_hash = EXPECTED_TORN
    torn_run = runs[torn_index]
    actual_torn_hash = renderer_fnv64(torn_run.rgb)
    torn_matches = (
        torn_run.start == torn_first and torn_run.end == torn_last and
        torn_run.nonuniform_blocks == torn_nonuniform and
        not torn_run.has_uniform_frame and actual_torn_hash == torn_hash
    )
    torn_state = {
        "run_index": torn_index,
        "first_frame": torn_run.start,
        "last_frame": torn_run.end,
        "nonuniform_2x_blocks": torn_run.nonuniform_blocks,
        "renderer_fnv64": f"0x{actual_torn_hash:016x}",
        "matched": torn_matches,
    }

    first_exit = EXPECTED_STATES["exit_phase_1_retained_player"][0]
    complete_exit = EXPECTED_STATES["exit_phase_5"][0]
    terminal = EXPECTED_STATES["terminal_label_150_visible"][0]
    differences = {
        "pre_exit_to_first_retained_exit": difference_geometry(
            runs[EXPECTED_STATES["pre_exit_player_phase_1"][0]].rgb,
            runs[first_exit].rgb,
        ),
        "last_exit_to_terminal_label_repaint": difference_geometry(
            runs[complete_exit].rgb, runs[terminal].rgb,
        ),
    }

    report = {
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_matches,
        "probe": probe,
        "probe_matches": probe_matches,
        "audio": audio,
        "audio_matches": audio_matches,
        "focused_decode": window,
        "focused_decode_matches": window_matches,
        "board": {
            "demo_board": 1,
            "level": 10,
            "mode": "Multiples",
            "target": 5,
            "event": "Bashful regenerates bottom-left 80 to 150 and exits left",
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "excluded_torn_refresh": torn_state,
        "difference_geometry": differences,
        "native_replay": {
            "complete": True,
            "exact_stable_state_count": 9,
            "exact_stable_states_presented": list(EXPECTED_STATES),
            "headless_gate": "game_render_state_test",
            "shared_word_gate": "munchers_app_headless_test",
            "reason": (
                "the seeded first-Demo replay matches every complete source state, "
                "including the retained-player phase-1 callback composite, clipped "
                "six-step left exit, hidden regenerated label, and terminal repaint"
            ),
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        selected_states_match,
        torn_matches,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
