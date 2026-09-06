#!/usr/bin/env python3
"""Pixel-reconcile non-LCS pages on the captured restarted fourth Demo board."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common
import audit_number_demo_first_board_continuous_sequence as alignment
import audit_number_demo_fourth_board_continuous_sequence as boardwide


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-fourth-board-reconciliation-report.json"
)
EXPECTED_SOURCE_ONLY_INDEXES = {
    1, 7, 14, 19, 36, 42, 44, 50, 58, 61, 63, 70, 86, 92, 98,
}
EXPECTED_NATIVE_ONLY_INDEXES: set[int] = set()


def read_ppm(path: Path) -> np.ndarray:
    magic, dimensions, maximum, rgb = path.read_bytes().split(b"\n", 3)
    if magic != b"P6" or dimensions != b"320 200" or maximum != b"255":
        raise ValueError(f"unexpected PPM header: {path}")
    if len(rgb) != common.LOGICAL_WIDTH * common.LOGICAL_HEIGHT * 3:
        raise ValueError(f"unexpected PPM payload length: {path}")
    return np.frombuffer(rgb, dtype=np.uint8).reshape(
        common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3
    )


def bbox(mask: np.ndarray) -> list[int] | None:
    rows, columns = np.where(mask)
    if not len(rows):
        return None
    return [
        int(columns.min()), int(rows.min()),
        int(columns.max()), int(rows.max()),
    ]


def partition(
    partial: np.ndarray, first: np.ndarray, second: np.ndarray,
) -> dict[str, object]:
    first_match = np.all(partial == first, axis=2)
    second_match = np.all(partial == second, axis=2)
    neither = ~first_match & ~second_match
    return {
        "first_only": int(np.count_nonzero(first_match & ~second_match)),
        "first_only_bbox": bbox(first_match & ~second_match),
        "second_only": int(np.count_nonzero(second_match & ~first_match)),
        "second_only_bbox": bbox(second_match & ~first_match),
        "common": int(np.count_nonzero(first_match & second_match)),
        "neither": int(np.count_nonzero(neither)),
        "neither_bbox": bbox(neither),
    }


def best_partition(
    partial: np.ndarray, pages: list[np.ndarray],
    first_candidate: int, last_candidate: int,
) -> tuple[int, int, dict[str, object]]:
    best: tuple[int, int, dict[str, object]] | None = None
    best_key: tuple[int, int, int, int] | None = None
    for first in range(first_candidate, last_candidate + 1):
        for second in range(first, last_candidate + 1):
            result = partition(partial, pages[first], pages[second])
            key = (result["neither"], second - first, first, second)
            if best is None or best_key is None or key < best_key:
                best = (first, second, result)
                best_key = key
    if best is None:
        raise ValueError("empty page candidate range")
    return best


def main() -> None:
    common.WINDOW_FIRST_FRAME = boardwide.FIRST_SOURCE_FRAME
    common.WINDOW_LAST_FRAME = boardwide.LAST_SOURCE_FRAME
    source_runs, source_window = common.decode_window(CAPTURE)
    source_hashes = [common.renderer_fnv64(run.rgb) for run in source_runs]
    source_pages = [
        np.frombuffer(run.rgb, dtype=np.uint8).reshape(
            common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3
        )
        for run in source_runs
    ]

    with tempfile.TemporaryDirectory(prefix="number-board4-pages-") as temporary:
        dump_directory = Path(temporary)
        native_hashes, native_starts, native_execution = (
            boardwide.read_native_sequence(dump_directory)
        )
        pages_by_index: dict[int, np.ndarray] = {}
        for path in dump_directory.glob("*.ppm"):
            index = int(path.stem.rsplit("-", 1)[1])
            pages_by_index[index] = read_ppm(path)
        native_pages = [pages_by_index[index] for index in range(len(native_hashes))]

    pairs = alignment.lcs_pairs(native_hashes, source_hashes)
    source_to_native = {source: native for native, source in pairs}
    native_to_source = {native: source for native, source in pairs}
    matched_source = set(source_to_native)
    matched_native = set(native_to_source)
    source_only = [
        index for index in range(len(source_runs)) if index not in matched_source
    ]
    native_only = [
        index for index in range(len(native_hashes)) if index not in matched_native
    ]

    source_artifacts = []
    for source_index in source_only:
        previous_source = max(index for index in matched_source if index < source_index)
        following_source = min(index for index in matched_source if index > source_index)
        first_candidate = source_to_native[previous_source]
        last_candidate = source_to_native[following_source]
        first_page, second_page, result = best_partition(
            source_pages[source_index], native_pages,
            first_candidate, last_candidate,
        )
        source_artifacts.append({
            "source_run_index": source_index,
            "source_frame_start": source_runs[source_index].start,
            "source_frame_end": source_runs[source_index].end,
            "source_renderer_fnv64": f"0x{source_hashes[source_index]:016x}",
            "nonuniform_2x_blocks": source_runs[source_index].nonuniform_blocks,
            "has_uniform_frame": source_runs[source_index].has_uniform_frame,
            "candidate_native_run_start": first_candidate,
            "candidate_native_run_end": last_candidate,
            "best_native_pair": [first_page, second_page],
            "best_native_pair_renderer_fnv64": [
                f"0x{native_hashes[first_page]:016x}",
                f"0x{native_hashes[second_page]:016x}",
            ],
            **result,
        })

    native_artifacts = []
    for native_index in native_only:
        previous_native = max(index for index in matched_native if index < native_index)
        following_native = min(index for index in matched_native if index > native_index)
        first_candidate = native_to_source[previous_native]
        last_candidate = native_to_source[following_native]
        first_page, second_page, result = best_partition(
            native_pages[native_index], source_pages,
            first_candidate, last_candidate,
        )
        native_artifacts.append({
            "native_run_index": native_index,
            "native_frame_start": native_starts[native_index],
            "native_renderer_fnv64": f"0x{native_hashes[native_index]:016x}",
            "candidate_source_run_start": first_candidate,
            "candidate_source_run_end": last_candidate,
            "best_source_pair": [first_page, second_page],
            "best_source_pair_renderer_fnv64": [
                f"0x{source_hashes[first_page]:016x}",
                f"0x{source_hashes[second_page]:016x}",
            ],
            **result,
        })

    source_sha256 = common.sha256_file(CAPTURE)
    source_indexes_match = set(source_only) == EXPECTED_SOURCE_ONLY_INDEXES
    native_indexes_match = set(native_only) == EXPECTED_NATIVE_ONLY_INDEXES
    report = {
        "schema": "number-fourth-demo-board-reconciliation-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "source_window": source_window,
        "native_execution": native_execution,
        "exact_lcs_run_count": len(pairs),
        "source_only_artifacts": source_artifacts,
        "native_only_artifacts": native_artifacts,
        "checks": {
            "source_only_indexes_match": source_indexes_match,
            "native_only_indexes_match": native_indexes_match,
        },
        "classification": {
            "continuous_gameplay_parity_claim": True,
            "scope": [boardwide.FIRST_SOURCE_FRAME, boardwide.LAST_SOURCE_FRAME],
            "reason": (
                "all 86 native changed pages are exact ordered source pages; "
                "eleven physically nonuniform refreshes and four uniform "
                "incomplete dirty-painter states remain source-only, with no "
                "native-only flicker or collision composite"
            ),
        },
    }
    nonuniform_artifact_count = sum(
        not item["has_uniform_frame"] for item in source_artifacts
    )
    uniform_artifacts = {
        item["source_run_index"]: item for item in source_artifacts
        if item["has_uniform_frame"]
    }
    uniform_artifacts_match = all((
        set(uniform_artifacts) == {50, 58, 61, 92},
        all(uniform_artifacts[index]["neither"] == 0 for index in (50, 58, 92)),
        uniform_artifacts[61]["neither"] == 46,
        uniform_artifacts[61]["neither_bbox"] == [80, 43, 102, 45],
    ))
    report["checks"].update({
        "nonuniform_source_artifact_count": nonuniform_artifact_count,
        "uniform_source_artifacts_match": uniform_artifacts_match,
        "all_native_pages_matched": len(matched_native) == len(native_hashes),
    })
    report["valid"] = all((
        report["source_matches"], native_execution["test_passed"],
        source_indexes_match, native_indexes_match, len(pairs) == 86,
        nonuniform_artifact_count == 11, uniform_artifacts_match,
        len(matched_native) == len(native_hashes),
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "valid": report["valid"],
        "exact_lcs": len(pairs),
        "source_only_pages": len(source_artifacts),
        "native_only_pages": [
            {
                "native_run_index": item["native_run_index"],
                "best_source_pair": item["best_source_pair"],
                "counts": [
                    item["first_only"], item["second_only"],
                    item["common"], item["neither"],
                ],
                "first_only_bbox": item["first_only_bbox"],
                "second_only_bbox": item["second_only_bbox"],
                "neither_bbox": item["neither_bbox"],
            }
            for item in native_artifacts
        ],
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
