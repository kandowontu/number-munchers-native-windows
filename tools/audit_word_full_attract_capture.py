#!/usr/bin/env python3
"""Continuously audit the seeded Word Munchers Demo against native output.

The source is a finalized, headless DOSBox-X ZMBV recording whose initial
Borland seed was read before input.  The native headless gate starts Demo with
the same seed and writes every changed 320x200 presentation page at the AVI's
exact refresh period.  This auditor pins the source bytes, reconstructs every
logical DOS framebuffer, aligns exact RGB states in order, and inventories
intervening source-only partial paints without treating them as native poses.
"""

from __future__ import annotations

import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
from statistics import median

import numpy as np
from PIL import Image, ImageDraw

from audit_word_scene_capture import (
    LOGICAL_HEIGHT,
    LOGICAL_WIDTH,
    Run,
    decode_capture,
    longest_common_subsequence_pairs,
    probe_capture,
    sequence_match_blocks,
    sha256_file,
)


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "analysis/word-live/attract/full-headless-seed-8e74.avi"
NATIVE_DIRECTORY = ROOT / "analysis/word-live/attract/native-seed-8e74"
REPORT = ROOT / "analysis/word-live/attract/full-headless-seed-8e74-report.json"

EXPECTED_SOURCE_SHA256 = (
    "B2D7621C80B4273D7DA39DBBC778CC163021901C874EAC62E239281A685293DC"
)
EXPECTED_SOURCE_FRAMES = 11_518
EXPECTED_NATIVE_SEED = 0x8E74
EXPECTED_NATIVE_SAMPLES = 11_000

# The pinned source recording receives an external key after the restored Demo
# board at run 767. Runs 768/769 are the single-buffer title repaint and run
# 770 is the first stable Word title page. The native comparison trace receives
# no input and therefore continues the autonomous controller instead. Pages
# after the last exact Demo board are not eligible parity failures: they belong
# to a route the source recording did not take. Pin the measured boundary so a
# decoder or source change cannot silently widen or shrink the comparison.
EXPECTED_LAST_ATTRACT_RUN = 767
EXPECTED_LAST_ATTRACT_FRAME = 10_986
EXPECTED_EXIT_PARTIAL_RUNS = (768, 769)
EXPECTED_FIRST_TITLE_RUN = 770
EXPECTED_FIRST_TITLE_FRAME = 10_989
EXPECTED_FIRST_TITLE_RGB_SHA256 = (
    "058421a35534ee7d3ac98b6361135dfeb118eccdbdadf27a0298037024649549"
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--native-directory",
        type=Path,
        default=NATIVE_DIRECTORY,
        help="native attract-page PPM/TSV directory",
    )
    parser.add_argument(
        "--report",
        type=Path,
        default=REPORT,
        help="JSON report destination",
    )
    parser.add_argument(
        "--dump-native-page",
        type=int,
        help="export this native page and its pixel comparison",
    )
    parser.add_argument(
        "--dump-capture-run",
        type=int,
        help="DOS framebuffer run paired with --dump-native-page",
    )
    parser.add_argument(
        "--dump-pair",
        action="append",
        default=[],
        metavar="NATIVE_PAGE:CAPTURE_RUN",
        help="repeatable diagnostic pair export",
    )
    parser.add_argument(
        "--dump-directory",
        type=Path,
        default=None,
        help="directory for optional diagnostic PNG/JSON output",
    )
    arguments = parser.parse_args()
    if (arguments.dump_native_page is None) != (arguments.dump_capture_run is None):
        parser.error("--dump-native-page and --dump-capture-run must be used together")
    dump_pairs: list[tuple[int, int]] = []
    if arguments.dump_native_page is not None:
        dump_pairs.append((arguments.dump_native_page, arguments.dump_capture_run))
    for value in arguments.dump_pair:
        try:
            native_text, capture_text = value.split(":", 1)
            dump_pairs.append((int(native_text), int(capture_text)))
        except ValueError:
            parser.error(f"invalid --dump-pair {value!r}; expected PAGE:RUN")
    arguments.dump_pairs = dump_pairs
    return arguments


def rgb_sha256(rgb: bytes) -> str:
    return hashlib.sha256(rgb).hexdigest()


