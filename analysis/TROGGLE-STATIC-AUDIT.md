# Original Troggle difficulty tables

This note records the executable evidence used by the native enemy scheduler. It is a static audit of the preserved unpacked DOS load image, not a design inferred from the manual.

## Address mapping

The game's data segment begins at load-image offset `0x27340`. The mapping is independently anchored by the HUD string passed as `DS:150d`, which occurs at load-image offset `0x2884d`, and by direct references to the level (`DS:5868`) and reserve-Muncher (`DS:5998`) variables in the gameplay segment.

The original keeps a zero-based pressure index at `DS:5ab2`. It begins at the selected starting tier, increments once per completed board, and saturates at 11 (`NM-unpacked.ndisasm` at `0x089c7`–`0x08a0f` and `0x09789`–`0x0979e`). The visible level at `DS:5868` continues increasing.

The adjacent score table at `DS:0654` is also indexed directly by the visible level in `0x07e97`–`0x07ebe`: levels 1–16 award `5, 5, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70`, and the fallback for level 17 onward is 75. This corrected the earlier native curve that incorrectly held levels 12–18 at 50.

## Recovered tables

All values below are little-endian words from `NM-unpacked-image.bin`.

| Visible level / pressure tier | Concurrent Troggles `DS:058e` | Arrival base ticks `DS:05a6` | Reggie | Worker | Bashful | Helper | Smarty | Move-callback interval ticks `DS:0636` |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 300 | 100 | 0 | 0 | 0 | 0 | 3 |
| 2 | 1 | 300 | 90 | 0 | 10 | 0 | 0 | 3 |
| 3 | 1 | 270 | 80 | 0 | 10 | 10 | 0 | 3 |
| 4 | 2 | 270 | 70 | 10 | 10 | 10 | 0 | 3 |
| 5 | 2 | 240 | 60 | 10 | 10 | 10 | 10 | 2 |
| 6 | 2 | 210 | 50 | 15 | 10 | 15 | 10 | 2 |
| 7 | 2 | 180 | 45 | 15 | 15 | 15 | 10 | 2 |
| 8 | 3 | 150 | 40 | 15 | 15 | 15 | 15 | 2 |
| 9 | 3 | 120 | 35 | 15 | 20 | 15 | 15 | 1 |
| 10 | 3 | 90 | 30 | 15 | 20 | 20 | 15 | 1 |
| 11 | 3 | 60 | 25 | 20 | 20 | 20 | 15 | 1 |
| 12+ | 3 | 30 | 20 | 20 | 20 | 20 | 20 | 1 |

The five weights occupy 10 bytes per tier at `DS:05be`; every row totals 100. The selection routine at `0x08f1e`–`0x08f82` draws `random(0,100)` with an upper-exclusive bound and subtracts the row weights in numeric resource order. The species pointer table at `DS:1284`, used by the collision-feedback routine at `0x10ac2`, establishes that order without relying on artwork guesses:

| Numeric type | BTMP | Display name | Feedback species |
|---:|---:|---|---|
| 1 | 1007 | Reggie | `normalus` |
| 2 | 1008 | Worker | `laborus` |
| 3 | 1009 | Bashful | `timidus` |
| 4 | 1010 | Helper | `assistus` |
| 5 | 1011 | Smarty | `smarticus` |

This proves the non-linear user-facing unlock order that the earlier native heuristic missed: Bashful appears at level 2, Helper at level 3, Worker at level 4, and Smarty at level 5.

The arrival scheduler at `0x08fb2`–`0x09037` adds upper-exclusive `random(0,90)` to the tier's base tick count. The timer driver programs PIT channel 0 with divisor `0x0555` at image `0x08b8`–`0x08c6`, and its interrupt handler advances the public scheduler clock every `0x1e` interrupts at `0x07f2`–`0x0807`. Using the 1,193,182 Hz PIT input gives about 29.1375 scheduler ticks per second. The demo controller, Troggles, and safe zones all have job mask 2 and therefore share this clock. A level-1 arrival spans about 10.30–13.35 seconds; the recorded warnings agree within board-start and capture boundaries. The movement callback at `0x091ef`–`0x0921a` loads the tier value from `DS:0636` as the actor job's callback interval. The preserved machine follows the low-speed branch at `0x0c9f8`: each callback advances 8 horizontal or 6 vertical pixels, so a 48×30 cell takes six horizontal callbacks or five vertical callbacks. The values 3/2/1 are ticks per interpolation callback, not seconds per cell.

The setup loop chooses each slot's weighted species once and stores it at `DS:59f8 + slot*2` before installing that slot's recurring job. When a Troggle leaves or is removed, `0x092dd` changes that same job back to arrival stage 1 and restores the tier base plus a new 0–89-tick jitter; it does not rerun the species selector. Stage 1 chooses the edge and coordinate, creates the off-board actor, changes the job to state 2, and loads the literal warning countdown `0x005a` (90 ticks, about 3.09 seconds) at `0x085aa`–`0x085b9`. Native therefore owns one independent arrival/warning cycle per slot and preserves the slot species across recurrences instead of drawing a new species from one global spawn timer. Because states 1–6 all remain on that persistent selector-1 record, mixed waiting/warning/actor callbacks dispatch by job ID; they cannot be batched into separate actor and controller passes. The focused calls-186–188 tie now locks that PRNG ownership in both ports.

After an actor reaches an in-board endpoint, `0x09b16` changes that actor's current job to dwell state 4 and stores an independent upper-exclusive `random(0,90)+30` countdown (30–119). Scheduler id zero at this call means the current job, not a shared global record. The scheduler's inclusive deadline comparison makes the observed native interval 31–120 subsequent public ticks; omitting that boundary advances every later move by one tick. When the dwell expires, state 4 calls `0x0904d` to choose and begin the next move. An actor exiting the board is removed and rearmed without this dwell roll.

## Recovered ordinary animation records

The actor's BTMP record is state-owned, not selected from a global wall clock.
The edge initializer at `0x08416` writes records `0/3/6/9` for
up/right/down/left and retains that first directional record through the
90-tick warning and entry move. `0x0904d` writes `2/5/8/11` when an ordinary
interior or exit move begins. The live rightward Reggie crossing in
`analysis/captures/wm_005.avi` proves the callback then paints records
`5,4,3,4,5,4` over its six horizontal phases, the direction-group form of the
shared local cycle `2,1,0,1`. The endpoint initializer at `0x09B16` writes the
stationary middle poses `1/4/15/10`. BTMP 1007-1011 each contain 16 records;
record 15 names exactly the same source rectangle as record 7, so the unusual
down-dwell alias is visually intentional. Warning/entry and dwell remain
fixed; interior movement advances only with its actor callback.

