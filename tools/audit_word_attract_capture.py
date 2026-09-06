#!/usr/bin/env python3
"""Inventory the short live Word Demo interval preserved in wm_009.avi."""

from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
FRAME_DIR = ROOT / "analysis" / "word-live" / "attract" / "wm009-exact"
REPORT_PATH = ROOT / "analysis" / "word-live" / "attract" / "wm009-report.json"
NATIVE_PATH = ROOT / "analysis" / "word-live" / "attract" / "attract-wm009-board-native.png"
EXPECTED_FRAME_COUNT = 140
BOARD_BLUE = (0, 0, 121)
EXPECTED_BOARD_INDICES = list(range(43, 94))
STABLE_FIRST = 49
STABLE_LAST = 92
EXPECTED_STABLE_FULL_HASH = 0x6426D16846667DAC
EXPECTED_STABLE_HEADER_HASH = 0x3CDA3059237B16CC
EXPECTED_STABLE_BODY_HASH = 0x018F83DE58F12485
EXPECTED_STABLE_FOOTER_HASH = 0x15935ECD1FF9E561


def pixel_hash(image: Image.Image) -> int:
    # Match the historical renderer regression convention used by the C++
    # gates and the other live-capture auditors in this repository.
    value = 1469598103934665603
    for red, green, blue in image.get_flattened_data():
        value ^= (red << 16) | (green << 8) | blue
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def crop_hash(image: Image.Image, box: tuple[int, int, int, int]) -> int:
    return pixel_hash(image.crop(box))


def runs(values: list[int]) -> list[dict[str, int | str]]:
    result: list[dict[str, int | str]] = []
    first = 0
    for index in range(1, len(values) + 1):
        if index == len(values) or values[index] != values[first]:
            result.append(
                {
                    "first": first,
                    "last": index - 1,
                    "frames": index - first,
                    "hash": f"0x{values[first]:016x}",
                }
            )
            first = index
    return result


def compare_region(reference: Image.Image, native: Image.Image,
                   box: tuple[int, int, int, int]) -> dict[str, object]:
    reference_pixels = list(reference.crop(box).get_flattened_data())
    native_pixels = list(native.crop(box).get_flattened_data())
    pairs = Counter(
        (source, target)
        for source, target in zip(reference_pixels, native_pixels)
        if source != target
    )
    width = box[2] - box[0]
    coordinates = [
        [box[0] + index % width, box[1] + index // width]
        for index, (source, target) in enumerate(zip(reference_pixels, native_pixels))
        if source != target
    ]
    samples = [
        {
            "coordinate": [box[0] + index % width, box[1] + index // width],
            "reference": list(source),
            "native": list(target),
        }
        for index, (source, target) in enumerate(zip(reference_pixels, native_pixels))
        if source != target
    ]
    return {
        "region": list(box),
        "mismatched_pixels": sum(pairs.values()),
        "mismatch_coordinates": coordinates[:40],
        "mismatch_samples": samples[:40],
        "most_common_mismatches": [
            {"reference": list(source), "native": list(target), "pixels": count}
            for (source, target), count in pairs.most_common(12)
        ],
    }


paths = sorted(FRAME_DIR.glob("frame-*.png"))
images = [Image.open(path).convert("RGB") for path in paths]
if not images:
    raise SystemExit(f"No frames in {FRAME_DIR}")

sizes_match = all(image.size == (320, 200) for image in images)
board_indices = [
    index
    for index, image in enumerate(images)
    if image.getpixel((0, 0)) == BOARD_BLUE
    and sum(1 for pixel in image.get_flattened_data() if pixel == BOARD_BLUE) > 30_000
]

board_full_hashes = [pixel_hash(images[index]) for index in board_indices]
board_header_hashes = [crop_hash(images[index], (0, 0, 320, 24)) for index in board_indices]
board_body_hashes = [crop_hash(images[index], (0, 24, 320, 179)) for index in board_indices]
board_footer_hashes = [crop_hash(images[index], (0, 179, 320, 200)) for index in board_indices]

report = {
    "source": "analysis/captures/wm_009.avi",
    "decoded_segment_seconds": [25.5, 27.5],
    "frame_rate_hz": 70.086304,
    "logical_size": [320, 200],
    "frame_count": len(images),
    "sizes_match": sizes_match,
    "demo_board_indices": board_indices,
    "stable_demo_board": {
        "first": STABLE_FIRST,
        "last": STABLE_LAST,
        "frames": STABLE_LAST - STABLE_FIRST + 1,
        "full_hash_with_target_exemplar":
            f"0x{pixel_hash(images[STABLE_FIRST]):016x}",
        "header_hash":
            f"0x{crop_hash(images[STABLE_FIRST], (0, 0, 320, 24)):016x}",
        "body_hash":
            f"0x{crop_hash(images[STABLE_FIRST], (0, 24, 320, 179)):016x}",
        "footer_hash":
            f"0x{crop_hash(images[STABLE_FIRST], (0, 179, 320, 200)):016x}",
    },
    "demo_board_full_runs": runs(board_full_hashes),
    "demo_board_header_runs": runs(board_header_hashes),
    "demo_board_body_runs": runs(board_body_hashes),
    "demo_board_footer_runs": runs(board_footer_hashes),
    "frame_count_matches": len(images) == EXPECTED_FRAME_COUNT,
}
if NATIVE_PATH.exists():
    native = Image.open(NATIVE_PATH).convert("RGB")
    reference = images[49]
    report["native_comparison"] = {
        "reference_frame": paths[49].name,
        "native": NATIVE_PATH.relative_to(ROOT).as_posix(),
        "full_frame": compare_region(reference, native, (0, 0, 320, 200)),
        "header": compare_region(reference, native, (0, 0, 320, 24)),
        "body": compare_region(reference, native, (0, 24, 320, 179)),
        "footer": compare_region(reference, native, (0, 179, 320, 200)),
    }
native_comparison = report.get("native_comparison", {})
native_regions_match = bool(native_comparison) and all(
    native_comparison[name]["mismatched_pixels"] == 0
    for name in ("full_frame", "header", "body", "footer")
)
stable_hashes_match = (
    pixel_hash(images[STABLE_FIRST]) == EXPECTED_STABLE_FULL_HASH
    and crop_hash(images[STABLE_FIRST], (0, 0, 320, 24))
        == EXPECTED_STABLE_HEADER_HASH
    and crop_hash(images[STABLE_FIRST], (0, 24, 320, 179))
        == EXPECTED_STABLE_BODY_HASH
    and crop_hash(images[STABLE_FIRST], (0, 179, 320, 200))
        == EXPECTED_STABLE_FOOTER_HASH
    and all(
        pixel_hash(images[index]) == EXPECTED_STABLE_FULL_HASH
        for index in range(STABLE_FIRST, STABLE_LAST + 1)
    )
)
report["board_indices_match"] = board_indices == EXPECTED_BOARD_INDICES
report["stable_hashes_match"] = stable_hashes_match
report["native_regions_match"] = native_regions_match
report["valid"] = (
    sizes_match
    and len(images) == EXPECTED_FRAME_COUNT
    and report["board_indices_match"]
    and stable_hashes_match
    and native_regions_match
)
REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
REPORT_PATH.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(json.dumps(report, indent=2))