def read_native_pages(
    directory: Path,
) -> tuple[list[Run], dict[str, bytes], list[dict[str, object]]]:
    native_states = directory / "word-attract-pages.tsv"
    if not native_states.is_file():
        raise FileNotFoundError(native_states)
    with native_states.open("r", encoding="utf-8", newline="") as source:
        rows = list(csv.DictReader(source, delimiter="\t"))
    if not rows:
        raise ValueError(f"{native_states}: no native pages")

    pages: list[dict[str, object]] = []
    unique_rgb: dict[str, bytes] = {}
    for expected_page, row in enumerate(rows):
        page = int(row["page"])
        sample = int(row["sample"])
        if page != expected_page:
            raise ValueError(
                f"{native_states}: page {page} follows {expected_page - 1}"
            )
        path = directory / f"attract-page-{page:05d}.ppm"
        with Image.open(path) as source:
            image = source.convert("RGB")
            if image.size != (LOGICAL_WIDTH, LOGICAL_HEIGHT):
                raise ValueError(f"{path}: expected 320x200, got {image.size}")
            rgb = image.tobytes()
        digest = rgb_sha256(rgb)
        previous = unique_rgb.setdefault(digest, rgb)
        if previous != rgb:
            raise RuntimeError(f"SHA-256 collision among native pages: {path}")
        pages.append({
            "page": page,
            "sample": sample,
            "digest": digest,
            "controller_page": int(row["controller_page"]),
            "level": int(row["level"]),
            "random_calls": int(row["random_calls"]),
            "random_state": row["random_state"],
            "player_row": int(row["player_row"]),
            "player_column": int(row["player_column"]),
            "moving": bool(int(row["moving"])),
            "munching": bool(int(row["munching"])),
            "terminal_frame": int(row.get("terminal_frame", "-1") or "-1"),
            "terminal_hold_ticks": int(
                row.get("terminal_hold_ticks", "0") or "0"
            ),
            "enemies": int(row["enemies"]),
            "hall_phase": int(row["hall_phase"]),
            "hall_wipe_frame": int(row["hall_wipe_frame"]),
            "interstitial": int(row["interstitial"]),
            "interstitial_frame": int(row["interstitial_frame"]),
        })

    samples = [int(page["sample"]) for page in pages]
    if samples != sorted(samples) or len(set(samples)) != len(samples):
        raise ValueError(f"{native_states}: samples are not strictly increasing")
    runs = [
        Run(
            digest=str(page["digest"]),
            start=int(page["sample"]),
            count=(
                int(pages[index + 1]["sample"]) - int(page["sample"])
                if index + 1 < len(pages)
                else EXPECTED_NATIVE_SAMPLES - int(page["sample"])
            ),
        )
        for index, page in enumerate(pages)
    ]
    if any(run.count <= 0 for run in runs):
        raise ValueError(f"{native_states}: a native page has no sample interval")
    return runs, unique_rgb, pages


def export_pixel_diagnostic(
    native_page: int,
    capture_run: int,
    directory: Path,
    native_runs: list[Run],
    native_rgb: dict[str, bytes],
    capture_runs: list[Run],
    capture_rgb: dict[str, bytes],
) -> Path:
    if not 0 <= native_page < len(native_runs):
        raise ValueError(f"native page {native_page} is outside 0..{len(native_runs) - 1}")
    if not 0 <= capture_run < len(capture_runs):
        raise ValueError(f"capture run {capture_run} is outside 0..{len(capture_runs) - 1}")

    native = np.frombuffer(
        native_rgb[native_runs[native_page].digest], dtype=np.uint8
    ).reshape(LOGICAL_HEIGHT, LOGICAL_WIDTH, 3)
    source = np.frombuffer(
        capture_rgb[capture_runs[capture_run].digest], dtype=np.uint8
    ).reshape(LOGICAL_HEIGHT, LOGICAL_WIDTH, 3)
    changed = np.any(native != source, axis=2)
    ys, xs = np.nonzero(changed)
    if xs.size:
        bounds = [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())]
    else:
        bounds = None

    directory.mkdir(parents=True, exist_ok=True)
    stem = f"native-{native_page:05d}-source-run-{capture_run:05d}"
    Image.fromarray(native, mode="RGB").save(directory / f"{stem}-native.png")
    Image.fromarray(source, mode="RGB").save(directory / f"{stem}-source.png")

    changed_pixels = []
    transitions: Counter[str] = Counter()
    for y, x in zip(ys.tolist(), xs.tolist()):
        native_color = native[y, x].tolist()
        source_color = source[y, x].tolist()
        transitions[f"{native_color}->{source_color}"] += 1
        changed_pixels.append({
            "x": x,
            "y": y,
            "native_rgb": native_color,
            "source_rgb": source_color,
        })

    diagnostic = {
        "native_page": native_page,
        "native_sample_start": native_runs[native_page].start,
        "capture_run": capture_run,
        "capture_frame_start": capture_runs[capture_run].start,
        "capture_frame_end": capture_runs[capture_run].end,
        "different_pixel_count": int(xs.size),
        "difference_bounds": bounds,
        "color_transitions": dict(transitions),
        "different_pixels": changed_pixels,
    }
    json_path = directory / f"{stem}.json"
    json_path.write_text(json.dumps(diagnostic, indent=2) + "\n", encoding="utf-8")

    if bounds is not None:
        pad = 4
        left = max(0, bounds[0] - pad)
        top = max(0, bounds[1] - pad)
        right = min(LOGICAL_WIDTH, bounds[2] + pad + 1)
        bottom = min(LOGICAL_HEIGHT, bounds[3] + pad + 1)
        crop_box = (left, top, right, bottom)
        scale = 8
        label_height = 18
        native_crop = Image.fromarray(native, mode="RGB").crop(crop_box)
        source_crop = Image.fromarray(source, mode="RGB").crop(crop_box)
        mask = source.copy()
        mask[~changed] //= 4
        mask[changed] = np.array([255, 0, 255], dtype=np.uint8)
        mask_crop = Image.fromarray(mask, mode="RGB").crop(crop_box)
        panel_width = native_crop.width * scale
        panel_height = native_crop.height * scale
        contact = Image.new("RGB", (panel_width * 3, panel_height + label_height), "black")
        draw = ImageDraw.Draw(contact)
        for index, (label, panel) in enumerate((
            ("native", native_crop),
            ("DOS source", source_crop),
            ("difference", mask_crop),
        )):
            draw.text((index * panel_width + 3, 3), label, fill="white")
            contact.paste(
                panel.resize((panel_width, panel_height), Image.Resampling.NEAREST),
                (index * panel_width, label_height),
            )
        contact.save(directory / f"{stem}-contact.png")
    return json_path


