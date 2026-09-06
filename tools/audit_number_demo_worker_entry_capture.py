#!/usr/bin/env python3
"""Audit the right-edge Worker arrival on Number's third Demo board.

The focused lossless interval contains all five horizontal entry phases and
the dwell while the Muncher finishes a concurrent chew.  Two complete
callback-intermediate framebuffer states expose the DOS painter between the
player and Troggle jobs; two separate one-block refreshes are classified as
torn host presentation.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np

from audit_number_demo_bottom_entry_capture import (
    CAPTURE,
    CAPTURE_FRAME_BYTES,
    CAPTURE_HEIGHT,
    CAPTURE_WIDTH,
    EXPECTED_AUDIO_BYTES,
    EXPECTED_AUDIO_NONZERO_BYTES,
    EXPECTED_CAPTURE_FRAMES,
    EXPECTED_CAPTURE_SHA256,
    LOGICAL_HEIGHT,
    LOGICAL_WIDTH,
    ROOT,
    decode_audio_track,
    probe_capture,
    renderer_fnv64,
    sha256_file,
)


REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-worker-entry-report.json"
)
WINDOW_FIRST_FRAME = 11_840
WINDOW_LAST_FRAME = 11_876
EXPECTED_WINDOW_FRAMES = 37
EXPECTED_WINDOW_NONUNIFORM_BLOCKS = 2
EXPECTED_WINDOW_RUNS = 10


@dataclass
class Run:
    digest: str
    start: int
    end: int
    nonuniform_blocks: int
    has_uniform_frame: bool
    rgb: bytes


# The paired phase-2/phase-3 states differ only in the concurrent player chew
# pose. They are complete, uniform DOS framebuffers and are not torn frames.
EXPECTED_STATES = {
    "entry_phase_2_player_pose_a": (
        1, 11841, 11842, 0xF6FE900E88A30737, (302, 156, 309, 173, 18)
    ),
    "entry_phase_2_player_pose_b": (
        2, 11843, 11844, 0xC0942CB89DE34ACB, (302, 156, 309, 173, 18)
    ),
    "entry_phase_3_player_pose_b": (
        3, 11845, 11845, 0xA5D7F189AEFDDFB2, (294, 151, 307, 173, 26)
    ),
    "entry_phase_3_player_pose_a": (
        4, 11846, 11849, 0xB8D21853E1CA581E, (294, 151, 307, 173, 26)
    ),
    "entry_phase_4": (
        5, 11850, 11854, 0xF0D15D79889D1260, (286, 151, 299, 173, 26)
    ),
    "entry_phase_5": (
        6, 11855, 11859, 0xD3CCE8EF612D3B0F, (277, 151, 290, 171, 21)
    ),
    "entry_phase_6": (
        7, 11860, 11863, 0x5819250CE11B6E9C, (270, 151, 283, 173, 26)
    ),
    "entry_dwell": (
        9, 11865, 11876, 0xB9828F8AF6989002, (270, 151, 283, 173, 26)
    ),
}

EXPECTED_TORN = {
    "phase_2_leading_refresh": (0, 11840, 11840, 1, 0xA40A5B21CA6FFC75),
    "terminal_refresh": (8, 11864, 11864, 1, 0x1D851B7BB01C64A7),
}


def decode_window(path: Path) -> tuple[list[Run], dict[str, int]]:
    selection = (
        f"select=between(n\\,{WINDOW_FIRST_FRAME}\\,{WINDOW_LAST_FRAME})"
    )
    result = subprocess.run(
        [
            "ffmpeg", "-v", "error", "-i", str(path), "-map", "0:v:0",
            "-vf", selection, "-fps_mode", "passthrough", "-f", "rawvideo",
            "-pix_fmt", "rgb24", "pipe:1",
        ],
        check=True,
        capture_output=True,
    )
    if len(result.stdout) % CAPTURE_FRAME_BYTES:
        raise ValueError("focused Worker decode ended with a partial frame")

    runs: list[Run] = []
    total_nonuniform = 0
    frame_count = len(result.stdout) // CAPTURE_FRAME_BYTES
    for offset in range(0, len(result.stdout), CAPTURE_FRAME_BYTES):
        global_frame = WINDOW_FIRST_FRAME + offset // CAPTURE_FRAME_BYTES
        captured = np.frombuffer(
            result.stdout[offset:offset + CAPTURE_FRAME_BYTES], dtype=np.uint8
        ).reshape(CAPTURE_HEIGHT, CAPTURE_WIDTH, 3)
        logical = captured[0::2, 0::2]
        mismatches = (
            np.any(logical != captured[0::2, 1::2], axis=2) |
            np.any(logical != captured[1::2, 0::2], axis=2) |
            np.any(logical != captured[1::2, 1::2], axis=2)
        )
        nonuniform = int(np.count_nonzero(mismatches))
        total_nonuniform += nonuniform
        rgb = logical.tobytes()
        digest = hashlib.sha256(rgb).hexdigest()
        if runs and runs[-1].digest == digest:
            runs[-1].end = global_frame
            runs[-1].nonuniform_blocks += nonuniform
            runs[-1].has_uniform_frame |= nonuniform == 0
        else:
            runs.append(Run(
                digest=digest,
                start=global_frame,
                end=global_frame,
                nonuniform_blocks=nonuniform,
                has_uniform_frame=nonuniform == 0,
                rgb=rgb,
            ))
    return runs, {
        "first_global_frame": WINDOW_FIRST_FRAME,
        "last_global_frame": WINDOW_LAST_FRAME,
        "decoded_frames": frame_count,
        "nonuniform_2x_blocks": total_nonuniform,
        "logical_run_count": len(runs),
    }


def worker_geometry(rgb: bytes) -> dict[str, object]:
    image = np.frombuffer(rgb, dtype=np.uint8).reshape(
        LOGICAL_HEIGHT, LOGICAL_WIDTH, 3
    )
    target = np.all(image == (0xCB, 0x5D, 0xFF), axis=2)
    target[:145, :] = False
    target[:, :260] = False
    local = np.all(image == (0xB6, 0x20, 0xFF), axis=2)
    local[:145, :] = False
    local[:, :260] = False
    rows, columns = np.where(target)
    bbox = None if not len(rows) else [
        int(columns.min()), int(rows.min()),
        int(columns.max()), int(rows.max()), int(len(rows)),
    ]
    return {
        "worker_resident_highlight_bbox_and_pixels": bbox,
        "pcxf_local_highlight_pixels": int(np.count_nonzero(local)),
    }


def main() -> None:
    source_sha256 = sha256_file(CAPTURE)
    probe = probe_capture(CAPTURE)
    audio = decode_audio_track(CAPTURE)
    runs, window = decode_window(CAPTURE)

    source_matches = source_sha256 == EXPECTED_CAPTURE_SHA256
    probe_matches = probe == {
        "codec": "zmbv",
        "pixel_format": "bgr0",
        "width": 640,
        "height": 400,
        "frame_rate_fraction": "2190197/31250",
        "frame_rate_hz": 70.086304,
        "frames": EXPECTED_CAPTURE_FRAMES,
        "duration_seconds": 313.955777,
        "audio_codec": "pcm_s16le",
        "audio_sample_rate": 48_000,
        "audio_channels": 2,
    }
    audio_matches = audio == {
        "decoded_bytes": EXPECTED_AUDIO_BYTES,
        "nonzero_bytes": EXPECTED_AUDIO_NONZERO_BYTES,
    }
    window_matches = window == {
        "first_global_frame": WINDOW_FIRST_FRAME,
        "last_global_frame": WINDOW_LAST_FRAME,
        "decoded_frames": EXPECTED_WINDOW_FRAMES,
        "nonuniform_2x_blocks": EXPECTED_WINDOW_NONUNIFORM_BLOCKS,
        "logical_run_count": EXPECTED_WINDOW_RUNS,
    }

    stable_states: dict[str, dict[str, object]] = {}
    stable_states_match = True
    geometry_matches = True
    for name, (index, first, last, expected_hash, expected_box) in (
        EXPECTED_STATES.items()
    ):
        run = runs[index]
        actual_hash = renderer_fnv64(run.rgb)
        actual_geometry = worker_geometry(run.rgb)
        geometry_matched = (
            actual_geometry["worker_resident_highlight_bbox_and_pixels"] ==
                list(expected_box)
            and actual_geometry["pcxf_local_highlight_pixels"] == 0
        )
        matched = (
            run.start == first and run.end == last and
            run.nonuniform_blocks == 0 and run.has_uniform_frame and
            actual_hash == expected_hash
        )
        stable_states_match &= matched
        geometry_matches &= geometry_matched
        stable_states[name] = {
            "run_index": index,
            "first_frame": run.start,
            "last_frame": run.end,
            "frames": run.end - run.start + 1,
            "nonuniform_2x_blocks": run.nonuniform_blocks,
            "renderer_fnv64": f"0x{actual_hash:016x}",
            "geometry": actual_geometry,
            "state_matched": matched,
            "geometry_matched": geometry_matched,
        }

    torn_states: dict[str, dict[str, object]] = {}
    torn_states_match = True
    for name, (index, first, last, expected_nonuniform, expected_hash) in (
        EXPECTED_TORN.items()
    ):
        run = runs[index]
        actual_hash = renderer_fnv64(run.rgb)
        matched = (
            run.start == first and run.end == last and
            run.nonuniform_blocks == expected_nonuniform and
            not run.has_uniform_frame and actual_hash == expected_hash
        )
        torn_states_match &= matched
        torn_states[name] = {
            "run_index": index,
            "first_frame": run.start,
            "last_frame": run.end,
            "nonuniform_2x_blocks": run.nonuniform_blocks,
            "renderer_fnv64": f"0x{actual_hash:016x}",
            "matched": matched,
        }

    report = {
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_matches,
        "probe": probe,
        "probe_matches": probe_matches,
        "audio": audio,
        "audio_matches": audio_matches,
        "focused_decode": window,
        "focused_decode_matches": window_matches,
        "board": {
            "demo_board": 3,
            "level": 8,
            "mode": "Equality",
            "target": 30,
            "entry": {
                "species": "Worker", "direction": "left", "row": 4,
                "column": 5, "from_row": 4, "from_column": 6,
            },
            "concurrent_player_action": "correct chew at row 1, column 5",
        },
        "stable_states": stable_states,
        "excluded_torn_refreshes": torn_states,
        "native_replay": {
            "complete": True,
            "exact_stable_state_count": 8,
            "exact_stable_states_presented": [
                "entry_phase_2_player_pose_a",
                "entry_phase_2_player_pose_b",
                "entry_phase_3_player_pose_b",
                "entry_phase_3_player_pose_a",
                "entry_phase_4",
                "entry_phase_5",
                "entry_phase_6",
                "entry_dwell",
            ],
            "open_callback_intermediate_states": [],
            "headless_gate": "game_render_state_test",
            "reason": (
                "entry begins at visual phase 1; the presenter queues the complete "
                "phase-3 framebuffer with retained record-14 chew pixels before "
                "the terminal record-13 player paint"
            ),
        },
        "stable_states_match": stable_states_match,
        "geometry_matches": geometry_matches,
        "torn_refreshes_match": torn_states_match,
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        stable_states_match,
        geometry_matches,
        torn_states_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
