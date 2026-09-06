#!/usr/bin/env python3
"""Audit selected live DOS CGA UI/gameplay pages against native framebuffers."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from PIL import Image

from audit_cga_startup_capture import (
    EXPECTED_PALETTE,
    EXPECTED_RATE,
    HEIGHT,
    ROOT,
    WIDTH,
    decode,
    frame_runs,
    probe,
    rgb_palette,
    sha256_file,
)


CAPTURES = (
    {
        "game": "Number",
        "page": "Options",
        "capture": ROOT / "analysis/number-live/cga/original-number-options.avi",
        "capture_sha256":
            "D6312DBECA7A91CD7F2C8CE4F600BA13D836B765503CB091160AE666F0B32A0F",
        "frames": 380,
        "native": ROOT / "analysis/cga-live/native/number-options-native.ppm",
        "mode_switch_capture":
            ROOT / "analysis/number-live/cga/original-number-options-mode-switch.avi",
        "mode_switch_sha256":
            "57E2CD818032475731B27AEA2770C6261F9D3229A382CCD27E93070D1778349A",
    },
    {
        "game": "Number",
        "page": "Hall of Fame: empty Factors",
        "capture": ROOT / "analysis/number-live/cga/original-number-hall.avi",
        "capture_sha256":
            "2E21C11303BF2DD021C9A8B3E361F087DC8B470B4428B3139BF500AE44EDB595",
        "frames": 450,
        "native":
            ROOT / "analysis/cga-live/native/number-hall-empty-factors-native.ppm",
        "mode_switch_capture":
            ROOT / "analysis/number-live/cga/original-number-hall-mode-switch.avi",
        "mode_switch_sha256":
            "D305B30EA51BE0D8657E756EB0536E9F9E34199370C99A3561F3D10C051FD88A",
    },
    {
        "game": "Number",
        "page": "level-1 Factors gameplay board",
        "capture":
            ROOT / "analysis/number-live/cga/original-number-gameplay-first-board.avi",
        "capture_sha256":
            "66B7996C34F92EAEE0D0B5A8EB96DC6196E74C7B704CF3F4B7048D2FB5B7641E",
        "frames": 373,
        "native":
            ROOT / "analysis/cga-live/native/number-gameplay-first-board-native.ppm",
        "mode_switch_capture":
            ROOT / "analysis/number-live/cga/original-number-gameplay-first-board-mode-switch.avi",
        "mode_switch_sha256":
            "6FFF2665230387F483736A38024BD9E98554DF7D0955F87D5668E86287E9982F",
        "mode_switch_frames": 6,
    },
    {
        "game": "Word",
        "page": "Options",
        "capture": ROOT / "analysis/word-live/cga/original-word-options.avi",
        "capture_sha256":
            "3E30190D56D4445D19AF36FB33AD7FECD88100CF624321DB8D6DBD8D5092C1A7",
        "frames": 396,
        "native": ROOT / "analysis/cga-live/native/word-options-native.ppm",
    },
    {
        "game": "Word",
        "page": "Hall of Fame",
        "capture": ROOT / "analysis/word-live/cga/original-word-hall.avi",
        "capture_sha256":
            "90A809FCFBBDE9410333268F0EC3E400DD2D917FA51EA3F86785E642EA890295",
        "frames": 383,
        "native": ROOT / "analysis/cga-live/native/word-hall-native.ppm",
    },
    {
        "game": "Word",
        "page": "level-1 /e/ as in tree gameplay board",
        "capture":
            ROOT / "analysis/word-live/cga/original-word-gameplay-first-board.avi",
        "capture_sha256":
            "72D5A4C26974A438E8CD641A645D8A353922780B986CA7EB9EAD622F861A78AB",
        "frames": 378,
        "native":
            ROOT / "analysis/cga-live/native/word-gameplay-first-board-native.ppm",
        "mode_switch_capture":
            ROOT / "analysis/word-live/cga/original-word-gameplay-first-board-mode-switch.avi",
        "mode_switch_sha256":
            "67F9D9A1B17D02E619379C035A126B976EEA9F0BBA56EAFAB5C232EDA55BC178",
        "mode_switch_frames": 1,
    },
)

MODE_SWITCH_RATE = "1498069/25000"


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def audit_mode_switch(spec: dict[str, object]) -> dict[str, object] | None:
    path_value = spec.get("mode_switch_capture")
    if path_value is None:
        return None
    path = Path(path_value)
    if not path.is_file():
        raise FileNotFoundError(path)
    digest = sha256_file(path)
    video = probe(path)
    expected_digest = str(spec["mode_switch_sha256"])
    valid = bool(
        digest == expected_digest
        and video["codec"] == "zmbv"
        and video["width"] == WIDTH * 2
        and video["height"] == HEIGHT * 2
        and video["frame_rate_fraction"] == MODE_SWITCH_RATE
        and video["frames"] == int(spec.get("mode_switch_frames", 5))
    )
    return {
        "capture": relative(path),
        "capture_sha256": digest,
        "expected_capture_sha256": expected_digest,
        "capture_bytes_pinned": digest == expected_digest,
        "video": video,
        "valid": valid,
    }


def audit_capture(spec: dict[str, object]) -> dict[str, object]:
    capture = Path(spec["capture"])
    native_path = Path(spec["native"])
    if not capture.is_file() or not native_path.is_file():
        raise FileNotFoundError(capture if not capture.is_file() else native_path)

    capture_digest = sha256_file(capture)
    expected_capture_digest = str(spec["capture_sha256"])
    video = probe(capture)
    frames = decode(capture)
    frame_digests = [hashlib.sha256(frame).hexdigest() for frame in frames]

    with Image.open(native_path) as source:
        native_image = source.convert("RGB")
        if native_image.size != (WIDTH, HEIGHT):
            raise RuntimeError(f"{native_path}: expected 320x200")
        native_rgb = native_image.tobytes()

    native_digest = hashlib.sha256(native_rgb).hexdigest()
    occurrences = [
        index for index, digest in enumerate(frame_digests)
        if digest == native_digest
    ]
    palette = rgb_palette(native_rgb)
    expected_frames = int(spec["frames"])
    capture_pinned = capture_digest == expected_capture_digest
    video_matches = bool(
        video["codec"] == "zmbv"
        and video["width"] == WIDTH
        and video["height"] == HEIGHT
        and video["frame_rate_fraction"] == EXPECTED_RATE
        and video["frames"] == expected_frames
        and len(frames) == expected_frames
    )
    exact = bool(occurrences)
    palette_exact = palette <= EXPECTED_PALETTE
    mode_switch = audit_mode_switch(spec)
    mode_switch_valid = mode_switch is None or bool(mode_switch["valid"])
    valid = bool(
        capture_pinned and video_matches and exact and palette_exact
        and mode_switch_valid
    )
    return {
        "game": spec["game"],
        "page": spec["page"],
        "capture": relative(capture),
        "capture_sha256": capture_digest,
        "expected_capture_sha256": expected_capture_digest,
        "capture_bytes_pinned": capture_pinned,
        "video": video,
        "native": relative(native_path),
        "native_rgb_sha256": native_digest.upper(),
        "exact_source_frame_count": len(occurrences),
        "exact_source_frame_runs": frame_runs(occurrences),
        "palette": ["#%02X%02X%02X" % color for color in sorted(palette)],
        "uses_exact_cga_palette": palette_exact,
        "mode_switch": mode_switch,
        "valid": valid,
    }


def main() -> int:
    captures = [audit_capture(spec) for spec in CAPTURES]
    report = {
        "schema": "munchers-cga-ui-live-audit-v2",
        "logical_size": [WIDTH, HEIGHT],
        "expected_palette": [
            "#%02X%02X%02X" % color for color in sorted(EXPECTED_PALETTE)
        ],
        "captures": captures,
        "all_captured_pages_exact": all(
            int(capture["exact_source_frame_count"]) > 0
            for capture in captures
        ),
        "valid": all(bool(capture["valid"]) for capture in captures),
    }
    output = ROOT / "analysis/cga-live/ui-report.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for capture in captures:
        print(
            f"{capture['game']} CGA {capture['page']}: "
            f"exact_frames={capture['exact_source_frame_count']} "
            f"runs={capture['exact_source_frame_runs']}; "
            f"valid={capture['valid']}"
        )
    print(f"wrote {relative(output)}")
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
