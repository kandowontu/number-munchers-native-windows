#!/usr/bin/env python3
"""Pixel-reconcile every non-LCS page on the captured third Demo board."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common
import audit_number_demo_first_board_continuous_sequence as alignment
import audit_number_demo_third_board_continuous_sequence as boardwide


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-third-board-reconciliation-report.json"
)
EXPECTED_SOURCE_ONLY_INDEXES = {
    4, 32, 38, 41, 47, 54, 65, 73, 84, 89, 103, 108,
}
EXPECTED_PARTITIONS = {
    4: ([3, 4], 136, 141, 63_669, 54, [116, 71, 144, 75]),
    32: ([30, 31], 257, 90, 63_651, 2, [212, 78, 212, 79]),
    38: ([35, 36], 196, 44, 63_757, 3, [212, 82, 212, 84]),
    41: ([37, 38], 65, 280, 63_655, 0, None),
    47: ([42, 43], 104, 136, 63_760, 0, None),
    54: ([48, 49], 275, 35, 63_687, 3, [260, 82, 260, 84]),
    65: ([58, 59], 245, 122, 63_633, 0, None),
    73: ([65, 66], 9, 20, 63_953, 18, [272, 159, 291, 160]),
    84: ([75, 76], 11, 147, 63_842, 0, None),
    89: ([79, 79], 0, 0, 63_978, 22, [269, 53, 293, 53]),
    103: ([92, 93], 522, 65, 63_413, 0, None),
    108: ([96, 97], 16, 226, 63_758, 0, None),
}


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
        "second_only": int(np.count_nonzero(second_match & ~first_match)),
        "common": int(np.count_nonzero(first_match & second_match)),
        "neither": int(np.count_nonzero(neither)),
        "neither_bbox": bbox(neither),
    }


def best_partition(
    partial: np.ndarray, native_pages: list[np.ndarray],
    first_candidate: int, last_candidate: int,
) -> tuple[int, int, dict[str, object]]:
    best: tuple[int, int, dict[str, object]] | None = None
    best_key: tuple[int, int, int, int] | None = None
    for first in range(first_candidate, last_candidate + 1):
        for second in range(first, last_candidate + 1):
            result = partition(partial, native_pages[first], native_pages[second])
            key = (result["neither"], second - first, first, second)
            if best is None or best_key is None or key < best_key:
                best = (first, second, result)
                best_key = key
    if best is None:
        raise ValueError("empty native candidate range")
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

    with tempfile.TemporaryDirectory(prefix="number-board3-pages-") as temporary:
        dump_directory = Path(temporary)
        native_hashes, native_starts, native_execution = (
            boardwide.read_native_sequence(dump_directory)
        )
        pages_by_index: dict[int, np.ndarray] = {}
        for path in dump_directory.glob("*.ppm"):
            index = int(path.stem.rsplit("-", 1)[1])
            pages_by_index[index] = read_ppm(path)
        native_pages = [
            pages_by_index[index] for index in range(len(native_hashes))
        ]

    pairs = alignment.lcs_pairs(native_hashes, source_hashes)
    source_to_native = {source: native for native, source in pairs}
    matched_source = set(source_to_native)
    matched_native = {native for native, _ in pairs}
    source_only = [
        index for index in range(len(source_runs)) if index not in matched_source
    ]
    native_only = [
        index for index in range(len(native_hashes)) if index not in matched_native
    ]

    artifacts = []
    for source_index in source_only:
        previous_source = max(
            index for index in matched_source if index < source_index
        )
        following_source = min(
            index for index in matched_source if index > source_index
        )
        first_candidate = source_to_native[previous_source]
        last_candidate = source_to_native[following_source]
        first_native, second_native, result = best_partition(
            source_pages[source_index], native_pages,
            first_candidate, last_candidate,
        )
        artifacts.append({
            "source_run_index": source_index,
            "source_frame_start": source_runs[source_index].start,
            "source_frame_end": source_runs[source_index].end,
            "source_renderer_fnv64": f"0x{source_hashes[source_index]:016x}",
            "nonuniform_2x_blocks": source_runs[source_index].nonuniform_blocks,
            "has_uniform_frame": source_runs[source_index].has_uniform_frame,
            "candidate_native_run_start": first_candidate,
            "candidate_native_run_end": last_candidate,
            "best_native_pair": [first_native, second_native],
            "best_native_pair_renderer_fnv64": [
                f"0x{native_hashes[first_native]:016x}",
                f"0x{native_hashes[second_native]:016x}",
            ],
            **result,
            "classification": (
                "uniform incomplete adjacent-page dirty painter"
                if source_runs[source_index].has_uniform_frame
                else "nonuniform 2x scanout/dirty refresh"
            ),
            "absent_from_native": source_hashes[source_index] not in native_hashes,
        })

    source_sha256 = common.sha256_file(CAPTURE)
    source_indexes_match = set(source_only) == EXPECTED_SOURCE_ONLY_INDEXES
    all_artifacts_absent = all(item["absent_from_native"] for item in artifacts)
    actual_partitions = {
        item["source_run_index"]: (
            item["best_native_pair"], item["first_only"],
            item["second_only"], item["common"], item["neither"],
            item["neither_bbox"],
        )
        for item in artifacts
    }
    artifact_partitions_match = actual_partitions == EXPECTED_PARTITIONS
    report = {
        "schema": "number-third-demo-board-reconciliation-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "source_window": source_window,
        "native_execution": native_execution,
        "exact_lcs_run_count": len(pairs),
        "source_only_artifacts": artifacts,
        "native_only_pages": native_only,
        "checks": {
            "source_only_indexes_match": source_indexes_match,
            "all_source_artifacts_absent_from_native": all_artifacts_absent,
            "no_native_only_pages": not native_only,
            "artifact_partitions_match": artifact_partitions_match,
        },
        "classification": {
            "continuous_gameplay_parity_claim": True,
            "scope": [boardwide.FIRST_SOURCE_FRAME, boardwide.LAST_SOURCE_FRAME],
            "reason": (
                "all 106 presentation-complete native pages match the source "
                "in order, including the corrected phase-5 restored board; "
                "the twelve remaining source pages have exact locked partial-"
                "refresh partitions and there are no native-only pages"
            ),
        },
    }
    report["valid"] = all((
        report["source_matches"], native_execution["test_passed"],
        source_indexes_match, all_artifacts_absent, not native_only,
        artifact_partitions_match, len(pairs) == 106,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "valid": report["valid"],
        "exact_lcs": len(pairs),
        "classified_source_artifacts": len(artifacts),
        "native_only_pages": len(native_only),
        "artifact_partitions": [
            {
                "source_run_index": item["source_run_index"],
                "best_native_pair": item["best_native_pair"],
                "counts": [
                    item["first_only"], item["second_only"],
                    item["common"], item["neither"],
                ],
                "neither_bbox": item["neither_bbox"],
            }
            for item in artifacts
        ],
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
