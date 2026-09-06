#!/usr/bin/env python3
"""Inventory every gameplay-board interval and Troggle species in Number Demo.

This is deliberately an exhaustive capture inventory, not a behavioral-parity
claim.  It proves which species are and are not visible anywhere on a live
gameplay board in the supplied 22,004-frame lossless Demo recording.
"""

from __future__ import annotations

import json
from pathlib import Path
import subprocess

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-troggle-inventory-report.json"
)

LOGICAL_FRAME_BYTES = common.LOGICAL_WIDTH * common.LOGICAL_HEIGHT * 3

# Four pixels which remain invariant on a fully presented gameplay board and
# distinguish it from the title, Hall, feedback, and transition compositions.
BOARD_SENTINELS = {
    (18, 178): (255, 85, 255),
    (20, 26): (255, 85, 255),
    (308, 176): (255, 85, 255),
    (20, 25): (0, 0, 121),
}
BOARD_CROP = (18, 25, 309, 177)

SPECIES_COLORS = {
    "Reggie": (255, 93, 93),
    "Bashful": (93, 190, 255),
    "Worker": (203, 93, 255),
    "Helper": (211, 255, 93),
    "Smarty": (255, 251, 93),
}

EXPECTED_BOARD_RUNS = [
    {
        "first_frame": 2390,
        "last_frame": 6171,
        "species_present": ["Bashful", "Reggie"],
    },
    {
        "first_frame": 8352,
        "last_frame": 9005,
        "species_present": [],
    },
    {
        "first_frame": 11186,
        "last_frame": 12392,
        "species_present": ["Reggie", "Worker"],
    },
    {
        "first_frame": 15320,
        "last_frame": 16390,
        "species_present": ["Bashful", "Reggie", "Worker"],
    },
    {
        "first_frame": 18569,
        "last_frame": 20527,
        "species_present": ["Helper"],
    },
]

EXPECTED_CLASSIFIED_WINDOWS = {
    "bottom_entry": {
        "first_global_frame": 5188,
        "last_global_frame": 5215,
        "decoded_frames": 28,
        "nonuniform_2x_blocks": 20,
        "logical_run_count": 8,
    },
    "bottom_exit": {
        "first_global_frame": 5700,
        "last_global_frame": 5715,
        "decoded_frames": 16,
        "nonuniform_2x_blocks": 26,
        "logical_run_count": 6,
    },
}


def board_is_present(frame: np.ndarray) -> bool:
    return all(
        tuple(int(value) for value in frame[y, x]) == rgb
        for (x, y), rgb in BOARD_SENTINELS.items()
    )


def new_run(first_frame: int) -> dict[str, object]:
    return {
        "first_frame": first_frame,
        "last_frame": first_frame,
        "frames": 1,
        "species_frames_present": {name: 0 for name in SPECIES_COLORS},
        "species_pixels": {name: 0 for name in SPECIES_COLORS},
    }


def accumulate_species(run: dict[str, object], frame: np.ndarray) -> None:
    left, top, right, bottom = BOARD_CROP
    crop = frame[top:bottom + 1, left:right + 1]
    frame_counts = run["species_frames_present"]
    pixel_counts = run["species_pixels"]
    assert isinstance(frame_counts, dict)
    assert isinstance(pixel_counts, dict)
    for name, color in SPECIES_COLORS.items():
        count = int(np.count_nonzero(np.all(crop == color, axis=2)))
        pixel_counts[name] += count
        if count:
            frame_counts[name] += 1


