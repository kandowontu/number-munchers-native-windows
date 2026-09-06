# Level progression and cartoon-order static audit

This audit uses the preserved `NM-unpacked-image.bin` and its matching
`NM-unpacked.ndisasm`. It does not execute the DOS program.

## Completion and next-board states

The high-level state dispatcher begins at image `0x12831`. Its state-5 branch
at `0x12a49` calls far address `07e7:1919`, image `0x09789`, which is the
next-board initializer. Its state-6 branch at `0x12a52` reaches far address
`0f10:0001`, image `0x0f101`, which selects either the ordinary advance path
or a reward cartoon for the board that was just completed.

The next-board initializer at `0x09789` increments the zero-based pressure
tier at `DS:5ab2` only while it is below 11, then increments the visible level
at `DS:5868` without that cap. It resets the board jobs and answer records and
constructs the next board. Consequently the completed level remains current
while a cartoon is running; the visible level advances when cartoon teardown
returns the high-level sequence to state 5.

The final correct-chew callback reaches the board-live predicate at `0x093F3`
(Word `0x09FC0`). That predicate installs state 6 rather than letting elapsed
time from before the callback belong to the direct next board. Native tracks a
board-generation serial and transfers only a host update's post-terminal
remainder; focused tests compare a 2.25-tick spanning update with direct
completion plus its 1.25-tick tail and require identical fresh job timers. The
same gate now covers a third-board terminal: the remainder advances only the
synchronous board-to-cartoon Wipe while the initialized scene clock stays at
its setup tick.

## Exact cartoon cadence and shuffle

The completion selector at `0x0f101` implements the following order:

1. If the visible level is 1, set the cartoon cursor at `DS:118e` to zero and
   initialize the five words at `DS:5f7a` to `0,1,2,3,4`
   (`0x0f107`–`0x0f12e`).
2. Whenever the cursor is zero, loop over indices 0 through 4. Each iteration
   calls the Borland upper-exclusive `random(0,5)`, then swaps
   `order[randomIndex]` with `order[index]` (`0x0f130`–`0x0f181`). This is five
   independent full-range swaps, not Fisher-Yates.
3. Divide the current visible level by three. A nonzero remainder takes the
   direct next-board path (`0x0f183`–`0x0f1b7`). A zero remainder selects
   `order[cursor]` and dispatches that cartoon (`0x0f1ba`–`0x0f1f8`).
4. Cartoon teardown increments the cursor and wraps values above 4 back to
   zero (`0x0f2f7`–`0x0f320`).

Because the cursor does not advance on direct completions, levels 1, 2, and 3
all perform a five-call shuffle before the first displayed cartoon. After the
cartoons at levels 3, 6, 9, 12, and 15 consume the five slots, the cursor wraps;
levels 16, 17, and 18 likewise reshuffle before the next displayed cartoon.
The order array is not reset between those later shuffles.

The native port now retains this five-word order and cursor, spends the exact
five PRNG calls at the same completion condition, selects the corresponding
logical VGA/CGA cartoon and sound bank, advances the cursor only when the scene
closes, and advances the visible level only when the next board is built.
`game_render_state_test` locks three consecutive known-seed shuffle results,
the no-call interval while the cursor is nonzero, cursor wrap, shuffled scene
selection, animated scene output, and the level/cursor state on teardown.
`CARTOON-PRESENTER-STATIC-AUDIT.md` records the separate close/load/open call
chain and its deliberately non-capture-backed native presentation boundary.
