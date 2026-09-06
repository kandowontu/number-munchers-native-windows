#!/usr/bin/env python3
"""Measure Number Munchers' board-to-cartoon Wipe without GUI playback.

The controlled lossless recordings contain a long cyan covered run, the torn
type-1 open, a stable white target, optional first-paint transients, and then
10 Hz stable scene states.  Detection deliberately does not require native
scene hashes, allowing this report to diagnose the native palette/painter
before exact parity has been reached.
"""

from __future__ import annotations

import argparse
from dataclasses import asdict
from fractions import Fraction
import json
from pathlib import Path
import re

import audit_word_scene_capture as shared


SCENE_COUNT = 5
DEFAULT_SCENES = (0, 1, 2, 3, 4)
BLACK_RGB = bytes((0, 0, 0)) * (shared.LOGICAL_WIDTH * shared.LOGICAL_HEIGHT)
CYAN_RGB = bytes((85, 255, 255)) * (shared.LOGICAL_WIDTH * shared.LOGICAL_HEIGHT)
WHITE_RGB = bytes((255, 255, 255)) * (shared.LOGICAL_WIDTH * shared.LOGICAL_HEIGHT)
BLACK_DIGEST = shared.rgb_digest(BLACK_RGB)
CYAN_DIGEST = shared.rgb_digest(CYAN_RGB)
WHITE_DIGEST = shared.rgb_digest(WHITE_RGB)


def run_record(run: object) -> dict[str, object]:
    record = asdict(run)
    record["end"] = run.end
    return record


def renderer_fnv64(rgb: bytes) -> str:
    value = 1469598103934665603
    for offset in range(0, len(rgb), 3):
        pixel = (rgb[offset] << 16) | (rgb[offset + 1] << 8) | rgb[offset + 2]
        value ^= pixel
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"0x{value:016x}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--capture-dir", type=Path,
        default=Path("analysis/number-live/scenes/original"),
    )
    parser.add_argument(
        "--native-dir", type=Path,
        default=Path("analysis/number-live/scenes/native"),
    )
    parser.add_argument(
        "--report", type=Path,
        default=Path("analysis/number-live/scenes/transition-report.json"),
    )
    parser.add_argument(
        "--scenes", type=int, nargs="+", default=list(DEFAULT_SCENES),
    )
    arguments = parser.parse_args()
    scene_indices = list(dict.fromkeys(arguments.scenes))
    if not scene_indices or any(scene < 0 or scene >= SCENE_COUNT for scene in scene_indices):
        parser.error("--scenes must contain indices in the range 0..4")

    report: dict[str, object] = {
        "schema": "number-cartoon-transition-live-audit-v1",
        "logical_size": [shared.LOGICAL_WIDTH, shared.LOGICAL_HEIGHT],
        "capture_size": [shared.CAPTURE_WIDTH, shared.CAPTURE_HEIGHT],
        "completed_state_renderer_fnv64": {
            "cyan": renderer_fnv64(CYAN_RGB),
            "black": renderer_fnv64(BLACK_RGB),
            "white": renderer_fnv64(WHITE_RGB),
        },
        "captured_scene_indices": scene_indices,
        "missing_scene_indices": sorted(set(range(SCENE_COUNT)) - set(scene_indices)),
        "scenes": [],
    }

    for scene_index in scene_indices:
        capture_path = arguments.capture_dir / f"scene-{scene_index}-full.avi"
        # Native RGB is supplied only for collision checking and the optional
        # first-tick comparison. Transition localization itself is hash-free.
        shared.SCENE_COUNT = SCENE_COUNT
        shared.EXPECTED_TICKS = (182, 198, 222, 193, 185)
        shared.TICK_NAME = re.compile(
            r"scene-(?P<scene>[0-4])-tick-(?P<tick>[0-9]{5})[.]ppm$"
        )
        native_runs, native_rgb, _ = shared.read_native_scene(
            arguments.native_dir, scene_index
        )
        capture_runs, statistics, _, _, capture_rgb = shared.decode_capture(
            capture_path, native_rgb
        )
        video = shared.probe_capture(capture_path)
        frame_rate = Fraction(str(video["frame_rate_fraction"]))

        cyan_index = next(
            index for index, run in enumerate(capture_runs)
            if run.digest == CYAN_DIGEST and run.count >= 30
        )
        target_index = next(
            index for index in range(cyan_index + 1, len(capture_runs))
            if capture_runs[index].digest == WHITE_DIGEST
        )
        first_scene_index = next(
            index for index in range(target_index + 1, len(capture_runs))
            if capture_runs[index].digest not in (
                BLACK_DIGEST, CYAN_DIGEST, WHITE_DIGEST
            )
            and capture_runs[index].count >= 4
            and capture_runs[index].nonuniform_blocks == 0
        )

        board_index = max(0, cyan_index - 2)
        close_runs = capture_runs[board_index + 1:cyan_index]
        open_runs = capture_runs[cyan_index + 1:target_index]
        initial_runs = capture_runs[target_index + 1:first_scene_index]
        cyan_samples = capture_runs[cyan_index].count
        target_samples = capture_runs[target_index].count
        first_scene_run = capture_runs[first_scene_index]
        first_native_digest = native_runs[0].digest
        scene_report = {
            "scene": scene_index,
            "capture": str(capture_path),
            "capture_sha256": shared.sha256_file(capture_path),
            "video": video,
            **statistics,
            "stable_board_run": run_record(capture_runs[board_index]),
            "torn_close_runs": [run_record(run) for run in close_runs],
            "covered_cyan_run": run_record(capture_runs[cyan_index]),
            "covered_cyan_seconds_by_sample_span": float(
                Fraction(cyan_samples, 1) / frame_rate
            ),
            "torn_open_runs": [run_record(run) for run in open_runs],
            "stable_target_color": "white",
            "stable_target_run": run_record(capture_runs[target_index]),
            "stable_target_seconds_by_sample_span": float(
                Fraction(target_samples, 1) / frame_rate
            ),
            "initial_scene_dirty_runs": [run_record(run) for run in initial_runs],
            "first_stable_scene_run": run_record(first_scene_run),
            "first_stable_scene_rgb_sha256": first_scene_run.digest.upper(),
            "native_tick_1_rgb_sha256": first_native_digest.upper(),
            "first_native_scene_exact": first_scene_run.digest == first_native_digest,
        }
        report["scenes"].append(scene_report)
        print(
            f"scene {scene_index}: cyan={cyan_samples} "
            f"white={target_samples} close={sum(run.count for run in close_runs)} "
            f"open={sum(run.count for run in open_runs)} "
            f"initial={sum(run.count for run in initial_runs)} "
            f"first={first_scene_run.start} exact={scene_report['first_native_scene_exact']}"
        )

    report["all_captured_first_native_scenes_exact"] = all(
        bool(scene["first_native_scene_exact"]) for scene in report["scenes"]
    )
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {arguments.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
