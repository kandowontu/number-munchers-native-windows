from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
FRAME_ROOT = ROOT / "assets" / "ripped" / "vga" / "frames"
OUTPUT_ROOT = ROOT / "analysis"
NAMES = {
    1007: "Reggie",
    1008: "Worker",
    1009: "Bashful",
    1010: "Helper",
    1011: "Smarty",
}


for sheet_id, name in NAMES.items():
    output = Image.new("RGB", (600, 560), (25, 25, 25))
    draw = ImageDraw.Draw(output)
    for frame_id in range(16):
        source = Image.open(FRAME_ROOT / f"{sheet_id:05}" / f"{frame_id:03}.png").convert("RGB")
        x = (frame_id % 4) * 150
        y = (frame_id // 4) * 140
        draw.text((x + 4, y + 4), f"{name} frame {frame_id}", fill="white")
        height = 120 if frame_id < 3 else 90
        output.paste(source.resize((135, height), Image.Resampling.NEAREST), (x, y + 20))
    output.save(OUTPUT_ROOT / f"troggle-{sheet_id}-numbered.png")
