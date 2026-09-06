# Word Munchers attract/demo static audit

This gate combines static evidence from `analysis/WM-unpacked-image.bin` and
`analysis/WM-unpacked.ndisasm`, deterministic in-memory native tests, and the
muted lossless DOS recording `analysis/captures/wm_009.avi`. The recording
contains an organically entered Demo board and key-driven return to title.
`tools/audit_word_attract_capture.py` validates its stable logical regions
against native. Longer controller transitions, feedback/Hall/logo Wipes, and
audio remain static/shared-driver or headless evidence unless cited otherwise.

A second pinned source now covers a complete seeded controller run:
`analysis/word-live/attract/full-headless-seed-8e74.avi` contains 11,518
headless DOS frames, and `tools/audit_word_full_attract_capture.py` compares
them continuously with every changed native page. The guest-memory auditor
`tools/audit_word_live_memory.py` independently reads the resident scheduler
and PRNG state without a visible DOSBox window.

## Controller and lifecycle

The large Word controller begins at load-image `0x11F02`. Its event 2 branch
at `0x1204C` sets the demo flag at `DS:5D74` before calling the normal game
entry point; event 4 clears that flag. Event 3 at `0x11FF8` renders the Hall in
demo context and installs controller state 3 for 450 public scheduler ticks.
Event 1 at `0x11F6E` installs the logo/interstitial state for the same 450
ticks.

The record at `DS:2312` uses global callback selector 12. The global dispatcher
at load-image `0x12902` maps selector 12 to `1191:004A`, load-image `0x1195A`.
That callback switches controller argument 1 to 2, or every other argument to
1, clears `DS:5D78`, and schedules the switched event. This controller is
byte-isomorphic to Number Munchers' recovered attract controller. The measured
shared title-idle delay is 30 seconds; Word's Hall and logo records each carry
the exact literal duration 450.

The demo game initializer at `0x957A` dispatches music class `0x90` before
drawing a level, chooses `random(0,9)+1`, and reserves three Munchers at
`DS:5DAE`. The native route preserves that ordering: music is started first,
then a uniformly selected visible level 1-9 begins with the ordinary Word board
and Troggle/safe-zone scheduler.

When a demo board has no positive records, the branch at `0xAD76` omits the
ordinary board-completion cue 15 and calls the common demo post-game path at
`0x947F`. The first wrong word at `0xA9D5` and the first completed Troggle bite
at `0x9FF6` also force this path whenever the demo flag is set, regardless of
remaining reserves. The native sequence is:

The only call to cartoon selector `0xFD74` is ordinary main-screen action
`0x120F9`. Silent Demo completion therefore consumes none of the selector's
five shuffle draws; the executable-wide call inventory and corrected native
boundary are recorded in `WORD-PRNG-STATIC-AUDIT.md`.

1. after a wrong word or collision, show demo feedback for 150 public
   scheduler ticks; silent board completion enters the next phase immediately;
2. retain the unobscured board for 39 measured shared-driver capture intervals,
   with one preceding collision-actor transient on the bite route;
3. close the board through the type-1 PCX Wipe over seven clean intervals or
   six collision intervals;
4. retain the demo Hall for 450 public scheduler ticks;
5. Wipe from Hall to the Word PCXF `6005` logo over six intervals;
6. retain that logo for 450 public scheduler ticks;
7. close, paint, and open the next board over nine intervals, with the live
   Word board painter reached at interval seven; and
8. begin another random level 1-9 demo with a fresh 30-tick action delay.

The interval duration used by the native presenter is the captured shared
driver cadence `31250 / 2190197` seconds. The counts and aperture shapes come
from the lossless Number Munchers Wipe capture; applying them to Word is backed
by the common executable driver and byte-identical transition payloads, but is
not presented as a fresh live Word capture.

### Recovered shared Wipe path

