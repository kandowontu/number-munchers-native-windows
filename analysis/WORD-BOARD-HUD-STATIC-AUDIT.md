# Word Munchers board question and HUD static audit

This audit combines the preserved unpacked Word executable, embedded resources,
the in-memory native renderer, and a muted lossless level-1 user-game capture.
Its repeatable audit does not launch DOSBox, `WM.EXE`, the native GUI, or audio.

## Board painter dispatch

The board painter beginning near load-image `0x108C2` draws the board frame and
then dispatches the presentation helpers in this order:

- `0x10C87`: warning/Time Out region;
- `0x10AAF`: target exemplar BTMP 6001/6000 at the upper right;
- `0x10CF5`: current target-sound question;
- `0x10E84`: user score footer;
- `0x10FE9`: user reserve icons;
- `0x110AE`: user Level or Demo header.

It also draws the two white question rules from `(92,2)` through `(234,2)` and
from `(92,16)` through `(234,16)`. These endpoints are inclusive, matching the
native renderer's horizontal-line primitive.

## Exact target-sound question

Before the label, the owner at load-image `0x10AAF` selects VGA BTMP 6001 or
CGA BTMP 6000, indexes its 20 records by the same target sound, and blits into
the inclusive destination rectangle `(260,0)..(306,22)`. Black record pixels
are transparent. On VGA boards the sheet indexes pass through the resident
PCXF 6003 Hall/gameplay DAC; the organic tree and bell boards prove that the
local green ramp must appear as the captured yellow/olive ramp. Native now
embeds and paints all 20 exact exemplars rather than omitting the picture.

Routine `0x10CF5` copies the 20 far string pointers rooted at `DS:2026`, then
selects one with the live target index at `DS:5EB2`. The preserved strings are:

| Index | Header | Index | Header |
|---:|---|---:|---|
| 0 | `/a/ as in cake` | 10 | `/oo/ as in moon` |
| 1 | `/a/ as in hat` | 11 | `/oo/ as in book` |
| 2 | `/e/ as in tree` | 12 | `/ou/ as in mouse` |
| 3 | `/e/ as in bell` | 13 | `/au/ as in haunt` |
| 4 | `/i/ as in kite` | 14 | `/oi/ as in oil` |
| 5 | `/i/ as in fish` | 15 | `/ar/ as in car` |
| 6 | `/o/ as in boat` | 16 | `/air/ as in chair` |
| 7 | `/o/ as in fox` | 17 | `/eer/ as in deer` |
| 8 | `/u/ as in mule` | 18 | `/ir/ as in bird` |
| 9 | `/u/ as in duck` | 19 | `/or/ as in corn` |

The called helper at load-image `0x0D2CB` computes
`x = (0x120 - textWidth) / 2 + 0x14`. This is centering over the board span
`x=20..308`, whose center is `164`. The ordinary text baseline is `y=6`;
target index 5 alone increments it to `y=7`. Native `renderBoard()` therefore
uses `drawCenteredText(font, 164, target == 5 ? 7 : 6, label)` rather than a
generic screen-centered approximation.

The switch at `0x10D81` then calls the same zero-width font-driver
macron/breve helper used by Preview for sound indices 0-11. The live
`/e/ as in bell` Demo header exposed the previously omitted short-e breve:
two two-pixel caps at `(115..116,5)` and `(122..123,5)` plus the five-pixel
base at `(117..121,6)`. Native now applies the recovered even/odd single- and
double-vowel marks to all twelve affected labels and tests all twenty header
variants independently. The second organic board also isolates the even
short-e macron as the helper's inclusive seven-pixel `x..x+6` stroke; the
double-vowel form is the inclusive fifteen-pixel `x..x+14` stroke.

## Level, Demo, score, and reserves

Routine `0x110AE` branches on the Demo flag. A user board writes the
length-prefixed `Level: ` string from `DS:2268` at `(0,6)` and appends the
formatted live level. A Demo board writes `Demo` from `DS:2270` at `(30,6)`.

