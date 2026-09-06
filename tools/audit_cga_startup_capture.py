#!/usr/bin/env python3
"""Audit live DOS CGA startup pages against native 320x200 framebuffers."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
WIDTH = 320
HEIGHT = 200
FRAME_BYTES = WIDTH * HEIGHT * 3
EXPECTED_RATE = "14980681/250000"

SPECS = (
    {
        "game": "Number",
        "capture": ROOT / "analysis/number-live/cga/original-number-startup.avi",
        "capture_sha256":
            "16D6D9B215058223E1BACC4393EA2F504874A5450D1197CFA008343E502AC311",
        "mode_switch_capture":
            ROOT / "analysis/number-live/cga/original-number-startup-mode-switch.avi",
        "mode_switch_sha256":
            "12BC0C1F1C00FD6D45936AB41AD445A2BDD0B4757D075CF22F30DE0E71C891D7",
        "frames": 380,
        "prefix": "number",
    },
    {
        "game": "Word",
        "capture": ROOT / "analysis/word-live/cga/original-word-startup.avi",
        "capture_sha256":
            "2AB6F023A01D2B3C7512BB5D0B2556E96F6762DC1D4DF2EBA59F439743599F6C",
        "mode_switch_capture":
            ROOT / "analysis/word-live/cga/original-word-startup-mode-switch.avi",
        "mode_switch_sha256":
            "67F9D9A1B17D02E619379C035A126B976EEA9F0BBA56EAFAB5C232EDA55BC178",
        "frames": 388,
        "prefix": "word",
    },
)

EXPECTED_PAGES = ("version", "splash", "title")
EXPECTED_PALETTE = {
    (0x00, 0x00, 0x00),
    (0x00, 0x00, 0xAA),
    (0x55, 0xFF, 0xFF),
    (0xFF, 0x55, 0xFF),
    (0xFF, 0xFF, 0xFF),
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def probe(path: Path) -> dict[str, object]:
    result = subprocess.run(
        [
            "ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
            "-show_entries", "stream=codec_name,width,height,r_frame_rate,nb_read_frames",
            "-of", "json", str(path),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    streams = json.loads(result.stdout).get("streams", [])
    if len(streams) != 1:
        raise RuntimeError(f"{path}: expected one video stream")
    stream = streams[0]
    return {
        "codec": stream.get("codec_name"),
        "width": int(stream.get("width", 0)),
        "height": int(stream.get("height", 0)),
        "frame_rate_fraction": stream.get("r_frame_rate"),
        "frames": int(stream.get("nb_read_frames", 0)),
    }


def decode(path: Path) -> list[bytes]:
    result = subprocess.run(
        [
            "ffmpeg", "-v", "error", "-i", str(path), "-map", "0:v:0",
            "-f", "rawvideo", "-pix_fmt", "rgb24", "-",
        ],
        check=True,
        capture_output=True,
    )
    if len(result.stdout) % FRAME_BYTES:
        raise RuntimeError(f"{path}: incomplete RGB frame payload")
    return [
        result.stdout[offset:offset + FRAME_BYTES]
        for offset in range(0, len(result.stdout), FRAME_BYTES)
    ]


def frame_runs(indices: list[int]) -> list[dict[str, int]]:
    if not indices:
        return []
    result: list[dict[str, int]] = []
    start = previous = indices[0]
    for index in indices[1:]:
        if index != previous + 1:
            result.append({"first_frame": start, "last_frame": previous,
                           "frame_count": previous - start + 1})
            start = index
        previous = index
    result.append({"first_frame": start, "last_frame": previous,
                   "frame_count": previous - start + 1})
    return result


def rgb_palette(rgb: bytes) -> set[tuple[int, int, int]]:
    return {tuple(rgb[index:index + 3]) for index in range(0, len(rgb), 3)}


def audit(spec: dict[str, object]) -> dict[str, object]:
    capture = Path(spec["capture"])
    mode_switch = Path(spec["mode_switch_capture"])
    if not capture.is_file() or not mode_switch.is_file():
        raise FileNotFoundError(capture if not capture.is_file() else mode_switch)
    capture_hash = sha256_file(capture)
    mode_switch_hash = sha256_file(mode_switch)
    video = probe(capture)
    frames = decode(capture)
    frame_digests = [hashlib.sha256(frame).hexdigest() for frame in frames]

    pages: list[dict[str, object]] = []
    first_occurrences: list[int] = []
    native_directory = ROOT / "analysis/cga-live/native"
    for page_name in EXPECTED_PAGES:
        native_path = native_directory / (
            f"{spec['prefix']}-{page_name}-native.ppm"
        )
        with Image.open(native_path) as source:
            image = source.convert("RGB")
            if image.size != (WIDTH, HEIGHT):
                raise RuntimeError(f"{native_path}: expected 320x200")
            native_rgb = image.tobytes()
        digest = hashlib.sha256(native_rgb).hexdigest()
        occurrences = [
            index for index, frame_digest in enumerate(frame_digests)
            if frame_digest == digest
        ]
        if occurrences:
            first_occurrences.append(occurrences[0])
        palette = rgb_palette(native_rgb)
        pages.append({
            "page": page_name,
            "native": native_path.relative_to(ROOT).as_posix(),
            "rgb_sha256": digest.upper(),
            "exact_source_frame_count": len(occurrences),
            "exact_source_frame_runs": frame_runs(occurrences),
            "palette": ["#%02X%02X%02X" % color for color in sorted(palette)],
            "uses_exact_cga_palette": palette <= EXPECTED_PALETTE,
        })

    pages_exact = all(int(page["exact_source_frame_count"]) > 0 for page in pages)
    pages_ordered = (
        len(first_occurrences) == len(EXPECTED_PAGES)
        and first_occurrences == sorted(first_occurrences)
    )
    bytes_pinned = capture_hash == spec["capture_sha256"]
    mode_switch_pinned = mode_switch_hash == spec["mode_switch_sha256"]
    video_matches = bool(
        video["codec"] == "zmbv"
        and video["width"] == WIDTH
        and video["height"] == HEIGHT
        and video["frame_rate_fraction"] == EXPECTED_RATE
        and video["frames"] == spec["frames"]
        and len(frames) == spec["frames"]
    )
    valid = bool(
        bytes_pinned and mode_switch_pinned and video_matches
        and pages_exact and pages_ordered
        and all(bool(page["uses_exact_cga_palette"]) for page in pages)
    )
    return {
        "game": spec["game"],
        "capture": capture.relative_to(ROOT).as_posix(),
        "capture_sha256": capture_hash,
        "expected_capture_sha256": spec["capture_sha256"],
        "capture_bytes_pinned": bytes_pinned,
        "mode_switch_capture": mode_switch.relative_to(ROOT).as_posix(),
        "mode_switch_capture_sha256": mode_switch_hash,
        "expected_mode_switch_capture_sha256": spec["mode_switch_sha256"],
        "mode_switch_capture_bytes_pinned": mode_switch_pinned,
        "video": video,
        "all_three_pages_exact": pages_exact,
        "all_three_pages_observed_in_order": pages_ordered,
        "pages": pages,
        "valid": valid,
    }


def main() -> int:
    games = [audit(spec) for spec in SPECS]
    report = {
        "schema": "munchers-cga-startup-live-audit-v1",
        "logical_size": [WIDTH, HEIGHT],
        "expected_palette": [
            "#%02X%02X%02X" % color for color in sorted(EXPECTED_PALETTE)
        ],
        "games": games,
        "valid": all(bool(game["valid"]) for game in games),
    }
    output = ROOT / "analysis/cga-live/startup-report.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for game in games:
        counts = ", ".join(
            f"{page['page']}={page['exact_source_frame_count']}"
            for page in game["pages"]
        )
        print(f"{game['game']} CGA startup: {counts}; valid={game['valid']}")
    print(f"wrote {output.relative_to(ROOT)}")
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