The former fixed 20-tick restoration phase was an inference, not an executable
record. Word controller action 3 at `0x11FF8` calls `0x11993`; that routine
calls `0x102c:001A` with effect type 1. The loader at `0x102DA` selects the
logical `6051`/`6053` PCXF transition payloads (`6050`/`6052` in CGA) and
dispatches the type-1 effect through `0x1EB0:0004`. Action 1 at `0x11F6E`
passes through the same Wipe before the logo. The actual ordinary next-board
initializer at `0x0A356` ends at presenter `0x10A66`, which calls effect type
2 at `0x10A6F`, clears the screen, calls effect type 1 at `0x10AA3`, and then
calls the dynamic board painter at `0x108C2`. The old `0x0FE2D` attribution
was the non-Demo every-third-board cartoon loader, not a board painter; see
`BOARD-PRESENTER-STATIC-AUDIT.md`.

The Word and Number VGA transition payloads are byte-for-byte identical:

- PCXF `6051`: 3,297 bytes, SHA-256
  `BC20267D3F43CECD6CF80326B57C219EEC11AF3627FE32DF8287F96ABB172E20`;
- PCXF `6053`: 3,297 bytes, SHA-256
  `E28ACB93987B9C0EACEA640C8246B0436F39AC63D39538FAA30E937E6954B4D6`.

There is no 20-tick scheduler record between feedback and the Hall. The native
runtime therefore reuses the measured shared Wipe geometry while composing
Word-owned board, Hall, and logo pages.

Controller action 3 nevertheless contains one shorter blocking operation:
its call at `0x11FF9` reaches cleanup helper `0x11BC4`, whose first call at
`0x11BCE` waits a literal 15 ms. As in Number, that call sits inside the
shared measured 39-frame restored-board lead-in. It is not added after the
46-interval aggregate, which would double-count it. See
`CONTROLLER-CLEANUP-DELAY-STATIC-AUDIT.md`.

The Hall uses the original `Press a key for Muncher Menu` footer. Any key,
character input, or pointer-button input exits the native demonstration,
stops its independent music channel, and returns to the Word title.
Character-producing Win32 keys are consumed as one physical event: key-down
defers, the translated character performs the exit, and neither half reaches
the title menu or its global `+`/`-` shortcut. The headless gate exercises that
two-message route without launching a window; see
`WIN32-INPUT-CONSUMPTION-AUDIT.md`.

## Autonomous player callback

The Word record at `DS:13A2` is byte-for-byte identical to Number Munchers'
selector-5 record:

```text
07 00 05 00 00 00 02 00 1E 00 00 00 1E 00 00 00 00 00 00 00
```

The global dispatcher maps selector 5 to `08A4:074E`, load-image `0x0918E`.
Its decision routine is byte-isomorphic to Number's callback and has this exact
order:

- reload the callback with `random(0,30)+15` ticks;
- if the current signed cell value is positive, emit Space;
- if it is negative, emit Space only when `random(0,10)==0`;
- otherwise draw one initial direction with `random(0,4)` and search clockwise
  through at most four directions for an in-bounds, live, positive,
  non-enemy destination; and
- emit the final selected direction even if the search found no candidate.

The setup at `0xA41C` appends this demo callback after the safe-square and
Troggle records. Its initial countdown is exactly 30 public scheduler ticks.
The native test controls board contents and PRNG seeds to lock both actions and
call counts: immediate correct munch, the one-in-ten wrong munch, busy-action
suppression after consuming only the reload draw, and clockwise Up-to-Right
selection consuming exactly the reload and initial-direction draws.

The ordering remains observable when that callback and a Troggle bite become
due together. The Troggle actor already precedes the Demo record in the
board-start job order. Its tick-21 terminal therefore removes the Muncher
before selector 5 runs; the selector still performs its mandatory
`random(0,30)+15` reload, but cannot consume the later current-cell or
direction draws. Word's native scheduler formerly ran selector 5 one interval
too early at this boundary. A focused headless case now forces the tie and
locks the terminal order as survivor-dwell draw, feedback-phrase draw, then
selector reload draw.

The same test submits two whole scheduler ticks plus a quarter-tick fractional
remainder in one presentation update. The two whole ticks finish the bite;
only the remaining quarter tick ages the newly installed 150-tick feedback
hold, leaving exactly 149.75 ticks. This prevents Win32 presentation cadence
from silently lengthening each collision terminal by up to one display frame.

