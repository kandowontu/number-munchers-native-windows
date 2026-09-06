#!/usr/bin/env python3
"""Read-only FAT12/16 extractor for a fixed VHD or raw disk image.

This intentionally implements only the small, well-understood subset needed for
the original Number Munchers image.  It never writes to the source image.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path


@dataclass
class Entry:
    path: str
    size: int
    sha256: str | None
    attributes: int
    first_cluster: int
    modified: str | None


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def dos_datetime(date_word: int, time_word: int) -> str | None:
    if not date_word:
        return None
    try:
        value = datetime(
            1980 + ((date_word >> 9) & 0x7F),
            (date_word >> 5) & 0x0F,
            date_word & 0x1F,
            (time_word >> 11) & 0x1F,
            (time_word >> 5) & 0x3F,
            (time_word & 0x1F) * 2,
        )
    except ValueError:
        return None
    return value.isoformat()


class FatVolume:
    def __init__(self, image: bytes, partition_offset: int):
        self.image = image
        self.base = partition_offset
        boot = image[self.base : self.base + 512]
        self.bytes_per_sector = u16(boot, 11)
        self.sectors_per_cluster = boot[13]
        self.reserved_sectors = u16(boot, 14)
        self.fat_count = boot[16]
        self.root_entries = u16(boot, 17)
        self.total_sectors = u16(boot, 19) or u32(boot, 32)
        self.sectors_per_fat = u16(boot, 22)
        if self.bytes_per_sector != 512:
            raise ValueError(f"unsupported sector size: {self.bytes_per_sector}")
        root_sectors = (self.root_entries * 32 + 511) // 512
        data_sectors = self.total_sectors - (
            self.reserved_sectors + self.fat_count * self.sectors_per_fat + root_sectors
        )
        cluster_count = data_sectors // self.sectors_per_cluster
        self.fat_bits = 12 if cluster_count < 4085 else 16 if cluster_count < 65525 else 32
        if self.fat_bits not in (12, 16):
            raise ValueError(f"unsupported FAT{self.fat_bits} volume")
        self.fat_offset = self.base + self.reserved_sectors * 512
        self.root_offset = self.fat_offset + self.fat_count * self.sectors_per_fat * 512
        self.root_size = self.root_entries * 32
        self.data_offset = self.root_offset + root_sectors * 512
        self.cluster_size = self.sectors_per_cluster * 512

    def next_cluster(self, cluster: int) -> int:
        fat = self.image[self.fat_offset : self.fat_offset + self.sectors_per_fat * 512]
        if self.fat_bits == 12:
            pos = cluster + cluster // 2
            word = u16(fat, pos)
            return (word >> 4) if cluster & 1 else (word & 0x0FFF)
        return u16(fat, cluster * 2)

    def is_end(self, cluster: int) -> bool:
        return cluster >= (0xFF8 if self.fat_bits == 12 else 0xFFF8)

    def read_chain(self, first_cluster: int, size: int | None = None) -> bytes:
        if first_cluster < 2:
            return b""
        chunks: list[bytes] = []
        seen: set[int] = set()
        cluster = first_cluster
        while not self.is_end(cluster):
            if cluster < 2 or cluster in seen:
                raise ValueError(f"invalid or cyclic cluster chain at {cluster}")
            seen.add(cluster)
            offset = self.data_offset + (cluster - 2) * self.cluster_size
            chunks.append(self.image[offset : offset + self.cluster_size])
            cluster = self.next_cluster(cluster)
        result = b"".join(chunks)
        return result if size is None else result[:size]

    @staticmethod
    def decode_name(record: bytes) -> str:
        stem = record[:8].decode("cp437", errors="replace").rstrip()
        extension = record[8:11].decode("cp437", errors="replace").rstrip()
        return stem + (("." + extension) if extension else "")

    def iter_directory(self, data: bytes):
        for offset in range(0, len(data), 32):
            record = data[offset : offset + 32]
            if len(record) < 32 or record[0] == 0x00:
                break
            if record[0] == 0xE5 or record[11] == 0x0F or record[11] & 0x08:
                continue
            name = self.decode_name(record)
            if name in (".", ".."):
                continue
            yield {
                "name": name,
                "attributes": record[11],
                "modified": dos_datetime(u16(record, 24), u16(record, 22)),
                "first_cluster": u16(record, 26),
                "size": u32(record, 28),
            }

    def extract(self, output: Path) -> list[Entry]:
        manifest: list[Entry] = []

        def visit(directory_data: bytes, relative: Path) -> None:
            for item in self.iter_directory(directory_data):
                relpath = relative / item["name"]
                target = output / relpath
                is_directory = bool(item["attributes"] & 0x10)
                if is_directory:
                    target.mkdir(parents=True, exist_ok=True)
                    manifest.append(
                        Entry(relpath.as_posix() + "/", 0, None, item["attributes"], item["first_cluster"], item["modified"])
                    )
                    visit(self.read_chain(item["first_cluster"]), relpath)
                else:
                    payload = self.read_chain(item["first_cluster"], item["size"])
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(payload)
                    manifest.append(
                        Entry(
                            relpath.as_posix(),
                            len(payload),
                            hashlib.sha256(payload).hexdigest(),
                            item["attributes"],
                            item["first_cluster"],
                            item["modified"],
                        )
                    )

        root_data = self.image[self.root_offset : self.root_offset + self.root_size]
        output.mkdir(parents=True, exist_ok=True)
        visit(root_data, Path())
        return manifest


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    image = args.image.read_bytes()
    if image[510:512] != b"\x55\xaa":
        raise SystemExit("source does not contain a valid MBR signature")
    partition_lba = u32(image, 0x1BE + 8)
    volume = FatVolume(image, partition_lba * 512)
    entries = volume.extract(args.output)
    report = {
        "source": str(args.image),
        "source_sha256": hashlib.sha256(image).hexdigest(),
        "partition_lba": partition_lba,
        "filesystem": f"FAT{volume.fat_bits}",
        "bytes_per_sector": volume.bytes_per_sector,
        "sectors_per_cluster": volume.sectors_per_cluster,
        "entries": [asdict(entry) for entry in entries],
    }
    manifest_path = args.manifest or args.output / "manifest.json"
    manifest_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Extracted {len(entries)} entries from FAT{volume.fat_bits} at LBA {partition_lba}")
    print(f"Manifest: {manifest_path}")


if __name__ == "__main__":
    main()
