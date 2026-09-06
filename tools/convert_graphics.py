#!/usr/bin/env python3
"""Convert extracted Munchers PCX and BTMP resources to PNGs.

Pillow handles the VGA 256-colour PCX files.  The original CGA archive uses
packed two-bit PCX scanlines, a valid old variant Pillow does not currently
decode, so a compact standards-compatible decoder is included here. Super
Munchers also uses 8-bit PCX indexes without a 256-colour trailer: its exact
EGAT resource maps those indexes into the 16-colour header palette.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from PIL import Image, ImageDraw


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def decode_rle(data: bytes, expected: int) -> bytes:
    result = bytearray()
    position = 0
    while len(result) < expected:
        value = data[position]
        position += 1
        if value & 0xC0 == 0xC0:
            count = value & 0x3F
            value = data[position]
            position += 1
            result.extend([value] * count)
        else:
            result.append(value)
    return bytes(result[:expected])


def load_pcx(path: Path, index_map: bytes | None = None) -> Image.Image:
    data = path.read_bytes()
    if len(data) < 128 or data[0] != 10 or data[2] != 1:
        raise ValueError(f"{path} is not an RLE PCX file")
    bits = data[3]
    xmin, ymin, xmax, ymax = (u16(data, offset) for offset in (4, 6, 8, 10))
    width, height = xmax - xmin + 1, ymax - ymin + 1
    planes = data[65]
    bytes_per_line = u16(data, 66)
    decoded = decode_rle(data[128:], bytes_per_line * planes * height)
    if bits == 2 and planes == 1:
        # MECC's CGA archives preserve authoritative packed pixel indexes but
        # place red-only placeholder triples in the PCX header.  BGI mode 1
        # displays high-intensity palette 1 instead.
        palette = [(0, 0, 0), (85, 255, 255), (255, 85, 255), (255, 255, 255)]
    elif bits == 8 and planes == 1:
        if len(data) >= 769 and data[-769] == 12:
            palette = [tuple(data[-768 + index * 3 : -765 + index * 3]) for index in range(256)]
        elif index_map is not None and len(index_map) == 256:
            palette = [tuple(data[16 + index * 3 : 19 + index * 3]) for index in range(16)]
        else:
            raise ValueError(
                f"{path} has no 256-colour PCX palette and no 256-byte index map"
            )
    else:
        palette = [tuple(data[16 + index * 3 : 19 + index * 3]) for index in range(16)]
    pixels = []
    for y in range(height):
        row_start = y * bytes_per_line * planes
        rows = [
            decoded[row_start + plane * bytes_per_line : row_start + (plane + 1) * bytes_per_line]
            for plane in range(planes)
        ]
        indices = []
        if bits == 8 and planes == 1:
            indices = list(rows[0][:width])
        elif bits in (2, 4) and planes == 1:
            mask = (1 << bits) - 1
            per_byte = 8 // bits
            for packed in rows[0]:
                for item in range(per_byte):
                    shift = 8 - bits * (item + 1)
                    indices.append((packed >> shift) & mask)
        elif bits == 1 and 1 <= planes <= 4:
            for x in range(width):
                index = 0
                for plane in range(planes):
                    index |= ((rows[plane][x // 8] >> (7 - x % 8)) & 1) << plane
                indices.append(index)
        else:
            raise ValueError(f"unsupported PCX layout: {bits} bits x {planes} planes")
        if bits == 8 and planes == 1 and len(palette) == 16:
            indices = [index_map[index] for index in indices]
            if any(index >= len(palette) for index in indices):
                raise ValueError(f"{path} index map exceeds its 16-colour header palette")
        pixels.extend((*palette[index], 255) for index in indices[:width])
    image = Image.new("RGBA", (width, height))
    image.putdata(pixels)
    return image


def crop_with_black_padding(image: Image.Image, box: tuple[int, int, int, int]) -> Image.Image:
    """Match the original cleared work page when a legacy BTMP box overhangs a sheet."""
    cropped = image.crop(box).convert("RGBA")
    result = Image.new("RGBA", cropped.size, (0, 0, 0, 255))
    result.alpha_composite(cropped)
    return result


def convert_archive(archive: Path, output: Path, index_map_path: Path | None = None) -> dict:
    manifest = json.loads((archive / "manifest.json").read_text(encoding="utf-8"))
    index_map = index_map_path.read_bytes() if index_map_path is not None else None
    if index_map is not None and len(index_map) != 256:
        raise ValueError(f"{index_map_path} is not a 256-byte palette index map")
    pcx_type = next(item for item in manifest["types"] if item["tag"] == "PCXF")
    btmp_type = next(item for item in manifest["types"] if item["tag"] == "BTMP")
    sheets_dir = output / "sheets"
    frames_dir = output / "frames"
    sheets_dir.mkdir(parents=True, exist_ok=True)
    frames_dir.mkdir(parents=True, exist_ok=True)
    sheets: dict[int, Image.Image] = {}
    report: dict = {
        "index_map": index_map_path.as_posix() if index_map_path is not None else None,
        "sheets": [],
        "animations": [],
    }

    for entry in pcx_type["entries"]:
        source = archive / entry["file"]
        image = load_pcx(source, index_map)
        sheets[entry["id"]] = image
        target = sheets_dir / f'{entry["id"]:05d}.png'
        image.save(target)
        report["sheets"].append(
            {"id": entry["id"], "width": image.width, "height": image.height, "file": target.as_posix()}
        )

    for entry in btmp_type["entries"]:
        data = (archive / entry["file"]).read_bytes()
        if len(data) % 12:
            raise ValueError(f'{entry["file"]} has a non-integral BTMP record count')
        animation_dir = frames_dir / f'{entry["id"]:05d}'
        animation_dir.mkdir(exist_ok=True)
        frames = []
        frame_count = len(data) // 12
        for index in range(frame_count):
            sheet_id, top, left, bottom, right = struct.unpack_from("<IHHHH", data, index * 12)
            if sheet_id not in sheets:
                raise ValueError(f"BTMP {entry['id']} references missing sheet {sheet_id}")
            frame_info = {
                "index": index,
                "sheet_id": sheet_id,
                "rectangle_inclusive_yxyx": [top, left, bottom, right],
            }
            if bottom >= top and right >= left:
                frame = crop_with_black_padding(
                    sheets[sheet_id], (left, top, right + 1, bottom + 1)
                )
                target = animation_dir / f"{index:03d}.png"
                frame.save(target)
                frame_info.update({"width": frame.width, "height": frame.height, "file": target.as_posix()})
            else:
                # The Muncher life icons and every Troggle table preserve only
                # the sheet origin in these records; zeroes in the final pair
                # are an engine sentinel rather than a bottom-right corner.
                derived_size: tuple[int, int] | None = None
                if bottom == 0 and right == 0 and frame_count == 19 and index >= 16:
                    derived_size = (36, 39)
                elif bottom == 0 and right == 0 and frame_count == 16:
                    derived_size = (45, 40 if index < 3 else 30)
                if derived_size is None:
                    frame_info["sentinel"] = True
                else:
                    width, height = derived_size
                    frame = crop_with_black_padding(
                        sheets[sheet_id], (left, top, left + width, top + height)
                    )
                    target = animation_dir / f"{index:03d}.png"
                    frame.save(target)
                    frame_info.update(
                        {
                            "width": width,
                            "height": height,
                            "file": target.as_posix(),
                            "derived_from_origin_sentinel": True,
                        }
                    )
            frames.append(frame_info)
        report["animations"].append({"id": entry["id"], "frame_count": len(frames), "frames": frames})

    cell_width, cell_height = 340, 230
    columns = 4
    rows = (len(sheets) + columns - 1) // columns
    contact = Image.new("RGB", (columns * cell_width, rows * cell_height), "#20242b")
    draw = ImageDraw.Draw(contact)
    for index, sheet_id in enumerate(sorted(sheets)):
        image = sheets[sheet_id].convert("RGB")
        scale = min(320 / image.width, 200 / image.height, 1.0)
        preview = image.resize((round(image.width * scale), round(image.height * scale)), Image.Resampling.NEAREST)
        x = (index % columns) * cell_width + 10
        y = (index // columns) * cell_height + 22
        contact.paste(preview, (x, y))
        draw.text((x, 4 + (index // columns) * cell_height), str(sheet_id), fill="white")
    contact_path = output / "contact-sheet.png"
    contact.save(contact_path)
    report["contact_sheet"] = contact_path.as_posix()
    (output / "graphics-manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--index-map",
        type=Path,
        help="optional 256-byte map from 8-bit PCX indexes to its 16-colour header palette",
    )
    args = parser.parse_args()
    report = convert_archive(args.archive, args.output, args.index_map)
    print(f"Converted {len(report['sheets'])} sheets and {len(report['animations'])} animation tables")
    print(f"Contact sheet: {report['contact_sheet']}")


if __name__ == "__main__":
    main()