The actor callback count also determines the exact painted position. A
horizontal cell uses six 8-pixel callbacks and a vertical cell uses five
6-pixel callbacks; native now divides the corresponding interpolation by six
or five instead of applying the horizontal denominator to both axes. Entry
moves and captured vertical exits install one additional terminal callback.
That callback is an endpoint hold: it does not postpone the first visible
step. Headless gates lock every horizontal and vertical phase, the immediate
first-interval step, and the terminal endpoint hold in both cases.

## Recovered actor redraw order

The complete-board actor painter does not traverse fixed scheduler order or
native container insertion order. Initializer `0x09874` writes one-based job
IDs `1,2,3` to `DS:5A84`. Helper `0x09893` finds a selected ID in that list and
moves it to the end. Its callers include warning-to-entry setup, ordinary
movement setup, and the collision initializer. Board redraw `0x11498` walks
`DS:5A84` from front to back and calls the actor painter at `0x114CF`, so the
most recently activated actor is painted last wherever dirty rectangles
overlap. Native keeps this list independently from its fixed scheduler slots.

## Recovered safe-zone scheduler

The safe-zone initializer at `0x08cc8` creates the number of recurring jobs in `DS:069c`: `2,2,2,2,2,2,2,1,1,1,1,1`. `DS:06b4` seeds `1,1,1,0,0,0,0,0,0,0,0,0` of those jobs in the active phase. Every job draws one fixed upper-exclusive `random(0,210)+210` period (210–419 ticks, or about 7.21–14.38 seconds) and stores it as both its initial and reload countdown.

The event record at `DS:0688` has dispatcher kind 6. Its callback at `0x09937` alternates two states without changing that job's period: state 1 removes its saved cell, and state 2 chooses and saves a new cell, activates the marker, and removes an occupying Troggle. The cell selector at `0x0895e` uses upper-exclusive `random(0,3)+1` and `random(0,4)+1`, proving that markers are restricted to rows 1–3 and columns 1–4 of the zero-based 5×6 board. The player initializer at `0x08b20`–`0x08b51` uses the same interior 3×4 bounds.

## Recovered steering and trail effects

The steering switch at load-image offset `0x08099` uses direction values 0/1/2/3 for up/right/down/left. Reggie continues straight. Worker and Helper each continue straight 80% of the time and turn left or right 10% each. Bashful flees from the player at a Manhattan pixel distance of 96 or less; beyond that it uses 40% straight, 20% reverse, and 20% for each side turn. Smarty always moves toward the player through 96 pixels, does so 50% of the time from 97 through 192 pixels, and otherwise continues straight. `0x096b9` compares the Troggle actor coordinates with the Muncher's live `DS:59ac/59ae` coordinates, including its current 8-pixel horizontal or 6-pixel vertical walk phase; substituting the destination grid cell changes the 96-pixel boundary and the PRNG stream. Horizontal distance wins an axis tie. If the selected in-board destination is protected, the movement routine at `0x0904d` selects a random side turn and retries; it does not reject another Troggle in the destination. Troggles may overlap, and any species can continue through an edge and leave the board. The entry initializer at `0x08416` likewise accepts its directly selected edge coordinate without an occupancy retry. Edge selector values map as 0=bottom, 1=left, 2=top, and 3=right.

The protected-cell retry has no counter: `0x09170` jumps back to `0x09096`
until a direction is open. Native's former 32-attempt escape is removed. An
exhaustive Borland low-17-bit-state gate proves that at most two live safe
zones yield a maximum reachable chain of 24 draws, while a synthetic
three-neighbor fixture requires 37 and locks the unbounded structure. See
`TROGGLE-SAFE-RETRY-STATIC-AUDIT.md`.

Troggle overlap is not merely permissive layering. The state-3 dispatcher at `0x09ca0` changes a resident state-4 Troggle in the moving actor's destination to inert state 6. The survivor's state-4 handler at `0x0a0e6` detects the matching state-6 endpoint and calls the same bite initializer at `0x08db6`. State 5 advances the repeating `0x07f5b` record cycle `12,13,14,13,12,13` once per scheduler tick for 21 ticks; at `0x09d97`–`0x09da5` its counter reaches 21, `0x09b16` installs a fresh dwell for the survivor, and the scan at `0x09e15`–`0x09e5e` rearms every matching state-6 victim through `0x092dd`. Records 12 and 14 are pixel-identical, so a live framebuffer visibly alternates only the two open/closed poses. The supplied first-demo capture shows this exact Reggie-on-Reggie interval from about 42.01 through 42.73 seconds and the victim's later recurring entry.

The focused global-frame audit now closes that live interval completely.
Frames 2941–2944 are the last uniform overlap, frames 2945–2995 expose all
21 bite ticks, and frame 2996 is the terminal repaint. The three nonuniform
2x refreshes at 2940, 2952, and 2964 are classified separately. The terminal
call sequence at `0x09da5` pushes literal direction 2 before `0x09b16`; that
writes stationary record 15 even though the actor's movement heading remains
the earlier rightward value. Native formerly derived the stationary record
from that heading and therefore matched every bite pose but returned to the
wrong post-bite pose. Its actor state now stores the dwell BTMP record
separately. The deterministic Demo replay matches the pre-bite frame, all 21
complete tick frames, and terminal full-frame FNV-64 `0x9c2f96cd8e6918c2`.
See `analysis/number-live/gameplay/full-demo-cannibal-report.json`.

The continuous replay later reaches a second Reggie-on-Reggie bite at row 4,
column 5. Global frames 4653–4707 expose all 21 stable state-5 callbacks and
the terminal record-15 repaint. Native matches those 22 complete surfaces;
four uniform incomplete dirty paints and one nine-block torn refresh are
capture-only. Concurrent player work changes the final two bite surfaces and
proves an additional scheduler edge: player job 4 paints before the persistent
Troggle records, so its callback-local surface must be queued before the later
biter callback. Number's continuous gate and Word's paired fixture now enforce
that ordering. This raises aggregate complete live Troggle-state coverage from
197 to 219 at this boundary. See
`analysis/number-live/gameplay/full-demo-second-cannibal-report.json`.

