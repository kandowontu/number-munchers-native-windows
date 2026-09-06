# Word Munchers Troggle and safe-zone static audit

This gate began with `analysis/WM-unpacked-image.bin` and
`analysis/WM-unpacked.ndisasm` and is exercised headlessly. It is now also
checked against the lossless DOS recordings `analysis/captures/wm_005.avi`
and `analysis/captures/wm_011.avi`.
The repeatable extractor/auditor in `tools/audit_word_collision_capture.py`
validates 631 decoded 320x200 frames covering a complete rightward Reggie
crossing, the full player collision, the feedback strip, and board recovery.
The live evidence corrected three earlier interpretations: an ordinary moving
actor cycles directional records, the collision bite advances one record on
every scheduler tick, and an edge arrival is hidden during warning/its first
two callbacks before a board-interior-clipped pose sequence. This remains a
visual/timing audit rather than a physical-audio comparison.

## Recovered pressure tables

The Word data segment begins at load-image offset `0x22C80`. The complete
pressure block is byte-for-byte identical to Number Munchers:

| Purpose | Load image | `DS` | Tiers 0 through 11 |
|---|---:|---:|---|
| Concurrent Troggles | `0x23E80` | `1200` | `1,1,1,2,2,2,2,3,3,3,3,3` |
| Base arrival ticks | `0x23E98` | `1218` | `300,300,270,270,240,210,180,150,120,90,60,30` |
| Move callback ticks | `0x23F28` | `12A8` | `3,3,3,3,2,2,2,2,1,1,1,1` |
| Maximum safe jobs | `0x23F8E` | `130E` | `2,2,2,2,2,2,2,1,1,1,1,1` |
| Initially active safe jobs | `0x23FA6` | `1326` | `1,1,1,0,0,0,0,0,0,0,0,0` |

The five-species selection weights at load image `0x23EB0`, `DS:1230`, are:

| Tier | Reggie | Worker | Bashful | Helper | Smarty |
|---:|---:|---:|---:|---:|---:|
| 0 | 100 | 0 | 0 | 0 | 0 |
| 1 | 90 | 0 | 10 | 0 | 0 |
| 2 | 80 | 0 | 10 | 10 | 0 |
| 3 | 70 | 10 | 10 | 10 | 0 |
| 4 | 60 | 10 | 10 | 10 | 10 |
| 5 | 50 | 15 | 10 | 15 | 10 |
| 6 | 45 | 15 | 15 | 15 | 10 |
| 7 | 40 | 15 | 15 | 15 | 15 |
| 8 | 35 | 15 | 20 | 15 | 15 |
| 9 | 30 | 15 | 20 | 20 | 15 |
| 10 | 25 | 20 | 20 | 20 | 15 |
| 11 | 20 | 20 | 20 | 20 | 20 |

Every row sums to 100. The pressure index is stored at `DS:5EBC`; the
level-advance path at image `0x0A35C`-`0x0A367` increments it and saturates at
11.

## Board-start lifecycle and player spawn

The new-board caller invokes board generation at `0x0A386`, clears safe flags
at `0x0A392`, installs safe-zone jobs at `0x0A3A4`, initializes the player at
`0x0A3A8`, and installs the persistent Troggle slots at `0x0A3AC`.

Player initialization at `0x096E1` draws the column first as
`random(0,4)+1`, then the row as `random(0,3)+1`, placing the Muncher in one
of the twelve interior cells. It clears that cell's signed board record and
does not reject a safe cell. Safe-zone selection is the separate routine at
`0x0952B` and rejects cells already marked safe.

Safe initialization begins at `0x09895`. It reads the initially-active table
at `0x0990F` and the maximum-job table at `0x0996C`. Each job receives a
period of `random(0,210)+210` scheduler ticks and reuses that period for later
callbacks.

## Troggle job lifecycle

Enemy initialization begins at `0x09ABC`. It reads the concurrent-count table
at `0x09ADD`, chooses a species from the weighted row at
`0x09AEB`-`0x09B30` using `random(0,100)`, and schedules its next arrival at
`0x09B7F`-`0x09C0A` as `random(0,90)` plus the pressure-tier base. Move jobs
read the interval table at `0x09DD1` and `0x0AAA3`. The rearm path around
`0x09EF1`-`0x09F75` removes the departing actor and schedules a fresh
`random(0,90)+base` delay.

