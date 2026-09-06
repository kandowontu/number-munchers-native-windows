#!/usr/bin/env python3
"""Extract MECC's typed RES containers used by Number Munchers.

The format begins with the data offset and a zero-based count of resource
types.  Each type descriptor contains a four-character tag, a zero-based
entry count, and the absolute offset of its entry table.  Entries are
little-endian (id, payload offset, payload size, flags) uint32 tuples.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


EXTENSIONS = {
    "PCXF": ".pcx",
    "BTMP": ".btmp",
    "CONF": ".conf",
    "DATA": ".data",
    "GSND": ".gsnd",
    "PSND": ".psnd",
    "ADLI": ".adli",
    "SCPT": ".scpt",
}


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def extract(source: Path, output: Path) -> dict:
    data = source.read_bytes()
    data_offset = u16(data, 0)
    type_count = u16(data, 2) + 1
    types = []
    output.mkdir(parents=True, exist_ok=True)

    for index in range(type_count):
        descriptor = 4 + index * 8
        tag = data[descriptor : descriptor + 4].decode("ascii")
        entry_count = u16(data, descriptor + 4) + 1
        table_offset = u16(data, descriptor + 6)
        extension = EXTENSIONS.get(tag, ".bin")
        type_dir = output / tag
        type_dir.mkdir(exist_ok=True)
        entries = []

        for entry_index in range(entry_count):
            record_offset = table_offset + entry_index * 16
            resource_id = u32(data, record_offset)
            payload_offset = u32(data, record_offset + 4)
            payload_size = u32(data, record_offset + 8)
            flags = u32(data, record_offset + 12)
            end = payload_offset + payload_size
            if payload_offset < data_offset or end > len(data):
                raise ValueError(
                    f"invalid {tag}:{resource_id} range "
                    f"{payload_offset:#x}..{end:#x} in {source.name}"
                )
            payload = data[payload_offset:end]
            filename = f"{resource_id:05d}{extension}"
            target = type_dir / filename
            target.write_bytes(payload)
            entries.append(
                {
                    "id": resource_id,
                    "offset": payload_offset,
                    "size": payload_size,
                    "flags": flags,
                    "sha256": hashlib.sha256(payload).hexdigest(),
                    "file": target.relative_to(output).as_posix(),
                }
            )

        types.append(
            {
                "tag": tag,
                "entry_count": entry_count,
                "table_offset": table_offset,
                "entries": entries,
            }
        )

    report = {
        "source": str(source),
        "source_size": len(data),
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "data_offset": data_offset,
        "type_count": type_count,
        "types": types,
    }
    manifest = output / "manifest.json"
    manifest.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = extract(args.source, args.output)
    total = sum(item["entry_count"] for item in report["types"])
    summary = ", ".join(f'{item["tag"]}:{item["entry_count"]}' for item in report["types"])
    print(f"Extracted {total} resources ({summary}) from {args.source.name}")


if __name__ == "__main__":
    main()
