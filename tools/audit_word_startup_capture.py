#!/usr/bin/env python3
"""Hash and time the preserved lossless Word Munchers startup captures."""

from __future__ import annotations

import argparse
import json
from fractions import Fraction
from pathlib import Path

from PIL import Image


WIDTH = 320
HEIGHT = 200
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1
CAPTURE_RATE = Fraction(2_190_197, 31_250)
EXPECTED = {
    "version": 0x0F20A0F87C2B1BB8,
    "splash": 0x4CD75EB6D6FEA514,
    "title": 0xD0862A8EBEBC6146,
    "instructions_question": 0x070B08AA80400C33,
}
EXPECTED_INFORMATION = [
    0x19132D0AD87AD94F,
    0x1FEA2ED3CFF0ADF0,
    0xF7FE41A965EB0A00,
    0x980EAFE6EFBCE961,
    0xBD3EC3A29D500521,
    0x061D6E48D17E1F79,
]
EXPECTED_PREGAME = [
    0x0C5EE9E438C025A9,
    0xFCFE7362EB6EA8BE,
    0xD48C0F88E905A11A,
    0x7AD219A332BA7FCC,
    0x0239185C800A79DD,
]


def logical_pixels(path: Path) -> list[tuple[int, int, int]]:
    with Image.open(path) as source:
        image = source.convert("RGB")
        if image.size == (WIDTH * 2, HEIGHT * 2):
            image = image.resize((WIDTH, HEIGHT), Image.Resampling.NEAREST)
        if image.size != (WIDTH, HEIGHT):
            raise ValueError(f"{path}: expected 320x200 or 640x400, got {image.size}")
        return list(image.get_flattened_data())


def frame_hash(path: Path) -> int:
    value = FNV_OFFSET
    for red, green, blue in logical_pixels(path):
        value ^= (red << 16) | (green << 8) | blue
        value = (value * FNV_PRIME) & MASK64
    return value


def seconds_for_frame(frame: int) -> float:
    return float(Fraction(frame - 1, 1) / CAPTURE_RATE)


def scan_sequence(directory: Path) -> dict[str, object]:
    paths = sorted(directory.glob("*.png"))
    if not paths:
        raise ValueError(f"{directory}: no PNG frames")

    hashes = [frame_hash(path) for path in paths]
    runs: list[dict[str, object]] = []
    start = 0
    for index in range(1, len(hashes) + 1):
        if index == len(hashes) or hashes[index] != hashes[start]:
            hash_value = hashes[start]
            labels = [name for name, expected in EXPECTED.items() if expected == hash_value]
            runs.append(
                {
                    "start_frame": start + 1,
                    "end_frame": index,
                    "frame_count": index - start,
                    "start_seconds": round(seconds_for_frame(start + 1), 6),
                    "end_seconds": round(seconds_for_frame(index), 6),
                    "hash": f"0x{hash_value:016x}",
                    "expected_page": labels[0] if labels else None,
                }
            )
            start = index

    return {
        "directory": str(directory),
        "frame_count": len(paths),
        "capture_rate": f"{CAPTURE_RATE.numerator}/{CAPTURE_RATE.denominator}",
        "capture_rate_decimal": round(float(CAPTURE_RATE), 6),
        "runs": runs,
    }


def audit_pages(directory: Path, pattern: str, expected: list[int]) -> list[dict[str, object]]:
    paths = sorted(directory.glob(pattern))
    if len(paths) != len(expected):
        raise ValueError(
            f"{directory}/{pattern}: expected {len(expected)} pages, found {len(paths)}"
        )
    pages = []
    for index, (path, expected_hash) in enumerate(zip(paths, expected), start=1):
        value = frame_hash(path)
        pages.append(
            {
                "page": index,
                "path": str(path),
                "hash": f"0x{value:016x}",
                "expected_native_hash": f"0x{expected_hash:016x}",
                "matches_native": value == expected_hash,
            }
        )
    return pages


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--unkeyed", type=Path, required=True)
    parser.add_argument("--keyed", type=Path, required=True)
    parser.add_argument("--title", type=Path, required=True)
    parser.add_argument("--information-dir", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    title_hash = frame_hash(args.title)
    report = {
        "logical_resolution": [WIDTH, HEIGHT],
        "hash": "FNV-1a-64 over one 0xRRGGBB integer per logical pixel",
        "expected_hashes": {name: f"0x{value:016x}" for name, value in EXPECTED.items()},
        "unkeyed": scan_sequence(args.unkeyed),
        "keyed_after_about_one_second": scan_sequence(args.keyed),
        "title": {
            "path": str(args.title),
            "hash": f"0x{title_hash:016x}",
            "matches_expected": title_hash == EXPECTED["title"],
        },
    }
    if args.information_dir:
        question = args.information_dir / "00-instructions-question-live-640x400.png"
        question_hash = frame_hash(question)
        report["information_and_instructions"] = {
            "question": {
                "path": str(question),
                "hash": f"0x{question_hash:016x}",
                "expected_native_hash":
                    f"0x{EXPECTED['instructions_question']:016x}",
                "matches_native": question_hash == EXPECTED["instructions_question"],
            },
            "information": audit_pages(
                args.information_dir, "*-information-live-640x400.png", EXPECTED_INFORMATION
            ),
            "pre_game": audit_pages(
                args.information_dir, "*-pregame-live-640x400.png", EXPECTED_PREGAME
            ),
        }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