The shared engine block from approximately `0x08B28` through `0x0ADxx`
retains the Number Munchers rules: 96/192 live-player pixel thresholds for
steering, safe-cell avoidance and retry, edge entry/exit, species-specific
cell trails, Troggle-overlap freezing and cannibal biting, and player
collision. Initialization at `0x099A4` selects record 12; the state-5 callback
at `0x0A964` advances elapsed time and rearms the next absolute deadline once
per scheduler tick through `0x0AA3A`/`0x0A6BC`. The BTMP record cycle
`12,13,14,13,12,13` therefore repeats across all 21 ticks; it is not a set of
progressively longer holds. Records 12 and 14 are pixel-identical in all five
species sheets, so the live result visibly alternates the two open/closed
poses on every tick. Reserve loss remains deferred until tick 21, and recovery
remains input-locked until the selected biter moves clear.

Word's protected-cell loop is also explicitly unbounded: `0x09D3D` jumps to
`0x09C63` after each rejected direction. The native Word controller no longer
has its synthetic 32-attempt terminal. Shared exhaustive PRNG evidence proves
a 24-draw maximum with the reachable two-safe-zone invariant, and Word's own
37-draw structural fixture prevents that cap from returning. See
`TROGGLE-SAFE-RETRY-STATIC-AUDIT.md`.

The shared player-collision predicate at `0x09445` queries Muncher job ID 4
and requires returned scheduler state 4 before comparing cells. Movement state
3 and chew state 5 are therefore immune to an enemy endpoint; recovery state 6
is also excluded. Native now applies that exact standing-only gate, while the
player movement terminal still catches a Troggle already resident at its
destination. Headless tests cover moving, chewing, and standing endpoints.

The ordinary pose writes and callback-owned animation are also retained. The
edge initializer at `0x09064`-`0x09106` chooses first directional records
`0/3/6/9`; movement setup at `0x09CA4`-`0x09CCE` begins on third records
`2/5/8/11`; and the endpoint initializer at `0x0A782`-`0x0A7B5` chooses dwell
records `1/4/15/10`. Word's BTMP 1007-1011 tables each contain 16 records,
with record 15 naming the same rectangle as down-facing record 7. The live
rightward Reggie crossing then proves the six painted phases are records
`5,4,3,4,5,4`, corresponding to the shared callback-local cycle `2,1,0,1`
within a direction group. The `wm_011` top-edge entry refines the arrival path:
the warning-stage actor and first two interpolation callbacks are invisible;
phases 2–5 paint down-facing records `7,8,7,6` through the board-interior clip,
then endpoint dwell selects record 15 (the record-7 alias). Ordinary interior
movement advances only when its callback advances, not from an unrelated
global render clock.

The independently decoded Number `nm_000` left-edge arrival supplies the
horizontal endpoint that the vertical Word run cannot expose. Its phases 2–6
use callback-local offsets `1,2,1,0,1`; the final middle pose is identical to
the following right-facing dwell. Both runtimes therefore use the bounded
five-entry arrival table, while vertical paths consume only its first four
elements. See `analysis/number-live/gameplay/nm000-entry-report.json`.

Number `nm_001` supplies the right edge. It proves the viewport reaches x=309,
not x=307: entry overwrites the x=308 grid line and x=309 gutter while leaving
the x=310 outer outline intact. Phase 6 retains the blue grid column after the
actor box has moved left; the dwell callback restores it. Word uses that same
measured asymmetric boundary when the player job is idle: seeded source run
736 exactly matches native page 632 with the complete 29-pixel column still
blue. The continuous Word trace also exposes one callback-local exception.
When a leftward player movement reaches phase 4 during the entrant's held
phase 6, source run 263 restores x=308/y=117..145 before dwell; those 29 pixels
were the only difference from old native page 221. Native now gates both the
idle retained-damage page and the concurrent-player restored-rule page. See
`analysis/number-live/gameplay/nm001-right-entry-report.json`.

The seeded Word run also resolves a safe-zone/player boundary that is not a
logical scheduler discrepancy. At native sample 1013, selector 6 has already
removed row-1/column-2 from the safe array and selector 5 has started a correct
chew. DOS source run 139 nevertheless retains the old 68-pixel white safe
outline on chew tick 0; every other pixel equals the corresponding native
state. Chew tick 1 presents the completed removal. Native now keeps only the
safe callback's old pixel delta on that first chew resident page, without
delaying the safe job, chew state, sound call, or PRNG stream.

