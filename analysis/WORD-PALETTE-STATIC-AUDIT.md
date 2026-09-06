# Word Munchers VGA page-palette static audit

This audit examines the raw indexed PCXF records, their 256-entry palettes, the
preserved Word load image/disassembly, lossless Number and Word framebuffer
evidence, and native in-memory frames. Its repeatable checks are offline and do
not launch DOSBox, either original executable, the native GUI, or audio.

## Shared page evidence

Word uses the same full-page image helper at image `0x1220E` for all three
relevant VGA pages:

| Page | Call site | VGA resource handle | PCXF |
|---|---:|---:|---:|
| Title | `0x0F525` | `DS:1779` | 6009 |
| Hall | `0x11C5B` | `DS:1773` | 6003 |
| Startup/demo splash | `0x11F8D` | `DS:1775` | 6005 |

The Number and Word copies of PCXF 6003 are byte-identical. Their PCXF 6009
pixel maps differ, but their complete palette blocks are byte-identical. The
PCXF 6005 palette blocks differ at only one entry: index 252 is `(187,0,59)` in
Word and `(0,0,0)` in Number. Word does not use that index; Number uses it only
as black. All 45 indexes actually used by Word 6005 therefore have the same
source RGB triples in the Number palette.

This proves that the existing Number capture-backed page mappings transfer by
palette index to the Word pages. It also rules out the tempting but incorrect
explanation that Word 6005 needs a wholly different VGA palette.

## Missing Word-only ramp entry

The old native title remap was deliberately sparse: it contained only colors
encountered in captured Number title/splash pixels. Word 6005 uses standard
palette color `07CF00` for 121 pixels, while neither Number 6005 nor Number
6009 uses that source color. Those Word pixels consequently bypassed the
remap and emerged from the generic six-bit DAC path as green `04CF00`.

The captured Hall palette provides the missing same-index pair
`07CF00 -> CFC700`. This is not an arbitrary neighboring-color estimate:

- PCXF 6003 is byte-identical between games;
- PCXF 6005 uses the same value at that palette index in both games;
- Hall, title, and splash call the same original image helper; and
- every green-ramp pair shared by the independently measured native title and
  Hall mappings already agrees exactly (`0B8700`, `07B700`, `00E700`,
  `077300`, `23FF23`, and `DBFFDB`).

The native title/splash mapping now includes the omitted pair. A focused gate
requires `CFC700` in the composed Word splash and rejects the former `04CF00`.
The corrected full-frame VGA splash FNV-64 is `4cd75eb6d6fea514`; exactly 121
source pixels are affected. Title, Hall, Number, and CGA hashes remain on their
separate existing gates.

## Muncher palette slot 111

The live first-board Word user chew and the first Number Demo board independently
resolve the darkest occupied Muncher slot. Both display `414100` at raw player
index 111. The first Number board sample is frame 2544 of
`analysis/original-demo-full-internal.avi`; the Word sample is in frames
2523-2538 of `analysis/captures/wm_007.avi`. A complete scan of the preserved
Number Demo AVI finds no `413D00` pixel.

This invalidates the former native-only first-board/post-Hall color split. The
raw inputs remain useful cross-checks:

- both PCXF `1006` player sheets store `(04,40,00)` at index 111;
- both games' PCXF `6003`, `6005`, and `6009` palettes store `(07,43,00)` at
  that same index; and
- open-mouth record 12 contains exactly one occupied index-111 pixel, while
  reserve record 17's `(16,140)` 36x39 crop contains none.

Native now maps the occupied player slot to `414100` on first, clean-terminal,
post-Hall, replay, and later boards. The recovered Hall/title/Demo latch is
still modeled and headless-tested because it belongs to the original page
lifecycle, but toggling it is pixel-neutral for this player-sheet slot. The
renderer gates require complete first-board/post-Hall frame identity rather
than permitting the obsolete `413D00 -> 414100` delta, and the reserve icons
remain unaffected.

The exact Word movement/chew hashes and both partial-refresh exceptions are
reproducible from the preserved logical frames with
`tools/audit_word_gameplay_capture.py`; its checked report is
`analysis/word-live/gameplay/report.json`.
