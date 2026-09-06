#!/usr/bin/env python3
"""Audit the complete Worker-on-Muncher collision on Demo board 3.

This continuous reference interval starts at the stationary endpoint callback,
contains every bite pose while a later Reggie begins entering, and ends on the
stable ``Aargh`` feedback framebuffer. Native intentionally omits torn host
refreshes while preserving every complete callback-local page in source order.
"""

from __future__ import annotations

import json
from pathlib import Path

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-worker-collision-report.json"
)
WINDOW_FIRST_FRAME = 11_940
WINDOW_LAST_FRAME = 11_992

# (label, first, last, FNV-64, nonuniform 2x blocks, has uniform source frame)
EXPECTED_RUNS = [
    ("pre_bite_stationary_callback", 11_940, 11_941, 0xDBF84ED13C67DEDA, 0, True),
    ("bite_tick_0", 11_942, 11_943, 0xF8E5C94E49765996, 0, True),
    ("bite_tick_1_pre_safe_surface", 11_944, 11_946, 0x64C5C546D7B61954, 1, True),
    ("bite_open_1", 11_947, 11_948, 0xE9A0890639BA7AA6, 0, True),
    ("bite_closed_1", 11_949, 11_951, 0x2F6A98E811428C64, 0, True),
    ("bite_open_2", 11_952, 11_953, 0xE9A0890639BA7AA6, 0, True),
    ("bite_closed_2", 11_954, 11_955, 0x2F6A98E811428C64, 0, True),
    ("bite_open_3", 11_956, 11_958, 0xE9A0890639BA7AA6, 0, True),
    ("bite_closed_3", 11_959, 11_960, 0x2F6A98E811428C64, 0, True),
    ("bite_open_4", 11_961, 11_963, 0xE9A0890639BA7AA6, 0, True),
    ("bite_closed_4", 11_964, 11_965, 0x2F6A98E811428C64, 0, True),
    ("bite_open_5", 11_966, 11_967, 0xE9A0890639BA7AA6, 0, True),
    ("bite_closed_5", 11_968, 11_969, 0x2F6A98E811428C64, 0, True),
    ("left_edge_refresh_tear", 11_970, 11_970, 0xE15A45869B1F49FA, 4, False),
    ("bite_with_reggie_entry_a", 11_971, 11_972, 0x86DDF50F242878E4, 0, True),
    ("bite_with_reggie_entry_b", 11_973, 11_975, 0x28B02FDB861BBA66, 0, True),
    ("retained_reggie_page_a", 11_976, 11_977, 0x86DDF50F242878E4, 0, True),
    ("bite_with_reggie_entry_c", 11_978, 11_979, 0x6EEAD36170FA7886, 0, True),
    ("reggie_entry_refresh_tear", 11_980, 11_980, 0xBEA1F9D9F3F41B98, 7, False),
    ("bite_with_reggie_entry_d", 11_981, 11_981, 0x0A94F93FE8D7E660, 0, True),
    ("bite_with_reggie_entry_e", 11_982, 11_982, 0xE2FDFE9FA771BCB6, 0, True),
    ("bite_with_reggie_entry_f", 11_983, 11_984, 0xC7902B1B3549D7F4, 0, True),
    ("retained_reggie_page_e", 11_985, 11_986, 0xE2FDFE9FA771BCB6, 0, True),
    ("bite_with_reggie_entry_g", 11_987, 11_987, 0x8C2F87AB2E8FAA40, 0, True),
    ("bite_with_reggie_entry_h", 11_988, 11_989, 0xF20C3488C9978B6E, 0, True),
    ("final_bite_pose_retained_entry", 11_990, 11_991, 0x8C2F87AB2E8FAA40, 0, True),
    ("stable_aargh_feedback", 11_992, 11_992, 0x89CD39B70D508C96, 0, True),
]

EXPECTED_LOGICAL_TICK_HASHES = [
    0xF8E5C94E49765996,
    0x2F6A98E811428C64, 0xE9A0890639BA7AA6,
    0x2F6A98E811428C64, 0xE9A0890639BA7AA6,
    0x2F6A98E811428C64, 0xE9A0890639BA7AA6,
    0x2F6A98E811428C64, 0xE9A0890639BA7AA6,
    0x2F6A98E811428C64, 0xE9A0890639BA7AA6,
    0x2F6A98E811428C64, 0x86DDF50F242878E4,
    0x28B02FDB861BBA66, 0x0A94F93FE8D7E660,
    0x6EEAD36170FA7886, 0xE2FDFE9FA771BCB6,
    0xC7902B1B3549D7F4, 0x8C2F87AB2E8FAA40,
    0xF20C3488C9978B6E, 0x8C2F87AB2E8FAA40,
]

EXPECTED_CLEAN_PRESENTED_HASHES = [
    0xDBF84ED13C67DEDA, 0xF8E5C94E49765996, 0x64C5C546D7B61954,
    0xE9A0890639BA7AA6, 0x2F6A98E811428C64,
    0xE9A0890639BA7AA6, 0x2F6A98E811428C64,
    0xE9A0890639BA7AA6, 0x2F6A98E811428C64,
    0xE9A0890639BA7AA6, 0x2F6A98E811428C64,
    0xE9A0890639BA7AA6, 0x2F6A98E811428C64,
    0x86DDF50F242878E4, 0x28B02FDB861BBA66,
    0x86DDF50F242878E4, 0x6EEAD36170FA7886,
    0x0A94F93FE8D7E660, 0xE2FDFE9FA771BCB6,
    0xC7902B1B3549D7F4, 0xE2FDFE9FA771BCB6,
    0x8C2F87AB2E8FAA40, 0xF20C3488C9978B6E,
    0x8C2F87AB2E8FAA40, 0x89CD39B70D508C96,
]