The third first-board cannibal event is now complete. At row 0, column 2 the
bite overlaps a two-leg upward Muncher walk; global frames 5332–5385 contain
30 uniform logical runs. Pixel partitions prove that three one-frame runs
split a player or biter dirty paint, leaving 27 complete callback pages. The
seeded native event presents exactly those 27 pages in source order and emits
no extras. Its retained cell/page model preserves the selector-5 phase-0 page,
player-idle callback, earlier/later player-cell paints around the biter, the
penultimate interrupted player paint, terminal retained movement strip,
terminal bite, and two postterminal player callbacks without reproducing the
three incomplete source paints. Aggregate complete live Troggle-state coverage
therefore rises from 219 to 246. Word shares the implementation and has an
independent retained-cell/page ordering gate. See
`analysis/number-live/gameplay/full-demo-third-cannibal-player-walk-report.json`.

A separately mined earlier first-board interval at global frames 4309–4339
adds Bashful's right-edge arrival without requiring a renderer change. Native
presents the six completed phase-2-through-dwell pages at exact full-frame
hashes, taking aggregate complete live Troggle-state coverage from 246 to 252.
The remaining source run lasts one capture frame and restores only the four
x=308/y=112..115 rule pixels before the next actor pose overwrites them. That
is a uniform but incomplete erase-between-poses dirty repaint, so the native
double-buffered presenter deliberately excludes it rather than reproducing
source flicker. See
`analysis/number-live/gameplay/full-demo-bashful-right-entry-report.json`.

The adjacent continuous interval at global frames 4340–4652 now matches all
49 presentation-complete pages through the approach to the second cannibal
collision. It spans Reggie's rightward trail, four Muncher moves, Bashful's
right exit, the next right-edge Reggie entry, and the older Reggie's overlap
approach. Ten intervening runs are pixel-classified incomplete dirty repaints
and remain capture-only. The exact gate exposed both the dynamic `DS:5A84`
paint list above and a presenter-only entry boundary at frames 4562–4591:
three entrant callbacks retain the warning page, after which current Bashful
exit pages combine with correspondingly older entrant records, including the
first invisible record twice. Native retains that older actor history only in
the presenter; logical scheduler deadlines and Borland PRNG calls remain
unchanged. These 49 nonoverlapping pages raised aggregate complete live Number
Troggle coverage at that stage from 449 to 498. See
`analysis/number-live/gameplay/full-demo-pre-second-cannibal-bridge-report.json`.

Global frames 5386–5445 add the same Bashful's later `23 -> 26` trail. The
opening page remains unchanged for 39 frames after the third cannibal/player-
walk terminal, closing every formerly ungated frame 5386–5419, after which all
six leftward phases and the new dwell match native at exact full-frame hashes.
Seven pages are new, raising aggregate complete live Troggle-state coverage
from 252 to 259. The ninth source run is a one-frame terminal dirty repaint
with a 13-pixel one-row strip belonging to neither completed neighbor; it is
excluded as source flicker. See
`analysis/number-live/gameplay/full-demo-later-bashful-left-trail-report.json`.

The companion exhaustive inventory decodes all 22,004 logical source frames
and finds exactly five gameplay-board intervals. Their visible species sets are
Reggie+Bashful, none, Reggie+Worker, Reggie+Bashful+Worker, and Helper. Smarty
has zero pixels inside the board crop in every interval, which narrows the open
Smarty item to missing source evidence rather than an overlooked event in this
recording. Both formerly uncatalogued first-board Bashful edge windows are now
closed by isolated seven-page full-frame gates using their directly captured
concurrent Muncher states. The bottom-exit interval's 26-block frame is an
exact row-90 scanout splice of its adjacent completed pages. See
`analysis/number-live/gameplay/full-demo-troggle-inventory-report.json`.

The next complete live event on that board is Bashful's bottom-left trail and
left exit. Global frames 3073–3092 show `80` replaced by `150`, a phase-1
Troggle repaint briefly composed over the player's retained prior movement
frame, the following complete phase-1 state, phases 2–5, and terminal removal.
The regenerated `150` remains hidden by the swept board-blue actor rectangle
until the terminal callback; neither the actor nor that rectangle crosses the
left board viewport. This proves that ordinary state-3 movement advances to
visual phase 1 immediately rather than exposing setup phase 0. Native's seeded
replay matches all nine uniform full-frame states; frame 3082's 12 nonuniform
2× blocks are preserved as a torn refresh. Number and Word now share the
phase-1 clock, clipped exit painter, swept-label suppression, and bounded
retained-player presentation gate. See
`analysis/number-live/gameplay/full-demo-bashful-trail-exit-report.json`.

Frames 3179–3208 add Reggie's next in-board trail. Before `207 -> 65`, the
Demo Muncher walks upward and exposes another physical-record boundary:
selector 5 installs logical player phase 0 after the player job has already
run, so the resident framebuffer remains unchanged until the next player
callback paints phase 1. At the movement terminal, the following standing
player callback becomes visible before the later Reggie job starts its trail
and phase-1 move. Native now retains the phase-0 source framebuffer and queues
that callback-local standing state. Thirteen complete frames match through all
six rightward Reggie phases and dwell. A uniform-2× partial phase-2 actor paint
and two nonuniform refreshes are classified separately. See
`analysis/number-live/gameplay/full-demo-reggie-trail-report.json`.

The next Bashful callback at frames 3740–3815 independently reuses the same
ordinary state-3 path. Its `150 -> 220` trail, phases 1–6 moving right, and
dwell give eight more exact complete native frames; the one 19-block torn
refresh is capture-only. See
`analysis/number-live/gameplay/full-demo-second-bashful-trail-report.json`.

Frames 3816–3870 continue without a board transition. Fourteen further
uniform framebuffers match in order: a second complete six-phase Reggie
crossing and dwell, followed by five upward Bashful exit phases and the
terminal top-right repaint. Two nonuniform refreshes remain source-only;
native instead presents two complete callback-local composites. The paired
Word fixture proves that the shared vertical-exit dirty rectangle is clipped
below the header and hides the trail payload until removal. See
`analysis/number-live/gameplay/full-demo-later-reggie-top-exit-report.json`.

