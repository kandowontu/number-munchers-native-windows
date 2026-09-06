#!/usr/bin/env python3
"""Locate exact gameplay Troggle highlight components in a lossless capture."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

import numpy as np
from scipy import ndimage


WIDTH = 320
HEIGHT = 200
FRAME_BYTES = WIDTH * HEIGHT * 3
SPECIES_COLORS = {
    "Reggie": (255, 93, 93),
    "Bashful": (93, 190, 255),
    "Worker": (203, 93, 255),
    "Helper": (211, 255, 93),
    "Smarty": (255, 251, 93),
}


def components(image: np.ndarray, color: tuple[int, int, int], minimum: int) -> tuple:
    # Include the whole board viewport plus its two-pixel right/bottom arrival
    # overhang so entry and exit phases are not dropped.
    board = image[24:179, 18:311]
    selected = np.all(board == np.asarray(color, dtype=np.uint8), axis=2)
    labels, count = ndimage.label(selected)
    found: list[tuple[int, int, int, int, int]] = []
    for label_index in range(1, count + 1):
        ys, xs = np.nonzero(labels == label_index)
        if xs.size < minimum:
            continue
        found.append((
            int(xs.min()) + 18,
            int(xs.max()) + 18,
            int(ys.min()) + 24,
            int(ys.max()) + 24,
            int(xs.size),
        ))
    return tuple(sorted(found))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("video", type=Path)
    parser.add_argument("--first-frame", type=int, required=True)
    parser.add_argument("--last-frame", type=int, required=True)
    parser.add_argument("--fps", type=float, default=70.086304)
    parser.add_argument("--minimum-pixels", type=int, default=4)
    parser.add_argument(
        "--species",
        choices=tuple(SPECIES_COLORS),
        action="append",
        help="limit output to one or more species",
    )
    args = parser.parse_args()
    if args.first_frame < 0 or args.last_frame < args.first_frame:
        parser.error("frame window must be nonnegative and ordered")

    selected_species = args.species or list(SPECIES_COLORS)
    frame_filter = (
        f"select=between(n\\,{args.first_frame}\\,{args.last_frame}),"
        "scale=320:200:flags=neighbor"
    )
    command = [
        "ffmpeg", "-v", "error", "-i", str(args.video),
        "-vf", frame_filter, "-fps_mode", "passthrough",
        "-pix_fmt", "rgb24", "-f", "rawvideo", "-",
    ]
    process = subprocess.Popen(command, stdout=subprocess.PIPE)
    assert process.stdout is not None

    previous: tuple | None = None
    local_index = 0
    while True:
        data = process.stdout.read(FRAME_BYTES)
        if len(data) != FRAME_BYTES:
            break
        image = np.frombuffer(data, dtype=np.uint8).reshape(HEIGHT, WIDTH, 3)
        signature = tuple(
            (name, components(image, SPECIES_COLORS[name], args.minimum_pixels))
            for name in selected_species
        )
        if signature != previous:
            global_frame = args.first_frame + local_index
            visible = "; ".join(
                f"{name}={boxes}" for name, boxes in signature if boxes
            ) or "none"
            print(
                f"frame={global_frame:5d} seconds={global_frame / args.fps:9.4f} "
                f"{visible}"
            )
            previous = signature
        local_index += 1

    return_code = process.wait()
    expected_frames = args.last_frame - args.first_frame + 1
    if return_code == 0 and local_index != expected_frames:
        raise RuntimeError(
            f"decoded {local_index} frames, expected {expected_frames}"
        )
    return return_code


if __name__ == "__main__":
    raise SystemExit(main())
