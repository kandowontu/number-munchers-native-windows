#!/usr/bin/env python3
"""Audit the complete left-edge Reggie entry in the supplied nm_000 AVI.

The lossless ZMBV recording isolates safe-zone expiry, the Troggle warning,
warning removal, the clipped dirty-box-only entry phase, three progressively
visible poses, and the horizontal endpoint/dwell alias.  This tool pins the
source and exact stable 320x200 framebuffer states used by the native test.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess

from audit_word_scene_capture import decode_capture, probe_capture


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "captures" / "nm_000.avi"
REPORT = ROOT / "analysis" / "number-live" / "gameplay" / "nm000-entry-report.json"

EXPECTED_CAPTURE_SHA256 = (
    "F0F646C42DA0F8B0DF8F7A640F92DF1AABFCF35F80949F74FDBD67AC18F205BC"
)
EXPECTED_FRAME_COUNT = 5557
EXPECTED_NONUNIFORM_BLOCKS = 403
EXPECTED_RUN_COUNT = 96
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1

# Run indexes refer to the complete capture's logical-frame RLE.  Every state
# below is a uniform, completed 320x200 framebuffer.  Runs 15 and 18 are the
# two incomplete DOS dirty refreshes bracketing the warning.
EXPECTED_STATES = {
    "two_safe_zones": (13, 617, 940, 0x890C293F7DF8A877),
    "last_safe_zone": (14, 941, 976, 0x29D0A1A8BE8DA53F),
    "warning_with_safe_zone": (16, 978, 1178, 0xDDD30B30E2D2144D),
    "warning_after_safe_zone_expiry": (17, 1179, 1190, 0x62E80F6DDA05399D),
    "warning_removed": (19, 1192, 1200, 0x1766E260202CB0B7),
    "entry_phase_2_clipped_clear_only": (20, 1201, 1207, 0x1A90CF1D67B26CDB),
    "entry_phase_3": (21, 1208, 1215, 0x869E0DD009334DA3),
    "entry_phase_4": (22, 1216, 1222, 0x2FBD082DF523F344),
    "entry_phase_5": (23, 1223, 1229, 0x1A543C81A846F4D5),
    "entry_phase_6_and_dwell": (24, 1230, 1390, 0xE42C2A440D9C2FDB),
}

EXPECTED_TORN_REFRESHES = {
    "warning_start": (15, 977, 977, 6),
    "warning_removal": (18, 1191, 1191, 6),
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
            "player_start_row_column": [2, 3],
            "safe_cells_before_warning": [13, 21],
            "entry": {
                "species": "Reggie",
                "direction": "right",
                "row": 2,
                "from_column": -1,
                "to_column": 0,
            },
            "labels_row_major": [
                "39", "577", "312", "222", "483", "117",
                "416", "380", "65", "219", "172", "132",
                "547", "270", "78", "", "468", "286",
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
