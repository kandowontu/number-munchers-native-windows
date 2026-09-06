#!/usr/bin/env python3
"""Reconcile every first-Demo board-wide source/native sequence difference."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import tempfile

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common
import audit_number_demo_first_board_continuous_sequence as boardwide


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
BOARDWIDE_REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-first-board-continuous-sequence-report.json"
)
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-first-board-reconciliation-report.json"
)
NATIVE_TEST = ROOT / "build" / "NumberMunchersRenderTests.exe"


def source_evidence_report(frame: int) -> str:
    if frame == 2_459:
        return "full-demo-first-board-opening-continuous-report.json"
    if frame <= 2_870:
        return "full-demo-early-bottom-entry-continuous-report.json"
    if frame == 2_940:
        return "full-demo-first-cannibal-approach-continuous-report.json"
    if frame <= 2_964:
        return "full-demo-cannibal-report.json"
    if frame == 3_082:
        return "full-demo-bashful-trail-exit-report.json"
    if frame <= 3_207:
        return "full-demo-reggie-trail-report.json"
    if frame <= 3_491:
        return "full-demo-post-warning-artifacts-report.json"
    if frame <= 3_883:
        return "full-demo-pre-departing-enemy-artifacts-report.json"
    if frame == 3_931:
        return "full-demo-departing-reggie-player-overlap-report.json"
    if frame <= 4_044:
        return "full-demo-post-departure-continuous-report.json"
    if frame <= 4_121:
        return "full-demo-chew-right-exit-report.json"
    if frame <= 4_304:
        return "full-demo-player-reggie-safe-bridge-report.json"
    if frame == 4_311:
        return "full-demo-bashful-right-entry-report.json"
    if frame <= 4_650:
        return "full-demo-pre-second-cannibal-bridge-report.json"
    if frame <= 4_703:
        return "full-demo-second-cannibal-report.json"
    if frame <= 5_172:
        return "full-demo-post-second-cannibal-continuous-report.json"
    if frame <= 5_196:
        return "full-demo-later-bottom-entry-continuous-report.json"
    if frame <= 5_324:
        return "full-demo-chew-top-entry-report.json"
    if frame <= 5_377:
        return "full-demo-third-cannibal-player-walk-report.json"
    if frame == 5_439:
        return "full-demo-later-bashful-left-trail-report.json"
    if frame == 5_598:
        return "full-demo-reggie-down-safe-entry-report.json"
    if frame == 5_711:
        return "full-demo-later-bottom-exit-continuous-report.json"
    if frame <= 5_764:
        return "full-demo-first-player-collision-report.json"
    raise ValueError(f"no focused source evidence for frame {frame}")


CALLBACK_LOCAL_PAGES = (
    {
        "native_run": 139,
        "native_frame": 1_077,
        "hash": 0x0E8E08721D4058A5,
        "previous_source_frame": 3_460,
        "following_source_frame": 3_463,
        "partition": {
            "previous_only": 240, "next_only": 506, "common": 63_254,
            "neither": 0, "neither_bbox": None,
            "native_delta_from_previous": 506,
            "native_delta_from_previous_bbox": [77, 87, 116, 115],
            "following_delta_from_native": 240,
            "following_delta_from_native_bbox": [173, 38, 198, 53],
        },
    },
    {
        "native_run": 141,
        "native_frame": 1_080,
        "hash": 0x8D7D65B430D3BB11,
        "previous_source_frame": 3_463,
        "following_source_frame": 3_465,
        "partition": {
            "previous_only": 240, "next_only": 480, "common": 63_280,
            "neither": 0, "neither_bbox": None,
            "native_delta_from_previous": 480,
            "native_delta_from_previous_bbox": [85, 91, 114, 114],
            "following_delta_from_native": 240,
            "following_delta_from_native_bbox": [173, 38, 198, 53],
        },
    },
    {
        "native_run": 181,
        "native_frame": 1_298,
        "hash": 0x4828C2E029993611,
        "previous_source_frame": 3_681,
        "following_source_frame": 3_684,
        "partition": {
            "previous_only": 240, "next_only": 506, "common": 63_254,
            "neither": 0, "neither_bbox": None,
            "native_delta_from_previous": 506,
            "native_delta_from_previous_bbox": [125, 87, 164, 115],
            "following_delta_from_native": 240,
            "following_delta_from_native_bbox": [221, 68, 246, 83],
        },
    },
    {
        "native_run": 184,
        "native_frame": 1_303,
        "hash": 0x3C602A2C866DFE39,
        "previous_source_frame": 3_686,
        "following_source_frame": 3_689,
        "partition": {
            "previous_only": 240, "next_only": 505, "common": 63_255,
            "neither": 0, "neither_bbox": None,
            "native_delta_from_previous": 505,
            "native_delta_from_previous_bbox": [141, 91, 184, 114],
            "following_delta_from_native": 240,
            "following_delta_from_native_bbox": [221, 68, 246, 83],
        },
    },
    {
        "native_run": 205,
        "native_frame": 1_433,
        "hash": 0xE23A4B5428AD3DEB,
        "previous_source_frame": 3_816,
        "following_source_frame": 3_819,
        "partition": {
            "previous_only": 240, "next_only": 506, "common": 63_254,
            "neither": 0, "neither_bbox": None,
            "native_delta_from_previous": 506,
            "native_delta_from_previous_bbox": [173, 87, 212, 115],
            "following_delta_from_native": 240,
            "following_delta_from_native_bbox": [269, 68, 294, 83],
        },
        "support_report": "full-demo-later-reggie-top-exit-report.json",
    },
    {
        "native_run": 212,
        "native_frame": 1_448,
        "hash": 0xA420CC163F3EEA11,
        "previous_source_frame": 3_828,
        "following_source_frame": 3_831,
        "partition": {
            "previous_only": 0, "next_only": 720, "common": 63_165,
            "neither": 115, "neither_bbox": [177, 87, 212, 115],
            "native_delta_from_previous": 835,
            "native_delta_from_previous_bbox": [177, 68, 294, 115],
            "following_delta_from_native": 115,
            "following_delta_from_native_bbox": [177, 87, 212, 115],
        },
        "support_report": "full-demo-later-reggie-top-exit-report.json",
    },
)


def bbox(mask: np.ndarray) -> list[int] | None:
    rows, columns = np.where(mask)
    if not len(rows):
        return None
    return [
        int(columns.min()), int(rows.min()),
        int(columns.max()), int(rows.max()),
    ]


def pixel_partition(
    previous: np.ndarray, native: np.ndarray, following: np.ndarray
) -> dict[str, object]:
    previous_match = np.all(native == previous, axis=2)
    following_match = np.all(native == following, axis=2)
    neither = ~previous_match & ~following_match
    native_delta = ~previous_match
    following_delta = ~following_match
    return {
        "previous_only": int(np.count_nonzero(
            previous_match & ~following_match
        )),
        "next_only": int(np.count_nonzero(
            following_match & ~previous_match
        )),
        "common": int(np.count_nonzero(
            previous_match & following_match
        )),
        "neither": int(np.count_nonzero(neither)),
        "neither_bbox": bbox(neither),
        "native_delta_from_previous": int(np.count_nonzero(native_delta)),
        "native_delta_from_previous_bbox": bbox(native_delta),
        "following_delta_from_native": int(np.count_nonzero(following_delta)),
        "following_delta_from_native_bbox": bbox(following_delta),
    }


def read_ppm(path: Path) -> np.ndarray:
    header, size, maximum, pixels = path.read_bytes().split(b"\n", 3)
    if header != b"P6" or size != b"320 200" or maximum != b"255":
        raise ValueError(f"unexpected PPM header in {path}")
    if len(pixels) != common.LOGICAL_WIDTH * common.LOGICAL_HEIGHT * 3:
        raise ValueError(f"unexpected PPM payload length in {path}")
    return np.frombuffer(pixels, dtype=np.uint8).reshape(
        common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3
    )


def decode_source_pages(frames: list[int]) -> dict[int, np.ndarray]:
    ordered = sorted(set(frames))
    selection = "+".join(f"eq(n\\,{frame})" for frame in ordered)
    result = subprocess.run(
        [
            "ffmpeg", "-v", "error", "-i", str(CAPTURE),
            "-map", "0:v:0", "-vf", f"select={selection}",
            "-fps_mode", "passthrough", "-f", "rawvideo",
            "-pix_fmt", "rgb24", "pipe:1",
        ],
        check=True, capture_output=True,
    )
    expected_bytes = len(ordered) * common.CAPTURE_FRAME_BYTES
    if len(result.stdout) != expected_bytes:
        raise ValueError(
            f"focused source page decode returned {len(result.stdout)}/"
            f"{expected_bytes} bytes"
        )
    decoded: dict[int, np.ndarray] = {}
    for index, frame in enumerate(ordered):
        first = index * common.CAPTURE_FRAME_BYTES
        physical = np.frombuffer(
            result.stdout[first:first + common.CAPTURE_FRAME_BYTES],
            dtype=np.uint8,
        ).reshape(common.CAPTURE_HEIGHT, common.CAPTURE_WIDTH, 3)
        decoded[frame] = physical[0::2, 0::2].copy()
    return decoded


def run_native_with_dumps(
    dump_directory: Path,
) -> tuple[list[int], list[int], dict[str, object]]:
    environment = os.environ.copy()
    environment["MUNCHERS_AUDIT_FIRST_BOARD_SEQUENCE"] = "1"
    environment["MUNCHERS_AUDIT_FIRST_BOARD_PAGE_DIR"] = str(dump_directory)
    result = subprocess.run(
        [str(NATIVE_TEST)], cwd=NATIVE_TEST.parent, env=environment,
        capture_output=True, text=True, timeout=120,
    )
    combined = result.stdout + "\n" + result.stderr
    match = boardwide.SEQUENCE_LINE.search(combined)
    if match is None:
        raise RuntimeError("native test did not emit the first-board sequence")
    items = list(boardwide.SEQUENCE_ITEM.finditer(match.group("body")))
    hashes = [int(item.group("hash"), 16) for item in items]
    starts = [int(item.group("frame")) for item in items]
    return hashes, starts, {
        "executable": NATIVE_TEST.relative_to(ROOT).as_posix(),
        "exit_code": result.returncode,
        "test_passed": result.returncode == 0,
        "changed_page_count": len(hashes),
        "first_presentation_frame": starts[0] if starts else None,
        "last_presentation_frame": starts[-1] if starts else None,
        "permanent_gate": "expectedCallbackLocalCompositeHashes",
    }


def main() -> None:
    boardwide_report = json.loads(BOARDWIDE_REPORT.read_text(encoding="utf-8"))
    source_runs, source_window = boardwide.decode_source_runs()
    source_pages = decode_source_pages([
        int(page[key])
        for page in CALLBACK_LOCAL_PAGES
        for key in ("previous_source_frame", "following_source_frame")
    ])

    with tempfile.TemporaryDirectory(
        prefix="number-first-board-reconciliation-"
    ) as temporary:
        dump_directory = Path(temporary)
        native_hashes, native_starts, native_execution = (
            run_native_with_dumps(dump_directory)
        )
        source_hashes = [run.fnv64 for run in source_runs]
        pairs = boardwide.lcs_pairs(native_hashes, source_hashes)
        matched_native = {native for native, _ in pairs}
        matched_source = {source for _, source in pairs}

        source_resolutions = []
        source_resolved = True
        duplicate_count = 0
        artifact_count = 0
        for index, run in enumerate(source_runs):
            if index in matched_source:
                continue
            native_occurrences = [
                native_index for native_index, value in enumerate(native_hashes)
                if value == run.fnv64
            ]
            if native_occurrences:
                resolved = run.start == 2_945 and run.end == 2_947
                duplicate_count += 1
                source_resolutions.append({
                    "source_run": index,
                    "first_frame": run.start,
                    "last_frame": run.end,
                    "renderer_fnv64": f"0x{run.fnv64:016x}",
                    "native_occurrences": native_occurrences,
                    "classification": "repeated-hash LCS alignment ambiguity",
                    "resolved": resolved,
                })
                source_resolved &= resolved
                continue

            evidence_name = source_evidence_report(run.start)
            evidence_path = BOARDWIDE_REPORT.parent / evidence_name
            evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
            evidence_text = evidence_path.read_text(encoding="utf-8").lower()
            hash_text = f"{run.fnv64:016x}"
            resolved = bool(evidence.get("valid")) and hash_text in evidence_text
            artifact_count += 1
            source_resolutions.append({
                "source_run": index,
                "first_frame": run.start,
                "last_frame": run.end,
                "nonuniform_2x_blocks": run.nonuniform_blocks,
                "has_uniform_frame": run.has_uniform_frame,
                "renderer_fnv64": f"0x{run.fnv64:016x}",
                "classification": "focused capture-only repaint/scanout artifact",
                "evidence_report": evidence_path.relative_to(ROOT).as_posix(),
                "evidence_report_valid": bool(evidence.get("valid")),
                "hash_present_in_evidence": hash_text in evidence_text,
                "resolved": resolved,
            })
            source_resolved &= resolved

        callback_pages = []
        callback_pages_match = True
        for expected in CALLBACK_LOCAL_PAGES:
            native_run = int(expected["native_run"])
            native_hash = int(expected["hash"])
            candidates = list(dump_directory.glob(f"{native_hash:x}-*.ppm"))
            dump_found = len(candidates) == 1
            if dump_found:
                native_pixels = read_ppm(candidates[0])
                dump_hash = common.renderer_fnv64(native_pixels.tobytes())
                partition = pixel_partition(
                    source_pages[int(expected["previous_source_frame"])],
                    native_pixels,
                    source_pages[int(expected["following_source_frame"])],
                )
            else:
                dump_hash = 0
                partition = {}
            support_valid = True
            support_path_text = None
            if "support_report" in expected:
                support_path = BOARDWIDE_REPORT.parent / str(
                    expected["support_report"]
                )
                support = json.loads(support_path.read_text(encoding="utf-8"))
                support_text = support_path.read_text(encoding="utf-8").lower()
                support_valid = (
                    bool(support.get("valid")) and
                    f"{native_hash:016x}" in support_text
                )
                support_path_text = support_path.relative_to(ROOT).as_posix()
            matched = all((
                dump_found,
                native_run < len(native_hashes),
                native_hashes[native_run] == native_hash,
                native_starts[native_run] == int(expected["native_frame"]),
                dump_hash == native_hash,
                partition == expected["partition"],
                support_valid,
            ))
            callback_pages_match &= matched
            callback_pages.append({
                "native_run": native_run,
                "native_frame": int(expected["native_frame"]),
                "renderer_fnv64": f"0x{native_hash:016x}",
                "previous_source_frame": int(
                    expected["previous_source_frame"]
                ),
                "following_source_frame": int(
                    expected["following_source_frame"]
                ),
                **partition,
                "classification": (
                    "complete callback-local actor composite between adjacent "
                    "DOS capture samples"
                ),
                "support_report": support_path_text,
                "matched": matched,
            })

    actual_source_only = len(source_resolutions)
    actual_native_only_groups = boardwide.missing_groups(
        len(native_hashes), matched_native
    )
    expected_internal_groups = [(139, 139), (141, 141), (181, 181),
                                (184, 184), (205, 205), (212, 212)]
    internal_groups_match = actual_native_only_groups[:-1] == expected_internal_groups
    transition_group = actual_native_only_groups[-1]
    transition_hashes = native_hashes[
        transition_group[0]:transition_group[1] + 1
    ]
    attract_audit = ROOT / "analysis" / "ATTRACT-STATIC-AUDIT.md"
    test_source = ROOT / "tests" / "game_render_test.cpp"
    transition_evidence = (
        attract_audit.read_text(encoding="utf-8").lower() +
        test_source.read_text(encoding="utf-8").lower()
    )
    transition_hashes_documented = all(
        f"{value:016x}" in transition_evidence for value in transition_hashes
    )

    common.WINDOW_FIRST_FRAME = 6_132
    common.WINDOW_LAST_FRAME = 6_133
    boundary_runs, boundary_window = common.decode_window(CAPTURE)
    transition_boundary_match = (
        len(boundary_runs) == 2 and
        common.renderer_fnv64(boundary_runs[0].rgb) ==
            0xD26DE8ADDAFE2FDE and
        common.renderer_fnv64(boundary_runs[1].rgb) == transition_hashes[0]
    )

    report_matches_current = all((
        bool(boardwide_report.get("valid")),
        boardwide_report.get("source_window") == source_window,
        int(boardwide_report.get("exact_lcs_run_count", -1)) == len(pairs),
        int(boardwide_report.get("matched_native_changed_pages", -1)) ==
            len(matched_native),
        int(boardwide_report.get("matched_source_logical_runs", -1)) ==
            len(matched_source),
        int(boardwide_report.get("native_execution", {}).get(
            "changed_page_count", -1
        )) == len(native_hashes),
    ))

    transition_group_match = (
        transition_group == (521, 540) and
        len(transition_hashes) == 20 and
        native_starts[521] == 3_747 and native_starts[540] == 5_971
    )
    report = {
        "schema": "number-first-demo-board-reconciliation-v1",
        "boardwide_diagnostic":
            BOARDWIDE_REPORT.relative_to(ROOT).as_posix(),
        "boardwide_diagnostic_matches_current_execution":
            report_matches_current,
        "source_window": source_window,
        "exact_lcs_run_count": len(pairs),
        "source_only_resolution": {
            "source_only_logical_runs": actual_source_only,
            "focused_capture_artifacts": artifact_count,
            "repeated_hash_lcs_ambiguities": duplicate_count,
            "all_resolved": source_resolved,
            "runs": source_resolutions,
        },
        "native_execution": native_execution,
        "native_only_resolution": {
            "internal_callback_local_pages": callback_pages,
            "internal_groups_match": internal_groups_match,
            "all_callback_local_pages_pixel_matched": callback_pages_match,
            "post_scope_transition": {
                "native_run_start": transition_group[0],
                "native_run_end": transition_group[1],
                "native_frame_start": native_starts[transition_group[0]],
                "native_frame_end": native_starts[transition_group[1]],
                "run_count": len(transition_hashes),
                "renderer_fnv64": [
                    f"0x{value:016x}" for value in transition_hashes
                ],
                "source_gameplay_scope_last_frame":
                    boardwide.LAST_SOURCE_FRAME,
                "source_transition_boundary": boundary_window,
                "first_transition_native_hash_matches_source_frame_6133":
                    transition_boundary_match,
                "all_hashes_documented_and_gated":
                    transition_hashes_documented,
                "evidence": [
                    attract_audit.relative_to(ROOT).as_posix(),
                    test_source.relative_to(ROOT).as_posix(),
                ],
                "classification": (
                    "outside the first-board gameplay scope: collision Wipe, "
                    "Hall, logo, and next-board painter"
                ),
                "matched": all((
                    transition_group_match,
                    transition_boundary_match,
                    transition_hashes_documented,
                )),
            },
        },
        "classification": {
            "board_wide_first_board_gameplay_parity_claim": True,
            "scope": "source frames 2390-6131 inclusive",
            "reason": (
                "every unmatched source run is either a pixel-classified "
                "capture artifact or a repeated-hash LCS ambiguity; all six "
                "internal native-only pages are pixel-verified callback-local "
                "composites; the remaining native block begins after the "
                "gameplay scope and is separately exact-hash-gated"
            ),
        },
    }
    report["valid"] = all((
        report_matches_current,
        source_resolved,
        actual_source_only == 69,
        artifact_count == 68,
        duplicate_count == 1,
        native_execution["test_passed"],
        internal_groups_match,
        callback_pages_match,
        transition_group_match,
        transition_boundary_match,
        transition_hashes_documented,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "valid": report["valid"],
        "source_only_resolved": actual_source_only,
        "internal_native_only_resolved": len(callback_pages),
        "post_scope_native_pages": len(transition_hashes),
        "board_wide_first_board_gameplay_parity_claim":
            report["classification"][
                "board_wide_first_board_gameplay_parity_claim"
            ],
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
