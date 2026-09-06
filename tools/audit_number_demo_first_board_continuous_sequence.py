#!/usr/bin/env python3
"""Align every changed native first-Demo page with the DOS capture.

This is a board-wide diagnostic and regression oracle.  It streams the large
lossless capture one frame at a time, reconstructs the 320x200 top-left logical
page, run-length encodes it, obtains the native headless presentation sequence,
and reports exact FNV-64 LCS blocks plus every source-only/native-only span.
Source-only spans are not automatically called parity failures: some are the
already measured dirty-paint or scanout fragments that must remain capture-only.
"""

from __future__ import annotations

from dataclasses import dataclass
from fractions import Fraction
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
NATIVE_TEST = ROOT / "build" / "NumberMunchersRenderTests.exe"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-first-board-continuous-sequence-report.json"
)
FIRST_SOURCE_FRAME = 2_390
# The terminal collision board remains unchanged through frame 6131. Frame
# 6132 begins the first-board-to-Hall transition, which is audited separately.
LAST_SOURCE_FRAME = 6_131
EXPECTED_SOURCE_FRAMES = LAST_SOURCE_FRAME - FIRST_SOURCE_FRAME + 1
SEQUENCE_LINE = re.compile(r"^FIRST_BOARD_PRESENTATION_SEQUENCE(?P<body>.*)$", re.M)
SEQUENCE_ITEM = re.compile(r"0x(?P<hash>[0-9a-fA-F]+)@(?P<frame>[0-9]+)")


@dataclass
class SourceRun:
    start: int
    end: int
    fnv64: int
    sha256: str
    nonuniform_blocks: int
    has_uniform_frame: bool


