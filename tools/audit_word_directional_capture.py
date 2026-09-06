#!/usr/bin/env python3
"""Audit the live Word Muncher left/up/right/down movement loop."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image


WINDOW_SIZE = (658, 553)
SURFACE_CROP = (9, 103, 649, 503)
LOGICAL_SIZE = (320, 200)
MOVEMENT_UNION = (68, 56, 164, 116)
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1

# Two one-frame hashes are incomplete DOS dirty refreshes. They are retained
# as evidence but deliberately are not rendered by the native double buffer.
EXPECTED_RUNS = (
    (0, 4, 0xB1520AA051F2D0E5, "standing"),
    (5, 5, 0x095C18FB4159183F, "left_phase_1"),
    (6, 7, 0x5C72469D262A7F70, "left_phase_2"),
    (8, 11, 0x14C639E8AE73DBD4, "left_phase_3"),
    (12, 13, 0x70837173E555809F, "left_phase_4"),
    (14, 15, 0xDFF0A418146468FC, "left_phase_5"),
    (16, 16, 0xFA5FF9D249642EAB, "left_terminal"),
    (17, 21, 0xA38181D4BDD7A498, "left_standing"),
    (22, 23, 0xCC30702E95532784, "up_phase_2"),
    (24, 26, 0xA83555055DE4886B, "up_phase_3"),
    (27, 27, 0x813850D066DE5498, "partial_dirty_refresh"),
    (28, 29, 0x5FD20038A728CE07, "up_phase_4"),
    (30, 30, 0x8354513F78FF38A4, "up_terminal"),
    (31, 39, 0xCF97EBB5A6C7F684, "up_standing"),
    (40, 40, 0x20D33B8F7D9DFA80, "right_phase_2"),
    (41, 43, 0x5E4E80A82283B717, "right_phase_3"),
    (44, 45, 0x8EC84507ADCE8E7C, "right_phase_4"),
    (46, 47, 0x0A6F3FA7DB9FBA3A, "right_phase_5"),
    (48, 48, 0x337B69AC6DFA07DD, "right_terminal"),
    (49, 56, 0xE16659A4DA7E5470, "right_standing"),
    (57, 57, 0xE4B80035436EA023, "down_phase_1"),
    (58, 60, 0x22EDCC317BCD8F16, "down_phase_2"),
    (61, 62, 0x5C571A132C797EED, "down_phase_3"),
    (63, 64, 0x01B266D37B948261, "down_phase_4"),
    (65, 66, 0xE077DCBBED08BACD, "down_terminal"),
    (67, 99, 0xB1520AA051F2D0E5, "standing"),
)


def pixel_hash(image: Image.Image) -> int:
    value = FNV_OFFSET
    for red, green, blue in image.get_flattened_data():
        value ^= (red << 16) | (green << 8) | blue
        value = (value * FNV_PRIME) & MASK64
    return value


def logical_frame(path: Path) -> tuple[Image.Image, int]:
    with Image.open(path) as source:
        window = source.convert("RGB")
    if window.size != WINDOW_SIZE:
        raise ValueError(f"{path}: expected {WINDOW_SIZE}, got {window.size}")
    surface = window.crop(SURFACE_CROP)
    pixels = surface.load()
    logical = Image.new("RGB", LOGICAL_SIZE)
    output = logical.load()
    nonuniform = 0
    for y in range(LOGICAL_SIZE[1]):
        for x in range(LOGICAL_SIZE[0]):
            block = {
                pixels[x * 2, y * 2],
                pixels[x * 2 + 1, y * 2],
                pixels[x * 2, y * 2 + 1],
                pixels[x * 2 + 1, y * 2 + 1],
            }
            if len(block) != 1:
                nonuniform += 1
            output[x, y] = pixels[x * 2, y * 2]
    return logical, nonuniform


def stable_runs(hashes: list[int]) -> list[tuple[int, int, int]]:
    runs: list[tuple[int, int, int]] = []
    start = 0
    previous = hashes[0]
    for index, value in enumerate(hashes[1:], 1):
        if value != previous:
            runs.append((start, index - 1, previous))
            start = index
            previous = value
    runs.append((start, len(hashes) - 1, previous))
    return runs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--frames",
        type=Path,
        default=Path("analysis/word-live/gameplay/directional3"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("analysis/word-live/gameplay/directional3/report.json"),
    )
    args = parser.parse_args()

    paths = sorted(args.frames.glob("frame-*.png"))
    if len(paths) != 100:
        raise ValueError(f"expected 100 frames, found {len(paths)}")
    hashes: list[int] = []
    nonuniform_counts: list[int] = []
    for path in paths:
        logical, nonuniform = logical_frame(path)
        hashes.append(pixel_hash(logical.crop(MOVEMENT_UNION)))
        nonuniform_counts.append(nonuniform)

    observed = stable_runs(hashes)
    expected = [(first, last, value) for first, last, value, _ in EXPECTED_RUNS]
    runs_match = observed == expected
    blocks_match = all(value == 0 for value in nonuniform_counts)
    run_reports = [
        {
            "first": first,
            "last": last,
            "frames": last - first + 1,
            "hash": f"0x{value:016x}",
            "classification": classification,
        }
        for first, last, value, classification in EXPECTED_RUNS
    ]
    report = {
        "window_size": list(WINDOW_SIZE),
        "surface_crop": list(SURFACE_CROP),
        "logical_size": list(LOGICAL_SIZE),
        "movement_union": list(MOVEMENT_UNION),
        "frame_count": len(paths),
        "nonuniform_2x_blocks": sum(nonuniform_counts),
        "runs": run_reports,
        "runs_match": runs_match,
        "valid": runs_match and blocks_match,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
