#!/usr/bin/env python3
"""Report selected sprite-color component motion from a preserved lossless demo."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

import numpy as np
from scipy import ndimage


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("video", type=Path)
    parser.add_argument("--start", type=float, default=33.0)
    parser.add_argument("--duration", type=float, default=55.0)
    parser.add_argument("--fps", type=float, default=70.086304)
    parser.add_argument("--color", choices=("red", "cyan", "yellow"), default="red")
    args = parser.parse_args()

    command = [
        "ffmpeg", "-v", "error", "-ss", str(args.start), "-i", str(args.video),
        "-t", str(args.duration), "-vf", "scale=320:200:flags=neighbor",
        "-pix_fmt", "rgb24", "-f", "rawvideo", "-",
    ]
    process = subprocess.Popen(command, stdout=subprocess.PIPE)
    assert process.stdout is not None

    frame_bytes = 320 * 200 * 3
    last_signature: tuple[tuple[int, int, int, int], ...] | None = None
    frame_index = 0
    while True:
        data = process.stdout.read(frame_bytes)
        if len(data) != frame_bytes:
            break
        image = np.frombuffer(data, dtype=np.uint8).reshape((200, 320, 3))
        board = image[26:176, 20:308]
        if args.color == "red":
            selected = ((board[:, :, 0] > 120) & (board[:, :, 1] < 120) &
                        (board[:, :, 2] < 120))
        elif args.color == "cyan":
            selected = ((board[:, :, 0] < 130) & (board[:, :, 1] > 110) &
                        (board[:, :, 2] > 150))
        else:
            selected = ((board[:, :, 0] > 120) & (board[:, :, 1] > 110) &
                        (board[:, :, 2] < 120))
        labels, count = ndimage.label(selected)
        components: list[tuple[int, int, int, int]] = []
        for label_index in range(1, count + 1):
            ys, xs = np.nonzero(labels == label_index)
            if xs.size < 35:
                continue
            x0, x1 = int(xs.min()) + 20, int(xs.max()) + 20
            y0, y1 = int(ys.min()) + 26, int(ys.max()) + 26
            if x1 - x0 >= 12 and y1 - y0 >= 4:
                components.append((x0, x1, y0, y1))
        signature = tuple(sorted(components))
        if signature != last_signature:
            timestamp = args.start + frame_index / args.fps
            print(f"{timestamp:9.4f}  frame={frame_index:5d}  {signature}")
            last_signature = signature
        frame_index += 1

    return process.wait()


if __name__ == "__main__":
    raise SystemExit(main())
