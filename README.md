# Number Munchers — native Windows port

This repository contains native Win32/C++20 ports reconstructed from the supplied Number Munchers, Word Munchers, and Super Munchers media. The executable embeds all three games' required original resources and fonts; it neither launches nor reads any DOS executable at runtime. The independent [`analysis/PARITY.md`](analysis/PARITY.md), [`analysis/WORD-PARITY.md`](analysis/WORD-PARITY.md), and [`analysis/SUPER-PARITY.md`](analysis/SUPER-PARITY.md) ledgers are complete at 62/62 verified rows under the reproducible application/platform boundary defined in [`analysis/PLATFORM-BOUNDARY-AUDIT.md`](analysis/PLATFORM-BOUNDARY-AUDIT.md).

The executable now starts with a self-contained three-game launcher. A confirmed
in-game quit follows that game's original Hall/title lifecycle, and a terminal
Quit from any game's own main menu returns to the launcher; only the
launcher's Exit closes the application. Number and Word exits retain their original
controller's literal 15 ms blocking cleanup interval before that return; the
native launcher's own Exit remains immediate. A mouse press consumed during
that interval also carries its one-release suppression across the return, so
it cannot accidentally activate the launcher. These complete wrapper routes are
headless-gated and documented in
[`analysis/SHARED-LAUNCHER-STATIC-AUDIT.md`](analysis/SHARED-LAUNCHER-STATIC-AUDIT.md)
and [`analysis/CONTROLLER-CLEANUP-DELAY-STATIC-AUDIT.md`](analysis/CONTROLLER-CLEANUP-DELAY-STATIC-AUDIT.md).
Delayed Win32 timer messages also retain their complete steady-clock interval
instead of dropping time above 100 ms; the shared owner applies that interval
in ordered bounded slices as documented in
[`analysis/HOST-ELAPSED-TIME-STATIC-AUDIT.md`](analysis/HOST-ELAPSED-TIME-STATIC-AUDIT.md).
Word Munchers is fully integrated and parity-gated. Its supplied VHD and all
MECC payloads are preserved, all 225
VGA and 225 CGA frame records are decoded, and the original 2,203-record word
table plus board-selection rules are native-tested. The shared native asset
layer now embeds and exhaustively checks both Word graphics modes, archives,
fonts, configuration, exact game-specific resource-ID mapping, and all six
scripted scenes with their original per-bank sound callbacks. Its recovered
two-gate version/splash startup, title/input flow, exact 6x5 generated-word
board, four-direction movement,
exact seven-tick (240.24 ms) chew sequence with immediate answer blanking and
terminal snapshot-based scoring, shared score/life progression, and requested level
selector are now hosted by the same window. The runtime also hosts the exact
random interior player spawn and the statically recovered twelve-tier Word
Troggle/safe-zone scheduler, species trails, warning/entry/movement, and
21-tick deferred collision loss. Both games also propagate the original
board-live return after every Troggle trail, so a last-cell completion aborts
the old movement/safe-zone callback before it can mutate the newly generated
board or append retirement/activation cues. Its recovered wrong-word/final-loss flow now
preserves the one-row feedback strip, performs Hall admission and name entry,
and reaches the original default-Yes replay prompt. All six recovered Word cartoons
also run through their original scheduler counts, actor composition, per-scene
ADLI/PSND callbacks, continuation-sheet frames, complete host-visible
framebuffer timelines, blocking board-close/scene-open entry, and teardown/
next-board lifecycle. Six muted lossless original captures establish their
independent 10 Hz clock, opaque retained dirty painter, and all 586
DOS-presented distinct stable framebuffers; every one now matches native
exactly. Their post-Wipe `0xA6` event now starts each embedded
stream-38 AdLib score after the original 10 ms hold, beside the live channel-0
callbacks. The selected-scene loader first retires completion stream 15 and
retains the executable's 15 ms minimum inside the now-live-measured
bank-specific covered hold; teardown issues two resident GSND-0 stops separated
by the same 15 ms without resetting the chip. The six captures also cover the
production board-close/load/open owner: cyan holds span 41–44 lossless samples,
the upward opening reveals a cleared black page for 6–8 samples, and scene tick
1 follows on the next 10 Hz callback instead of being painted underneath the
Wipe. Native matches those completed states while suppressing the original torn
single-buffer refreshes. The presenter follows
the BTMP source-PCXF field for all six frames
stored in sheets 3003/3005 instead of cropping their logical base sheets. These paths have
headless launcher-return, VGA/CGA, and all-scene gates. Both original Word
instruction sequences are now rendered directly from the embedded `DATA:1`
archive: six Information pages and five pre-game character/control pages with
their original text, Muncher/Troggle placements, pager input, and routing. This
build also replaces Word's fabricated Options placeholder with the recovered
five-entry menu and functional content/difficulty/vowel/preview, Hall erasure,
and password-gate flows. Word's joystick calibration, threshold, direction,
repeat, and button contracts are integrated too; the disconnected prompt now
has an exact live pixel gate while physical-controller validation remains open.
Word settings, Hall, password, and content now persist
in a separate validated per-user file without modifying `WM.CFG`. Word's
embedded gameplay sound bank now supplies the recovered Troggle-entry,
Time Out, chew, warning, collision, retirement, pre-game pager, and board-
completion cues through both native AdLib and PC-speaker renderers; physical
audible comparison remains open. Ordinary AdLib gameplay and cartoon cues keep
one YM3812 clock running across events instead of resetting a PCM renderer for
each sound. The Word title now also enters the recovered autonomous demo after
its idle interval: it uses the original PRNG-driven player callback,
first-failure routing, demo HUD, and the shared PCX Wipe lifecycle rather than
an invented fixed restoration delay. Native gates cover the distinct
clean/collision terminal Wipes, Hall-to-logo Wipe, live logo-to-board paint,
Hall/logo loop, the selector-5/tick-21 collision tie, fractional-frame transfer
into the 150-tick feedback hold, and exact 224,919 ms two-section attract
score. Score channels
1-8 and replaceable channel-0 cues now share one continuously clocked YM3812
rather than two reset waveform owners. All 33 executable Word random-wrapper
call sites are inventoried as well, with deterministic gates over board,
safe-zone, player, and Troggle startup order. That audit corrected a five-draw
native drift: silent Demo completion goes directly to the unattended terminal
and does not run the ordinary cartoon shuffle. This is statically and headless
gated. A preserved organic live Demo board now matches native across the
entire 320x200 framebuffer. The top-right bell is BTMP 6001 frame 3—the
target exemplar for `/e/ as in bell`—not a guest pointer. Restoring that
missing picture, its resident Hall/gameplay DAC mapping, the centered
`Press a key for Muncher Menu` footer, and the target-sound breve produces the
exact full-frame hash `0x6426d16846667dac`. A second organic user recording
also locks the complete `/e/ as in tree` board, target picture, pause/quit
overlays, downward movement, safe-zone removal/addition, warning, and clipped
top-edge Reggie entry at exact full-frame pixels. The pinned seed-`0x8E74`
route now closes Title-to-Demo entry, autonomous movement/chew/terminal
transitions, all controller phases, and whole-session PRNG ownership through
later boards. Digital audio schedules and PCM close the
application-controlled sound boundary. Word's four-state vowel marks and exact ten-row-by-
six-column Preview Words page are now recovered too, including the distinct
executable target-availability table, 60-word pager, and correct record-1 grid
start. Muted physical-window captures match native for Options, Set Content,
Difficulty, Vowels, F1 Help, two Preview densities, the supplied Hall eraser,
and the supplied title-browse Hall. A second ten-page batch adds exact
zero-mismatch matches for Set Password, the password gate/rejection, joystick
attach, all three vowel validations, insufficient targets, initially empty
Hall, and the configuration-write alert. These runs corrected the compact
Preview font, pronunciation marks, Vowel Help layout/footer, visible-state
validation, modal/footer geometry, password/hint placement, empty-Hall
baselines, calibration footer, and write-alert border/text. Hall/password
contracts are now recovered too;
the Hall uses its original wreath PCXF, exact title/row/footer baselines,
mode-specific VGA/CGA text colors, small-font ranking, score alignment,
`Perfect` label, and opaque newly admitted-row highlight. A muted user path now
also matches the original wrong-word/collision feedback, blank and typed name
entry, admitted highlighted Hall, replay Yes/No, and return to title. Exhaustive
headless composition and state gates cover the remaining calibration,
admission/rank, no-more-entries, and parameterized editor variants; the
Win32/WinMM sample boundary is documented in
[`analysis/WORD-OPTIONS-STATIC-AUDIT.md`](analysis/WORD-OPTIONS-STATIC-AUDIT.md).
Word's independent executable CGA color table and complete native CGA startup,
title,
Options, Hall, and board frame gates are documented in
[`analysis/WORD-CGA-STATIC-AUDIT.md`](analysis/WORD-CGA-STATIC-AUDIT.md).
The Word parity ledger is complete; see
[`analysis/WORD-BASELINE.md`](analysis/WORD-BASELINE.md) and
[`analysis/WORD-CONTENT-STATIC-AUDIT.md`](analysis/WORD-CONTENT-STATIC-AUDIT.md),
the [`analysis/WORD-ASSET-BRIDGE.md`](analysis/WORD-ASSET-BRIDGE.md) gate,
[`analysis/WORD-CONFIG-STATIC-AUDIT.md`](analysis/WORD-CONFIG-STATIC-AUDIT.md), and scoring/progression evidence in
[`analysis/WORD-SCORING-STATIC-AUDIT.md`](analysis/WORD-SCORING-STATIC-AUDIT.md),
plus the live-and-headless [`analysis/WORD-SCENE-STATIC-AUDIT.md`](analysis/WORD-SCENE-STATIC-AUDIT.md)
and [`analysis/WORD-STARTUP-STATIC-AUDIT.md`](analysis/WORD-STARTUP-STATIC-AUDIT.md).
The native Word runtime core now also owns exact board consumption,
score/bonus/four-Muncher state, and capped pressure progression; see
[`analysis/WORD-GAME-CORE-STATIC-AUDIT.md`](analysis/WORD-GAME-CORE-STATIC-AUDIT.md).
Word's supplied Hall entries and exact rank/tie/cutoff behavior are separately
gated in [`analysis/WORD-HALL-STATIC-AUDIT.md`](analysis/WORD-HALL-STATIC-AUDIT.md),
and the integrated runtime/launcher boundary is recorded in
[`analysis/WORD-RUNTIME-PRESENTATION-AUDIT.md`](analysis/WORD-RUNTIME-PRESENTATION-AUDIT.md),
with the Troggle evidence and remaining live-comparison boundary in
[`analysis/WORD-TROGGLE-STATIC-AUDIT.md`](analysis/WORD-TROGGLE-STATIC-AUDIT.md),
and the feedback/name/Hall/replay call trace in
[`analysis/WORD-FEEDBACK-TERMINAL-STATIC-AUDIT.md`](analysis/WORD-FEEDBACK-TERMINAL-STATIC-AUDIT.md),
plus the archived instruction-page evidence in
[`analysis/WORD-INFORMATION-STATIC-AUDIT.md`](analysis/WORD-INFORMATION-STATIC-AUDIT.md),
and the gameplay cue/decoder evidence in
[`analysis/WORD-GAMEPLAY-AUDIO-STATIC-AUDIT.md`](analysis/WORD-GAMEPLAY-AUDIO-STATIC-AUDIT.md),
plus the autonomous controller and score evidence in
[`analysis/WORD-ATTRACT-STATIC-AUDIT.md`](analysis/WORD-ATTRACT-STATIC-AUDIT.md),
and the byte-identical player/eating records plus continuation-sheet correction
in [`analysis/WORD-SPRITE-STATIC-AUDIT.md`](analysis/WORD-SPRITE-STATIC-AUDIT.md).
Word's page-palette audit also recovers the splash-only yellow-ramp index that
the earlier sparse Number-derived remap missed. A muted first-board Word chew
and the re-extracted first Number Demo board independently measure the occupied
player index-111 pixel as `414100`; the earlier `413D00` first-board value was
native-only. The logical Hall/title/Demo latch remains modeled, but this sprite
slot is pixel-neutral across first board, terminal, Hall, replay, and later
boards. See
[`analysis/WORD-PALETTE-STATIC-AUDIT.md`](analysis/WORD-PALETTE-STATIC-AUDIT.md).

