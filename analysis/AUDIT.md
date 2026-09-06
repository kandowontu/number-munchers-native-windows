# Number Munchers reverse-engineering and port audit

## Current deterministic boundary

The current headless replay supersedes the older intermediate attract notes
later in this historical audit. Static recovery plus exact lossless Number and
Word re-extraction resolves the player-collision bite as the repeating
21-scheduler-tick BTMP sequence `12,13,14,13,12,13`, followed in the
demo by the statically recovered 150-tick feedback wait, 39 captured restored-
board frames, and the original six-stage PCX Wipe over seven frame intervals.
The same lossless stream now locks the subsequent six-frame Hall-to-logo Wipe
and nine-frame logo-to-board Wipe, including both torn dynamic board-painter
samples, to exact full-frame hashes.
Static xrefs now additionally prove that this board presenter is not confined
to Demo: Number `0x09789 -> 0x1175A` and Word `0x0A356 -> 0x10A66` end every
ordinary initial/next-board construction in the same type-2 close, clear,
type-1 open, and dynamic paint. Native now runs that nine-interval lifecycle
for normal starts, Replay Yes, direct advances, and post-cartoon advances,
freezing input and fresh gameplay jobs; fixed-seed Number and Word full-frame
timelines are recorded in `BOARD-PRESENTER-STATIC-AUDIT.md`.
The separate every-third-board branches at Number `0x0F1BA` and Word
`0x0FE2D` are recovered as blocking type-2 close, scene load/setup, target
clear, type-1 open, and literal 10 ms delay sequences. Native preserves the
completed board and freezes input/scene callbacks during the transition.
Number retains a five-state static/native entry timeline. Six muted lossless
Word reference-control captures now measure the production owner itself:
bank-specific covered cyan holds last 41–44 samples, the upward opening reveals
a cleared black page for 6–8 samples, and tick 1 follows on the next 10 Hz
callback. Native gates those completed states/timers while suppressing DOS's
torn single-buffer refreshes; see `CARTOON-PRESENTER-STATIC-AUDIT.md`.
The loader's following event `0xA6` is now resolved through the resident
high-bit branch as Music/AdLib-gated stream 38 from the active scene bank.
All eleven channel-9 conductors, their fractional-rate parts, exact patch
records, 10 ms post-Wipe start, channel-0 coexistence, and teardown are
decoded and headless-gated in `CARTOON-SCORE-STATIC-AUDIT.md`.
The replay now follows the complete first board through that collision and
interstitial lifecycle, constructs the captured second Prime board from PRNG
call 271, then matches its up/correct-2/left/wrong-1 controller route and
second interstitial. The following Equals-30 board begins at call 334 with all
30 labels, player cell `(1,1)`, and the complete 320×200 frame matched. This
extension exposed and corrected the original descending multiplication-divisor
order, random commutative-operand swap, and thin board-operator glyphs. The
replay now continues across the captured third-board rightward five-answer
route, upward final munch, Worker collision, 21-tick bite, fresh terminal
Troggle dwell, and the captured `Aargh` exclamation at PRNG call 507. Static
event recovery proves that the following 10.63-second Hall span is an external
any-key interruption of the ordinary 450-tick record, not a three-board cutoff;
native therefore continues through the Hall/logo pair unless input is actually
supplied. The captured fresh Multiples-of-15 initializer remains independently
locked at calls 507–584 with a complete-frame match. Replaying the external
title restart now follows the measured standing-only collision route through
call 607, the exact Hall/logo transition, and the following Factors-of-63
initializer at call 695. Frames 18570–20527 then provide a continuous board-
wide oracle through the final warning page. `PARITY.md` is the authoritative
completion ledger for behavior outside those measured intervals.

`MENU-STATIC-AUDIT.md` additionally closes the instruction-specific routing
gap: selection-only Y/N, horizontal choice arrows, Enter-only prompt
acceptance, ignored prompt Space, Enter/Space/left-release paging with right
release inert, Escape back to the
main menu, and normal completion into game selection are implemented and
headless-tested. Gameplay pointer routing is now statically recovered and
headless-tested through its exact hit bounds, queued incremental walk,
vertical tie-break, and second-click munch behavior. `MENU-POINTER-STATIC-AUDIT.md`
recovers the fixed menu widget's inclusive ragged rectangles, non-hovering
motion, click-to-select/click-selected-to-activate rule, selector centering,
single-entry shortcut, Enter-versus-Space filters, and the physical callback's
left/right press/release mapping. `ACCEPTED-KEY-POINTER-STATIC-AUDIT.md`
separately proves that the common waiter rewrites event type 2 to Space, does
not rewrite type 13/code `0xFD`, and accepts keyboard Enter as well as Space
for both feedback implementations. `HALL-STATIC-AUDIT.md`
now separates the Enter-only Hall selector, blocking user Hall, post-game
replay continuation, and demo-only any-key Hall; every keyboard route is
headless-tested. `NAME-ENTRY-STATIC-AUDIT.md` also closes Left/Home/Escape,
empty-name fallback, and replay-choice behavior.
`FEEDBACK-STATIC-AUDIT.md` closes the remaining wrong-answer variants: the DOS
dispatcher reads only the active component, shares one arithmetic formatter
between Equality and Inequality, and lets Challenge delegate to that selected
component. Lossless user and Demo strip checks exposed and corrected the
overlay's erased grid side, two-pixel text offset, heavy arithmetic operator,
and incorrect Demo continuation line.
`SCORING-STATIC-AUDIT.md` additionally corrects a material later-game shortcut:
the executable awards a Muncher at 1,000 and at every later 10,000-point
boundary, permits internal reserves above 99 while drawing only three icons,
and ends the scored game immediately at one million. Native now preserves that
terminal's qualifying-name versus full-Hall routing in headless tests.

## Source preservation

The supplied path contained one fixed VHD. It was copied byte-for-byte to `original-media/Number Munchers.vhd` before analysis.

| Artifact | Size | SHA-256 |
|---|---:|---|
| Supplied VHD | 7,340,544 | `192EEC8BC3255FFFC4D472969AE1CE3DE60C68512E412B6A3C13A6B8BEDEC0C7` |
| Packed DOS `NM.EXE` | 97,983 | `2045DAEE393B5520C9709FA18B70800B4578ABAC3F12D5C8C246DBB46534D670` |
| Unpacked DOS image | 198,128 | `DE42F666542C0EF84B6DD5F0FE49CFAA06EBB79CC05DAA5B9F0E74EBD9B994C3` |

The VHD consists of a 7 MiB raw disk followed by its 512-byte fixed-disk footer. Its FAT12 partition begins at LBA 63. `tools/extract_fat12.py` walks the FAT chains and directory tree without mounting or modifying the source. The complete extraction contains 83 file/directory entries and is indexed by `disk-manifest.json`.

## Executable audit

`NM.EXE` identifies as LZEXE 0.91. It was unpacked for static inspection only; neither packed nor unpacked DOS code is part of the Windows executable.

The unpacked MZ has an 11,776-byte header, a 186,352-byte load image, 2,900 relocation records, and entry point `CS:IP 0000:0000`. `tools/disassemble_dos.ps1` strips the MZ header, preserves the raw load image, and produces:

- `NM-unpacked.ndisasm` — full 16-bit x86 disassembly (76,418 lines)
- `NM-unpacked-audit.json` — parsed MZ fields, all relocation locations, and instruction summary
- `NM-unpacked-strings.txt` — recovered user-facing and runtime strings

The strings and observed execution establish the six game modes, title/options/Hall flows, difficulty configuration, high-score name entry, mouse/joystick paths, time-out state, rule headings, scoring screens, safe zones, and the five named Troggle classes. Reference execution was captured under DOSBox-X at the original 320×200 VGA resolution to audit layout, startup, menu flow, and live boards in every rule family. The scanned MECC user's guide in `tmp/pdfs/MECC_Number_Munchers_manual.pdf` supplies the original species definitions and progression rules; direct behavior from the supplied DOS build takes precedence where the printing differs.

## Resource audit

Three MECC archives were decoded with `tools/extract_mecc_res.py`:

| Archive | Types | Payloads | Purpose |
|---|---|---:|---|
| `NM.RES` | CONF, DATA, GSND, PSND, ADLI, SCPT | 15 | configuration, instructions/data, speaker/AdLib audio, scene scripts |
| `NMCGA.RES` | BTMP, PCXF | 37 | VGA artwork and crop tables |
| `NCGA.RES` | BTMP, PCXF | 39 | CGA artwork and crop tables |

Notable decoded data includes the exact 46-entry prime table through 199, the original instruction/information text, all title/sprite/scene sheets, PC-speaker sound data, five AdLib payloads, and five scripted scene payloads. Every payload is retained independently under `assets/ripped/raw`; artwork also has lossless PNG renditions and per-frame coordinates. Origin-only records are resolved reproducibly into 282 VGA and 302 CGA frame PNGs, including all Muncher life and Troggle poses, with no unresolved sentinel entries.

## Functional parity status

The native program is complete at the reproducible application-controlled boundary. Exact resources alone are not treated as parity evidence: the authoritative row-by-row gates in [`PARITY.md`](PARITY.md), [`WORD-PARITY.md`](WORD-PARITY.md), and [`SUPER-PARITY.md`](SUPER-PARITY.md) total 62/62 verified rows across behavior, palette handling, placement, timing, state transitions, device event streams, and presentation. `CGA-STATIC-AUDIT.md` proves the original driver/archive/logo split, complete 16-to-4 foreground table, and independently programmable background register; native implements them behind `--cga` and headless-tests every CGA sheet/frame. Pinned live CGA audits add exact original/native version, splash, title, Options, Hall, level-1 gameplay, all five Number cartoons, all six Word cartoons, every completed Wipe framebuffer, and Super's startup/title/ordinary-board presentation. The Number empty-Hall comparison corrected CGA's mode-specific y=120/129 message baselines, while the gameplay captures corrected the board background from collapsed light cyan to the live dark-blue register value. The exact boundary and optional physical-machine QA are documented in [`PLATFORM-BOUNDARY-AUDIT.md`](PLATFORM-BOUNDARY-AUDIT.md).