Two more intervals distinguish that player-owned boundary from ordinary
Troggle ownership. With the Muncher idle and at least one actor job moving, a
selector-6 activation/removal remains on its nonresident page for three public
ticks. Source runs 422–424 retain the old row-2/column-3 marker after logical
removal; run 501 keeps the new row-1/column-1 marker absent after logical
activation. Native stores the before/after safe delta, overlays its old pixels
on each current actor pose for exactly those three ticks, and then exposes the
already-current safe array. This adds four exact states without freezing an
actor, changing a deadline, or losing any prior exact page.

The next isolated attract boundary has the same callback-local character.
After an idle state-4 player record clears its directional terminal pose, a
later selector-1 actor record may repaint elsewhere on the board. Source run
419 retains the prior top-left Muncher delta while already showing the later
actor pose; the old native page differed at only those 71 Muncher pixels.
Native now keeps the prior player-owned pixels that the actor callback did not
touch for one presentation tick. Five source-only states leave the
unclassified bucket, four additional resident pages match exactly, and no
PRNG call, logical position, or actor deadline changes.

One final-phase player callback later exposes a different portion of the same
resident actor model. A row-2 Bashful has already regenerated `sleep` in its
source cell and reached the last ordinary rightward movement surface while
the Muncher reaches the last upward player phase. The player job is due but
the actor job is not, so source run 351 repairs the 109 white label pixels and
29 magenta x=260 rule pixels over the blue actor sweep while retaining both
current poses. Native now applies that repair only to the eligible actor's
source cell; broader foreground replay was rejected because it created 97
source-unobserved pages. The narrowed rule adds exactly one ordered match and
no native-only page.

The full Number Demo supplies the bottom edge with Bashful rather than
Reggie. Its four completed upward phases reach through y=177, retain the
overwritten y=176 grid segment, preserve the y=178 outer outline, and restore
the complete rule only at dwell. It also measures Bashful's active-board
highlight as `5DBEFF`. Word's byte-identical actor resources and board painter
now have a separate headless gate for those exact bounding boxes, palette
pixels, rule damage, and restoration. See
`analysis/number-live/gameplay/full-demo-bottom-entry-report.json`.

The same source's third board provides a right-edge Worker during a player
chew. Its eight complete states directly establish Worker's `CB5DFF`
active-board highlight and all entry phases/dwell. Word shares and gates the
corrected palette path, phase-1 entry clock, and bounded callback-frame queue.
The shared headless gate constructs the terminal-chew/phase-3 boundary and
requires the queued retained-chew framebuffer before the final record-13
state; Number's live replay supplies the two exact source hashes. See
`analysis/number-live/gameplay/full-demo-worker-entry-report.json`.

The following Number frames 11940–11992 add the complete Worker collision
state corpus. Word gates the stationary pre-bite surface, first closed bite on
the pre-safe resident framebuffer, all 21 logical state-5 ticks, and the final
physical-slot abort before a later actor record. Number's corrected terminal
retains the concurrently entering Reggie at phase 4 and matches the stable
`Aargh` full-frame hash. The shared physical-slot dispatcher now queues the
callback-local page before later Reggie repaints and retains the clipped
phase-1 surface, so all 25 complete source runs match in order. Two torn source
runs are excluded. See
`analysis/number-live/gameplay/full-demo-worker-collision-report.json`.

Number frames 15899–16390 add Bashful's complete approach and state-5 collision
as shared evidence. Word gates all four top-entry poses plus Bashful's exact
stationary/open/closed actor boxes for all 21 ticks and restores terminal dwell
record 15 when collision feedback text clears, rather than retaining bite
record 12. Number independently locks all 35 presentation-complete full frames
across the Worker/Bashful/Reggie approach, bite, `Aargh! ... Trogglus timidus.`,
and the following collision board. Four torn refreshes and two strict scanout
splices remain capture-only. See
`analysis/number-live/gameplay/full-demo-bashful-collision-report.json`.

The full Number Demo closes two shared Reggie-on-Reggie bite/terminal paths.
The first contains a right-moving biter, one complete pre-bite overlap, all 21
alternating bite ticks, and the terminal frame. The shared state-5 terminal
passes literal direction 2 to the dwell initializer, so the actor paints record
15 without replacing its retained rightward heading. `WordGame` stores those
two actor properties independently; its headless fixture requires direction 1
plus dwell record 15 after victim retirement. Number's seeded replay supplies
23 exact source full-frame gates. See
`analysis/number-live/gameplay/full-demo-cannibal-report.json`.

