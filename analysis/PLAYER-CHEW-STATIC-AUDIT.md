# Original player-chew callback audit

This audit resolves the Muncher eating animation from the preserved Number and
Word executables, their extracted BTMP/PCXF records, the lossless Number Demo
capture, and a muted lossless Word user-game capture. The evidence files are
analyzed offline; rerunning the audit does not launch either original
executable, the native GUI, or an audio device.

## Number Munchers dispatcher

The munch initializer at image `0x08A31` selects correct/wrong cue 7/8, saves
the signed board record at `DS:59B4`, clears the live board word at `0x08A8E`,
writes player BTMP record 12 at `0x08A9C`, and resets the common actor animation
counter to zero at `0x08AA2`. Its 20-byte job template sets the public scheduler
mask to 2, the recurring callback interval to one tick, and state to 5. Thus the
answer is already absent beneath the first chew pose; scoring, remaining-answer
decrement, wrong-answer formatting, and life loss wait for the terminal callback.

The chew callback at `0x093A9` first calls the shared animator at `0x07F5B`.
Starting from record 12/counter 0, that animator increments while its counter
is below 3, decrements at counters 3 and 4, and resets after counter 4. The
callback then increments its own transition count and takes the terminal
answer-resolution branch when that count reaches 7. The exact actor records
are therefore:

| Public tick | Actor record | State |
|---:|---:|---|
| 0 | 12 | initial visible interval |
| 1 | 13 | visible interval |
| 2 | 14 | visible interval |
| 3 | 13 | visible interval |
| 4 | 12 | visible interval |
| 5 | 13 | visible interval |
| 6 | 14 | visible interval |
| 7 | 13 | terminal callback; answer resolution occurs immediately |

Record 15 is not selected by this dispatcher. BTMP 1006 intentionally maps
records 12 and 14 to the same source rectangle and records 13 and 15 to a
second shared rectangle. That duplication made the former linear native order
`12,13,14,15,12,13,14,15` look correct in visual hashes even though its actor
record state was wrong.

The PIT divisor `0x0555` and 30-interrupt public-clock divider give about
29.1375 scheduler ticks per second. Seven ticks are 240.239963 ms, agreeing
with the existing lossless capture's approximately 244 ms observed boundary
without retaining the former rounded 250 ms timer.

## Word Munchers dispatcher

Word uses the instruction-equivalent path: initializer `0x095FE` writes record
12/counter 0 at `0x09669`/`0x0966F`, after saving `DS:5DD2` and clearing the
live board word at `0x0965B`; callback `0x09F76` calls the shared animator at
`0x08B28` before the same seven-transition terminal test. Word and Number PCXF
1006 and BTMP 1006 are byte-identical, so the exact record and pixel behavior
is shared.

## Board mutation and collision boundary

Both initializers retain the signed selected value separately from the cleared
live board. This matters because safe-zone, Troggle, and demo jobs continue
while the chew job runs: terminal scoring/feedback must use the saved value,
not whatever later occupies that board slot. Native therefore stages the
selected record, blanks the render-visible cell immediately without decrementing
the correct count, and consumes the staged record only at tick 7. Full native
board recounts explicitly retain a pending positive record so an unrelated
trail mutation cannot decrement it early or make the terminal decrement twice.

The endpoint collision predicates at Number `0x08878` and Word `0x09445` pass
Muncher job ID 4 to the scheduler-state query and accept the collision only when
the returned state is 4. Moving is state 3, chewing is state 5, and recovery is
state 6. An enemy endpoint therefore cannot interrupt a walk or chew; the
ordinary movement terminal still checks a resident enemy after the player has
returned to standing state. Focused tests exercise harmless moving/chewing
endpoints and a fatal standing endpoint in both runtimes.

## Munch eligibility and the cell-status guard

Before either cue or actor setup, Number `0x08A42` and Word `0x0960F` reject a
zero board word. This is the authoritative blank-cell test. Native previously
checked only its auxiliary `eaten` flag. Number's Reggie/Bashful trail path can
restore an already blank saved record, and that branch had produced an empty
label with `eaten == false`; pressing Space on that reachable state therefore
started a spurious wrong-answer chew. Blank trail results now retain the
native zero-record invariant, and both cores independently reject an empty
payload even if a synthetic/restored auxiliary flag is clear. Word's
presentation guard runs before selecting cue 7/8, matching the original
ordering.

Both initializers next compare the selected cell's status byte with exactly
`0x10` (Number `DS:5A3C` at `0x08A4F`; Word `DS:5E5A` at `0x0961C`). An
exhaustive writer trace proves that this is not an enemy-occupancy predicate
and is unreachable after board-status initialization:

- every cell starts at `0x20` (Number `0x1159F`, Word `0x108AB`);
- the only writer that can set bit `0x10` is the Troggle redraw operation
  `OR 0x58`, which simultaneously sets bits `0x08` and `0x40`;
- `AND 0xBF` can remove only `0x40`, leaving `0x18`, while the painter's final
  `AND 0x40` removes both `0x10` and `0x08` together;
- all other writers only set `0x20`, `0x40`, `0x60`, or `0x80`.

Consequently a reachable status containing bit `0x10` always also contains
bit `0x08`, until both are cleared; it can never equal `0x10`. The comparison
is a vestigial exact-value guard in both executables. Adding an enemy-at-cell
restriction to native would invent behavior that the executable does not
perform.

## Native lock

Both runtimes now derive `MunchAnimationDuration` from seven public ticks and
select records through the exact `12,13,14,13,12,13,14` visible sequence. The
terminal tick maps to record 13 for state auditing but resolves the answer in
that callback, before another chew interval is painted.