## Run

The current development output is `build\Number Munchers.exe`; the verified
release copy is `dist\Number Munchers.exe`. It opens the
shared collection menu and needs no adjacent asset directory, DOSBox
installation, or original disk/executable. The packaged executable is checked
in an isolated temporary directory for embedded-resource completeness and
system-DLL-only imports without running its GUI.

## Controls

| Action | Keyboard / mouse |
|---|---|
| Move | Arrow keys or 8/A/I (up), 4/J (left), 6/K (right), 2/M/Z (down); click a square |
| Munch | Space; click the occupied square again |
| Joystick | Options can enable and calibrate WinMM joystick 1; buttons 1/2 map to Munch/Pause; `+`/`=` and `-`/`_` make held-direction repeat faster/slower and play the original pitch feedback |
| Pause / resume | Enter or right mouse button |
| Leave a game | Escape |
| Menu navigation | On fixed numbered lists, Left/Up select the previous row, Right/Down the next, and Home/End the first/last; Enter activates and number keys select rows without activating. On original unnumbered list widgets, 4/8 select previous and 2/6 next. In Yes/No prompts, Left/Home select Yes, Right/End select No, Y/N also select, Up/Down and Space are ignored, and Enter accepts. Space works only where explicitly prompted; mouse is also supported. |
| Skip a level cartoon | Any ordinary key except Escape; Escape leaves the cartoon running |
| Toggle fullscreen | **Alt+Enter** |
| Cheat / level select | **Ctrl+Alt+F1** |
| Toggle sound effects | Alt+S |
| Toggle music cues | Alt+M |
| Toggle resident sound device (AdLib / PC speaker) | Alt+P |

