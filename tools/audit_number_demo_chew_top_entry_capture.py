#!/usr/bin/env python3
"""Audit the first-Demo correct chew and overlapping top Reggie entry."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-chew-top-entry-report.json"
)

WINDOW_FIRST_FRAME = 5_216
WINDOW_LAST_FRAME = 5_331
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 116,
    "nonuniform_2x_blocks": 18,
    "logical_run_count": 18,
}

# name: run, first, last, renderer FNV-64, player state, entry state, warning.
EXPECTED_COMPLETE_STATES = {
    "pre_chew_player_idle":
        (0, 5_216, 5_236, 0x364C0086FE50EA46, 0, 0, True),
    "chew_tick_0_record_12":
        (1, 5_237, 5_239, 0x88A6BC3C750F5D31, 1, 0, True),
    "chew_tick_1_record_13":
        (2, 5_240, 5_241, 0x0C49574C442045281, 2, 0, True),
    "chew_tick_2_record_14":
        (3, 5_242, 5_243, 0x88A6BC3C750F5D31, 3, 0, True),
    "chew_tick_3_record_13":
        (5, 5_245, 5_246, 0x0C49574C442045281, 4, 0, True),
    "chew_tick_4_record_12":
        (6, 5_247, 5_248, 0x88A6BC3C750F5D31, 5, 0, True),
    "chew_tick_5_record_13":
        (7, 5_249, 5_251, 0x0C49574C442045281, 6, 0, True),
    "chew_tick_6_record_14":
        (8, 5_252, 5_253, 0x88A6BC3C750F5D31, 7, 0, True),
    "terminal_record_13_hold":
        (9, 5_254, 5_313, 0x0C49574C442045281, 8, 0, True),
    "warning_removed_entry_phase_1_invisible":
        (11, 5_315, 5_319, 0x9109D9DF3FFEF78B, 8, 1, False),
    "top_entry_phase_2":
        (12, 5_320, 5_321, 0x00652CF1BE948BE5, 8, 2, False),
    "top_entry_phase_3":
        (13, 5_322, 5_323, 0x4CEA0C8DBFC9B06F, 8, 3, False),
    "top_entry_phase_4":
        (15, 5_325, 5_326, 0x0D7CF12C93E39149, 8, 4, False),
    "top_entry_phase_5":
        (16, 5_327, 5_328, 0xB527905804BB3EA7, 8, 5, False),
    "entry_terminal_overlap_hold":
        (17, 5_329, 5_331, 0x9109D9DF3FFEF78B, 8, 6, False),
}

EXPECTED_ARTIFACTS = {
    "chew_tick_2_to_3_scanout": {
        "run_index": 4,
        "first_frame": 5_244,
        "last_frame": 5_244,
        "nonuniform_2x_blocks": 0,
        "renderer_fnv64": 0x1B5CB440613A5841,
        "partition": {"previous_only": 736, "next_only": 224,
                      "common": 255_040, "neither": 0},
        "physical_boundary_row": 340,
    },
    "warning_removal_scanout": {
        "run_index": 10,
        "first_frame": 5_314,
        "last_frame": 5_314,
        "nonuniform_2x_blocks": 0,
        "renderer_fnv64": 0xD810BA893161DC43,
        "partition": {"previous_only": 1_016, "next_only": 364,
                      "common": 254_620, "neither": 0},
        "physical_boundary_row": 236,
    },
    "top_entry_phase_3_to_4_torn_repaint": {
        "run_index": 14,
        "first_frame": 5_324,
        "last_frame": 5_324,
        "nonuniform_2x_blocks": 18,
        "renderer_fnv64": 0x30D642C102C75E9E,
        "partition": {"previous_only": 1_048, "next_only": 320,
                      "common": 254_484, "neither": 148},
        "nonuniform_logical_blocks": [
            [125, 43], [126, 43], [127, 43],
            [131, 43], [132, 43], [133, 43], [134, 43],
            [135, 43], [136, 43], [137, 43], [138, 43],
            [139, 43], [140, 43], [141, 43], [142, 43],
            [145, 43], [146, 43], [147, 43],
        ],
        "neither_physical_bbox": [250, 82, 295, 86],
    },
}


def decode_physical_frame(frame: int) -> np.ndarray:
    result = subprocess.run(
        [
            "ffmpeg", "-v", "error", "-i", str(CAPTURE), "-map", "0:v:0",
            "-vf", f"select=eq(n\\,{frame})", "-frames:v", "1",
            "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1",
        ],
        check=True,
        capture_output=True,
    )
    if len(result.stdout) != common.CAPTURE_FRAME_BYTES:
        raise ValueError(f"frame {frame} did not decode to one physical page")
    return np.frombuffer(result.stdout, dtype=np.uint8).reshape(
        common.CAPTURE_HEIGHT, common.CAPTURE_WIDTH, 3
    )


def classify_artifact(
    name: str, expected: dict[str, object], runs: list[common.Run]
) -> tuple[dict[str, object], bool]:
    run = runs[int(expected["run_index"])]
    frame = int(expected["first_frame"])
    previous = decode_physical_frame(frame - 1)
    current = decode_physical_frame(frame)
    following = decode_physical_frame(frame + 1)

    previous_match = np.all(current == previous, axis=2)
    following_match = np.all(current == following, axis=2)
    previous_only = previous_match & ~following_match
    next_only = following_match & ~previous_match
    common_pixels = previous_match & following_match
    neither = ~previous_match & ~following_match
    partition = {
        "previous_only": int(np.count_nonzero(previous_only)),
        "next_only": int(np.count_nonzero(next_only)),
        "common": int(np.count_nonzero(common_pixels)),
        "neither": int(np.count_nonzero(neither)),
    }

    logical = current[0::2, 0::2]
    nonuniform = (
        np.any(logical != current[0::2, 1::2], axis=2) |
        np.any(logical != current[1::2, 0::2], axis=2) |
        np.any(logical != current[1::2, 1::2], axis=2)
    )
    rows, columns = np.where(nonuniform)
    nonuniform_blocks = [
        [int(column), int(row)] for row, column in zip(rows, columns)
    ]
    actual_hash = common.renderer_fnv64(run.rgb)
    result: dict[str, object] = {
        "run_index": int(expected["run_index"]),
        "first_frame": run.start,
        "last_frame": run.end,
        "frames": run.end - run.start + 1,
        "nonuniform_2x_blocks": run.nonuniform_blocks,
        "renderer_fnv64_from_top_left_samples": f"0x{actual_hash:016x}",
        "physical_pixel_partition": partition,
        "nonuniform_logical_blocks": nonuniform_blocks,
        "classification": "capture-only incomplete refresh",
    }

    if "physical_boundary_row" in expected:
        previous_rows = np.where(previous_only)[0]
        next_rows = np.where(next_only)[0]
        strict_boundary = (
            previous_rows.size > 0 and next_rows.size > 0 and
            int(previous_rows.max()) < int(next_rows.min())
        )
        boundary = int(next_rows.min()) if strict_boundary else None
        result.update({
            "classification": "capture-only exact horizontal scanout splice",
            "strict_horizontal_boundary": strict_boundary,
            "physical_boundary_row": boundary,
            "logical_boundary_row": (
                boundary // 2 if boundary is not None else None
            ),
            "reason": (
                "every physical pixel belongs to one complete neighbor, with "
                "the previous page strictly above the following page"
            ),
        })
    else:
        neither_rows, neither_columns = np.where(neither)
        bbox = [
            int(neither_columns.min()), int(neither_rows.min()),
            int(neither_columns.max()), int(neither_rows.max()),
        ]
        result.update({
            "neither_physical_bbox": bbox,
            "reason": (
                "the nonuniform entry repaint contains physical pixels that "
                "belong to neither completed neighboring page"
            ),
        })

    matched = all((
        run.start == int(expected["first_frame"]),
        run.end == int(expected["last_frame"]),
        run.nonuniform_blocks == int(expected["nonuniform_2x_blocks"]),
        actual_hash == int(expected["renderer_fnv64"]),
        partition == expected["partition"],
        (
            result.get("physical_boundary_row") ==
                expected.get("physical_boundary_row")
        ),
        (
            nonuniform_blocks == expected.get(
                "nonuniform_logical_blocks", nonuniform_blocks
            )
        ),
        (
            result.get("neither_physical_bbox") ==
                expected.get("neither_physical_bbox",
                             result.get("neither_physical_bbox"))
        ),
    ))
    result["matched"] = matched
    return result, matched


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
        "frame_rate_hz": 70.086304, "frames": common.EXPECTED_CAPTURE_FRAMES,
        "duration_seconds": 313.955777, "audio_codec": "pcm_s16le",
        "audio_sample_rate": 48_000, "audio_channels": 2,
    }
    audio_matches = audio == {
        "decoded_bytes": common.EXPECTED_AUDIO_BYTES,
        "nonzero_bytes": common.EXPECTED_AUDIO_NONZERO_BYTES,
    }
    window_matches = window == EXPECTED_WINDOW

    selected_states: dict[str, dict[str, object]] = {}
    selected_states_match = True
    for name, expected in EXPECTED_COMPLETE_STATES.items():
        state, matched = common.selected_state(runs, expected[:4])
        state["logical_state"] = {
            "player_state": expected[4],
            "top_entry_state": expected[5],
            "warning": expected[6],
            "resident_reggie": "row_0_column_2",
            "bashful": "row_4_column_3_up_dwell",
        }
        selected_states[name] = state
        selected_states_match &= matched

    artifacts: dict[str, dict[str, object]] = {}
    artifacts_match = True
    for name, expected in EXPECTED_ARTIFACTS.items():
        artifact, matched = classify_artifact(name, expected, runs)
        artifacts[name] = artifact
        artifacts_match &= matched

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
        "oracle_scope": {
            "classification": "continuous first-board replay evidence",
            "continuous_replay_claim": True,
            "reason": (
                "the deterministic native replay reaches this interval between "
                "the bottom-entry and third-cannibal gates and matches all "
                "fifteen presentation-complete pages in source order"
            ),
        },
        "board": {
            "mode": "Multiples", "target": 5, "footer": "Demo",
            "events": [
                "Muncher performs the exact seven-record correct chew of 220 at row 4 column 0",
                "the terminal record-13 page remains resident through the Demo wait",
                "the warning is removed through an exact scanout splice",
                "a Reggie enters from the top onto the resident row-0-column-2 Reggie",
                "the entry terminal paints an ordinary overlapping endpoint for one public tick before cannibal record 12",
                "Bashful dwells at row 4 column 3 throughout",
            ],
            "labels_row_major_before_chew": [
                "21", "", "", "", "133", "11",
                "", "", "", "", "", "",
                "", "", "", "", "", "134",
                "59", "211", "176", "14", "2", "230",
                "220", "207", "115", "23", "125", "175",
            ],
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "capture_only_incomplete_state_count": len(EXPECTED_ARTIFACTS),
        "capture_only_incomplete_states": artifacts,
        "capture_only_incomplete_states_match": artifacts_match,
        "native_replay": {
            "complete": True,
            "exact_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "incremental_new_complete_state_count": (
                len(EXPECTED_COMPLETE_STATES) - 1
            ),
            "overlap": (
                "the opening player-idle page is the terminal page of the "
                "later bottom-entry audit"
            ),
            "presentation_fix": (
                "an entry-terminal cannibal overlap now retains its ordinary "
                "endpoint page until the first state-5 callback on the next "
                "public tick instead of exposing bite record 12 immediately"
            ),
            "exact_complete_states_presented": list(EXPECTED_COMPLETE_STATES),
            "headless_gate": "game_render_state_test",
        },
    }
    report["valid"] = all((
        source_matches, probe_matches, audio_matches, window_matches,
        selected_states_match, artifacts_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