The following frames 4090–4161 close a scheduler-composition edge rather than
adding a synthetic actor phase. Reggie regenerates `232 -> 3` and exits right
while the Muncher performs its full seven-interval chew. Nine complete source
framebuffers match native in order, through actor removal and the terminal
record-13 chew hold. Frame 4109 is a uniform-2× partial dirty refresh whose
pixels all come from either the pre-action or complete phase-1 framebuffer;
frame 4121 has one nonuniform 2× block. Both stay capture-only. The native
Number gate and paired Word fixture require the complete later-job composite
without an enemy-only intermediate presentation. See
`analysis/number-live/gameplay/full-demo-chew-right-exit-report.json`.

An earlier interval on the later Factors-of-63 board now adds simultaneous
Helper arrivals from both horizontal edges. Frames 19420–19480 contain thirteen
presentation-complete pages matching native from the warning through a
rightward Muncher walk, both clipped entry sequences, terminal poses, and
dwell. They add thirteen unique states and raise aggregate complete live
Number Troggle-state coverage from 259 to 272. Two one-frame source runs have
11 and 22 nonuniform doubled blocks. A third is uniform but clears only the
warning outline's bottom 26 pixels; restoring those pixels reconstructs the
separately gated native completed surface exactly. All three remain
capture-only. See
`analysis/number-live/gameplay/full-demo-dual-helper-entry-report.json`.

The later Factors-of-63 board supplies the first live Helper trail evidence.
Frames 19530–19590 contain fifteen uniform logical runs: two Helpers move at
once (bottom-left rightward and right-edge downward), both clear a source `3`,
and the Muncher overlaps the callbacks while moving down. Thirteen selected complete
framebuffers match native exactly, bringing the complete live Troggle-state
total from 112 to 125. Three are exact resident-surface player callbacks, and
both production schedulers route that compositor. Word gates the same thirteen
scheduler tuples, both axis geometries, the `D3FF5D` Helper highlight, and the
destructive source-cell effect. The preceding dwell is context. Although run 7
is itself uniform at the doubled-pixel level, every one of its rows proves it
is a horizontal scanout tear: rows 0–80 equal matched run 6 and rows 81–199
equal matched run 8. It is capture-only, so all complete callback framebuffers
in this isolated window are closed.
Because this board occurs after the withdrawn divergent route, it is used only
as isolated frame/callback evidence and not as a continuous replay claim. See
`analysis/number-live/gameplay/full-demo-dual-helper-report.json`.

Global frames 19690–19886 continue those two Helper jobs through 27 more exact
presentation-complete pages. They cover a destructive `8` trail, the Muncher's
leftward walk and complete seven-record chew of `63`, a destructive `12`
vertical trail, and an overlapping destructive `30` bottom exit. Aggregate
complete live Number Troggle-state coverage rises from 272 to 299. Two source
runs contain 15 and 20 nonuniform doubled blocks; a third uniform run contains
the completed next player phase plus a 15-pixel strip belonging to neither
completed neighbor. All three are capture-only. See
`analysis/number-live/gameplay/full-demo-later-dual-helper-report.json`.

Global frames 19887–20062 add 30 exact presentation-complete pages between the
first Helper's removal and the surviving Helper's later bottom exit. The
Muncher walks left, performs a complete seven-record chew of the row-2/
column-2 `1`, retains terminal record 13 while the row-2/column-1 safe zone
expires on selector-5's held phase-zero page, walks down, and performs the same
complete chew on the row-3/column-2 `1`. The surviving Helper remains in its
row-4/column-5 dwell throughout. Three actor transitions contain 21, 20, and
25 nonuniform doubled blocks. Four uniform copies lock the second chew's final
record-13 page before frame 20062 updates only the lower half of five doubled
pixels on the other safe-zone outline; that partial expiration remains
capture-only. Aggregate complete live Number Troggle-state coverage rises from
299 to 329. See
`analysis/number-live/gameplay/full-demo-post-helper-player-report.json`.

Frames 20063–20187 on that board add twelve more exact uniform source states,
bringing the complete live Troggle-state total from 125 to 137. A Helper dwells
at row 4/column 5, erases `7`, traverses all five clipped bottom-exit phases,
and is removed while the Muncher simultaneously moves upward through phases
1–4, its endpoint record, and the following standing callback. Number matches
every full-frame hash; Word gates the same twelve tuples, bottom viewport,
destructive trail, and no pixels below the board. The window remains isolated
post-divergence evidence rather than a continuous replay claim. See
`analysis/number-live/gameplay/full-demo-helper-bottom-exit-report.json`.

Frames 20188–20527 close the remaining gameplay-board tail with 29 more exact
presentation-complete pages. They cover left/down/left Muncher movement, the
complete seven-record chew of row 3/column 0 value `21`, terminal record 13,
and the next actor-owned `Troggle!` warning while no enemy is yet visible. The
only three excluded pages are pixel-proven incomplete paints: a phase-3 page
plus four invalidated border pixels and one nonuniform doubled block, a full
39-pixel idle delta plus 16 pixels belonging to neither endpoint, and a
uniform horizontal record-12/record-13 chew splice. This closes every complete
source page through the detected board run's final frame and raises aggregate
complete live Number Troggle-state coverage from 329 to 358. See
`analysis/number-live/gameplay/full-demo-final-board-tail-report.json`.

The same common bite initializer is used for player collision. It assigns bite frame 12 to the selected Troggle and frame -1/state 6 to other active Troggles occupying that cell; the selected actor alone supplies the species pointer used by feedback. Every BTMP 1007–1011 has the shared 16-record layout with records 12/13/14, so the animation is species-independent. Initialization selects record 12, and the state-5 callback advances elapsed time and schedules its next absolute deadline once per public tick until the terminal callback at tick 21. The selected biter's pre-existing dwell cannot overwrite that collision record, while independent jobs such as later Troggle slots and the demo controller continue on the collision-start tick. The common state-5 terminal at `0x09D41` calls `0x09B16` first, which snapshots the survivor's cell and consumes a fresh dwell roll; only then does it call the reserve-loss routine `0x09262`, submit stream 10, and invoke the randomized feedback painter at `0x109E3`. On an ordinary nonterminal life it next removes/rearms every extra state-6 actor on the collision cell and submits stream 11 for each. Demo and final-life terminals skip that scan because the imminent terminal transition owns the records. Thus the footer retains all reserves throughout the bite, removes one only as feedback begins, and preserves the original phrase-before-rearm PRNG order. (`0x093A9` is the separate Muncher chew dispatcher.)

