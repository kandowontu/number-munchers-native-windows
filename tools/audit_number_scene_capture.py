#!/usr/bin/env python3
"""Audit lossless live Number Munchers cartoons against native scene ticks.

The shared comparison engine reconstructs each 320x200 VGA frame from the
lossless DOSBox-X 2x capture, run-length encodes both clocks, and compares the
completed state at every 10 Hz script tick.  Number's terminal signal callback
is not included in the native dump, so every dumped state is a DOS-presented
state and participates in the exact-pixel gate.

The default audited set is all five controlled recordings.  Every recording
contains its complete ordered DOS-presented timeline, including all 165 scene-3
runs and the full terminal four-frame loop.
"""

from __future__ import annotations

import argparse
from fractions import Fraction
import json
from pathlib import Path
import re

import audit_word_scene_capture as shared


NUMBER_SCENE_COUNT = 5
NUMBER_EXPECTED_TICKS = (182, 198, 222, 193, 185)
DEFAULT_SCENES = (0, 1, 2, 3, 4)


def configure_shared_engine() -> None:
    shared.SCENE_COUNT = NUMBER_SCENE_COUNT
    shared.EXPECTED_TICKS = NUMBER_EXPECTED_TICKS
    shared.WORD_CARTOON_TICKS_PER_SECOND = Fraction(10, 1)
    shared.TICK_NAME = re.compile(
        r"scene-(?P<scene>[0-4])-tick-(?P<tick>[0-9]{5})[.]ppm$"
    )
    shared.UNPRESENTED_FINAL_RUNS = 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--capture-directory", type=Path,
        default=Path("analysis/number-live/scenes/original"),
    )
    parser.add_argument(
        "--native-directory", type=Path,
        default=Path("analysis/number-live/scenes/native"),
    )
    parser.add_argument(
        "--output", type=Path,
        default=Path("analysis/number-live/scenes/report.json"),
    )
    parser.add_argument(
        "--scenes", type=int, nargs="+", default=list(DEFAULT_SCENES),
        help="validated scene indices to audit (default: 0 1 2 3 4)",
    )
    parser.add_argument(
        "--require-all-scenes", action="store_true",
        help="also fail unless all five validated scene captures are supplied",
    )
    args = parser.parse_args()

    scene_indices = list(dict.fromkeys(args.scenes))
    if not scene_indices or any(
        scene < 0 or scene >= NUMBER_SCENE_COUNT for scene in scene_indices
    ):
        parser.error("--scenes must contain unique indices in the range 0..4")

    configure_shared_engine()
    scenes: list[dict[str, object]] = []
    for scene_index in scene_indices:
        capture_path = args.capture_directory / f"scene-{scene_index}-full.avi"
        if not capture_path.is_file():
            raise FileNotFoundError(capture_path)
        native_runs, native_rgb, native_tick_digests = shared.read_native_scene(
            args.native_directory, scene_index
        )
        scene = shared.audit_scene(
            scene_index, capture_path, native_runs, native_rgb, native_tick_digests
        )
        scenes.append(scene)
        print(
            f"scene {scene_index}: exact={scene['exact_pixels']} "
            f"ticks={scene['native_tick_count']} "
            f"presented-runs={scene['matched_dos_presented_state_runs']}/"
            f"{scene['dos_presented_native_state_runs']}"
        )

    missing = sorted(set(range(NUMBER_SCENE_COUNT)) - set(scene_indices))
    all_captured_exact = all(bool(scene["exact_pixels"]) for scene in scenes)
    all_captured_distinct_states_observed = all(
        bool(scene["all_distinct_dos_presented_rgb_states_observed"])
        for scene in scenes
    )
    all_five_exact = not missing and all_captured_exact
    all_five_distinct_states_observed = (
        not missing and all_captured_distinct_states_observed
    )
    report = {
        "schema": "number-scene-live-audit-v1",
        "logical_size": [shared.LOGICAL_WIDTH, shared.LOGICAL_HEIGHT],
        "capture_size": [shared.CAPTURE_WIDTH, shared.CAPTURE_HEIGHT],
        "number_cartoon_ticks_per_second_fraction": "10",
        "number_cartoon_ticks_per_second": 10.0,
        "captured_scene_indices": scene_indices,
        "missing_scene_indices": missing,
        "all_captured_scenes_exact": all_captured_exact,
        "all_captured_distinct_dos_presented_rgb_states_observed":
            all_captured_distinct_states_observed,
        "all_five_scenes_captured_and_all_distinct_states_observed":
            all_five_distinct_states_observed,
        "all_five_scenes_captured_and_exact": all_five_exact,
        "audited_native_ticks": sum(NUMBER_EXPECTED_TICKS[index] for index in scene_indices),
        "total_expected_native_ticks": sum(NUMBER_EXPECTED_TICKS),
        "fixed_phase_sample_total_mismatched_pixels": sum(
            int(scene.get("fixed_phase_sample_mismatched_pixels", 0))
            for scene in scenes
        ),
        "scenes": scenes,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {args.output}")
    if not all_captured_exact:
        return 1
    if args.require_all_scenes and not all_five_exact:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