Both presentation hosts now preserve the scheduler creation boundary when a
queued second click starts a chew on the same public tick that a walk ends.
The recovered physical slot order is safe zones, player ID 4, Troggles, fixed
controllers, then Demo; the mask-2 pass therefore dispatches safe zones,
player, Troggles, and Demo. Elapsed time from before the terminal movement
callback is not subtracted from the newly created chew job. Each host frame is
split into original public ticks so a newly created job can advance only on a
later tick, not from bulk timer subtraction. See
`GAMEPLAY-SCHEDULER-STATIC-AUDIT.md` for both executable record tables and scan
addresses.

The seventh correct-answer callback reaches the board-live predicate directly
at Number `0x093F3` and Word `0x09FC0`. When the saved answer is the last
positive record, that predicate posts selector-100 action 6; the controller,
not the remainder of the old actor callback, owns direct next-board setup.
Both native hosts now tag each board generation and, when a presentation update
spans this terminal, abort the invalid old-board pass before dispatching later
old records. The tick driver then charges only complete post-callback ticks and
the fractional remainder to the newly installed jobs. The adversarial test
places the terminal one tick into a 2.25-tick update and compares it with direct
completion followed by 1.25 ticks: the level-2 PRNG state, every safe/Troggle
timer, and the quarter-tick accumulator must be identical.

The same recovered order closes two collision edges. A Troggle endpoint due
while the Muncher remains in state-5 chew cannot collide; a coalesced 2.25-tick
test reaches that endpoint, completes the saved correct chew on the following
tick, and retains its quarter-tick tail. Conversely, if a movement terminal
starts a collision in the player slot, later Troggle and Demo slots still run
on that public tick while the selected biter remains at collision age zero.
That same-tick tail preserves the captured Number Demo controller reload at
PRNG call 271.

Wrong-answer termination supplies an additional slot-order gate. Safe jobs,
which precede ID 4, receive all seven chew ticks; the terminal player callback
disables the later board jobs, so Troggle timers receive only the first six.
The feedback hold then freezes both exact timer values.

`game_render_state_test`, `word_content_static_test`, and
`munchers_app_headless_test` independently check the numerical record
sequence—including the duplicate-looking 13-versus-15 case—the exact duration,
immediate blank/count-staging boundary, saved-record terminal resolution,
zero-record eligibility guard, queued movement-to-chew creation boundary,
protected-chew endpoint collision, safe/player/Troggle terminal ordering, and
all seven visible pose intervals. They also lock the final-chew selector
handoff, its multi-tick presentation remainder, and the collision-start Demo
tail.

The visual gate no longer accepts merely alternating, internally consistent
frames. It independently requires BTMP 1006 records 12/14 to reference the
same exact `(132,137)` 45×30 crop and records 13/15 the same exact `(108,17)`
45×30 crop. On the fixed row-2/column-2 fixture, the complete erased-cell plus
sprite regions must alternate FNV-64 `0x1c94da58fe5737fd` and
`0x3fb6cdbad7ba0655` in the recovered seven-interval order. This locks the
source rectangles, transparency, measured yellow palette, board-relative
placement, and immediate label erasure together. Word's independent asset
gate requires the identical four source records before exercising the same
runtime sequence.

The preserved lossless Demo recording supplies the previously missing live
later-level full-frame oracle without opening DOSBox. On its second, Prime
board, frames `8462–8478` cover one complete correct munch of the `2` at
row 2/column 1. The seven stable capture intervals are `8462–8463`,
`8464–8466`, `8467–8468`, `8469–8470`, `8471–8473`, `8474–8475`, and
`8476–8478`; frame `8479` is the resolved standing pose. After discarding the
capture format's unused high byte to match the renderer's 24-bit pixel words,
the two complete 320×200 FNV-64 hashes are `0x09ee3b653a7a0c5f` and
`0xa2e8dc753a83a4b7`, alternating A/B/A/B/A/B/A. The fixture-free native
`0xF585` replay now requires every one of those seven hashes and the captured
25-point terminal state.

The new Word capture is `analysis/captures/wm_007.avi`, preserved as
`analysis/word-live/gameplay/word-gameplay-move-chew-live.avi`. Its first user
board reaches the word `said` by moving right and then performs one successful
chew. Stable cell hashes are closed `0x3fb6cdbad7ba0655` and open
`0x1c94da58fe5737fd`; frames 2523-2538 show the exact
closed/open/closed/open/closed/open/closed sequence. Frame 2530 is a one-refresh
partial sprite update and is excluded from the stable-pose oracle. The complete
movement/chew run table and that exception are reproduced by
`tools/audit_word_gameplay_capture.py` in
`analysis/word-live/gameplay/report.json`.

This live first-board Word pose also corrected the earlier palette conclusion.
Its one occupied index-111 pixel is `0x414100`, not `0x413d00`. Re-extracting
the first Number Demo board at frame 2544 finds the same `0x414100`, and a scan
of the complete preserved Number Demo AVI finds no `0x413d00` pixel. The old
first-board/post-Hall delta was generated only by native's former special case.
Both native runtimes now map that player-sheet slot to `0x414100` on every
board. The logical Hall/title latch remains tested because it controls page
lifecycle, but changing it is pixel-neutral for this sprite slot.

Restoring the state-4 collision gate invalidated an older native replay
expectation that allowed a Troggle collision roughly 20 ms after chew start.
The continuous headless replay is relocked on the later standing-state collision
and its resulting page/board/PRNG sequence; it is not presented as new live-DOS
capture evidence.