The two requested toggle chords, Alt+Enter and Ctrl+Alt+F1, act only on the
initial physical key-down; Windows key-repeat messages cannot bounce their
fullscreen or overlay state.

Alt+S/M/P follow the original resident-game scope: they are active during Demo,
play/pause/feedback, and cartoons, but not in the launcher or synchronous menu,
information, Hall, prompt, editor, or cheat-overlay pages. Alt+S returns after
toggling; Alt+M/P also forward the event, so they exit Demo or skip a cartoon.
When Alt+M enables music during Demo, the original restarts the score only on
the active board/feedback/board-to-Hall state and only when AdLib is selected;
it does not briefly restart it on the Hall or logo/splash phases before that
same event exits Demo. Alt+S and Alt+M emit the original descending/ascending
GSND acknowledgements on both device paths; music-off delays its descending
cue by 50 ms. With music enabled, Alt+P's AdLib-to-speaker edge stops the Demo
score, while its speaker-to-AdLib edge restores the score only in that same
state-2 phase. See
[`analysis/SOUND-TOGGLE-STATIC-AUDIT.md`](analysis/SOUND-TOGGLE-STATIC-AUDIT.md).
If an Alt+M/P settings save fails, its blocking write alert consumes a later key
before that original Demo-exit/cartoon-skip continuation runs; Alt+S has no such
post-alert continuation.

The cheat overlay can start levels 1–20, toggle invincibility, add a Muncher, or skip the current board.

## Current native implementation