The second bite at row 4, column 5 supplies another 22 exact source states: all
21 bite callbacks and the terminal repaint. Concurrent player work changes the
last two callbacks and proves that the earlier player-job surface is presented
before the later biter job. Word's paired fixture prepares the same callback
tie and requires exactly that queued player page followed by the final biter
surface. See
`analysis/number-live/gameplay/full-demo-second-cannibal-report.json`.

The next row-0/column-2 bite overlaps an upward Muncher walk and is now a
complete shared presentation gate. Its 30 uniform Number source runs are
measured exactly; pixel partitions classify three as incomplete dirty paints,
leaving 27 complete callback pages. Number presents exactly those 27 pages in
source order with no extras. The shared retained board-cell/page scheduler
preserves selector-5 phase 0, idle/terminal player callbacks, intermediate
player-cell paints around the biter, and the terminal leading movement strip;
Word independently gates the same retained endpoint-cell ordering and final
held page. See
`analysis/number-live/gameplay/full-demo-third-cannibal-player-walk-report.json`.

The immediately following Number interval adds the first complete shared
species trail and exit. Bashful regenerates bottom-left `80` to `150`, then
moves left in five captured positions before terminal removal. Nine uniform
framebuffers cover the two preceding player poses, the phase-1 Bashful drawn
over retained player pixels, the complete phase-1 repaint, phases 2–5, and the
terminal label reveal; one 12-block refresh is torn. `WordGame` has a paired
headless fixture using the same Bashful path. It requires immediate ordinary
phase 1, an unchanged left screen margin, no regenerated payload pixels until
terminal removal, and the retained-player callback framebuffer followed by
the complete new player frame. See
`analysis/number-live/gameplay/full-demo-bashful-trail-exit-report.json`.

The next Number trail closes two more byte-identical presenter edges. Demo
selector 5 installs a player move after the player job, so logical phase 0
must retain the pre-action framebuffer until the next player callback. Later,
the complete standing repaint from a player terminal is visible before a due
Reggie job regenerates `207` to `65` and moves right. `WordGame` now uses the
same phase-0 hold and callback queue. A focused Word fixture requires both,
while Number's seeded replay supplies 13 exact complete source frames through
all six Reggie positions and dwell. The uniform-2× partial phase-2 repaint and
two nonuniform source refreshes are excluded from native presentation. See
`analysis/number-live/gameplay/full-demo-reggie-trail-report.json`.

Bashful's next `150 -> 220` in-board trail adds a second shared-species
confirmation: pre-trail dwell, all six rightward phases, and dwell match eight
more complete Number framebuffers. Word uses the same gated actor/trail path.
See `analysis/number-live/gameplay/full-demo-second-bashful-trail-report.json`.

The immediately adjacent Number interval adds 14 more complete shared-engine
matches: a later six-phase Reggie crossing/dwell and all five phases of
Bashful's upward top-right exit through terminal repaint. Two nonuniform
source refreshes stay capture-only. Word's paired headless fixture requires
the vertical exit to remain below the header, cover the regenerated payload
through phase 5, and reveal it only after actor removal. See
`analysis/number-live/gameplay/full-demo-later-reggie-top-exit-report.json`.

The first-Demo frames 4090–4161 add a simultaneous shared-scheduler gate:
Reggie regenerates `232 -> 3` and exits right while the Muncher completes all
seven chew callbacks. Number matches nine complete full-frame source states;
one uniform partial dirty repaint and one nonuniform refresh remain
capture-only. A paired Word fixture requires the same due-enemy callback to
compose directly with the current chew pose and forbids a synthetic
enemy-only intermediate framebuffer. See
`analysis/number-live/gameplay/full-demo-chew-right-exit-report.json`.

The isolated later Factors-of-63 Number window adds shared Helper coverage.
Two Helpers move simultaneously on different axes and clear their source `3`
cells while the Muncher moves down. Number matches thirteen complete full-frame
source states, including three exact resident-surface player callbacks.
`WordGame` now replays those same thirteen state tuples with Word records,
routes the shared compositor through its production scheduler, and requires
the horizontal/vertical phase numbers, endpoint dwells, `D3FF5D` Helper pixels
in both actor boxes, distinct framebuffers, and both destructive trails. The
only intervening source run is an exact horizontal scanout splice—matched run
6 above logical row 81 and matched run 8 below it—so it remains capture-only
raster tearing. The post-divergence window is not cited as a
continuous replay. See
`analysis/number-live/gameplay/full-demo-dual-helper-report.json`.