Routine `0x10E84` branches in Demo. On a user board it writes `Score: `
from the string table rooted at `DS:21F6` at `(0,187)`, outlines the inclusive
screen area represented natively by `(50,184,65,13)`, and writes the formatted
32-bit score at `(56,187)`. The numeric formatter is at `0x10E49`. Its Demo
branch at `0x10F13` omits Score and instead centers
`Press a key for Muncher Menu` from `DS:220F` at baseline 187.

Routine `0x10FE9` is also suppressed in Demo. It caps the visible reserve count
at three and paints `BTMP 1006` frame 17 at `y=179`, beginning at `x=148` and
advancing by 44 pixels. The resulting positions are `148`, `192`, and `236`.

## Native regression gate

`munchers_app_headless_test` reconstructs the header independently and compares
its pixels against `renderBoard()` for all 20 target sounds. The gate locks
both rules, all 20 BTMP exemplar pictures, their VGA/CGA resource mapping,
all recovered overstrike marks, the exact Level string, every label, center
`164`, and the index-5 baseline exception. A second region
comparison locks score `12345`, the score box, and two reserve icons. A third
locks the Demo header and exact centered Muncher Menu footer.

## Live level-1 board slice

`analysis/captures/wm_007.avi` reaches a first user board whose target is
`/e/ as in bell`. The complete 6x5 contents, `Level: 1`, zero-point score box,
reserve row, blank safe starting cell at row 3/column 3, and the destination
word `said` at row 3/column 4 are retained in
`analysis/word-live/gameplay/chew-sequence`.

The right move proves an otherwise invisible presenter detail: the cell label
primitive repaints an opaque blue 32x8 character box before its white glyphs,
then the grid and safe corners are foreground records. Native now uses that
ordering, and the union of the source and destination cells matches every
stable captured movement, target, standing, and chew hash. The two single-frame
partial updates are cataloged rather than promoted to logical animation poses.
`tools/audit_word_gameplay_capture.py` reproduces the complete run table in
`analysis/word-live/gameplay/report.json`, while `munchers_app_headless_test`
locks the stable composites directly.

`analysis/captures/wm_011.avi` supplies a second complete organic user board:
all 30 `/e/ as in tree` cells, its tree exemplar, player and safe-zone start,
the full `Time out` overlay, repeated quit-No overlay, downward movement, and
later safe/warning/entry states match the native full frame. The repeatable
source/run/hash report is `analysis/word-live/gameplay/wm011-report.json`.

`analysis/captures/wm_009.avi` additionally contains a 51-frame organically
entered Demo board interval. After its initial painter samples, frames 49-92
hold one stable 320x200 board for 44 refreshes. The repeatable report at
`analysis/word-live/attract/wm009-report.json` matches native with zero pixel
differences across the complete 320x200 frame at `0x6426d16846667dac`. Its
top-right bell is the frame-3 exemplar paired with `/e/ as in bell`, not a
guest pointer, and is now included in both the audit and native gate.

This closes the board/HUD/player row rather than only its level-1 slice. The
board generator is independently executable-backed for all 20 sound sections,
eight difficulties, target/distractor selection, and exact PRNG draw order;
the painter gate exhaustively renders every target label, exemplar, overstrike,
footer branch, score/reserve combination, cell payload, movement direction,
and all seven chew records. The two organic user boards and organic Demo board
provide complete full-frame anchors for that parameterized composition.

The long seed-`0x8E74` source also supplies the formerly missing organic
completion boundary: its clean final chew reaches the measured board-to-cartoon
Wipe with terminal record 13, and the six scene captures independently lock the
completed close/load/open/black stages, bank-specific covered holds, and first
10 Hz scene tick. Reference-control scene runs still provide fresh-board
fixtures, but they are no longer the only source evidence for the completion
predicate. CGA rendering, physical display timing, pointer devices, and audio
remain tracked by their dedicated rows rather than keeping this board/player
row partial.