- Multiples, Factors, Primes, Equality, Inequality, and mixed Challenge games
- measured 6×5 rule-board and fixed HUD geometry, correct/incorrect values, safe zones, scoring, lives, and level progression
- native-size original sprite tiles, fixed idle/life poses, recovered five/six-phase four-direction movement, the exact seven-tick Muncher chew callback (cell blank/cue at start, scoring at terminal), standing-only Troggle endpoint collisions, and the exact 21-scheduler-tick collision sequence repeating records `12,13,14,13,12,13`
- exact fixed-array gameplay scheduler order—safe zones, player, persistent Troggle jobs by ID, then Demo—with coalesced Windows frames dispatched one original public tick at a time; this preserves mixed waiting/warning/actor PRNG ownership, protected chewing, queued movement-to-chew creation, wrong-answer terminal ordering, inert non-selected collision actors, endpoint- and movement-collision tails, and selector-100 final-chew transfer into direct-board or board-to-cartoon Wipes without aging fresh jobs or the initialized scene with old-frame time
- measured level-1 Reggie warning/spawn/movement/exit/recovery flow, including an invisible warning-stage actor and exact top-, left-, right-, and bottom-edge entry poses; the lossless horizontal arrivals lock the phase-2 clear/reveal states, three progressively visible poses, phase-6 middle pose, asymmetric right viewport through x=309, and terminal restoration of the x=308 grid line, while the full-Demo Bashful arrival locks the bottom viewport through y=177, retained y=176 grid damage, dwell restoration, and the resident `5DBEFF` gameplay highlight, plus exact 12-tier Troggle count, arrival, speed, five-species weights, steering rules, and cell-trail effects recovered from the executable
- continuous third-Demo Worker collision gate: native now presents all 25 complete source runs in exact order from the stationary pre-bite callback through all 21 logical bite ticks, simultaneous safe-zone/Reggie callbacks, and stable `Aargh` feedback. It fixes the pre-bite dwell, first closed bite over the pre-safe surface, and callback-local slot ordering: the bite paints at physical slot 1 before later Reggie phases, retains the clipped phase-1 page, and aborts before that Reggie on its final-pose/terminal callbacks. Two torn refreshes stay capture-only.
- complete fourth-Demo Bashful approach/collision gate: 35 presentation-complete full-frame runs are source-locked across the preceding Worker entry/dwell, all four clipped Bashful entry phases, all five concurrent right-entry Reggie phases, the exact pre-bite frame, all 21 alternating bite ticks, stable `Aargh! ... Trogglus timidus.` feedback, and the following terminal-dwell collision board. This fixes the native collision transition that incorrectly held bite record 12 after the feedback text cleared; both runtimes now restore terminal dwell record 15, and Word separately gates the four shared top-entry poses. Four nonuniform refreshes and two pixel-proven scanout splices stay capture-only.
- continuous restarted fourth-Demo board gate: frames 15321–16390 now align 86/86 native changed pages to exact DOS pages in order with zero native-only composites. The chained collision keeps player record 12 under the final Bashful entry poses, drains the earlier-slot Reggie’s complete entry phases over the stationary Bashful endpoint, and then exposes the full 21-pose bite sequence. Pixel reconciliation classifies all fifteen source-only runs as eleven nonuniform refreshes and four uniform incomplete painter states; calls 604/605/607, `Aargh`, the restored board, and the following Hall are locked headlessly.
- continuous restarted fifth-Demo board gate: frames 18570–20527 cover the complete Factors-of-63 route after the Hall/logo restart. The call-695 initializer, all thirty labels, target 63, player cell, and full-frame hash are exact. Native emits 224 changed pages: 218 exact ordered DOS pages, four repeated exact DOS pages, and two complete callback surfaces that partition adjacent captured painter updates. All 27 source-only scanout/painter records are pixel-classified; no unexplained native page remains through the final `Troggle!` board.
- final Demo exit gate: frames 20528–22003 contain one 347-block nonuniform DOS transition followed by 1,475 identical title frames. At relative frame 1958/call 767, native consumes the external key, clears Demo state, emits the exact title hash `0xedf7e5d8c59eeac7`, and holds it for the remaining 21.0598-second capture tail without a PRNG change or automatic restart.
- two complete first-Demo Reggie-on-Reggie bite/terminal gates: the first interval matches its pre-bite overlap, all 21 alternating bite ticks, and terminal record-15 repaint across 23 uniform DOS frames; the later row-4/column-5 interval independently matches all 21 bite callbacks plus its terminal repaint, adding 22 exact stable states and raising aggregate live Number Troggle coverage from 197 to 219. The actor keeps that painted dwell record independent of its retained rightward steering heading, and both Number and Word queue the earlier player callback before a later cannibal job.
- complete third first-Demo cannibal/player-walk gate: frames 5332–5385 contain 30 uniform source runs while the Muncher walks upward through a row-0/column-2 Reggie bite. Pixel partitions prove three one-frame runs split an actor dirty paint, leaving 27 presentation-complete pages. Native now presents exactly those 27 complete pages in source order—including the earlier/later dirty-cell callback pages, terminal bite, and two following player callbacks—without reproducing the three incomplete paints or emitting extra pages. The retained board-cell/page model closes the former five-state gap and raises aggregate live Number Troggle coverage from 219 to 246.
- complete additional first-Demo Bashful right-entry gate: frames 4309–4339 contain six completed phase-2-through-dwell pages that native presents at exact full-frame hashes, raising aggregate live Number Troggle coverage from 246 to 252. The intervening one-frame source run restores only four x=308 grid pixels before the next actor pose overwrites them; it is classified as an incomplete dirty repaint and deliberately remains capture-only instead of becoming native flicker.
- complete later first-Demo Bashful `23 -> 26` left-trail gate: frames 5386–5445 retain the third-cannibal terminal page for a continuous 39-frame dwell, closing the former 5386–5419 gap, then eight completed pages match native in exact order across all six leftward phases and terminal dwell. The opening page overlaps the third cannibal audit, so this adds seven new states and raises aggregate coverage from 252 to 259. A one-frame terminal repaint contains a 13-pixel one-row strip belonging to neither neighboring completed surface and remains capture-only.
- complete later first-Demo Bashful bottom-entry/player gate: all seven presentation-complete pages from frames 5188–5215 match native across the Muncher's final two downward phases, endpoint and idle, Bashful's invisible phase 1, visible entry phases 2–5, dwell, an earlier Reggie dwell, and a warning retained by another slot. This converts the exhaustive inventory's formerly ungated window into exact isolated evidence and raises aggregate live Number Troggle coverage from 358 to 365. The sole 20-block Bashful phase-3-to-4 repaint contains 47 pixels belonging to neither complete neighbor and remains capture-only.
- complete later first-Demo Bashful bottom-exit/player gate: all seven presentation-complete pages from frames 5687–5718 match native across Bashful's left-facing dwell, `115 -> 105` trail, all five clipped downward-exit phases, terminal removal, the Muncher's upward phases 1–5, a row-1 Reggie dwell, the active safe zone, and a warning retained by another slot. This closes the exhaustive inventory's other formerly partial Bashful window and raises aggregate live Number Troggle coverage from 365 to 372. Frame 5711 is exactly the prior completed page above logical scanline 90 and the following completed page below it, so that scanout splice remains capture-only.
- complete first-Demo Reggie/player-eating gate: all 23 presentation-complete pages from frames 5719–5799 match the continuous native replay in exact order across the stationary collision page, every one of the 21 bite callbacks, and the complete `Oops`/`Trogglus normalus` feedback page. The stationary page is pixel-identical to the already gated collision-transient board, so this adds 22 pages and raises aggregate live Number Troggle coverage from 372 to 394. Frames 5740, 5752, and 5764 are exact horizontal scanout splices at logical rows 65, 70, and 76 and are not reproduced as native flicker.
- complete first-Demo rightward collision-approach gate: all eight presentation-complete pages from frames 5660–5687 match native across the Muncher's starting safe-zone dwell, five interpolated horizontal positions, right-facing endpoint record, and idle callback while Reggie, Bashful, and the actor-owned warning remain resident. The idle page opens the bottom-exit audit, so this adds seven pages and raises aggregate live Number Troggle coverage from 394 to 401. This interval contains no incomplete refreshes.
- complete first-Demo Reggie-down/safe-entry bridge: all 15 presentation-complete pages from frames 5446–5659 match the continuous native replay across Reggie's source dwell, all five downward callbacks, destination dwell and source-cell restoration, the safe-zone activation shared with Demo's next action, all five rightward Muncher callbacks, endpoint, idle, and the next actor-owned warning while Bashful dwells. Its two boundary pages are already covered by the neighboring left-trail and right-approach audits, so this contributes 13 new pages and raises aggregate live Number Troggle coverage from 401 to 414. The audit removed a native-only phase-zero page by retaining the pre-safe-zone framebuffer when safe activation and move installation share one scheduler tick. Frame 5598 contains the complete following idle pose plus a 46-physical-pixel strip belonging to neither complete neighbor and remains capture-only.
- complete first-Demo correct-chew/top-entry bridge: all 15 presentation-complete pages from frames 5216–5331 match the continuous native replay across the exact seven-record chew of `220`, terminal record-13 hold, warning removal, all four visible top-entry callbacks, and the ordinary overlapping Reggie endpoint immediately before the third cannibal bite. The opening page overlaps the later bottom-entry audit, so this adds 14 pages and raises aggregate live Number Troggle coverage from 414 to 428. Native now retains an entry-terminal overlap page until state 5's first callback on the next public tick instead of exposing bite record 12 immediately. Two exact horizontal scanout splices at physical rows 340 and 236 and one 18-block torn entry repaint remain capture-only.
- complete first-Demo player/Reggie/safe-zone bridge: frames 4162–4308 form 28 logical runs across two downward Muncher walks, the intervening upward walk, Reggie's simultaneous rightward crossing and `92 -> 115` trail, and the safe-zone/entry handoff before Bashful appears from the right. Exact pixel partitions classify seven runs as incomplete dirty repaints; native matches the remaining 21 presentation-complete pages in exact order, raising aggregate live Number Troggle coverage from 428 to 449. The gate removes a native-only safe-zone/warning dwell by retaining the pre-safe resident page until warning state advances to Bashful's invisible phase-1 entry. Five rejected pages also break physical 2x duplication; the two uniform rejects are exact old/new mixtures with no unique pixels, so none are reproduced as flicker.
- complete first-Demo chew/entry callback boundary: all 15 presentation-complete pages from frames 3520–3606 now match native in exact order across the pre-chew dwell, the terminal callback of a resident bottom entrant, the Muncher's full seven-record row-1/column-3 chew, warning removal, and a new right-edge entry. This adds 15 focused pages and raises aggregate live Number Troggle coverage from 527 to 542. The nearby source-only frames 3462, 3474, and 3491 are separately proven scanout/partial dirty artifacts; one torn block at frame 3568 shares its logical page with two uniform copies and is not reproduced as flicker.
- complete pre-crossing artifact classification: the eight source-only pages at frames 3616, 3717, 3729, 3796, 3818, 3830, 3871, and 3883 are pixel-partitioned against their complete neighbors. Seven contain measured incomplete dirty pixels and frame 3818 is an exact adjacent-page scanout splice; none is emitted by native, so no flicker state is synthesized.
- complete first-Demo departing-Reggie/player overlap: all nine presentation-complete pages from frames 3884–3941 now match native contiguously while the Muncher moves from row 1/column 4 into Reggie's departing row 2/column 4 cell and Reggie moves right. The recovered callback rule clears only Reggie's immediately previous/current 41x29 actor boxes, retaining the newer Muncher record in the older swept trail. This raises aggregate focused live Number Troggle coverage from 542 to 551. Frame 3931 is an incomplete 16-block Muncher/Troggle repaint and remains capture-only.
- complete first-Demo post-departure bridge: all 24 presentation-complete pages from frames 3941–4090 match one contiguous native sequence across the standing boundary, the exact seven-record chew, two complete Muncher moves, and the pre-chew/right-exit boundary. Fourteen states are new beyond the neighboring overlap, chew, and exit gates, raising aggregate focused live Number Troggle coverage from 551 to 565. Frames 4027 and 4044 contain 24-block and one-block partial dirty repaints with seven/eight pixels belonging to neither complete neighbor; both remain capture-only.
- complete true first-board opening gate: frames 2390–2544 contain the stable initial Multiples board and the first downward Muncher move. Native matches all eight presentation-complete pages contiguously; frame 2459 is an exact adjacent-page scanout splice with 20 nonuniform doubled blocks and no unique logical pixel. These eight newly focused pages raise aggregate live Number Troggle coverage from 565 to 573.
- complete post-second-Cannibal bridge: frames 4706–5173 contain 55 source runs across the terminal bite surface, three Muncher moves, two Troggle moves, and the next bottom-entry boundary. Native matches the 51 completed pages as one exact contiguous sequence. Frames 4946 and 5172 are nonuniform scanout/dirty fragments; uniform frames 4958 and 5136 each expose only an unfinished 41-pixel grid row. The two boundary pages overlap neighboring gates, so 49 pages are new and aggregate focused coverage rises from 573 to 622.
- reconciled whole first-Demo gameplay board: the expanded board-wide oracle streams every source frame 2390–6131 and measures 584 logical source runs, 541 changed native pages, and 515 exact LCS matches. A reproducible second-stage audit resolves all 69 source-only runs as 68 pixel-classified capture artifacts plus one repeated-hash alignment ambiguity; pixel-dumps prove the six internal native-only pages are complete callback-local actor composites, and the remaining 20 native pages begin after gameplay in the separately gated collision-Wipe/Hall/logo/next-board transition. The scoped first-board gameplay sequence now has a measured board-wide parity claim rather than a provisional diagnostic.
- reconciled complete second-Demo lifecycle: frames 8353–11187 continuously span the stable Prime board, both Muncher moves, the exact seven-record correct chew, wrong chew and feedback, restored board, board-to-Hall Wipe, Prime Hall, Hall-to-logo Wipe, logo, logo-to-board Wipe/painter, and the first stable Equals-30 board. The comparator measures 53 source runs and 51 native changed pages, with 40 exact ordered matches. Pixel reconciliation classifies the 13 source-only runs as eleven torn refreshes plus two uniform partial dirty/painter intermediates; all eleven native-only pages are the permanent-gated completed Wipe or board-painter surfaces. The whole captured lifecycle now has a measured continuous parity claim.
- reconciled complete third-Demo gameplay board: frames 11187–12392 span the stable Equals-30 opening, five Muncher moves and correct chews, Worker/Reggie arrivals, safe-zone activity, the Worker collision and all 21 bite ticks, stable `Aargh`, and the restored-board hold immediately before the Wipe. A board-wide comparison exposed and fixed a real terminal gap: the concurrently entering bottom Reggie now advances from phase 4 to the captured final visible phase 5 when collision feedback is removed, changing the restored native hash from `0xd27819377a45835c` to exact DOS hash `0x25a5e4cc4a413f32`. Native now matches all 106 presentation-complete pages; the only 12 source-only runs are ten nonuniform refreshes and two uniform adjacent-page partial painters, and there are no native-only pages.
- complete first-Demo Bashful trail/left-exit gate: nine uniform DOS framebuffers match in order across the two pre-exit player poses, retained-player/phase-1 callback composite, complete phase-1 repaint, phases 2–5, and terminal `150` repaint; the shared renderer clips the actor and swept blue dirty rectangle to the board, hides the regenerated label until removal, and starts ordinary movement at the original immediate phase 1 in both runtimes
- complete first-Demo Reggie in-board trail gate: 13 complete DOS framebuffers match the preceding upward player phases/terminal, the player-idle callback visible before the later Troggle job, `207 -> 65` regeneration, all six rightward Reggie positions, and dwell; selector-5's logical player phase 0 is held offscreen exactly as in the retained DOS framebuffer, while one uniform-2x partial actor repaint and two nonuniform refreshes remain classified capture evidence
- complete second Bashful trail gate: the 54-frame pre-trail dwell, `150 -> 220` regeneration, all six rightward phases, and dwell match eight more complete source states; its one 19-block torn refresh stays capture-only
- complete adjacent later Reggie/top-exit gate: fourteen more uniform DOS framebuffers match a second six-phase Reggie crossing and dwell, then all five clipped upward Bashful exit poses and the terminal top-right repaint; two nonuniform refreshes remain capture-only, while the shared Word gate proves the vertical exit cannot leak into the header or expose its trail payload early
- complete first-board Demo chew gate: eight consecutive uniform DOS framebuffers match the full `12,13,14,13,12,13,14` Muncher animation and terminal record-13 hold at exact 320×200 pixels, with no partial-refresh exclusions
- complete simultaneous first-Demo chew/right-exit gate: nine complete DOS framebuffers match the pre-action state, every combined Muncher chew/Reggie movement pose, actor removal, and terminal record-13 hold while Reggie regenerates `232 -> 3` and leaves the right edge; frame 4109 is proven pixel-by-pixel to be a partial dirty refresh and frame 4121 has one torn 2× block, so neither incomplete source update is reproduced as native flicker
- isolated simultaneous dual-Helper entry gate from the later Factors-of-63 Demo board: thirteen completed DOS framebuffers match native across the warning, a rightward Muncher walk, all presented left- and right-edge Helper arrival phases, both terminal poses, and dwell. This adds thirteen unique states and raises aggregate live Number Troggle coverage from 259 to 272. Two nonuniform refreshes and a one-frame 26-pixel partial warning-box erase remain capture-only.
- isolated dual-Helper gate from the later Factors-of-63 Demo board: all thirteen complete DOS framebuffers now match exact full-frame hashes across simultaneous horizontal/vertical Helper movement, destructive `3` trails, and the overlapping downward Muncher move. Three are exact resident-surface player callbacks, and the production Number/Word schedulers route that compositor; Word separately gates the same thirteen state tuples and shared Helper geometry. The only intervening source run is proven pixel-for-pixel to splice run 6 above logical row 81 with run 8 below it, so it remains capture-only scanout tearing in the double-buffered port. This is isolated visual/callback evidence, not a restored continuous-route claim.
- complete later concurrent-Helper gate on that board: 27 presentation-complete source pages match native across a destructive `8` trail, the Muncher's leftward walk and exact `12,13,14,13,12,13,14` chew of `63`, a destructive `12` trail, and a simultaneous destructive `30` bottom exit. This raises aggregate live Number Troggle coverage from 272 to 299. Two nonuniform refreshes and one uniform 15-pixel incomplete player paint remain capture-only.
- complete intervening player/Helper-dwell gate on that board: 30 presentation-complete source pages match native across a left step, two exact `12,13,14,13,12,13,14` chews of correct `1` cells, a downward step, the retained phase-zero page, and the first safe-zone expiry while the surviving Helper dwells at row 4/column 5. This raises aggregate live Number Troggle coverage from 299 to 329. Three nonuniform actor transitions and the five-block lower-half start of the second safe-zone expiry remain capture-only; four preceding uniform copies independently lock that terminal page.
- complete later-Demo Helper bottom-exit gate: twelve more uniform DOS framebuffers match the Helper dwell, all five clipped downward exit phases, destructive `7` trail, terminal removal, and every overlapping upward Muncher phase through its standing callback. The paired Word fixture gates the same state tuples, bottom viewport, trail, and no-margin-leak invariant. This is isolated post-divergence evidence from the same Factors-of-63 board.
- complete final gameplay-board tail gate: 29 presentation-complete DOS pages match native across left/down/left Muncher movement, the exact `12,13,14,13,12,13,14` chew of correct `21`, terminal record 13, and the next actor-owned `Troggle!` warning with no enemy yet visible. This closes every complete source page from frames 20188–20527 and raises aggregate live Number Troggle coverage from 329 to 358. A one-block border update, a 16-pixel terminal-to-idle remnant, and a uniform horizontal chew splice are capture-only incomplete paints.
- complete seeded Smarty presenter gate: a pinned level-10 `0xAE67` DOS run matches all 75 presentation-complete pages in exact order from Helper/Reggie/Smarty entry overlap through the selected Smarty's 21 bite callbacks, `smarticus` feedback, and restored board. The recovered configuration begins at the exact call-61 PRNG state and ends at call 84; nine nonuniform frames and two old/new partial repaints are pixel-proven capture artifacts. A 31-page Smarty-through-restored-board oracle now runs in the normal renderer suite.
- pixel-matched startup, initial and filtered game selection, all six Information pages, Hall selection, supplied Multiples Hall, newly admitted opaque-highlight Hall, eighteen Options-family frames, full Word target-exemplar pictures, the in-game quit dialog, vertical `Time out` marker, reference-backed early attract boards plus deterministic corrected continuation, the statically recovered 150-tick Demo feedback wait, captured 39-frame board/PCX-Wipe Hall lead-in, complete six-frame Hall-to-logo and nine-frame logo-to-board Wipes (including both torn dynamic board paints), the same executable-proven nine-interval presenter on ordinary first/replay/direct/post-cartoon boards, and the live-measured Word board-close/scene-load/open path with 41–44 covered samples, a cleared black target, and post-open 10 Hz first tick, plus exact attract Hall/logo and external-key/title-idle lifecycles, board-preserving Hall name entry, functional final-loss routing, and level-complete scenes
- exact shared feedback-waiter Escape routing: the default-No prompt resumes/restores on No or Escape, accepted Yes uses the scored Hall/name-admission terminal, and an exhausted final reserve still continues through the ordinary loss route
- transactional Set Content editing for enabled games, ranges, random/in-order sequences, the Multiples ceiling, and Equality/Inequality operations; accepted settings drive the live generators and filtered Play menu
- exact 11-level difficulty presets recovered from the executable, including enabled games, content ranges, sequence modes, multiplier caps, operation masks, and Challenge component filtering
- statically recovered joystick detection/calibration flow, midpoint thresholds, single-axis pending-sample/deadline phase, movement-time direction gating, default-four mutable 1–10-tick repeats, direct 950..500 Hz/50 ms PC-speaker speed feedback, and edge-triggered Space/Enter buttons through WinMM joystick 1
- lossless original Number joystick-calibration gate: all 199 stable attach-page frames match native at exact 320×200 RGB pixels after the capture exposed and corrected a nine-pixel footer error; the canonical AVI, physical 2× duplication, native RGB SHA-256, and source FNV-64 are reproducibly audited
- controlled original later-level user-HUD gate: all 554 stable Level-7/Score-321/three-reserve source frames match native at exact full-frame pixels, including the Factors-of-41 board, player and safe-zone placement; the process-state sidecar proves the pinned `0x1CB5` RNG and retained level-1 pressure before the original Wipe/HUD painter ran
- exact board-generator distributions recovered from the executable, including descending Factors selection, Prime-range growth/composite gaps, Equality/Inequality result construction, descending multiplication divisors, random commutative-operand swaps, and all-stream progression
- original Borland 32-bit random generator, 16-bit time seed, high-15-bit output, and modulo range mapping used by boards, enemies, safe zones, and the attract controller
- original board-live callback boundary propagated through the whole job pass, so last-positive Troggle trails cannot retire a stale actor, append extra cues, or age a freshly generated board on the destroying tick
- deterministic attract replay across the reference-backed early boards and uninterrupted corrected continuation through call 1677, with every Hall/logo pair using the executable's exact 450 scheduler ticks and no native-only three-board cutoff; post-capture pixels remain uncaptured rather than claimed as verified
- a bounded native interpreter for all eleven original SCPT cartoons, including concurrent thread/painter order, signed 16.16 movement, waits, frame loops, synchronization, opaque retained dirty-framebuffer detach/reuse, continuation sprite sheets, actor removal, synchronized original ADLI cues, and each bank's non-repeating stream-38 score
- Original VGA and CGA title, sprite, and scene artwork decoded directly from the MECC resource archives; `--cga` selects the recovered four-color hardware path
- Original variable-width GEM fonts rendered pixel-for-pixel
- lossless original live-play AdLib traces retained as preservation fixtures, while runtime gameplay now dispatches decoded GSND streams 3–15 at their statically recovered event boundaries (ordinary board creation is silent; completion is a Sound/device-routed stream 15); one embedded YM3812 retains chip state across gameplay/scene cues and carries both the fractionally timed eight-part 105-second Demo score and all eleven bank-local cartoon scores on channels 1–8 alongside replaceable channel 0, with exact GSND 1/2 Alt+S/M acknowledgements, the 50 ms music-off delay, GSND 0's non-resetting 36-write channel retirement/depth cleanup across Demo, board, title, pre-prompt Escape, the 15 ms scene-load cutoff, and two-call/15 ms cartoon teardown, current-device-gated Alt+P score transitions, and explicit native output-device shutdown only when the active game returns to the shared launcher
- Original `NM.CFG` Hall entries as first-run defaults, then per-user Hall data under `%LOCALAPPDATA%\NumberMunchersNative`
- per-user persistence for difficulty, custom content, sound/music, joystick selection/calibration, password, and hint without modifying the supplied DOS configuration
- DPI-aware integer scaling and borderless fullscreen on the active monitor

