#!/usr/bin/env python3
"""Audit the seeded Number Munchers Smarty Demo against native presentation."""

from __future__ import annotations

from dataclasses import dataclass
from fractions import Fraction
import csv
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (
    ROOT / "analysis/number-live/gameplay/smarty-seed-ae67/"
    "original-number-smarty-seed-ae67.avi"
)
NATIVE_DIRECTORY = (
    ROOT / "analysis/number-live/gameplay/smarty-seed-ae67/native"
)
REPORT = (
    ROOT / "analysis/number-live/gameplay/smarty-seed-ae67/report.json"
)

EXPECTED_SOURCE_SHA256 = (
    "BA43B76F81AB5AF8C34C88D93970B1E5B687AB62216D79E153DBCF5A018745FA"
)
EXPECTED_SOURCE_FRAMES = 6_266
WINDOW_FIRST_FRAME = 2_424
WINDOW_LAST_FRAME = 3_405
NATIVE_WINDOW_LAST_SAMPLE = WINDOW_LAST_FRAME - WINDOW_FIRST_FRAME
CAPTURE_WIDTH = 640
CAPTURE_HEIGHT = 400
LOGICAL_WIDTH = 320
LOGICAL_HEIGHT = 200
CAPTURE_FRAME_BYTES = CAPTURE_WIDTH * CAPTURE_HEIGHT * 3
SMARTY_RGB = np.asarray((255, 251, 93), dtype=np.uint8)
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1
EXPECTED_NONUNIFORM_RUNS = [2, 8, 37, 60, 63, 67, 73, 79, 84]
EXCLUDED_COMPLETE_RUNS = {0, 46, 72}
EXPECTED_FINAL_CALLS = 84
EXPECTED_FINAL_STATE = 0x09C161E3
EXPECTED_FEEDBACK_HASH = 0xF5D6A69A5070204C


@dataclass
class SourceRun:
    start: int
    end: int
    rgb: bytes
    fnv64: int
    nonuniform_blocks: int
    uniform_frames: int
    smarty_pixels: int


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def renderer_fnv64(rgb: bytes) -> int:
    value = FNV_OFFSET
    for offset in range(0, len(rgb), 3):
        value ^= (
            (rgb[offset] << 16)
            | (rgb[offset + 1] << 8)
            | rgb[offset + 2]
        )
        value = (value * FNV_PRIME) & MASK64
    return value


