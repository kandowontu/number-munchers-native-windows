#!/usr/bin/env python3
"""Audit the board-to-cartoon Wipe embedded in the six Word scene captures.

The reference-control executables alter only predicates/scene selection; the
production type-2 close, covered loader, type-1 open, and scene owner remain
unchanged.  Each lossless AVI therefore contains a fresh Word board, the real
transition, and the selected cartoon.  DOS occasionally exposes torn 2x video
blocks while a Wipe or dirty painter is in progress.  This audit identifies
those transient samples separately and locks the completed cyan/black states,
per-bank covered hold, black-to-first-tick interval, and exact first native
scene state.
"""

from __future__ import annotations

import argparse
from dataclasses import asdict
from fractions import Fraction
import json
from pathlib import Path

from audit_word_scene_capture import (
    CAPTURE_HEIGHT,
    CAPTURE_WIDTH,
    LOGICAL_HEIGHT,
    LOGICAL_WIDTH,
    decode_capture,
    probe_capture,
    read_native_scene,
    rgb_digest,
    sha256_file,
)


SCENE_COUNT = 6
EXPECTED_COVERED_CYAN_SAMPLES = (42, 44, 43, 41, 42, 42)
EXPECTED_BLACK_SAMPLES = (7, 6, 8, 6, 6, 8)
EXPECTED_OPEN_DIRTY_SAMPLES = (1, 1, 2, 2, 2, 1)
EXPECTED_INITIAL_SCENE_DIRTY_SAMPLES = (1, 1, 0, 0, 1, 0)
BLACK_RGB = bytes((0, 0, 0)) * (LOGICAL_WIDTH * LOGICAL_HEIGHT)
CYAN_RGB = bytes((85, 255, 255)) * (LOGICAL_WIDTH * LOGICAL_HEIGHT)
BLACK_DIGEST = rgb_digest(BLACK_RGB)
CYAN_DIGEST = rgb_digest(CYAN_RGB)


def renderer_fnv64(rgb: bytes) -> str:
    value = 1469598103934665603
    for offset in range(0, len(rgb), 3):
        pixel = (rgb[offset] << 16) | (rgb[offset + 1] << 8) | rgb[offset + 2]
        value ^= pixel
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"0x{value:016x}"


def run_record(run: object) -> dict[str, object]:
    record = asdict(run)
    record["end"] = run.end
    return record


