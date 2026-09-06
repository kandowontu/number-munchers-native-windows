#!/usr/bin/env python3
"""Audit the second muted Word Options/persistence capture batch.

Each PrintWindow image contains a uniform 640x400 nearest-neighbor surface in
the DPI-aware 658x553 DOSBox-X window.  This script reconstructs 320x200,
compares every pixel with the native PPM fixtures dumped by
MunchersAppTests.exe, and records the renderer-style FNV-64 hash.
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
    ("set_password", "03-set-password.png", "set-password-live-native.ppm",
     0xE0D28B7D3E121749),
    ("password_gate", "05-password-gate.png", "password-gate-live-native.ppm",
     0xF77F4595E849048F),
    ("password_rejected", "06-password-rejected.png",
     "password-rejected-live-native.ppm", 0x00694BB174F63F0C),
    ("calibration_attach", "07-calibration-attach.png",
     "calibration-attach-live-native.ppm", 0x761C0D49D5D02FA6),
    ("vowels_none", "08-vowels-none-validation.png",
     "vowels-none-live-native.ppm", 0x1853B48FF3866660),
    ("vowels_group2", "09-vowels-group2-validation.png",
     "vowels-group2-live-native.ppm", 0x00476E702721659C),
    ("vowels_group3", "10-vowels-group3-validation.png",
     "vowels-group3-live-native.ppm", 0x8D3FC297B93BD8AC),
    ("insufficient_targets", "11-insufficient-targets-warning.png",
     "insufficient-targets-live-native.ppm", 0x607C6B32D1A263C9),
    ("initially_empty_hall", "12-empty-hall.png",
     "hall-initially-empty-live-native.ppm", 0x0BFC01284BFE5E29),
    ("configuration_write_error", "13-write-error.png",
     "write-error-live-native.ppm", 0x1A6EBBC842249539),
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
    logical = Image.new("RGB", LOGICAL_SIZE)
    source_pixels = surface.load()
    target_pixels = logical.load()
    nonuniform = 0
    for y in range(LOGICAL_SIZE[1]):
        for x in range(LOGICAL_SIZE[0]):
            block = {
                source_pixels[x * 2, y * 2],
                source_pixels[x * 2 + 1, y * 2],
                source_pixels[x * 2, y * 2 + 1],
                source_pixels[x * 2 + 1, y * 2 + 1],
            }
            nonuniform += len(block) != 1
            target_pixels[x, y] = source_pixels[x * 2, y * 2]
    return logical, nonuniform


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--directory", type=Path,
        default=Path("analysis/word-live/options-gaps"),
    )
    parser.add_argument(
        "--output", type=Path,
        default=Path("analysis/word-live/options-gaps/report.json"),
    )
    args = parser.parse_args()

    pages: list[dict[str, object]] = []
    for page, live_name, native_name, expected_hash in PAGES:
        live_path = args.directory / live_name
        native_path = args.directory / native_name
        if not live_path.is_file():
            raise FileNotFoundError(live_path)
        if not native_path.is_file():
            raise FileNotFoundError(native_path)

        logical, nonuniform = logical_frame(live_path)
        with Image.open(native_path) as source:
            native = source.convert("RGB")
        if native.size != LOGICAL_SIZE:
            raise ValueError(f"{native_path}: expected {LOGICAL_SIZE}, got {native.size}")

        live_pixels = list(logical.get_flattened_data())
        native_pixels = list(native.get_flattened_data())
        mismatch_count = sum(left != right for left, right in zip(live_pixels, native_pixels))
        live_hash = pixel_hash(logical)
        native_hash = pixel_hash(native)
        logical_path = args.directory / f"{Path(live_name).stem}-320x200.png"
        logical.save(logical_path)
        valid = (
            nonuniform == 0
            and mismatch_count == 0
            and live_hash == expected_hash
            and native_hash == expected_hash
        )
        pages.append({
            "page": page,
            "physical_window": live_path.as_posix(),
            "logical_original": logical_path.as_posix(),
            "native_fixture": native_path.as_posix(),
            "nonuniform_2x2_blocks": nonuniform,
            "mismatched_pixels": mismatch_count,
            "original_hash": f"0x{live_hash:016x}",
            "native_hash": f"0x{native_hash:016x}",
            "expected_hash": f"0x{expected_hash:016x}",
            "valid": valid,
        })

    report = {
        "valid": all(bool(page["valid"]) for page in pages),
        "capture": {
            "physical_window_size": list(WINDOW_SIZE),
            "logical_surface_crop_xyxy": list(SURFACE_CROP),
            "logical_resolution": list(LOGICAL_SIZE),
            "scale": 2,
            "audio": "muted (DOSBox-X mixer nosound=true)",
        },
        "pages": pages,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
