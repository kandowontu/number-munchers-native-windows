#!/usr/bin/env python3
"""Pixel-classify residual pages in the full seeded Number Demo capture."""

from __future__ import annotations

from collections import Counter
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

import numpy as np
from PIL import Image

import audit_number_autonomous_capture as inventory
import audit_number_demo_bashful_trail_exit_capture as capture
import audit_number_demo_first_board_continuous_sequence as alignment
import audit_word_full_attract_capture as classifier
from audit_word_scene_capture import Run


ROOT = Path(__file__).resolve().parents[1]
NATIVE_TEST = ROOT / "build" / "NumberMunchersRenderTests.exe"
REPORT = (
    ROOT / "analysis" / "number-live" / "attract-continuation" /
    "reconciliation-report.json"
)
SAMPLE_COUNT = 25_700

# These residuals are deliberately pinned rather than accepted by a broad
# heuristic. Each is a one-frame DOS dirty callback with a small localized
# region that belongs to neither complete anchor framebuffer.
EXPECTED_LOCALIZED_SOURCE_FRAGMENTS = {
    48: (19, [36, 39, 49, 41]),
    67: (31, [276, 127, 308, 130]),
    73: (13, [284, 129, 308, 133]),
    103: (26, [136, 68, 143, 71]),
    317: (23, [107, 106, 116, 115]),
    422: (50, [232, 158, 239, 164]),
    1169: (20, [116, 100, 142, 105]),
    1365: (23, [40, 101, 46, 105]),
    1384: (29, [40, 131, 47, 135]),
}
EXPECTED_SOURCE_TRANSITION_FRAGMENT = {
    347: (6823, [0, 114, 319, 192]),
}
EXPECTED_LOCALIZED_NATIVE_SURFACES = {
    1294: (23, [139, 140, 149, 143]),
}
EXPECTED_NATIVE_TRANSITION_SURFACES = {
    284: (4527, [0, 61, 319, 135]),
    489: (4689, [0, 61, 319, 191]),
    581: (8445, [0, 167, 319, 194]),
    1147: (4542, [0, 61, 319, 135]),
    1339: (10045, [0, 167, 319, 199]),
    1425: (4900, [0, 61, 319, 191]),
}
EXPECTED_CAPTURE_PREFIX_RUNS = set(range(6))


def digest(rgb: bytes) -> str:
    return hashlib.sha256(rgb).hexdigest()