## Demo-only presentation

The board painter at `0x110D1` draws the literal `Demo` at `(30,6)` in demo
mode instead of the normal level label. The score/reserve painter branches to
`0x10F13`, which centers `Press a key for Muncher Menu` at baseline 187 while
omitting Score and reserve icons. The target-sound switch also preserves its
macron/breve overstrike. Wrong-word and collision feedback moves
the two message baselines from the normal user placement to `y=93` and
suppresses `Press SPACE BAR to continue.`

The native in-memory renderer gates these distinctions directly: demo pages
contain the `Demo` label, no normal Level label or Score/reserve footer, the
centered global Muncher Menu prompt, the shifted feedback text, and no
user-specific Space acknowledgement. PCXF `6005` is the
preserved Word Munchers pale-logo interstitial; title artwork remains PCXF
`6009` in VGA mode.

The `wm_009.avi` slice decodes to 140 frames from seconds 25.5-27.5. Frames
43-93 contain Demo board paint; frames 49-92 are 44 identical stable refreshes.
Native matches the entire captured 320x200 framebuffer with zero pixel
differences and renderer-style hash `0x6426d16846667dac`; the complete header
through y=23 hashes to `0x3cda3059237b16cc`. The top-right bell formerly
classified as a stationary guest pointer is BTMP 6001 frame 3, the target
exemplar paired with `/e/ as in bell`. The report includes that graphic and
classifies the preceding/following partial painter samples rather than treating
them as logical board states.

The player sheet and Hall/logo/title pages share palette index 111 byte-for-byte
with Number. Live first-board Word and Number frames plus the later Number chew
all display this occupied Muncher slot as `414100`; the old first-board
`413D00` output was native-only. Word still models the recovered logical state:
`beginAttractHall()` latches it for the logo/later boards, and a new or exited
Demo resets it. Renderer gates require pixel identity at this slot. See
`WORD-PALETTE-STATIC-AUDIT.md`.

## Attract score

Word's embedded `GSND:2` bank is 2,147 bytes. Stream 16 begins at bank offset
`0x342`, on channel 9, and is a two-section conductor:

```text
09 04 AE 01 AF 01 A6 B4 9C 06
```

It launches the first eight persistent score tracks in this order:

```text
196, 194, 192, 190, 188, 186, 184, 182
```

After `32 * 256` sequencer ticks it launches the second eight:

```text
180, 179, 178, 177, 176, 175, 174, 173
```

After another `32 * 256` ticks it jumps back. The corresponding track channels
are `6,7,8,2,3,4,5,1` in both sections. Unlike Number's finite score parts,
each Word part intentionally loops until the conductor replaces the section.
The decoder therefore runs each part for the conductor's exact bounded section
duration instead of treating that persistence as malformed bytecode.

Fractional sequencer timing gives an exact first-section boundary of 112,459
ms and a complete loop duration of 224,919 ms. The native renderer requires at
least 40 YM3812 writes at the second-section boundary, proving that the second
ensemble is present rather than a duplicated or prematurely stopped first
section. The loop and ordinary demo effects now share one continuously clocked
YM3812: the score owns channels 1-8 while each new cue replaces channel 0.
This preserves shared chip phase and register interaction instead of mixing
two waveforms rendered from separately reset chips. Alt+M retires every channel
without resetting the composite owner and restarts the score only in controller
state 2. The original `0x03E9`
dispatcher also sends a driver-wide
stop before disabling music, so native Alt+M-off now terminates any effect that
was already sounding; ordinary effects remain enabled for subsequent events.
The resident handler at `0xDE9D` tests the music flag and `0xDEAB` requires
state 2 before `0xDEB1` dispatches `0x90`. Consequently the active board,
feedback hold, and board-to-Hall Wipe resume; Hall/Hall-to-logo state 3 and
logo/logo-to-board state 1 do not.