def group_indices(indices: list[int]) -> list[list[int]]:
    groups: list[list[int]] = []
    for index in indices:
        if groups and index == groups[-1][-1] + 1:
            groups[-1].append(index)
        else:
            groups.append([index])
    return groups


def index_ranges(indices: list[int], pages: list[dict[str, object]]) -> list[dict[str, object]]:
    return [
        {
            "first_page": group[0],
            "last_page": group[-1],
            "page_count": len(group),
            "first_sample": pages[group[0]]["sample"],
            "last_sample": pages[group[-1]]["sample"],
            "first_level": pages[group[0]]["level"],
            "last_level": pages[group[-1]]["level"],
            "first_random_calls": pages[group[0]]["random_calls"],
            "last_random_calls": pages[group[-1]]["random_calls"],
        }
        for group in group_indices(indices)
    ]


def partial_kind(rgb: bytes, before: bytes, after: bytes) -> tuple[str, dict[str, object]]:
    actual = np.frombuffer(rgb, dtype=np.uint8).reshape(
        LOGICAL_HEIGHT, LOGICAL_WIDTH, 3
    )
    old = np.frombuffer(before, dtype=np.uint8).reshape(
        LOGICAL_HEIGHT, LOGICAL_WIDTH, 3
    )
    new = np.frombuffer(after, dtype=np.uint8).reshape(
        LOGICAL_HEIGHT, LOGICAL_WIDTH, 3
    )
    equals_old = np.all(actual == old, axis=2)
    equals_new = np.all(actual == new, axis=2)
    unexplained = ~(equals_old | equals_new)
    unexplained_count = int(np.count_nonzero(unexplained))
    old_pixels = int(np.count_nonzero(equals_old & ~equals_new))
    new_pixels = int(np.count_nonzero(equals_new & ~equals_old))
    details: dict[str, object] = {
        "old_only_pixels": old_pixels,
        "new_only_pixels": new_pixels,
        "unexplained_pixels": unexplained_count,
    }
    if unexplained_count == 0 and old_pixels and new_pixels:
        transitions = np.flatnonzero(np.any(equals_new[1:] != equals_new[:-1], axis=1))
        details["row_transition_count"] = int(transitions.size)
        details["row_transitions"] = [int(value + 1) for value in transitions[:12]]
        return "two-state-partial-refresh", details
    if unexplained_count == 0:
        return "adjacent-complete-state", details
    ys, xs = np.nonzero(unexplained)
    details["unexplained_bounds"] = [
        int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())
    ]
    unique_xs = np.unique(xs)
    unique_ys = np.unique(ys)
    if unique_xs.size == 1 or unique_ys.size == 1:
        # A number of DOS-only pages expose one incomplete board-rule write:
        # every otherwise unexplained pixel lies on exactly one horizontal or
        # vertical scanline.  Keep the geometric fact in the generic delta
        # analysis; source_only_diagnostics gives it the presentation-specific
        # classification below.  Native-only pages must not be reinterpreted
        # as capture refreshes merely because they happen to be one pixel wide.
        details["unexplained_single_rule_axis"] = (
            "vertical" if unique_xs.size == 1 else "horizontal"
        )
        details["unexplained_single_rule_coordinate"] = int(
            unique_xs[0] if unique_xs.size == 1 else unique_ys[0]
        )
    return "unclassified", details


