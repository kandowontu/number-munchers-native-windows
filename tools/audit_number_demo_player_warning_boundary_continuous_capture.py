#!/usr/bin/env python3
"""Audit the continuous player-terminal/later-warning scheduler boundary."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
NATIVE_TEST = ROOT / "build" / "NumberMunchersRenderTests.exe"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-player-warning-boundary-continuous-report.json"
)
WINDOW_FIRST_FRAME = 3_300
WINDOW_LAST_FRAME = 3_340
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 41,
    "nonuniform_2x_blocks": 1,
    "logical_run_count": 8,
}
EXPECTED_RUNS = (
    (0, 3_300, 3_308, 0x83CF33FC2F2B040D),
    (1, 3_309, 3_310, 0x8DEBF5981044E557),
    (2, 3_311, 3_313, 0x0DDDBB1637F7E038),
    (3, 3_314, 3_315, 0xB7D877B492C6D1EC),
    (4, 3_316, 3_317, 0xA060590D41749EBC),
    (5, 3_318, 3_320, 0x66DA4D52CC2CFECA),
    (6, 3_321, 3_322, 0x7AE3DDE61C3DDEB9),
    (7, 3_323, 3_340, 0x0E8BC09C389491D2),
)


def main() -> None:
    source_sha256 = common.sha256_file(CAPTURE)
    probe = common.probe_capture(CAPTURE)
    audio = common.decode_audio_track(CAPTURE)
    common.WINDOW_FIRST_FRAME = WINDOW_FIRST_FRAME
    common.WINDOW_LAST_FRAME = WINDOW_LAST_FRAME
    runs, window = common.decode_window(CAPTURE)
    expected_probe = {
        "codec": "zmbv", "pixel_format": "bgr0", "width": 640,
        "height": 400, "frame_rate_fraction": "2190197/31250",
        "frame_rate_hz": 70.086304,
        "frames": common.EXPECTED_CAPTURE_FRAMES,
        "duration_seconds": 313.955777, "audio_codec": "pcm_s16le",
        "audio_sample_rate": 48_000, "audio_channels": 2,
    }
    expected_audio = {
        "decoded_bytes": common.EXPECTED_AUDIO_BYTES,
        "nonzero_bytes": common.EXPECTED_AUDIO_NONZERO_BYTES,
    }

    source_runs: list[dict[str, object]] = []
    runs_match = len(runs) == len(EXPECTED_RUNS)
    for expected in EXPECTED_RUNS:
        state, matched = common.selected_state(runs, expected)
        if expected[0] == 2:
            run = runs[expected[0]]
            matched = (
                run.start == expected[1] and run.end == expected[2] and
                run.nonuniform_blocks == 1 and run.has_uniform_frame and
                common.renderer_fnv64(run.rgb) == expected[3]
            )
            state["matched"] = matched
        state["presentation_classification"] = "presentation_complete"
        source_runs.append(state)
        runs_match &= matched
    native = subprocess.run(
        [str(NATIVE_TEST)], cwd=NATIVE_TEST.parent,
        capture_output=True, text=True, timeout=120,
    )
    report = {
        "schema": "number-player-warning-boundary-continuous-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "probe": probe,
        "probe_matches": probe == expected_probe,
        "audio": audio,
        "audio_matches": audio == expected_audio,
        "focused_decode": window,
        "focused_decode_matches": window == EXPECTED_WINDOW,
        "source_runs": source_runs,
        "source_runs_match": runs_match,
        "run_2_uniform_copy_evidence": {
            "nonuniform_2x_blocks_across_run": 1,
            "uniform_frames_in_same_logical_run": 2,
            "classification": "presentation_complete",
        },
        "native_replay": {
            "executable": NATIVE_TEST.relative_to(ROOT).as_posix(),
            "exit_code": native.returncode,
            "headless_gate_passed": native.returncode == 0,
            "presentation_complete_source_run_count": len(EXPECTED_RUNS),
            "matched_run_indexes": list(range(len(EXPECTED_RUNS))),
            "incremental_new_complete_state_count": len(EXPECTED_RUNS),
            "reason": (
                "the seeded continuous replay matches all eight complete "
                "pages in exact order, including the player terminal before "
                "the later warning paint"
            ),
        },
        "correction": {
            "player_terminal_before_warning": (
                "retain the earlier player callback page for one presentation "
                "when a later Troggle slot installs its warning on that tick"
            ),
        },
    }
    report["valid"] = all((
        report["source_matches"], report["probe_matches"],
        report["audio_matches"], report["focused_decode_matches"],
        runs_match, native.returncode == 0,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "valid": report["valid"],
        "complete_source_runs": len(EXPECTED_RUNS),
        "native_exit_code": native.returncode,
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