The static walk also exposed five additional resident Word patch records used
by this score (`50`, `69`, `BA`, `DC`, and `F0`) in addition to gameplay keys
`E8` and `E9`. Their exact bytes are recorded in
`WORD-GAMEPLAY-AUDIO-STATIC-AUDIT.md` and embedded in `wordPatchFor`.

## Native headless gate

`munchers_app_headless_test` now verifies all of the following without an
audio device or window:

- the 30-second title-idle transition and random visible level 1-9;
- the initial 30-tick action record and exact selector-5 PRNG/action rules;
- collision-terminal ties with the exact dwell/phrase/selector-reload PRNG
  order and fractional-frame transfer into the 150-tick hold;
- demo HUD and feedback-only composition;
- first-collision and first-wrong terminal routing;
- exact 150-tick feedback plus the 39-interval restored-board hold, six/seven-
  interval terminal Wipes, literal 450-tick Hall/logo holds, six-interval
  Hall-to-logo Wipe, and nine-interval logo-to-board Wipe;
- silent completed-board routing with zero PRNG calls at the
  completion-to-terminal boundary;
- any-key and pointer exits, including the byte-isomorphic ordered `+/-`
  repeat adjustment before the same translated event exits Demo;
- deterministic native full-frame hashes for the collision transient, both
  restored boards, every terminal/interstitial Wipe interval, and the two torn
  dynamic Word board paints, plus four-color confinement for every CGA Wipe;
- exact first-board/clean-terminal/later-board logical palette state with
  pixel identity at live-measured `414100` and explicit
  new-Demo/Hall/Demo-exit latch boundaries;
- continuous single-chip score/cue ownership; exact GSND 1/2 Alt+S/M
  acknowledgements and Alt+M-off's 50 ms delay; sound-off channel-0
  replacement without score loss; Alt+P's music-enabled all-channel stop; and
  non-speaker state-2-only score restoration with explicit state-3/1
  non-resume;
- the exact one-second score-plus-correct-cue signed-PCM hash
  `0xfc21539d1eb4c238`; and
- the exact 112,459 ms section boundary and 224,919 ms conductor cycle.

The pinned `0x8E74` trace also locks the first mixed selector-1/selector-5
boundary that the earlier short slice could not reach. Original entry state 3
uses exactly five vertical or six horizontal callbacks, without a second
endpoint interval. The blue Troggle therefore consumes PRNG call 360 before
Demo consumes calls 361/362 and moves down through the eaten cell. Native now
has that same order. The continuous comparison improved from 316/851 to
606/669 exact eligible native pages in source order. A subsequent 29-pixel
diagnostic isolated the right-entry/player overlap: idle right-entry phase 6
retains the overwritten x=308 rule, while concurrent player phase 4 restores
it on the resident page. Source runs 736 and 263 prove both branches. Gating
that distinction raises the ordered result again to 607/669. Source run 139
then isolates a second callback-local boundary: selector 6 removes a safe zone
before selector 5 starts a chew, but chew tick 0 still presents the old
68-pixel white outline. Tick 1 exposes the removal. Retaining only that
safe-job delta on the first chew page raises the result to 608/669.
Two later intervals establish a broader idle-player/active-Troggle rule. A
safe activation or removal remains on the nonresident selector-6 page for
three public ticks while current actor poses continue advancing. Runs 422–424
retain a removed marker; run 501 suppresses a newly activated marker. Applying
only that safe-painter delta adds four exact pages, removes one native-only
duplicate, and raises the result to 612/668.

A final player/Troggle callback boundary in this pass is likewise a resident
page rather than a logical-state delay. When the state-4 player job clears its
directional terminal pose before a later selector-1 actor repaint, DOS keeps
the old player-owned pixel delta for one presentation while showing the actor
at its current pose. Source run 419 differs from the former native page at
only the 71 top-left Muncher pixels. Restoring only player pixels untouched by
the later actor painter exposes four additional DOS-matching resident pages
across the trace, without changing scheduler state or the PRNG. The ordered
result is now 616/672.

