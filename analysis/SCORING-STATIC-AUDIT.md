# Scoring and bonus-Muncher static audit

This audit uses the preserved `NM-unpacked-image.bin` and
`NM-unpacked.ndisasm`. It does not execute the DOS program.

## Per-answer points

The scoring routine begins at image `0x07e7a`. It snapshots the previous
32-bit score from `DS:5864/5866`, then reads the visible level at `DS:5868`.
Levels 1 through 16 index the word table at `DS:0654`; levels above 16 take the
literal 75-point branch at `0x07eb4`. The recovered table is:

`5, 5, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70`

The selected amount is added to the full 32-bit score at
`0x07eaa`–`0x07eb9`.

## One-million terminal

Immediately after the update, `0x07ece` compares the score against
`000f:4240`, decimal 1,000,000. Reaching or exceeding that value calls the
ordinary game terminal at far `07e7:0a42`, image `0x088b2`, and skips the bonus
test. The Hall admission routine at `0x0c66b`–`0x0c688` clamps an overshoot to
exactly 1,000,000 before ranking or name entry.

The terminal is the same user-game path used after ordinary life exhaustion:
it performs Hall admission, obtains a name when the score qualifies, displays
the current Hall, and proceeds to the replay question. It does not advance to
another board or reward cartoon even when the million-point answer was the
last answer on the board.

## Bonus Munchers

The first comparison at `0x07ee7` awards one Muncher when the previous score
was below 1,000 and the updated score is at least 1,000. All subsequent awards
use a different rule:

1. `0x07f07` divides the updated score by 10,000 and retains its remainder.
2. `0x07f1f` does the same for the previous score.
3. `0x07f31`–`0x07f39` awards a Muncher when the updated remainder is smaller,
   proving that every crossed 10,000-point boundary is rewarded.

The award at `0x07f3b` increments `DS:5998` without an artificial 99-life cap.
Only the redraw notification is conditional on the result remaining at most
three, because the HUD intentionally displays no more than three reserve
icons. Higher reserves remain internally usable.

Native previously implemented only the 1,000 and 10,000 bonuses and capped
internal lives at 99. It now reproduces the one-time 1,000 award, every later
10,000-boundary wrap, uncapped internal reserve count, one-million terminal,
exact Hall cap, and the qualifying versus full-list routing. The headless suite
tests 1,000, 10,000, 20,000, 990,000, a non-boundary award, the million-point
no-bonus edge, a life count above 99, and both terminal destinations.
