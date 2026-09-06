# Word Munchers scoring, lives, and progression static audit

This audit uses only the preserved Word Munchers load image.  It does not run
DOSBox or either game executable.

## Correct-word scoring

The correct-cell resolution calls the score routine at image `0x08A47` from
image `0x09FB8`.  The routine snapshots the prior 32-bit score at
`DS:5C0A/5C0C`, then adds points selected by the visible level at `DS:5C0E`.
The 16 signed words at `DS:12C6` are:

```text
level:   1  2  3   4   5   6   7   8   9  10  11  12  13  14  15  16
points:  5  5  5  10  15  20  25  30  35  40  45  50  55  60  65  70
```

Level 17 and above takes the explicit fallback branch at `0x08A81` and awards
75 points.  This is byte-for-byte the same point schedule statically recovered
from Number Munchers.

After the add, the routine compares the 32-bit score to `000F:4240`
(1,000,000).  Reaching or exceeding that value immediately calls the scored
game terminal at far `08A4:0A3F`; it does not continue into the bonus branch.
The Hall path at image `0x0D060` clamps an overshoot to exactly 1,000,000 before
admission and display.

The chew caller makes the terminal ordering explicit. Its correct branch calls
the score routine at `0x09FB8`; only the subsequent instructions at `0x09FBC`
and `0x09FC0` reach the actor reset and board-empty check. Thus a million-point
award takes the scored terminal before a simultaneously emptied board can
schedule stream 15, a level-complete action, cartoon selection, or cartoon
shuffle. Native gates the adversarial level-18 case in both the core and the
presenter: the last correct word reaches 1,000,000, enters perfect-score name
admission, leaves board-complete/cartoon state unset, retains cue 7 as the last
gameplay cue, and consumes no scene PRNG calls.

## Bonus Munchers

The bonus branch compares the previous and new values in two stages:

- a one-time crossing from below 1,000 to at least 1,000 awards one reserve;
- otherwise it compares the previous and new remainders modulo 10,000, and a
  remainder wrap awards one reserve at every 10,000-point boundary.

The reserve at `DS:5DAE` is incremented without an internal cap.  The sound/UI
notification is emitted only while the new reserve count is at most three,
but the value itself remains larger.  The HUD loop at image `0x11002` also has
an independent three-icon ceiling before comparing against `DS:5DAE`.

## Four-Muncher lifecycle

New-game setup at image `0x095DC` writes reserve count 3 and clears the 32-bit
score.  Both loss paths eventually decrement the reserve at image `0x09E70`;
the terminal branch is taken only when the signed result is below zero.  Thus
the active Muncher plus three displayed reserves gives four total Munchers,
matching Number Munchers while preserving bonus reserves beyond the HUD cap.

## Level pressure and cartoons

The level-advance routine at image `0x0A356` increments the visible level on
every completed board.  Its separate internal pressure index at `DS:5EBC`
increments only while below 11, producing twelve capped difficulty tiers while
the visible level continues upward.

The scene selector at image `0x0FD74` initializes and full-range-shuffles a
five-entry cartoon order.  Ordinary cartoons are considered whenever the
visible level is divisible by three.  Word Munchers also has a sixth SCPT
payload: the explicit level-18 branch at `0x0FE5B` selects scene index 5 rather
than the current item in the shuffled five-scene order.

The completed ordinary-game call trace closes the cursor boundaries. At
`0x0FD7A`, completed level 1 resets the order to `0,1,2,3,4` and its cursor to
zero. Whenever that cursor is zero, `0x0FDA3` performs five independent swaps:
loop index 0–4 is swapped with one full-range `random(0,5)` result. This occurs
before the divisible-by-three test, so ordinary completed levels 1, 2, and 3
each reshuffle while the cursor remains zero. Scene teardown at `0x0FFAD`, not
selection, advances the cursor and wraps it after 4. Level 18's special scene 5
follows the same teardown and therefore consumes the current ordinary cursor
slot even though the shuffled scene at that slot was not shown.

Demo completion does not enter this selector. The empty-board branch at
`0xAD76` calls the unattended terminal at `0x947F`; the selector at `0xFD74` is
called only from ordinary main-screen action `0x120F9`. Native formerly hid the
cartoon in Demo but still consumed its five shuffle draws. That PRNG drift is
now corrected and locked at the static/headless level in
`WORD-PRNG-STATIC-AUDIT.md`.

## Native reuse boundary

The common point/bonus/reserve/one-million rules now live in the shared
`MuncherScore` module. Number's existing runtime delegates to it without
changing its total-life interface, and `WordGameCore` applies it to native
Word correct/wrong cells and capped pressure progression under independent
headless tests; see `WORD-GAME-CORE-STATIC-AUDIT.md`. The presenter now hosts
chew timing, the 21-tick collision loss, the rendered level-18 scene lifecycle,
Hall admission, name entry, post-game Hall context, and replay routing under
separate static/headless gates; see `WORD-TROGGLE-STATIC-AUDIT.md`,
`WORD-SCENE-STATIC-AUDIT.md`, and
`WORD-FEEDBACK-TERMINAL-STATIC-AUDIT.md`. Configuration editing/persistence,
live frame comparison, and physical audio remain outside these gates. The
complete executable random-call inventory and integrated ordering gate are in
`WORD-PRNG-STATIC-AUDIT.md`.