Frames 20063–20187 add a second isolated shared Helper gate. Number matches all
twelve uniform source framebuffers while a row-4/column-5 Helper erases `7`,
traverses all five clipped bottom-exit phases, and disappears as the Muncher
moves upward through phases 1–4, its endpoint record, and the next standing
callback. `WordGame` gates the same twelve tuples, bottom viewport, destructive
trail, terminal removal, and zero pixels below the board. See
`analysis/number-live/gameplay/full-demo-helper-bottom-exit-report.json`.

The callback geometry is likewise exact. Horizontal movement advances
through six 8-pixel phases and vertical movement through five 6-pixel phases.
Entry consumes exactly that axis-specific callback budget; only the captured
vertical-exit timer includes an additional terminal callback that holds the
actor at its endpoint. A pinned `0x8E74` guest-memory trace confirms the
distinction: the first arrivals enter selector-1 state 3 at PRNG call 277 and
reach their dwell draws at call 281, while a synthetic extra entry interval
eventually reversed calls 360-362 between the Troggle and Demo records.
`WordGame` derives its painted phase from the full installed timer and uses
the axis-specific divisor. Entry
clips against `(21,27)..(309,177)`: top and left grid rules remain protected,
while right and bottom arrivals may overwrite x=308/x=309 or y=176/y=177 up
to the preserved x=310/y=178 outer outlines. The first completed visible
top-edge state is vertical phase 2. The lossless crossing matches the
six native cell-region hashes
`8a6ac96bd4d361cb`, `55644716910f3d5c`, `d53d0bb11b280463`,
`643ce67de4ed616e`, `f6ea119e906493a3`, and `0db2a3f1d70b456b`.
It also establishes the original dirty-rectangle order: the player repairs
cell foreground first, the later Troggle callback clears the swept actor box
opaquely in board blue, then paints the actor over it. Native reproduces the
stable logical frames with a double buffer; the single recorded partial dirty
refresh is retained as host-presentation evidence rather than synthesized as
a native tear.

The Word executable supplies the associated feedback strings
`Yikes`, `Oops`, `Aargh`, `Oh, Oh`, and `Rats`, species labels
`normalus`, `laborus`, `timidus`, `assistus`, and `smarticus`, and the
collision text `! You were eaten by a` / `Trogglus`.

## Native headless gate

`WordGame` now installs the jobs in recovered order, preserves three
persistent enemy slots and two safe-zone job records, paints Word BTMP
1007-1011 actors and safe-zone brackets, and uses the common PIT-derived rate
`1,193,182 / (0x0555 * 0x1e)` ticks per second. The runtime implements the
recovered trails against the active Word board pools:

- Smarty restores the saved cell;
- Reggie and Bashful preserve a saved blank, otherwise regenerate the cell;
- Worker always regenerates the cell;
- Helper clears the cell.

Per-cell regeneration deliberately consumes the normal two Word-generator
PRNG calls and reuses the current correct and distractor pools; it does not
rebuild a new tuple or whole board.

The shared board-live routine at `0x0AD50` scans all 30 signed records and
returns false after owning the no-positive-cell terminal. Both relevant
callers obey that return directly. Ordinary state-4 movement applies the
species trail at `0x09C24`, tests `0x0AD50` at `0x09C2A`, and jumps to the
callback return at `0x09DE9` on false. Safe-zone activation applies the trail
at `0x0A5AB`, tests the same predicate at `0x0A5B1`, and exits at `0x0A607`
instead of rearming the old slot or submitting streams 11 and 4. Native now
propagates this Boolean through both callers rather than inferring continuity
from `WordGamePage::Playing`; that page value is insufficient because an
ordinary direct advance constructs a fresh board and then returns to Playing.

