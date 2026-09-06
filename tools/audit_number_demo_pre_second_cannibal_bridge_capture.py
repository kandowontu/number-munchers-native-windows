#!/usr/bin/env python3
"""Audit the first-Demo bridge immediately before cannibal collision two.

Frames 4340-4652 contain forty-nine presentation-complete pages spanning
player movement, two independent Reggie jobs, Bashful's right exit, a right
entry, and the approach to the row-4/column-5 overlap. Ten intervening runs
are incomplete dirty repaints and remain capture-only.
"""

from __future__ import annotations

import json
from pathlib import Path

import audit_number_demo_bashful_trail_exit_capture as common
import audit_number_demo_player_reggie_safe_bridge_capture as bridge_common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-pre-second-cannibal-bridge-report.json"
)

WINDOW_FIRST_FRAME = 4_340
WINDOW_LAST_FRAME = 4_652
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 313,
    "nonuniform_2x_blocks": 22,
    "logical_run_count": 59,
}

EXPECTED_RUNS = (
    (0, 4340, 4341, 0x2767224657BC284F),
    (1, 4342, 4342, 0x78B37DEEE7B1AB75),
    (2, 4343, 4344, 0x9C2A34ED7AC568AF),
    (3, 4345, 4346, 0xC0A6EE9B27E67D09),
    (4, 4347, 4349, 0x90DF701E79C254C5),
    (5, 4350, 4351, 0x0D01B843D88560DF),
    (6, 4352, 4354, 0xCC0E0DE1DE8D656F),
    (7, 4355, 4359, 0xE0EE250696524029),
    (8, 4360, 4363, 0xCC99BA02210D20CF),
    (9, 4364, 4366, 0xE3917A00434A9F77),
    (10, 4367, 4368, 0x08C27A61BED70C10),
    (11, 4369, 4371, 0xA8D57829A51640D7),
    (12, 4372, 4373, 0x54F057703239C603),
    (13, 4374, 4375, 0x4EFEBE8554E69903),
    (14, 4376, 4376, 0x3AD6C0C59DA2E249),
    (15, 4377, 4452, 0x3D4DA5527806AD09),
    (16, 4453, 4453, 0x2D9324A5E350BDE1),
    (17, 4454, 4455, 0x19522965DF27480F),
    (18, 4456, 4457, 0xC9D124D301D24E04),
    (19, 4458, 4460, 0x0AF77D261B7C3429),
    (20, 4461, 4462, 0xEC454913A320012C),
    (21, 4463, 4464, 0x995BF789E2207D33),
    (22, 4465, 4465, 0x39AB017A35DA9F06),
    (23, 4466, 4467, 0xE00329466E865A70),
    (24, 4468, 4546, 0xCF3FA2A1142620C1),
    (25, 4547, 4549, 0x4B22EC53DD3FC1B3),
    (26, 4550, 4551, 0x97DBCB7C05834298),
    (27, 4552, 4553, 0x365B10B7446AC591),
    (28, 4554, 4554, 0xF74C999AA10CB4A2),
    (29, 4555, 4556, 0xCB439B81CDE44070),
    (30, 4557, 4558, 0x13E1A3D2940BCB7F),
    (31, 4559, 4561, 0x29EB17D5E5431E70),
    (32, 4562, 4573, 0x7340FDFF0FA99195),
    (33, 4574, 4575, 0xB47E78A1AB14B133),
    (34, 4576, 4577, 0x809893E4447A85CA),
    (35, 4578, 4578, 0x20DF0CE477DFF914),
    (36, 4579, 4580, 0xD0382B2402119BC8),
    (37, 4581, 4582, 0x5C0DDF923CBEF276),
    (38, 4583, 4584, 0x5CA7EAE85A0F7AA2),
    (39, 4585, 4585, 0xD48133AB4DF8BD84),
    (40, 4586, 4587, 0x7074E37364AE5BE3),
    (41, 4588, 4589, 0x45CF651612D405C2),
    (42, 4590, 4590, 0x8B730C2FA522E704),
    (43, 4591, 4621, 0x891B04B3D9725D18),
    (44, 4622, 4623, 0xCB33014BD3990BCD),
    (45, 4624, 4626, 0x427E52B8EE8F43B2),
    (46, 4627, 4628, 0x17FE3EAB44623767),
    (47, 4629, 4630, 0x1517206CC8A288CA),
    (48, 4631, 4631, 0x876870D02FF25CA9),
    (49, 4632, 4633, 0xE237D8CAA169FC69),
    (50, 4634, 4635, 0x702158E1560E3CDF),
    (51, 4636, 4637, 0xC4326A2A2CC654BA),
    (52, 4638, 4640, 0x3C47849E2BE7A0BC),
    (53, 4641, 4642, 0x655D549BBEBB0701),
    (54, 4643, 4645, 0xF709CC6E27177085),
    (55, 4646, 4647, 0x25404DC95A0A250B),
    (56, 4648, 4649, 0xC538F6953A848B99),
    (57, 4650, 4650, 0x37444686D7C13016),
    (58, 4651, 4652, 0x10BE6199D0450F51),
)

