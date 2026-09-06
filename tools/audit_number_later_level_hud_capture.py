#!/usr/bin/env python3
"""Audit the controlled original Number later-level user HUD capture."""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "analysis/number-live/progression/original-number-level7-score321.avi"
STATE = ROOT / "analysis/number-live/progression/original-number-level7-score321.state.txt"
NATIVE = ROOT / "analysis/number-live/progression/native/number-level7-score321-native.ppm"
REPORT = ROOT / "analysis/number-live/progression/report.json"

SOURCE_SHA256 = "2485292ac3d2740b19165974643a2c4349858d4781e4577b0052a3f14f58c561"
SOURCE_SIZE = 4_302_300
SOURCE_FRAME_COUNT = 1_486
SOURCE_WIDTH = 640
SOURCE_HEIGHT = 400
SOURCE_RATE = "2190197/31250"
REFERENCE_FRAME = 1_300
LOGICAL_WIDTH = 320
LOGICAL_HEIGHT = 200
LOGICAL_FRAME_BYTES = LOGICAL_WIDTH * LOGICAL_HEIGHT * 3
REFERENCE_RAW_SHA256 = "134273586aaf92b8f5fe4129945bbe881e18644af67e7169f4d6ff9741388007"
REFERENCE_FNV64 = 0x8B5FF7FA8195E03F


def checked_output(arguments: list[str]) -> bytes:
    return subprocess.run(arguments, check=True, stdout=subprocess.PIPE).stdout


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def fnv64_rgb(raw: bytes) -> int:
    value = 1_469_598_103_934_665_603
    mask = (1 << 64) - 1
    for offset in range(0, len(raw), 3):
        pixel = (raw[offset] << 16) | (raw[offset + 1] << 8) | raw[offset + 2]
        value = ((value ^ pixel) * 1_099_511_628_211) & mask
    return value


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    position = 0
    tokens: list[bytes] = []
    while len(tokens) < 4:
        while position < len(data) and data[position] in b" \t\r\n":
            position += 1
        if position < len(data) and data[position] == ord("#"):
            while position < len(data) and data[position] not in b"\r\n":
                position += 1
            continue
        start = position
        while position < len(data) and data[position] not in b" \t\r\n":
            position += 1
        tokens.append(data[start:position])
    while position < len(data) and data[position] in b" \t\r\n":
        position += 1
    if tokens[0] != b"P6" or tokens[3] != b"255":
        raise ValueError(f"Unsupported PPM header in {path}")
    width, height = int(tokens[1]), int(tokens[2])
    payload = data[position:]
    if len(payload) != width * height * 3:
        raise ValueError(f"Truncated PPM payload in {path}")
    return width, height, payload


def probe_source() -> dict[str, str]:
    output = checked_output([
        "ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
        "-show_entries", "stream=codec_name,width,height,r_frame_rate,nb_read_frames",
        "-of", "default=noprint_wrappers=1", str(SOURCE),
    ]).decode("utf-8")
    return dict(line.split("=", 1) for line in output.splitlines() if "=" in line)


def parse_state() -> dict[str, object]:
    text = STATE.read_text(encoding="utf-8").strip()
    match = re.fullmatch(
        r"guest_base=0x[0-9A-F]+ rng_before=(0x[0-9A-F]+) "
        r"rng_after=(0x[0-9A-F]+) demo=(\d+) level=(\d+) pressure=(-?\d+) "
        r"enemy_types=\((\d+), (\d+), (\d+)\) score=(\d+) reserves=(\d+)",
        text,
    )
    if not match:
        raise ValueError("Unrecognized controlled-capture state record")
    return {
        "rng_before": match.group(1).lower(),
        "rng_after": match.group(2).lower(),
        "demo": int(match.group(3)),
        "level": int(match.group(4)),
        "pressure": int(match.group(5)),
        "enemy_types": [int(match.group(6)), int(match.group(7)), int(match.group(8))],
        "score": int(match.group(9)),
        "reserves": int(match.group(10)),
    }


def logical_capture_runs() -> tuple[int, int, list[list[int]]]:
    process = subprocess.Popen([
        "ffmpeg", "-v", "error", "-i", str(SOURCE),
        "-vf", "scale=320:200:flags=neighbor", "-pix_fmt", "rgb24",
        "-f", "rawvideo", "-",
    ], stdout=subprocess.PIPE)
    assert process.stdout is not None
    frame_index = 0
    run_count = 0
    previous_hash: str | None = None
    matching_frames: list[int] = []
    while True:
        frame = process.stdout.read(LOGICAL_FRAME_BYTES)
        if not frame:
            break
        if len(frame) != LOGICAL_FRAME_BYTES:
            process.kill()
            raise RuntimeError("ffmpeg returned a truncated logical frame")
        digest = hashlib.sha256(frame).hexdigest()
        if digest != previous_hash:
            run_count += 1
            previous_hash = digest
        if digest == REFERENCE_RAW_SHA256:
            matching_frames.append(frame_index)
        frame_index += 1
    if process.wait() != 0:
        raise RuntimeError("ffmpeg failed while decoding logical frames")

    ranges: list[list[int]] = []
    for frame in matching_frames:
        if not ranges or frame != ranges[-1][1] + 1:
            ranges.append([frame, frame])
        else:
            ranges[-1][1] = frame
    return frame_index, run_count, ranges


