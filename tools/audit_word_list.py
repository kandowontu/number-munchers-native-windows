#!/usr/bin/env python3
"""Parse and validate the Word Munchers WLIST.BIN content table.

The file is an offset table followed by vowel-sound sections.  This tool is
deliberately lossless: it exposes the decoded word for convenience while also
retaining every six-byte source record and every byte after the first NUL.
Those trailing bytes have not yet been assigned semantics from executable
code, so the audit must not silently treat them as padding.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


SOUND_LABELS = [
    "/a/ as in cake",
    "/a/ as in hat",
    "/e/ as in tree",
    "/e/ as in bell",
    "/i/ as in kite",
    "/i/ as in fish",
    "/o/ as in boat",
    "/o/ as in fox",
    "/u/ as in mule",
    "/u/ as in duck",
    "/oo/ as in moon",
    "/oo/ as in book",
    "/ou/ as in mouse",
    "/au/ as in haunt",
    "/oi/ as in oil",
    "/ar/ as in car",
    "/air/ as in chair",
    "/eer/ as in deer",
    "/ir/ as in bird",
    "/or/ as in corn",
]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def parse(source: Path) -> dict:
    data = source.read_bytes()
    if len(data) < 8:
        raise ValueError("word list is too short to contain an offset table")

    table_bytes = u32(data, 0)
    if table_bytes < 8 or table_bytes % 4:
        raise ValueError(f"invalid first section offset {table_bytes:#x}")
    offset_count = table_bytes // 4
    if offset_count < 2 or table_bytes > len(data):
        raise ValueError("invalid word-list offset count")

    offsets = [u32(data, index * 4) for index in range(offset_count)]
    if offsets[0] != table_bytes:
        raise ValueError("first section does not begin after the offset table")
    if offsets[-1] != len(data):
        raise ValueError(
            f"final offset {offsets[-1]:#x} does not equal file size {len(data):#x}"
        )
    if any(left >= right for left, right in zip(offsets, offsets[1:])):
        raise ValueError("section offsets are not strictly increasing")

    sections = []
    trailing_histogram: dict[str, int] = {}
    total_records = 0
    for index, (start, end) in enumerate(zip(offsets, offsets[1:])):
        payload_size = end - start
        if payload_size < 8 or (payload_size - 8) % 6:
            raise ValueError(
                f"section {index} has invalid {payload_size}-byte payload"
            )

        difficulty_counts = list(data[start : start + 8])
        if difficulty_counts != sorted(difficulty_counts):
            raise ValueError(f"section {index} difficulty counts decrease")
        record_count = (payload_size - 8) // 6
        if not record_count or difficulty_counts[-1] != record_count - 1:
            raise ValueError(
                f"section {index} final difficulty count {difficulty_counts[-1]} does not "
                f"leave the required extra record at index {record_count - 1}"
            )

        records = []
        for record_index in range(record_count):
            record_offset = start + 8 + record_index * 6
            raw = data[record_offset : record_offset + 6]
            nul = raw.find(b"\0")
            if nul < 1:
                raise ValueError(
                    f"section {index} record {record_index} has no terminated word"
                )
            word = raw[:nul].decode("cp437")
            trailing = raw[nul + 1 :]
            for value in trailing:
                if value:
                    key = f"{value:02X}"
                    trailing_histogram[key] = trailing_histogram.get(key, 0) + 1
            records.append(
                {
                    "index": record_index,
                    "offset": record_offset,
                    "word": word,
                    "raw_hex": raw.hex(),
                    "trailing_hex": trailing.hex(),
                }
            )

        label = SOUND_LABELS[index] if index < len(SOUND_LABELS) else None
        sections.append(
            {
                "index": index,
                "label": label,
                "start": start,
                "end": end,
                "difficulty_counts": difficulty_counts,
                "record_count": record_count,
                "exemplar": records[0]["word"],
                "records": records,
            }
        )
        total_records += record_count

    return {
        "source": source.as_posix(),
        "source_size": len(data),
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "offset_count": offset_count,
        "section_count": len(sections),
        "labeled_sound_section_count": len(SOUND_LABELS),
        "record_count": total_records,
        "nonzero_trailing_byte_histogram_hex": dict(sorted(trailing_histogram.items())),
        "sections": sections,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    report = parse(args.source)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(
        f"Validated {report['record_count']} records in "
        f"{report['section_count']} sections from {args.source.name}"
    )
    print(
        "Nonzero trailing bytes: "
        + ", ".join(
            f"{key}={value}"
            for key, value in report["nonzero_trailing_byte_histogram_hex"].items()
        )
    )


if __name__ == "__main__":
    main()
