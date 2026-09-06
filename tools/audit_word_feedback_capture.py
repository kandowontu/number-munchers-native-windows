#!/usr/bin/env python3
"""Audit complete live Word feedback, admission, Hall, and replay captures.

The source PNGs are DPI-aware 658x553 DOSBox-X PrintWindow captures. Their
640x400 game surfaces are exact uniform 2x presentations of the 320x200 VGA
framebuffer. Hashes use renderer-style 0xRRGGBB pixels so they can be asserted
directly in the native C++ tests.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path

from PIL import Image


WINDOW_SIZE = (658, 553)
SURFACE_CROP = (9, 103, 649, 503)
LOGICAL_SIZE = (320, 200)
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1


@dataclass(frozen=True)
class Capture:
    page: str
    window_name: str
    logical_name: str
    region: tuple[int, int, int, int]
    expected_hash: int


CAPTURES = (
    Capture(
        "wrong_feedback",
        "wrong-feedback-full-window.png",
        "wrong-feedback-live-320x200.png",
        (20, 86, 309, 117),
        0x59CEA3CABAB4E368,
    ),
    Capture(
        "collision_feedback",
        "collision-terminal-window.png",
        "collision-feedback-live-320x200.png",
        (20, 86, 309, 117),
        0x9D7E227A62F941CC,
    ),
    Capture(
        "name_entry_blank",
        "name-entry-window.png",
        "name-entry-live-320x200.png",
        (0, 45, 320, 155),
        0x05EEC9C3F1EF5EC3,
    ),
    Capture(
        "name_entry_typed",
        "name-entry-typed2-window.png",
        "name-entry-typed2-live-320x200.png",
        (0, 45, 320, 155),
        0x285A173C1222B63D,
    ),
    Capture(
        "hall_admission",
        "post-admission-hall-window.png",
        "post-admission-hall-live-320x200.png",
        (0, 0, 320, 200),
        0x4C3FDA7428CB5224,
    ),
    Capture(
        "replay_yes",
        "replay-question-window.png",
        "replay-question-live-320x200.png",
        (0, 0, 320, 200),
        0xDFF55D7DFE18637E,
    ),
    Capture(
        "replay_no",
        "replay-no-window.png",
        "replay-no-live-320x200.png",
        (0, 0, 320, 200),
        0x4E398CFE7456FD82,
    ),
    Capture(
        "replay_return_title",
        "replay-no-return-window.png",
        "replay-no-return-live-320x200.png",
        (0, 0, 320, 200),
        0xD0862A8EBEBC6146,
    ),
)


def pixel_hash(image: Image.Image) -> int:
    value = FNV_OFFSET
    for red, green, blue in image.get_flattened_data():
        value ^= (red << 16) | (green << 8) | blue
        value = (value * FNV_PRIME) & MASK64
    return value


def logical_frame(window_path: Path) -> tuple[Image.Image, int]:
    with Image.open(window_path) as source:
        window = source.convert("RGB")
    if window.size != WINDOW_SIZE:
        raise ValueError(
            f"{window_path}: expected physical window {WINDOW_SIZE}, got {window.size}"
        )
    surface = window.crop(SURFACE_CROP)
    source_pixels = surface.load()
    logical = Image.new("RGB", LOGICAL_SIZE)
    target_pixels = logical.load()
    nonuniform_blocks = 0
    for y in range(LOGICAL_SIZE[1]):
        for x in range(LOGICAL_SIZE[0]):
            block = {
                source_pixels[x * 2, y * 2],
                source_pixels[x * 2 + 1, y * 2],
                source_pixels[x * 2, y * 2 + 1],
                source_pixels[x * 2 + 1, y * 2 + 1],
            }
            if len(block) != 1:
                nonuniform_blocks += 1
            target_pixels[x, y] = source_pixels[x * 2, y * 2]
    return logical, nonuniform_blocks


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--capture-directory",
        type=Path,
        default=Path("analysis/word-live/feedback"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("analysis/word-live/feedback/report.json"),
    )
    args = parser.parse_args()

    reports: list[dict[str, object]] = []
    for capture in CAPTURES:
        window_path = args.capture_directory / capture.window_name
        reference_path = args.capture_directory / capture.logical_name
        if not window_path.is_file():
            raise FileNotFoundError(window_path)
        if not reference_path.is_file():
            raise FileNotFoundError(reference_path)

        reconstructed, nonuniform = logical_frame(window_path)
        with Image.open(reference_path) as source:
            reference = source.convert("RGB")
        if reference.size != LOGICAL_SIZE:
            raise ValueError(
                f"{reference_path}: expected logical frame {LOGICAL_SIZE}, got {reference.size}"
            )

        pixels_match = (
            list(reconstructed.get_flattened_data())
            == list(reference.get_flattened_data())
        )
        observed_hash = pixel_hash(reconstructed.crop(capture.region))
        valid = (
            nonuniform == 0
            and pixels_match
            and observed_hash == capture.expected_hash
        )
        reports.append(
            {
                "page": capture.page,
                "window": capture.window_name,
                "logical": capture.logical_name,
                "region": list(capture.region),
                "nonuniform_2x_blocks": nonuniform,
                "pixels_match_reference": pixels_match,
                "hash": f"0x{observed_hash:016x}",
                "expected_hash": f"0x{capture.expected_hash:016x}",
                "valid": valid,
            }
        )

    report = {
        "window_size": list(WINDOW_SIZE),
        "surface_crop": list(SURFACE_CROP),
        "logical_size": list(LOGICAL_SIZE),
        "captures": reports,
        "valid": all(item["valid"] for item in reports),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