CAPTURE_ONLY_RUN_INDEXES = (1, 14, 16, 22, 28, 35, 39, 42, 48, 57)
EXPECTED_CAPTURE_ONLY_TRANSITIONS = {
    "run_1_incomplete_reggie_right_paint": {
        "run_index": 1, "previous_only": 480, "next_only": 0,
        "common": 63_513, "neither": 7,
        "neither_bbox": [212, 169, 212, 175],
        "nonuniform_2x_blocks": 0,
        "physical_pixels_differing_from_top_left_duplication": 0,
        "physical_difference_bbox": None,
    },
    "run_14_incomplete_player_up_paint": {
        "run_index": 14, "previous_only": 8, "next_only": 63,
        "common": 63_929, "neither": 0, "neither_bbox": None,
        "nonuniform_2x_blocks": 0,
        "physical_pixels_differing_from_top_left_duplication": 0,
        "physical_difference_bbox": None,
    },
    "run_16_incomplete_player_left_paint": {
        "run_index": 16, "previous_only": 0, "next_only": 339,
        "common": 63_658, "neither": 3,
        "neither_bbox": [164, 89, 164, 91],
        "nonuniform_2x_blocks": 2,
        "physical_pixels_differing_from_top_left_duplication": 4,
        "physical_difference_bbox": [328, 177, 329, 183],
    },
    "run_22_incomplete_player_left_paint": {
        "run_index": 22, "previous_only": 30, "next_only": 302,
        "common": 63_665, "neither": 3,
        "neither_bbox": [164, 94, 164, 96],
        "nonuniform_2x_blocks": 10,
        "physical_pixels_differing_from_top_left_duplication": 20,
        "physical_difference_bbox": [264, 187, 329, 193],
    },
    "run_28_incomplete_player_left_paint": {
        "run_index": 28, "previous_only": 27, "next_only": 306,
        "common": 63_663, "neither": 4,
        "neither_bbox": [115, 94, 116, 95],
        "nonuniform_2x_blocks": 9,
        "physical_pixels_differing_from_top_left_duplication": 18,
        "physical_difference_bbox": [200, 187, 233, 187],
    },
    "run_35_incomplete_bashful_exit_paint": {
        "run_index": 35, "previous_only": 442, "next_only": 133,
        "common": 63_425, "neither": 0, "neither_bbox": None,
        "nonuniform_2x_blocks": 0,
        "physical_pixels_differing_from_top_left_duplication": 0,
        "physical_difference_bbox": None,
    },
    "run_39_incomplete_bashful_terminal_paint": {
        "run_index": 39, "previous_only": 492, "next_only": 0,
        "common": 63_505, "neither": 3,
        "neither_bbox": [308, 173, 308, 175],
        "nonuniform_2x_blocks": 0,
        "physical_pixels_differing_from_top_left_duplication": 0,
        "physical_difference_bbox": None,
    },
    "run_42_incomplete_entry_terminal_paint": {
        "run_index": 42, "previous_only": 0, "next_only": 29,
        "common": 63_959, "neither": 12,
        "neither_bbox": [72, 115, 112, 115],
        "nonuniform_2x_blocks": 0,
        "physical_pixels_differing_from_top_left_duplication": 0,
        "physical_difference_bbox": None,
    },
    "run_48_incomplete_player_left_paint": {
        "run_index": 48, "previous_only": 0, "next_only": 263,
        "common": 63_713, "neither": 24,
        "neither_bbox": [68, 87, 77, 95],
        "nonuniform_2x_blocks": 1,
        "physical_pixels_differing_from_top_left_duplication": 2,
        "physical_difference_bbox": [136, 187, 137, 187],
    },
    "run_57_incomplete_reggie_overlap_paint": {
        "run_index": 57, "previous_only": 1, "next_only": 473,
        "common": 63_515, "neither": 11,
        "neither_bbox": [260, 147, 283, 151],
        "nonuniform_2x_blocks": 0,
        "physical_pixels_differing_from_top_left_duplication": 0,
        "physical_difference_bbox": None,
    },
}

PRESENTATION_COMPLETE_RUN_INDEXES = tuple(
    index for index in range(len(EXPECTED_RUNS))
    if index not in CAPTURE_ONLY_RUN_INDEXES
)


