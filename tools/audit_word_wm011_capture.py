#!/usr/bin/env python3
"""Audit the complete user-board/Troggle slice in the supplied wm_011 AVI.

The lossless ZMBV stream contains a second organic Word board, pause and quit
overlays, a downward Muncher move, safe-zone changes, the Troggle warning, and
a complete top-edge Reggie entry.  This tool pins the source, reconstructs the
logical 320x200 framebuffer from the uniform 2x blocks, and locks the stable
states used by the native headless regression test.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess

from audit_word_scene_capture import decode_capture, probe_capture


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "captures" / "wm_011.avi"
REPORT = ROOT / "analysis" / "word-live" / "gameplay" / "wm011-report.json"

EXPECTED_CAPTURE_SHA256 = (
    "EF691B25CCE75093A81FFCB521C87EFFA05519B1CF4CCBF3565E9597A343AFD0"
)
EXPECTED_FRAME_COUNT = 2420
EXPECTED_NONUNIFORM_BLOCKS = 1140
EXPECTED_RUN_COUNT = 62
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1

# Run indexes refer to the complete capture's logical-frame RLE.  Torn DOS
# dirty refreshes remain in the evidence stream, but every state below is a
# uniform, completed framebuffer and is required at its exact interval/hash.
EXPECTED_STATES = {
    "initial_safe_board": (24, 991, 1042, 0xC4364D11E662B99F),
    "time_out": (26, 1044, 1104, 0x060AD563DE6F0AB9),
    "quit_no": (30, 1168, 1203, 0x1B87B07181B63185),
    "down_phase_1": (40, 1391, 1392, 0xD9D7272C2A7B27F3),
    "down_phase_2": (41, 1393, 1394, 0x4D368EE30ABADDB8),
    "down_phase_3": (42, 1395, 1397, 0x39ED8ADAD1B97658),
    "down_phase_4": (43, 1398, 1399, 0x7E6F2626C9AABE9F),
    "down_terminal": (44, 1400, 1402, 0x557B5A0EFB81F2BA),
    "down_standing_safe": (45, 1403, 1407, 0xB19C10DFEEA1A0C2),
    "safe_off": (52, 1891, 1969, 0x2538522157185E2A),
    "troggle_warning": (53, 1970, 2183, 0x32CE10EA97A25480),
    "warning_removed": (54, 2184, 2193, 0x2538522157185E2A),
    "reggie_entry_phase_2": (55, 2194, 2200, 0xE276384D24494E1D),
    "reggie_entry_phase_3": (57, 2202, 2207, 0xFCE51C140F43D0D5),
    "reggie_entry_phase_3_safe_on": (58, 2208, 2208, 0x195538AD19D455E5),
    "reggie_entry_phase_4": (59, 2209, 2215, 0x35DE7193323D8017),
    "reggie_entry_phase_5": (60, 2216, 2222, 0xE1966208D455BE5D),
    "reggie_entry_dwell": (61, 2223, 2419, 0x9811B24A078483F9),
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

    # Run 56 is the one incomplete dirty refresh between the first two entry
    # poses.  It is preserved but must never be promoted to a native stable
    # frame in the double-buffered port.
    torn_entry = runs[56]
    torn_entry_check = {
        "run_index": 56,
        "first_frame": torn_entry.start,
        "last_frame": torn_entry.end,
        "nonuniform_2x_blocks": torn_entry.nonuniform_blocks,
        "renderer_fnv64":
            f"0x{renderer_fnv64(capture_rgb[torn_entry.digest]):016x}",
        "matched": (
            torn_entry.start == 2201
            and torn_entry.end == 2201
            and torn_entry.nonuniform_blocks == 17
        ),
    }
    audio = decoded_audio_track(CAPTURE)
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
        "audio": audio,
        "board": {
            "level": 1,
            "target_sound": 2,
            "target_label": "/e/ as in tree",
            "player_start_row_column": [1, 2],
            "initial_safe_cell": 8,
            "later_safe_cell": 20,
            "words_row_major": [
                "yet", "them", "we", "them", "he", "get",
                "yes", "me", "", "get", "be", "yes",
                "pep", "pep", "help", "she", "he", "get",
                "sleep", "help", "he", "red", "see", "she",
                "me", "she", "she", "red", "me", "them",
            ],
        },
        "stable_states": checks,
        "excluded_torn_entry_refresh": torn_entry_check,
        "structure_matches": structure_matches,
        "selected_states_match": selected_states_match,
    }
    report["valid"] = (
        source_matches
        and structure_matches
        and selected_states_match
        and torn_entry_check["matched"]
    )
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