Measured progress includes the requested Alt+Enter and Ctrl+Alt+F1 integrations, a persistent double-buffered framebuffer, six reachable game modes, and the original player chew records. The Win32 class now has no erase brush or resize-redraw flags; timer, resize, and fullscreen paths request no-erase presentation and the client receives only the final complete `BitBlt`. A muted desktop-level probe recorded 761 live presentations through movement, two fullscreen round trips, and two resizes, with no near-black inner-client frame; see `PRESENTATION-FLICKER-AUDIT.md`. A clean 120 Hz reference capture measures four squash pulses in approximately 244 ms. Static recovery at `0x08A31`, `0x093A9`, and `0x07F5B` now resolves that as seven one-tick intervals using records `12,13,14,13,12,13,14`, followed by a terminal record-13 callback at 240.240 ms; the former visually equivalent linear 12–15 order was behaviorally wrong. The preserved Demo AVI additionally gives a later-level live oracle: frames 8462–8478 cover the second Prime board's full correct chew, and native now matches both alternating 320×200 RGB hashes in all seven intervals before reaching the captured 25-point state. Rechecking the first Number board and the live first-board Word chew proved both use `414100`; the former supposed Hall carry-over was native-only. Lossless DOSBox-X internal captures established the original 6×5 board/fixed HUD geometry, native sprite-tile scale, fixed player idle pose, four directional frame groups and movement steps, exact life-icon resource/placement, one blank starting cell with variable answer density, default content ranges, four-operation Equality/Inequality expressions, mode-specific feedback, and a roughly 730 ms Troggle collision sequence alternating stable BTMP 12/13 bite poses before feedback. The captured user Inequality feedback band now matches FNV-64 `0x873482585c0afb4b`, while the Demo collision band matches `0x15a745407e98c823`; all formatter branches and the frozen same-board resume lifecycle are headless-tested. Raw FM capture preserved five exact live-play YM3812 traces: historically named board-start (219 ms), correct Prime munch (260 ms), final-munch/completion/next-board (1,112 ms), wrong Prime munch/feedback (274 ms), and Reggie collision/death/feedback (1,647 ms). Later static call-site recovery proved that several are compound/replaced sequences rather than event-local cues: ordinary board construction is silent, collision starts with stream 12 rather than warning stream 9, and completion submits ordinary stream 15. The traces remain embedded preservation/decoder assets, while runtime dispatches decoded GSND at each recovered caller. Later live runs prove that incorrect munches clear the attempted cell and consume a Muncher, while a voluntary scored quit goes straight to the current Hall without name submission. Final-loss runs establish direct Hall routing for a non-qualifying score, a pixel-identical board-preserving name-entry modal for a qualifying score, the 25-character field, and empty Enter/Escape fallback to `The Unknown Muncher`. Repeated controlled runs at 5, 10, 15, 20, 30, 45, 50, 60, and 85 establish the exact reachable empty-list admission floor: 45 is rejected and 50 is accepted. Two additional Troggle runs measure the level-1 vertical warning, 11.7–13.9-second spawn delay, straight Reggie traversal/exit, safe-zone removal, and post-feedback player recovery. Static executable recovery then identifies the original 12-tier concurrent-enemy, arrival-tick, fixed movement-speed, and five-species weight tables. The executable's feedback pointer table fixes the numeric order as Reggie/Worker/Bashful/Helper/Smarty and the unlock order as Bashful/Helper/Worker/Smarty at levels 2/3/4/5. Static recovery also supplies the exact species steering boundaries, all-species edge exit, and five trail-effect branches now implemented by native. A second static table audit recovers every field of the eleven grade-level content presets and proves that Challenge selects only enabled component games; native now replaces all six content rows when difficulty changes. The same raw screenshot path locks the initial instruction decision, initial and disabled-game selectors, Hall selector, supplied Multiples Hall page, all six Information pages, and eighteen Options-family frames—including editor variants, F1 help, validation dialogs, and the transactional Hall eraser—to complete 320×200 references. It also establishes the board-preserving in-game quit dialog and the exact vertical `Time out` pause marker.

No material audit row remains open. The pinned 365.264-second Number seed-`0xF585` continuation reaches the corrected final collision/phrase draw, exact Less Than 20 initializer, complete last-board route, final Hall, and closing logo. Its companion reconciliation gives 1,363 exact ordered pages and classifies every one of the 237 source-only and 71 native-only residuals; the 25,700-sample route is now an always-on gate. The pinned 11,518-frame Word seed-`0x8E74` run continuously covers autonomous movement, chew, Troggle work, feedback, Hall/logo/board Wipes, and later boards; every residual is classified against the atomic no-flicker presentation contract. Six muted lossless Word VGA captures establish the Wipe's completed stable states/cadence, the separate 10 Hz cartoon clock, and exact native matches for all 586 original-presented distinct stable scene framebuffer runs. Eleven additional native-resolution captures close the five Number and six Word CGA cartoons; Word matches all 583 presented state runs exactly, while Number scene 0 transparently records one first-refresh sampling exception and observes all 118 distinct states. The preserved demo AVI PCM independently validates the attract score: its onset-to-restart span is within 1.691 ms of native, and aligned 10 ms RMS envelopes correlate at `0.9025445222` across the live-effect-bearing cycle. One continuously clocked YM3812 carries score channels 1-8 and replaceable channel 0, while ordinary gameplay and scene cues retain the same chip across events. Physical speaker, controller, mixer, monitor, and room characteristics remain optional machine QA rather than executable behavior; see `PLATFORM-BOUNDARY-AUDIT.md`.

The muted first-board Word user chew supplies an independent palette oracle.
Its occupied player index-111 pixel is `414100`, exactly like the first Number
Demo board and the later Number Prime chew. A scan of the complete preserved
Number Demo AVI finds no `413D00`; that value came only from native's former
first-board special case. Both runtimes still model the recovered logical
Hall/title/Demo latch, but require this sprite slot to remain pixel-identical at
`414100` across first board, clean terminal, Hall, logo, replay, and later
boards.

Muted live Word startup capture now closes a different boundary. An untouched
949-frame run advances from the exact native-matching version hash to the
exact splash hash without input, while a second run accepts a key after about
one second and reaches that same splash. Static wrappers `WM 0x1193C` and
`NM 0x121B6` supply the missing contract: both pass literal `0x012C` to the
common event-or-clock waiter. Native now holds through tick 299, changes page
at tick 300, and cannot spend coalesced remainder on the separately gated
splash. `tools/audit_word_startup_capture.py` reproduces every live run and
hash in `analysis/word-live/report.json`.

A later muted Word run reaches a level-1 user board, moves right onto `said`,
and performs a successful chew. Stable native union hashes match the translated
actor, opaque destination-label background/glyph repaint, directional terminal
hold, standing arrival, and every alternating chew interval. Offline audit
distinguishes frame 2465's 22-pixel partial dirty refresh and frame 2530's
partial sprite update from complete logical frames; see
`analysis/word-live/gameplay/report.json`.

The same muted original session captured the instruction question, all six
main-menu Information pages, and all five pre-game pages. Nine pages matched
immediately. The two Troggle-bearing pages exposed frame/palette defects hidden
by static-native hashes: Troggles use record 15, Helper's record 15 aliases its
record 13 pose, and five highlight colors inherit the title palette. Corrected
native full-frame hashes now match all twelve original frames exactly.

A separate DPI-aware muted run now locks nine Word Options/Hall-family states.
`tools/audit_word_options_capture.py` verifies each 658x553 physical window,
the `(9,103)..(648,502)` 640x400 surface, every uniform 2x2 block, and the
resulting 320x200 hash. The comparison corrected Set Content/Difficulty visual
baselines, Group-1 vowel marks and focus backing, F1 Help placement/footer,
Preview's compact font and 60-word pager, and title-browse Hall's Space footer.
All nine original/native hashes pass in
`analysis/word-live/options/report.json`. A second muted ten-page batch covers
Set Password, the password gate/rejection, joystick attach, three vowel
validations, insufficient targets, initially empty Hall, and the forced
configuration-write alert. `tools/audit_word_options_gap_capture.py` proves
uniform 2x reconstruction and zero pixel mismatches for all ten pages in
`analysis/word-live/options-gaps/report.json`; the comparison corrected
visible-state validation, modal/footer geometry, password/hint placement, Hall
baselines, calibration footer, and write-alert border/text. A separate captured
user terminal path covers both feedback kinds, name entry, an admitted
highlighted Hall, replay Yes/No, and title return. Detected calibration
steps/ready, admission-message/rank variants, no-more-entries Hall, exhaustive
parameterized editor/pointer behavior, and physical WinMM calibration remain
explicitly open in `WORD-PARITY.md`.

The preserved lossless `wm_009.avi` also supplies a short organic Word Demo
board oracle. Forty-four stable logical refreshes share the same board, and
native now matches the complete 320x200 frame at hash `0x6426d16846667dac`
with zero differing pixels. The top-right bell formerly excluded as a guest
pointer is actually the BTMP 6001 frame-3 target exemplar for
`/e/ as in bell`. The comparison exposed and corrected that missing graphic,
its resident gameplay-palette mapping, the centered
`Press a key for Muncher Menu` footer, and the short-e breve. See
`analysis/word-live/attract/wm009-report.json` and
`WORD-ATTRACT-STATIC-AUDIT.md`.

The previously unaudited `wm_011.avi` adds 2,420 lossless frames of an
organic user session. Eighteen completed full-frame states match the native
renderer exactly: a second 30-cell `/e/ as in tree` board and tree exemplar,
`Time out`, quit-No, all downward Muncher phases, safe-zone removal/addition,
the full warning, and the top-edge Reggie reveal/entry/dwell. It corrected the
former visible warning-stage actor and unclipped header-crossing entry. The
one torn entry refresh remains classified separately. See
`analysis/word-live/gameplay/wm011-report.json`.

