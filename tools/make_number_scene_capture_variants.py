#!/usr/bin/env python3
"""Build isolated NM.EXE control variants for live cartoon capture.

The preserved packed disk executable is never modified.  Variants start from
the byte-gated unpacked executable and change only the path used to reach a
cartoon: the first nonblank chew is accepted, the board-live scan reports
completion, the first completion takes the cartoon branch, and one shuffled
scene slot is pushed directly.  Scene code, resources, Wipe/painter, timers,
and audio dispatch remain byte-identical to the recovered program image.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import shutil


SOURCE_SHA256 = "DE42F666542C0EF84B6DD5F0FE49CFAA06EBB79CC05DAA5B9F0E74EBD9B994C3"
MZ_HEADER_BYTES = 11_776
COMMON_PATCHES = (
    (0x093DE, bytes.fromhex("7E18"), bytes.fromhex("9090")),
    (0x0A19A, bytes.fromhex("7E04"), bytes.fromhex("EB04")),
    (0x0F18E, bytes.fromhex("7507"), bytes.fromhex("9090")),
)
SELECTOR_OFFSET = 0x0F1E8
SELECTOR_ORIGINAL = bytes.fromhex("8B1E8E11D1E3FFB77A5F")


def patch_at(image: bytearray, image_offset: int, expected: bytes,
             replacement: bytes) -> None:
    if len(expected) != len(replacement):
        raise ValueError("in-place patches must retain their exact length")
    file_offset = MZ_HEADER_BYTES + image_offset
    actual = bytes(image[file_offset:file_offset + len(expected)])
    if actual != expected:
        raise ValueError(
            f"patch 0x{image_offset:05x}: expected {expected.hex()}, "
            f"found {actual.hex()}"
        )
    image[file_offset:file_offset + len(expected)] = replacement


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source-executable", type=Path,
        default=Path("analysis/NM-unpacked.exe"),
    )
    parser.add_argument(
        "--source-directory", type=Path,
        default=Path("extracted/disk/NMUNCH"),
    )
    parser.add_argument(
        "--output-directory", type=Path,
        default=Path("analysis/number-live/scenes/reference-control"),
    )
    arguments = parser.parse_args()

    workspace = Path.cwd().resolve()
    output_directory = arguments.output_directory.resolve()
    if output_directory == workspace or not output_directory.is_relative_to(workspace):
        raise ValueError("output directory must be a child of the current workspace")

    source = arguments.source_executable.read_bytes()
    digest = hashlib.sha256(source).hexdigest().upper()
    if digest != SOURCE_SHA256:
        raise ValueError(f"unexpected source NM-unpacked.exe SHA-256: {digest}")

    output_directory.mkdir(parents=True, exist_ok=True)
    for scene_index in range(5):
        destination = output_directory / f"scene-{scene_index}"
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(arguments.source_directory, destination)

        variant = bytearray(source)
        for image_offset, expected, replacement in COMMON_PATCHES:
            patch_at(variant, image_offset, expected, replacement)
        selector_replacement = bytes((0xB8, scene_index, 0x00, 0x50)) + bytes((0x90,) * 6)
        patch_at(variant, SELECTOR_OFFSET, SELECTOR_ORIGINAL, selector_replacement)
        (destination / "NM.EXE").write_bytes(variant)
        variant_digest = hashlib.sha256(variant).hexdigest().upper()
        print(f"scene {scene_index}: {destination / 'NM.EXE'} {variant_digest}")

    final_digest = hashlib.sha256(arguments.source_executable.read_bytes()).hexdigest().upper()
    if final_digest != SOURCE_SHA256:
        raise ValueError(f"source NM-unpacked.exe changed during derivation: {final_digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