The player-collision predicate at `0x08878` passes Muncher job ID 4 to the scheduler query and requires the returned state to equal 4 before comparing actor cells. A moving state-3 or chewing state-5 Muncher therefore cannot be caught by a Troggle endpoint, and recovery state 6 remains excluded. The player's own movement terminal still checks for an enemy after returning to standing state. Native and focused tests now enforce all three boundaries; this removes an older deterministic-replay expectation that incorrectly admitted a collision during the first chew interval.

The native survivor and player-collision painters now share the exact
tick-local sequence. A tick-by-tick regression gate requires
`12,13,14,13,12,13` to repeat through tick 20. Lossless Number and Word user
collisions independently produce the same stable cell hashes
`0x2cc4ae9134dff1ee` and `0xe98dda1e9e05c039`; their measured durations are
0.713406 and 0.727674 seconds versus 0.720720 seconds for 21 public ticks. See
`analysis/number-live/gameplay/collision-avi-exact/report.json` and
`analysis/word-live/gameplay/collision-avi-exact/report.json`.

Safe-zone activation at `0x09987` marks the chosen cell first, then calls the general moving-actor intersection test at `0x08277` for every Troggle. A Troggle is caught when either endpoint of its current move intersects the new zone. Before `0x092dd` removes and rearms that slot, `0x081de` applies its normal species trail effect. The player is intentionally not excluded from the safe-cell selector: a zone may begin or end underneath the Muncher without costing a life.

The apparent state-3 “disappearance animation” is a dirty-rectangle erase, not a timed visual phase. `0x0994d` changes the marker byte from 1 to 3 and requests a redraw. The cell renderer at `0x0113fd` selects color zero, calls the same four-corner marker routine, and immediately stores state 0 at `0x01141e`. Because native redraws the full logical framebuffer each frame, omitting the marker produces the same final pixels without retaining a separate transient state.

The lossless `analysis/captures/nm_000.avi` now supplies an exact live oracle
for the left-edge arrival itself. Runs 13/14 and 16/17 isolate two safe-zone
expiries around the warning; run 19 is the completed warning removal. At
horizontal phase 2, the clipped 41x29 blue actor box erases four pixels of the
edge-cell label while the sprite remains outside the board. Phases 3–5 reveal
successive directional records, and phase 6 installs the right-facing middle
record at the endpoint, merging pixel-for-pixel with the 161-frame dwell run.
The five entry-owned offsets are therefore `1,2,1,0,1` for phases 2–6, rather
than a four-element cycle valid only for vertical arrivals. Native matches all
ten selected full-frame FNV-64 values; the two nonuniform DOS warning refreshes
are preserved only as capture evidence. See
`analysis/number-live/gameplay/nm000-entry-report.json`.

The continuation `nm_001.avi` supplies the opposite horizontal edge and shows
that the entry viewport is not symmetric around the grid. Right-entry actor
pixels and its blue dirty rectangle remain paintable through x=309, overwriting
the x=308 grid rule while preserving the outer x=310 outline. That damaged
grid column remains blue even after phase 6 has moved the 41-pixel actor box
wholly left of it; the terminal dwell callback then restores exactly those 29
magenta pixels. Native now carries this retained one-column state explicitly.
All twelve selected safe/warning/right-entry frames match full-frame hashes;
the one warning-start, warning-removal, and phase-4 torn refreshes remain
capture-only evidence. See
`analysis/number-live/gameplay/nm001-right-entry-report.json`.

Global frames 2864–2918 of the full lossless Demo add a bottom-edge Bashful.
Stable phases 2–5 climb in 6-pixel steps and paint through y=177 while the
y=178 outer outline remains intact. The entry painter retains the overwritten
y=176 grid segment through phase 5 even after the actor record has moved above
it; the terminal dwell restores all 49 magenta pixels across x=20..68. The
same frames directly correct Bashful's gameplay highlight from the PCXF-local
post-DAC `20AAFF` to the resident-DAC `5DBEFF`. Native matches the pre-entry,
four visible phases, and 44-frame dwell full-frame hashes; the single
22-nonuniform-block refresh is capture-only evidence. See
`analysis/number-live/gameplay/full-demo-bottom-entry-report.json`.

Global frames 5188–5215 provide a second bottom-entry Bashful under denser
callback composition. Seven presentation-complete pages now match native
across the Muncher's downward phases 3–4, endpoint and idle, Bashful's invisible
phase 1, visible phases 2–5, dwell, a row-0/column-2 Reggie dwell, and a warning
owned by a later slot. The former deterministic mismatch was only the
concurrent player's captured route; reconstructing that direct state closes
the full pages without reinstating the withdrawn continuous route. Frame 5196
contains 20 nonuniform doubled blocks and 47 pixels belonging to neither
complete neighbor, so it remains capture-only. Aggregate complete live Number
Troggle-state coverage rises from 358 to 365. See
`analysis/number-live/gameplay/full-demo-later-bottom-entry-report.json`.

The seeded continuous route now reaches backward through global frames
5173–5215. Native presents all eleven complete source pages in exact order:
the resident warning hold, Bashful's bottom-entry records, the overlapping
downward Muncher move, Reggie dwell, and entrant dwell. The retained-page
presenter supplies the source ordering without changing any job deadline or
Borland PRNG call. Frame 5184 is a uniform one-row dirty-write intermediate
with 41 pixels belonging to neither complete neighbor; frame 5196 is the
already measured 20-block torn repaint with 47 such pixels. Both remain
capture-only. Seven completed pages overlap the isolated gate, so four are new
and current aggregate complete live Number Troggle coverage rises from 498 to
502. See `analysis/number-live/gameplay/full-demo-later-bottom-entry-
continuous-report.json`.