The original graphics-device branch is also native now: VGA256 remains the
default, while `Number Munchers.exe --cga` selects `CGA.BGI` mode-1 artwork,
the `LOGO.004` startup image, the recovered three-color foreground collapse,
and the page-selected black or dark-blue background register. Number's 22 CGA
sheets and 302 frame records are headless regression gates. Pinned live CGA
recordings match Number and Word startup, title, Options, Hall, and level-1
gameplay framebuffers exactly; Super's startup, title and ordinary board are
independently locked under `analysis/super-cga-live`. See
`analysis/cga-live/startup-report.json`, `analysis/cga-live/ui-report.json`, and
[`analysis/SUPER-PRESENTATION-LIVE-AUDIT.md`](analysis/SUPER-PRESENTATION-LIVE-AUDIT.md).

The parity audit is complete at 62/62 verified rows across all three ledgers. Long-form source/native evidence—including uninterrupted attract routes, all Number and Word cartoons and Wipes in VGA/CGA, Super's five missions and live presentation oracles, player chew and collision callbacks, input ownership, digital audio schedules, and atomic no-flicker presentation—is indexed by those ledgers. Physical speaker, controller, mixer, monitor, and room characteristics remain optional machine QA under [`analysis/PLATFORM-BOUNDARY-AUDIT.md`](analysis/PLATFORM-BOUNDARY-AUDIT.md), not missing executable behavior.