def main() -> None:
    source_sha256 = common.sha256_file(CAPTURE)
    probe = common.probe_capture(CAPTURE)
    audio = common.decode_audio_track(CAPTURE)
    common.WINDOW_FIRST_FRAME = WINDOW_FIRST_FRAME
    common.WINDOW_LAST_FRAME = WINDOW_LAST_FRAME
    runs, window = common.decode_window(CAPTURE)

    expected_probe = {
        "codec": "zmbv", "pixel_format": "bgr0", "width": 640, "height": 400,
        "frame_rate_fraction": "2190197/31250", "frame_rate_hz": 70.086304,
        "frames": common.EXPECTED_CAPTURE_FRAMES, "duration_seconds": 313.955777,
        "audio_codec": "pcm_s16le", "audio_sample_rate": 48_000,
        "audio_channels": 2,
    }
    expected_audio = {
        "decoded_bytes": common.EXPECTED_AUDIO_BYTES,
        "nonzero_bytes": common.EXPECTED_AUDIO_NONZERO_BYTES,
    }
    expected_window = {
        "first_global_frame": WINDOW_FIRST_FRAME,
        "last_global_frame": WINDOW_LAST_FRAME,
        "decoded_frames": 53,
        "nonuniform_2x_blocks": 12,
        "logical_run_count": len(EXPECTED_RUNS),
    }

    run_reports: dict[str, dict[str, object]] = {}
    runs_match = len(runs) == len(EXPECTED_RUNS)
    complete_run_count = 0
    for index, expected in enumerate(EXPECTED_RUNS):
        name, first, last, expected_hash, nonuniform, has_uniform = expected
        if index >= len(runs):
            run_reports[name] = {"run_index": index, "matched": False}
            runs_match = False
            continue
        run = runs[index]
        actual_hash = common.renderer_fnv64(run.rgb)
        matched = (
            run.start == first and run.end == last and
            run.nonuniform_blocks == nonuniform and
            run.has_uniform_frame == has_uniform and actual_hash == expected_hash
        )
        runs_match &= matched
        if has_uniform:
            complete_run_count += 1
        run_reports[name] = {
            "run_index": index, "first_frame": run.start, "last_frame": run.end,
            "frames": run.end - run.start + 1,
            "nonuniform_2x_blocks": run.nonuniform_blocks,
            "has_uniform_frame": run.has_uniform_frame,
            "renderer_fnv64": f"0x{actual_hash:016x}", "matched": matched,
        }

    complete_hashes = {
        expected_hash for _, _, _, expected_hash, _, has_uniform in EXPECTED_RUNS
        if has_uniform
    }
    native_hashes = set(EXPECTED_CLEAN_PRESENTED_HASHES)
    complete_state_coverage = complete_hashes <= native_hashes
    complete_source_order = [
        expected_hash for _, _, _, expected_hash, _, has_uniform in EXPECTED_RUNS
        if has_uniform
    ]
    complete_ordered_presentation = (
        complete_source_order == EXPECTED_CLEAN_PRESENTED_HASHES
    )
    report = {
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "probe": probe, "probe_matches": probe == expected_probe,
        "audio": audio, "audio_matches": audio == expected_audio,
        "focused_decode": window, "focused_decode_matches": window == expected_window,
        "board": {
            "demo_board": 3, "mode": "Equality", "target": 30,
            "collision": "Worker/laborus bites Muncher at row 0 column 5",
            "concurrent": [
                "a second Worker dwells at row 4 column 5",
                "a Reggie begins bottom entry at row 4 column 2 during the bite",
                "a safe zone activates at row 2 column 2",
            ],
        },
        "runs": run_reports,
        "runs_match": runs_match,
        "complete_uniform_logical_run_count": complete_run_count,
        "refresh_artifacts": {
            "mixed_run_one_block_sample": "bite_tick_1_pre_safe_surface",
            "fully_torn_runs": ["left_edge_refresh_tear", "reggie_entry_refresh_tear"],
            "native_reproduces_torn_refreshes": False,
        },
        "native_replay": {
            "headless_gate": "game_render_state_test",
            "shared_word_gate": "munchers_app_headless_test",
            "logical_bite_tick_count": len(EXPECTED_LOGICAL_TICK_HASHES),
            "clean_presented_transition_count": len(EXPECTED_CLEAN_PRESENTED_HASHES),
            "complete_source_hash_coverage": complete_state_coverage,
            "exact_complete_source_run_count": complete_run_count,
            "exact_terminal_hash": "0x89cd39b70d508c96",
            "fixed_gap": (
                "the state-5 bite now paints at physical slot 1 between earlier "
                "and later Troggle callbacks, retains the clipped phase-1 page, "
                "and aborts before the later Reggie on its terminal callbacks"
            ),
            "complete_ordered_source_presentation": complete_ordered_presentation,
            "ordered_complete_source_hashes": [
                f"0x{value:016x}" for value in complete_source_order
            ],
        },
    }
    report["valid"] = all((
        report["source_matches"], report["probe_matches"],
        report["audio_matches"], report["focused_decode_matches"],
        runs_match, complete_state_coverage, complete_ordered_presentation,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