The 5,557-frame Number `nm_000.avi` now has a dedicated warning/entry audit as
well. Ten completed 320x200 states match the native renderer exactly across
two safe zones, both expiry steps, the warning, its removal, the left-edge
phase-2 dirty-box clear with no visible actor, phases 3–5 progressively
revealing Reggie, and the phase-6 endpoint/dwell alias. This independently
corrected the horizontal entry cycle to `middle, forward, middle, back,
middle`; the former four-element lookup could read past its end at phase 6.
The two partial DOS warning refreshes remain classified separately rather
than being synthesized in the double-buffered port. See
`analysis/number-live/gameplay/nm000-entry-report.json`.

The 3,967-frame continuation `nm_001.avi` adds the opposite horizontal edge.
Twelve completed native frames match its two safe-zone expiries, a new zone
during the warning, warning removal, all five right-edge arrival phases, and
the dwell. This exposed an asymmetric original viewport: right-entry pixels
may overwrite the x=308 grid rule and x=309 gutter while the x=310 outer
outline survives; phase 6 retains the damaged grid column, and only the dwell
callback restores it. Native formerly clipped at x=307 and could not match any
of those arrival frames. Three torn DOS refreshes are classified separately.
See `analysis/number-live/gameplay/nm001-right-entry-report.json`.

The full 22,004-frame Number Demo now supplies the missing bottom edge and a
non-Reggie species. Its focused global-frame interval 2864–2918 contains a
clean pre-entry board, all four visible upward Bashful phases, one 22-block
torn refresh, and a 44-frame endpoint dwell. Six completed states match native
full-frame hashes exactly. The reference proves that the bottom viewport
extends through y=177, preserves the y=178 outer outline, retains blue damage
across the y=176 grid rule through phase 5, and restores all 49 magenta rule
pixels at dwell. It also exposes the active gameplay-DAC mapping of Bashful's
`20A8FC` source highlight to `5DBEFF`, rather than the former native
`20AAFF`. Both Number and Word renderers now gate the shared geometry and
palette behavior. See
`analysis/number-live/gameplay/full-demo-bottom-entry-report.json`.

The later first-board bottom-entry window at global frames 5188–5215 is now a
full isolated parity gate rather than inventory-only geometry. Reconstructing
the directly captured Muncher instead of the corrected continuous route makes
all seven presentation-complete pages match native: downward phases 3–4,
endpoint and idle, Bashful's invisible phase 1, visible phases 2–5, dwell, an
earlier Reggie dwell, and the warning retained by another actor slot. Frame
5196 contains 20 nonuniform doubled blocks and 47 logical pixels belonging to
neither complete neighboring Bashful page, so it remains capture-only. This
raises aggregate complete live Number Troggle coverage from 358 to 365. See
`analysis/number-live/gameplay/full-demo-later-bottom-entry-report.json`.

Extending that interval backward produces a continuous seeded replay across
global frames 5173–5215. All eleven presentation-complete pages match in exact
order through the resident warning, Bashful bottom entry, overlapping downward
Muncher move, Reggie dwell, and entrant dwell. Frame 5184 is a uniform one-row
incomplete dirty write with 41 pixels belonging to neither complete neighbor;
frame 5196 is the previously measured 20-block torn repaint with 47 such
pixels. Both remain capture-only. Seven complete pages overlap the isolated
audit, so four are new and aggregate complete live Number Troggle coverage
rises from 498 to 502. See `analysis/number-live/gameplay/full-demo-later-
bottom-entry-continuous-report.json`.

The later first-board Bashful bottom-exit is now closed across its complete
logical interval at global frames 5687–5718. Seven native pages match the
captured left-facing dwell, `115 -> 105` trail, all five clipped downward
exit callbacks, terminal removal, Muncher upward phases 1–5, row-1 Reggie
dwell, active safe zone, and retained warning. Frame 5711 contains 26
nonuniform doubled blocks, but its logical page is pixel-exactly the prior
completed page above scanline 90 and the following completed page at and below
that boundary. It is therefore capture-only scanout, not native flicker. This
raises aggregate complete live Number Troggle coverage from 365 to 372. See
`analysis/number-live/gameplay/full-demo-later-bottom-exit-report.json`.

That same frames 5687–5718 window is now also a continuous seeded replay,
not only an isolated state reconstruction. Native retains the pre-exit dwell
page for the measured callback delay, composes the current upward Muncher
callbacks with the older Bashful exit records, suppresses three native-only
player callback pages, and presents actor removal immediately after the final
clipped pose instead of repeating that endpoint. The ordered seven-page gate
matches every presentation-complete source run and still excludes frame 5711's
row-90 scanout splice. Logical scheduler deadlines and Borland PRNG calls are
unchanged. These seven pages already contribute to the current 502-page
aggregate, so this stronger evidence adds zero unique pages and leaves the
total at 502. See `analysis/number-live/gameplay/full-demo-later-bottom-exit-
continuous-report.json`.

The board-wide changed-page comparator exposed three earlier complete-page
gaps that the isolated gates had hidden. First, global frames 2840–2918 add a
continuous terminal-player/chew/Reggie-entry/Bashful-bottom-entry route. All
fourteen presentation-complete pages now match in exact order. The prior
selector-5 entry-lag rule had treated an imminent Space callback as a walk and
therefore retained old entry records across the correct-value chew. Native now
predicts the callback on a copy of the Borland generator, preserving the live
PRNG state and reserving the lag for a movement callback. Frames 2851 and 2870
are respectively one-block and 22-block incomplete refreshes, with exact
previous/next/neither pixel partitions, and remain capture-only. Six complete
pages overlap the isolated bottom-entry audit, so eight are new. See
`analysis/number-live/gameplay/full-demo-early-bottom-entry-continuous-report.json`.

The adjacent frames 2919–2944 then cover the leftward player walk while a
later Reggie enters from the right into the first cannibal cell. The DOS
resident surface presents the earlier player's two dirty cells while retaining
the entrant's newer paint only on its working page. Native formerly exposed a
clean recomposition and one extra callback page. It now keeps presentation and
working surfaces separate, matches all ten complete source pages in order,
and releases the later working page after the final player callback. Frame
2940 is a 17-block refresh made entirely from its two completed neighbors. The
stable pre-bite page overlaps the existing first-cannibal gate, adding nine
new complete pages. See
`analysis/number-live/gameplay/full-demo-first-cannibal-approach-continuous-report.json`.

Global frames 3300–3340 expose the next physical scheduler boundary. A
rightward Muncher reaches its terminal record on the same public tick that a
later Troggle slot installs the warning. The original presents the earlier
terminal page without the warning, then the following standing/warning page;
native had flattened both callbacks into terminal-plus-warning. A one-callback
pre-warning hold now reproduces all eight complete pages exactly. Run 2
contains one nonuniform physical frame but two uniform copies of its complete
logical page, so no run is excluded. These eight focused pages are new. The
three new gates therefore add 25 complete live pages and raise the current
aggregate Number Troggle coverage from 502 to 527. See
`analysis/number-live/gameplay/full-demo-player-warning-boundary-continuous-report.json`.

The next three source-only pages at frames 3462, 3474, and 3491 are not
completed game states. The first is an exact previous/next scanout partition;
the second has nine unmatched dirty pixels in a vertical strip; and the third,
despite uniform 2x output, is a 41-pixel incomplete horizontal row write.
Their neighboring complete pages already match native, so no flicker page was
added. See
`analysis/number-live/gameplay/full-demo-post-warning-artifacts-report.json`.

Global frames 3520–3606 then expose a genuine callback-order gap. The row-1/
column-3 Muncher starts its seven-record chew while a bottom-left entrant
finishes, and a new right-edge entrant begins before the chew terminal is
released. The original keeps the earlier player dirty record and later
Troggle working page separate. Native now does the same and matches all fifteen
presentation-complete pages in exact order, including the formerly absent
frames 3559–3563. Frames 3566 and 3567 independently establish their logical
page; frame 3568 changes only one half of one doubled block. This adds fifteen
focused pages and raises aggregate live Number Troggle coverage from 527 to
542. See
`analysis/number-live/gameplay/full-demo-chew-entry-boundary-continuous-report.json`.

Global frames 3607–3885 contain eight remaining source-only samples at frames
3616, 3717, 3729, 3796, 3818, 3830, 3871, and 3883. Exact 64,000-pixel
partitions prove that seven are incomplete dirty repaints; frame 3818 is an
adjacent-page scanout splice with no unique pixel. The uniform 3729 sample is
still incomplete: it retains 277 prior-page pixels plus one pixel belonging to
neither completed endpoint. All eight hashes are absent from native, so no
flicker page is required. See `analysis/number-live/gameplay/full-demo-pre-
departing-enemy-artifacts-report.json`.

The following row-1/column-4 Muncher move overlaps Reggie's departure from row
2/column 4 to column 5. Across frames 3884–3941, native now matches all nine
presentation-complete pages contiguously, including the Muncher's next
movement/terminal/standing records beside Reggie phases 1–6. The recovered
dirty callback clears only the immediately previous/current 41x29 Troggle
boxes; the older part of the clean source-to-current sweep retains the newer
Muncher BTMP paint. Frame 3931 is a 16-block incomplete actor repaint and stays
capture-only. This raises aggregate focused live Number Troggle coverage from
542 to 551. See `analysis/number-live/gameplay/full-demo-departing-reggie-
player-overlap-report.json`.

Frames 3941–4090 close the next continuous interval. Native matches all 24
presentation-complete pages across the standing boundary, the already exact
seven-record chew, two full Muncher walks, and the opening dwell of the
simultaneous chew/right-exit gate. Exact partitions classify frame 4027 as a
24-block partial paint with seven unique pixels at x=116/y=147–153 and frame
4044 as a one-block partial paint with eight unique pixels at x=212/y=107–114.
Neither incomplete page is reproduced. Fourteen complete states are new beyond
the neighboring gates, raising aggregate focused live Number Troggle coverage
from 551 to 565. See `analysis/number-live/gameplay/full-demo-post-departure-
continuous-report.json`.