Global frames 5687–5718 close the later first-board Bashful bottom exit. All
seven presentation-complete pages match native across the left-facing dwell,
`115 -> 105` trail, five clipped downward-exit callbacks, terminal removal,
Muncher upward phases 1–5, row-1 Reggie dwell, safe zone, and retained warning.
Frame 5711 contains 26 nonuniform doubled blocks, but every logical pixel
above scanline 90 belongs to the prior completed page and every pixel from
scanline 90 down belongs to the following completed page. It remains a
capture-only scanout splice. Aggregate complete live Number Troggle-state
coverage rises from 365 to 372. See
`analysis/number-live/gameplay/full-demo-later-bottom-exit-report.json`.

The seeded first-board replay now reaches continuously through that same
5687–5718 bottom-exit interval. Its presenter holds the pre-exit dwell page,
combines current Muncher callbacks with delayed Bashful exit records, omits
three native-only player callback pages, and emits the actor-free page directly
after the final clipped pose rather than duplicating it. All seven completed
source pages match in order; frame 5711 remains the proven row-90 scanout
splice. This changes no scheduler deadline or Borland PRNG call. Because all
seven pages overlap the isolated gate, current aggregate complete live Number
Troggle coverage remains 502. See `analysis/number-live/gameplay/full-demo-
later-bottom-exit-continuous-report.json`.

The first-board continuous comparator next exposed three earlier scheduler-
presentation gaps. Frames 2840–2918 now match fourteen completed pages across
the correct-value chew, resident Reggie entry, overlapping Bashful bottom
entry, and dwell; two measured incomplete refreshes remain capture-only.
Selector 5's imminent action is predicted on a copied PRNG state so a chew no
longer receives the presenter lag recovered for movement. Six pages overlap
the isolated bottom-entry audit, adding eight. Frames 2919–2944 then match ten
completed pages through a leftward player walk and simultaneous right entry:
the earlier player callback is presented from a separate dirty surface while
the entrant advances the resident working page. One 17-block refresh is
capture-only, and the final page overlaps the cannibal audit, adding nine.
Finally, frames 3300–3340 match eight complete player-walk pages including the
terminal record before a later warning callback and its following standing
page. These three gates add 25 focused pages and raise current aggregate live
Number Troggle coverage from 502 to 527. See
`analysis/number-live/gameplay/full-demo-early-bottom-entry-continuous-report.json`,
`analysis/number-live/gameplay/full-demo-first-cannibal-approach-continuous-report.json`,
and `analysis/number-live/gameplay/full-demo-player-warning-boundary-continuous-report.json`.

Frames 3462, 3474, and 3491 are now pixel-classified capture artifacts rather
than missing callback states. Frame 3462 is composed entirely from its complete
neighbors, frame 3474 contains a nine-pixel incomplete vertical write, and the
uniform frame 3491 contains a 41-pixel incomplete row write. See
`analysis/number-live/gameplay/full-demo-post-warning-artifacts-report.json`.

Frames 3520–3606 then lock fifteen complete pages across a row-1/column-3 chew,
the terminal callback of an already resident bottom entrant, and the start of
a new right entrant. The first-board presenter now preserves callback-local
player and entrant surfaces through both overlaps and exactly matches all
fifteen source hashes; scheduler and PRNG state are unchanged. This raises
aggregate focused live coverage from 527 to 542. See
`analysis/number-live/gameplay/full-demo-chew-entry-boundary-continuous-report.json`.

Frames 3607–3885 classify eight more source-only samples. Seven are exact
partial-dirty partitions and frame 3818 is an adjacent-page scanout splice;
none occurs in native. Frames 3884–3941 then lock nine complete pages while the
Muncher enters the source cell of a right-departing Reggie. The source preserves
the next player record in the older part of Reggie's swept trail because each
Troggle callback clears only its previous/current 41x29 boxes. Native now does
the same without changing scheduler or PRNG state, raising aggregate focused
live coverage from 542 to 551. Frame 3931 remains a measured incomplete repaint.
See `analysis/number-live/gameplay/full-demo-pre-departing-enemy-artifacts-
report.json` and `analysis/number-live/gameplay/full-demo-departing-reggie-
player-overlap-report.json`.

Frames 3941–4090 then provide a 24-page contiguous bridge across the standing
boundary, exact chew, two Muncher walks, and the pre-chew/right-exit dwell.
Native matches every complete source page. Pixel partitions exclude only the
24-block frame 4027 and one-block frame 4044 incomplete dirty paints. Fourteen
pages are new beyond neighboring gates, raising aggregate focused live coverage
from 551 to 565. See `analysis/number-live/gameplay/full-demo-post-departure-
continuous-report.json`.

The board-wide diagnostic now covers the real first-board opening at frame 2390
through the terminal collision hold at frame 6131. It aligns 515 of 584 source
logical runs against 541 changed native pages. The new opening audit locks eight
complete initial/downward-player pages and excludes the exact frame-2459
scanout splice. The post-second-Cannibal audit locks another 51-page contiguous
sequence through frame 5173; its two nonuniform fragments and two uniform
incomplete 41-pixel row repaints remain capture-only. With two shared boundary
pages, these intervals add 57 focused pages and raise aggregate live coverage
from 565 to 622. See `analysis/number-live/gameplay/full-demo-first-board-
opening-continuous-report.json` and `analysis/number-live/gameplay/full-demo-
post-second-cannibal-continuous-report.json`.

The reconciliation pass accounts for every residual occurrence. All 69
source-only runs are either one of 68 focused, pixel-classified capture
artifacts or the repeated first-Cannibal hash that already occurs ten times in
native. Five of six internal native-only pages are exact zero-unique-pixel
actor-layer partitions of adjacent DOS pages; the sixth is the separately
gated overlapping Reggie callback composite. The remaining 20 native pages
start after the gameplay window and are the already exact-hash-gated collision
Wipe, Hall, logo, and next-board painter. The scoped first-board gameplay
sequence therefore now has a measured board-wide parity claim. See
`analysis/number-live/gameplay/full-demo-first-board-continuous-sequence-report.json`
and `analysis/number-live/gameplay/full-demo-first-board-reconciliation-report.json`.

Global frames 5719–5799 then independently lock the first Demo Reggie/player
collision at full-frame scope. The continuous native replay presents the
stationary collision page, all 21 ordered bite callbacks, and the complete
`Oops`/`Trogglus normalus` feedback page in the exact 23-page source order.
The stationary page duplicates the already gated collision-transient
framebuffer, so this contributes 22 new pages and raises aggregate complete
live Number Troggle-state coverage from 372 to 394. The only other source
pages are exact horizontal scanout splices at logical rows 65, 70, and 76;
they remain capture-only. See
`analysis/number-live/gameplay/full-demo-first-player-collision-report.json`.