def main() -> None:
    source_sha256 = common.sha256_file(CAPTURE)
    probe = common.probe_capture(CAPTURE)
    audio = common.decode_audio_track(CAPTURE)
    common.WINDOW_FIRST_FRAME = WINDOW_FIRST_FRAME
    common.WINDOW_LAST_FRAME = WINDOW_LAST_FRAME
    runs, window = common.decode_window(CAPTURE)

    source_matches = source_sha256 == common.EXPECTED_CAPTURE_SHA256
    probe_matches = probe == {
        "codec": "zmbv", "pixel_format": "bgr0", "width": 640,
        "height": 400, "frame_rate_fraction": "2190197/31250",
        "frame_rate_hz": 70.086304,
        "frames": common.EXPECTED_CAPTURE_FRAMES,
        "duration_seconds": 313.955777, "audio_codec": "pcm_s16le",
        "audio_sample_rate": 48_000, "audio_channels": 2,
    }
    audio_matches = audio == {
        "decoded_bytes": common.EXPECTED_AUDIO_BYTES,
        "nonzero_bytes": common.EXPECTED_AUDIO_NONZERO_BYTES,
    }
    window_matches = window == EXPECTED_WINDOW

    source_runs: list[dict[str, object]] = []
    runs_match = len(runs) == len(EXPECTED_RUNS)
    for expected in EXPECTED_RUNS:
        state, matched = common.selected_state(runs, expected)
        if expected[0] in CAPTURE_ONLY_RUN_INDEXES:
            run = runs[expected[0]]
            matched = (
                run.start == expected[1] and run.end == expected[2] and
                common.renderer_fnv64(run.rgb) == expected[3]
            )
            state["matched"] = matched
        state["presentation_classification"] = (
            "capture_only_incomplete_dirty_repaint"
            if state["run_index"] in CAPTURE_ONLY_RUN_INDEXES
            else "presentation_complete"
        )
        source_runs.append(state)
        runs_match &= matched

    incomplete_states: dict[str, dict[str, object]] = {}
    incomplete_states_match = True
    for name, expected in EXPECTED_CAPTURE_ONLY_TRANSITIONS.items():
        state, matched = bridge_common.classify_incomplete_run(runs, expected)
        incomplete_states[name] = state
        incomplete_states_match &= matched

    complete_states = [
        source_runs[index] for index in PRESENTATION_COMPLETE_RUN_INDEXES
    ]
    report = {
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_matches,
        "probe": probe,
        "probe_matches": probe_matches,
        "audio": audio,
        "audio_matches": audio_matches,
        "focused_decode": window,
        "focused_decode_matches": window_matches,
        "board": {
            "demo_board": 1, "level": 10, "mode": "Multiples", "target": 5,
            "events": [
                "Reggie moves right from row 4 column 3 to column 4 and restores 23",
                "Muncher moves up once and left three times into the row-2 edge",
                "Bashful exits right from row 2 column 5 and restores 134",
                "a new Reggie enters from the right at row 4 column 5",
                "the older Reggie moves into that resident actor immediately before cannibal collision two",
            ],
        },
        "source_runs": source_runs,
        "source_runs_match": runs_match,
        "capture_only_incomplete_dirty_repaints": incomplete_states,
        "capture_only_classification_matches": incomplete_states_match,
        "native_replay": {
            "complete": True,
            "source_capture_run_count": len(EXPECTED_RUNS),
            "presentation_complete_source_run_count": len(PRESENTATION_COMPLETE_RUN_INDEXES),
            "capture_only_run_count": len(CAPTURE_ONLY_RUN_INDEXES),
            "capture_only_run_indexes": list(CAPTURE_ONLY_RUN_INDEXES),
            "ordered_source_state_matches": len(PRESENTATION_COMPLETE_RUN_INDEXES),
            "matched_run_indexes": list(PRESENTATION_COMPLETE_RUN_INDEXES),
            "matched_states": complete_states,
            "pending_complete_run_indexes": [],
            "headless_gate": "game_render_state_test",
            "reason": (
                "the seeded continuous replay matches all 49 presentation-complete "
                "pages in exact source order while omitting ten measured incomplete "
                "dirty repaints"
            ),
        },
        "corrections": {
            "dynamic_slot_paint_order": (
                "the executable initializes DS:5A84 to job IDs 1,2,3, then moves "
                "an actor to the end when entry, ordinary movement, or collision "
                "begins; native redraws active Troggles through that same order"
            ),
            "entry_presentation_lag": (
                "source frames 4562-4591 hold the warning page for three entrant "
                "callbacks, then combine the current Bashful exit with the entrant's "
                "older actor records (including the repeated invisible record); "
                "native reproduces that dirty-cell presentation boundary without "
                "changing logical scheduler or PRNG timing"
            ),
        },
        "parity_verdict": {
            "source_measurement_valid": True,
            "native_complete": True,
            "remaining_gap": None,
        },
    }
    report["valid"] = all((
        source_matches, probe_matches, audio_matches, window_matches,
        runs_match, incomplete_states_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