The permanent first-board comparator now streams every capture frame from
2390 through the terminal collision hold at 6131 and aligns all changed native
pages. The earlier 2390–2544 interval contributes eight exact opening/movement
pages; frame 2459 is a 20-block adjacent-page scanout splice with no unique
pixel. Frames 4706–5173 contribute another exact 51-page contiguous bridge
after the second Cannibal. Four one-frame surfaces there are capture-only: two
nonuniform fragments and two uniform incomplete 41-pixel row writes. The two
boundary pages overlap adjacent gates, so those audits add 8 + 49 pages and
raise aggregate focused live Number Troggle coverage from 565 to 622. See
`analysis/number-live/gameplay/full-demo-first-board-opening-continuous-report.json`
and `analysis/number-live/gameplay/full-demo-post-second-cannibal-continuous-report.json`.

The expanded comparator reports 584 source logical runs, 541 native changed
pages, and 515 exact LCS matches in 74 contiguous blocks. Its second-stage
reconciliation resolves all 69 source-only runs: 68 have valid focused
dirty-paint/scanout evidence and the remaining repeated Cannibal hash is an LCS
alignment ambiguity already present ten times in native. Headless pixel dumps
prove five of the six internal native-only pages are exact actor-layer
partitions of adjacent completed DOS pages; the sixth overlapping Reggie page
is the separately gated callback-local composite. The final 20-page native
block begins after frame 6131 and is the independently hash-gated collision
Wipe, Hall, logo, and next-board painter. This supports a measured board-wide
parity claim for first-board gameplay without broadening it to the rest of the
Demo. See `analysis/number-live/gameplay/full-demo-first-board-continuous-
sequence-report.json` and `analysis/number-live/gameplay/full-demo-first-board-
reconciliation-report.json`.

The next continuous comparator now covers global frames 8353–11187: the first
stable second Prime board through the first stable Equals-30 board. Across
2,835 source frames it measures 53 logical runs and 51 changed native pages;
the exact LCS contains 40 pages in eleven blocks, including one uninterrupted
26-page gameplay run. The interval contains the complete short Prime route,
seven-record correct chew, wrong chew and feedback, restored board, all three
Wipes, Prime Hall, logo, both board painters, and the next stable board. A
reproducible pixel reconciliation classifies all thirteen source-only runs:
eleven are nonuniform scanout/dirty refreshes, frame 8429 is a uniform
three-pixel incomplete player repaint, and frame 11186 is a uniform partial
board painter composed entirely from the two adjacent completed native stages.
All eleven native-only pages are independently permanent-gated completed Wipe
or board-painter surfaces. This supports a measured continuous lifecycle parity
claim for the captured second board and its full interstitial, while making no
claim about the later uncaptured autonomous trajectory. See
`analysis/number-live/gameplay/full-demo-second-board-continuous-sequence-
report.json` and `analysis/number-live/gameplay/full-demo-second-board-
reconciliation-report.json`.

The adjacent Equals-30 gameplay audit streams frames 11187–12392, ending on
the restored board before frame 12393 starts its Wipe. Its 118 source runs cover
five moves/correct chews, Worker and Reggie arrivals, safe-zone activity, the
Worker collision, all 21 bite ticks, stable `Aargh`, and the final 40-frame
board hold. The initial 105/106 alignment exposed one real implementation gap:
native froze a concurrent bottom-entry Reggie at phase 4 when collision
feedback was removed, producing `0xd27819377a45835c`; controlled phase variants
proved the original's phase-5 callback produces exact source hash
`0x25a5e4cc4a413f32`. `beginAttractHallTransition` now performs that one visual
teardown callback without spending a movement decision or PRNG value. Native
matches all 106 completed pages in order. The twelve source-only runs are ten
nonuniform refreshes and two uniform partial-painter composites with locked
pixel partitions; there are no native-only pages. This supports a measured
board-wide gameplay parity claim for the captured Equals-30 board. See
`analysis/number-live/gameplay/full-demo-third-board-continuous-sequence-
report.json` and `analysis/number-live/gameplay/full-demo-third-board-
reconciliation-report.json`.

The immediately following first-board collision at global frames 5719–5799
now has a complete full-frame gate. Native presents the stationary Reggie/
hidden-player page, every one of the 21 ordered bite callbacks, and the final
`Oops`/`Trogglus normalus` feedback page in the exact 23-page source order.
The stationary page is pixel-identical to the separately gated collision-
transient board, so 22 pages are new and aggregate complete live Number
Troggle coverage rises from 372 to 394. Frames 5740, 5752, and 5764 are exact
horizontal scanout splices at logical rows 65, 70, and 76: every pixel belongs
to one of the two adjacent completed bite pages. They remain capture-only. See
`analysis/number-live/gameplay/full-demo-first-player-collision-report.json`.

The immediately preceding global frames 5660–5687 add an exact eight-page
rightward Muncher approach under the same Reggie, Bashful, safe-zone, and
warning composition. Native matches the starting dwell, five interpolated
positions, right-facing endpoint record, and following idle callback with no
source refresh exclusions. The idle page is shared with the bottom-exit gate,
so seven pages are new and aggregate complete live Number Troggle coverage
rises from 394 to 401. See
`analysis/number-live/gameplay/full-demo-first-player-right-approach-report.json`.

The temporal bridge at global frames 5446–5659 now has a continuous exact
gate as well. Its 15 presentation-complete pages cover Reggie's source dwell,
all five downward movement callbacks, destination dwell and restoration of
the source-cell `5`, then safe-zone activation on the same tick as Demo's next
action, all five rightward Muncher callbacks, endpoint, idle, and the next
slot-owned warning while Bashful remains resident. The first and last pages
overlap the neighboring left-trail and right-approach audits, so 13 pages are
new and aggregate complete live Number Troggle coverage rises from 401 to 414.
The gate exposed and removed a native-only phase-zero page: when the safe-zone
job and selector 5 share a tick, native now retains the pre-safe-zone page
until the player's first visible movement callback. Frame 5598 already
contains the complete following idle actor but also has a 46-physical-pixel
horizontal strip belonging to neither complete neighbor; it remains
capture-only. See
`analysis/number-live/gameplay/full-demo-reggie-down-safe-entry-report.json`.

The adjacent global frames 5216–5331 now close the bridge from the later
bottom-entry gate to the third cannibal/player-walk gate. Native matches all
15 presentation-complete pages across the exact seven-record correct chew of
`220`, terminal record-13 hold, warning removal, the invisible first top-entry
state, all four visible entry callbacks, and the ordinary overlapping Reggie
endpoint before bite record 12. The opening page overlaps the bottom-entry
audit, so 14 pages are new and aggregate complete live Number Troggle coverage
rises from 414 to 428. This exposed a separate resident-page error: an entry
terminal that installs state-5 cannibalism must leave its ordinary endpoint on
screen until the first bite callback one public tick later. Native now does so.
Frames 5244 and 5314 are exact horizontal scanout splices at physical rows 340
and 236; frame 5324 is an 18-block torn entry repaint with 148 physical pixels
belonging to neither complete neighbor. All three remain capture-only. See
`analysis/number-live/gameplay/full-demo-chew-top-entry-report.json`.

The earlier global frames 4162–4308 now close the bridge immediately before
Bashful's right-edge arrival. They contain 28 logical runs across two downward
Muncher walks, the intervening upward walk, Reggie's simultaneous rightward
crossing and `92 -> 115` trail, and the safe-zone/entry handoff. Pixel
partitions classify seven runs as incomplete dirty repaints: five break the
physical 2x duplication, while the uniform terminal-to-idle and partial-safe
pages are exact mixtures of their complete neighbors. Native matches the
remaining 21 pages in exact order, taking aggregate complete live Number
Troggle coverage from 428 to 449. The gate also removes native-only hash
`0xa9b8de4b879ca671`: a safe update during the outside warning now keeps the
pre-safe resident page visible until the warning advances to Bashful's
invisible phase-1 entry. See
`analysis/number-live/gameplay/full-demo-player-reggie-safe-bridge-report.json`.

The following global frames 4340–4652 now close the continuous first-board
route through the second Reggie-on-Reggie collision approach. Fifty-nine
logical source runs span Reggie's rightward trail, four Muncher moves,
Bashful's right exit, a new right-edge Reggie entry, and the older Reggie's
approach to the occupied row-4/column-5 cell. Ten runs are pixel-proven
incomplete dirty repaints; native matches all other 49 full pages in exact
order. Static recovery corrects the former fixed-slot painter assumption:
`DS:5A84` begins as job IDs `1,2,3`, but entry, movement, and collision each
move their actor to the end before board redraw. The frames 4562–4591 boundary
also proves a presenter-only entry lag: three callbacks retain the warning,
then current Bashful exit states compose with older entrant records, repeating
the first invisible record. Native reproduces that retained dirty-cell history
without changing gameplay deadlines or PRNG calls. This raises aggregate
complete live Number Troggle coverage from 449 to 498. See
`analysis/number-live/gameplay/full-demo-pre-second-cannibal-bridge-report.json`.

The third Demo board adds a right-edge Worker entry at global frames
11840–11876. Eight complete states pin the resident `CB5DFF` Worker
highlight, phase-2/3 concurrent player-chew combinations, phases 4–6, and
dwell; two separate one-block refreshes are classified as torn. The widened
timeline proves that entry begins at visible phase 1 and that the terminal
chew/Troggle handoff briefly retains record-14 player pixels after the Worker
reaches phase 3, before record 13 closes the player. The bounded complete-frame
presentation queue now reproduces that callback framebuffer. The deterministic
replay and production presenter therefore match all eight uniform states,
including `C0942CB89DE34ACB` and `A5D7F189AEFDDFB2`, in order. See
`analysis/number-live/gameplay/full-demo-worker-entry-report.json`.

