# Attract-demo static and capture audit

This audit used the unpacked executable, the existing lossless unattended
capture, extracted frame hashes, and headless native tests only. No DOSBox or
native game window was launched during this work.

## Original state and controller

`DS:596C` is the original demo flag. Main-screen action 2 sets it and enters
the game initializer at image `0x089AD`; action 4 clears it. Demo board setup
also dispatches event `0x90`, the previously recovered GSND 16 score.

At image `0x09856`, demo setup adds the 20-byte job at `DS:0730`:

```text
07 00 05 00 00 00 02 00 1E 00 00 00 1E 00 00 00 00 00 00 00
```

The global callback dispatch at image `0x13574` maps selector 5 to
`0x7E7:0751`, image `0x085C1`. The neighboring selector 6 reaches image
`0x09937`, which is a safe-zone callback; treating that neighboring slot as
the demo callback was an earlier off-by-one disassembly error. The job first
fires after 30 job ticks. Timer setup at image `0x08B8`–`0x08C6` programs PIT
channel 0 with divisor `0x0555`, and the handler advances the scheduler clock
every 30 interrupts. The resulting shared mask-2 cadence is about 29.1375 Hz.
At the start of every callback, `0x085C1` replaces
both its reload and countdown with `random(0, 30) + 15`, giving subsequent
upper-exclusive delays of 15–44 ticks.

The recovered action chooser is:

1. If the current signed answer value is positive, inject Space to munch it.
2. If it is negative, inject Space on a `random(0, 10) == 0` roll.
3. Otherwise choose `random(0, 4)` as an initial direction, inspect up to four
   directions in clockwise order, and inject the first direction whose
   adjacent cell contains a positive answer.
4. If no adjacent positive answer exists, inject the initially chosen
   direction anyway; the ordinary movement path rejects an edge hit.

This is a lightly fallible answer-seeking controller, not either a
nearest-answer solver or an arbitrary random-key chooser. The preserved Prime
demo's wrong `1` is the observed one-in-ten incorrect-cell branch. Native now
implements this chooser and variable countdown order directly. The Borland
LCG, 16-bit seed truncation, and modulo mapping are also matched as documented
in `PRNG-STATIC-AUDIT.md`; the exact stream state/call order for the preserved
run is now reconciled through construction of the first board.

The original demo initializer first chooses `random(0,9)` at image `0x089FD`,
then board setup advances that zero-based pressure index and visible level.
For the preserved capture's recovered seed `0xF585`, the roll is 8, producing
pressure tier 9 and visible level 10. The four random target streams, enabled
component selection, and otherwise-unused relation roll consume the next six
outputs. Cell generation begins on output eight. Native mirrors those calls,
including the otherwise-unused relation draw and the original column-before-
row player-position order. It no longer installs a hard-coded demo board.

The mode-selection branch at image `0x0FAE0` treats the demo like Challenge:
it selects a component from the runtime enabled-game table and then constructs
that game's board. It does not follow a fixed five-entry family cycle. Native
now selects among enabled component games and never exposes Challenge itself
as the board/Hall label.

## Terminal sequence

The supplied 313.96-second ZMBV capture establishes this unattended sequence:

```text
demo board -> terminal feedback (when applicable) -> restored board
           -> PCX Wipe -> Hall -> logo -> next board
```

The former “20-tick board restore” description was an inference from elapsed
video, not an executable record. Static recovery now separates the sequence.
The Demo branch of the feedback painter at `0x109CF` calls `0x1056:0764`
(image `0x10CC4`). That routine snapshots the public clock at `0x10CCA`, adds
literal `0x0096` at `0x10CCF`, and pumps input until the 150-tick deadline.
It then restores the saved board rectangle at `0x109D4`. No second clock or
scheduler deadline follows. Terminal action 3 eventually reaches `0x1220D`,
which calls `0x10FB:0022` with argument 1; that loads the shared PCX-effects
record and dispatches it through `0x1FCE:0008`. The embedded effects library
identifies this family as `Wipe`.

The 70.086304-fps lossless stream fixes two contextual forms of that action
path. Both take exactly 46 capture intervals (`0.656333` seconds), but their
visible subdivision is different rather than arbitrary capture-phase
variation.

The first, collision-owned terminal has one terminal-dwell capture interval,
39 pixel-identical restored-board frames, and six Wipe frames before the
stable Multiples Hall.
Its exact logical-frame hashes are:

| Visible stage | FNV-64 |
|---|---:|
| collision terminal-dwell transient | `0xeaf1becc0fdbab4e` |
| restored collision board | `0xeaf1becc0fdbab4e` |
| late cyan close over board | `0x4a4c45bf4704e54e` |
| full cyan | `0x2cc687e766578183` |
| small navy opening | `0x8a6d8c6b73f1ac1b` |
| large navy opening | `0xba04247ef217e4ad` |
| full navy | `0xb1a8f948642db583` |
| navy with black bottom 33 rows | `0x99fdbcb6bd37a9c3` |
| stable Multiples Hall | `0x0dc6649d785d392a` |

Static xrefs refine what occurs inside that captured lead-in. Controller
action 3 calls cleanup helper `0x1243E` at `0x12928`; the helper's first call
at `0x12448` waits a literal 15 ms before the shared Wipe and Hall work. That
operation is contained by the measured 39-frame restored-board hold, not an
additional native interval after it. See
`CONTROLLER-CLEANUP-DELAY-STATIC-AUDIT.md`.

The clean Prime wrong-answer terminal has 39 stable restored-board frames and
seven Wipe frames before its Hall. It retains the terminal open-mouth Muncher
record while the newly installed Hall palette changes the record's single
darkest occupied pixel. Its Wipe starts with the measured bottom-edge cyan
close, includes two full-cyan and two full-navy intervals, and ends on a torn
Hall painter. The exact hashes are:

| Visible stage | FNV-64 |
|---|---:|
| restored clean board | `0xe252873073689d8b` |
| bottom-edge cyan close over board | `0x805dc5541d914cdd` |
| full cyan, first and second intervals | `0x2cc687e766578183` |
| medium navy opening | `0x413bd71b36091f2b` |
| full navy, first and second intervals | `0xb1a8f948642db583` |
| torn Prime Hall painter | `0xb87498efff8f0b15` |
| stable Prime Hall | `0x2faa0bed13235713` |

Pixel subtraction reconstructs the torn Prime Hall from the completed live
Hall: board blue remains through y=60, black occupies y=61–73, and seven
white restoration rectangles expose the lower Hall fragments. The empty-list
pair is at y=120/129 in this nonblocking demo Hall, twenty pixels below the
VGA argument-7 user/browser branch; a later live CGA user-Hall capture proves
that four-color argument-7 rendering also uses y=120/129. Native selects the collision or clean sequence
from the terminal feedback kind and exact-hash gates every stable/transient/
Wipe/Hall frame above.

The same lossless stream now closes the two remaining visible Wipes in this
loop. Stable Hall frame 7256 is followed by exactly six transition intervals,
frames 7257–7262, before stable logo frame 7263. Their complete hashes are:

| Visible stage | FNV-64 |
|---|---:|
| Hall under late cyan staircase | `0x3bcbb335a57022d8` |
| full cyan | `0x2cc687e766578183` |
| small black opening | `0x3f124d04d181aa5e` |
| large black opening | `0xf79c067ba1bc34f6` |
| full black | `0xf75309bcac42bb83` |
| black with pale bottom 16 rows | `0x68c619a01cdb6783` |

Stable logo frame 8343 is followed by nine transition intervals, frames
8344–8352, before stable Prime board frame 8353. Those stages are the logo
under a cyan staircase, two full-cyan frames, a large navy opening, three
full-navy frames, and two torn board-painter frames. The corresponding hashes
are `0xa804ae173352194c`, `0x2cc687e766578183` twice,
`0x8887db592e3c9ca1`, `0xb1a8f948642db583` three times,
`0x0dfdc39623c3439f`, and `0x43e28d7e7b30136b`; the stable board is
`0x9c1938a9423716df`. Pixel comparison proves that the first painter sample is
a strict subset of the completed dynamic board: header-rule and outer-border
fragments, a grid prefix, and the first two live cell labels. The second is
the complete board minus only its 374 header-text pixels. Native therefore
reconstructs painter geometry and live labels rather than embedding pixels
from this one Prime board. At `70.086304` fps the Hall-to-logo cost is
`0.085609` seconds and the logo-to-stable-board cost is `0.128413` seconds.

The first stable Multiples Hall occupies 88.148–103.544 seconds (15.395 s),
followed by the logo at 103.629–119.053 seconds (15.424 s). The Prime wrong
feedback is stable at 122.777–127.928 seconds (5.151 s), its Hall follows at
128.584–143.980 seconds (15.395 s), and the logo follows at
144.065–159.489 seconds (15.424 s). Static main-screen cases 1 and 3 create
the same interstitial job with duration 450 and state values 1 and 3.