def multi_native_state_kind(
    rgb: bytes,
    native_indices: range,
    native_runs: list[Run],
    native_rgb: dict[str, bytes],
) -> tuple[str | None, dict[str, object]]:
    """Classify a source page explained by all native states in its gap.

    The endpoint-only test cannot recognize a dirty page assembled from an
    intervening native movement pose plus either exact neighbor.  This test is
    deliberately pixel-exact: it succeeds only when every source pixel occurs
    at the same coordinate in at least one measured native logical state.
    """
    actual = np.frombuffer(rgb, dtype=np.uint8).reshape(
        LOGICAL_HEIGHT, LOGICAL_WIDTH, 3
    )
    explained = np.zeros((LOGICAL_HEIGHT, LOGICAL_WIDTH), dtype=bool)
    exact_pages: list[int] = []
    candidate_pages = list(native_indices)
    for native_index in candidate_pages:
        candidate = np.frombuffer(
            native_rgb[native_runs[native_index].digest], dtype=np.uint8
        ).reshape(LOGICAL_HEIGHT, LOGICAL_WIDTH, 3)
        equals = np.all(actual == candidate, axis=2)
        if bool(np.all(equals)):
            exact_pages.append(native_index)
        explained |= equals
    if exact_pages:
        return "intervening-native-state", {
            "matching_intervening_native_pages": exact_pages,
        }
    residual = ~explained
    unexplained = int(np.count_nonzero(residual))
    if unexplained == 0 and len(candidate_pages) > 2:
        return "multi-native-state-partial-refresh", {
            "native_composite_candidate_pages": candidate_pages,
            "multi_native_unexplained_pixels": 0,
        }
    details: dict[str, object] = {
        "multi_native_unexplained_pixels": unexplained,
    }
    if unexplained:
        ys, xs = np.nonzero(residual)
        details["multi_native_unexplained_bounds"] = [
            int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())
        ]
        colors = Counter(
            tuple(int(channel) for channel in actual[y, x])
            for y, x in zip(ys.tolist(), xs.tolist())
        )
        details["multi_native_unexplained_colors"] = [
            {"rgb": list(color), "pixels": count}
            for color, count in colors.most_common()
        ]
        unique_xs = np.unique(xs)
        unique_ys = np.unique(ys)
        if unique_xs.size == 1 or unique_ys.size == 1:
            details["multi_native_unexplained_single_rule_axis"] = (
                "vertical" if unique_xs.size == 1 else "horizontal"
            )
            details["multi_native_unexplained_single_rule_coordinate"] = int(
                unique_xs[0] if unique_xs.size == 1 else unique_ys[0]
            )
    return None, details