`munchers_app_headless_test` enforces every pressure-table value and weight
row, level-1 job counts, the interior random spawn and cleared spawn cell,
safe/arrival bounds, the 90-tick warning transition, safe and warning pixels,
an invisible pending actor, every completed top-edge entry/dwell frame, and
the thirteen selected dual-Helper movement/trail state tuples and production
resident-surface player-callback route, the twelve paired Helper bottom-exit/
upward-player tuples and below-board clipping,
the standing-only collision gate, tick-21 deferred reserve loss, recovery input lock, and all five trail
rules including PRNG consumption. Forced last-positive Helper cases exercise
both movement and safe-zone origins: each must advance exactly once, leave
stream 15 as the sole terminal cue, suppress the old callback tail, and match
a direct terminal's post-generation PRNG state. That false result also aborts
the enclosing board-job pass, and the test requires the newly installed safe
and Troggle timers to equal a direct-completion baseline instead of aging them
on the destroying tick. The public scheduler tick is
charged before dispatch, preserving the new-board accumulator reset at zero
instead of creating a one-tick startup delay; Word's shared dispatcher
decrements at `0x15340`, reloads at `0x1535C`–`0x15372`, and invokes the
callback only afterward at `0x153AE`. The Demo collision gate additionally forces
a selector-5/tick-21 tie: the survivor dwell and phrase precede the selector's
reload-only draw, and a quarter-tick frame remainder immediately ages the new
150-tick feedback hold. The application test also locks all four ordinary
warning/entry, movement, and dwell record mappings plus the down-dwell
record-15 alias, all five vertical and six horizontal movement phases, and
the entry axis budget plus the vertical-exit endpoint hold. Its native collision fixture matches all
six stable live approach hashes and all 21 live bite ticks. The bite uses
repeating records `12,13,14,13,12,13`; because records 12 and 14 share pixels,
the exact captured cell hashes alternate `0x2cc4ae9134dff1ee` and
`0xe98dda1e9e05c039`. The measured 51-frame bite is 0.727674 seconds versus
0.720720 seconds for 21 original scheduler ticks. The matching Number
recording independently produces the same two pose hashes and a
0.713406-second measurement. Both audit reports are valid. Gameplay Reggie red
is also mapped through the live board palette (`FC2020` source to `FF5D5D`
display), correcting the 24 pixels that previously retained the raw sheet DAC
value.

The board initializer's first-free-slot insertion order is also byte-isomorphic
with Number: safe-zone records, player ID 4, Troggle slots, fixed mask-1
controllers, then the optional selector-5 Demo record. Word now divides every
coalesced host update into original public ticks and dispatches the effective
mask-2 order `safe -> player -> Troggle -> Demo`. Each Troggle's states 1–6
remain on one persistent selector-1 job, so mixed waiting/warning/actor ties
are now dispatched by ascending job ID rather than by two native phase
batches. The headless gate covers the calls-186–188 PRNG ownership boundary, a
Troggle endpoint during protected state-5 chewing, the seventh wrong-chew tick
reaching safe jobs but not later Troggle jobs, and the later-record tails after
both movement-terminal and Troggle-endpoint collisions. The endpoint gate also
requires a non-selected collocated state-6 actor to remain inert while the next
unrelated waiting job consumes its normal warning draws. See
`GAMEPLAY-SCHEDULER-STATIC-AUDIT.md` for the paired template addresses and
scanner control flow.

## Completed parity boundary

The captured level-1 rightward Reggie trajectory, vertical warning/top-edge
entry, safe-zone changes, 21-tick player bite, collision-feedback strip, and
post-Space board recovery provide direct Word visual/timing anchors in
`analysis/word-live/gameplay/wm011-report.json` and
`analysis/word-live/gameplay/collision-avi-exact/report.json`. The uninterrupted
Word attract run adds multi-actor entry/movement/chew, feedback, terminal Wipes,
and later-board interleaving; its remaining source/native pages are now fully
classified under the atomic no-flicker gate.

The executable proves that Word and Number use byte-isomorphic job templates,
state selectors, actor records, steering/trail branches, collision/cannibal
loops, safe-zone callbacks, and scheduler order. Number's larger lossless
corpus therefore supplies independent live pixels for all four entry edges,
Worker/Bashful player collisions, Reggie-on-Reggie cannibalism, all five trail
effects, Helper destruction, Bashful/Reggie crossings and exits, concurrent
player/Troggle callbacks, and terminal dwell restoration. Both runtimes gate
the same recovered tuples while Word independently gates its word-regeneration
PRNG effects, resource IDs, palette, clipping, and retained framebuffer order.

This is branch-complete evidence rather than an assumption that one Reggie
capture represents every species. Every species-specific branch, edge budget,
trail, safe-zone origin, cannibal/player collision, Demo tie, and last-positive
board terminal has an executable-backed deterministic test, and each shared
presentation primitive has a live full-frame anchor. CGA conversion and audio
output remain in their dedicated rows; they no longer keep the Word
Troggle/safe-zone behavior row partial.