Global frames 5660–5687 provide the immediately preceding eight-page Muncher
approach under the same resident Reggie, Bashful, safe zone, and warning.
Native matches the starting dwell, five interpolated rightward positions,
right-facing endpoint record, and idle callback with no refresh exclusions.
The idle page is shared with the bottom-exit audit, so seven pages are new and
aggregate complete live Number Troggle-state coverage rises from 394 to 401.
See
`analysis/number-live/gameplay/full-demo-first-player-right-approach-report.json`.

Global frames 5446–5659 close the continuous gap between that approach and
the earlier Bashful left-trail gate. Fifteen presentation-complete pages match
native across a full downward Reggie step and source-cell restoration, a safe
zone activating on the same tick as selector 5 installs a rightward player
move, all five visible player callbacks, endpoint, idle, and the next warning.
Both boundary pages were already gated, so 13 pages are new and aggregate
complete live Number Troggle-state coverage rises from 401 to 414. This audit
also removes native-only phase-zero hash `0x9d789ca35264aafe`: selector 5 now
retains the page from before the earlier safe-zone callback until player phase
1 becomes visible. Frame 5598 is a nonuniform incomplete repaint whose 46-
physical-pixel horizontal strip belongs to neither completed neighbor and
remains capture-only. See
`analysis/number-live/gameplay/full-demo-reggie-down-safe-entry-report.json`.

The preceding frames 5216–5331 add 15 more presentation-complete pages across
the exact correct chew of `220`, its terminal hold, warning removal, top-entry
Reggie phases 1–5, and the ordinary overlapping endpoint before the already
gated third cannibal bite. The opening page is shared with the later bottom-
entry audit, so 14 pages are new and aggregate complete live Number Troggle-
state coverage rises from 414 to 428. The endpoint exposed another resident-
surface boundary: an entry callback may install state 5 logically, but its
ordinary endpoint remains displayed until the first bite callback on the next
public tick. Native now retains that page. Two exact scanout splices and one
18-block torn entry repaint remain capture-only. See
`analysis/number-live/gameplay/full-demo-chew-top-entry-report.json`.

Global frames 4162–4308 add the continuous bridge before Bashful's right-edge
arrival. Twenty-one presentation-complete pages match native in exact order
across two downward Muncher walks, the intervening upward walk, Reggie's
simultaneous rightward crossing and `92 -> 115` trail, and safe activation just
before Bashful's invisible phase-1 entry. Seven other logical runs are
pixel-proven incomplete dirty repaints: five are physically nonuniform, and
the two uniform pages contain only old/new-neighbor pixels. Aggregate complete
live Troggle-state coverage rises from 428 to 449. Native formerly exposed
safe/warning hash `0xa9b8de4b879ca671`; it now retains the pre-safe resident
page until the warning actor begins entry, matching the captured page-turn
boundary without reproducing the partial safe outline. See
`analysis/number-live/gameplay/full-demo-player-reggie-safe-bridge-report.json`.

The third Demo board's frames 11840–11876 independently cover a Worker
arriving from the right during a correct player chew. Eight uniform states
lock Worker's active-board `CB5DFF` highlight and the full phase-2-through-
dwell geometry; two one-block torn refreshes are separate. Widening the chew
timeline establishes that an entering actor begins at visual phase 1 and that
the phase-3 painter is exposed once with retained record-14 chew pixels before
the terminal record-13 player paint. Native now queues that complete callback
framebuffer and exactly presents all eight uniform states in source order. See
`analysis/number-live/gameplay/full-demo-worker-entry-report.json`.

The immediately following frames 11940–11992 expose the same Worker's complete
player-collision path. Twenty-five source runs contain a uniform framebuffer,
raising the complete live Troggle-state coverage from 137 to 162. They include
the stationary endpoint callback before bite record 12, all 21 logical bite
ticks, safe-zone activation, a later Reggie beginning bottom entry, and stable
`Aargh` feedback. The capture corrected two scheduler details: the first closed
bite is painted onto the pre-safe resident surface, and the final-pose/terminal
callbacks abort at the biter's physical slot before later slot 2, retaining the
Reggie at phase 4. Word gates the same surface lifetime and slot boundary. Two
fully torn runs are excluded. The shared dispatcher now paints the bite at its
physical slot between earlier and later Troggle callbacks, queues the
callback-local page, and retains the clipped phase-1 entry surface. All 25
complete source runs therefore match in exact order, including both later
resident-page appearances. See
`analysis/number-live/gameplay/full-demo-worker-collision-report.json`.

The fourth Demo board's frames 15899–16390 add Bashful's top-entry collision.
Thirty-five presentation-complete full-frame runs raise the live Troggle-state
coverage from 162 to 197: three Worker entry/dwell states, four Bashful entry
phases, five concurrent Reggie entry phases, the exact pre-bite callback, all
21 state-5 bite ticks, stable `Aargh! ... Trogglus timidus.` feedback, and the
following collision-transient board. The pre-bite run is shared by the
approach and collision subsets, so this adds eleven approach states beyond the
previous 24-run collision gate. The clipped entry cells match exactly,
including the retained Muncher record 12 under phases 4 and 5. The terminal
board exposed that the text-visible feedback holds bite record 12, but clearing
the text restores survivor dwell record 15; both native runtimes now follow
that boundary. Four nonuniform refreshes are excluded. Frame 15901 is a strict
label scanout splice between exact native labeled/eaten surfaces, and frame
15978 is a strict adjacent-state scanout splice with zero pixels outside its
two neighboring bite frames. See
`analysis/number-live/gameplay/full-demo-bashful-collision-report.json`.

The board-effect switch at load-image offset `0x081de` operates on the saved cell contents. Reggie and Bashful replace a populated cell but leave an already blank cell blank. Worker always generates a replacement, including on a blank cell. Helper clears the cell. Smarty restores the original cell unchanged.

