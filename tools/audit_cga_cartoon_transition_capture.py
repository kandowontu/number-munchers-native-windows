#!/usr/bin/env python3
"""Audit native-resolution CGA board-to-cartoon Wipe transitions.

The pinned scene AVIs contain the production close/load/open Wipe immediately
before each cartoon. Native deliberately presents only completed pages, while
the original single-buffered driver can expose intermediate Wipe refreshes.
This gate therefore locks the completed cyan/black pages, source cadence,
intermediate-sample inventory, and exact handoff to the native scene timeline.
"""

from __future__ import annotations

from dataclasses import asdict
from fractions import Fraction
import json
from pathlib import Path

import audit_cga_scene_capture as cga
import audit_word_scene_capture as shared


EXPECTED = {
    "Number": {
        "scene_count": 5,
        "close_samples": (3, 3, 3, 3, 3),
        "cyan_samples": (35, 35, 35, 35, 35),
        "open_samples": (4, 4, 4, 4, 4),
        "black_samples": (7, 7, 6, 5, 7),
        "initial_scene_samples": (7, 1, 2, 1, 1),
        "first_native_run": (1, 0, 0, 0, 0),
        "vga_covered_samples": (43, 43, 44, 43, 44),
    },
    "Word": {
        "scene_count": 6,
        "close_samples": (3, 4, 4, 4, 4, 3),
        "cyan_samples": (34, 35, 35, 34, 34, 35),
        "open_samples": (4, 4, 3, 4, 4, 4),
        "black_samples": (5, 8, 5, 5, 5, 6),
        "initial_scene_samples": (2, 1, 2, 1, 2, 1),
        "first_native_run": (0, 0, 0, 0, 0, 0),
        "vga_covered_samples": (42, 44, 43, 41, 42, 42),
    },
}

VGA_CAPTURE_RATE = Fraction(2190197, 31250)
BLACK_RGB = bytes((0, 0, 0)) * (shared.LOGICAL_WIDTH * shared.LOGICAL_HEIGHT)
CYAN_RGB = bytes((85, 255, 255)) * (shared.LOGICAL_WIDTH * shared.LOGICAL_HEIGHT)
BLACK_DIGEST = shared.rgb_digest(BLACK_RGB)
CYAN_DIGEST = shared.rgb_digest(CYAN_RGB)


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


