# Super Munchers live presentation audit

Date: 2026-08-28

## Capture boundary

The original was run only for controlled evidence collection. Captures used a
unique DOSBox configuration with sound and mouse integration disabled, stayed
minimized except for the capture surface, and were closed in each script's
`finally` path. No DOSBox process remains. The native release does not launch
DOSBox or read these recordings.

## Exact live oracles

The renderer gates each listed source page over all 64,000 pixels at 320x200.
Hashes below are the test suite's FNV-1a 64-bit RGB framebuffer fingerprints.

| Page | Mode / source evidence | Expected hash |
|---|---|---:|
| Version | VGA startup capture | `252bf149cd778836` |
| Splash | VGA startup capture | `4629938e5e5bcade` |
| Title | VGA startup capture | `6c2833b00363de98` |
| Version | CGA startup capture | `244a67ce82ba6562` |
| Splash | CGA startup capture | `12869f99f37d987e` |
| Title | CGA startup capture | `00cb0fdfaeb841a5` |
| Options | VGA controlled menu capture | `5ec15fd100e375f0` |
| Instructions question | VGA controlled menu capture | `070b08aa80400c33` |
| Difficulty | VGA controlled menu capture | `59cf4295ac513588` |
| Game selection | VGA controlled menu capture | `5a3a2d415acd556a` |
| First ordinary board | VGA `super-vga-first-board-frames/frame-014.png` | `7e5cf95c68a2efa9` |
| First ordinary board | CGA `super-cga-live/first-board-frames/frame-012.png` | `4e35eefdc3d4372d` |
| Chew record 12 | VGA `super-vga-chew-frames-320/frame-0064.png` | `482dd1311847b8d1` |
| Chew record 13 | VGA `super-vga-chew-frames-320/frame-0065.png` | `f74ec4e5374f33b1` |
| Reggie overlap | VGA `super-vga-troggle-frames/frame-048.png` | `4eb8736860f8f311` |

The chew controller separately gates the eight-tick
`12,13,14,13,12,13,14,13` sequence and clears the eaten label before its first
pose. The Troggle oracle gates its occupied-cell text occlusion, actor inset,
raw palette and surrounding board pixels—not an isolated sprite crop.

## Corrections established by the captures

The comparisons fixed the exact startup hold flow, version text, menu rows and
pointer rectangles; full-width board geometry and DOS text centering; the
mode-specific player/life/Troggle insets; cell blanking during chewing; and the
resident VGA DAC rule for all 199 raw indexes exercised by Super artwork.
Partial refreshes are not synthesized: native presents one completed private
framebuffer per host paint, preserving the shared no-black-flicker contract.

Together with the exhaustive 336-record VGA and 336-record CGA BTMP gate, these
oracles close the presentation row in `SUPER-PARITY.md`.
