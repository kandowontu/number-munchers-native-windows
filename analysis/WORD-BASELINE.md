# Word Munchers preservation and static baseline

This is the initial evidence ledger for the supplied Word Munchers disk.  It
was produced without launching DOSBox, `WM.EXE`, or any native GUI.  Static
extraction establishes provenance and reusable assets; it does **not** yet
establish behavioral or visual parity.

## Preserved source

The supplied VHD was copied byte-for-byte to
`original-media/Word Munchers.vhd`.  The source and preserved copy have the
same SHA-256 digest.

| File | Bytes | SHA-256 |
|---|---:|---|
| `Word Munchers.vhd` | 7,340,544 | `6495CEEFDE1B00533C1D7D95A0E1709A3EDD3229870EAF54AD042182F5DDDF56` |
| `WM/WM.EXE` | 179,344 | `EF9C02D87E994E4874C698F553574812A2EA8E9BBCB20220F3665AF4DA00F728` |
| `WM/WM.RES` | 30,890 | `CA3F86791024626B29FB384439955E2DD0F87620D61B93CD80765DAF81D3815E` |
| `WM/MCGA.RES` | 213,837 | `1B029B81E3CC4D114A453C3749948FF61E7B0E26A3F70814F80A992ADD5FE8BC` |
| `WM/CGA.RES` | 85,637 | `DBCF22A13B49BD6707B3F980E3B0863545136B69163B1B026EC8BCE4DEBD0803` |
| `WM/WLIST.BIN` | 13,474 | `6BB68A54D58098D4438C1EBC2AC4EFB0063B4211894405BA4DAB2402BA9A0E91` |
| `WM/WM.CFG` | 636 | `644424464FB69197C3CFBD4FF60056C28C5B9A617D4A9D3EBCEBCA8AC4D9BD94` |
| `WM/PRODUCT.PF` | 350 | `265158C6DDE6B2A919FA58BB3A7510725F4DF518949D4B7C6E47DA12075E80A5` |
| `WM/LOGO.256` | 2,117 | `75896A90C1114721A2681EB79BC1E76B9AFC846810D92341672E0E568E69D1BF` |
| `WM/LOGO.004` | 1,562 | `F4373C8578A604275550BA0CACD6FD66A2A4DD38BEFCDC6F1AA123FE822BD8BA` |

`tools/extract_fat12.py` identifies a FAT12 volume at partition LBA 63 and
records all 87 filesystem entries in `analysis/word-disk-manifest.json`.
The complete read-only extraction is under `extracted/word-munchers`.

## Executable image

`WM.EXE` is an unpacked MZ executable.  The DOS header declares the complete
179,344-byte file, a 10,752-byte header, a 168,592-byte load image, 2,663
relocations, and initial `CS:IP = 0000:0000`.  The reproducible static outputs
are:

- `analysis/WM-unpacked-image.bin`
- `analysis/WM-unpacked.ndisasm` (69,083 lines, 3,119 call instructions)
- `analysis/WM-unpacked-strings.txt`
- `analysis/WM-unpacked-audit.json`

Readable program text establishes the high-level surface that the future
parity ledger must cover: Play, Hall of Fame, Information, Options, Quit,
password and Hall editing, joystick calibration, content difficulty, selected
vowel sounds, word preview, Demo, score/level display, wrong-word and Troggle
feedback, replay, name entry, VGA/CGA selection, and PC-speaker/AdLib resources.
This is a static inventory only; exact routing, timing, and composition remain
to be recovered and tested.

## Resource archives

The existing MECC extractor parses all three archives without an invalid
range or missing payload:

| Archive | Payloads | Types |
|---|---:|---|
| `WM.RES` | 22 | `CONF` 1, `DATA` 1, `GSND` 2, `PSND` 6, `ADLI` 6, `SCPT` 6 |
| `MCGA.RES` | 38 | `BTMP` 18, `PCXF` 20 |
| `CGA.RES` | 38 | `BTMP` 18, `PCXF` 20 |

Raw payloads and per-entry hashes are under `assets/word/ripped/raw`.  Both
graphics families produce 20 sheets, 18 animation tables, and 225 frame PNGs.
All 83 origin-only BTMP records are resolved by the same measured Muncher-life
and Troggle dimensions used by the Number Munchers archive; none remains an
unrendered sentinel.  VGA and CGA contact sheets are under
`assets/word/ripped/vga` and `assets/word/ripped/cga` respectively.

