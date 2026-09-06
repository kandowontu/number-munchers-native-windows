#!/usr/bin/env python3
"""Audit the preserved live Word Munchers move-and-chew capture.

The capture is already reduced to the original 320x200 logical VGA frame.
This audit deliberately hashes renderer-style 0xRRGGBB pixels rather than PNG
bytes, so its values can be asserted directly by the native C++ render tests.
"""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Iterable

from PIL import Image


FIRST_FRAME = 2440
LAST_FRAME = 2560
MOVEMENT_UNION = (164, 116, 260, 146)
CHEW_CELL = (212, 116, 260, 146)

EXPECTED_UNION_RUNS = [
    (2440, 2464, 0xA267649814989B5B),
    (2465, 2465, 0xE4F24D098E7707A1),
    (2466, 2467, 0x15EB545083D73B62),
    (2468, 2469, 0x09F369BAE8A91A8D),
    (2470, 2472, 0xF7B39E834458194A),
    (2473, 2474, 0x5580298D6BAC0860),
    (2475, 2477, 0x1B03B04A282EBB3F),
    (2478, 2522, 0x075582701FFE799E6),
    (2523, 2525, 0x0CB9AB674660D563),
    (2526, 2527, 0xA81F55D6192D6BB3),
    (2528, 2529, 0x0CB9AB674660D563),
    (2530, 2530, 0xC48A209BE76E230B),
    (2531, 2532, 0xA81F55D6192D6BB3),
    (2533, 2534, 0x0CB9AB674660D563),
    (2535, 2537, 0xA81F55D6192D6BB3),
    (2538, 2560, 0x0CB9AB674660D563),
]

EXPECTED_CHEW_RUNS = [
    (2523, 2525, 0x3FB6CDBAD7BA0655),
    (2526, 2527, 0x1C94DA58FE5737FD),
    (2528, 2529, 0x3FB6CDBAD7BA0655),
    (2530, 2530, 0xE637FFEA705450E5),
    (2531, 2532, 0x1C94DA58FE5737FD),
    (2533, 2534, 0x3FB6CDBAD7BA0655),
    (2535, 2537, 0x1C94DA58FE5737FD),
    (2538, 2560, 0x3FB6CDBAD7BA0655),
]

BOARD_BLUE = (0, 0, 121)
MAGENTA = (255, 85, 255)
WHITE = (255, 255, 255)
MUNCHER_REMAP = {
    (0, 228, 0): (231, 219, 0),
    (8, 132, 0): (134, 134, 0),
    (4, 112, 0): (113, 109, 0),
    (4, 88, 0): (89, 85, 0),
    (4, 64, 0): (65, 65, 0),
    (252, 252, 252): WHITE,
}


def pixel_hash(image: Image.Image) -> int:
    value = 1469598103934665603
    for red, green, blue in image.get_flattened_data():
        value ^= (red << 16) | (green << 8) | blue
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def frame_path(frames: Path, number: int) -> Path:
    return frames / f"frame-{number}.png"


def load_region(frames: Path, number: int, region: tuple[int, int, int, int]) -> Image.Image:
    image = Image.open(frame_path(frames, number)).convert("RGB")
    if image.size != (320, 200):
        raise ValueError(f"frame {number} is {image.size}, expected logical 320x200")
    return image.crop(region)


def stable_runs(values: Iterable[tuple[int, int]]) -> list[tuple[int, int, int]]:
    iterator = iter(values)
    start, previous_hash = next(iterator)
    previous_frame = start
    result: list[tuple[int, int, int]] = []
    for number, current_hash in iterator:
        if current_hash != previous_hash:
            result.append((start, previous_frame, previous_hash))
            start = number
            previous_hash = current_hash
        previous_frame = number
    result.append((start, previous_frame, previous_hash))
    return result


def json_runs(runs: Iterable[tuple[int, int, int]]) -> list[dict[str, object]]:
    return [
        {"first": first, "last": last, "frames": last - first + 1, "hash": f"0x{value:016x}"}
        for first, last, value in runs
    ]