def scan_capture(path: Path) -> tuple[list[dict[str, object]], int]:
    process = subprocess.Popen(
        [
            "ffmpeg", "-v", "error", "-i", str(path), "-map", "0:v:0",
            "-vf", "scale=320:200:flags=neighbor", "-fps_mode", "passthrough",
            "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if process.stdout is None or process.stderr is None:
        raise RuntimeError("ffmpeg did not expose raw-video pipes")

    runs: list[dict[str, object]] = []
    decoded_frames = 0
    while True:
        raw = process.stdout.read(LOGICAL_FRAME_BYTES)
        if not raw:
            break
        if len(raw) != LOGICAL_FRAME_BYTES:
            raise ValueError("raw-video decode ended with a partial frame")
        frame = np.frombuffer(raw, dtype=np.uint8).reshape(
            common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3
        )
        if board_is_present(frame):
            if not runs or int(runs[-1]["last_frame"]) != decoded_frames - 1:
                runs.append(new_run(decoded_frames))
            else:
                runs[-1]["last_frame"] = decoded_frames
                runs[-1]["frames"] = int(runs[-1]["frames"]) + 1
            accumulate_species(runs[-1], frame)
        decoded_frames += 1

    process.stdout.close()
    error_text = process.stderr.read().decode("utf-8", errors="replace")
    return_code = process.wait()
    if return_code:
        raise subprocess.CalledProcessError(
            return_code, process.args, stderr=error_text
        )
    for run in runs:
        run["species_present"] = sorted(
            name for name, count in run["species_frames_present"].items()
            if count
        )
    return runs, decoded_frames


def classify_window(first: int, last: int) -> dict[str, int]:
    common.WINDOW_FIRST_FRAME = first
    common.WINDOW_LAST_FRAME = last
    _, summary = common.decode_window(CAPTURE)
    return summary


def main() -> None:
    source_sha256 = common.sha256_file(CAPTURE)
    probe = common.probe_capture(CAPTURE)
    board_runs, decoded_frames = scan_capture(CAPTURE)

    compact_runs = [
        {
            "first_frame": int(run["first_frame"]),
            "last_frame": int(run["last_frame"]),
            "species_present": run["species_present"],
        }
        for run in board_runs
    ]
    board_runs_match = compact_runs == EXPECTED_BOARD_RUNS
    smarty_board_pixels = sum(
        int(run["species_pixels"]["Smarty"]) for run in board_runs
    )

    classified_windows = {
        name: classify_window(
            expected["first_global_frame"], expected["last_global_frame"]
        )
        for name, expected in EXPECTED_CLASSIFIED_WINDOWS.items()
    }
    classified_windows_match = (
        classified_windows == EXPECTED_CLASSIFIED_WINDOWS
    )

    source_matches = source_sha256 == common.EXPECTED_CAPTURE_SHA256
    probe_matches = probe == {
        "codec": "zmbv",
        "pixel_format": "bgr0",
        "width": 640,
        "height": 400,
        "frame_rate_fraction": "2190197/31250",
        "frame_rate_hz": 70.086304,
        "frames": common.EXPECTED_CAPTURE_FRAMES,
        "duration_seconds": 313.955777,
        "audio_codec": "pcm_s16le",
        "audio_sample_rate": 48_000,
        "audio_channels": 2,
    }

    report = {
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_matches,
        "probe": probe,
        "probe_matches": probe_matches,
        "decoded_frames": decoded_frames,
        "decoded_frames_match": decoded_frames == common.EXPECTED_CAPTURE_FRAMES,
        "board_detection": {
            "logical_sentinel_pixels": {
                f"{x},{y}": list(rgb)
                for (x, y), rgb in BOARD_SENTINELS.items()
            },
            "logical_species_crop_inclusive": list(BOARD_CROP),
        },
        "board_runs": board_runs,
        "board_runs_match": board_runs_match,
        "smarty_gameplay_board_pixels": smarty_board_pixels,
        "smarty_absent_from_all_detected_gameplay_boards": (
            smarty_board_pixels == 0
        ),
        "classified_uncatalogued_bashful_windows": {
            "bottom_entry": {
                **classified_windows["bottom_entry"],
                "classification": (
                    "all seven presentation-complete pages now have an isolated "
                    "full-frame native gate using the directly captured concurrent "
                    "Muncher state; the corrected continuous route remains distinct; "
                    "see full-demo-later-bottom-entry-report.json"
                ),
            },
            "bottom_exit": {
                **classified_windows["bottom_exit"],
                "classification": (
                    "all seven completed pages in the surrounding frames "
                    "5687-5718 dwell/exit/removal interval now have an "
                    "isolated native gate with the directly captured "
                    "concurrent Muncher state; frame 5711 is an exact "
                    "scanline-90 splice; see "
                    "full-demo-later-bottom-exit-report.json"
                ),
            },
            "focused_window_counts_match": classified_windows_match,
        },
        "scope": (
            "capture inventory only: absence proves no live Smarty comparison "
            "can be extracted from this recording, not that Smarty behavior is "
            "fully parity-verified"
        ),
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        report["decoded_frames_match"],
        board_runs_match,
        smarty_board_pixels == 0,
        classified_windows_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