The native presenter also follows each BTMP record's source-sheet ID. This is
material for the six late frames in Word scenes 2003/2005 that live in PCXF
3003/3005. A dedicated static audit locks those continuation frames and the
byte-identical Word/Number eating records; see
`analysis/WORD-SPRITE-STATIC-AUDIT.md`.

The CGA PCX headers contain placeholder color triples, as in Number Munchers.
The converter renders their authoritative packed indexes through BGI mode-1's
black/light-cyan/light-magenta/white artwork palette; gameplay separately
programs the background register to dark blue. Word's exact 16×2
primitive-color table at `DS:1188` is now recovered and matches the common
native collapse byte-for-byte. Complete startup, title, Options, Hall, and deterministic
board compositions are four-color/headless hash gates; see
`analysis/WORD-CGA-STATIC-AUDIT.md`. Live startup, Options/Hall, and level-1 board CGA
framebuffer comparisons are now exact in `analysis/cga-live/startup-report.json`
and `analysis/cga-live/ui-report.json`; broader CGA gameplay/cartoon pages remain
open.

## Word-list structure recovered so far

`tools/audit_word_list.py` validates the complete structure and emits the
lossless `analysis/word-list-audit.json` report.

`WLIST.BIN` begins with 22 little-endian 32-bit offsets: 21 section starts and
one end offset.  Each section contains eight cumulative available-word counts
(one per difficulty) followed by fixed six-byte records.  Static loader code
confirms that the values are counts rather than end indexes: every section
retains one additional record, and the final count is therefore one less than
the physical record count.  The first 20 section exemplars agree
with the executable's displayed sound labels:

`cake`, `hat`, `tree`, `bell`, `kite`, `fish`, `boat`, `fox`, `mule`, `duck`,
`moon`, `book`, `mouse`, `haunt`, `oil`, `car`, `chair`, `deer`, `bird`, and
`corn`.

Those sections contain 2,197 records in total; section 21 contains the exact
six-record low-difficulty `mule` distractor fallback. Some short words carry
nonzero bytes after their first NUL (for example `T`, `S`, or `p`). The complete
consumer trace proves those tails are inert fixed-record padding: board pointers
are only passed to NUL-terminated BGI `textwidth`/`outtext`, while correctness
lives in a separate signed-index table. Native preserves the tails losslessly
but never displays them. The eight counts map directly
to the eight visible choices from first-grade easy through
fifth-grade-and-above.  Exact target/distractor use and the fallback branch are
traced in `analysis/WORD-CONTENT-STATIC-AUDIT.md`.

## Shared-engine opportunity and parity boundary