The reference-backed attract replay remains valid through the first three boards, call-507 `Aargh`, and the externally interrupted third Hall. It also locks every full-frame interval of the second Prime board's captured correct chew and its 25-point result. Rechecking the first Number board and a new first-board Word chew proved both use `414100`; the former supposed Hall carry-over was native-only. The externally restarted Multiples-of-15 route matches calls 507-584 at initialization and then continues through its complete captured gameplay/collision interval with calls 604/605/607, zero native-only presentation pages, and the exact following Hall. Replaying that captured restart reaches the call-695 Factors-of-63 initializer and the fully reconciled frames 18570–20527 route. The corrected native continuation after the supplied capture remains deterministic through call 1677, but is not presented as live DOS parity. [`analysis/PARITY.md`](analysis/PARITY.md) remains the completion gate.

## Build

The port has no third-party runtime dependencies. A MinGW-w64 C++20 toolchain, CMake, and Ninja are sufficient:

```powershell
cmake -S . -B build -G Ninja
cmake --build build --clean-first --parallel
```

The output is `build\Number Munchers.exe`. The MinGW configuration links
libgcc and libstdc++ statically. A production-artifact regression now copies
that exact EXE into an otherwise-empty temporary directory, verifies all 31
embedded Number/Word/Super resources, and rejects non-Windows DLL imports without
executing the GUI; see
[`analysis/SELF-CONTAINMENT-AUDIT.md`](analysis/SELF-CONTAINMENT-AUDIT.md).