The same continuous third-board route now covers the Worker collision at
frames 11940–11992. Twenty-five logical runs contain at least one complete
uniform source framebuffer: the stationary endpoint callback, all 21 logical
bite ticks, a concurrent safe-zone activation and bottom Reggie entry, and the
stable `Aargh` feedback state. Native covers every distinct complete hash. It
now preserves the pre-bite dwell and first closed-bite resident callback, and
the final-pose/terminal callbacks abort at the biter's physical slot so the
later Reggie remains at captured phase 4 beneath feedback. Two fully torn runs
stay capture-only; one three-frame logical run includes a single one-block host
tear but also two uniform frames. The shared dispatcher now runs earlier
Troggle slots, paints state 5 at the biter's physical slot, and then runs later
slots. It queues the callback-local page before each later Reggie repaint and
retains the clipped phase-1 surface, reproducing all 25 complete source runs in
exact order. Word gates the same resident-surface and physical-slot boundary.
See
`analysis/number-live/gameplay/full-demo-worker-collision-report.json`.

The fourth Demo board adds the complete Bashful approach and collision at
frames 15899–16390. The lossless window contains 41 logical runs: four are
nonuniform refresh tears. Frame 15901 is a strict horizontal scanout splice
through the `690` label between exact labeled and eaten-cell native surfaces;
frame 15978 similarly splices the upper part of bite tick 15 with the lower
part of bite tick 16. Native gates all 35 presentation-complete full-frame
runs: three Worker entry/dwell states, four clipped Bashful entry phases, five
concurrent Reggie entry phases, the pre-bite callback, all 21 logical bite
ticks, stable `Aargh! ... Trogglus timidus.` feedback, and the following
collision-transient board. The terminal exposed a real shared gap: after the
feedback text clears, DOS returns the survivor to dwell record 15 rather than
holding bite record 12. Number and Word now make that handoff, and Word also
gates the four shared Bashful top-entry poses. See
`analysis/number-live/gameplay/full-demo-bashful-collision-report.json`.

The broader restarted-board stream now closes the same event at board scope.
Frames 15321–16390 contain 101 source logical runs; the native replay emits 86
changed pages, all 86 occur as exact source pages in order, and there is no
native-only page. The first comparison exposed two scheduler/painter defects:
the last Bashful entry callbacks must retain player record 12 without entering
the ordinary player-terminal compositor, and its new state-5 job must wait
while the earlier-slot Reggie drains phases 2–6. Native now presents the exact
`B3AF...`, `E160...`, `F8F3...`, `F0CB...`, `BA34...`, `72B4...`, and
`2815...` boundary before all 21 bite poses. Eleven physically nonuniform
refreshes and four uniform incomplete dirty-painter states remain source-only;
the companion pixel report pins the three exact neighbor partitions and the
fourth state’s 46-pixel clipped strip. The collision consumes call 604, the
Reggie terminal dwell consumes 605, and the Bashful terminal selects exact
call-607 `Aargh`; the terminal board and following Hall hashes also match. See
`analysis/number-live/gameplay/full-demo-fourth-board-continuous-sequence-report.json`
and
`analysis/number-live/gameplay/full-demo-fourth-board-reconciliation-report.json`.

The following captured Hall/logo restart reaches a stable Factors-of-63 board
at global frame 18570. Setup consumes calls through 695 and exactly matches the
captured mode, target 63, visible level 7, difficulty tier 6, player cell
`(1,1)`, sixteen remaining answers, all thirty labels, and full-frame FNV-64
`0x320cd1496e8cf4b3`. The continuous audit spans all 1,958 source frames through
the final `Troggle!` board at frame 20527. Native emits 224 changed pages; 218
are exact ordered DOS pages. Four of the six LCS-only native pages are exact
DOS pages repeated elsewhere in the same board, while two are complete
callback surfaces that partition adjacent dirty-painter captures. The 27
source-only runs resolve to sixteen nonuniform scanouts, seven exact neighbor
partitions, one repeated exact native page, and three pinned incomplete painter
records. The audit exposed and corrected four production boundaries: a first-
batch safe/warning hold leaking into this board, two-Helper/downward-player
resident ordering, the Helper-trail/leftward-player callback pair, and the
whole-surface delay required while a bottom Helper exits under an upward
Muncher move. No unexplained native page remains. See
`analysis/number-live/gameplay/full-demo-fifth-board-continuous-sequence-report.json`
and `analysis/number-live/gameplay/full-demo-fifth-board-reconciliation-report.json`.

The recording ends with an external key on global frame 20528. That one DOS
frame contains 347 nonuniform doubled blocks while the single-buffer painter
switches from the Factors board to the title; frames 20529–22003 are one stable
title run at FNV-64 `0xedf7e5d8c59eeac7`. Native injects the same event at
relative presentation frame 1958 and PRNG call 767, clears Demo state, emits
only the completed exact title, and holds it for the remaining measured
21.0598 seconds without spending another random value or re-entering Demo. See
`analysis/number-live/gameplay/full-demo-fifth-board-exit-report.json`.

The same first Demo board contains a complete Reggie-on-Reggie cannibal
interval at global frames 2941–2996. Twenty-three uniform states cover the
last overlap frame, all 21 public-tick bite poses, and the terminal repaint;
three incomplete 2x refreshes are preserved separately. The bite sequence
already matched, but the terminal exposed a distinct actor-state field in the
original: `0x09DA5` passes literal direction 2 to `0x09B16`, painting dwell
record 15 while leaving the survivor's rightward movement heading intact.
Both native runtimes now store the painted dwell record separately from the
next steering direction. Number's deterministic Demo matches terminal
full-frame FNV-64 `9C2F96CD8E6918C2`, and Word independently gates the same
record/heading split. See
`analysis/number-live/gameplay/full-demo-cannibal-report.json`.

The same continuous first-board replay reaches a second Reggie-on-Reggie bite
at row 4, column 5. Global frames 4653–4707 expose all 21 complete bite
callbacks plus the terminal record-15 surface. Native matches those 22 stable
states exactly; four uniform incomplete dirty paints and one nine-block torn
refresh remain capture-only. The last two bite callbacks overlap unrelated
player work, which exposed the physical scheduler boundary: the player job
runs before the Troggle slots, so its callback-local page can be presented
before the later biter repaint. Number now gates the complete seeded interval,
and Word independently gates that shared callback queue. Aggregate complete
live Number Troggle coverage therefore rises from 197 to 219 at this boundary. See
`analysis/number-live/gameplay/full-demo-second-cannibal-report.json`.

A third bite at row 0, column 2 occurs during a two-leg upward Muncher walk.
Frames 5332–5385 contain 30 uniform logical runs with no torn 2x blocks, but
uniform scaling is not sufficient to prove a completed actor callback. Exact
player/biter pixel partitions identify runs 4, 10, and 24 as one-frame dirty
paint splits, leaving 27 presentation-complete pages. The seeded native replay
now presents exactly those 27 pages in source order, including the retained
selector-5 phase-0 page, the player-idle callback, earlier and later dirty-cell
pages around biter callbacks, the penultimate interrupted player paint, the
terminal player's retained leading movement strip, terminal bite, and two
following player callbacks. It emits no extra resident page and does not
reproduce the three incomplete source paints. This closes the former five-page
gap and raises aggregate complete live Number Troggle coverage from 219 to 246.
The same retained board-cell/page scheduler is installed and independently
headless-gated in Word. See
`analysis/number-live/gameplay/full-demo-third-cannibal-player-walk-report.json`.

Mining another first-board interval at global frames 4309–4339 adds a complete
Bashful right-edge arrival. Six phase-2-through-dwell pages match the seeded
native presenter at exact 320×200 hashes, raising aggregate complete live
Number Troggle coverage from 246 to 252. The seventh source run lasts one
capture frame and differs from the completed phase-2 page at only four pixels,
the x=308/y=112..115 rule segment; the following actor paint immediately
overwrites that restoration. It is therefore a uniform but incomplete
erase-between-poses repaint and remains capture-only rather than reintroducing
flicker. See
`analysis/number-live/gameplay/full-demo-bashful-right-entry-report.json`.

Global frames 5386–5445 close the same Bashful's later ordinary trail. The
opening page remains unchanged for 39 frames after the third cannibal/player-
walk terminal, closing every formerly ungated frame 5386–5419. Bashful then
regenerates bottom-row `23` as `26`, traverses all six
leftward phases, and reaches its new dwell. Native matches all eight completed
pages in exact order. The overlap means seven are new, raising aggregate
complete live Number Troggle coverage from 252 to 259. Frame 5439 is uniform
at 2× but not callback-complete: it contains the terminal pixels plus a
13-pixel one-row strip belonging to neither completed neighbor. It remains
capture-only rather than native flicker. See
`analysis/number-live/gameplay/full-demo-later-bashful-left-trail-report.json`.

An exhaustive logical-frame inventory now scans all 22,004 frames of the same
lossless Demo rather than selecting events by eye. It finds exactly five
gameplay-board intervals: Reggie+Bashful, no visible species, Reggie+Worker,
Reggie+Bashful+Worker, and Helper. Smarty contributes zero gameplay-board
pixels in all five intervals, so the supplied recording cannot close Smarty
live parity. Both formerly uncatalogued first-board Bashful edge windows are
now closed by isolated full-frame gates using their directly captured
concurrent Muncher states. The bottom-exit interval additionally proves its
26-block frame to be an exact row-90 scanout splice of adjacent completed
pages. See
`analysis/number-live/gameplay/full-demo-troggle-inventory-report.json`.