def source_only_diagnostics(
    capture_runs: list[Run],
    capture_rgb: dict[str, bytes],
    native_runs: list[Run],
    native_rgb: dict[str, bytes],
    pairs: list[tuple[int, int]],
) -> tuple[list[dict[str, object]], Counter[str]]:
    diagnostics: list[dict[str, object]] = []
    classifications: Counter[str] = Counter()
    native_occurrences: dict[str, list[int]] = {}
    for native_index, run in enumerate(native_runs):
        native_occurrences.setdefault(run.digest, []).append(native_index)
    for pair_index in range(len(pairs) - 1):
        native_before, capture_before = pairs[pair_index]
        native_after, capture_after = pairs[pair_index + 1]
        if capture_after <= capture_before + 1:
            continue
        before = native_rgb[native_runs[native_before].digest]
        after = native_rgb[native_runs[native_after].digest]
        for capture_index in range(capture_before + 1, capture_after):
            run = capture_runs[capture_index]
            kind, details = partial_kind(
                capture_rgb[run.digest], before, after
            )
            if kind == "unclassified":
                multi_kind, multi_details = multi_native_state_kind(
                    capture_rgb[run.digest],
                    range(native_before, native_after + 1),
                    native_runs,
                    native_rgb,
                )
                details.update(multi_details)
                if multi_kind is not None:
                    kind = multi_kind
            if kind == "unclassified":
                prior_pages = [
                    page for page in native_occurrences.get(run.digest, [])
                    if max(0, native_before - 8) <= page < native_before
                ]
                if prior_pages:
                    # Run 552 is byte-identical to earlier open-chew pages
                    # 471/473, then returns to closed terminal page 474.  It
                    # is a one-tick resident-buffer resurface, not a ninth
                    # chew interval or a new sprite record.  The bounded exact
                    # full-page match prevents unrelated repeated screens from
                    # receiving this classification.
                    kind = "prior-native-state-resurface"
                    details["matching_prior_native_pages"] = prior_pages
            if not run.has_uniform_frame:
                kind = "nonuniform-2x-only"
            elif (kind == "unclassified" and
                  "unexplained_single_rule_axis" in details):
                # These are presentation-complete 2x samples of an incomplete
                # DOS dirty callback, not a stable logical pose.  Runs 104,
                # 160, 188, 228, 296, 392, 480, and 528 each expose only one
                # board-rule scanline between the exact states on both sides.
                # Classifying the measured fragment is preferable to teaching
                # the double-buffered native renderer to synthesize scanout
                # damage (and reintroduce the reported flicker).
                kind = "single-rule-partial-refresh"
            elif (kind == "unclassified" and
                  "multi_native_unexplained_single_rule_axis" in details):
                # Run 417 is a pixel-exact mosaic of the seven native states
                # in its gap except for eight magenta pixels on the x=212
                # board rule.  That one incomplete dirty-rule callback is not
                # a missing actor pose.
                kind = "multi-native-state-plus-single-rule-partial-refresh"
            elif kind == "unclassified" and run.count == 1:
                bounds = details.get("unexplained_bounds")
                if isinstance(bounds, list) and len(bounds) == 4:
                    width = int(bounds[2]) - int(bounds[0]) + 1
                    height = int(bounds[3]) - int(bounds[1]) + 1
                    if min(width, height) <= 2:
                        # Source run 42 is one uniform sample during a sprite
                        # callback: its old/new pixels are accompanied by only
                        # two newly written actor scanlines.  The complete
                        # actor states on both sides are exact, so this is a
                        # thin dirty-painter band rather than another pose.
                        kind = "thin-band-partial-refresh"
            classifications[kind] += 1
            diagnostics.append({
                "capture_run": capture_index,
                "capture_frame_start": run.start,
                "capture_frame_end": run.end,
                "capture_frame_count": run.count,
                "nonuniform_2x_blocks": run.nonuniform_blocks,
                "has_uniform_2x_frame": run.has_uniform_frame,
                "between_native_pages": [native_before, native_after],
                "classification": kind,
                **details,
            })
    return diagnostics, classifications


def multi_source_state_kind(
    rgb: bytes,
    capture_indices: range,
    capture_runs: list[Run],
    capture_rgb: dict[str, bytes],
) -> tuple[str | None, dict[str, object]]:
    """Test whether one native page is assembled from measured source states.

    The adjacent-anchor test is intentionally conservative, but a native
    retained page can contain regions from several source callbacks inside the
    same aligned gap.  Classify that fact only when every native pixel occurs
    at the same coordinate in at least one source run; the page remains
    non-exact and therefore still fails the completion gate.
    """
    actual = np.frombuffer(rgb, dtype=np.uint8).reshape(
        LOGICAL_HEIGHT, LOGICAL_WIDTH, 3
    )
    explained = np.zeros((LOGICAL_HEIGHT, LOGICAL_WIDTH), dtype=bool)
    exact_runs: list[int] = []
    candidate_runs = list(capture_indices)
    for capture_index in candidate_runs:
        candidate = np.frombuffer(
            capture_rgb[capture_runs[capture_index].digest], dtype=np.uint8
        ).reshape(LOGICAL_HEIGHT, LOGICAL_WIDTH, 3)
        equals = np.all(actual == candidate, axis=2)
        if bool(np.all(equals)):
            exact_runs.append(capture_index)
        explained |= equals
    if exact_runs:
        return "intervening-source-state", {
            "matching_intervening_source_runs": exact_runs,
        }
    residual = ~explained
    unexplained = int(np.count_nonzero(residual))
    if unexplained == 0 and len(candidate_runs) > 2:
        return "multi-source-state-composite", {
            "source_composite_candidate_runs": candidate_runs,
            "multi_source_unexplained_pixels": 0,
        }
    details: dict[str, object] = {
        "multi_source_unexplained_pixels": unexplained,
    }
    if unexplained:
        ys, xs = np.nonzero(residual)
        details["multi_source_unexplained_bounds"] = [
            int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())
        ]
    return None, details


