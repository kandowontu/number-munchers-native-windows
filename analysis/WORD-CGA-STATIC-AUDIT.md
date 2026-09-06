# Word Munchers CGA device and presentation static audit

This audit uses the preserved `WM-unpacked-image.bin`, Word graphics archives,
and the in-memory native renderer only. It does not launch DOSBox, `WM.EXE`,
the native GUI, or any visible window.

## Device and resource split

Word stores its graphics-family selector at `DS:6386`: zero is CGA and one is
VGA. The startup branches at image `0x0D8AD` and `0x0D8D0` install those exact
values. Presentation call sites choose the parallel even CGA and odd VGA
resources already enumerated in `WORD-ASSET-BRIDGE.md`, including title PCXF
`6008/6009`, actor tables `1000..1005/1006..1011`, and the even/odd scene
families.

The Word CGA archive's two-bit PCX indexes are displayed through BGI mode 1's
high-intensity palette 1. Light cyan, light magenta, and white are the fixed
foreground colors; the independently programmable background is black on the
startup/UI pages and live-measured dark blue on gameplay. The PCX header's
placeholder color triples are not display colors.

## Exact primitive-color table

The 32 bytes at `DS:1188` are sixteen `(CGA,VGA)` device-index pairs. They are
byte-for-byte the same mapping recovered independently from Number Munchers:

```text
logical:  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
CGA:      0  1  1  1  2  2  2  3  0  1  1  1  2  2  3  3
VGA:      0  B  D  F  4  5  6  7  8  9  A  1  C  2  E  3
```

Therefore CGA maps the blue/green/cyan family to light cyan, the
red/magenta/brown family to light magenta, light colors to white, and dark
gray to black. Word's smaller direct lookup pairs at `DS:11A0..11A6` are the
last four rows of this same table: `(2,12)`, `(2,2)`, `(3,14)`, and `(3,3)`.
The common native renderer already implements this exact collapse, so no
Word-specific approximation or second color policy is required.

## Native regression gates

The asset test independently checks all 20 Word CGA PCXF sheets, all 225 CGA
BTMP records, and exact four-index artwork confinement. The application test
now composes seven complete deterministic CGA pages. Each page is restricted
to the three foreground colors plus its selected background; their union must
contain black and dark blue as well as all three foreground colors.

| Page | Native FNV-64 |
|---|---:|
| version page | `8ed7651c7827ba8c` |
| startup splash | `3043b05c7674e388` |
| title | `20f7bd4383d7775c` |
| Options | `caf1504d701741dc` |
| supplied Hall | `80795be2acc95ec2` |
| level-1 board, seed `0x1234` | `512ba7bf24df5501` |
| captured level-1 tree board | `4748e03008e2ab7a` |

These hashes began as native static-regression values. The later muted live
`machine=cga` recording now independently matches the version, splash, and
title framebuffers at every pixel for 50, 85, and 231 source frames. The pinned
source hashes, native PPMs, exact four-color palette, and ordered occurrences
are recorded in `analysis/cga-live/startup-report.json`. The follow-up pinned
UI audit in `analysis/cga-live/ui-report.json` independently matches Word's
Options, supplied Hall, and level-1 `/e/ as in tree` gameplay framebuffers for
142, 129, and 148 source frames. The gameplay comparison independently proves
the dark-blue background register and exact foreground palette. Six additional
native-resolution recordings match all 583 DOS-presented cartoon state runs,
all 561 distinct per-scene RGB states, and every completed close/load/open Wipe
at exact pixels. Those captures exposed and corrected the packed-byte rectangle
width/alignment rule and scene 4's wholly offscreen x=324 record. Exhaustive
resource, primitive, layout, behavior, and composition gates cover the broader
parameter space at the 320x200 framebuffer boundary; desktop scaling and
monitor response are host properties. See
`analysis/cga-live/cartoon-transition-report.json`, the six
`analysis/cga-live/word-scene-*-report.json` reports, and
`analysis/PLATFORM-BOUNDARY-AUDIT.md`.