Another first-board interval, global frames 3073–3092, provides the first complete
live species-trail/exit oracle. Bashful replaces bottom-left `80` with `150`
and moves left off the board. Nine uniform 320×200 states match native in
source order: two preceding player poses, a one-refresh phase-1 Bashful drawn
over the retained earlier player framebuffer, the complete phase-1 repaint,
phases 2–5, and the terminal removal where `150` first becomes visible. The
single frame 3082 contains 12 nonuniform 2× blocks and remains classified as
a torn DOSBox-X refresh. This interval exposed three coupled native defects:
ordinary movement had a synthetic visible phase 0, exiting actor pixels leaked
into the left margin, and the regenerated label reappeared beneath the moving
actor. Both runtimes now begin state-3 movement at phase 1, clip exiting art
and its swept blue dirty rectangle to the board viewport, retain that rectangle
until terminal removal, and queue the callback-local retained-player frame
when a new Troggle move starts on the same public tick. Word's separate
headless gate exercises the same Bashful geometry, label suppression, terminal
repaint, and two-refresh presentation boundary. See
`analysis/number-live/gameplay/full-demo-bashful-trail-exit-report.json`.

Global frames 3179–3208 close the next trail without a scene or board change.
The Muncher first moves upward; its logical selector-5 setup phase 0 is never
presented, while phases 1–4, the directional terminal, and the following
standing repaint are complete source framebuffers. That standing repaint is
visible before the later Reggie job replaces row-2/column-0 `207` with `65`
and starts moving right. Native now holds selector-5's pre-move framebuffer
until the player callback reaches phase 1 and queues the complete standing
player callback when a later Troggle begins on the same public tick. All six
Reggie positions and dwell then match. Thirteen complete states are exact in
order. Frame 3195 is a uniform-2× but incomplete dirty repaint: it equals the
complete phase-2 position except for a retained 28×7 top actor band. Frames
3188 and 3207 contain 22 and one nonuniform 2× blocks. All three are preserved
as presenter evidence but intentionally not synthesized as native flicker.
Word gates both shared callback boundaries headlessly. See
`analysis/number-live/gameplay/full-demo-reggie-trail-report.json`.

The same corrected movement clock then matches Bashful's next ordinary trail
without further native changes. Global frames 3740–3815 contain a 54-frame
dwell, exact bottom-left `150 -> 220` regeneration, all six rightward Bashful
positions, and dwell. Eight complete states match the seeded replay; frame
3796 has 19 nonuniform 2× blocks and remains capture-only. This independently
confirms that the first trail/exit correction was not a one-event special case.
See `analysis/number-live/gameplay/full-demo-second-bashful-trail-report.json`.

The uninterrupted frames 3816–3870 then generalize both axes again. A later
Reggie crosses row 2 from column 3 to column 4 through all six phases and
dwell; immediately afterward, the top-right Bashful exits upward through all
five vertical phases before the payload reappears at terminal removal. All 14
uniform source framebuffers occur in exact order in the seeded native replay.
Frames 3818 and 3830 contain 23 and 20 nonuniform 2× blocks; native keeps the
corresponding callback-local composites complete and does not reproduce those
partial refreshes. A separate Word fixture locks the shared top viewport,
header isolation, swept-label suppression, and terminal repaint. See
`analysis/number-live/gameplay/full-demo-later-reggie-top-exit-report.json`.

Global frames 3977–4014 then provide a clean first-board answer to the original
missing-eating-animation concern. There are exactly eight uniform logical
framebuffer runs and no partial refreshes: seven callback intervals paint
records `12,13,14,13,12,13,14`, followed by the terminal record-13 hold. The
two alternating visible poses differ across 240 pixels in the row-2/column-4
Muncher box. Native's seeded presenter matches every full-frame hash in order,
independently of the static dispatcher proof and the later Prime-board chew.
See `analysis/number-live/gameplay/full-demo-first-board-chew-report.json`.

Global frames 4090–4161 then exercise that chew while a later Troggle job is
due on the same scheduler ticks. The Muncher chews row 2/column 3 value `100`
while Reggie changes row 2/column 5 from `232` to `3` and exits right. Nine
complete source states match native exactly: the pre-action framebuffer, all
seven combined chew/exit intervals, actor removal, and the terminal record-13
hold. Frame 4109 is a uniform-2× partial dirty refresh rather than a third
logical pose: all 762 differing pixels resolve exclusively to either the
pre-action or complete phase-1 framebuffer, with zero pixels from any other
state. Frame 4121 contains one nonuniform 2× block. Native intentionally
presents neither incomplete update. Number's seeded full-frame gate and a
paired Word scheduler fixture require the complete combined callback order
without a synthetic enemy-only or chew-only frame. See
`analysis/number-live/gameplay/full-demo-chew-right-exit-report.json`.

Global frames 19420–19480 on the later Factors-of-63 Demo board provide an
isolated simultaneous Helper-entry oracle before the already audited trail
window. Thirteen presentation-complete framebuffers match native exactly from
the warning through a rightward Muncher walk, every presented phase of a
left-edge and right-edge Helper arrival, both terminal poses, and dwell. Two
one-frame runs contain 11 and 22 nonuniform doubled blocks. A third uniform
one-frame run is also incomplete: it has the complete retained-player/left-
Helper surface but clears only the warning outline's bottom 26 pixels. Copying
those pixels from the preceding completed warning reconstructs native's exact
`0x321eddc11c6bca6e` completed surface. All three partial refreshes therefore
remain capture-only. The thirteen unique matches raise aggregate complete live
Number Troggle coverage from 259 to 272. See
`analysis/number-live/gameplay/full-demo-dual-helper-entry-report.json`.

The later Factors-of-63 Demo board provides an isolated live Helper oracle even
though it occurs after the withdrawn divergent replay route. Global frames
19530–19590 are all uniform 2× output and form 15 logical runs. Native now
matches thirteen selected complete framebuffers exactly across a bottom-left Helper
moving right, a right-edge Helper moving down, both destructive `3` trails,
and the overlapping downward Muncher movement. Runs 6, 8, and 9 are exact
resident-surface player callbacks whose compositor is routed by both production
runtimes; Word gates the same thirteen logical state tuples and shared actor
geometry. Run 0 is preceding context. Run 7 is one additional complete
uniform-2× output, but pixel provenance proves it is a horizontal scanout tear:
rows 0–80 come exactly from matched run 6 and rows 81–199 exactly from matched
run 8. It is therefore capture-only rather than a callback state. The isolated
report's `native_replay.complete` is true for all complete framebuffers, while
it makes no continuous replay claim. See
`analysis/number-live/gameplay/full-demo-dual-helper-report.json`.

The same jobs continue through global frames 19690–19886, producing thirty
logical runs. Native matches all 27 presentation-complete pages in order: the
bottom Helper clears `8` and moves right, the Muncher walks left and performs
the exact seven-record chew on `63`, the right Helper clears `12` and moves
down, and the first Helper clears `30` while exiting through the bottom edge.
The sequence includes both overlapping Helper callbacks throughout the chew
and exit. Runs 3 and 21 contain 15 and 20 nonuniform doubled blocks. Uniform
run 10 already contains all 333 pixels of the completed next player phase but
also retains a 15-pixel strip belonging to neither completed neighbor; it too
is capture-only. The 27 new matches raise aggregate complete live Number
Troggle coverage from 272 to 299. See
`analysis/number-live/gameplay/full-demo-later-dual-helper-report.json`.

Global frames 19887–20062 bridge that interval to the already audited final
Helper exit. Thirty presentation-complete pages now match native in exact
order across the Muncher's left step, complete chew of row 2/column 2, the
first safe-zone expiry on the retained phase-zero page, downward step, and
complete chew of row 3/column 2 while the remaining Helper dwells at row 4/
column 5. Both chews use the exact `12,13,14,13,12,13,14` record sequence and
terminal record 13. Frames 19961, 19973, and 19997 are nonuniform actor
refreshes. Frames 20058–20061 are uniform copies of the final terminal page;
frame 20062 keeps that top-left logical page but updates only the lower half of
five doubled pixels on the second safe-zone outline, so it too is capture-only.
This raises aggregate complete live Number Troggle coverage from 299 to 329.
See `analysis/number-live/gameplay/full-demo-post-helper-player-report.json`.

The same later board supplies a second complete Helper oracle at global frames
20063–20187. All 125 decoded frames are uniform 2× output and collapse to
twelve logical runs. Native matches every full-frame hash across the row-4/
column-5 dwell, all five clipped downward exit phases, destructive `7` trail,
terminal removal, and a simultaneous upward Muncher move through phases 1–4,
its endpoint record, and the following standing callback. Word gates the same
twelve tuples, bottom viewport, destructive trail, and below-board clipping.
This remains isolated post-divergence frame/callback evidence rather than a
continuous route claim. See
`analysis/number-live/gameplay/full-demo-helper-bottom-exit-report.json`.

Global frames 20188–20527 contain the supplied capture's final gameplay-board
tail. Twenty-nine presentation-complete pages match native in source order
across left/down/left player movement, the exact seven-record chew of correct
`21`, its terminal record-13 hold, and the next `Troggle!` warning before an
actor becomes visible. Frame 20192 contains the complete next movement phase
plus four invalidated border pixels and one nonuniform doubled block. Frame
20288 contains the complete 39-pixel idle delta plus 16 pixels belonging to
neither terminal nor idle. Frame 20389 horizontally splices 135 pixels of chew
record 12 with 105 pixels of record 13. Those three incomplete paints remain
capture-only. This closes every complete source page through the board run's
last frame and raises aggregate complete live Number Troggle coverage from 329
to 358. See
`analysis/number-live/gameplay/full-demo-final-board-tail-report.json`.

The byte-isomorphic Word collision/Demo scheduler now also preserves Number's
terminal-tick boundary. When the selector-5 record and tick-21 bite terminal
meet, the biter terminates first; the Demo callback performs only its reload
draw after the Muncher is removed. A headless fractional-frame case locks both
that three-draw terminal order and immediate transfer of the unconsumed
quarter tick into the new 150-tick feedback hold. The former Word path could
consume an extra direction draw and lengthen the hold by the remainder of the
current Win32 update.