The first attract Hall frame at
`analysis/original-demo-1fps-frames/0089.png` has whole-frame FNV-1a hash
`0x0DC6649D785D392A`. It differs from an ordinary Hall only in the footer:
`Press a key for Muncher Menu`. The headless native render test now matches
that complete 320x200 frame exactly.

Native now holds terminal feedback for the exact 150 public ticks, preserves
both captured 46-interval restored-board/PCX closing-Wipe sequences, and uses
the executable's ordinary Hall-to-logo pair: the Hall job remains armed for
exactly 450 public ticks, its measured six-frame Wipe opens the already pixel-
matched logo, the logo job remains armed for another 450 ticks, and its
measured nine-frame Wipe paints the next enabled component board. At the
recovered PIT/divider rate each scheduler interstitial is approximately
15.444 seconds; the former native 15.40/15.42 capture-rounded constants have
been removed. A key or pointer press exits from the board, feedback, Hall, or
logo and stops both demo audio streams. For character-producing keyboard
input, native defers the transition from `WM_KEYDOWN` to the following
`WM_CHAR`; that byte is consumed as the one DOS event and cannot affect the
newly exposed title. Because the `+/-` branches precede selector states 1-3,
an enabled repeat-control byte first changes the repeat word and emits its
50 ms PIT feedback, then that same byte exits Demo. Headless tests cover the
physical Space pair on both the board and logo routes and the ordered physical
`+` pair. See `WIN32-INPUT-CONSUMPTION-AUDIT.md`.

Alt+M has an additional controller-state guard. After toggling at `0xD1E8`,
the resident path tests the music flag at `0xD1F3`, compares the selector state
with 2 at `0xD201`, and only then dispatches score event `0x90` at `0xD207`.
Thus the board, feedback hold, and board-to-Hall Wipe resume a newly enabled
score; the state-3 Hall/Hall-to-splash Wipe and state-1 splash/splash-to-board
Wipe do not. Native headless integration locks all four phase boundaries by
counting loop submissions before the same Alt+M event exits Demo.

## Remaining parity boundary

The first captured Multiples-of-5 board and attract Hall are pixel-locked. The
board is now reproduced through the ordinary initializer from the uniquely
recovered `0xF585` time seed, rather than a runtime fixture; a headless test
locks all labels, target, level/tier, player position, answer count, scheduler
counts, and the complete frame hash.
The terminal lifecycle, both contextual board-to-Hall Wipes, both measured
Hall/logo/board Wipes, interstitial durations, enabled-mode selection,
initial 30-tick delay, subsequent 15–44-tick delays, and exact controller
decision tree and original PRNG algorithm are now covered by headless tests.
The interstitial gate independently checks the collision transient, both
restored boards, all six collision and seven clean board-to-Hall frames, both
stable Halls, all six Hall-to-logo frames, and all nine logo-to-board frames,
including all torn painter samples. An ordinary Hall and logo each retain the
literal 450-tick countdown recovered from both main-screen records. The long
post-capture replay is also relocked to those scheduler and synchronous-effect
durations. Behavior after the recording's external interruption remains the
honest parity boundary; no continuation capture exists for comparison.

## Demo exit and the apparent third-board cutoff

An exact event-path audit rejects the former native three-board cutoff. At
image `0x088C1`, the terminal dispatcher tests `DS:596C`; Demo takes the jump
at `0x088C6` and unconditionally posts high-level action 3 at `0x0891B`.
Action 3 paints the Hall and installs the selector-12/state-3 job at
`0x12927`; its literal countdown and reload are both `0x01C2`. Callback
`0x121D4` changes state 3 to action 1, and action 1 installs the same 450-tick
job with state 1 before that callback posts action 2 for the next Demo board.
None of those routines reads a board count.

The high-level action-4 transition at `0x12A3B` is what clears the Demo flag.
The immediate selector-100 action-4 posts in this executable are the non-Demo
terminal branch at `0x088E7` (which Demo skips) and the input/menu action-7
route at `0x12A1B`. The any-key callback at `0x12B4C` posts that action 7.
The generic input dispatcher posts only actions 1, 3, or 5; its any-key branch
calls `0x12B4C`. There is no timer callback, third-board test, or alternate
short Hall record that can manufacture the observed title transition.

