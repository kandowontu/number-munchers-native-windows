#!/usr/bin/env python3
"""Reconcile every non-LCS page in the captured second-Demo lifecycle."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common
import audit_number_demo_first_board_continuous_sequence as alignment
import audit_number_demo_second_board_continuous_sequence as boardwide


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-second-board-reconciliation-report.json"
)

SOURCE_CLASSIFICATIONS = {
    5: "incomplete player-movement dirty repaint",
    32: "board-to-Hall scanout refresh",
    34: "board-to-Hall scanout refresh",
    36: "board-to-Hall scanout refresh",
    38: "Hall-to-logo scanout refresh",
    40: "Hall-to-logo scanout refresh",
    41: "Hall-to-logo scanout refresh",
    43: "Hall-to-logo scanout refresh",
    45: "logo-to-board scanout refresh",
    47: "logo-to-board scanout refresh",
    48: "logo-to-board scanout refresh",
    50: "logo-to-board partial painter",
    51: "logo-to-board partial painter",
}

NATIVE_CLASSIFICATIONS = {
    31: "complete board-to-Hall Wipe stage",
    33: "complete board-to-Hall Wipe stage",
    35: "complete board-to-Hall Wipe stage",
    37: "complete Hall-to-logo Wipe stage",
    39: "complete Hall-to-logo Wipe stage",
    40: "complete Hall-to-logo Wipe stage",
    42: "complete Hall-to-logo Wipe stage",
    44: "complete logo-to-board Wipe stage",
    46: "complete logo-to-board Wipe stage",
    48: "complete logo-to-board painter stage",
    49: "complete logo-to-board painter stage",
}

EXPECTED_PARTITIONS = {
    5:  ([4, 5], 196, 195, 63_606, 3, [91, 105, 93, 105]),
    32: ([30, 31], 309, 128, 63_563, 0, None),
    34: ([32, 33], 20, 36_328, 27_652, 0, None),
    36: ([34, 35], 320, 43_846, 19_520, 314, [0, 74, 319, 191]),
    38: ([37, 38], 21_899, 12_020, 30_081, 0, None),
    40: ([39, 40], 43_474, 13_340, 7_186, 0, None),
    41: ([40, 41], 921, 2_288, 60_791, 0, None),
    43: ([42, 43], 43_842, 15_495, 4_663, 0, None),
    45: ([44, 45], 44_510, 3_756, 15_734, 0, None),
    47: ([45, 46], 51_657, 274, 12_069, 0, None),
    48: ([46, 47], 10_088, 1_981, 51_931, 0, None),
    50: ([48, 49], 5_125, 516, 58_359, 0, None),
    51: ([48, 49], 310, 5_331, 58_359, 0, None),
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


def best_native_partition(
    partial: np.ndarray, native_pages: list[np.ndarray],
    first_candidate: int, last_candidate: int,
) -> tuple[int, int, dict[str, object]]:
    best: tuple[int, int, dict[str, object]] | None = None
    for first in range(first_candidate, last_candidate + 1):
        for second in range(first, last_candidate + 1):
            result = partition(partial, native_pages[first], native_pages[second])
            key = (result["neither"], second - first, first, second)
            if best is None:
                best = (first, second, result)
                best_key = key
            elif key < best_key:
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

    with tempfile.TemporaryDirectory(prefix="number-board2-pages-") as temporary:
        dump_directory = Path(temporary)
        native_hashes, native_starts, native_execution = (
            boardwide.read_native_sequence(dump_directory)
        )
        native_pages_by_index: dict[int, np.ndarray] = {}
        for path in dump_directory.glob("*.ppm"):
            index = int(path.stem.rsplit("-", 1)[1])
            native_pages_by_index[index] = read_ppm(path)
        native_pages = [
            native_pages_by_index[index] for index in range(len(native_hashes))
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
        previous_native = source_to_native[previous_source]
        first_candidate = previous_native
        last_candidate = source_to_native[following_source]
        first_native, second_native, result = best_native_partition(
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
            "immediately_previous_native_run": previous_native,
            "best_native_pair": [first_native, second_native],
            "best_native_pair_renderer_fnv64": [
                f"0x{native_hashes[first_native]:016x}",
                f"0x{native_hashes[second_native]:016x}",
            ],
            **result,
            "classification": SOURCE_CLASSIFICATIONS.get(
                source_index, "unclassified"
            ),
            "absent_from_native": source_hashes[source_index] not in native_hashes,
        })

    native_reconstructed = [{
        "native_run_index": index,
        "native_presentation_frame": native_starts[index],
        "renderer_fnv64": f"0x{native_hashes[index]:016x}",
        "classification": NATIVE_CLASSIFICATIONS.get(index, "unclassified"),
    } for index in native_only]

    source_indexes_match = set(source_only) == set(SOURCE_CLASSIFICATIONS)
    native_indexes_match = set(native_only) == set(NATIVE_CLASSIFICATIONS)
    all_artifacts_absent = all(item["absent_from_native"] for item in artifacts)
    all_artifacts_classified = all(
        item["classification"] != "unclassified" for item in artifacts
    )
    all_native_classified = all(
        item["classification"] != "unclassified"
        for item in native_reconstructed
    )
    actual_partitions = {
        item["source_run_index"]: (
            item["best_native_pair"], item["first_only"],
            item["second_only"], item["common"], item["neither"],
            item["neither_bbox"],
        )
        for item in artifacts
    }
    artifact_partitions_match = actual_partitions == EXPECTED_PARTITIONS
    source_sha256 = common.sha256_file(CAPTURE)
    report = {
        "schema": "number-second-demo-board-reconciliation-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "source_window": source_window,
        "native_execution": native_execution,
        "exact_lcs_run_count": len(pairs),
        "source_only_artifacts": artifacts,
        "native_only_reconstructed_pages": native_reconstructed,
        "checks": {
            "source_only_indexes_match": source_indexes_match,
            "native_only_indexes_match": native_indexes_match,
            "all_source_artifacts_absent_from_native": all_artifacts_absent,
            "all_source_artifacts_classified": all_artifacts_classified,
            "all_native_pages_classified": all_native_classified,
            "artifact_partitions_match": artifact_partitions_match,
        },
        "classification": {
            "continuous_lifecycle_parity_claim": True,
            "scope": [boardwide.FIRST_SOURCE_FRAME, boardwide.LAST_SOURCE_FRAME],
            "reason": (
                "all presentation-complete source pages match in order; the "
                "remaining source pages are classified capture refreshes or "
                "partial painters, and every native-only page is an explicitly "
                "gated completed Wipe/painter surface"
            ),
        },
    }
    report["valid"] = all((
        report["source_matches"], native_execution["test_passed"],
        source_indexes_match, native_indexes_match, all_artifacts_absent,
        all_artifacts_classified, all_native_classified,
        artifact_partitions_match, len(pairs) == 40,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "valid": report["valid"],
        "exact_lcs": len(pairs),
        "classified_source_artifacts": len(artifacts),
        "classified_native_reconstructed_pages": len(native_reconstructed),
        "artifact_partitions": [
            {
                "source_run_index": item["source_run_index"],
                "best_native_pair": item["best_native_pair"],
                "neither": item["neither"],
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