def audit_game(game: str) -> dict[str, object]:
    expected = EXPECTED[game]
    scene_count, native_directory = cga.configure_engine(game)
    if scene_count != expected["scene_count"]:
        raise RuntimeError(f"{game}: unexpected scene count {scene_count}")

    game_report: dict[str, object] = {
        "game": game,
        "scene_count": scene_count,
        "completed_state_renderer_fnv64": {
            "cyan": renderer_fnv64(CYAN_RGB),
            "black": renderer_fnv64(BLACK_RGB),
        },
        "scenes": [],
    }
    all_valid = True

    for scene in range(scene_count):
        pin = cga.PINS[(game, scene)]
        capture_path = Path(pin.relative_path)
        native_runs, native_rgb, _ = shared.read_native_scene(
            native_directory, scene
        )
        capture_runs, statistics, _, _, capture_rgb = shared.decode_capture(
            capture_path, native_rgb
        )
        video = shared.probe_capture(capture_path)
        frame_rate = Fraction(str(video["frame_rate_fraction"]))
        native_digests = {run.digest for run in native_runs}

        cyan_candidates = []
        for index, run in enumerate(capture_runs):
            if run.digest != CYAN_DIGEST or run.count < 25:
                continue
            try:
                black_index = next(
                    later for later in range(index + 1, min(len(capture_runs), index + 12))
                    if capture_runs[later].digest == BLACK_DIGEST
                )
                first_native_index = next(
                    later for later in range(black_index + 1,
                                             min(len(capture_runs), black_index + 12))
                    if capture_runs[later].digest in native_digests
                )
            except StopIteration:
                continue
            cyan_candidates.append((index, black_index, first_native_index))
        if not cyan_candidates:
            raise ValueError(f"{capture_path}: CGA cartoon transition not found")
        # The first qualifying long cyan cover is the board-to-cartoon Wipe.
        # Some cartoons later animate full-screen cyan/black surfaces and can
        # otherwise form a second syntactically valid candidate.
        cyan_index, black_index, first_native_index = cyan_candidates[0]
        board_index = max(
            index for index in range(cyan_index)
            if capture_runs[index].count >= 20
            and capture_runs[index].nonuniform_blocks == 0
        )

        close_samples = sample_span(capture_runs, board_index + 1, cyan_index)
        cyan_samples = capture_runs[cyan_index].count
        open_samples = sample_span(capture_runs, cyan_index + 1, black_index)
        black_samples = capture_runs[black_index].count
        initial_samples = sample_span(
            capture_runs, black_index + 1, first_native_index
        )
        expected_native_run = int(expected["first_native_run"][scene])
        first_native_exact = (
            capture_runs[first_native_index].digest ==
                native_runs[expected_native_run].digest
            and capture_rgb[capture_runs[first_native_index].digest] ==
                native_rgb[native_runs[expected_native_run].digest]
        )

        native_covered_seconds = float(
            Fraction(int(expected["vga_covered_samples"][scene]) - 1, 1) /
            VGA_CAPTURE_RATE
        )
        captured_covered_seconds = float(Fraction(cyan_samples, 1) / frame_rate)
        # A stable N-frame run has one-sample uncertainty at each endpoint.
        # The two-frame bound therefore compares the CGA sampling phase with
        # the already pinned native/VGA loader timer without inventing a
        # display-mode-specific logical delay.
        covered_timer_consistent = (
            abs(captured_covered_seconds - native_covered_seconds) <=
            float(Fraction(2, 1) / frame_rate)
        )

        metadata_checks = {
            "capture_path": capture_path.as_posix() == pin.relative_path,
            "capture_sha256": shared.sha256_file(capture_path) == pin.sha256,
            "codec": video["codec"] == "zmbv",
            "dimensions": (video["width"], video["height"]) == (320, 200),
            "frame_rate": video["frame_rate_fraction"] == pin.frame_rate,
            "decoded_frames": statistics["decoded_frames"] == pin.decoded_frames,
            "audio_stream_count": video["audio_stream_count"] == pin.audio_streams,
        }
        sample_checks = {
            "close": close_samples == expected["close_samples"][scene],
            "covered_cyan": cyan_samples == expected["cyan_samples"][scene],
            "open": open_samples == expected["open_samples"][scene],
            "stable_black": black_samples == expected["black_samples"][scene],
            "initial_scene":
                initial_samples == expected["initial_scene_samples"][scene],
        }
        valid = (
            all(metadata_checks.values()) and all(sample_checks.values())
            and capture_runs[board_index].count >= 20
            and capture_rgb[capture_runs[cyan_index].digest] == CYAN_RGB
            and capture_rgb[capture_runs[black_index].digest] == BLACK_RGB
            and first_native_exact and covered_timer_consistent
        )
        all_valid = all_valid and valid
        scene_report = {
            "scene": scene,
            "pinned_capture": {
                "path": pin.relative_path,
                "sha256": pin.sha256,
                "decoded_frames": pin.decoded_frames,
                "frame_rate_fraction": pin.frame_rate,
                "audio_stream_count": pin.audio_streams,
            },
            "metadata_checks": metadata_checks,
            "sample_checks": sample_checks,
            "stable_board_run": run_record(capture_runs[board_index]),
            "close_intermediate_runs": [
                run_record(run)
                for run in capture_runs[board_index + 1:cyan_index]
            ],
            "covered_cyan_run": run_record(capture_runs[cyan_index]),
            "open_intermediate_runs": [
                run_record(run)
                for run in capture_runs[cyan_index + 1:black_index]
            ],
            "stable_black_run": run_record(capture_runs[black_index]),
            "initial_scene_intermediate_runs": [
                run_record(run)
                for run in capture_runs[black_index + 1:first_native_index]
            ],
            "first_exact_native_scene_run": run_record(
                capture_runs[first_native_index]
            ),
            "expected_native_run_index": expected_native_run,
            "expected_native_tick_start": native_runs[expected_native_run].start,
            "first_native_scene_exact": first_native_exact,
            "native_covered_timer_seconds": native_covered_seconds,
            "captured_covered_sample_span_seconds": captured_covered_seconds,
            "covered_timer_two_sample_phase_bound_seconds":
                float(Fraction(2, 1) / frame_rate),
            "covered_timer_consistent": covered_timer_consistent,
            "valid": valid,
            **statistics,
        }
        game_report["scenes"].append(scene_report)
        print(
            f"{game} scene {scene}: close={close_samples} cyan={cyan_samples} "
            f"open={open_samples} black={black_samples} initial={initial_samples} "
            f"native-run={expected_native_run} valid={valid}"
        )

    game_report["all_transitions_valid"] = all_valid
    return game_report


def main() -> int:
    report = {
        "schema": "cga-cartoon-transition-live-audit-v1",
        "logical_size": [shared.LOGICAL_WIDTH, shared.LOGICAL_HEIGHT],
        "capture_size": [shared.LOGICAL_WIDTH, shared.LOGICAL_HEIGHT],
        "games": [audit_game("Number"), audit_game("Word")],
    }
    report["valid"] = all(
        game["all_transitions_valid"] for game in report["games"]
    )
    output = Path("analysis/cga-live/cartoon-transition-report.json")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {output}")
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
