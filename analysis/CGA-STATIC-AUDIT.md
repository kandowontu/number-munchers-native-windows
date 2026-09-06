# CGA device and presentation static audit

This audit uses only the preserved DOS load image, the two original graphics
archives, `LOGO.004`, and native headless tests. It does not launch DOSBox or
the native game window.

## Original device split

DOS startup stores the selected graphics family in `DS:61BE`. The initializer
at load-image `0x0CB65` has two explicit branches:

| `DS:61BE` | Driver setup | Mode | Startup logo |
|---:|---|---:|---|
| `0` | `CGA.BGI` | `1` | `logo.004` (`DS:0B78`) |
| `1` | `VGA256` | `8` | `logo.256` (`DS:0B81`) |

The CGA branch at `0x0CC03` writes driver and mode value 1 and retains the
`logo.004` pointer. The VGA branch at `0x0CC26` opens `VGA256`, installs mode
8, and substitutes `logo.256`.

The archive opener at image `0x0D3C0` always opens `nm.res`, then tests the
same flag. Nonzero opens `nmcga.res`; zero opens `ncga.res`. This proves that
the latter archive is the active CGA artwork bank rather than an unused asset
copy.

The main painters choose paired resource IDs through the same flag:

| Use | CGA | VGA | Static branch |
|---|---:|---:|---|
| Hall | 6002 | 6003 | `0x124CF` |
| startup splash | 6004 | 6005 | `0x128B5` |
| title | 6008 | 6009 | `0x0E881` |
| interstitial A | 6050 | 6051 | `0x10FE7` |
| interstitial B | 6052 | 6053 | `0x10FFC` |

The same odd/even pairing is present in the extracted animation banks:
Muncher/Troggle tables 1000–1005 versus 1006–1011, cartoons 2012/14/16/18/20
versus 2013/15/17/19/21, and continuation sheets 3012/14/16/18/20 versus
3013/15/17/19/21.

## Exact four-color mapping

The 32 bytes at `DS:0464` are 16 device-index pairs. Indexing each pair by the
ordinary 16-color logical value gives:

```text
logical:  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
CGA:      0  1  1  1  2  2  2  3  0  1  1  1  2  2  3  3
VGA:      0  B  D  F  4  5  6  7  8  9  A  1  C  2  E  3
```

Thus the CGA foreground path collapses the blue/green/cyan family to hardware
index 1, red/magenta/brown to index 2, the light family to index 3, and dark
gray to black. Driver mode 1 supplies light cyan, light magenta, and white as
the three fixed foreground colors. Its separate background register is
page-selected: startup/UI pages use black, while the live gameplay board
proves dark blue `0000AA`.

The archived CGA PCX files are two-bit packed images whose header palette
contains red-only placeholder triples. Their scanline indexes are authoritative;
the actual displayed RGB colors come from the active CGA hardware palette.
Native therefore decodes indexes 0–3 directly to `000000`, `55FFFF`, `FF55FF`,
and `FFFFFF` rather than treating the placeholder header triples as display
colors.

## Native implementation and gates

The native loader accepts a graphics mode, selects `NCGA.RES`/`LOGO.004`, and
resolves the logical VGA IDs above at the asset boundary. Game state, animation
frame numbers, coordinates, timing, and scene script IDs remain shared. The
renderer applies the recovered 16-to-4 collapse to vector primitives while
the exact indexed CGA images supply the static art and actors.

Because a modern Windows machine does not expose DOS video-adapter detection,
VGA remains the default and command-line `--cga` (also `-cga` or `/cga`) selects
the original CGA presentation.

`game_render_state_test` now enforces:

- independent raw-PCX FNV hashes for `LOGO.004`, the CGA title, and the CGA
  Muncher sheet;
- one aggregate independent hash across all 22 CGA PCXF sheets;
- valid resolution of all 302 CGA BTMP records, including origin-sentinel
  life/Troggle records and continuation sheets;
- exact primitive color collapse for every native logical color group;
- confinement to the three CGA foreground colors plus the page-selected
  background register;
- exact dark-blue board background, magenta grid, white HUD, and cyan actor
  output;
- complete Hall compositions for both ordinary white-on-black rows and the
  statically recovered opaque white-on-magenta admitted-row highlight, with
  native FNV-64 values `6de5ba02b0677e9b` and `797c489e50013b35`;
- the live-proven empty Factors user Hall, including CGA-specific y=120/129
  empty-list baselines, at native FNV-64 `cc8863d9b565938e`;
- no changes to the existing pixel-identical VGA regression hashes.

The later muted live audit adds an original DOSBox-X `machine=cga` run at the
hardware mode's native 320x200 output. Its version, splash, and title pages
match the native framebuffers at every pixel for 46, 86, and 226 captured
frames respectively; the source bytes, mode-switch segment, frame count, rate,
palette, and ordering are pinned in `analysis/cga-live/startup-report.json`.
A second pinned audit in `analysis/cga-live/ui-report.json` matches the Number
Options page for 130 source frames, the empty Factors Hall for 140, and a
complete level-1 Factors gameplay board for 94. The Hall comparison exposed
and corrected a real mode-specific layout difference: CGA uses y=120/129 for
the empty-list pair while the VGA argument-7 browser/post-game branch uses
y=100/109. The board comparison independently exposed the background-register
distinction above; all other board pixels already matched. CGA remains
complete at the application-controlled framebuffer boundary. Five additional
native-resolution scene recordings match all five cartoons and their completed
close/load/open Wipes: scenes 1-4 match every ordered source run, while scene 0
matches ticks 2-182 in order and observes its unsampled first-refresh state
exactly during the repeated animation. Together with exhaustive resource,
primitive, layout, behavior, and composition gates, these cover the remaining
parameterized states without treating desktop scaling or monitor response as
game code. See `analysis/cga-live/cartoon-transition-report.json`, the five
`analysis/cga-live/number-scene-*-report.json` reports, and
`analysis/PLATFORM-BOUNDARY-AUDIT.md`.