def probe_capture(path: Path) -> dict[str, object]:
    result = subprocess.run(
        [
            "ffprobe", "-v", "error", "-show_streams", "-show_format",
            "-of", "json", str(path),
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


def decode_source_runs(path: Path) -> list[SourceRun]:
    selection = f"select=between(n\\,{WINDOW_FIRST_FRAME}\\,{WINDOW_LAST_FRAME})"
    result = subprocess.run(
        [
            "ffmpeg", "-v", "error", "-i", str(path), "-map", "0:v:0",
            "-vf", selection, "-fps_mode", "passthrough", "-f", "rawvideo",
            "-pix_fmt", "rgb24", "pipe:1",
        ],
        check=True,
        capture_output=True,
    )
    if len(result.stdout) % CAPTURE_FRAME_BYTES:
        raise ValueError("Smarty raw-video decode ended with a partial frame")

    runs: list[SourceRun] = []
    for offset in range(0, len(result.stdout), CAPTURE_FRAME_BYTES):
        global_frame = WINDOW_FIRST_FRAME + offset // CAPTURE_FRAME_BYTES
        captured = np.frombuffer(
            result.stdout[offset : offset + CAPTURE_FRAME_BYTES], dtype=np.uint8
        ).reshape(CAPTURE_HEIGHT, CAPTURE_WIDTH, 3)
        logical = captured[0::2, 0::2]
        mismatches = (
            np.any(logical != captured[0::2, 1::2], axis=2)
            | np.any(logical != captured[1::2, 0::2], axis=2)
            | np.any(logical != captured[1::2, 1::2], axis=2)
        )
        nonuniform = int(np.count_nonzero(mismatches))
        rgb = logical.tobytes()
        fnv64 = renderer_fnv64(rgb)
        smarty_pixels = int(np.count_nonzero(np.all(logical == SMARTY_RGB, axis=2)))
        if runs and runs[-1].fnv64 == fnv64:
            run = runs[-1]
            run.end = global_frame
            run.nonuniform_blocks += nonuniform
            run.uniform_frames += int(nonuniform == 0)
            run.smarty_pixels = max(run.smarty_pixels, smarty_pixels)
        else:
            runs.append(SourceRun(
                start=global_frame,
                end=global_frame,
                rgb=rgb,
                fnv64=fnv64,
                nonuniform_blocks=nonuniform,
                uniform_frames=int(nonuniform == 0),
                smarty_pixels=smarty_pixels,
            ))
    return runs


def read_native_pages(directory: Path) -> list[dict[str, object]]:
    table = directory / "number-smarty-pages.tsv"
    with table.open("r", encoding="utf-8", newline="") as source:
        rows = list(csv.DictReader(source, delimiter="\t"))
    pages: list[dict[str, object]] = []
    for row in rows:
        page = int(row["page"])
        image_path = directory / f"smarty-page-{page:05d}.ppm"
        image = np.asarray(Image.open(image_path).convert("RGB"), dtype=np.uint8)
        rgb = image.tobytes()
        fnv64 = renderer_fnv64(rgb)
        expected = int(row["hash"], 16)
        if fnv64 != expected:
            raise ValueError(
                f"native page {page} hash mismatch: 0x{fnv64:016x} != {row['hash']}"
            )
        pages.append({
            "page": page,
            "sample": int(row["sample"]),
            "time_seconds": float(row["time_seconds"]),
            "fnv64": fnv64,
            "controller_page": int(row["controller_page"]),
            "random_calls": int(row["random_calls"]),
            "random_state": row["random_state"],
            "actors": row["actors"],
            "feedback_kind": int(row["feedback_kind"]),
            "feedback_message": row["feedback_message"],
            "feedback_slot": int(row["feedback_slot"]),
            "post_feedback": bool(int(row["post_feedback"])),
            "smarty_pixels": int(np.count_nonzero(np.all(image == SMARTY_RGB, axis=2))),
        })
    return pages


def lcs_matches(
    source_runs: list[SourceRun], source_indices: list[int],
    native_pages: list[dict[str, object]],
) -> tuple[list[dict[str, object]], list[int], list[int]]:
    """Match repeated bite hashes without greedily consuming the wrong pose."""
    source_hashes = [source_runs[index].fnv64 for index in source_indices]
    native_hashes = [int(page["fnv64"]) for page in native_pages]
    rows = len(source_hashes) + 1
    columns = len(native_hashes) + 1
    lengths = [[0] * columns for _ in range(rows)]
    for source_pos in range(len(source_hashes) - 1, -1, -1):
        for native_pos in range(len(native_hashes) - 1, -1, -1):
            if source_hashes[source_pos] == native_hashes[native_pos]:
                lengths[source_pos][native_pos] = (
                    1 + lengths[source_pos + 1][native_pos + 1]
                )
            else:
                lengths[source_pos][native_pos] = max(
                    lengths[source_pos + 1][native_pos],
                    lengths[source_pos][native_pos + 1],
                )

    source_pos = 0
    native_pos = 0
    pairs: list[tuple[int, int]] = []
    while source_pos < len(source_hashes) and native_pos < len(native_hashes):
        if source_hashes[source_pos] == native_hashes[native_pos]:
            pairs.append((source_pos, native_pos))
            source_pos += 1
            native_pos += 1
        elif lengths[source_pos + 1][native_pos] >= lengths[source_pos][native_pos + 1]:
            source_pos += 1
        else:
            native_pos += 1

    matched_source_positions = {pair[0] for pair in pairs}
    matched_native_positions = {pair[1] for pair in pairs}
    matches: list[dict[str, object]] = []
    for matched_source_pos, matched_native_pos in pairs:
        source_index = source_indices[matched_source_pos]
        run = source_runs[source_index]
        page = native_pages[matched_native_pos]
        matches.append({
            "native_page": page["page"],
            "source_run": source_index,
            "source_first_frame": run.start,
            "source_last_frame": run.end,
            "fnv64": f"0x{run.fnv64:016x}",
            "smarty_pixels": run.smarty_pixels,
        })
    unmatched_source = [
        source_indices[position] for position in range(len(source_indices))
        if position not in matched_source_positions
    ]
    unmatched_native = [
        int(native_pages[position]["page"])
        for position in range(len(native_pages))
        if position not in matched_native_positions
    ]
    return matches, unmatched_source, unmatched_native


def partial_repaint_evidence(
    previous: SourceRun, partial: SourceRun, following: SourceRun,
) -> dict[str, object]:
    previous_rgb = np.frombuffer(previous.rgb, dtype=np.uint8).reshape(
        LOGICAL_HEIGHT, LOGICAL_WIDTH, 3
    )
    partial_rgb = np.frombuffer(partial.rgb, dtype=np.uint8).reshape(
        LOGICAL_HEIGHT, LOGICAL_WIDTH, 3
    )
    following_rgb = np.frombuffer(following.rgb, dtype=np.uint8).reshape(
        LOGICAL_HEIGHT, LOGICAL_WIDTH, 3
    )
    same_previous = np.all(partial_rgb == previous_rgb, axis=2)
    same_following = np.all(partial_rgb == following_rgb, axis=2)
    changed = np.argwhere(~same_previous)
    unique_pixels = int(np.count_nonzero(~(same_previous | same_following)))
    bbox = None
    if changed.size:
        bbox = {
            "x_min": int(changed[:, 1].min()),
            "y_min": int(changed[:, 0].min()),
            "x_max": int(changed[:, 1].max()),
            "y_max": int(changed[:, 0].max()),
        }
    return {
        "changed_from_previous": int(np.count_nonzero(~same_previous)),
        "changed_from_following": int(np.count_nonzero(~same_following)),
        "unique_pixels": unique_pixels,
        "changed_bbox": bbox,
        "is_previous_following_composite": unique_pixels == 0,
    }


def main() -> None:
    source_sha256 = sha256_file(SOURCE)
    probe = probe_capture(SOURCE)
    source_runs = decode_source_runs(SOURCE)
    native_pages = read_native_pages(NATIVE_DIRECTORY)
    scoped_native_pages = [
        page for page in native_pages
        if int(page["sample"]) <= NATIVE_WINDOW_LAST_SAMPLE
    ]
    post_window_native_pages = [
        int(page["page"]) for page in native_pages
        if int(page["sample"]) > NATIVE_WINDOW_LAST_SAMPLE
    ]
    nonuniform_source_runs = [
        index for index, run in enumerate(source_runs)
        if run.uniform_frames == 0
    ]
    auditable_source_runs = [
        index for index, run in enumerate(source_runs)
        if run.uniform_frames > 0 and index not in EXCLUDED_COMPLETE_RUNS
    ]
    matches, unmatched_source_runs, unmatched_native_pages = lcs_matches(
        source_runs, auditable_source_runs, scoped_native_pages
    )
    matched_pages = {int(match["native_page"]) for match in matches}
    native_smarty_pages = [
        int(page["page"]) for page in scoped_native_pages
        if int(page["smarty_pixels"]) > 0
    ]
    unmatched_smarty_pages = [
        page for page in native_smarty_pages if page not in matched_pages
    ]
    source_smarty_runs = [
        index for index, run in enumerate(source_runs)
        if index in auditable_source_runs and run.smarty_pixels > 0
    ]
    matched_source_runs = {int(match["source_run"]) for match in matches}
    unmatched_source_smarty_runs = [
        index for index in source_smarty_runs if index not in matched_source_runs
    ]
    run_46_evidence = partial_repaint_evidence(
        source_runs[45], source_runs[46], source_runs[47]
    )
    run_72_evidence = partial_repaint_evidence(
        source_runs[71], source_runs[72], source_runs[74]
    )
    capture_artifacts_verified = (
        nonuniform_source_runs == EXPECTED_NONUNIFORM_RUNS
        and run_46_evidence == {
            "changed_from_previous": 43,
            "changed_from_following": 333,
            "unique_pixels": 0,
            "changed_bbox": {"x_min": 269, "y_min": 166, "x_max": 293, "y_max": 167},
            "is_previous_following_composite": True,
        }
        and run_72_evidence == {
            "changed_from_previous": 29,
            "changed_from_following": 344,
            "unique_pixels": 0,
            "changed_bbox": {"x_min": 308, "y_min": 147, "x_max": 308, "y_max": 175},
            "is_previous_following_composite": True,
        }
    )

    source_matches = source_sha256 == EXPECTED_SOURCE_SHA256
    probe_matches = (
        probe["codec"] == "zmbv"
        and probe["width"] == CAPTURE_WIDTH
        and probe["height"] == CAPTURE_HEIGHT
        and probe["frame_rate_fraction"] == "2190197/31250"
        and probe["frames"] == EXPECTED_SOURCE_FRAMES
        and probe["audio_codec"] == "pcm_s16le"
        and probe["audio_sample_rate"] == 48_000
        and probe["audio_channels"] == 2
    )
    initial_native = native_pages[0]
    initialization_matches = (
        initial_native["random_calls"] == 61
        and initial_native["random_state"].lower() == "0x915fbdd8"
        and initial_native["actors"] == ""
    )
    final_native = native_pages[-1]
    final_state_matches = (
        final_native["random_calls"] == EXPECTED_FINAL_CALLS
        and int(final_native["random_state"], 16) == EXPECTED_FINAL_STATE
    )
    feedback_pages = [
        page for page in scoped_native_pages
        if page["feedback_kind"] == 2
        and page["feedback_message"] == "Aargh"
        and page["feedback_slot"] == 2
    ]
    feedback_matches = any(
        int(page["fnv64"]) == EXPECTED_FEEDBACK_HASH for page in feedback_pages
    )
    ordered_exact = (
        [source_runs[index].fnv64 for index in auditable_source_runs]
        == [int(page["fnv64"]) for page in scoped_native_pages]
    )

    report = {
        "source": SOURCE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_matches,
        "probe": probe,
        "probe_matches": probe_matches,
        "focused_window": {
            "first_frame": WINDOW_FIRST_FRAME,
            "last_frame": WINDOW_LAST_FRAME,
            "frames": WINDOW_LAST_FRAME - WINDOW_FIRST_FRAME + 1,
            "logical_runs": len(source_runs),
            "auditable_presentation_runs": len(auditable_source_runs),
            "nonuniform_2x_blocks": sum(run.nonuniform_blocks for run in source_runs),
        },
        "source_classifications": {
            "left_window_boundary": [0],
            "nonuniform_capture_frames": nonuniform_source_runs,
            "uniform_partial_repaint_runs": {
                "46": run_46_evidence,
                "72": run_72_evidence,
            },
            "capture_artifacts_verified": capture_artifacts_verified,
        },
        "native": {
            "directory": NATIVE_DIRECTORY.relative_to(ROOT).as_posix(),
            "seed": "0xae67",
            "configuration": (
                "NM.CFG SHA-256 "
                "8C3D76A109029EC9AB7E72C851A857F0EDF28AC0D788475DC29DAF854628B8F4"
            ),
            "changed_pages": len(native_pages),
            "focused_window_pages": len(scoped_native_pages),
            "post_window_pages": post_window_native_pages,
            "initial_setup_calls": initial_native["random_calls"],
            "initial_random_state": initial_native["random_state"],
            "initialization_matches": initialization_matches,
            "final_random_calls": final_native["random_calls"],
            "final_random_state": final_native["random_state"],
            "final_state_matches": final_state_matches,
            "feedback_matches": feedback_matches,
        },
        "ordered_comparison": {
            "exact_native_pages": len(matches),
            "expected_source_pages": len(auditable_source_runs),
            "scoped_native_pages": len(scoped_native_pages),
            "ordered_exact": ordered_exact,
            "unmatched_source_runs": unmatched_source_runs,
            "unmatched_native_pages": unmatched_native_pages,
            "matches": matches,
        },
        "smarty": {
            "source_complete_runs": source_smarty_runs,
            "native_pages": native_smarty_pages,
            "unmatched_native_pages": unmatched_smarty_pages,
            "unmatched_source_runs": unmatched_source_smarty_runs,
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        initialization_matches,
        capture_artifacts_verified,
        ordered_exact,
        not unmatched_source_runs,
        not unmatched_native_pages,
        final_state_matches,
        feedback_matches,
        not unmatched_smarty_pages,
        not unmatched_source_smarty_runs,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