The next shared-engine audit recovered a second Boolean terminal boundary.
After either a state-4 movement trail or safe-zone displacement trail, the
original 30-cell board-live predicate performs completion and returns false;
both callers then return immediately. Native had inferred continuity from the
visible page, which remains Playing across an ordinary direct advance, so it
could continue through an invalidated old actor reference, consume an extra
steering draw, or append streams 11 and 4 after stream 15. Number and Word now
propagate the predicate result explicitly, with headless last-positive Helper
tests for both callback origins and exact post-generation PRNG/audio tails.
Those tests enter through the public tick loop and also exposed a second-order
error: debiting the dispatched tick after synchronous board construction
changed the new board's accumulator reset from zero to `-1`. Both runtimes now
debit before invoking callbacks, eliminating that extra-tick startup delay.
The false result now reaches the enclosing board-job pass as well, so the
destroying tick cannot age a freshly installed Troggle job. Both regressions
compare every new level-2 safe/enemy timer with a direct-completion baseline.

The adjacent player-chew audit closes the same handoff for the seventh chew
callback. Number `0x093F3` and Word `0x09FC0` call the board-live predicate
from the final correct-answer path, which posts selector-100 action 6. Native
now detects the synchronous board replacement and transfers only the
post-terminal fraction of a spanning host update. A 2.25-tick regression with
the terminal one tick away exactly matches direct completion plus a 1.25-tick
tail, including PRNG state, all new job timers, and a 0.25-tick accumulator.

The fixed scheduler arrays then close the enclosing actor-order boundary.
First-free-slot insertion places safe-zone jobs before player ID 4, Troggles
after the player, and selector-5 Demo last in the public mask-2 pass. Both
native hosts now split a coalesced Windows update into original public ticks
and dispatch `safe -> player -> Troggle -> Demo` on each one. Regression cases
lock a protected chew against a same-frame endpoint, safe tick seven versus
Troggle tick six on wrong-answer termination, queued movement-to-chew creation,
and the later-slot Demo reload after a movement-terminal collision. That last
scan tail is required to retain the captured Number second-board initializer at
PRNG call 271 while collision age remains zero. The paired record templates and
scanner addresses are recorded in `GAMEPLAY-SCHEDULER-STATIC-AUDIT.md`.

The recovered `0xF585` seed and scheduler match the reference-backed first three boards through call-507 `Aargh` and the externally interrupted third Hall. The fresh Multiples-of-15 initializer independently matches calls 507-584. After replaying the captured external title restart, the corrected standing-only route matches the fourth-board collision at calls 604/605/607, its Hall/logo lifecycle, the call-695 Factors-of-63 initializer, and all presentation-complete gameplay pages through global frame 20527. Native continuation beyond the supplied capture remains deterministic through call 1677 but is not substituted for live evidence.

The common gameplay keyboard dispatchers additionally recover 8/A/I-up,
4/J-left, 6/K-right, and 2/M/Z-down in both games, with Space as the separate
chew key. Headless tests lock all sixteen digit/uppercase/lowercase forms,
destination, direction
identity, and the absence of letter-triggered chewing; see
`KEYBOARD-INPUT-STATIC-AUDIT.md`.
The same tables expose a formerly omitted joystick-repeat control: the default
word is 4, `+`/`=` lowers it toward 1, and `-`/`_` raises it toward 10 whenever
either joystick flag is set. The recovered main-pump call sites prove this
table is resident only during Demo and selector states 4-6; synchronous menus,
waiters, and editors own private loops and never execute it. Both runtimes now
use that word for held directions, and headless gates cover both flag states,
bounds, one/ten-tick deadlines, every modal/live page, cheat-overlay isolation,
and Demo's ordered adjustment before the same key exits.
The adjacent selector-100 trace also corrects the level-complete input gate in
both games. State 6 forwards ordinary keys to immediate cartoon teardown but
the common Escape branch applies only to states 4/5. Native therefore ignores
Escape/modifiers during cartoons, accepts forwarded keys as skips, stops the
scene effect, and consumes printable Win32 input once so it cannot move the
new-board player. Both headless runtimes lock those transitions.
The state-4/5 Enter branch is equally high-level and never consults the player
actor job. Native now permits Time out during a walk, chew, or recovery and
freezes that operation until resume in both games; Word formerly advanced the
hidden actor behind its pause overlay, while Number rejected Enter during
walking/recovery.
The shell also preserves the original Alt-shortcut fallthrough within that
same resident scope: Alt+M/P rejoin the high-level dispatcher after toggling,
while Alt+S returns immediately. Modal pages and the collection launcher no
longer expose those audio shortcuts; startup event waits receive one inert
non-character acknowledgment instead. The version wait also retains its
independent literal 300-tick timeout. The window now delegates this whole
decision to a shared app-level route. Headless integration locks Number and
Word Demo behavior, both cartoon runtimes, launcher/title/cheat suppression,
one-gate startup acknowledgment, and Alt+M's narrower state-2-only score
resume (with explicit state-3 Hall and state-1 logo/splash non-resume) rather
than relying on static `WndProc` inspection alone. Failed settings writes keep
one more original ordering edge: the write alert consumes a later key before
M/P resumes its pending Demo-exit/cartoon-skip fallthrough, while S dismisses
without such a continuation. Device-free tests lock that alert sequencing in
both runtimes.
Right-button press is now a distinct `0xFE` event for active play, Demo, and
cartoons; only unconsumed fixed-widget clicks retain right-release activation.
Accepted-key waiters instead map left release to Space and reject right
release, with Number/Word Information, Hall, feedback, calibration, and the
proven Word help/Preview callers locked headlessly.
Physical press/release pairing is owned by the persistent `MunchersApp`, not a
shorter-lived controller: consumed left and right presses therefore suppress
exactly one matching release even when the 15 ms game exit has already restored
the launcher. Cross-mode Number/left and Word/right regressions lock the bridge.

The paired main-controller cleanup functions are now recovered as a third
resident-delay boundary. Number `0x1243E` and Word `0x11BC4` both begin with a
literal 15 ms wait and are called by controller actions 3 and 6. Action 3's
wait is contained inside the already measured 39-frame Demo board hold, so the
native capture-matched aggregate is not lengthened. Action 6 now retains each
title, owns all input, and delays its game-level quit flag for exactly 15 ms
before the shared shell returns to the launcher. A mouse press consumed inside
that boundary cannot leak its later release into the launcher. The native launcher's final
Exit remains outside that DOS-controller contract. See
`CONTROLLER-CLEANUP-DELAY-STATIC-AUDIT.md`.

The paired Troggle endpoint loops also had a dormant native-only terminal.
Number `0x09170 -> 0x09096` and Word `0x09D3D -> 0x09C63` retry protected
directions without a counter, whereas native stopped after 32 turns. Both
runtimes now retain the original unbounded branch. An exhaustive low-17-bit
Borland-state gate proves that the valid two-safe-zone invariant terminates
within 24 draws; a separate 37-draw fixture makes the structural correction
observable. See `TROGGLE-SAFE-RETRY-STATIC-AUDIT.md`.

The production timer bridge no longer discards every portion of a delayed
`WM_TIMER` interval above 100 ms. Win32 and both controllers now retain the
full nonnegative steady-clock delta; `MunchersApp` visits it in ordered
100 ms-or-smaller slices. A 29.8-second Word title fixture proves that a
coalesced 350 ms interval and 100+100+100+50 ms enter Demo identically. See
`HOST-ELAPSED-TIME-STATIC-AUDIT.md`.

Static/capture evidence separately locks the attract controller and Wipes, Hall/name-entry/replay routing, scoring and reserve boundaries, password transactions, menu/pointer widgets, Set Content, fixed UI frames, joystick transaction, and the exact Troggle/safe-zone rules documented by their focused audits. The accepted-key audit now also corrects the former any-event assumptions for both rejected-password and initially-empty-Hall overlays, removes native-only arrow paging from Number Set Content Help, and gates the left-release/right-release split on Number validation, Word vowel validation, and Word's formerly silent insufficient-target Set Content failures. The Word route now retains the original changed byte, bypasses rebuild after an untouched visit, displays the exact six-line warning for either a changed zero-target draft or zero-target Preview request, and returns to the same invalid editor loop after an accepted dismissal. The complete pAccept inventory exposed and removed an immediate-commit shortcut in Multiples Other. The follow-up numeric-editor trace additionally corrects early lower-limit mutation, the range-order retry stage, empty-Enter behavior, per-row digit caps, and the executable's literal `3..50` Multiples-Other bounds. Its state-1 Escape returns to cleared number entry; Enter/right release commits and left release remains inert. Both games now also preserve the original password-first-field versus hint-cancel save split and the keyboard-only failed-configuration-write alert. Number routes both its native settings and separately stored Hall failures through that alert; see `NUMBER-PERSISTENCE-STATIC-AUDIT.md`. Those gates are not substituted for the remaining live/physical comparisons. See `CONTENT-NUMERIC-EDITOR-STATIC-AUDIT.md` and `WORD-OPTIONS-STATIC-AUDIT.md`.

The Word vowel-editor follow-up expands its claimed three-column navigation
graph to all 140 arrow/Home/End/digit edges. Static instructions at `0x14579` prove the
Group-3 Left clamp is `min(current-3,9)`; native's former `max` sent four rows
to the wrong checkbox. The exact `DS:2998` returned-key table proves ASCII
`4`/`6` are custom-switch aliases for extended Left/Right, while the generic
widget consumes ASCII `2`/`8` through its Down/Up branches because flag
`0x0080` is clear. The same table leaves Home/End inside the generic widget's
first/last-item branches. All six formerly discarded routes are restored, and
the corrected graph is now exhaustively headless-gated.

The following shared-input pass replaced a narrower and incorrect recovery
model. `pSeqKey` normalizes all sixteen digit/case movement aliases to
`I/J/K/M`, then stores them with Space and negated `pSeqmouse` cell targets in
one ten-byte circular ring with nine usable entries. Busy movement, chew, and
state-6 recovery defer consumption instead of discarding new bytes. Movement
may consume its successor in its terminal callback; chew waits until the next
public player callback. Collision setup clears the pre-collision ring, while
new input accepted after feedback waits for the survivor to leave. The common
Enter branch clears the complete ring on both sides of `Time out`. Muted live
Number captures prove `JJ`, `Space+J`, and the pause reset; both games now share
the recovered FIFO and headless-test order, capacity, recovery, terminal timing,
and mixed keyboard/pointer reset. See `GAMEPLAY-INPUT-RING-STATIC-AUDIT.md`.