Launch without arguments for VGA256, or pass `--cga` for the original CGA
mode-1 presentation.

Run the integration smoke test with:

```powershell
.\tools\native_smoke_test.ps1 -AllowVisibleWindow
```

This flag must be used only with explicit permission because the integration
test opens and focuses the game window. Without it the script stops before
launching anything. The test exercises the Set Content transaction, help, and
its three editors, rolls back the per-list Hall eraser and declines Erase ALL,
verifies Ctrl+Alt+F1 and the level selector, checks both directions of the
Alt+Enter transition, starts all six game modes, samples the chew animation,
verifies the in-game quit dialog and vertical `Time out` marker, pauses/resumes,
and captures the tested screens under `analysis\native-smoke`.

## Repository map

- `src/` — native game, renderer, embedded-resource loader, and Win32 shell
- `assets/original/` — exact game resource containers and fonts used by the build
- `assets/super/original/` — exact Super Munchers archives, fonts, logos, configuration, and product data
- `assets/ripped/raw/` — every entry extracted from all three MECC resource archives
- `assets/ripped/vga/` and `assets/ripped/cga/` — decoded sheets and individual frames
- `assets/ripped/audio/` — isolated lossless original YM3812 register traces and provenance
- `extracted/disk/` — complete FAT12 disk extraction
- `analysis/` — binary audit, unpacked executable, full 16-bit disassembly, strings, and reference captures
- `third_party/ymfm/` — BSD-3-Clause YM3812 renderer used for self-contained AdLib playback
- `src/mecc_sound.*` — native implementation of the recovered MECC GSND timing, loop, patch, pitch, rest, channel-control, and eight-part conductor bytecode
- `src/scene_script.*` — bounds-checked implementation of the recovered concurrent MECC SCPT cartoon VM and its construction-order retained dirty-paint events
- `tools/` — reproducible disk/resource/graphics extraction and verification scripts