def sample_span(runs: list[object], start: int, end: int) -> int:
    return sum(run.count for run in runs[start:end])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--capture-dir", type=Path,
        default=Path("analysis/word-live/scenes/original"),
    )
    parser.add_argument(
        "--native-dir", type=Path,
        default=Path("analysis/word-live/scenes/native"),
    )
    parser.add_argument(
        "--report", type=Path,
        default=Path("analysis/word-live/scenes/transition-report.json"),
    )
    arguments = parser.parse_args()

    report: dict[str, object] = {
        "schema": "word-cartoon-transition-live-audit-v1",
        "logical_size": [LOGICAL_WIDTH, LOGICAL_HEIGHT],
        "capture_size": [CAPTURE_WIDTH, CAPTURE_HEIGHT],
        "completed_state_renderer_fnv64": {
            "cyan": renderer_fnv64(CYAN_RGB),
            "black": renderer_fnv64(BLACK_RGB),
        },
        "expected_covered_cyan_samples": list(EXPECTED_COVERED_CYAN_SAMPLES),
        "scenes": [],
    }
    all_exact = True

    for scene_index in range(SCENE_COUNT):
        capture_path = arguments.capture_dir / f"scene-{scene_index}-full.avi"
        native_runs, native_rgb, _ = read_native_scene(
            arguments.native_dir, scene_index)
        capture_runs, statistics, _, _, capture_rgb = decode_capture(
            capture_path, native_rgb)
        video = probe_capture(capture_path)
        frame_rate = Fraction(str(video["frame_rate_fraction"]))

        first_tick_digest = native_runs[0].digest
        cyan_index = next(
            index for index, run in enumerate(capture_runs)
            if run.digest == CYAN_DIGEST and run.count >= 30
            and any(later.digest == first_tick_digest
                    for later in capture_runs[index + 1:])
        )
        black_index = next(
            index for index in range(cyan_index + 1, len(capture_runs))
            if capture_runs[index].digest == BLACK_DIGEST
        )
        first_tick_index = next(
            index for index in range(black_index + 1, len(capture_runs))
            if capture_runs[index].digest == first_tick_digest
        )

        # The stable board is followed by exactly one torn close refresh and
        # then the fully covered cyan page in all six captures.
        board_index = cyan_index - 2
        close_start = board_index + 1
        open_start = cyan_index + 1
        initial_dirty_start = black_index + 1
        covered_samples = capture_runs[cyan_index].count
        black_samples = capture_runs[black_index].count
        close_dirty_samples = sample_span(capture_runs, close_start, cyan_index)
        open_dirty_samples = sample_span(capture_runs, open_start, black_index)
        initial_scene_dirty_samples = sample_span(
            capture_runs, initial_dirty_start, first_tick_index)
        covered_seconds = float(Fraction(covered_samples, 1) / frame_rate)
        black_seconds = float(Fraction(black_samples, 1) / frame_rate)

        pattern_exact = (
            board_index >= 0
            and capture_runs[board_index].nonuniform_blocks == 0
            and capture_runs[board_index].count >= 30
            and close_dirty_samples == 1
            and sum(run.nonuniform_blocks
                    for run in capture_runs[close_start:cyan_index]) > 0
            and covered_samples == EXPECTED_COVERED_CYAN_SAMPLES[scene_index]
            and open_dirty_samples == EXPECTED_OPEN_DIRTY_SAMPLES[scene_index]
            and sum(run.nonuniform_blocks
                    for run in capture_runs[open_start:black_index]) > 0
            and black_samples == EXPECTED_BLACK_SAMPLES[scene_index]
            and initial_scene_dirty_samples ==
                EXPECTED_INITIAL_SCENE_DIRTY_SAMPLES[scene_index]
            and capture_runs[first_tick_index].digest == first_tick_digest
            and capture_rgb[first_tick_digest] == native_rgb[first_tick_digest]
        )
        all_exact = all_exact and pattern_exact
        scene_report = {
            "scene": scene_index,
            "capture": str(capture_path),
            "capture_sha256": sha256_file(capture_path),
            "video": video,
            **statistics,
            "stable_board_run": run_record(capture_runs[board_index]),
            "torn_close_runs": [
                run_record(run) for run in capture_runs[close_start:cyan_index]
            ],
            "covered_cyan_run": run_record(capture_runs[cyan_index]),
            "covered_cyan_seconds_by_sample_span": covered_seconds,
            "torn_open_runs": [
                run_record(run) for run in capture_runs[open_start:black_index]
            ],
            "stable_black_run": run_record(capture_runs[black_index]),
            "stable_black_seconds_by_sample_span": black_seconds,
            "initial_scene_dirty_runs": [
                run_record(run)
                for run in capture_runs[initial_dirty_start:first_tick_index]
            ],
            "first_native_scene_run": run_record(
                capture_runs[first_tick_index]),
            "first_native_scene_tick": native_runs[0].start,
            "first_native_scene_exact": True,
            "pattern_exact": pattern_exact,
        }
        report["scenes"].append(scene_report)
        print(
            f"scene {scene_index}: cyan={covered_samples} "
            f"({covered_seconds:.6f}s) black={black_samples} "
            f"({black_seconds:.6f}s) close-dirty={close_dirty_samples} "
            f"open-dirty={open_dirty_samples} "
            f"scene-dirty={initial_scene_dirty_samples} exact={pattern_exact}"
        )

    report["all_transition_patterns_exact"] = all_exact
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {arguments.report}")
    return 0 if all_exact else 1


if __name__ == "__main__":
    raise SystemExit(main())