Source run 351 isolates the complementary moving-player case. The state-4
player record reaches its final vertical phase while a Bashful's final
ordinary-movement surface is resident and that actor record is not due. The
player callback repairs only the Bashful source cell's regenerated `sleep`
label (109 white pixels) and shared x=260 rule (29 magenta pixels) over the
actor's blue sweep; the current enemy and player poses remain unchanged.
Restricting that foreground repair to the final player phase, final ordinary
enemy phase, and that enemy's source cell adds exactly one native page and one
exact source match, with no former match lost. The result is 617/673 and the
PRNG endpoint remains `0x42919cb4` after 960 calls.

Two controller-boundary comparisons then close three more stable source
states. Source runs 600 and 657 are byte-identical uniform BoardBlue pages
between the cyan close and Hall repaint. They occur on both the clean and
collision routes, so the shared terminal `Wipe` now clears to BoardBlue before
the Hall painter runs; both source states match without changing the route or
PRNG trace. Source run 595 separately proves that a clean correct
board-completion hold retains terminal player record 13, while the clean wrong
answer continues to retain the open record-12/14 group. Correcting that branch
adds native page 474 / sample 4217 as a third exact match. The continuous result
is therefore 620/674, with the same 960 calls and `0x42919cb4` endpoint.

The next pass closes one more eating-lifecycle state and removes three native-
only presentations. DOS run 651 retains the final open record-12/14 Muncher
through the entire 150-tick wrong-answer explanation; native had fallen back to
standing record 7 until the later board hold. Extending the already-correct
wrong-answer retention through the explanation adds that exact run with no
loss. The Hall-to-logo callback then exposed the port's invented cream-footer-
only page twice. DOS holds the complete black page while its partial logo paint
runs, so native now does the same until the complete logo is ready, eliminating
both one-refresh flashes. Finally, source run 351 holds its exact repaired
player-callback surface for two captured refreshes. Native now keeps that
surface resident until the next scheduler dispatch instead of briefly
recomposing away the `sleep` label and x=260 rule. The then-current auditor
reported 621/671, still at 960 PRNG calls and endpoint `0x42919cb4`.

The player-terminal/new-Troggle boundary is now directionally pinned as well.
At the upward terminal, source run 184 holds the complete selector-4 player
callback for both refreshes before run 185 exposes the newly moving Troggle.
Native previously queued the correct callback once and then displayed a
one-sample, 47-pixel synthetic player hybrid. It now keeps the exact callback
surface resident through the next public dispatch. A later rightward terminal
takes the other captured branch: source run 497 is the retained player/Troggle
composite. Native preserves that exact page rather than applying the upward
rule globally. Source-backed tests pin samples 1384-1386 and 3545-3546 at the
recording's exact refresh cadence. The correction removes one non-exact native
page without dropping any of the 621 exact ordered pages or changing the PRNG
endpoint.

The first remaining two-adjacent-state composite was the same kind of resident
page error at selector-5 movement start. On source run 121 both simultaneous
entering Troggle callbacks update the nonresident surface, so movement state
zero keeps the complete pre-dispatch page through the last two refreshes and
run 122 is the next visible state. Native had captured after the Troggle jobs,
inserting a `new Troggles + old player` page with no source occurrence. The
dual-entry-only phase-zero hold now uses the complete pre-dispatch surface;
samples 869-871 are source-hash gated. Two other phase-zero starts that already
matched DOS remain on their original callback surface. This removes one more
non-exact page without dropping an exact state or changing scheduler/PRNG
ownership.

The next one-refresh composite was the attract-only chew-terminal boundary at
source runs 146/147. Selector 4's retained open-chew pixels had been combined
with a Troggle surface painted later on the same dispatch, even though the DOS
recording advances directly to run 147 and holds that complete state for five
frames. Native Demo now omits that unsupported recombination whenever the
later Troggle callback visibly changed the surface. Samples 1030/1031 and 1035
are pinned to runs 147/148. The ordinary user-game path deliberately retains
the shared queued chew callback and remains independently regression-tested.
This removes a third non-exact page while leaving the exact sequence, source-
only inventory, scheduler state, and PRNG endpoint unchanged.