def composed_first_move(frames: Path, sprite_path: Path) -> Image.Image:
    """Reconstruct the complete x=176/frame-5 composition.

    Live frame 2465 catches the DOS dirty presenter partway through its first
    refresh. This model uses the undamaged standing board, the ripped sprite,
    and the recovered opaque text order to distinguish that partial refresh
    from an intentional logical animation frame.
    """

    standing = load_region(frames, 2440, MOVEMENT_UNION)
    result = Image.new("RGB", (96, 30), BOARD_BLUE)
    output = result.load()
    captured = standing.load()

    for x in range(96):
        output[x, 0] = MAGENTA
    for y in range(30):
        output[0, y] = MAGENTA
        output[48, y] = MAGENTA

    for x in (*range(1, 10), *range(39, 48)):
        output[x, 1] = WHITE
        output[x, 29] = WHITE
    for y in (*range(1, 10), *range(21, 30)):
        output[1, y] = WHITE
        output[47, y] = WHITE

    # Cell 22 is not safe, so its white pixels in the standing fixture are
    # exactly the four-letter destination payload.
    for y in range(1, 30):
        for x in range(49, 96):
            if captured[x, y] == WHITE:
                output[x, y] = WHITE

    sprite = Image.open(sprite_path).convert("RGB")
    if sprite.size != (45, 30):
        raise ValueError(f"frame-5 sprite is {sprite.size}, expected 45x30")
    for source_y in range(sprite.height):
        for source_x in range(sprite.width):
            source = sprite.getpixel((source_x, source_y))
            if source == (0, 0, 0):
                continue
            target_x = 12 + source_x
            target_y = 1 + source_y
            if target_x < 96 and target_y < 30:
                output[target_x, target_y] = MUNCHER_REMAP[source]

    # "said" is 32x8 and centered at x=236, so the original opaque GFT box
    # occupies union-local (56,12)..(87,19).
    for y in range(12, 20):
        for x in range(56, 88):
            output[x, y] = BOARD_BLUE
    for y in range(1, 30):
        for x in range(49, 96):
            if captured[x, y] == WHITE:
                output[x, y] = WHITE

    # Grid and safe corners are the last foreground records.
    for x in range(96):
        output[x, 0] = MAGENTA
    for y in range(30):
        output[0, y] = MAGENTA
        output[48, y] = MAGENTA
    for x in (*range(1, 10), *range(39, 48)):
        output[x, 1] = WHITE
        output[x, 29] = WHITE
    for y in (*range(1, 10), *range(21, 30)):
        output[1, y] = WHITE
        output[47, y] = WHITE
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--frames",
        type=Path,
        default=Path("analysis/word-live/gameplay/chew-sequence"),
    )
    parser.add_argument(
        "--sprite",
        type=Path,
        default=Path("assets/word/ripped/vga/frames/01006/005.png"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("analysis/word-live/gameplay/report.json"),
    )
    args = parser.parse_args()

    missing = [
        str(frame_path(args.frames, number))
        for number in range(FIRST_FRAME, LAST_FRAME + 1)
        if not frame_path(args.frames, number).is_file()
    ]
    if missing:
        raise FileNotFoundError(f"missing {len(missing)} capture frames; first is {missing[0]}")

    union_hashes = [
        (number, pixel_hash(load_region(args.frames, number, MOVEMENT_UNION)))
        for number in range(FIRST_FRAME, LAST_FRAME + 1)
    ]
    chew_hashes = [
        (number, pixel_hash(load_region(args.frames, number, CHEW_CELL)))
        for number in range(FIRST_FRAME, LAST_FRAME + 1)
    ]
    union_runs = stable_runs(union_hashes)
    chew_runs = stable_runs(chew_hashes)
    chew_excerpt = [run for run in chew_runs if run[1] >= 2523]

    composed = composed_first_move(args.frames, args.sprite)
    partial = load_region(args.frames, 2465, MOVEMENT_UNION)
    pair_counts: Counter[tuple[tuple[int, int, int], tuple[int, int, int]]] = Counter()
    damage_pixels = 0
    for logical, observed in zip(
        composed.get_flattened_data(), partial.get_flattened_data()
    ):
        if logical != observed:
            damage_pixels += 1
            pair_counts[(logical, observed)] += 1

    checks = {
        "complete_union_runs_match": union_runs == EXPECTED_UNION_RUNS,
        "chew_runs_match": chew_excerpt == EXPECTED_CHEW_RUNS,
        "composed_phase_1_hash_matches": pixel_hash(composed) == 0x9198CD622D02E095,
        "phase_1_partial_refresh_is_22_pixels": damage_pixels == 22,
        "phase_1_partial_refresh_only_erases_foreground": pair_counts
        == Counter({(WHITE, BOARD_BLUE): 20, (MAGENTA, BOARD_BLUE): 2}),
        "chew_torn_frame_is_unique": sum(
            1 for _, value in chew_hashes if value == 0xE637FFEA705450E5
        )
        == 1,
    }

    report = {
        "valid": all(checks.values()),
        "capture": {
            "frames_directory": args.frames.as_posix(),
            "first_frame": FIRST_FRAME,
            "last_frame": LAST_FRAME,
            "frame_count": LAST_FRAME - FIRST_FRAME + 1,
            "logical_resolution": [320, 200],
        },
        "regions": {
            "movement_union_xyxy": list(MOVEMENT_UNION),
            "chew_cell_xyxy": list(CHEW_CELL),
        },
        "checks": checks,
        "movement_and_chew_union_runs": json_runs(union_runs),
        "chew_cell_runs": json_runs(chew_excerpt),
        "partial_presenter_frames": [
            {
                "frame": 2465,
                "kind": "first-move partial dirty-cell refresh",
                "observed_hash": "0xe4f24d098e7707a1",
                "complete_logical_hash": f"0x{pixel_hash(composed):016x}",
                "damaged_pixels": damage_pixels,
                "damage_pairs": [
                    {
                        "logical_rgb": list(logical),
                        "observed_rgb": list(observed),
                        "pixels": count,
                    }
                    for (logical, observed), count in sorted(pair_counts.items())
                ],
            },
            {
                "frame": 2530,
                "kind": "mid-chew partial sprite update",
                "observed_cell_hash": "0xe637ffea705450e5",
                "stable_closed_hash": "0x3fb6cdbad7ba0655",
                "stable_open_hash": "0x1c94da58fe5737fd",
            },
        ],
        "stable_chew_hashes": {
            "closed": "0x3fb6cdbad7ba0655",
            "open": "0x1c94da58fe5737fd",
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {args.output} ({'valid' if report['valid'] else 'FAILED'})")
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
