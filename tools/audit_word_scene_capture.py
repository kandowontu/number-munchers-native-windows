#!/usr/bin/env python3
"""Audit live original Word Munchers cartoons against native scene ticks.

DOSBox-X records the 320x200 VGA surface as uniform 2x blocks in a lossless
640x400 ZMBV stream.  The native headless test dumps one 320x200 RGB PPM for
every visible recovered-script tick.  This tool decodes each AVI losslessly,
proves the 2x reconstruction, run-length encodes both clocks, and locates the
entire native state sequence inside the original recording.

Consecutive native ticks can intentionally paint the same frame.  They are
therefore compared as runs rather than mistaken for missing animation.  The
report retains the original capture repeat count for every native run so the
~70 Hz DOS video cadence can also be audited against Word's scheduler clock.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re
import subprocess
from typing import Iterator

import numpy as np
from PIL import Image


LOGICAL_WIDTH = 320
LOGICAL_HEIGHT = 200
CAPTURE_SCALE = 2
CAPTURE_WIDTH = LOGICAL_WIDTH * CAPTURE_SCALE
CAPTURE_HEIGHT = LOGICAL_HEIGHT * CAPTURE_SCALE
RGB_BYTES_PER_PIXEL = 3
CAPTURE_FRAME_BYTES = CAPTURE_WIDTH * CAPTURE_HEIGHT * RGB_BYTES_PER_PIXEL
WORD_CARTOON_TICKS_PER_SECOND = Fraction(10, 1)
SCENE_COUNT = 6
EXPECTED_TICKS = (101, 141, 132, 105, 109, 250)
TICK_NAME = re.compile(r"scene-(?P<scene>[0-5])-tick-(?P<tick>[0-9]{5})[.]ppm$")
UNPRESENTED_FINAL_RUNS = 1


def configure_capture_scale(scale: int) -> None:
    """Select the integer presentation scale used by the lossless AVI."""
    if scale < 1:
        raise ValueError(f"capture scale must be positive, got {scale}")
    global CAPTURE_SCALE, CAPTURE_WIDTH, CAPTURE_HEIGHT, CAPTURE_FRAME_BYTES
    CAPTURE_SCALE = scale
    CAPTURE_WIDTH = LOGICAL_WIDTH * scale
    CAPTURE_HEIGHT = LOGICAL_HEIGHT * scale
    CAPTURE_FRAME_BYTES = CAPTURE_WIDTH * CAPTURE_HEIGHT * RGB_BYTES_PER_PIXEL


@dataclass
class Run:
    digest: str
    start: int
    count: int
    nonuniform_blocks: int = 0
    has_uniform_frame: bool = False

    @property
    def end(self) -> int:
        return self.start + self.count - 1


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def rgb_digest(rgb: bytes) -> str:
    return hashlib.sha256(rgb).hexdigest()


def append_run(
    runs: list[Run], digest: str, index: int, nonuniform_blocks: int = 0
) -> None:
    if runs and runs[-1].digest == digest:
        runs[-1].count += 1
        runs[-1].nonuniform_blocks += nonuniform_blocks
        runs[-1].has_uniform_frame |= nonuniform_blocks == 0
    else:
        runs.append(Run(
            digest=digest,
            start=index,
            count=1,
            nonuniform_blocks=nonuniform_blocks,
            has_uniform_frame=nonuniform_blocks == 0,
        ))


def read_native_scene(
    directory: Path, scene_index: int
) -> tuple[list[Run], dict[str, bytes], list[str]]:
    paths: list[tuple[int, Path]] = []
    for path in directory.glob(f"scene-{scene_index}-tick-*.ppm"):
        match = TICK_NAME.fullmatch(path.name)
        if match and int(match.group("scene")) == scene_index:
            paths.append((int(match.group("tick")), path))
    paths.sort()
    expected_ticks = list(range(1, EXPECTED_TICKS[scene_index] + 1))
    actual_ticks = [tick for tick, _ in paths]
    if actual_ticks != expected_ticks:
        raise ValueError(
            f"scene {scene_index}: expected ticks 1..{EXPECTED_TICKS[scene_index]}, "
            f"got {actual_ticks[:3]}..{actual_ticks[-3:] if actual_ticks else []}"
        )

    runs: list[Run] = []
    unique_rgb: dict[str, bytes] = {}
    tick_digests: list[str] = []
    for tick, path in paths:
        with Image.open(path) as source:
            image = source.convert("RGB")
            if image.size != (LOGICAL_WIDTH, LOGICAL_HEIGHT):
                raise ValueError(f"{path}: expected 320x200, got {image.size}")
            rgb = image.tobytes()
        digest = rgb_digest(rgb)
        previous = unique_rgb.setdefault(digest, rgb)
        if previous != rgb:
            raise RuntimeError(f"SHA-256 collision among native frames: {path}")
        tick_digests.append(digest)
        append_run(runs, digest, tick)
    return runs, unique_rgb, tick_digests


def probe_capture(path: Path) -> dict[str, object]:
    command = [
        "ffprobe", "-v", "error", "-show_streams", "-show_format",
        "-of", "json", str(path),
    ]
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    probe = json.loads(result.stdout)
    video_streams = [stream for stream in probe["streams"] if stream["codec_type"] == "video"]
    if len(video_streams) != 1:
        raise ValueError(f"{path}: expected one video stream, got {len(video_streams)}")
    video = video_streams[0]
    if (video.get("codec_name"), video.get("width"), video.get("height")) != (
        "zmbv", CAPTURE_WIDTH, CAPTURE_HEIGHT
    ):
        raise ValueError(
            f"{path}: expected ZMBV 640x400, got "
            f"{video.get('codec_name')} {video.get('width')}x{video.get('height')}"
        )
    frame_rate = Fraction(video["r_frame_rate"])
    duration_text = probe.get("format", {}).get("duration") or video.get("duration")
    return {
        "codec": video["codec_name"],
        "width": video["width"],
        "height": video["height"],
        "frame_rate_fraction": str(frame_rate),
        "frame_rate_hz": float(frame_rate),
        "duration_seconds": float(duration_text) if duration_text is not None else None,
        "audio_stream_count": sum(
            stream["codec_type"] == "audio" for stream in probe["streams"]
        ),
    }


def read_exact(stream: object, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = stream.read(size - len(chunks))
        if not chunk:
            break
        chunks.extend(chunk)
    return bytes(chunks)


def decode_capture(
    path: Path, native_rgb: dict[str, bytes]
) -> tuple[list[Run], dict[str, int], list[str], list[int], dict[str, bytes]]:
    command = [
        "ffmpeg", "-v", "error", "-i", str(path), "-map", "0:v:0",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1",
    ]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if process.stdout is None or process.stderr is None:
        raise RuntimeError("ffmpeg did not provide its output pipes")

    runs: list[Run] = []
    frame_count = 0
    nonuniform_blocks = 0
    collision_mismatches = 0
    frame_digests: list[str] = []
    frame_nonuniform: list[int] = []
    capture_rgb: dict[str, bytes] = {}
    try:
        while True:
            frame = read_exact(process.stdout, CAPTURE_FRAME_BYTES)
            if not frame:
                break
            if len(frame) != CAPTURE_FRAME_BYTES:
                raise ValueError(
                    f"{path}: truncated RGB frame {frame_count}: "
                    f"{len(frame)}/{CAPTURE_FRAME_BYTES} bytes"
                )
            pixels = np.frombuffer(frame, dtype=np.uint8).reshape(
                CAPTURE_HEIGHT, CAPTURE_WIDTH, RGB_BYTES_PER_PIXEL
            )
            logical = pixels[0::CAPTURE_SCALE, 0::CAPTURE_SCALE]
            block_mismatch = np.zeros(
                (LOGICAL_HEIGHT, LOGICAL_WIDTH), dtype=np.bool_
            )
            for block_y in range(CAPTURE_SCALE):
                for block_x in range(CAPTURE_SCALE):
                    if block_x == 0 and block_y == 0:
                        continue
                    block_mismatch |= np.any(
                        logical != pixels[
                            block_y::CAPTURE_SCALE, block_x::CAPTURE_SCALE
                        ],
                        axis=2,
                    )
            frame_nonuniform_blocks = int(np.count_nonzero(block_mismatch))
            nonuniform_blocks += frame_nonuniform_blocks
            rgb = logical.tobytes()
            digest = rgb_digest(rgb)
            if digest in native_rgb and native_rgb[digest] != rgb:
                collision_mismatches += 1
            previous = capture_rgb.setdefault(digest, rgb)
            if previous != rgb:
                raise RuntimeError(f"SHA-256 collision among capture frames: {path}")
            frame_digests.append(digest)
            frame_nonuniform.append(frame_nonuniform_blocks)
            append_run(runs, digest, frame_count, frame_nonuniform_blocks)
            frame_count += 1
    finally:
        process.stdout.close()
    error_text = process.stderr.read().decode("utf-8", errors="replace")
    return_code = process.wait()
    if return_code:
        raise subprocess.CalledProcessError(return_code, command, stderr=error_text)
    return runs, {
        "decoded_frames": frame_count,
        "nonuniform_2x_blocks": nonuniform_blocks,
        "sha256_collision_mismatches": collision_mismatches,
    }, frame_digests, frame_nonuniform, capture_rgb


def find_subsequence(haystack: list[Run], needle: list[Run]) -> list[int]:
    if not needle or len(needle) > len(haystack):
        return []
    needle_hashes = [run.digest for run in needle]
    return [
        start for start in range(len(haystack) - len(needle) + 1)
        if [run.digest for run in haystack[start:start + len(needle)]] == needle_hashes
    ]


def longest_contiguous_match(haystack: list[Run], needle: list[Run]) -> dict[str, int]:
    previous = [0] * (len(needle) + 1)
    best_length = 0
    best_capture_end = 0
    best_native_end = 0
    for capture_index, capture_run in enumerate(haystack, start=1):
        current = [0] * (len(needle) + 1)
        for native_index, native_run in enumerate(needle, start=1):
            if capture_run.digest == native_run.digest:
                current[native_index] = previous[native_index - 1] + 1
                if current[native_index] > best_length:
                    best_length = current[native_index]
                    best_capture_end = capture_index
                    best_native_end = native_index
        previous = current
    return {
        "run_count": best_length,
        "capture_run_start": best_capture_end - best_length,
        "native_run_start": best_native_end - best_length,
    }


def longest_common_subsequence_pairs(
    capture_runs: list[Run], native_runs: list[Run]
) -> list[tuple[int, int]]:
    native_count = len(native_runs)
    capture_count = len(capture_runs)
    lengths = [[0] * (capture_count + 1) for _ in range(native_count + 1)]
    for native_index in range(native_count - 1, -1, -1):
        for capture_index in range(capture_count - 1, -1, -1):
            if native_runs[native_index].digest == capture_runs[capture_index].digest:
                lengths[native_index][capture_index] = (
                    1 + lengths[native_index + 1][capture_index + 1]
                )
            else:
                lengths[native_index][capture_index] = max(
                    lengths[native_index + 1][capture_index],
                    lengths[native_index][capture_index + 1],
                )

    pairs: list[tuple[int, int]] = []
    native_index = 0
    capture_index = 0
    while native_index < native_count and capture_index < capture_count:
        if (
            native_runs[native_index].digest == capture_runs[capture_index].digest
            and lengths[native_index][capture_index]
                == 1 + lengths[native_index + 1][capture_index + 1]
        ):
            pairs.append((native_index, capture_index))
            native_index += 1
            capture_index += 1
        elif lengths[native_index + 1][capture_index] >= lengths[native_index][capture_index + 1]:
            native_index += 1
        else:
            capture_index += 1
    return pairs


def sequence_match_blocks(
    capture_runs: list[Run], native_runs: list[Run], pairs: list[tuple[int, int]]
) -> list[dict[str, object]]:
    blocks: list[dict[str, object]] = []
    grouped: list[list[tuple[int, int]]] = []
    for pair in pairs:
        if (
            grouped
            and pair[0] == grouped[-1][-1][0] + 1
            and pair[1] == grouped[-1][-1][1] + 1
        ):
            grouped[-1].append(pair)
        else:
            grouped.append([pair])
    for group in grouped:
        native_start = group[0][0]
        capture_start = group[0][1]
        native = native_runs[native_start:native_start + len(group)]
        captured = capture_runs[capture_start:capture_start + len(group)]
        native_ticks = sum(run.count for run in native)
        capture_frames = sum(run.count for run in captured)
        blocks.append({
            "native_run_start": native_start,
            "capture_run_start": capture_start,
            "run_count": len(group),
            "native_tick_start": native[0].start,
            "native_tick_end": native[-1].end,
            "native_tick_count": native_ticks,
            "capture_frame_start": captured[0].start,
            "capture_frame_end": captured[-1].end,
            "capture_frame_count": capture_frames,
            "capture_frames_per_native_tick": capture_frames / native_ticks,
            "nonuniform_2x_blocks": sum(run.nonuniform_blocks for run in captured),
        })
    return blocks


def unmatched_native_run_diagnostics(
    capture_runs: list[Run],
    native_runs: list[Run],
    pairs: list[tuple[int, int]],
    capture_rgb: dict[str, bytes],
    native_rgb: dict[str, bytes],
) -> list[dict[str, object]]:
    """Locate the closest captured state for every native-only state run.

    A 4x sparse pass keeps the complete-capture search cheap; the eight best
    candidates are then compared at all 64,000 logical pixels.  This is
    diagnostic evidence only and does not participate in the exact hash gate.
    """
    matched = {native_index for native_index, _ in pairs}
    capture_arrays = [
        np.frombuffer(capture_rgb[run.digest], dtype=np.uint8).reshape(
            LOGICAL_HEIGHT, LOGICAL_WIDTH, RGB_BYTES_PER_PIXEL
        )
        for run in capture_runs
    ]
    results: list[dict[str, object]] = []
    for native_index, run in enumerate(native_runs):
        if native_index in matched:
            continue
        expected = np.frombuffer(native_rgb[run.digest], dtype=np.uint8).reshape(
            LOGICAL_HEIGHT, LOGICAL_WIDTH, RGB_BYTES_PER_PIXEL
        )
        sparse_scores = [
            int(np.count_nonzero(np.any(actual[::4, ::4] != expected[::4, ::4], axis=2)))
            for actual in capture_arrays
        ]
        candidates = sorted(range(len(capture_runs)), key=sparse_scores.__getitem__)[:8]
        best_index = min(
            candidates,
            key=lambda index: int(np.count_nonzero(np.any(
                capture_arrays[index] != expected, axis=2
            ))),
        )
        best = capture_arrays[best_index]
        difference = np.any(best != expected, axis=2)
        mismatch = int(np.count_nonzero(difference))
        ys, xs = np.nonzero(difference)
        results.append({
            "native_run_index": native_index,
            "native_tick_start": run.start,
            "native_tick_end": run.end,
            "native_tick_count": run.count,
            "native_rgb_sha256": run.digest.upper(),
            "closest_capture_run_index": best_index,
            "closest_capture_frame_start": capture_runs[best_index].start,
            "closest_capture_frame_end": capture_runs[best_index].end,
            "closest_capture_rgb_sha256": capture_runs[best_index].digest.upper(),
            "mismatched_pixels": mismatch,
            "mismatch_bounds": None if mismatch == 0 else [
                int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())
            ],
        })
    return results


def completed_frame_digest(
    frame_digests: list[str], start: int, end: int
) -> tuple[str, int, int]:
    start = max(0, start)
    end = min(len(frame_digests), max(start + 1, end))
    window = frame_digests[start:end]
    counts = Counter(window)
    # The original single-buffered painter can spend more than half of a
    # 100 ms tick exposing dirty intermediate states. The completed state is
    # the final sample before the next clock boundary, not necessarily the
    # statistical mode of the window.
    digest = window[-1]
    representative = end - 1
    return digest, counts[digest], representative


def stable_tick_alignment(
    frame_rate: Fraction,
    frame_digests: list[str],
    frame_nonuniform: list[int],
    capture_rgb: dict[str, bytes],
    native_tick_digests: list[str],
    native_rgb: dict[str, bytes],
    capture_runs: list[Run],
    native_runs: list[Run],
    exact_run_pairs: list[tuple[int, int]],
) -> dict[str, object]:
    frames_per_tick = frame_rate / WORD_CARTOON_TICKS_PER_SECOND
    ratio = float(frames_per_tick)
    tick_count = len(native_tick_digests)
    maximum_start = len(frame_digests) - int(np.ceil(tick_count * ratio)) - 1
    if maximum_start < 0:
        raise ValueError("capture is too short to contain the complete scene timeline")

    # Every controlled launch reaches the scene after the startup pages and
    # debugger injection, but do not bake a particular launch delay into the
    # evidence. Search the complete range capable of holding all ticks, using
    # a sparse RGB grid to choose the clock phase cheaply.
    capture_small = {
        digest: np.frombuffer(rgb, dtype=np.uint8)
            .reshape(LOGICAL_HEIGHT, LOGICAL_WIDTH, RGB_BYTES_PER_PIXEL)[::8, ::8]
        for digest, rgb in capture_rgb.items()
    }
    native_small = [
        np.frombuffer(native_rgb[digest], dtype=np.uint8)
            .reshape(LOGICAL_HEIGHT, LOGICAL_WIDTH, RGB_BYTES_PER_PIXEL)[::8, ::8]
        for digest in native_tick_digests
    ]
    anchor_estimates = [
        capture_runs[capture_index].end + 1 - native_runs[native_index].end * ratio
        for native_index, capture_index in exact_run_pairs
    ]
    if anchor_estimates:
        # Repeated static frames later in a scene can give LCS several valid
        # pairings and therefore very broad median estimates. The earliest
        # matched native run is unambiguous in these controlled launches; its
        # end boundary gives the phase directly.
        best_start = int(round(anchor_estimates[0]))
        best_start = min(max(0, best_start), maximum_start)
        calibration_method = "earliest exact-state run end"
        phase_score: int | None = None
    else:
        scoring_ticks = min(40, tick_count)
        best_key: tuple[int, int, int] | None = None
        best_start = 0
        for candidate in range(maximum_start + 1):
            mismatch_score = 0
            modal_hold = 0
            for tick_index in range(scoring_ticks):
                start = int(np.floor(candidate + tick_index * ratio))
                end = int(np.floor(candidate + (tick_index + 1) * ratio))
                digest, count, _ = completed_frame_digest(frame_digests, start, end)
                modal_hold += count
                mismatch_score += int(np.count_nonzero(np.any(
                    capture_small[digest] != native_small[tick_index], axis=2
                )))
            key = (mismatch_score, -modal_hold, candidate)
            if best_key is None or key < best_key:
                best_key = key
                best_start = candidate
        calibration_method = "minimum first-40-tick sparse-RGB error"
        phase_score = best_key[0] if best_key else None

    ticks: list[dict[str, object]] = []
    exact_ticks = 0
    mismatched_pixels = 0
    selected_nonuniform = 0
    for tick_index, native_digest in enumerate(native_tick_digests):
        start = int(np.floor(best_start + tick_index * ratio))
        end = int(np.floor(best_start + (tick_index + 1) * ratio))
        digest, completed_hold, representative = completed_frame_digest(
            frame_digests, start, end
        )
        actual = np.frombuffer(capture_rgb[digest], dtype=np.uint8).reshape(
            LOGICAL_HEIGHT, LOGICAL_WIDTH, RGB_BYTES_PER_PIXEL
        )
        expected = np.frombuffer(native_rgb[native_digest], dtype=np.uint8).reshape(
            LOGICAL_HEIGHT, LOGICAL_WIDTH, RGB_BYTES_PER_PIXEL
        )
        mismatch = int(np.count_nonzero(np.any(actual != expected, axis=2)))
        exact_ticks += mismatch == 0
        mismatched_pixels += mismatch
        selected_nonuniform += frame_nonuniform[representative]
        ticks.append({
            "tick": tick_index + 1,
            "capture_window_start": start,
            "capture_window_end": end - 1,
            "representative_capture_frame": representative,
            "completed_state_capture_frame_count": completed_hold,
            "nonuniform_2x_blocks": frame_nonuniform[representative],
            "mismatched_pixels": mismatch,
            "original_rgb_sha256": digest.upper(),
            "native_rgb_sha256": native_digest.upper(),
        })

    return {
        "capture_frame_start": best_start,
        "capture_frame_end": int(np.floor(best_start + tick_count * ratio)) - 1,
        "frames_per_tick_fraction": str(frames_per_tick),
        "frames_per_tick": ratio,
        "calibration_method": calibration_method,
        "exact_state_anchor_count": len(anchor_estimates),
        "anchor_start_estimate_min": min(anchor_estimates) if anchor_estimates else None,
        "anchor_start_estimate_max": max(anchor_estimates) if anchor_estimates else None,
        "selected_anchor_start_estimate": anchor_estimates[0] if anchor_estimates else None,
        "phase_search_first_tick_mismatches_on_40x25_grid": phase_score,
        "tick_count": tick_count,
        "pixel_exact_tick_count": exact_ticks,
        "all_stable_ticks_pixel_exact": exact_ticks == tick_count,
        "mismatched_pixels": mismatched_pixels,
        "selected_nonuniform_2x_blocks": selected_nonuniform,
        "ticks": ticks,
    }


def audit_scene(
    scene_index: int,
    capture_path: Path,
    native_runs: list[Run],
    native_rgb: dict[str, bytes],
    native_tick_digests: list[str],
) -> dict[str, object]:
    probe = probe_capture(capture_path)
    capture_runs, decode, frame_digests, frame_nonuniform, capture_rgb = decode_capture(
        capture_path, native_rgb
    )
    frame_rate = Fraction(str(probe["frame_rate_fraction"]))
    matches = find_subsequence(capture_runs, native_runs)
    pairs = longest_common_subsequence_pairs(capture_runs, native_runs)
    matched_native_indices = {native_index for native_index, _ in pairs}
    # The last native run starts on the callback that publishes signal 99.
    # The DOS scene owner tears down immediately from that signal, before the
    # dirty painter's resulting internal framebuffer can be presented. Keep it
    # in the report as a VM state but gate visual parity on runs before it.
    # A terminal callback is visually absent only when it creates a distinct
    # run beginning on that terminal tick. If opcode FF merely unlinks an
    # actor without dirtying the retained framebuffer, the last run may have
    # begun earlier and is therefore an ordinary DOS-presented state.
    terminal_run_is_unpresented = bool(
        UNPRESENTED_FINAL_RUNS
        and native_runs
        and native_runs[-1].start == EXPECTED_TICKS[scene_index]
    )
    presented_run_count = len(native_runs) - int(terminal_run_is_unpresented)
    all_presented_runs_exact = all(
        native_index in matched_native_indices
        for native_index in range(presented_run_count)
    )
    presented_pairs = [
        pair for pair in pairs if pair[0] < presented_run_count
    ]
    presented_nonuniform_blocks = sum(
        capture_runs[capture_index].nonuniform_blocks
        for _, capture_index in presented_pairs
    )
    presented_runs_without_uniform_frame = sum(
        not capture_runs[capture_index].has_uniform_frame
        for _, capture_index in presented_pairs
    )
    presented_native_digests = {
        native_runs[index].digest for index in range(presented_run_count)
    }
    stable_capture_digests = {
        run.digest for run in capture_runs if run.has_uniform_frame
    }
    observed_presented_digests = presented_native_digests & stable_capture_digests
    stable_alignment = stable_tick_alignment(
        frame_rate,
        frame_digests,
        frame_nonuniform,
        capture_rgb,
        native_tick_digests,
        native_rgb,
        capture_runs,
        native_runs,
        pairs,
    )
    result: dict[str, object] = {
        "scene": scene_index,
        "capture": str(capture_path),
        "capture_sha256": sha256_file(capture_path),
        "video": probe,
        **decode,
        "native_tick_count": EXPECTED_TICKS[scene_index],
        "native_unique_state_runs": len(native_runs),
        "capture_unique_state_runs": len(capture_runs),
        "exact_state_sequence_matches": len(matches),
        "full_internal_state_sequence_exact": bool(matches),
        "dos_presented_native_state_runs": presented_run_count,
        "matched_dos_presented_state_runs": len(presented_pairs),
        "all_dos_presented_stable_states_exact": all_presented_runs_exact,
        "distinct_dos_presented_rgb_states": len(presented_native_digests),
        "observed_distinct_dos_presented_rgb_states":
            len(observed_presented_digests),
        "all_distinct_dos_presented_rgb_states_observed":
            observed_presented_digests == presented_native_digests,
        "matched_dos_presented_state_nonuniform_2x_blocks":
            presented_nonuniform_blocks,
        "matched_dos_presented_state_runs_without_uniform_frame":
            presented_runs_without_uniform_frame,
        "exact_pixels": all_presented_runs_exact
                        and presented_runs_without_uniform_frame == 0
                        and decode["sha256_collision_mismatches"] == 0,
        "fixed_phase_sample_mismatched_pixels":
            stable_alignment["mismatched_pixels"],
        "stable_tick_alignment": stable_alignment,
    }
    if not matches:
        blocks = sequence_match_blocks(capture_runs, native_runs, pairs)
        result["longest_contiguous_state_match"] = longest_contiguous_match(
            capture_runs, native_runs
        )
        result["sequence_match_blocks"] = blocks
        result["matched_native_state_runs"] = len(pairs)
        result["all_native_stable_states_exact"] = len(pairs) == len(native_runs)
        result["matched_stable_state_nonuniform_2x_blocks"] = sum(
            capture_runs[capture_index].nonuniform_blocks
            for _, capture_index in pairs
        )
        result["unmatched_native_run_diagnostics"] = unmatched_native_run_diagnostics(
            capture_runs, native_runs, pairs, capture_rgb, native_rgb
        )
        if terminal_run_is_unpresented:
            result["unpresented_signal_transition_run"] = {
                "native_run_index": len(native_runs) - 1,
                "native_tick_start": native_runs[-1].start,
                "native_tick_end": native_runs[-1].end,
                "rgb_sha256": native_runs[-1].digest.upper(),
            }
        return result

    start = matches[0]
    aligned = capture_runs[start:start + len(native_runs)]
    frames_per_tick = frame_rate / WORD_CARTOON_TICKS_PER_SECOND
    timing_runs: list[dict[str, object]] = []
    for native, captured in zip(native_runs, aligned, strict=True):
        expected_capture_frames = native.count * frames_per_tick
        timing_runs.append({
            "native_tick_start": native.start,
            "native_tick_end": native.end,
            "native_tick_count": native.count,
            "capture_frame_start": captured.start,
            "capture_frame_end": captured.end,
            "capture_frame_count": captured.count,
            "expected_capture_frames": float(expected_capture_frames),
            "frame_count_error": float(captured.count - expected_capture_frames),
            "rgb_sha256": native.digest.upper(),
        })
    captured_frame_count = sum(run.count for run in aligned)
    expected_frame_count = EXPECTED_TICKS[scene_index] * frames_per_tick
    result.update({
        "matched_capture_run_start": start,
        "matched_capture_frame_start": aligned[0].start,
        "matched_capture_frame_end": aligned[-1].end,
        "matched_capture_frame_count": captured_frame_count,
        "expected_capture_frame_count": float(expected_frame_count),
        "capture_frame_count_error": float(captured_frame_count - expected_frame_count),
        "frames_per_native_tick": float(frames_per_tick),
        "timing_runs": timing_runs,
    })
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--capture-directory", type=Path,
        default=Path("analysis/word-live/scenes/original"),
    )
    parser.add_argument(
        "--native-directory", type=Path,
        default=Path("analysis/word-live/scenes/native"),
    )
    parser.add_argument(
        "--output", type=Path,
        default=Path("analysis/word-live/scenes/report.json"),
    )
    args = parser.parse_args()

    scenes: list[dict[str, object]] = []
    for scene_index in range(SCENE_COUNT):
        capture_path = args.capture_directory / f"scene-{scene_index}-full.avi"
        if not capture_path.is_file():
            raise FileNotFoundError(capture_path)
        native_runs, native_rgb, native_tick_digests = read_native_scene(
            args.native_directory, scene_index
        )
        scene = audit_scene(
            scene_index, capture_path, native_runs, native_rgb, native_tick_digests
        )
        scenes.append(scene)
        print(
            f"scene {scene_index}: exact={scene['exact_pixels']} "
            f"ticks={scene['native_tick_count']} "
            f"presented-runs={scene['matched_dos_presented_state_runs']}/"
            f"{scene['dos_presented_native_state_runs']}"
        )

    report = {
        "schema": "word-scene-live-audit-v2",
        "logical_size": [LOGICAL_WIDTH, LOGICAL_HEIGHT],
        "capture_size": [CAPTURE_WIDTH, CAPTURE_HEIGHT],
        "word_cartoon_ticks_per_second_fraction": str(WORD_CARTOON_TICKS_PER_SECOND),
        "word_cartoon_ticks_per_second": float(WORD_CARTOON_TICKS_PER_SECOND),
        "all_scenes_exact": all(scene["exact_pixels"] for scene in scenes),
        "total_native_ticks": sum(EXPECTED_TICKS),
        # This secondary timing-phase diagnostic deliberately includes DOS's
        # partially painted video samples. It is not the exact stable-state
        # pixel gate represented by all_scenes_exact above.
        "fixed_phase_sample_total_mismatched_pixels": sum(
            int(scene.get("fixed_phase_sample_mismatched_pixels", 0))
            for scene in scenes
        ),
        "scenes": scenes,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {args.output}")
    return 0 if report["all_scenes_exact"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