That trail routine is followed immediately by the board-live predicate at
`0x0A183`, and its Boolean result is a control-flow boundary rather than a
page-state query. The ordinary state-4 movement callback calls the trail at
`0x09053`, calls `0x0A183` at `0x0905D`, and jumps directly to its return at
`0x0921C` when no positive cell remains; steering and the next actor move do
not run. The safe-zone callback has the same ordering: trail at `0x099DE`,
predicate at `0x099E4`, and whole-callback exit at `0x09A3A` on false. It
therefore skips slot rearm/actor removal and streams 11 and 4 after the
completion route has taken ownership. This matters on an ordinary direct
advance because the terminal routine constructs the next board while the old
callback still nominally has an actor index: continuing would mutate the new
PRNG stream and dereference an actor record invalidated by board construction.

## Native lock

The board initializer's first-free-slot insertion order is safe zones, player,
Troggles, fixed mask-1 controllers, and Demo. Native now dispatches each
coalesced presentation frame one original public tick at a time in the
effective mask-2 order `safe -> player -> Troggle -> Demo`. A protected-chew
test proves that a Troggle endpoint cannot skip ahead of the state-5 player
callback. A wrong-answer terminal proves that preceding safe jobs receive tick
seven while later Troggle jobs stop after tick six. A movement-terminal
collision still permits the later same-tick Demo reload while leaving bite age
at zero; without that scan tail, the captured second-board initializer shifts
from PRNG call 271 to 270. The complete record/address audit is in
`GAMEPLAY-SCHEDULER-STATIC-AUDIT.md`.

The renderer regression also locks the exact seven-state later Bashful
bottom-exit/upward-player sequence described above, independently of the
corrected continuous Demo route. It additionally gates the preceding exact
eight-page rightward Muncher approach. The continuous replay separately
requires the exact 23-page first Reggie/player collision sequence while
omitting all three capture-only scanout splices.
It now also requires the 21-page player/Reggie/safe-zone bridge and excludes
all seven measured incomplete dirty repaints in that interval.

## Completed live boundary

The uninterrupted seed-`0xF585` continuation removes the last live-route
boundary that previously kept this audit open. It follows the corrected fourth
board through its final collision, exact phrase/survivor draw order, restored
board, Hall/logo, the complete Less Than 20 initializer, and the last gameplay
route through wrong-answer Hall and closing logo. The full residual pixel audit
classifies every partial painter/composite and leaves no unexplained source or
native page.

Combined with the exhaustive table/branch gates and the independent Smarty,
Worker, Bashful, Helper, Reggie, cannibal, safe-zone, edge-entry/exit, trail,
and last-positive captures above, every species-specific control-flow branch
and shared presentation primitive has executable-backed tests plus a live
anchor. Additional species/edge permutations are parameter combinations, not
remaining unaudited implementations. CGA and physical audio remain tracked in
their dedicated rows.

`src/game.cpp` now uses these four tables for enemy count, arrival timing, weighted species choice, and movement-callback interval, plus the recovered species identity, live-pixel steering, edge-entry/exit, ordinary entry/move/dwell frame records, common bite, cell-effect, recurring-slot, inclusive per-endpoint dwell, safe-zone scheduler, and the 21-tick Troggle-cannibal sequence. Trail application now returns the original board-live result through both the state-4 movement and safe-zone callers and through the enclosing board-job pass; forced last-positive Helper cases require direct level advance, exactly one stream-15 submission, no retirement/activation tail, no post-generation steering draw, and no fresh Troggle/safe timer debit on the destroying tick. The original common dispatcher itself decrements at `0x165E3`, reloads a due record at `0x16602`–`0x16618`, and only then invokes its callback at `0x16654`. Native likewise debits the public tick before callback dispatch, so synchronous board construction cannot turn that reset into a synthetic `-1` and delay every fresh job by one tick. The regression compares the installed level-2 job records with a direct-completion baseline and requires exact timers, PRNG state, and a zeroed accumulator. The calls-186–188 mixed tie now follows persistent record order: lower job ID 2 consumes calls 186/187 for a left-edge, row-1 warning, then higher job ID 3 consumes call 188 for a 95-tick endpoint dwell. Offline frames around 61–65 seconds show that left warning concurrently with a distinct bottom-right Reggie; the former 118-tick/right-bottom claim had conflated the two actors. The first collision also proves a sixth terminal callback on vertical exits and a terminal tie in which call 269 installs the survivor dwell, call 270 selects the captured `Oops`, and the now-removed Muncher actor permits only the demo controller's call-271 reload. A focused three-record collision tie additionally locks original state 6: the non-selected collocated job remains inert and consumes no PRNG, while the next unrelated waiting job still consumes its edge/coordinate draws. `game_render_state_test` locks that ordering as well as the exact 22-state `nm_000`/`nm_001` left/right-entry composites, the six-state full-Demo Bashful right-entry sequence, the exact seven-state later Bashful bottom-entry/player sequence, the exact eight-page later Bashful left-trail sequence, the exact 13-state simultaneous dual-Helper entry/player-walk sequence, the exact 27-state later dual-Helper trail/chew/exit sequence, the exact 30-state post-Helper player/chew/safe-expiry sequence, the exact 29-state final-board player/chew/warning sequence, the exact 23-state first, 22-state second, and 27-state third live cannibal sequences/terminals (with the third's three incomplete source paints excluded), the exact nine-state Bashful trail/left-exit sequence, the exact 13-state Reggie trail/player-callback sequence, the exact eight-state second Bashful trail sequence, every ordinary directional frame state, the down-dwell alias, the separate post-bite dwell-record/heading ownership, every table value, each 100-point weight row, zero-weight exclusion, capped tier-12 behavior, persistent slot identity after exit, arrival jitter and fixed 90-tick warning, moving-player steering boundaries, occupied-cell entry, standing-only endpoint collisions, scheduler-ordered freeze/bite/rearm, exact 21-tick player-bite timing/records, selected-biter/hidden-overlap behavior, multi-species edge exit, every trail-effect branch, safe activation across a move, player/zone overlap, both safe-job count tables, fixed per-job periods, alternating phases, interior cell bounds, the interior player start, and the deferred collision-life decrement. The deterministic replay passes through first-board collision, the captured second-board initializer at PRNG call 271 and its enemy-free wrong-answer route, the pixel-identical third-board initializer at call 334, and the third board's five-munch Worker collision through its call-507 `Aargh` feedback. Its later unrecorded route is now relocked after removing the former impossible state-5 collision; that continuation is a native determinism gate, not additional live reference evidence.
