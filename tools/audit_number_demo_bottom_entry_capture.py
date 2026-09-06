#!/usr/bin/env python3
"""Audit the bottom-edge Bashful arrival in the supplied full Number demo.

The lossless ZMBV source exposes the complete four-phase vertical reveal,
one incomplete DOSBox-X 2x refresh, and the terminal repaint that restores
the overwritten y=176 grid rule.  This tool pins the full source while
decoding only the focused global-frame interval used by the native replay
regression.
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
    "full-demo-bottom-entry-report.json"
)

EXPECTED_CAPTURE_SHA256 = (
    "ED2DBDA447B54E33DCD2019DA0CB883F3E2B4FF6565EEE5AAE3427C404CE64F1"
)
EXPECTED_CAPTURE_FRAMES = 22_004
EXPECTED_AUDIO_BYTES = 60_277_056
EXPECTED_AUDIO_NONZERO_BYTES = 44_230_428
WINDOW_FIRST_FRAME = 2864
WINDOW_LAST_FRAME = 2918
EXPECTED_WINDOW_FRAMES = 55
EXPECTED_WINDOW_NONUNIFORM_BLOCKS = 22
EXPECTED_WINDOW_RUNS = 7

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


# Indexes are relative to the focused window's logical-frame RLE.
EXPECTED_STATES = {
    "pre_entry": (0, 2864, 2865, 0xB8270ABED0E3BAD1),
    "entry_phase_2": (1, 2866, 2867, 0x9B285BE3F8494708),
    "entry_phase_3": (2, 2868, 2869, 0x4DCB586C49227EB7),
    "entry_phase_4": (4, 2871, 2872, 0x596CA0BAAAB14F93),
    "entry_phase_5": (5, 2873, 2874, 0x7D512488E348373C),
    "entry_dwell_grid_restored": (6, 2875, 2918, 0xB5E9980AA54703C6),
}

EXPECTED_GEOMETRY = {
    "pre_entry": (None, 0, 49, 293),
    "entry_phase_2": ((33, 170, 41, 177, 31), 19, 8, 293),
    "entry_phase_3": ((31, 164, 41, 177, 60), 21, 8, 293),
    "entry_phase_4": ((32, 158, 42, 177, 78), 37, 8, 293),
    "entry_phase_5": ((30, 153, 40, 171, 74), 41, 8, 293),
    "entry_dwell_grid_restored": ((32, 152, 42, 173, 80), 0, 49, 293),
}

EXPECTED_TORN = (3, 2870, 2870, 22, 0x0C58DB712458F653)


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
    selection = (
        f"select=between(n\\,{WINDOW_FIRST_FRAME}\\,{WINDOW_LAST_FRAME})"
    )
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


def geometry(rgb: bytes) -> dict[str, object]:
    image = np.frombuffer(rgb, dtype=np.uint8).reshape(
        LOGICAL_HEIGHT, LOGICAL_WIDTH, 3
    )
    cyan = np.all(image == (0x5D, 0xBE, 0xFF), axis=2)
    cyan[:150, :] = False
    cyan[:, 70:] = False
    if np.any(cyan):
        rows, columns = np.where(cyan)
        cyan_box: list[int] | None = [
            int(columns.min()), int(rows.min()),
            int(columns.max()), int(rows.max()), int(np.count_nonzero(cyan)),
        ]
    else:
        cyan_box = None
    board_blue = np.array((0, 0, 0x79), dtype=np.uint8)
    magenta = np.array((0xFF, 0x55, 0xFF), dtype=np.uint8)
    return {
        "bottom_bashful_highlight_bbox_and_pixels": cyan_box,
        "blue_pixels_y176_x24_through_x64": int(np.count_nonzero(
            np.all(image[176, 24:65] == board_blue, axis=1)
        )),
        "magenta_pixels_y176_x20_through_x68": int(np.count_nonzero(
            np.all(image[176, 20:69] == magenta, axis=1)
        )),
        "magenta_pixels_outer_y178_x18_through_x310": int(np.count_nonzero(
            np.all(image[178, 18:311] == magenta, axis=1)
        )),
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

    stable_states: dict[str, dict[str, object]] = {}
    stable_states_match = True
    geometry_matches = True
    for name, (index, first, last, expected_hash) in EXPECTED_STATES.items():
        run = runs[index]
        actual_hash = renderer_fnv64(run.rgb)
        actual_geometry = geometry(run.rgb)
        expected_geometry = EXPECTED_GEOMETRY[name]
        geometry_matched = (
            actual_geometry["bottom_bashful_highlight_bbox_and_pixels"] ==
                (list(expected_geometry[0]) if expected_geometry[0] else None)
            and actual_geometry["blue_pixels_y176_x24_through_x64"] ==
                expected_geometry[1]
            and actual_geometry["magenta_pixels_y176_x20_through_x68"] ==
                expected_geometry[2]
            and actual_geometry["magenta_pixels_outer_y178_x18_through_x310"] ==
                expected_geometry[3]
        )
        matched = (
            run.start == first and run.end == last and
            run.nonuniform_blocks == 0 and run.has_uniform_frame and
            actual_hash == expected_hash
        )
        stable_states_match &= matched
        geometry_matches &= geometry_matched
        stable_states[name] = {
            "run_index": index,
            "first_frame": run.start,
            "last_frame": run.end,
            "frames": run.end - run.start + 1,
            "nonuniform_2x_blocks": run.nonuniform_blocks,
            "renderer_fnv64": f"0x{actual_hash:016x}",
            "geometry": actual_geometry,
            "state_matched": matched,
            "geometry_matched": geometry_matched,
        }

    torn_index, torn_first, torn_last, torn_nonuniform, torn_hash = EXPECTED_TORN
    torn = runs[torn_index]
    actual_torn_hash = renderer_fnv64(torn.rgb)
    torn_matches = (
        torn.start == torn_first and torn.end == torn_last and
        torn.nonuniform_blocks == torn_nonuniform and
        not torn.has_uniform_frame and actual_torn_hash == torn_hash
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
            "player_row_column": [1, 1],
            "active_safe_cells": [],
            "warning_visible": True,
            "visible_resident": {
                "species": "Reggie", "row": 2, "column": 0,
            },
            "entry": {
                "species": "Bashful", "direction": "up", "row": 4,
                "column": 0, "from_row": 5, "from_column": 0,
            },
            "labels_row_major_before_entry": [
                "21", "20", "232", "180", "133", "190",
                "145", "", "", "180", "130", "35",
                "207", "", "", "229", "124", "232",
                "59", "211", "176", "14", "2", "230",
                "80", "220", "188", "92", "146", "85",
            ],
        },
        "stable_states": stable_states,
        "excluded_torn_refresh": {
            "run_index": torn_index,
            "first_frame": torn.start,
            "last_frame": torn.end,
            "nonuniform_2x_blocks": torn.nonuniform_blocks,
            "renderer_fnv64": f"0x{actual_torn_hash:016x}",
            "matched": torn_matches,
        },
        "stable_states_match": stable_states_match,
        "geometry_matches": geometry_matches,
        "torn_refresh_matches": torn_matches,
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        stable_states_match,
        geometry_matches,
        torn_matches,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