def physical_reference_frame() -> tuple[bytes, bool]:
    raw = checked_output([
        "ffmpeg", "-v", "error", "-i", str(SOURCE), "-vf",
        f"select=eq(n\\,{REFERENCE_FRAME})", "-frames:v", "1",
        "-pix_fmt", "rgb24", "-f", "rawvideo", "-",
    ])
    expected_size = SOURCE_WIDTH * SOURCE_HEIGHT * 3
    if len(raw) != expected_size:
        raise RuntimeError(f"Reference frame has {len(raw)} bytes, expected {expected_size}")

    logical = bytearray()
    uniform = True
    stride = SOURCE_WIDTH * 3
    for y in range(0, SOURCE_HEIGHT, 2):
        for x in range(0, SOURCE_WIDTH, 2):
            offset = y * stride + x * 3
            pixel = raw[offset:offset + 3]
            logical.extend(pixel)
            if (raw[offset + 3:offset + 6] != pixel or
                    raw[offset + stride:offset + stride + 3] != pixel or
                    raw[offset + stride + 3:offset + stride + 6] != pixel):
                uniform = False
    return bytes(logical), uniform


def main() -> int:
    missing = [str(path) for path in (SOURCE, STATE, NATIVE) if not path.is_file()]
    if missing:
        print("Missing audit input(s): " + ", ".join(missing), file=sys.stderr)
        return 2

    probe = probe_source()
    state = parse_state()
    source_sha = file_sha256(SOURCE)
    decoded_frames, logical_runs, matching_ranges = logical_capture_runs()
    source_logical, physical_2x_uniform = physical_reference_frame()
    native_width, native_height, native_raw = read_ppm(NATIVE)

    source_raw_sha = hashlib.sha256(source_logical).hexdigest()
    native_raw_sha = hashlib.sha256(native_raw).hexdigest()
    source_fnv = fnv64_rgb(source_logical)
    native_fnv = fnv64_rgb(native_raw)
    state_verified = (
        state["rng_after"] == "0x00001cb5" and state["demo"] == 0 and
        state["level"] == 7 and state["pressure"] == 0 and
        state["enemy_types"] == [1, 0, 0] and state["score"] == 321 and
        state["reserves"] == 3
    )
    capture_artifacts_verified = (
        source_sha == SOURCE_SHA256 and SOURCE.stat().st_size == SOURCE_SIZE and
        probe.get("codec_name") == "zmbv" and
        probe.get("width") == str(SOURCE_WIDTH) and
        probe.get("height") == str(SOURCE_HEIGHT) and
        probe.get("r_frame_rate") == SOURCE_RATE and
        probe.get("nb_read_frames") == str(SOURCE_FRAME_COUNT) and
        decoded_frames == SOURCE_FRAME_COUNT and state_verified
    )
    reference_verified = (
        physical_2x_uniform and source_raw_sha == REFERENCE_RAW_SHA256 and
        source_fnv == REFERENCE_FNV64 and
        any(start <= REFERENCE_FRAME <= end for start, end in matching_ranges)
    )
    native_matches = (
        native_width == LOGICAL_WIDTH and native_height == LOGICAL_HEIGHT and
        native_raw_sha == REFERENCE_RAW_SHA256 and native_fnv == REFERENCE_FNV64 and
        native_raw == source_logical
    )
    valid = capture_artifacts_verified and reference_verified and native_matches

    report = {
        "source": str(SOURCE.relative_to(ROOT)).replace("\\", "/"),
        "source_sha256": source_sha,
        "source_size": SOURCE.stat().st_size,
        "probe": probe,
        "decoded_frame_count": decoded_frames,
        "logical_run_count": logical_runs,
        "controlled_state": state,
        "state_verified": state_verified,
        "reference_frame": REFERENCE_FRAME,
        "reference_matching_ranges": matching_ranges,
        "reference_raw_rgb24_sha256": source_raw_sha,
        "reference_fnv64": f"0x{source_fnv:016x}",
        "physical_2x_uniform": physical_2x_uniform,
        "native": str(NATIVE.relative_to(ROOT)).replace("\\", "/"),
        "native_raw_rgb24_sha256": native_raw_sha,
        "native_fnv64": f"0x{native_fnv:016x}",
        "capture_artifacts_verified": capture_artifacts_verified,
        "reference_verified": reference_verified,
        "native_matches": native_matches,
        "valid": valid,
    }
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if valid else 1


if __name__ == "__main__":
    raise SystemExit(main())