The SCPT audit closes native state-machine shortcuts that are materially visible in the original: movement uses the recovered signed 16.16 accumulator and rounding, records are opaque, and a retained framebuffer receives construction-order dirty paint events. Opcode `0E` detaches an actor without erasing pixels immediately; a later dirty rectangle clears detached art and repaints only active actors, while opcode `FF` removes its actor in the same callback. Number scene 4 adds the measured same-position frame-shrink rule: cleanup uses the new record bounds, so the old frame's lower tail remains until a later taller record covers it. The exact state-6 keyboard gate and terminal ticks remain headless regression gates. The real presenters hash all 980 host-visible Number frames and all 838 internal Word frames across their eleven recovered cartoons. Number now has complete live stable-framebuffer evidence for all five cartoons: all 568 distinct DOS-presented RGB states and all 721 ordered runs match exactly, including scene 3's complete 165-run terminal-loop tail. For Word, six muted lossless captures establish the independent 10 Hz clock and prove exact matches for all 586 DOS-presented distinct stable framebuffer runs; the one additional signal-99 internal run per scene is regression-tested but excluded because DOS tears down before presenting it. `PROGRESSION-STATIC-AUDIT.md` additionally corrects the former fixed cartoon cycle: the original uses five independent full-range swaps whenever its cartoon cursor is zero, selects that shuffled slot every third completed board, advances the cursor only at cartoon teardown, and increments the visible level only when the next board is initialized. Native now preserves that PRNG/cursor/order behavior in headless tests. Reconstructing the unobscured board behind the preserved typed-name framebuffer also upgrades the former modal-region check to a complete 320×200 match (`0xbab9ce334800c231`) and corrects the score value's one-pixel x placement.

The Hall/name-entry gate now continues one frame further than the modal: the
captured admitted 25-character Inequality row and score 85 match the complete
native frame at FNV-64 `0xeb488a361b66de0a`. Static recovery establishes the
first exact `(name,score)` lookup, the one-million `Perfect` label, transparent
normal rows, opaque white-on-black VGA selection, and opaque
white-on-magenta CGA selection. Native retains that pair across repaints and
consumes it when the Hall is left; both games headless-test the same lifecycle.

## Native architecture

The port is a source-level reimplementation, not an emulator wrapper. The Win32 shell owns timing, input, DPI/fullscreen transitions, and painting. A page/game state machine owns board generation, scoring, enemy updates, and menus. The renderer draws into a fixed 320×200 32-bit framebuffer and letterboxes it at the largest integer scale. Original resources are embedded in `.rsrc` and decoded by bounds-checked native readers.

The release does not contain `NM.EXE`, the unpacked executable, DOSBox, or any external helper. It imports only Windows system DLLs/API-set contracts; libgcc and libstdc++ are statically linked.

## Current substitutions and platform changes

Legacy installer/network-license checks and DOS memory/driver probing are not executed. Mouse input is handled directly by Win32. Original speaker/AdLib and cutscene-script payloads are fully extracted and preserved. The five lossless live-play DRO traces remain embedded decoder/reference fixtures, but several span event replacements and are not replayed as event-local cues. Runtime instead decodes every short GSND entry 0–15 from embedded `NM.RES` and submits streams 3–15 at the original call sites; normal board construction is silent, while completion stream 15 is an ordinary Sound/device-routed event. The recovered scheduler, 12-semitone F-number table, 11-byte patch layout, jumps/loops, rests, gate timing, and channel stops are tested against captured register events. Static recovery also decodes entry 16 as a channel-9 conductor that launches eight `0xB4`-rate parts on OPL channels 1–8 for one exact 7,680-scheduler-tick/105,431 ms cycle. Event `0x90` and the original demo flag prove entry 16's attract-only start/resume/stop lifecycle. The separate post-Wipe event `0xA6` selects stream 38 from each active cartoon bank; native decodes all eleven conductors, installs their non-repeating channels-1–8 scores without resetting channel 0, and retires them at scene teardown. Native clocks those scores and replaceable channel-0 effects through one YM3812 stream, and uses the same continuous-chip lifetime for ordinary GSND/ADLI cues instead of resetting the emulator for each event; the preserved demo PCM directly validates its tempo/rest/loop envelope. The PC-speaker branch uses the exact 128-word driver table and integer PIT divisor/gate behavior. Static recovery now corrects the former “effects-only” Alt+P interpretation: the current device byte gates score dispatch, AdLib-to-speaker stops all channels when music is enabled, and only state-2 speaker-to-AdLib restoration restarts the score. Alt+S/M submit audible GSND 1/2 acknowledgements on both devices, with the original 50 ms delay after music-off's all-channel stop. All eleven SCPT cartoons execute through the bounded native scene VM, and their opcode-`0D` events receive the original scene-bank `+3` stream bias before dispatch through ADLI or common PSND 11. Word's completed Wipe states/cadence and scene pixels are live-closed, including 586/586 original-presented distinct stable scene framebuffer runs on the 10 Hz clock. Number's five Wipes and every distinct stable scene RGB state are also live-closed, including the complete longer scene-3 tail. The uninterrupted Word attract route and Number continuation close the former organic-board and timing boundaries. Timestamped digital audio schedules/PCM are the executable parity gate; physical output filtering, gain, transients, room acoustics, and device mechanics remain optional machine QA under `PLATFORM-BOUNDARY-AUDIT.md`. The supplied demo's time restriction is not reproduced. See `GAMEPLAY-SOUND-ROUTING-STATIC-AUDIT.md`, `CARTOON-SCORE-STATIC-AUDIT.md`, `WORD-SCENE-STATIC-AUDIT.md`, `OPL-LIFETIME-STATIC-AUDIT.md`, and `SOUND-TOGGLE-STATIC-AUDIT.md`.

A later exhaustive direct-caller audit adds three lifecycle boundaries to both
paragraphs above. State-4/5 Escape submits resident GSND 0 before the quit
prompt and cancel does not restart the retired cue. Every-third-board
completion still submits stream 15 exactly once, but the selected-scene loader
then cuts it off with GSND 0 and blocks for a literal 15 ms before loading the
scene; that delay argument is not another stream submission. Scene teardown
submits GSND 0 in its inner helper, waits 15 ms, and submits it again in the
outer caller. The native owner and headless gates now preserve all three
boundaries without resetting or closing the live YM3812.

## Verification record

`tools/native_smoke_test.ps1` checks the development executable as a black box. The latest run verified:

- pixel-exact startup rendering, early acknowledgments, the 300-tick version timeout, and the separately gated splash;
- Ctrl+Alt+F1 visibly changes the framebuffer to the cheat overlay and its selector starts a level-6 board;
- Alt+Enter changes the window from `(40,40)-(1018,687)` to `(0,0)-(2560,1600)` and restores the exact original bounds/style;
- navigation reaches distinct live boards for all six game modes;
- the Set Content base page, two-page help, Range editor, Multiples Other editor, and operation editor are reachable in the packaged EXE and a committed edit remains usable;
- the per-list Hall eraser is reachable and rolls back with Escape, while Erase ALL defaults to No without mutating user score data;
- Space produces a visible player chew frame;
- Enter exposes the original-style vertical `Time out` marker, pause/resume completes with the process responsive, and Escape exposes the board-preserving quit confirmation.

`game_render_state_test` additionally checks full-frame hashes for the version, splash, initial menu, first attract board, instruction decision, initial and Multiples-disabled game selectors, Hall selector, supplied Multiples Hall page, a deterministic ten-entry Inequality Hall fixture, the post-game replay question, all six Information pages, and eighteen Options-family frames, plus the exact captured region for the qualifying-score name-entry modal. `tools/make_hall_fixture.ps1` reproducibly builds that full-list DOS configuration from the preserved pre-probe copy. The suite exercises Set Content commit/cancel, atomic numeric range and multiplier entry, per-field digit caps, empty and invalid retries, literal multiplier bounds, ordered targets, operation filtering, all four captured validation failures, and filtered-selector mapping. It also covers Hall erase-entry deletion/rollback/commit, Hall tie/cutoff semantics, password challenge/edit transactions, 25-character name submission, and empty-name fallback without touching real persisted scores. The gameplay checks cover the eight measured chew phases, fixed idle pose, native sprite decoding, four-direction movement, exact board/HUD geometry, deferred reserve loss through the bite sequence, Troggle collision/warning/timing, post-life recovery, exact 45/50 Hall-floor routing, direct-to-Hall voluntary quit routing, and the exact vertical `Time out` marker. Audio checks validate the five embedded DRO files as preservation traces, decode all 16 short GSND entries, compare recovered patch/note writes to measured timestamps, and require the 105-second conductor plus all eight long-score channel parts to decode. Integrated Number and Word gates cover silent board construction, every ordinary stream 3–15 call site, Sound/Music/device classification, collision `12 -> 10 -> 11` ordering and Demo/final-life exceptions, both Hall-to-logo `12 -> 13` pairs, PC-speaker completion, persistent ordinary cue history, and attract score/channel-0 composition. Toggle tests compare Alt+S/M against decoded GSND 1/2 on AdLib and speaker paths, gate the 50 ms music-off delay, and run Alt+M/P through all four Demo selector phases in both games. Speaker checks enforce the four measured GSND 7 frequency/timestamp writes, all 38 common PSND streams, Alt+P's asymmetric score transitions, and settings persistence. Scene checks execute all five SCPT resources through their exact thread lists, enforce valid frames and signal-99 termination, verify continuation sheets, add the original `+3` bank bias, decode all 43 unique callbacks through both ADLI and PSND (including repeated event values across banks), and prove the integrated first cartoon changes over time. The final release in `dist` has now been repackaged from the verified build and retested away from the repository asset tree; its embedded resources and system-DLL-only import boundary pass independently.