Offline `framemd5` analysis of the lossless capture makes the interruption
precise. The third Hall's 2x2 corner checksum begins at `176.911928` seconds;
the ordinary-title checksum begins at `187.541680`, only `10.629752` seconds
later. An uninterrupted 450-tick record at the recovered
`29.1375336`-tick/s rate would end at `192.355926`. The title then follows the
ordinary idle path: its Demo transition begins `30.933291` seconds later and
the first stable board appears after `30.976095` seconds. Thus the capture
contains an input interruption during an otherwise ordinary third Hall; it
does not evidence a three-board state-machine boundary.

Native no longer shortens board 3's Hall or injects a title restart. Every
Demo terminal now executes the same 450-tick Hall, 450-tick logo, and next-
board sequence. A real key or pointer press still exits immediately from any
Demo page and lets the normal title idle timer start a fresh Demo, reproducing
the captured route when that external event is actually supplied. The focused
headless gate explicitly starts from board index 2 and requires board index 3
after the full Hall/logo pair.

The preserved run is now reconciled through the complete first-board route,
the first Muncher collision, feedback/board/Hall/logo lifecycle, and ordinary
construction of the captured second Prime board. An offline recheck of
`original-demo-full-internal.avi` around 61–65 seconds corrects an earlier
actor-identity inference: the left-edge `Troggle!` warning is visible while a
different Reggie is already at the bottom-right. The fixed-record scan assigns
calls 186/187 to that lower-ID waiting job (left edge, row 1), then call 188 to
the higher-ID actor's 95-tick endpoint dwell. This is not an actor-before-
controller exception. The route also includes the vertical-exit terminal
callback before collision and the two demo-controller calls that remain due
during the 21-tick bite. The second-board
initializer begins at preserved PRNG call 271; its measured route moves up,
munches the correct `2`, moves left, and takes the one-in-ten wrong branch on
`1`, followed by the same feedback/board/Hall/logo lifecycle. The next
Equals-30 initializer begins at call 334 and matches all labels, player cell
`(1,1)`, and the complete 320×200 captured frame. The replay then matches the
captured moves through cells 8/9/10/11/5, all five correct munches, both Worker
slots and the entering Reggie, the safe-zone activation at cell 14, the Worker
collision at `(0,5)`, and its 21-tick bite. Static ordering at `0x09DA5` proves
that the terminal callback spends a fresh Troggle dwell roll before `0x109E3`
selects the feedback exclamation; calls 506/507 consequently reproduce the
captured `Aargh`. Page-color boundaries then lock the externally interrupted
third Hall and the ordinary title-idle restart described above. The title
return is retained as capture evidence for any-key exit timing, not as an
autonomous Demo rule.

The same second-board span now supplies a complete live chew oracle. Lossless
frames `8462–8478` show all seven scheduler-visible intervals of the correct
Prime `2`; their complete 320×200 RGB FNV-64 hashes alternate
`0x09ee3b653a7a0c5f` / `0xa2e8dc753a83a4b7` in the recovered
`12,13,14,13,12,13,14` order. The continuous seed-`0xF585` native replay
matches every interval and then the captured 25-point state. Pixel comparison
also proves that the Hall palette's darkest player shade persists through the
logo onto later Demo boards; native now preserves that one-slot palette state.

The board-two evidence is now continuous rather than a collection of route and
chew checkpoints. Global frames 8353–11187 cover the first stable Prime board,
the full short gameplay route and feedback, the restored board, Prime Hall,
logo, all three intervening Wipes/painters, and the first stable Equals-30
board. The source contains 53 logical runs; the seeded native replay emits 51
changed pages and matches 40 presentation-complete source pages in order. A
second-stage pixel audit accounts for every remaining page: thirteen source
runs are torn refreshes or incomplete dirty/painter surfaces, while the eleven
native-only pages are the already exact-hash-gated completed Wipe/painter
stages. The scoped lifecycle therefore carries a measured continuous parity
claim. See `analysis/number-live/gameplay/full-demo-second-board-continuous-
sequence-report.json` and `analysis/number-live/gameplay/full-demo-second-board-
reconciliation-report.json`.

The following Equals-30 gameplay board is likewise continuous through its
restored-board terminal. Global frames 11187–12392 form 118 source runs across
five moves/correct chews, Worker and Reggie arrivals, safe-zone activity, the
Worker collision, all 21 bite ticks, stable `Aargh`, and the 40-frame restored
board immediately before frame 12393 starts the Wipe. The first board-wide
alignment found one genuine native-only terminal: native left the concurrent
bottom Reggie at entry phase 4 (`0xd27819377a45835c`), while the DOS teardown
advances its final visible phase-5 callback (`0x25a5e4cc4a413f32`). Native now
performs that one no-PRNG teardown callback. All 106 completed pages match in
order; the twelve remaining source runs are ten nonuniform refreshes and two
uniform partial-painter composites with exact locked pixel partitions. No
native-only page remains. See `analysis/number-live/gameplay/full-demo-third-
board-continuous-sequence-report.json` and `analysis/number-live/gameplay/full-
demo-third-board-reconciliation-report.json`.

