#!/usr/bin/env python3
"""Audit the preserved physical-pixel Word Options and Hall captures.

DOSBox-X was captured through PrintWindow at its DPI-aware physical size. The
640x400 game surface is a uniform 2x nearest-neighbor presentation of the
original 320x200 VGA framebuffer, letterboxed inside the client area. This
script verifies that geometry, reconstructs each logical frame, and hashes the
same 0xRRGGBB pixel integers used by the native renderer tests.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image


WINDOW_SIZE = (658, 553)
SURFACE_CROP = (9, 103, 649, 503)
LOGICAL_SIZE = (320, 200)
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1

PAGES = (
    ("options", "dpi-options-window.png", "options-live-320x200.png",
     0xCAF1504D701741DC),
    ("set_content", "dpi-set-content-window.png", "set-content-live-320x200.png",
     0x2C1F159C04211BBF),
    ("difficulty", "dpi-difficulty-window.png", "difficulty-live-320x200.png",
     0x9FCECCD8C9179A03),
    ("vowels", "dpi-vowels-window.png", "vowels-live-320x200.png",
     0xA57416C9F7E33637),
    ("vowels_help", "dpi-vowels-help-window.png", "vowels-help-live-320x200.png",
     0x33598E143043CB06),
    ("preview", "dpi-preview-window.png", "preview-live-320x200.png",
     0xB6B873DCD7AB0EC7),
    ("preview_dense", "dpi-preview-sound1-difficulty1-window.png",
     "preview-sound1-difficulty1-live-320x200.png", 0x0C2B7FB6CAC342BA),
    ("hall_eraser", "dpi-hall-eraser-window.png", "hall-eraser-live-320x200.png",
     0xCDA041489B7A9B2B),
    ("hall", "dpi-hall-window.png", "hall-live-320x200.png",
     0x04616F92E6DFA290),
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
    if surface.size != (640, 400):
        raise AssertionError(f"invalid surface crop {surface.size}")

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
        default=Path("analysis/word-live/options"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("analysis/word-live/options/report.json"),
    )
    args = parser.parse_args()

    page_reports: list[dict[str, object]] = []
    for page, window_name, logical_name, expected_hash in PAGES:
        window_path = args.capture_directory / window_name
        reference_path = args.capture_directory / logical_name
        if not window_path.is_file():
            raise FileNotFoundError(window_path)
        if not reference_path.is_file():
            raise FileNotFoundError(reference_path)

        logical, nonuniform_blocks = logical_frame(window_path)
        with Image.open(reference_path) as source:
            reference = source.convert("RGB")
        if reference.size != LOGICAL_SIZE:
            raise ValueError(
                f"{reference_path}: expected {LOGICAL_SIZE}, got {reference.size}"
            )
        captured_hash = pixel_hash(logical)
        reference_hash = pixel_hash(reference)
        pixels_match_reference = (
            list(logical.get_flattened_data()) ==
            list(reference.get_flattened_data())
        )
        page_reports.append(
            {
                "page": page,
                "physical_window": window_path.as_posix(),
                "logical_reference": reference_path.as_posix(),
                "nonuniform_2x2_blocks": nonuniform_blocks,
                "hash": f"0x{captured_hash:016x}",
                "expected_native_hash": f"0x{expected_hash:016x}",
                "reference_hash": f"0x{reference_hash:016x}",
                "pixels_match_reference": pixels_match_reference,
                "matches_native": captured_hash == expected_hash,
                "valid": (
                    nonuniform_blocks == 0
                    and pixels_match_reference
                    and captured_hash == expected_hash
                ),
            }
        )

    report = {
        "valid": all(bool(page["valid"]) for page in page_reports),
        "capture": {
            "physical_window_size": list(WINDOW_SIZE),
            "logical_surface_crop_xyxy": list(SURFACE_CROP),
            "surface_size": [640, 400],
            "logical_resolution": list(LOGICAL_SIZE),
            "scale": 2,
            "hash": "FNV-1a-64 over one 0xRRGGBB integer per logical pixel",
        },
        "pages": page_reports,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
