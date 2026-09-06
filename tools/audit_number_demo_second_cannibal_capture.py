#!/usr/bin/env python3
"""Audit the second Reggie-on-Reggie bite in the full Number demo.

This interval follows the already-gated first-board chew/right-exit sequence.
It exposes all 21 cannibal callbacks at row 4, column 5 and the terminal
record-15 repaint while unrelated Demo/player work changes the final two bite
framebuffers. Four uniform-but-incomplete dirty paints and one torn 2x refresh
remain capture evidence rather than native logical presentations.
"""

from __future__ import annotations

import json

import audit_number_demo_cannibal_capture as common


REPORT = (
    common.ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-second-cannibal-report.json"
)
WINDOW_FIRST_FRAME = 4653
WINDOW_LAST_FRAME = 4707

EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 55,
    "nonuniform_2x_blocks": 9,
    "logical_run_count": 28,
}

EXPECTED_PRE_BITE_CAPTURE = (0, 4653, 4654, 0x75BC3A4AF4E4F54D)
EXPECTED_BITE_TICKS = (
    (1, 4655, 4657, 0x7330140D95DB0B7A),
    (2, 4658, 4659, 0xDA55160B92BBED1D),
    (3, 4660, 4661, 0x7330140D95DB0B7A),
    (5, 4663, 4664, 0xDA55160B92BBED1D),
    (6, 4665, 4666, 0x7330140D95DB0B7A),
    (7, 4667, 4669, 0xDA55160B92BBED1D),
    (8, 4670, 4671, 0x7330140D95DB0B7A),
    (9, 4672, 4673, 0xDA55160B92BBED1D),
    (11, 4675, 4676, 0x7330140D95DB0B7A),
    (12, 4677, 4678, 0xDA55160B92BBED1D),
    (13, 4679, 4681, 0x7330140D95DB0B7A),
    (14, 4682, 4683, 0xDA55160B92BBED1D),
    (15, 4684, 4685, 0x7330140D95DB0B7A),
    (17, 4687, 4688, 0xDA55160B92BBED1D),
    (18, 4689, 4690, 0x7330140D95DB0B7A),
    (19, 4691, 4693, 0xDA55160B92BBED1D),
    (20, 4694, 4695, 0x7330140D95DB0B7A),
    (21, 4696, 4697, 0xDA55160B92BBED1D),
    (23, 4699, 4700, 0x7330140D95DB0B7A),
    (24, 4701, 4702, 0x3857E0C412F9E4B9),
    (26, 4704, 4705, 0xD045AEA037581456),
)
EXPECTED_TERMINAL = (27, 4706, 4707, 0x6068127ED60CAA9E)

# These are exact source presentations, but not complete actor callbacks. The
# four uniform pages stop during a dirty actor paint; the fifth is also torn
# inside nine nominal 2x blocks. Native deliberately presents completed pages.
EXPECTED_CAPTURE_ONLY = (
    (4, 4662, 4662, 0, 0x33431CA84098C24B, "incomplete_dirty_paint"),
    (10, 4674, 4674, 0, 0x33B6A6256AD27EBB, "incomplete_dirty_paint"),
    (16, 4686, 4686, 9, 0x835236A20E8D880A, "torn_2x_refresh"),
    (22, 4698, 4698, 0, 0x2EEFFAB6088B7054, "incomplete_dirty_paint"),
    (25, 4703, 4703, 0, 0x09277B4C1467C8DE, "incomplete_dirty_paint"),
)

EXPECTED_DIFFERENCES = {
    "ordinary_pose_a_vs_b": {
        "changed_pixels": 296, "bbox": [269, 151, 292, 174]
    },
    "pre_bite_vs_first_bite": {
        "changed_pixels": 319, "bbox": [269, 151, 292, 174]
    },
    "tick_18_vs_19_with_concurrent_work": {
        "changed_pixels": 543, "bbox": [29, 98, 292, 174]
    },
    "tick_19_vs_20_with_concurrent_work": {
        "changed_pixels": 536, "bbox": [29, 98, 292, 174]
    },
    "tick_20_vs_terminal_with_concurrent_work": {
        "changed_pixels": 579, "bbox": [29, 98, 292, 174]
    },
}


def capture_only_state(runs: list[common.Run], expected: tuple) -> tuple[dict, bool]:
    index, first, last, nonuniform, expected_hash, classification = expected
    run = runs[index]
    actual_hash = common.renderer_fnv64(run.rgb)
    matched = (
        run.start == first and run.end == last and
        run.nonuniform_blocks == nonuniform and
        run.has_uniform_frame == (nonuniform == 0) and
        actual_hash == expected_hash
    )
    return ({
        "run_index": index,
        "first_frame": run.start,
        "last_frame": run.end,
        "nonuniform_2x_blocks": run.nonuniform_blocks,
        "renderer_fnv64": f"0x{actual_hash:016x}",
        "classification": classification,
        "matched": matched,
    }, matched)