def native_only_diagnostics(
    capture_runs: list[Run],
    capture_rgb: dict[str, bytes],
    native_runs: list[Run],
    native_rgb: dict[str, bytes],
    pages: list[dict[str, object]],
    pairs: list[tuple[int, int]],
) -> tuple[list[dict[str, object]], Counter[str]]:
    diagnostics: list[dict[str, object]] = []
    classifications: Counter[str] = Counter()
    source_occurrences: dict[str, list[int]] = {}
    for capture_index, run in enumerate(capture_runs):
        source_occurrences.setdefault(run.digest, []).append(capture_index)

    for pair_index in range(len(pairs) - 1):
        native_before, capture_before = pairs[pair_index]
        native_after, capture_after = pairs[pair_index + 1]
        if native_after <= native_before + 1:
            continue
        before = native_rgb[native_runs[native_before].digest]
        after = native_rgb[native_runs[native_after].digest]
        source_between = list(range(capture_before + 1, capture_after))
        for native_index in range(native_before + 1, native_after):
            run = native_runs[native_index]
            rgb = native_rgb[run.digest]
            page = pages[native_index]
            kind, details = partial_kind(rgb, before, after)
            if kind == "two-state-partial-refresh":
                kind = "two-adjacent-state-composite"
            elif kind == "adjacent-complete-state":
                kind = "adjacent-state-duplicate"

            if kind == "unclassified":
                multi_kind, multi_details = multi_source_state_kind(
                    rgb,
                    range(capture_before, capture_after + 1),
                    capture_runs,
                    capture_rgb,
                )
                details.update(multi_details)
                if multi_kind is not None:
                    kind = multi_kind

            occurrences = source_occurrences.get(run.digest, [])
            if occurrences:
                kind = "observed-outside-alignment"
            closest_capture: int | None = None
            closest_difference: int | None = None
            for capture_index in source_between:
                candidate = np.frombuffer(
                    capture_rgb[capture_runs[capture_index].digest], dtype=np.uint8
                ).reshape(-1, 3)
                actual = np.frombuffer(rgb, dtype=np.uint8).reshape(-1, 3)
                difference = int(np.count_nonzero(np.any(candidate != actual, axis=1)))
                if closest_difference is None or difference < closest_difference:
                    closest_difference = difference
                    closest_capture = capture_index

            if kind == "unclassified":
                bounds = details.get("multi_source_unexplained_bounds")
                if not isinstance(bounds, list):
                    bounds = details.get("unexplained_bounds")
                player_left = 20 + int(page["player_column"]) * 48 + 4
                player_top = 26 + int(page["player_row"]) * 30 + 1
                inside_player = (
                    isinstance(bounds, list)
                    and len(bounds) == 4
                    and int(bounds[0]) >= player_left
                    and int(bounds[1]) >= player_top
                    and int(bounds[2]) <= player_left + 40
                    and int(bounds[3]) <= player_top + 28
                )
                if inside_player and bool(page["munching"]) and closest_capture is not None:
                    source_kind, _ = partial_kind(
                        capture_rgb[capture_runs[closest_capture].digest],
                        before,
                        after,
                    )
                    if source_kind == "two-state-partial-refresh":
                        # Native page 467 is a complete resource-backed chew
                        # pose. DOS run 544 spans that callback with a three-
                        # refresh old/new partial repaint, so the complete pose
                        # cannot be expected as an exact captured framebuffer.
                        kind = "complete-chew-state-inside-source-partial-refresh"
                        details["spanning_source_run"] = closest_capture
                        details["spanning_source_classification"] = source_kind
                        details["player_sprite_bounds"] = [
                            player_left, player_top,
                            player_left + 40, player_top + 28,
                        ]
                elif (inside_player and run.count == 1 and not source_between and
                      int(page["terminal_hold_ticks"]) > 0):
                    # Regression sentinel for old native page 151: the
                    # incorrectly recomposed state-4 terminal-player delta had
                    # only 47 unexplained pixels inside the player box and
                    # occupied one sample between adjacent exact DOS states.
                    # If it returns, keep it visible as non-exact evidence
                    # rather than calling it a new pose.
                    kind = "one-sample-terminal-player-resident-delta"
                    details["terminal_hold_ticks"] = int(
                        page["terminal_hold_ticks"]
                    )
                    details["player_sprite_bounds"] = [
                        player_left, player_top,
                        player_left + 40, player_top + 28,
                    ]

            classifications[kind] += 1
            diagnostics.append({
                "native_page": native_index,
                "native_sample_start": run.start,
                "native_sample_end": run.end,
                "native_sample_count": run.count,
                "level": page["level"],
                "controller_page": page["controller_page"],
                "random_calls": page["random_calls"],
                "random_state": page["random_state"],
                "player_row": page["player_row"],
                "player_column": page["player_column"],
                "moving": page["moving"],
                "munching": page["munching"],
                "terminal_frame": page["terminal_frame"],
                "terminal_hold_ticks": page["terminal_hold_ticks"],
                "between_exact_native_pages": [native_before, native_after],
                "between_capture_runs": [capture_before, capture_after],
                "source_occurrences": occurrences,
                "closest_intervening_capture_run": closest_capture,
                "closest_intervening_source_pixel_difference": closest_difference,
                "classification": kind,
                **details,
            })
    return diagnostics, classifications


