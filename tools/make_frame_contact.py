import argparse
from pathlib import Path

from PIL import Image, ImageDraw


parser = argparse.ArgumentParser(description="Create a labeled nearest-neighbor frame contact sheet.")
parser.add_argument("input", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--start", type=int, default=0)
parser.add_argument("--count", type=int, default=40)
parser.add_argument("--columns", type=int, default=5)
parser.add_argument("--scale", type=int, default=1)
args = parser.parse_args()

files = sorted(args.input.glob("*.png"))[args.start:args.start + args.count]
if not files:
    raise SystemExit("No input frames found")

first = Image.open(files[0]).convert("RGB")
label_height = 16
tile_width = first.width * args.scale
tile_height = first.height * args.scale + label_height
rows = (len(files) + args.columns - 1) // args.columns
output = Image.new("RGB", (tile_width * args.columns, tile_height * rows), (24, 24, 24))
draw = ImageDraw.Draw(output)

for index, path in enumerate(files):
    frame = Image.open(path).convert("RGB")
    if args.scale != 1:
        frame = frame.resize((frame.width * args.scale, frame.height * args.scale), Image.Resampling.NEAREST)
    column = index % args.columns
    row = index // args.columns
    x = column * tile_width
    y = row * tile_height
    draw.text((x + 3, y + 2), f"{args.start + index}: {path.name}", fill="white")
    output.paste(frame, (x, y + label_height))

args.output.parent.mkdir(parents=True, exist_ok=True)
output.save(args.output)