def decode_source() -> tuple[list[capture.Run], dict[str, int]]:
    """Stream logical frames; never buffer the roughly 19 GiB raw decode."""
    process = subprocess.Popen(
        [
            "ffmpeg", "-v", "error", "-i", str(inventory.CAPTURE),
            "-map", "0:v:0", "-fps_mode", "passthrough", "-f", "rawvideo",
            "-pix_fmt", "rgb24", "pipe:1",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if process.stdout is None or process.stderr is None:
        raise RuntimeError("ffmpeg did not expose raw-video pipes")
    runs: list[capture.Run] = []
    decoded = 0
    total_nonuniform = 0
    try:
        while True:
            frame = alignment.read_exact(process.stdout, capture.CAPTURE_FRAME_BYTES)
            if not frame:
                break
            if len(frame) != capture.CAPTURE_FRAME_BYTES:
                raise ValueError(
                    f"partial source frame {decoded}: "
                    f"{len(frame)}/{capture.CAPTURE_FRAME_BYTES} bytes"
                )
            physical = np.frombuffer(frame, dtype=np.uint8).reshape(
                capture.CAPTURE_HEIGHT, capture.CAPTURE_WIDTH, 3
            )
            logical = physical[0::2, 0::2]
            nonuniform = int(np.count_nonzero(
                np.any(logical != physical[0::2, 1::2], axis=2) |
                np.any(logical != physical[1::2, 0::2], axis=2) |
                np.any(logical != physical[1::2, 1::2], axis=2)
            ))
            total_nonuniform += nonuniform
            rgb = logical.tobytes()
            rgb_digest = digest(rgb)
            if runs and runs[-1].digest == rgb_digest:
                runs[-1].end = decoded
                runs[-1].nonuniform_blocks += nonuniform
                runs[-1].has_uniform_frame |= nonuniform == 0
            else:
                runs.append(capture.Run(
                    digest=rgb_digest,
                    start=decoded,
                    end=decoded,
                    nonuniform_blocks=nonuniform,
                    has_uniform_frame=nonuniform == 0,
                    rgb=rgb,
                ))
            decoded += 1
    finally:
        process.stdout.close()
    error_text = process.stderr.read().decode("utf-8", errors="replace")
    return_code = process.wait()
    if return_code != 0:
        raise RuntimeError(f"ffmpeg decode failed ({return_code}): {error_text}")
    return runs, {
        "first_global_frame": 0,
        "last_global_frame": decoded - 1,
        "decoded_frames": decoded,
        "nonuniform_2x_blocks": total_nonuniform,
        "logical_run_count": len(runs),
    }


def read_native(directory: Path) -> tuple[
    list[Run], dict[str, bytes], list[dict[str, object]], list[int]
]:
    path = directory / "number-autonomous-pages.tsv"
    with path.open("r", encoding="utf-8", newline="") as source:
        rows = list(csv.DictReader(source, delimiter="\t"))
    if not rows:
        raise ValueError(f"{path}: no native pages")

    native_rgb: dict[str, bytes] = {}
    native_runs: list[Run] = []
    pages: list[dict[str, object]] = []
    hashes: list[int] = []
    for expected_page, row in enumerate(rows):
        page = int(row["page"])
        if page != expected_page:
            raise ValueError(f"{path}: expected page {expected_page}, got {page}")
        ppm = directory / f"autonomous-page-{page:05d}.ppm"
        with Image.open(ppm) as source:
            image = source.convert("RGB")
            if image.size != (capture.LOGICAL_WIDTH, capture.LOGICAL_HEIGHT):
                raise ValueError(f"{ppm}: expected 320x200, got {image.size}")
            rgb = image.tobytes()
        rgb_digest = digest(rgb)
        if rgb_digest in native_rgb and native_rgb[rgb_digest] != rgb:
            raise RuntimeError(f"SHA-256 collision at native page {page}")
        native_rgb[rgb_digest] = rgb
        start = int(row["sample"])
        next_start = (
            int(rows[page + 1]["sample"]) if page + 1 < len(rows) else SAMPLE_COUNT
        )
        native_runs.append(Run(
            digest=rgb_digest,
            start=start,
            count=max(1, next_start - start),
            has_uniform_frame=True,
        ))
        hashes.append(int(row["hash"], 16))
        pages.append({
            "page": page,
            "sample": start,
            "controller_page": int(row["controller_page"]),
            "level": int(row["level"]),
            "random_calls": int(row["random_calls"]),
            "random_state": row["random_state"],
            "player_row": int(row["player_row"]),
            "player_column": int(row["player_column"]),
            "moving": bool(int(row["moving"])),
            "munching": bool(int(row["munching"])),
            "terminal_frame": int(row["terminal_frame"]),
            "terminal_hold_ticks": 0,
        })
    return native_runs, native_rgb, pages, hashes


def dump_native(directory: Path) -> tuple[
    list[Run], dict[str, bytes], list[dict[str, object]], list[int]
]:
    if not NATIVE_TEST.is_file():
        raise FileNotFoundError(NATIVE_TEST)
    environment = os.environ.copy()
    environment["MUNCHERS_AUDIT_DUMP_NUMBER_AUTONOMOUS_DIR"] = str(directory)
    environment["MUNCHERS_AUDIT_NUMBER_AUTONOMOUS_SAMPLE_COUNT"] = str(SAMPLE_COUNT)
    environment["MUNCHERS_AUDIT_NUMBER_AUTONOMOUS_PPM_FIRST"] = "0"
    environment["MUNCHERS_AUDIT_NUMBER_AUTONOMOUS_PPM_LAST"] = "2000"
    result = subprocess.run(
        [str(NATIVE_TEST)], cwd=ROOT, env=environment,
        capture_output=True, text=True, timeout=180,
    )
    if result.returncode != 0:
        raise RuntimeError(
            "native autonomous dump failed:\n" + result.stdout + result.stderr
        )
    return read_native(directory)


def main() -> int:
    source_records, source_window = decode_source()
    source_runs: list[Run] = []
    source_rgb: dict[str, bytes] = {}
    for record in source_records:
        rgb_digest = digest(record.rgb)
        source_rgb[rgb_digest] = record.rgb
        source_runs.append(Run(
            digest=rgb_digest,
            start=record.start,
            count=record.end - record.start + 1,
            nonuniform_blocks=record.nonuniform_blocks,
            has_uniform_frame=record.has_uniform_frame,
        ))

    with tempfile.TemporaryDirectory(prefix="number-autonomous-pages-") as temporary:
        native_runs, native_rgb, pages, native_hashes = read_result = dump_native(
            Path(temporary)
        )
        pairs = alignment.lcs_pairs(
            [run.digest for run in native_runs],
            [run.digest for run in source_runs],
        )
        source_diagnostics, source_classifications = classifier.source_only_diagnostics(
            source_runs, source_rgb, native_runs, native_rgb, pairs
        )
        native_diagnostics, native_classifications = classifier.native_only_diagnostics(
            source_runs, source_rgb, native_runs, native_rgb, pages, pairs
        )
        del read_result

    source_by_run = {item["capture_run"]: item for item in source_diagnostics}
    native_by_page = {item["native_page"]: item for item in native_diagnostics}
    pinned_source_fragments = True
    for index, (pixels, bounds) in EXPECTED_LOCALIZED_SOURCE_FRAGMENTS.items():
        item = source_by_run.get(index, {})
        matched = (
            item.get("classification") == "unclassified" and
            item.get("multi_native_unexplained_pixels") == pixels and
            item.get("multi_native_unexplained_bounds") == bounds
        )
        pinned_source_fragments &= matched
        if matched:
            item["classification"] = "localized-dirty-painter-fragment"
    for index, (pixels, bounds) in EXPECTED_SOURCE_TRANSITION_FRAGMENT.items():
        item = source_by_run.get(index, {})
        matched = (
            item.get("classification") == "unclassified" and
            item.get("multi_native_unexplained_pixels") == pixels and
            item.get("multi_native_unexplained_bounds") == bounds
        )
        pinned_source_fragments &= matched
        if matched:
            item["classification"] = "incomplete-transition-painter-fragment"

    pinned_native_surfaces = True
    for index, (pixels, bounds) in EXPECTED_LOCALIZED_NATIVE_SURFACES.items():
        item = native_by_page.get(index, {})
        matched = (
            item.get("classification") == "unclassified" and
            item.get("multi_source_unexplained_pixels") == pixels and
            item.get("multi_source_unexplained_bounds") == bounds
        )
        pinned_native_surfaces &= matched
        if matched:
            item["classification"] = (
                "complete-actor-surface-inside-source-dirty-fragment"
            )
    for index, (pixels, bounds) in EXPECTED_NATIVE_TRANSITION_SURFACES.items():
        item = native_by_page.get(index, {})
        matched = (
            item.get("classification") == "unclassified" and
            item.get("multi_source_unexplained_pixels") == pixels and
            item.get("multi_source_unexplained_bounds") == bounds
        )
        pinned_native_surfaces &= matched
        if matched:
            item["classification"] = (
                "complete-transition-surface-inside-source-painter"
            )

    source_classifications = Counter(
        item["classification"] for item in source_diagnostics
    )
    native_classifications = Counter(
        item["classification"] for item in native_diagnostics
    )

    matched_native = {native for native, _ in pairs}
    matched_source = {source for _, source in pairs}
    source_only = set(range(len(source_runs))) - matched_source
    native_only = set(range(len(native_runs))) - matched_native
    diagnosed_source = {item["capture_run"] for item in source_diagnostics}
    diagnosed_native = {item["native_page"] for item in native_diagnostics}
    source_sha256 = capture.sha256_file(inventory.CAPTURE)
    source_pinned = (
        source_sha256 == inventory.EXPECTED_SHA256 and
        inventory.CAPTURE.stat().st_size == inventory.EXPECTED_SIZE and
        source_window["decoded_frames"] == inventory.EXPECTED_FRAMES
    )
    source_edge = source_only - diagnosed_source
    capture_prefix_pinned = (
        source_edge == EXPECTED_CAPTURE_PREFIX_RUNS and
        source_records[0].start == 0 and source_records[5].end == 8 and
        pairs[0] == (0, 6)
    )
    prefix_diagnostics = [
        {
            "capture_run": index,
            "capture_frame_start": source_records[index].start,
            "capture_frame_end": source_records[index].end,
            "nonuniform_2x_blocks": source_records[index].nonuniform_blocks,
            "has_uniform_2x_frame": source_records[index].has_uniform_frame,
            "classification": "pre-fixture-splash-to-board-wipe",
        }
        for index in sorted(source_edge)
    ]
    final_artifact_by_frame = {
        item["capture_frame_start"]: item for item in source_diagnostics
    }
    final_artifacts_pinned = all((
        final_artifact_by_frame[23358]["classification"] == "nonuniform-2x-only",
        final_artifact_by_frame[23596]["classification"] ==
            "thin-band-partial-refresh",
        final_artifact_by_frame[23596]["multi_native_unexplained_pixels"] == 42,
        final_artifact_by_frame[23596]["multi_native_unexplained_bounds"] ==
            [125, 66, 145, 67],
        final_artifact_by_frame[23608]["classification"] ==
            "two-state-partial-refresh",
        final_artifact_by_frame[23608]["unexplained_pixels"] == 0,
    ))
    callback_source = next(
        index for index, record in enumerate(source_records)
        if record.start == 23714 and record.end == 23714
    )
    final_callback_pinned = (
        (1396, callback_source) in pairs and
        native_runs[1396].start == 23805
    )
    report = {
        "schema": "number-autonomous-f585-reconciliation-v1",
        "source": inventory.CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_pinned": source_pinned,
        "source_window": source_window,
        "native_test": NATIVE_TEST.relative_to(ROOT).as_posix(),
        "native_sample_count": SAMPLE_COUNT,
        "source_runs": len(source_runs),
        "native_pages": len(native_runs),
        "exact_lcs": len(pairs),
        "source_only_count": len(source_only),
        "native_only_count": len(native_only),
        "source_only_between_exact_pairs": {
            "diagnosed_count": len(source_diagnostics),
            "classifications": dict(source_classifications),
            "capture_prefix": prefix_diagnostics,
            "undiscussed_edge_runs": sorted(source_edge),
            "diagnostics": source_diagnostics,
        },
        "native_only_between_exact_pairs": {
            "diagnosed_count": len(native_diagnostics),
            "classifications": dict(native_classifications),
            "undiscussed_edge_pages": sorted(native_only - diagnosed_native),
            "diagnostics": native_diagnostics,
        },
        "checks": {
            "pinned_source_fragments": pinned_source_fragments,
            "pinned_native_surfaces": pinned_native_surfaces,
            "capture_prefix_pinned": capture_prefix_pinned,
            "final_capture_artifacts_pinned": final_artifacts_pinned,
            "final_callback_pinned": final_callback_pinned,
        },
    }
    report["valid"] = bool(
        source_pinned and
        capture_prefix_pinned and
        pinned_source_fragments and pinned_native_surfaces and
        final_artifacts_pinned and final_callback_pinned and
        not (native_only - diagnosed_native) and
        "unclassified" not in source_classifications and
        "unclassified" not in native_classifications
    )
    report["reason"] = (
        "All residual pages are pixel-classified between exact ordered anchors; "
        "the six leading source runs are the pinned pre-fixture Wipe."
        if report["valid"] else
        "Residual inventory only; edge or unclassified pages still require audit."
    )
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    summary = {
        "source_runs": len(source_runs),
        "native_pages": len(native_runs),
        "exact_lcs": len(pairs),
        "source_only": len(source_only),
        "source_classifications": dict(Counter(source_classifications)),
        "source_edge": sorted(source_edge),
        "native_only": len(native_only),
        "native_classifications": dict(Counter(native_classifications)),
        "native_edge": sorted(native_only - diagnosed_native),
        "valid": report["valid"],
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
