#!/usr/bin/env python3
"""Build isolated WM.EXE control variants for original-scene capture.

The variants change only the path used to reach a cartoon: the first nonblank
chew is accepted, the board-live scan reports completion, the first completion
takes the cartoon branch, and one scene index is pushed directly.  Scene code,
resources, presentation, timers, and audio dispatch remain byte-identical to
the preserved executable.  The source tree is never modified.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import shutil


SOURCE_SHA256 = "EF9C02D87E994E4874C698F553574812A2EA8E9BBCB20220F3665AF4DA00F728"
MZ_HEADER_BYTES = 10_752

# (unpacked image offset, exact original bytes, replacement bytes)
COMMON_PATCHES = (
    (0x09FAB, bytes.fromhex("7E18"), bytes.fromhex("9090")),
    (0x0AD67, bytes.fromhex("7E04"), bytes.fromhex("EB04")),
    (0x0FE01, bytes.fromhex("7507"), bytes.fromhex("9090")),
)
SELECTOR_OFFSET = 0x0FE6F
SELECTOR_ORIGINAL = bytes.fromhex("8B1E921FD1E3FFB77263")


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
        "--source-directory", type=Path,
        default=Path("extracted/word-munchers/WM"),
    )
    parser.add_argument(
        "--output-directory", type=Path,
        default=Path("analysis/word-live/scenes/reference-control"),
    )
    args = parser.parse_args()

    workspace = Path.cwd().resolve()
    output_directory = args.output_directory.resolve()
    if output_directory == workspace or not output_directory.is_relative_to(workspace):
        raise ValueError("output directory must be a child of the current workspace")

    source_exe = args.source_directory / "WM.EXE"
    source = source_exe.read_bytes()
    digest = hashlib.sha256(source).hexdigest().upper()
    if digest != SOURCE_SHA256:
        raise ValueError(f"unexpected source WM.EXE SHA-256: {digest}")

    output_directory.mkdir(parents=True, exist_ok=True)
    for scene_index in range(6):
        destination = output_directory / f"scene-{scene_index}"
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(args.source_directory, destination)

        variant = bytearray(source)
        for image_offset, expected, replacement in COMMON_PATCHES:
            patch_at(variant, image_offset, expected, replacement)
        selector_replacement = bytes((0xB8, scene_index, 0x00, 0x50)) + bytes((0x90,) * 6)
        patch_at(variant, SELECTOR_OFFSET, SELECTOR_ORIGINAL, selector_replacement)
        (destination / "WM.EXE").write_bytes(variant)

        variant_digest = hashlib.sha256(variant).hexdigest().upper()
        print(f"scene {scene_index}: {destination / 'WM.EXE'} {variant_digest}")

    # The source is checked again so a concurrent or accidental mutation is
    # surfaced immediately rather than hidden by valid derived outputs.
    final_digest = hashlib.sha256(source_exe.read_bytes()).hexdigest().upper()
    if final_digest != SOURCE_SHA256:
        raise ValueError(f"source WM.EXE changed during derivation: {final_digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