The captured fresh batch's Multiples-of-15 initializer begins at call 507,
ends setup at call 584, and matches complete-frame hash
`0x373EB5DB7AB6534B`. Replaying the external title restart seen in the supplied
recording preserves the standing-only collision rule and reaches the measured
calls 604/605/607 collision/terminal boundary, exact Hall/logo transition, and
the following Factors-of-63 initializer at call 695. That board matches target
63, visible level 7, difficulty tier 6, player `(1,1)`, all thirty labels,
sixteen remaining answers, and hash `0x320cd1496e8cf4b3`.

The continuous Factors audit covers global frames 18570–20527. It reconciles
all 224 native changed pages: 218 exact ordered source pages, four repeated
exact source pages, and two complete callback surfaces partitioning adjacent
dirty-painter captures. All source-only runs are explicitly classified, and no
unexplained native page remains through the final warning board. See
`analysis/number-live/gameplay/full-demo-fifth-board-continuous-sequence-report.json`
and `analysis/number-live/gameplay/full-demo-fifth-board-reconciliation-report.json`.

The headless regression now withholds all external keys and locks uninterrupted
continuation across every later Hall/logo pair. Board indices advance rather
than resetting in native-only three-board batches; seventeen further page,
component-mode, target, visible-level, score, and PRNG-call transitions are
gated through call 1677. This continuation includes the corrected persistent-
Troggle job-ID interleaving and proves the recovered state machine keeps
constructing boards without an invented cutoff.

## Uninterrupted seed-F585 continuation

The former post-interruption evidence boundary is now closed by
`analysis/number-live/attract-continuation/original-number-autonomous-f585.avi`.
The artifact is pinned at SHA-256
`A680FB7A38456F0143378D3BE171249F0D5C97DF8EAF93458A00CC882005C903`,
75,133,426 bytes, 25,600 ZMBV frames, and 365.263947 seconds. Its exact initial
guest state is seed `0xF585`, call 87/state `0x5F26E95C`, level 10, pressure 9,
Factors of 57, and enemy types Reggie/Reggie/Smarty.

The continuous replay reaches the final Prime-board collision at call 850. A
previously omitted terminal scheduler draw is now recovered from two
independent consequences: the source selects `Aargh` at call 853 instead of
native's former `Yikes`, and the following board initializer begins from that
same call. The next board is not another Prime fixture; it is Demo / Less Than
20, level 6/pressure 5, player `(2,1)`, enemy types Bashful/Smarty, with its
complete thirty-expression board ending at call 1004/state `0x42D476E9` and
full-frame hash `0x55761275C6A0305D`.

The final route also exposes one genuine callback-local player page at source
frame 23714. Restoring chew record 13 for that scheduler slot produces exact
hash `0x4876D4BA4445BE25` between the exact surrounding pages
`0x89FD276642CE25E7` and `0x39CA790876F9A30A`. By contrast, source frame 23358
has nine nonuniform doubled blocks and only a five-pixel partial `Demo` glyph
write; frame 23596 is a 42-pixel two-row actor painter fragment at
`(125,66)..(145,67)`; and frame 23608 is a 32-pixel old/new splice with no
unique logical pixels. Those three remain capture-only rather than being
synthesized into the atomic renderer and reintroducing visible flicker.

The inventory aligns 1,363 of 1,434 native changed pages exactly and in order.
`analysis/number-live/attract-continuation/reconciliation-report.json` then
pixel-classifies every residual: all 237 source-only runs are nonuniform
scanout, exact old/new or multi-state partials, localized dirty fragments,
single-rule/thin-band painters, resident-state resurfaces, or adjacent complete
states; all 71 native-only pages are atomic old/new composites, complete
transition/callback surfaces, multi-source composites, or exact source states
seen elsewhere. The six source runs before the first exact page are the pinned
capture-fixture splash-to-board Wipe. No residual page remains unclassified.

The permanent renderer test now executes all 25,700 samples and locks the final
collision phrase/PRNG state, BoardHold, Less Than 20 initializer, record-13
callback, final wrong-answer feedback, Hall, and closing logo. Together with
the static through-call-1677 continuation, this closes the Number attract row's
former material boundary.
