#!/usr/bin/env python3
"""Audit the lossless live Word Munchers Reggie collision and bite."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image


LOGICAL_SIZE = (320, 200)
COLLISION_CELL = (164, 116, 212, 147)
FEEDBACK_STRIP = (20, 86, 309, 117)
FRAME_RATE = 2_190_197.0 / 31_250.0
SCHEDULER_RATE = 1_193_182.0 / (0x0555 * 0x1E)
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1

OPEN = 0x2CC4AE9134DFF1EE
CLOSED = 0xE98DDA1E9E05C039

EXPECTED_APPROACH_RUNS = (
    (262, 269, 0x8A6AC96BD4D361CB, "phase_1_record_5"),
    (270, 276, 0x55644716910F3D5C, "phase_2_record_4"),
    (277, 283, 0xD53D0BB11B280463, "phase_3_record_3"),
    (284, 290, 0x643CE67DE4ED616E, "phase_4_record_4"),
    (291, 297, 0xF6EA119E906493A3, "phase_5_record_5"),
    (298, 298, 0x71ACDF797E7EA43F, "partial_dirty_refresh"),
    (299, 305, 0x0DB2A3F1D70B456B, "phase_6_record_4"),
)

# Frame 310 is a genuine mid-blit DOS tear. The native renderer is double
# buffered, so the exact live tear is retained as evidence but not reproduced.
EXPECTED_BITE_RUNS = (
    (308, 309, OPEN, "tick_00_open"),
    (310, 310, 0xB08D0CFEA592050A, "partial_dirty_refresh"),
    (311, 312, CLOSED, "tick_01_closed"),
    (313, 314, OPEN, "tick_02_open"),
    (315, 317, CLOSED, "tick_03_closed"),
    (318, 319, OPEN, "tick_04_open"),
    (320, 322, CLOSED, "tick_05_closed"),
    (323, 324, OPEN, "tick_06_open"),
    (325, 326, CLOSED, "tick_07_closed"),
    (327, 329, OPEN, "tick_08_open"),
    (330, 331, CLOSED, "tick_09_closed"),
    (332, 334, OPEN, "tick_10_open"),
    (335, 336, CLOSED, "tick_11_closed"),
    (337, 338, OPEN, "tick_12_open"),
    (339, 341, CLOSED, "tick_13_closed"),
    (342, 343, OPEN, "tick_14_open"),
    (344, 346, CLOSED, "tick_15_closed"),
    (347, 348, OPEN, "tick_16_open"),
    (349, 350, CLOSED, "tick_17_closed"),
    (351, 353, OPEN, "tick_18_open"),
    (354, 355, CLOSED, "tick_19_closed"),
    (356, 358, OPEN, "tick_20_open"),
)

EXPECTED_FEEDBACK_RUNS = (
    (0, 358, 0xE757E86AC714F5B2, "board"),
    (359, 590, 0x9D7E227A62F941CC, "collision_feedback"),
    (591, 591, 0x1A780896802070DE, "partial_resume_refresh"),
    (592, 630, 0xE757E86AC714F5B2, "resumed_board"),
)


def pixel_hash(image: Image.Image) -> int:
    value = FNV_OFFSET
    for red, green, blue in image.get_flattened_data():
        value ^= (red << 16) | (green << 8) | blue
        value = (value * FNV_PRIME) & MASK64
    return value


def stable_runs(values: list[int], offset: int = 0) -> list[tuple[int, int, int]]:
    runs: list[tuple[int, int, int]] = []
    first = 0
    previous = values[0]
    for index, value in enumerate(values[1:], 1):
        if value != previous:
            runs.append((first + offset, index - 1 + offset, previous))
            first = index
            previous = value
    runs.append((first + offset, len(values) - 1 + offset, previous))
    return runs


def run_report(runs: tuple[tuple[int, int, int, str], ...]) -> list[dict[str, object]]:
    return [
        {
            "first": first,
            "last": last,
            "frames": last - first + 1,
            "hash": f"0x{value:016x}",
            "classification": classification,
        }
        for first, last, value, classification in runs
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--frames",
        type=Path,
        default=Path("analysis/word-live/gameplay/collision-avi-exact"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("analysis/word-live/gameplay/collision-avi-exact/report.json"),
    )
    args = parser.parse_args()

    paths = sorted(args.frames.glob("frame-*.png"))
    if len(paths) != 631:
        raise ValueError(f"expected 631 frames, found {len(paths)}")

    cell_hashes: list[int] = []
    feedback_hashes: list[int] = []
    sizes: list[tuple[int, int]] = []
    for path in paths:
        with Image.open(path) as source:
            logical = source.convert("RGB")
        sizes.append(logical.size)
        cell_hashes.append(pixel_hash(logical.crop(COLLISION_CELL)))
        feedback_hashes.append(pixel_hash(logical.crop(FEEDBACK_STRIP)))

    observed_approach = stable_runs(cell_hashes[262:306], 262)
    expected_approach = [
        (first, last, value) for first, last, value, _ in EXPECTED_APPROACH_RUNS
    ]
    observed_bite = stable_runs(cell_hashes[308:359], 308)
    expected_bite = [(first, last, value) for first, last, value, _ in EXPECTED_BITE_RUNS]
    observed_feedback = stable_runs(feedback_hashes)
    expected_feedback = [
        (first, last, value) for first, last, value, _ in EXPECTED_FEEDBACK_RUNS
    ]
    sizes_match = all(size == LOGICAL_SIZE for size in sizes)
    approach_matches = observed_approach == expected_approach
    bite_matches = observed_bite == expected_bite
    feedback_matches = observed_feedback == expected_feedback

    report = {
        "logical_size": list(LOGICAL_SIZE),
        "collision_cell": list(COLLISION_CELL),
        "feedback_strip": list(FEEDBACK_STRIP),
        "frame_count": len(paths),
        "frame_rate_hz": FRAME_RATE,
        "scheduler_rate_hz": SCHEDULER_RATE,
        "measured_bite_seconds": 51.0 / FRAME_RATE,
        "expected_21_tick_seconds": 21.0 / SCHEDULER_RATE,
        "approach_runs": run_report(EXPECTED_APPROACH_RUNS),
        "bite_runs": run_report(EXPECTED_BITE_RUNS),
        "feedback_runs": run_report(EXPECTED_FEEDBACK_RUNS),
        "sizes_match": sizes_match,
        "approach_runs_match": approach_matches,
        "bite_runs_match": bite_matches,
        "feedback_runs_match": feedback_matches,
        "valid": sizes_match and approach_matches and bite_matches and feedback_matches,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