def main() -> None:
    source_sha256 = common.sha256_file(common.CAPTURE)
    probe = common.probe_capture(common.CAPTURE)
    audio = common.decode_audio_track(common.CAPTURE)

    common.WINDOW_FIRST_FRAME = WINDOW_FIRST_FRAME
    common.WINDOW_LAST_FRAME = WINDOW_LAST_FRAME
    runs, window = common.decode_window(common.CAPTURE)

    source_matches = source_sha256 == common.EXPECTED_CAPTURE_SHA256
    probe_matches = probe == {
        "codec": "zmbv",
        "pixel_format": "bgr0",
        "width": 640,
        "height": 400,
        "frame_rate_fraction": "2190197/31250",
        "frame_rate_hz": 70.086304,
        "frames": common.EXPECTED_CAPTURE_FRAMES,
        "duration_seconds": 313.955777,
        "audio_codec": "pcm_s16le",
        "audio_sample_rate": 48_000,
        "audio_channels": 2,
    }
    audio_matches = audio == {
        "decoded_bytes": common.EXPECTED_AUDIO_BYTES,
        "nonzero_bytes": common.EXPECTED_AUDIO_NONZERO_BYTES,
    }
    window_matches = window == EXPECTED_WINDOW

    pre_bite, pre_bite_matches = common.selected_state(
        runs, EXPECTED_PRE_BITE_CAPTURE
    )
    bite_ticks: list[dict] = []
    bite_ticks_match = True
    for tick, expected in enumerate(EXPECTED_BITE_TICKS):
        state, matched = common.selected_state(runs, expected)
        state["tick"] = tick
        state["record"] = (12, 13, 14, 13, 12, 13)[tick % 6]
        bite_ticks.append(state)
        bite_ticks_match &= matched
    terminal, terminal_matches = common.selected_state(runs, EXPECTED_TERMINAL)

    capture_only: list[dict] = []
    capture_only_matches = True
    for expected in EXPECTED_CAPTURE_ONLY:
        state, matched = capture_only_state(runs, expected)
        capture_only.append(state)
        capture_only_matches &= matched

    differences = {
        "ordinary_pose_a_vs_b": common.difference_geometry(runs[1].rgb, runs[2].rgb),
        "pre_bite_vs_first_bite": common.difference_geometry(runs[0].rgb, runs[1].rgb),
        "tick_18_vs_19_with_concurrent_work": common.difference_geometry(
            runs[23].rgb, runs[24].rgb
        ),
        "tick_19_vs_20_with_concurrent_work": common.difference_geometry(
            runs[24].rgb, runs[26].rgb
        ),
        "tick_20_vs_terminal_with_concurrent_work": common.difference_geometry(
            runs[26].rgb, runs[27].rgb
        ),
    }
    differences_match = differences == EXPECTED_DIFFERENCES

    report = {
        "source": common.CAPTURE.relative_to(common.ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_matches,
        "probe": probe,
        "probe_matches": probe_matches,
        "audio": audio,
        "audio_matches": audio_matches,
        "focused_decode": window,
        "focused_decode_matches": window_matches,
        "board": {
            "demo_board": 1,
            "level": 10,
            "mode": "Multiples",
            "target": 5,
            "event": "second Reggie-on-Reggie bite at row 4 column 5",
        },
        "pre_bite_capture_surface": pre_bite,
        "bite_ticks": bite_ticks,
        "terminal_record_15_dwell": terminal,
        "difference_geometry": differences,
        "difference_geometry_matches": differences_match,
        "capture_only_refreshes": capture_only,
        "native_replay": {
            "complete_bite_and_terminal": True,
            "exact_stable_state_count": 22,
            "headless_gate": "game_render_state_test",
            "shared_word_gate": "munchers_app_headless_test",
            "reason": (
                "the seeded first-Demo replay reaches the second cannibal event "
                "organically and matches all 21 source bite callbacks plus the "
                "terminal record-15 surface; the Word gate locks the shared "
                "player-before-biter callback queue"
            ),
        },
        "selected_states_match": bite_ticks_match and terminal_matches,
        "capture_only_refreshes_match": capture_only_matches,
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        pre_bite_matches,
        bite_ticks_match,
        terminal_matches,
        capture_only_matches,
        differences_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
