#!/usr/bin/env python3
"""Audit the first Troggle-on-Troggle bite in the full Number demo.

The focused lossless interval contains the last complete overlap frame, all
21 public-tick bite poses, three incomplete DOSBox-X 2x refreshes, and the
terminal dwell repaint.  The terminal is especially useful: the original
writes down-facing dwell record 15 while retaining the survivor's logical
rightward heading for its later movement callback.
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
    "full-demo-cannibal-report.json"
)

EXPECTED_CAPTURE_SHA256 = (
    "ED2DBDA447B54E33DCD2019DA0CB883F3E2B4FF6565EEE5AAE3427C404CE64F1"
)
EXPECTED_CAPTURE_FRAMES = 22_004
EXPECTED_AUDIO_BYTES = 60_277_056
EXPECTED_AUDIO_NONZERO_BYTES = 44_230_428
WINDOW_FIRST_FRAME = 2919
WINDOW_LAST_FRAME = 2996
EXPECTED_WINDOW_FRAMES = 78
EXPECTED_WINDOW_NONUNIFORM_BLOCKS = 38
EXPECTED_WINDOW_RUNS = 35

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


EXPECTED_PRE_BITE = (10, 2941, 2944, 0x378E270F4598AC61)
EXPECTED_BITE_TICKS = (
    (11, 2945, 2947, 0xFB1AEA9A6D579C2E),
    (12, 2948, 2949, 0xD49CA9360564FFED),
    (13, 2950, 2951, 0xFB1AEA9A6D579C2E),
    (15, 2953, 2954, 0xD49CA9360564FFED),
    (16, 2955, 2956, 0xFB1AEA9A6D579C2E),
    (17, 2957, 2959, 0xD49CA9360564FFED),
    (18, 2960, 2961, 0xFB1AEA9A6D579C2E),
    (19, 2962, 2963, 0xD49CA9360564FFED),
    (21, 2965, 2966, 0xFB1AEA9A6D579C2E),
    (22, 2967, 2968, 0xD49CA9360564FFED),
    (23, 2969, 2971, 0xFB1AEA9A6D579C2E),
    (24, 2972, 2973, 0xD49CA9360564FFED),
    (25, 2974, 2976, 0xFB1AEA9A6D579C2E),
    (26, 2977, 2978, 0xD49CA9360564FFED),
    (27, 2979, 2980, 0xFB1AEA9A6D579C2E),
    (28, 2981, 2983, 0xD49CA9360564FFED),
    (29, 2984, 2985, 0xFB1AEA9A6D579C2E),
    (30, 2986, 2988, 0xD49CA9360564FFED),
    (31, 2989, 2990, 0xFB1AEA9A6D579C2E),
    (32, 2991, 2992, 0xD49CA9360564FFED),
    (33, 2993, 2995, 0xFB1AEA9A6D579C2E),
)
EXPECTED_TERMINAL = (34, 2996, 2996, 0x9C2F96CD8E6918C2)
EXPECTED_TORN = (
    (9, 2940, 2940, 17, 0x1FAC963566116D67),
    (14, 2952, 2952, 9, 0x3C99293E4EEC5AB6),
    (20, 2964, 2964, 12, 0x442AAED2DE59A491),
)
EXPECTED_DIFFS = {
    "bite_pose_a_vs_b": (296, (29, 91, 52, 114)),
    "pre_bite_vs_terminal": (285, (29, 91, 51, 114)),
    "pre_bite_vs_bite_pose_a": (319, (29, 91, 52, 114)),
}


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
    return {
        "decoded_bytes": decoded_bytes,
        "nonzero_bytes": nonzero_bytes,
    }


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

    pre_bite, pre_bite_matches = selected_state(runs, EXPECTED_PRE_BITE)
    bite_ticks = []
    bite_ticks_match = True
    for tick, expected in enumerate(EXPECTED_BITE_TICKS):
        state, matched = selected_state(runs, expected)
        state["tick"] = tick
        state["record"] = (12, 13, 14, 13, 12, 13)[tick % 6]
        bite_ticks.append(state)
        bite_ticks_match &= matched
    terminal, terminal_matches = selected_state(runs, EXPECTED_TERMINAL)

    torn_states = []
    torn_states_match = True
    for index, first, last, expected_nonuniform, expected_hash in EXPECTED_TORN:
        run = runs[index]
        actual_hash = renderer_fnv64(run.rgb)
        matched = (
            run.start == first and run.end == last and
            run.nonuniform_blocks == expected_nonuniform and
            not run.has_uniform_frame and actual_hash == expected_hash
        )
        torn_states_match &= matched
        torn_states.append({
            "run_index": index,
            "first_frame": run.start,
            "last_frame": run.end,
            "nonuniform_2x_blocks": run.nonuniform_blocks,
            "renderer_fnv64": f"0x{actual_hash:016x}",
            "matched": matched,
        })

    pose_a_index = EXPECTED_BITE_TICKS[0][0]
    pose_b_index = EXPECTED_BITE_TICKS[1][0]
    differences = {
        "bite_pose_a_vs_b": difference_geometry(
            runs[pose_a_index].rgb, runs[pose_b_index].rgb
        ),
        "pre_bite_vs_terminal": difference_geometry(
            runs[EXPECTED_PRE_BITE[0]].rgb, runs[EXPECTED_TERMINAL[0]].rgb
        ),
        "pre_bite_vs_bite_pose_a": difference_geometry(
            runs[EXPECTED_PRE_BITE[0]].rgb, runs[pose_a_index].rgb
        ),
    }
    differences_match = all(
        differences[name] == {
            "changed_pixels": expected[0], "bbox": list(expected[1])
        }
        for name, expected in EXPECTED_DIFFS.items()
    )

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
            "event": "Reggie moving right into a resident Reggie at row 2 column 0",
        },
        "pre_bite_overlap": pre_bite,
        "bite_ticks": bite_ticks,
        "terminal_down_dwell": terminal,
        "difference_geometry": differences,
        "difference_geometry_matches": differences_match,
        "excluded_torn_refreshes": torn_states,
        "native_replay": {
            "complete": True,
            "exact_stable_state_count": 23,
            "exact_stable_states_presented": [
                "pre_bite_overlap",
                "bite_tick_0_through_20",
                "terminal_record_15_dwell",
            ],
            "headless_gate": "game_render_state_test",
            "shared_word_gate": "munchers_app_headless_test",
            "reason": (
                "the seeded first-Demo replay matches the complete pre-bite frame, "
                "all 21 alternating bite ticks, and terminal record-15 repaint; "
                "both runtimes retain the rightward heading separately"
            ),
        },
        "selected_states_match": (
            pre_bite_matches and bite_ticks_match and terminal_matches
        ),
        "torn_refreshes_match": torn_states_match,
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        pre_bite_matches,
        bite_ticks_match,
        terminal_matches,
        differences_match,
        torn_states_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
