# Word Munchers startup/version static audit

This audit uses the preserved `WM.EXE`, `PRODUCT.PF`, `LOGO.256`,
`LOGO.004`, Word resource archives, the in-memory native renderer, and two
muted lossless DOSBox-X startup recordings. Static addresses below come from
the unpacked executable; the live-frame hashes are independently regenerated
by `tools/audit_word_startup_capture.py`.

## Version page

The Word graphics bootstrap begins at image `0x0D6B0`, calls the shared setup
at `0x0D80F`, and reaches the version-page painter at `0x0DAA0`. Its device
branches select `LOGO.004` through `DS:1904` or `LOGO.256` through `DS:190D`.
The supplied Word and Number copies of both logo files are byte-identical, so
the native port can use the already embedded shared logo without changing a
pixel.

The separately embedded 350-byte Word `PRODUCT.PF` has native FNV-64
`cca6a750823aeaad`. The fields consumed by the original painter are:

| File offset | Value |
|---:|---|
| `0x008`, `0x00A` | version `1`, `1` |
| `0x016` | `Copyright 1990,1991, MECC` |
| `0x135` | `Word Munchers` |

The runtime structure exposes the version words at `+0x12/+0x14`, and the
painter prepends `Version `. The common formatter clamps the first text row to
30 and applies its five-pixel inset, placing the product name at baseline 35
and `Version 1.1` at baseline 47. The logo begins at y=75. The copyright block
clamps to 180 and uses the same inset, placing its baseline at 185. Both device
paths use black text on a white background.

The complete native framebuffer gates are:

| Device | Native FNV-64 |
|---|---:|
| VGA | `0f20a0f87c2b1bb8` |
| CGA | `8ed7651c7827ba8c` |

## Timed version wait and input-gated splash

The Word main-state presenter begins at image `0x11F02`. Its startup-splash
branch at `0x11F86` selects PCXF `6004` for CGA or `6005` for VGA and draws the
`DS:244B` footer `Press a key for Muncher Menu`. The footer's formatter record
uses logical black with transparent background, matching the native painter.

The first page is not an indefinite input gate. Startup controller `0x11910`
calls the version bootstrap/painter at `0x0D6B0`, then calls `0x1193C`.
That wrapper passes literal `0x012C` (300) to the common event-or-clock waiter
at `0x0BD06`. The waiter returns on an earlier input event or synthesizes its
timeout result when the public clock reaches the deadline. At the recovered
29.1375336 Hz clock, 300 ticks are 10.2959984 seconds. Number's isomorphic
wrapper at `0x121B6` contains the same literal and contract.

Only the device-specific splash is indefinitely input-gated. After either an
early acknowledgment or the 300-tick version timeout, a second acknowledgment
enters the Word title. The native runtime preserves that split, permits a
pointer button to acknowledge either page, and lets `Ctrl+Alt+F1` skip startup
and open the level selector directly. A coalesced update that expires the
version wait stops at the splash and cannot apply leftover time to its input
gate. Returning from Word still reaches the shared launcher.
The Win32 bridge also preserves the original one-event boundary: a physical
printable key's `WM_KEYDOWN`/`WM_CHAR` pair crosses only one gate. Enter's
trailing control character cannot skip the splash, and a digit that dismisses
the splash cannot also select a title row. See
`WIN32-INPUT-CONSUMPTION-AUDIT.md`.

The complete splash framebuffer gates are:

| Device | Native FNV-64 |
|---|---:|
| VGA | `4cd75eb6d6fea514` |
| CGA | `3043b05c7674e388` |

## Muted live validation

`analysis/captures/wm_001.avi` is an untouched 949-frame run at exact capture
rate `2190197/31250` Hz. Full version frames 4-735 hash to native's
`0f20a0f87c2b1bb8`; transition begins at frame 736 (10.487070 seconds from
capture start), and full splash frames 742-949 hash to native's
`4cd75eb6d6fea514`. The splash remains present through the end of the run.

`analysis/captures/wm_003.avi` injects one key after about one second. Its full
version run ends at frame 71, transition begins at frame 72 (1.013037
seconds), and frames 78-450 remain on the exact splash hash. This independently
proves the earlier-input branch and that the same event does not cross the
second gate. A raw title screenshot hashes to native's
`d0862a8ebebc6146`.

The clean representative frames are
`analysis/word-live/00-version-live-640x400.png`,
`analysis/word-live/01-splash-live-640x400.png`, and
`analysis/word-live/02-title-live-640x400.png`; the complete run segmentation
is in `analysis/word-live/report.json`. The VGA values include the recovered
Word-only `07CF00 -> CFC700` palette-index mapping documented in
`WORD-PALETTE-STATIC-AUDIT.md`; the prior sparse Number-derived mapping left
121 splash pixels green.
