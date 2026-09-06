#!/usr/bin/env python3
"""Pixel-reconcile non-LCS pages on the continuous Factors-of-63 Demo board."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common
import audit_number_demo_first_board_continuous_sequence as alignment
import audit_number_demo_fifth_board_continuous_sequence as boardwide
import audit_number_demo_fourth_board_reconciliation as pixels


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-fifth-board-reconciliation-report.json"
)
EXPECTED_SOURCE_ONLY_INDEXES = {
    7, 18, 35, 48, 54, 57, 70, 73, 77, 83, 85, 86, 89, 95,
    114, 123, 129, 141, 148, 159, 161, 177, 183, 188, 215, 226, 236,
}
EXPECTED_NATIVE_ONLY_INDEXES = {66, 76, 140, 141, 142, 145}
EXPECTED_NONUNIFORM_SOURCE_INDEXES = {
    7, 18, 35, 48, 54, 57, 77, 89, 95, 123, 141, 159,
    177, 183, 188, 215,
}
EXPECTED_ZERO_PARTITION_UNIFORM_SOURCE_INDEXES = {
    70, 73, 83, 85, 86, 114, 236,
}
EXPECTED_REPEATED_NATIVE_SOURCE_INDEX = {161: [145]}
EXPECTED_RESIDUAL_SOURCE_ARTIFACTS = {
    129: (18, [216, 105, 256, 115]),
    148: (15, [213, 87, 221, 93]),
    226: (16, [88, 133, 95, 135]),
}
EXPECTED_NATIVE_GLOBAL_SOURCE_MATCHES = {
    66: [],
    76: [],
    140: [155],
    141: [154, 156],
    142: [158],
    145: [161],
}


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

    with tempfile.TemporaryDirectory(prefix="number-board5-pages-") as temporary:
        dump_directory = Path(temporary)
        native_hashes, native_starts, native_execution = (
            boardwide.read_native_sequence(dump_directory)
        )
        pages_by_index: dict[int, np.ndarray] = {}
        for path in dump_directory.glob("*.ppm"):
            index = int(path.stem.rsplit("-", 1)[1])
            pages_by_index[index] = pixels.read_ppm(path)
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
        first_page, second_page, result = pixels.best_partition(
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
        first_page, second_page, result = pixels.best_partition(
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
            "exact_source_run_indexes": [
                index for index, source_hash in enumerate(source_hashes)
                if source_hash == native_hashes[native_index]
            ],
            **result,
        })

    source_sha256 = common.sha256_file(CAPTURE)
    report = {
        "schema": "number-fifth-demo-board-reconciliation-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "source_window": source_window,
        "native_execution": native_execution,
        "exact_lcs_run_count": len(pairs),
        "source_only_artifacts": source_artifacts,
        "native_only_artifacts": native_artifacts,
        "classification": {
            "continuous_gameplay_parity_claim": True,
            "scope": [boardwide.FIRST_SOURCE_FRAME, boardwide.LAST_SOURCE_FRAME],
            "reason": (
                "all native-only LCS pages are either exact DOS pages repeated "
                "elsewhere in the same board or complete callback pages that "
                "partition adjacent DOS dirty-painter refreshes; all source-only "
                "pages are nonuniform scanout, exact partitions, a repeated exact "
                "native page, or one of three pinned incomplete painter records"
            ),
        },
    }
    source_indexes_match = set(source_only) == EXPECTED_SOURCE_ONLY_INDEXES
    native_indexes_match = set(native_only) == EXPECTED_NATIVE_ONLY_INDEXES
    source_by_index = {
        item["source_run_index"]: item for item in source_artifacts
    }
    native_by_index = {
        item["native_run_index"]: item for item in native_artifacts
    }
    nonuniform_indexes = {
        index for index, item in source_by_index.items()
        if not item["has_uniform_frame"]
    }
    zero_partition_uniform_indexes = {
        index for index, item in source_by_index.items()
        if item["has_uniform_frame"] and item["neither"] == 0
    }
    repeated_source_matches = {
        source_index: [
            native_index for native_index, native_hash in enumerate(native_hashes)
            if native_hash == source_hashes[source_index]
        ]
        for source_index in EXPECTED_REPEATED_NATIVE_SOURCE_INDEX
    }
    residual_artifacts_match = all(
        source_by_index[index]["neither"] == count and
        source_by_index[index]["neither_bbox"] == box
        for index, (count, box) in EXPECTED_RESIDUAL_SOURCE_ARTIFACTS.items()
    )
    native_global_matches = {
        index: native_by_index[index]["exact_source_run_indexes"]
        for index in native_by_index
    }
    report["checks"] = {
        "source_only_indexes_match": source_indexes_match,
        "native_only_indexes_match": native_indexes_match,
        "nonuniform_source_indexes_match": (
            nonuniform_indexes == EXPECTED_NONUNIFORM_SOURCE_INDEXES
        ),
        "zero_partition_uniform_source_indexes_match": (
            zero_partition_uniform_indexes ==
            EXPECTED_ZERO_PARTITION_UNIFORM_SOURCE_INDEXES
        ),
        "repeated_source_matches": repeated_source_matches,
        "residual_source_artifacts_match": residual_artifacts_match,
        "native_global_source_matches": native_global_matches,
        "all_native_artifacts_partition_source": all(
            item["neither"] == 0 for item in native_artifacts
        ),
    }
    report["valid"] = all((
        report["source_matches"], native_execution["test_passed"],
        len(pairs) == 218, source_indexes_match, native_indexes_match,
        nonuniform_indexes == EXPECTED_NONUNIFORM_SOURCE_INDEXES,
        zero_partition_uniform_indexes ==
            EXPECTED_ZERO_PARTITION_UNIFORM_SOURCE_INDEXES,
        repeated_source_matches == EXPECTED_REPEATED_NATIVE_SOURCE_INDEX,
        residual_artifacts_match,
        native_global_matches == EXPECTED_NATIVE_GLOBAL_SOURCE_MATCHES,
        all(item["neither"] == 0 for item in native_artifacts),
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "exact_lcs": len(pairs),
        "source_only_pages": len(source_artifacts),
        "native_only_pages": len(native_artifacts),
        "native_neither_zero": sum(
            item["neither"] == 0 for item in native_artifacts
        ),
        "valid": report["valid"],
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