Three captured arrival boundaries now share a narrowly gated resident-page
rule. Runs 394/396 keep the previous page across a rightward arrival while one
of two ordinary Troggles is moving. Runs 496/497 do the same immediately before
a solitary Reggie departs; the following clear/new-move dispatch now exposes
run 497 directly instead of first queueing a stale selector-4 page. Runs
561/562 retain the preceding page across a downward arrival while an exiting
Troggle and a waiting entering Troggle straddle selector 4. Native formerly
inserted four complete composites that never occur in source ordering. Seeded
hash gates cover samples 2848-2851, 3541-3546, and 3965-3967. These corrections
remove four non-exact pages without removing any exact source state.

The final seven pages in that denominator were not part of a shared route.
Source run 767 is the last restored Demo board through frame 10,986. Runs
768/769 are nonuniform title-repaint fragments after an external Demo-exit
key, and run 770 is the stable Word title page (RGB SHA-256
`058421A35534EE7D3AC98B6361135DFEB118ECCDBDADF27A0298037024649549`).
The native dump deliberately receives no key and continues the autonomous
controller, so its pages 657-663 cannot be parity-compared with that title
route. The auditor now pins this boundary, excludes exactly those seven pages,
and reports 621/657 over the genuinely shared route. This is an evidence-scope
correction, not a runtime match or a relaxed exact-pixel comparison.

The residual audit also separates eight incomplete DOS dirty callbacks from
stable logical poses. Runs 104, 160, 188, 228, 296, 392, 480, and 528 each
contain at least one uniform 2x sample, but every pixel unexplained by the
exact states on both sides lies on one horizontal or vertical board-rule
scanline. They are now reported as `single-rule-partial-refresh` rather than
left unclassified or synthesized in the double-buffered native renderer.
Source run 417 is the corresponding mixed case: all but eight pixels are
explained by nearby complete native states, and the eight residual magenta
pixels are one vertical rule segment at x=212/y=148-155. It is classified as
`multi-native-state-plus-single-rule-partial-refresh`. Source run 552 is a
one-presentation resurface of an older resident page: its full framebuffer is
byte-identical to prior native pages 471 and 473, between the exact closed
terminal pages at runs 551 and 553. It is classified as
`prior-native-state-resurface`; synthesizing a new chew tick there was tested
and rejected because it invented 38 unsupported native replays elsewhere.

The same release console run also retains the existing asset, controller,
gameplay, Options, persistence, Hall, scene, and audio gates.

## Completed parity boundary

The autonomous callback, controller events, resource IDs, event durations,
demo-only branches, score bytecode, Wipe call chain, and native routes are
closed at the static/headless level. The organic short run corrected the
missing footer and target-sound overstrike; the seeded long run covers
autonomous play, feedback, controller Wipes, and later boards continuously.

The report keeps two claims separate. Exact ordered coverage is 621 of 657
eligible native pages and remains transparently false as an all-pages exact
sequence. Atomic double-buffer parity is true: all 126 intervening source-only
runs are classified as 79 nonuniform 2x refreshes, 21 two-state partials, 14
multi-state partials, eight single-rule refreshes, one thin-band refresh, one
mixed multi-state/rule refresh, one prior resident-page resurface, and one
adjacent complete state. All 36 unmatched native pages are also exhaustively
classified: 20 two-adjacent-state composites, 14 pixel-exact multi-source-state
composites, one exact source state observed outside the selected LCS ordering,
and one complete chew pose inside a measured source partial repaint. No source
or native residual is unclassified.

Those completed native pages are the intentional result of presenting one
atomic framebuffer instead of exposing the DOS painter's scanout damage. The
passing auditor does not hide that exception: it retains
`all_eligible_native_pages_exact_in_order=false` while separately requiring
`atomic_double_buffer_parity=true`. This applies the same no-flicker completion
rule as the Number audit and closes the Word attract row. Pointer translation
and synchronized physical audio remain tracked by their independent parity
rows rather than being counted twice here.
