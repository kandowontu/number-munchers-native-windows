#!/usr/bin/env python3
"""Audit the complete right-edge Reggie entry in the supplied nm_001 AVI.

This lossless continuation isolates two safe-zone expiries, a later activation,
the Troggle warning, all five horizontal arrival phases from the right, and the
terminal repaint that restores the overwritten x=308 grid rule.  The script
pins the source and the exact 320x200 states used by the native regression.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess

from audit_word_scene_capture import decode_capture, probe_capture


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "captures" / "nm_001.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "nm001-right-entry-report.json"
)

EXPECTED_CAPTURE_SHA256 = (
    "B9F3F912CFC24D75C4DF687A89D56A7A690C58E469253DE59439921E071507DA"
)
EXPECTED_FRAME_COUNT = 3967
EXPECTED_NONUNIFORM_BLOCKS = 106
EXPECTED_RUN_COUNT = 91
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1

EXPECTED_STATES = {
    "two_safe_zones": (33, 779, 1084, 0xD8E0D1D50A878FEB),
    "last_safe_zone": (34, 1085, 1149, 0xCBB0782510AA73F3),
    "no_safe_zone": (35, 1150, 1579, 0xE4DB4B6021748043),
    "warning": (37, 1581, 1712, 0xC13D985D68AAD47D),
    "warning_with_new_safe_zone": (38, 1713, 1793, 0xEC7121E89EF91215),
    "warning_removed": (40, 1795, 1803, 0x347C8FF188B271CB),
    "entry_phase_2": (41, 1804, 1810, 0xBA7050B661BB0C15),
    "entry_phase_3": (42, 1811, 1817, 0xA44C939BDB119674),
    "entry_phase_4": (44, 1819, 1825, 0x78D0A537912E076C),
    "entry_phase_5": (45, 1826, 1832, 0x9E9BC76341973959),
    "entry_phase_6": (46, 1833, 1839, 0xC466AF82ADE08F12),
    "entry_dwell_grid_restored": (47, 1840, 1971, 0x4D86E27DD8F949F4),
}

EXPECTED_TORN_REFRESHES = {
    "warning_start": (36, 1580, 1580, 1),
    "warning_removal": (39, 1794, 1794, 4),
    "entry_phase_4": (43, 1818, 1818, 18),
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def renderer_fnv64(rgb: bytes) -> int:
    value = FNV_OFFSET
    for offset in range(0, len(rgb), 3):
        value ^= (rgb[offset] << 16) | (rgb[offset + 1] << 8) | rgb[offset + 2]
        value = (value * FNV_PRIME) & MASK64
    return value


def decoded_audio_track(path: Path) -> dict[str, int | bool]:
    command = [
        "ffmpeg", "-v", "error", "-i", str(path), "-map", "0:a:0",
        "-f", "s16le", "-acodec", "pcm_s16le", "pipe:1",
    ]
    result = subprocess.run(command, check=True, capture_output=True)
    return {
        "decoded_bytes": len(result.stdout),
        "nonzero_bytes": sum(value != 0 for value in result.stdout),
        "silent": not any(result.stdout),
    }


def main() -> None:
    capture_sha256 = sha256_file(CAPTURE)
    probe = probe_capture(CAPTURE)
    runs, decode_stats, _, _, capture_rgb = decode_capture(CAPTURE, {})

    checks: dict[str, dict[str, object]] = {}
    selected_states_match = True
    for name, (run_index, first, last, expected_hash) in EXPECTED_STATES.items():
        run = runs[run_index]
        actual_hash = renderer_fnv64(capture_rgb[run.digest])
        matched = (
            run.start == first
            and run.end == last
            and run.nonuniform_blocks == 0
            and run.has_uniform_frame
            and actual_hash == expected_hash
        )
        selected_states_match &= matched
        checks[name] = {
            "run_index": run_index,
            "first_frame": run.start,
            "last_frame": run.end,
            "frames": run.count,
            "nonuniform_2x_blocks": run.nonuniform_blocks,
            "renderer_fnv64": f"0x{actual_hash:016x}",
            "matched": matched,
        }

    torn_checks: dict[str, dict[str, object]] = {}
    torn_refreshes_match = True
    for name, (run_index, first, last, expected_nonuniform) in (
        EXPECTED_TORN_REFRESHES.items()
    ):
        run = runs[run_index]
        actual_hash = renderer_fnv64(capture_rgb[run.digest])
        matched = (
            run.start == first
            and run.end == last
            and run.nonuniform_blocks == expected_nonuniform
            and not run.has_uniform_frame
        )
        torn_refreshes_match &= matched
        torn_checks[name] = {
            "run_index": run_index,
            "first_frame": run.start,
            "last_frame": run.end,
            "nonuniform_2x_blocks": run.nonuniform_blocks,
            "renderer_fnv64": f"0x{actual_hash:016x}",
            "matched": matched,
        }

    source_matches = capture_sha256 == EXPECTED_CAPTURE_SHA256
    structure_matches = (
        decode_stats["decoded_frames"] == EXPECTED_FRAME_COUNT
        and decode_stats["nonuniform_2x_blocks"] == EXPECTED_NONUNIFORM_BLOCKS
        and len(runs) == EXPECTED_RUN_COUNT
        and decode_stats["sha256_collision_mismatches"] == 0
    )
    report = {
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": capture_sha256,
        "source_matches": source_matches,
        "probe": probe,
        "decode": {**decode_stats, "logical_run_count": len(runs)},
        "audio": decoded_audio_track(CAPTURE),
        "board": {
            "level": 1,
            "target": 13,
            "mode": "Multiples",
            "lives_including_active": 3,
            "player_row_column": [2, 3],
            "expiring_safe_cells": [21, 14],
            "warning_safe_cell": 8,
            "entry": {
                "species": "Reggie",
                "direction": "left",
                "row": 2,
                "from_column": 6,
                "to_column": 5,
            },
            "labels_row_major": [
                "39", "577", "312", "222", "483", "117",
                "416", "380", "65", "219", "172", "132",
                "130", "442", "608", "", "84", "59",
                "331", "82", "364", "507", "346", "208",
                "493", "465", "432", "248", "192", "183",
            ],
        },
        "stable_states": checks,
        "excluded_torn_refreshes": torn_checks,
        "structure_matches": structure_matches,
        "selected_states_match": selected_states_match,
        "torn_refreshes_match": torn_refreshes_match,
    }
    report["valid"] = (
        source_matches
        and structure_matches
        and selected_states_match
        and torn_refreshes_match
    )
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