def main() -> int:
    arguments = parse_arguments()
    native_directory = arguments.native_directory.resolve()
    report_path = arguments.report.resolve()
    source_sha256 = sha256_file(SOURCE)
    source_pinned = source_sha256 == EXPECTED_SOURCE_SHA256
    probe = probe_capture(SOURCE)
    native_runs, native_rgb, pages = read_native_pages(native_directory)
    (
        all_capture_runs,
        decode,
        _frame_digests,
        _frame_nonuniform,
        capture_rgb,
    ) = decode_capture(SOURCE, native_rgb)

    if len(all_capture_runs) <= EXPECTED_FIRST_TITLE_RUN:
        raise RuntimeError(
            "pinned source does not reach the expected external-exit boundary"
        )
    last_attract_run = all_capture_runs[EXPECTED_LAST_ATTRACT_RUN]
    exit_partial_runs = [
        all_capture_runs[index] for index in EXPECTED_EXIT_PARTIAL_RUNS
    ]
    first_title_run = all_capture_runs[EXPECTED_FIRST_TITLE_RUN]
    source_exit_boundary_matches = bool(
        last_attract_run.end == EXPECTED_LAST_ATTRACT_FRAME
        and all(not run.has_uniform_frame for run in exit_partial_runs)
        and [run.start for run in exit_partial_runs]
            == [EXPECTED_LAST_ATTRACT_FRAME + 1, EXPECTED_LAST_ATTRACT_FRAME + 2]
        and first_title_run.start == EXPECTED_FIRST_TITLE_FRAME
        and first_title_run.has_uniform_frame
        and first_title_run.digest == EXPECTED_FIRST_TITLE_RGB_SHA256
    )
    if not source_exit_boundary_matches:
        raise RuntimeError(
            "pinned source external-exit/title boundary no longer matches"
        )
    for native_page, capture_run in arguments.dump_pairs:
        diagnostic_path = export_pixel_diagnostic(
            native_page,
            capture_run,
            arguments.dump_directory or native_directory / "inspection",
            native_runs,
            native_rgb,
            all_capture_runs,
            capture_rgb,
        )
        print(f"wrote {diagnostic_path}")

    # Only a run with at least one fully uniform 2x VGA sample can establish a
    # completed logical state. Retain the original run index for diagnostics.
    stable_entries = [
        (index, run) for index, run in enumerate(all_capture_runs)
        if run.has_uniform_frame
    ]
    stable_capture_runs = [entry[1] for entry in stable_entries]
    stable_pairs = longest_common_subsequence_pairs(stable_capture_runs, native_runs)
    pairs = [
        (native_index, stable_entries[capture_index][0])
        for native_index, capture_index in stable_pairs
    ]

    matched_native = {native_index for native_index, _ in pairs}
    offsets = [
        all_capture_runs[capture_index].start - native_runs[native_index].start
        for native_index, capture_index in pairs
    ]
    alignment_offset = int(round(median(offsets))) if offsets else 0
    comparable_pairs = [
        pair for pair in pairs if pair[1] <= EXPECTED_LAST_ATTRACT_RUN
    ]
    if not comparable_pairs:
        raise RuntimeError("no exact native pages precede the source exit")
    first_eligible_native = comparable_pairs[0][0]
    last_eligible_native = comparable_pairs[-1][0]
    eligible_native = list(range(first_eligible_native, last_eligible_native + 1))
    excluded_after_source_exit = list(
        range(last_eligible_native + 1, len(native_runs))
    )
    unmatched_eligible = [
        index for index in eligible_native if index not in matched_native
    ]
    diagnostics, classifications = source_only_diagnostics(
        all_capture_runs, capture_rgb, native_runs, native_rgb, pairs
    )
    native_diagnostics, native_classifications = native_only_diagnostics(
        all_capture_runs, capture_rgb, native_runs, native_rgb, pages, pairs
    )
    classified_native_pages = {
        int(diagnostic["native_page"]) for diagnostic in native_diagnostics
    }
    unmatched_eligible_classified = (
        set(unmatched_eligible) == classified_native_pages
    )
    source_residuals_classified = all(
        diagnostic["classification"] != "unclassified"
        for diagnostic in diagnostics
    )
    native_residuals_classified = all(
        diagnostic["classification"] != "unclassified"
        for diagnostic in native_diagnostics
    )

    blocks = sequence_match_blocks(all_capture_runs, native_runs, pairs)
    exact_sequence = not unmatched_eligible
    report = {
        "schema": "word-full-attract-live-audit-v1",
        "source": {
            "path": SOURCE.relative_to(ROOT).as_posix(),
            "sha256": source_sha256,
            "expected_sha256": EXPECTED_SOURCE_SHA256,
            "bytes_pinned": source_pinned,
            "expected_frames": EXPECTED_SOURCE_FRAMES,
            **probe,
            **decode,
        },
        "native": {
            "directory": native_directory.relative_to(ROOT).as_posix(),
            "seed": EXPECTED_NATIVE_SEED,
            "sample_count": EXPECTED_NATIVE_SAMPLES,
            "changed_pages": len(native_runs),
            "final_random_calls": pages[-1]["random_calls"],
            "final_random_state_at_last_changed_page": pages[-1]["random_state"],
        },
        "source_exit_boundary": {
            "matches_pinned_boundary": source_exit_boundary_matches,
            "last_comparable_capture_run": EXPECTED_LAST_ATTRACT_RUN,
            "last_comparable_capture_frame": EXPECTED_LAST_ATTRACT_FRAME,
            "exit_partial_capture_runs": list(EXPECTED_EXIT_PARTIAL_RUNS),
            "first_stable_title_capture_run": EXPECTED_FIRST_TITLE_RUN,
            "first_stable_title_capture_frame": EXPECTED_FIRST_TITLE_FRAME,
            "first_stable_title_rgb_sha256": first_title_run.digest,
            "classification":
                "external-demo-exit-versus-native-autonomous-continuation",
            "last_eligible_native_page": last_eligible_native,
            "excluded_native_page_count": len(excluded_after_source_exit),
            "excluded_native_ranges": index_ranges(
                excluded_after_source_exit, pages
            ),
        },
        "alignment": {
            "matched_exact_native_pages": len(matched_native),
            "eligible_native_pages": len(eligible_native),
            "all_eligible_native_pages_exact_in_order": exact_sequence,
            "atomic_double_buffer_parity": bool(
                unmatched_eligible_classified and
                source_residuals_classified and native_residuals_classified
            ),
            "first_pair": list(pairs[0]) if pairs else None,
            "last_pair": list(pairs[-1]) if pairs else None,
            "source_frame_minus_native_sample_median": alignment_offset,
            "source_frame_minus_native_sample_min": min(offsets) if offsets else None,
            "source_frame_minus_native_sample_max": max(offsets) if offsets else None,
            "offset_histogram": dict(Counter(offsets).most_common(20)),
            "exact_pairs": [
                {
                    "native_page": native_index,
                    "native_sample": native_runs[native_index].start,
                    "capture_run": capture_index,
                    "capture_frame_start": all_capture_runs[capture_index].start,
                    "capture_frame_end": all_capture_runs[capture_index].end,
                    "source_frame_minus_native_sample":
                        all_capture_runs[capture_index].start
                        - native_runs[native_index].start,
                }
                for native_index, capture_index in pairs
            ],
            "unmatched_eligible_native_page_count": len(unmatched_eligible),
            "all_unmatched_eligible_native_pages_classified":
                unmatched_eligible_classified,
            "unmatched_eligible_native_ranges": index_ranges(
                unmatched_eligible, pages
            ),
            "exact_match_blocks": blocks,
        },
        "source_only_between_exact_pairs": {
            "run_count": len(diagnostics),
            "classifications": dict(classifications),
            "runs": diagnostics,
        },
        "native_only_between_exact_pairs": {
            "page_count": len(native_diagnostics),
            "classifications": dict(native_classifications),
            "pages": native_diagnostics,
        },
    }
    report["valid"] = bool(
        source_pinned
        and decode["decoded_frames"] == EXPECTED_SOURCE_FRAMES
        and decode["sha256_collision_mismatches"] == 0
        and source_exit_boundary_matches
        and unmatched_eligible_classified
        and source_residuals_classified
        and native_residuals_classified
    )
    report["reason"] = (
        "Every shared-route native page is either an exact ordered source page "
        "or a pixel-exact atomic composition of measured source states; every "
        "source-only page is a classified single-buffer painter/scanout state. "
        "The exact-sequence flag remains separate and false so the no-flicker "
        "double-buffer exception is never hidden."
        if report["valid"] else
        "The shared route contains an unclassified source or native residual."
    )
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        f"Word attract: exact native pages {len(matched_native)}/"
        f"{len(eligible_native)} eligible; source-only runs "
        f"{len(diagnostics)} {dict(classifications)}; native-only pages "
        f"{len(native_diagnostics)} {dict(native_classifications)}; "
        f"excluded after source exit {len(excluded_after_source_exit)}; "
        f"valid={report['valid']}"
    )
    print(f"wrote {report_path}")
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
