#!/usr/bin/env python3
"""Audit native-resolution live CGA cartoons against native scene ticks.

The minimized DOSBox-X capture path records the hardware 320x200 CGA surface
without VGA's 2x presentation scaling.  This wrapper reuses the established
scene comparison engine at scale 1 and pins every accepted source recording by
SHA-256 and decoded stream metadata.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from fractions import Fraction
import json
from pathlib import Path
import re

import audit_number_scene_capture as number
import audit_word_scene_capture as shared


@dataclass(frozen=True)
class CapturePin:
    relative_path: str
    sha256: str
    decoded_frames: int
    frame_rate: str
    audio_streams: int
    complete: bool = True


PINS: dict[tuple[str, int], CapturePin] = {
    ("Number", 0): CapturePin(
        relative_path="analysis/number-live/cga/original-number-scene-0-full.avi",
        sha256="F418D79833A40314624AE0DCDC187161DA58D4B52BB13ABD9976D2DAA6182046",
        decoded_frames=1730,
        frame_rate="14980681/250000",
        audio_streams=1,
    ),
    ("Number", 1): CapturePin(
        relative_path="analysis/number-live/cga/original-number-scene-1-full.avi",
        sha256="8E7C3B4D86B739C6F2E4EACF819462F2DFB09882AAA324A90117DB17C9EF2BB1",
        decoded_frames=1756,
        frame_rate="14980681/250000",
        audio_streams=1,
    ),
    ("Number", 2): CapturePin(
        relative_path="analysis/number-live/cga/original-number-scene-2-full.avi",
        sha256="A190076CCD8E56593DC38727C884D6A07D49A83BE9EEE6D96314535417A0C3FB",
        decoded_frames=1940,
        frame_rate="14980681/250000",
        audio_streams=1,
    ),
    ("Number", 3): CapturePin(
        relative_path="analysis/number-live/cga/original-number-scene-3-retry.avi",
        sha256="8911CBC25F7F6A6866D8ED13A9F20CE70C6ECE176F965757805D804B7CBC4F1F",
        decoded_frames=1696,
        frame_rate="14980681/250000",
        audio_streams=1,
    ),
    ("Number", 4): CapturePin(
        relative_path="analysis/number-live/cga/original-number-scene-4-full.avi",
        sha256="9942022BF311B22B08DA0C3EB5A35E0F46E572D61F090C844273C4E3BECE900F",
        decoded_frames=1755,
        frame_rate="14980681/250000",
        audio_streams=1,
    ),
    ("Word", 0): CapturePin(
        relative_path="analysis/word-live/cga/original-word-scene-0-full.avi",
        sha256="CD0740FDE2779ED65A106B61EDF1F50ED790133395537EF5AF68FB5ECB060451",
        decoded_frames=1347,
        frame_rate="14980681/250000",
        audio_streams=1,
    ),
    ("Word", 1): CapturePin(
        relative_path="analysis/word-live/cga/original-word-scene-1-full.avi",
        sha256="096C309A8528DD977AA783B21C6864221AEAAC6FF455FEBDD1414D8E4249F00C",
        decoded_frames=1593,
        frame_rate="14980681/250000",
        audio_streams=1,
    ),
    ("Word", 2): CapturePin(
        relative_path="analysis/word-live/cga/original-word-scene-2-full.avi",
        sha256="13ACEE67109C5DD6D42042511850B3BFBDD53DD4D4CB7B908CF2689BEAF6B7B8",
        decoded_frames=1189,
        frame_rate="14980681/250000",
        audio_streams=1,
    ),
    ("Word", 3): CapturePin(
        relative_path="analysis/word-live/cga/original-word-scene-3-full.avi",
        sha256="2E24EA960C2DAC428F48C72A48A49E1EF5FB6493F26DA3F36A176DB5CC55C82A",
        decoded_frames=1336,
        frame_rate="14980681/250000",
        audio_streams=1,
    ),
    ("Word", 4): CapturePin(
        relative_path="analysis/word-live/cga/original-word-scene-4-full.avi",
        sha256="ADD473174918A4AD68CEEA355744F9FCFB01F93DCE5AB42C7389B4F26EDA238D",
        decoded_frames=1339,
        frame_rate="14980681/250000",
        audio_streams=1,
    ),
    ("Word", 5): CapturePin(
        relative_path="analysis/word-live/cga/original-word-scene-5-retry.avi",
        sha256="F68476C4268378F33D06C851EE6DED77BC947E70CBC9AC58386A8FC641F60043",
        decoded_frames=2314,
        frame_rate="14980681/250000",
        audio_streams=1,
    ),
}


def configure_engine(game: str) -> tuple[int, Path]:
    if game == "Number":
        number.configure_shared_engine()
        scene_count = number.NUMBER_SCENE_COUNT
        native_directory = Path("analysis/number-live/cga/scenes/native")
    else:
        shared.SCENE_COUNT = 6
        shared.EXPECTED_TICKS = (101, 141, 132, 105, 109, 250)
        shared.TICK_NAME = re.compile(
            r"scene-(?P<scene>[0-5])-tick-(?P<tick>[0-9]{5})[.]ppm$"
        )
        shared.WORD_CARTOON_TICKS_PER_SECOND = Fraction(10, 1)
        shared.UNPRESENTED_FINAL_RUNS = 1
        scene_count = 6
        native_directory = Path("analysis/word-live/cga/scenes/native")
    shared.configure_capture_scale(1)
    return scene_count, native_directory


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", choices=("Number", "Word"), default="Number")
    parser.add_argument("--scene", type=int, default=0)
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--native-directory", type=Path)
    parser.add_argument(
        "--output", type=Path,
        default=Path("analysis/cga-live/scene-report.json"),
    )
    args = parser.parse_args()

    scene_count, default_native_directory = configure_engine(args.game)
    if not 0 <= args.scene < scene_count:
        parser.error(f"--scene must be in the range 0..{scene_count - 1}")
    pin = PINS.get((args.game, args.scene))
    if pin is None:
        parser.error(
            f"no pinned live CGA capture exists for {args.game} scene {args.scene}"
        )

    capture_path = args.capture or Path(pin.relative_path)
    native_directory = args.native_directory or default_native_directory
    if not capture_path.is_file():
        raise FileNotFoundError(capture_path)

    native_runs, native_rgb, native_tick_digests = shared.read_native_scene(
        native_directory, args.scene
    )
    if not pin.complete:
        video = shared.probe_capture(capture_path)
        capture_runs, decode, _, _, _ = shared.decode_capture(capture_path, native_rgb)
        pairs = shared.longest_common_subsequence_pairs(capture_runs, native_runs)
        blocks = shared.sequence_match_blocks(capture_runs, native_runs, pairs)
        native_digests = {run.digest for run in native_runs}
        captured_digests = {run.digest for run in capture_runs}
        capture_sha256 = shared.sha256_file(capture_path)
        metadata_checks = {
            "capture_path": capture_path.as_posix() == pin.relative_path,
            "capture_sha256": capture_sha256 == pin.sha256,
            "codec": video["codec"] == "zmbv",
            "dimensions": (video["width"], video["height"]) == (320, 200),
            "frame_rate": video["frame_rate_fraction"] == pin.frame_rate,
            "decoded_frames": decode["decoded_frames"] == pin.decoded_frames,
            "audio_stream_count": video["audio_stream_count"] == pin.audio_streams,
        }
        metadata_exact = all(metadata_checks.values())
        report = {
            "schema": "cga-scene-live-audit-v1",
            "game": args.game,
            "scene": args.scene,
            "logical_size": [shared.LOGICAL_WIDTH, shared.LOGICAL_HEIGHT],
            "capture_size": [shared.CAPTURE_WIDTH, shared.CAPTURE_HEIGHT],
            "capture_scale": shared.CAPTURE_SCALE,
            "pinned_capture": {
                "path": pin.relative_path,
                "sha256": pin.sha256,
                "decoded_frames": pin.decoded_frames,
                "frame_rate_fraction": pin.frame_rate,
                "audio_stream_count": pin.audio_streams,
                "complete": False,
            },
            "metadata_checks": metadata_checks,
            "pinned_capture_metadata_exact": metadata_exact,
            "partial_capture_evidence": {
                "capture_run_count": len(capture_runs),
                "native_full_scene_run_count": len(native_runs),
                "matched_native_run_count": len(pairs),
                "matched_native_run_indices": [pair[0] for pair in pairs],
                "matched_distinct_native_states": len(native_digests & captured_digests),
                "full_scene_distinct_native_states": len(native_digests),
                "sequence_match_blocks": blocks,
                **decode,
            },
            "capture_is_partial": True,
            "full_scene_valid": False,
            "valid": False,
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(
            f"{args.game} CGA scene {args.scene}: partial capture; "
            f"exact native runs={len(pairs)}/{len(native_runs)} "
            f"distinct={len(native_digests & captured_digests)}/{len(native_digests)}"
        )
        print(f"wrote {args.output}")
        return 1

    scene = shared.audit_scene(
        args.scene, capture_path, native_runs, native_rgb, native_tick_digests
    )
    metadata_checks = {
        "capture_path": capture_path.as_posix() == pin.relative_path,
        "capture_sha256": scene["capture_sha256"] == pin.sha256,
        "codec": scene["video"]["codec"] == "zmbv",
        "dimensions": (
            scene["video"]["width"], scene["video"]["height"]
        ) == (320, 200),
        "frame_rate": scene["video"]["frame_rate_fraction"] == pin.frame_rate,
        "decoded_frames": scene["decoded_frames"] == pin.decoded_frames,
        "audio_stream_count":
            scene["video"]["audio_stream_count"] == pin.audio_streams,
    }
    metadata_exact = all(metadata_checks.values())
    matched_native_runs: set[int] = set()
    for block in scene.get("sequence_match_blocks", []):
        start = int(block["native_run_start"])
        matched_native_runs.update(range(start, start + int(block["run_count"])))
    presented_run_count = int(scene["dos_presented_native_state_runs"])
    unmatched_native_runs = sorted(set(range(presented_run_count)) - matched_native_runs)
    unmatched_diagnostics = {
        int(item["native_run_index"]): item
        for item in scene.get("unmatched_native_run_diagnostics", [])
    }
    # The CGA scene's first callback can finish entirely between two 59.9 Hz
    # scanouts after the blocking Wipe. Accept that one missing ordered slot
    # only when its exact RGB state is observed elsewhere, every subsequent
    # native run is present in order, and every distinct native state appears.
    first_run_refresh_gap = (
        unmatched_native_runs == [0]
        and 0 in unmatched_diagnostics
        and int(unmatched_diagnostics[0]["mismatched_pixels"]) == 0
        and matched_native_runs == set(range(1, presented_run_count))
        and bool(scene["all_distinct_dos_presented_rgb_states_observed"])
    )
    presentation_exact = bool(scene["exact_pixels"]) or first_run_refresh_gap
    report = {
        "schema": "cga-scene-live-audit-v1",
        "game": args.game,
        "scene": args.scene,
        "logical_size": [shared.LOGICAL_WIDTH, shared.LOGICAL_HEIGHT],
        "capture_size": [shared.CAPTURE_WIDTH, shared.CAPTURE_HEIGHT],
        "capture_scale": shared.CAPTURE_SCALE,
        "pinned_capture": {
            "path": pin.relative_path,
            "sha256": pin.sha256,
            "decoded_frames": pin.decoded_frames,
            "frame_rate_fraction": pin.frame_rate,
            "audio_stream_count": pin.audio_streams,
            "complete": pin.complete,
        },
        "metadata_checks": metadata_checks,
        "pinned_capture_metadata_exact": metadata_exact,
        "scene_exact_pixels": scene["exact_pixels"],
        "presentation_equivalence": {
            "matched_ordered_native_run_count": len(matched_native_runs),
            "presented_native_run_count": presented_run_count,
            "unmatched_native_run_indices": unmatched_native_runs,
            "all_distinct_native_states_observed_exactly":
                scene["all_distinct_dos_presented_rgb_states_observed"],
            "first_run_refresh_gap_state_observed_elsewhere_exactly":
                first_run_refresh_gap,
            "accepted": presentation_exact,
        },
        "valid": metadata_exact and presentation_exact,
        "scene_audit": scene,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        f"{args.game} CGA scene {args.scene}: valid={report['valid']} "
        f"exact={scene['exact_pixels']} "
        f"runs={scene['matched_dos_presented_state_runs']}/"
        f"{scene['dos_presented_native_state_runs']} "
        f"distinct={scene['observed_distinct_dos_presented_rgb_states']}/"
        f"{scene['distinct_dos_presented_rgb_states']}"
    )
    print(f"wrote {args.output}")
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
