#!/usr/bin/env python3
"""Audit the lossless live Number Munchers Reggie collision and bite."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image


LOGICAL_SIZE = (320, 200)
COLLISION_CELL = (164, 86, 212, 117)
FEEDBACK_STRIP = (20, 86, 309, 117)
FRAME_RATE = 2_190_197.0 / 31_250.0
SCHEDULER_RATE = 1_193_182.0 / (0x0555 * 0x1E)
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1

OPEN = 0x2CC4AE9134DFF1EE
CLOSED = 0xE98DDA1E9E05C039

EXPECTED_BITE_RUNS = (
    (24, 25, OPEN, "tick_00_open"),
    (26, 27, CLOSED, "tick_01_closed"),
    (28, 28, 0x33D8B0B650EE6AC0, "partial_dirty_refresh"),
    (29, 30, OPEN, "tick_02_open"),
    (31, 32, CLOSED, "tick_03_closed"),
    (33, 35, OPEN, "tick_04_open"),
    (36, 37, CLOSED, "tick_05_closed"),
    (38, 39, OPEN, "tick_06_open"),
    (40, 40, 0xCF893966C4774C2A, "partial_dirty_refresh"),
    (41, 42, CLOSED, "tick_07_closed"),
    (43, 44, OPEN, "tick_08_open"),
    (45, 47, CLOSED, "tick_09_closed"),
    (48, 49, OPEN, "tick_10_open"),
    (50, 51, CLOSED, "tick_11_closed"),
    (52, 52, 0x6A2F25872B274C51, "partial_dirty_refresh"),
    (53, 54, OPEN, "tick_12_open"),
    (55, 56, CLOSED, "tick_13_closed"),
    (57, 59, OPEN, "tick_14_open"),
    (60, 61, CLOSED, "tick_15_closed"),
    (62, 63, OPEN, "tick_16_open"),
    (64, 64, 0xB08D0CFEA592050A, "partial_dirty_refresh"),
    (65, 66, CLOSED, "tick_17_closed"),
    (67, 68, OPEN, "tick_18_open"),
    (69, 71, CLOSED, "tick_19_closed"),
    (72, 73, OPEN, "tick_20_open"),
)

EXPECTED_FEEDBACK_RUNS = (
    (0, 6, 0x48D6A30F24E2367D, "approach_phase"),
    (7, 13, 0x199E6F0C4188C4FD, "approach_phase"),
    (14, 20, 0x03220401AAD29F75, "approach_phase"),
    (21, 23, 0x828510BB3016BB51, "collision_terminal"),
    (24, 25, 0x7660BEDBCBD4C19D, "tick_00_open"),
    (26, 27, 0x1B74F76DFD39457A, "tick_01_closed"),
    (28, 28, 0xB54D165852240E8B, "partial_dirty_refresh"),
    (29, 30, 0x7660BEDBCBD4C19D, "tick_02_open"),
    (31, 32, 0x1B74F76DFD39457A, "tick_03_closed"),
    (33, 35, 0x7660BEDBCBD4C19D, "tick_04_open"),
    (36, 37, 0x1B74F76DFD39457A, "tick_05_closed"),
    (38, 39, 0x7660BEDBCBD4C19D, "tick_06_open"),
    (40, 40, 0x41A06CA92DED9947, "partial_dirty_refresh"),
    (41, 42, 0x1B74F76DFD39457A, "tick_07_closed"),
    (43, 44, 0x7660BEDBCBD4C19D, "tick_08_open"),
    (45, 47, 0x1B74F76DFD39457A, "tick_09_closed"),
    (48, 49, 0x7660BEDBCBD4C19D, "tick_10_open"),
    (50, 51, 0x1B74F76DFD39457A, "tick_11_closed"),
    (52, 52, 0x26BE8BEAD1B8217C, "partial_dirty_refresh"),
    (53, 54, 0x7660BEDBCBD4C19D, "tick_12_open"),
    (55, 56, 0x1B74F76DFD39457A, "tick_13_closed"),
    (57, 59, 0x7660BEDBCBD4C19D, "tick_14_open"),
    (60, 61, 0x1B74F76DFD39457A, "tick_15_closed"),
    (62, 63, 0x7660BEDBCBD4C19D, "tick_16_open"),
    (64, 64, 0x5C899CB5A26BE9DF, "partial_dirty_refresh"),
    (65, 66, 0x1B74F76DFD39457A, "tick_17_closed"),
    (67, 68, 0x7660BEDBCBD4C19D, "tick_18_open"),
    (69, 71, 0x1B74F76DFD39457A, "tick_19_closed"),
    (72, 73, 0x7660BEDBCBD4C19D, "tick_20_open"),
    (74, 244, 0x28AB8F8FAEC245FE, "collision_feedback"),
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
        default=Path("analysis/number-live/gameplay/collision-avi-exact"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("analysis/number-live/gameplay/collision-avi-exact/report.json"),
    )
    args = parser.parse_args()

    paths = sorted(args.frames.glob("frame-*.png"))
    if len(paths) != 245:
        raise ValueError(f"expected 245 frames, found {len(paths)}")

    cell_hashes: list[int] = []
    feedback_hashes: list[int] = []
    sizes: list[tuple[int, int]] = []
    for path in paths:
        with Image.open(path) as source:
            logical = source.convert("RGB")
        sizes.append(logical.size)
        cell_hashes.append(pixel_hash(logical.crop(COLLISION_CELL)))
        feedback_hashes.append(pixel_hash(logical.crop(FEEDBACK_STRIP)))

    observed_bite = stable_runs(cell_hashes[24:74], 24)
    expected_bite = [(first, last, value) for first, last, value, _ in EXPECTED_BITE_RUNS]
    observed_feedback = stable_runs(feedback_hashes)
    expected_feedback = [
        (first, last, value) for first, last, value, _ in EXPECTED_FEEDBACK_RUNS
    ]
    sizes_match = all(size == LOGICAL_SIZE for size in sizes)
    bite_matches = observed_bite == expected_bite
    feedback_matches = observed_feedback == expected_feedback

    report = {
        "logical_size": list(LOGICAL_SIZE),
        "collision_cell": list(COLLISION_CELL),
        "feedback_strip": list(FEEDBACK_STRIP),
        "frame_count": len(paths),
        "frame_rate_hz": FRAME_RATE,
        "scheduler_rate_hz": SCHEDULER_RATE,
        "measured_bite_seconds": 50.0 / FRAME_RATE,
        "expected_21_tick_seconds": 21.0 / SCHEDULER_RATE,
        "bite_runs": run_report(EXPECTED_BITE_RUNS),
        "feedback_runs": run_report(EXPECTED_FEEDBACK_RUNS),
        "sizes_match": sizes_match,
        "bite_runs_match": bite_matches,
        "feedback_runs_match": feedback_matches,
        "valid": sizes_match and bite_matches and feedback_matches,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