The public Git repository omits the local workspace's multi-gigabyte raw
capture videos and decoded frame dumps. The written audits, compact listings,
deterministic hashes, build inputs, and automated gates remain tracked; see
[`analysis/PUBLIC-REPOSITORY-EVIDENCE.md`](analysis/PUBLIC-REPOSITORY-EVIDENCE.md).

See [`analysis/AUDIT.md`](analysis/AUDIT.md) for the reverse-engineering record; [`analysis/PARITY.md`](analysis/PARITY.md), [`analysis/WORD-PARITY.md`](analysis/WORD-PARITY.md), and [`analysis/SUPER-PARITY.md`](analysis/SUPER-PARITY.md) for the three completed parity ledgers; [`analysis/SUPER-BASELINE.md`](analysis/SUPER-BASELINE.md), [`analysis/SUPER-DEMO-STATIC-AUDIT.md`](analysis/SUPER-DEMO-STATIC-AUDIT.md), and [`analysis/SUPER-PRESENTATION-LIVE-AUDIT.md`](analysis/SUPER-PRESENTATION-LIVE-AUDIT.md) for the Super Munchers evidence; and [`analysis/PRESENTATION-FLICKER-AUDIT.md`](analysis/PRESENTATION-FLICKER-AUDIT.md), [`analysis/AUDIO-STREAMING-AUDIT.md`](analysis/AUDIO-STREAMING-AUDIT.md), and [`analysis/SELF-CONTAINMENT-AUDIT.md`](analysis/SELF-CONTAINMENT-AUDIT.md) for the shared release boundary.