The archives use the same MECC container tags, PCX/BTMP layouts, GFT fonts,
sound families, and SCPT scene format already implemented for Number
Munchers. The native asset loader now selects and embeds the complete Word
archive/font set in both display modes, including Word's distinct scene and
continuation-sheet ID ranges. Exhaustive native provenance, sheet, palette,
and all-225-frame descriptor checks are recorded in
`analysis/WORD-ASSET-BRIDGE.md`; the executable color table and complete
composed CGA page gates are in `analysis/WORD-CGA-STATIC-AUDIT.md`. All six Word SCPT resources now execute
through the native scene VM with exact thread orders, display-specific frame
resolution, terminal states, per-bank callback bias, and Word-specific OPL
patches locked in `analysis/WORD-SCENE-STATIC-AUDIT.md`. The indexed renderer
and input shell now host the first Word-specific presentation slice. The
key-or-300-tick version page and separately input-gated device splash are
recovered from the Word executable, product record, and muted live captures; see
`analysis/WORD-STARTUP-STATIC-AUDIT.md`. Hall
admission, name entry, post-game Hall context, and replay routing are now
statically recovered, integrated, and headless-tested; their complete live
pixel comparison remains open, while persistence is gated separately. See
`analysis/WORD-FEEDBACK-TERMINAL-STATIC-AUDIT.md`. The Word Troggle and
safe-zone scheduler is now statically recovered, integrated, and headless-
tested, including random interior player spawn, all twelve pressure tiers,
species trails, overlap/collision, and deferred life loss; see
`analysis/WORD-TROGGLE-STATIC-AUDIT.md`. Live enemy/presentation comparison
remains open. Both archived Information groups are now hosted
directly from `DATA:1`: six main-menu pages and five pre-game pages, including
exact text records, character placements, paging, and routing; see
`analysis/WORD-INFORMATION-STATIC-AUDIT.md`.
The former fabricated Options page is now replaced by the statically recovered
five-entry menu plus functional Set Content/difficulty/vowel/preview, Hall erase,
and password routes. Their exact-versus-approximate boundary and native frame
gates are recorded in `analysis/WORD-OPTIONS-STATIC-AUDIT.md`; joystick
calibration and input are now statically integrated as recorded in
`analysis/WORD-JOYSTICK-STATIC-AUDIT.md`. The exact four-state vowel boxes,
driver hatch, label/help coordinates, target-availability table, and 10x6
Preview grid and Hall/password contracts are now closed at the static/headless
level. Live comparison and physical-controller validation remain open.
Native persistence is gated separately in `analysis/WORD-PERSISTENCE-AUDIT.md`.
Word's embedded `GSND:2` gameplay bank now drives the statically recovered
Troggle entry, Time Out, chew, warning, collision, destructive-retirement,
pre-game pager, and board-completion sites through both native AdLib and
PC-speaker paths. Its two-section, 224,919 ms attract score is also decoded
from the original conductor and sixteen persistent parts. Seven formerly
missing Word-only resident instruments were recovered directly from the
executable. Ordinary gameplay and scene AdLib events now retain one chip across
cues, while Demo adds its score channels to that owner; see
`analysis/WORD-GAMEPLAY-AUDIO-STATIC-AUDIT.md`.
The first non-presentation Word runtime slice joins generated boards
to shared score/life/pressure state in
`analysis/WORD-GAME-CORE-STATIC-AUDIT.md`; presentation and routing remain
outside that core-only gate. The first real presenter now hosts the recovered
two-gate startup, title widget, generated board, player movement/chew input,
and both
shared-launcher return routes. It also hosts all six scripted cartoons with
their recovered trigger, tick, painter, callback-bank, and teardown lifecycle;
see
`analysis/WORD-RUNTIME-PRESENTATION-AUDIT.md`. Word Hall ranking/state and its
original static painter composition are likewise native and headless-tested in
`analysis/WORD-HALL-STATIC-AUDIT.md`; live Hall framebuffer comparison remains
open, while the separately recovered eraser composition is statically gated in
`analysis/WORD-OPTIONS-STATIC-AUDIT.md`.
The board's complete 20-label target question, original centering formula and
sound-5 baseline exception, Level/Demo branch, score box, and three-position
reserve footer are independently gated in
`analysis/WORD-BOARD-HUD-STATIC-AUDIT.md`.
The Word title now enters its statically recovered autonomous demonstration,
including exact selector-5 PRNG/action rules, first-failure terminal routing,
demo-only HUD/feedback, the recovered shared PCX Wipe lifecycle with distinct
clean/collision terminal variants and Hall/logo/board transitions, silent board
completion, and a looping score sharing one continuous YM3812 with channel-0
cues. The controller, timing, transition, and audio-conductor evidence
is recorded in `analysis/WORD-ATTRACT-STATIC-AUDIT.md`.
All 33 executable random-wrapper call sites and their integrated ordering are
now inventoried in `analysis/WORD-PRNG-STATIC-AUDIT.md`. That audit exposed and
corrected a native five-draw drift at silent Demo completion: the original
routes directly to the unattended terminal and never invokes the ordinary
cartoon selector.

No complete or 1:1 Word Munchers port is claimed by this baseline. Muted
original VGA evidence now covers startup, title, the instruction question, all
eleven Information pages, and one level-1 user-board/right-move/complete-chew
slice. The latter is preserved under `analysis/word-live/gameplay` with a
repeatable hash audit in `report.json`.
The word-list rules, core score rules, Troggle/safe-zone job lifecycle,
feedback/post-game routing, resource-ID mapping, and supplied configuration
layout now have native gates.
The latter includes all seven active Hall entries and a lossless parser in
`analysis/WORD-CONFIG-STATIC-AUDIT.md`. Scene/audio resource dispatch and its
native headless presenter lifecycle are now closed. The eleven archived
instruction pages now have static/headless presentation gates. Live
frame/device parity outside those captured slices and organic Word attract
presentation/audio-device comparison remain open before Word parity. Post-NUL
word-record semantics and the remaining
static Options contracts are now closed. The attract controller and decoded music are no
longer functional gaps, but uncaptured Word paths and physical audio comparison
are not claimed as live parity.