def read_exact(stream: object, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = stream.read(size - len(chunks))
        if not chunk:
            break
        chunks.extend(chunk)
    return bytes(chunks)


def probe_capture() -> dict[str, object]:
    result = subprocess.run(
        [
            "ffprobe", "-v", "error", "-show_streams", "-show_format",
            "-of", "json", str(CAPTURE),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    raw = json.loads(result.stdout)
    video = next(stream for stream in raw["streams"] if stream["codec_type"] == "video")
    audio = next(stream for stream in raw["streams"] if stream["codec_type"] == "audio")
    return {
        "codec": video["codec_name"],
        "pixel_format": video["pix_fmt"],
        "width": int(video["width"]),
        "height": int(video["height"]),
        "frame_rate_fraction": str(Fraction(video["r_frame_rate"])),
        "frame_rate_hz": float(Fraction(video["r_frame_rate"])),
        "frames": int(video["nb_frames"]),
        "duration_seconds": float(video["duration"]),
        "audio_codec": audio["codec_name"],
        "audio_sample_rate": int(audio["sample_rate"]),
        "audio_channels": int(audio["channels"]),
    }


def decode_source_runs() -> tuple[list[SourceRun], dict[str, int]]:
    selection = (
        f"select=between(n\\,{FIRST_SOURCE_FRAME}\\,{LAST_SOURCE_FRAME})"
    )
    process = subprocess.Popen(
        [
            "ffmpeg", "-v", "error", "-i", str(CAPTURE), "-map", "0:v:0",
            "-vf", selection, "-fps_mode", "passthrough", "-f", "rawvideo",
            "-pix_fmt", "rgb24", "pipe:1",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if process.stdout is None or process.stderr is None:
        raise RuntimeError("ffmpeg did not expose raw-video pipes")

    runs: list[SourceRun] = []
    decoded_frames = 0
    total_nonuniform_blocks = 0
    try:
        while True:
            frame = read_exact(process.stdout, common.CAPTURE_FRAME_BYTES)
            if not frame:
                break
            if len(frame) != common.CAPTURE_FRAME_BYTES:
                raise ValueError(
                    f"partial source frame {decoded_frames}: "
                    f"{len(frame)}/{common.CAPTURE_FRAME_BYTES} bytes"
                )
            captured = np.frombuffer(frame, dtype=np.uint8).reshape(
                common.CAPTURE_HEIGHT, common.CAPTURE_WIDTH, 3
            )
            logical = captured[0::2, 0::2]
            nonuniform = int(np.count_nonzero(
                np.any(logical != captured[0::2, 1::2], axis=2) |
                np.any(logical != captured[1::2, 0::2], axis=2) |
                np.any(logical != captured[1::2, 1::2], axis=2)
            ))
            total_nonuniform_blocks += nonuniform
            rgb = logical.tobytes()
            digest = hashlib.sha256(rgb).hexdigest()
            global_frame = FIRST_SOURCE_FRAME + decoded_frames
            if runs and runs[-1].sha256 == digest:
                runs[-1].end = global_frame
                runs[-1].nonuniform_blocks += nonuniform
                runs[-1].has_uniform_frame |= nonuniform == 0
            else:
                runs.append(SourceRun(
                    start=global_frame,
                    end=global_frame,
                    fnv64=common.renderer_fnv64(rgb),
                    sha256=digest,
                    nonuniform_blocks=nonuniform,
                    has_uniform_frame=nonuniform == 0,
                ))
            decoded_frames += 1
    finally:
        process.stdout.close()
    error_text = process.stderr.read().decode("utf-8", errors="replace")
    return_code = process.wait()
    if return_code:
        raise subprocess.CalledProcessError(
            return_code, process.args, stderr=error_text
        )
    return runs, {
        "first_global_frame": FIRST_SOURCE_FRAME,
        "last_global_frame": LAST_SOURCE_FRAME,
        "decoded_frames": decoded_frames,
        "logical_run_count": len(runs),
        "nonuniform_2x_blocks": total_nonuniform_blocks,
        "runs_with_uniform_frame": sum(run.has_uniform_frame for run in runs),
        "runs_without_uniform_frame": sum(not run.has_uniform_frame for run in runs),
    }


def read_native_sequence() -> tuple[list[int], list[int], dict[str, object]]:
    if not NATIVE_TEST.is_file():
        raise FileNotFoundError(NATIVE_TEST)
    environment = os.environ.copy()
    environment["MUNCHERS_AUDIT_FIRST_BOARD_SEQUENCE"] = "1"
    result = subprocess.run(
        [str(NATIVE_TEST)],
        cwd=NATIVE_TEST.parent,
        env=environment,
        capture_output=True,
        text=True,
        timeout=120,
    )
    combined = result.stdout + "\n" + result.stderr
    match = SEQUENCE_LINE.search(combined)
    if match is None:
        raise RuntimeError("native test did not emit the first-board sequence")
    items = list(SEQUENCE_ITEM.finditer(match.group("body")))
    hashes = [int(item.group("hash"), 16) for item in items]
    starts = [int(item.group("frame")) for item in items]
    if not hashes or len(hashes) != len(starts):
        raise RuntimeError("native first-board sequence was empty or malformed")
    return hashes, starts, {
        "executable": NATIVE_TEST.relative_to(ROOT).as_posix(),
        "exit_code": result.returncode,
        "test_passed": result.returncode == 0,
        "changed_page_count": len(hashes),
        "first_presentation_frame": starts[0],
        "last_presentation_frame": starts[-1],
    }


def lcs_pairs(left: list[int], right: list[int]) -> list[tuple[int, int]]:
    lengths = [[0] * (len(right) + 1) for _ in range(len(left) + 1)]
    for left_index in range(len(left) - 1, -1, -1):
        row = lengths[left_index]
        following = lengths[left_index + 1]
        for right_index in range(len(right) - 1, -1, -1):
            if left[left_index] == right[right_index]:
                row[right_index] = 1 + following[right_index + 1]
            else:
                row[right_index] = max(
                    following[right_index], row[right_index + 1]
                )
    pairs: list[tuple[int, int]] = []
    left_index = 0
    right_index = 0
    while left_index < len(left) and right_index < len(right):
        if (
            left[left_index] == right[right_index] and
            lengths[left_index][right_index] ==
                1 + lengths[left_index + 1][right_index + 1]
        ):
            pairs.append((left_index, right_index))
            left_index += 1
            right_index += 1
        elif (
            lengths[left_index + 1][right_index] >=
            lengths[left_index][right_index + 1]
        ):
            left_index += 1
        else:
            right_index += 1
    return pairs


def contiguous_pairs(pairs: list[tuple[int, int]]) -> list[list[tuple[int, int]]]:
    groups: list[list[tuple[int, int]]] = []
    for pair in pairs:
        if (
            groups and pair[0] == groups[-1][-1][0] + 1 and
            pair[1] == groups[-1][-1][1] + 1
        ):
            groups[-1].append(pair)
        else:
            groups.append([pair])
    return groups


def missing_groups(size: int, matched: set[int]) -> list[tuple[int, int]]:
    groups: list[tuple[int, int]] = []
    start: int | None = None
    for index in range(size + 1):
        missing = index < size and index not in matched
        if missing and start is None:
            start = index
        elif not missing and start is not None:
            groups.append((start, index - 1))
            start = None
    return groups


def main() -> None:
    source_sha256 = common.sha256_file(CAPTURE)
    probe = probe_capture()
    source_runs, source_window = decode_source_runs()
    native_hashes, native_starts, native_execution = read_native_sequence()
    source_hashes = [run.fnv64 for run in source_runs]
    pairs = lcs_pairs(native_hashes, source_hashes)
    matched_native = {native for native, _ in pairs}
    matched_source = {source for _, source in pairs}

    blocks = []
    for group in contiguous_pairs(pairs):
        first_native, first_source = group[0]
        last_native, last_source = group[-1]
        timing_deltas = [
            source_runs[source].start - native_starts[native]
            for native, source in group
        ]
        blocks.append({
            "native_run_start": first_native,
            "native_run_end": last_native,
            "source_run_start": first_source,
            "source_run_end": last_source,
            "source_frame_start": source_runs[first_source].start,
            "source_frame_end": source_runs[last_source].end,
            "run_count": len(group),
            "source_minus_native_frame_delta_min": min(timing_deltas),
            "source_minus_native_frame_delta_max": max(timing_deltas),
        })

    source_only = []
    for first, last in missing_groups(len(source_runs), matched_source):
        source_only.append({
            "source_run_start": first,
            "source_run_end": last,
            "source_frame_start": source_runs[first].start,
            "source_frame_end": source_runs[last].end,
            "run_count": last - first + 1,
            "runs_with_uniform_frame": sum(
                source_runs[index].has_uniform_frame
                for index in range(first, last + 1)
            ),
            "nonuniform_2x_blocks": sum(
                source_runs[index].nonuniform_blocks
                for index in range(first, last + 1)
            ),
            "renderer_fnv64": [
                f"0x{source_runs[index].fnv64:016x}"
                for index in range(first, last + 1)
            ],
        })

    native_only = []
    for first, last in missing_groups(len(native_hashes), matched_native):
        native_only.append({
            "native_run_start": first,
            "native_run_end": last,
            "native_frame_start": native_starts[first],
            "native_frame_end": native_starts[last],
            "run_count": last - first + 1,
            "renderer_fnv64": [
                f"0x{native_hashes[index]:016x}"
                for index in range(first, last + 1)
            ],
        })

    expected_probe = {
        "codec": "zmbv", "pixel_format": "bgr0", "width": 640,
        "height": 400, "frame_rate_fraction": "2190197/31250",
        "frame_rate_hz": 70.086304, "frames": common.EXPECTED_CAPTURE_FRAMES,
        "duration_seconds": 313.955777, "audio_codec": "pcm_s16le",
        "audio_sample_rate": 48_000, "audio_channels": 2,
    }
    report = {
        "schema": "number-first-demo-board-continuous-sequence-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "probe": probe,
        "probe_matches": probe == expected_probe,
        "source_window": source_window,
        "source_window_matches":
            source_window["decoded_frames"] == EXPECTED_SOURCE_FRAMES,
        "native_execution": native_execution,
        "exact_lcs_run_count": len(pairs),
        "matched_native_changed_pages": len(matched_native),
        "matched_source_logical_runs": len(matched_source),
        "continuous_match_blocks": blocks,
        "longest_contiguous_match_run_count": max(
            (block["run_count"] for block in blocks), default=0
        ),
        "source_only_spans": source_only,
        "native_only_spans": native_only,
        "classification": {
            "board_wide_parity_claim": False,
            "reason": (
                "source-only and native-only spans must be reconciled against "
                "the focused dirty-paint/scanout classifications before a "
                "board-wide continuous parity claim is valid"
            ),
        },
    }
    report["valid"] = all((
        report["source_matches"],
        report["probe_matches"],
        report["source_window_matches"],
        native_execution["test_passed"],
        bool(pairs),
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "valid": report["valid"],
        "source_runs": source_window["logical_run_count"],
        "native_pages": native_execution["changed_page_count"],
        "exact_lcs": len(pairs),
        "match_blocks": len(blocks),
        "longest_block": report["longest_contiguous_match_run_count"],
        "source_only_spans": len(source_only),
        "native_only_spans": len(native_only),
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
