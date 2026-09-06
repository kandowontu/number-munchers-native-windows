#!/usr/bin/env python3
"""Audit the captured Factors Demo any-key exit and stable title tail."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess

import audit_number_demo_bashful_trail_exit_capture as common
import audit_number_demo_first_board_continuous_sequence as alignment
import audit_number_demo_fifth_board_continuous_sequence as boardwide


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
NATIVE_TEST = ROOT / "build" / "NumberMunchersRenderTests.exe"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-fifth-board-exit-report.json"
)
FIRST_SOURCE_FRAME = 20_528
LAST_SOURCE_FRAME = 22_003
BOARD_FIRST_SOURCE_FRAME = boardwide.FIRST_SOURCE_FRAME
EXIT_PRESENTATION_FRAME = FIRST_SOURCE_FRAME - BOARD_FIRST_SOURCE_FRAME
TAIL_LAST_PRESENTATION_FRAME = LAST_SOURCE_FRAME - BOARD_FIRST_SOURCE_FRAME
SEQUENCE_LINE = re.compile(
    r"^RESTARTED_FIFTH_BOARD_PRESENTATION_SEQUENCE(?P<body>.*)$", re.M
)
SEQUENCE_ITEM = re.compile(r"0x(?P<hash>[0-9a-fA-F]+)@(?P<frame>[0-9]+)")
EXIT_LINE = re.compile(
    r"^RESTARTED_FIFTH_BOARD_EXIT frame=(?P<frame>[0-9]+) "
    r"page=(?P<page>[0-9]+) attract=(?P<attract>[01]) "
    r"title_idle=(?P<title_idle>[0-9.]+) calls=(?P<calls>[0-9]+) "
    r"hash=0x(?P<hash>[0-9a-fA-F]+)$",
    re.M,
)


def main() -> None:
    alignment.FIRST_SOURCE_FRAME = FIRST_SOURCE_FRAME
    alignment.LAST_SOURCE_FRAME = LAST_SOURCE_FRAME
    source_runs, source_window = alignment.decode_source_runs()

    environment = os.environ.copy()
    environment["MUNCHERS_AUDIT_RESTARTED_FIFTH_BOARD_SEQUENCE"] = "1"
    environment["MUNCHERS_AUDIT_FIFTH_BOARD_EXIT"] = "1"
    result = subprocess.run(
        [str(NATIVE_TEST)], cwd=NATIVE_TEST.parent, env=environment,
        capture_output=True, text=True, timeout=120,
    )
    combined = result.stdout + "\n" + result.stderr
    sequence_match = SEQUENCE_LINE.search(combined)
    exit_match = EXIT_LINE.search(combined)
    if sequence_match is None or exit_match is None:
        raise RuntimeError("native test did not emit the fifth-board exit trace")
    all_items = [
        (int(item.group("hash"), 16), int(item.group("frame")))
        for item in SEQUENCE_ITEM.finditer(sequence_match.group("body"))
    ]
    native_tail = [
        (hash_value, frame) for hash_value, frame in all_items
        if EXIT_PRESENTATION_FRAME <= frame <= TAIL_LAST_PRESENTATION_FRAME
    ]

    source_sha256 = common.sha256_file(CAPTURE)
    exit_state = {
        "frame": int(exit_match.group("frame")),
        "page": int(exit_match.group("page")),
        "attract": bool(int(exit_match.group("attract"))),
        "title_idle_seconds": float(exit_match.group("title_idle")),
        "random_calls": int(exit_match.group("calls")),
        "renderer_fnv64": f"0x{int(exit_match.group('hash'), 16):016x}",
    }
    report = {
        "schema": "number-fifth-demo-board-exit-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "source_window": source_window,
        "source_runs": [
            {
                "run_index": index,
                "first_frame": run.start,
                "last_frame": run.end,
                "renderer_fnv64": f"0x{run.fnv64:016x}",
                "has_uniform_frame": run.has_uniform_frame,
                "nonuniform_2x_blocks": run.nonuniform_blocks,
            }
            for index, run in enumerate(source_runs)
        ],
        "native_execution": {
            "executable": NATIVE_TEST.relative_to(ROOT).as_posix(),
            "exit_code": result.returncode,
            "test_passed": result.returncode == 0,
            "tail_changed_pages": [
                {
                    "presentation_frame": frame,
                    "renderer_fnv64": f"0x{hash_value:016x}",
                }
                for hash_value, frame in native_tail
            ],
            "exit_state": exit_state,
        },
        "classification": {
            "captured_key_exit_parity_claim": True,
            "reason": (
                "the one externally supplied key returns native directly to the "
                "exact stable DOS title; DOS frame 20528 is a physically "
                "nonuniform single-buffer transition and native correctly emits "
                "only the completed title page through the 21-second captured hold"
            ),
        },
    }
    report["valid"] = all((
        report["source_matches"],
        result.returncode == 0,
        source_window == {
            "first_global_frame": FIRST_SOURCE_FRAME,
            "last_global_frame": LAST_SOURCE_FRAME,
            "decoded_frames": 1_476,
            "logical_run_count": 2,
            "nonuniform_2x_blocks": 347,
            "runs_with_uniform_frame": 1,
            "runs_without_uniform_frame": 1,
        },
        len(source_runs) == 2,
        source_runs[0].start == 20_528,
        source_runs[0].end == 20_528,
        source_runs[0].fnv64 == 0xC9E6E0B36517E451,
        not source_runs[0].has_uniform_frame,
        source_runs[0].nonuniform_blocks == 347,
        source_runs[1].start == 20_529,
        source_runs[1].end == 22_003,
        source_runs[1].fnv64 == 0xEDF7E5D8C59EEAC7,
        source_runs[1].has_uniform_frame,
        native_tail == [(0xEDF7E5D8C59EEAC7, EXIT_PRESENTATION_FRAME)],
        exit_state["frame"] == TAIL_LAST_PRESENTATION_FRAME + 1,
        exit_state["page"] == 2,
        not exit_state["attract"],
        21.0 < exit_state["title_idle_seconds"] < 22.0,
        exit_state["random_calls"] == 767,
        exit_state["renderer_fnv64"] == "0xedf7e5d8c59eeac7",
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "valid": report["valid"],
        "source_runs": len(source_runs),
        "native_tail_changed_pages": len(native_tail),
        "title_hash": exit_state["renderer_fnv64"],
        "title_idle_seconds": exit_state["title_idle_seconds"],
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
