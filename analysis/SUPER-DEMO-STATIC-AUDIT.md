# Super Munchers attract-demo controller static audit

Date: 2026-08-28

## Scope

This audit pins the autonomous title-to-game demonstration to the unpacked
`SM.EXE` image. It replaces the earlier native approximation that selected the
nearest correct cell, acted on a floating-point timer, always started at level
1, and stopped after an invented 60-second limit.

## Source controller

The demo callback occupies image offsets `0x085E1..0x08739`. Its scheduler
record is the 20-byte object at relocated image offset `0x27276` (`DS:03B6`):

```text
07 00 05 00 00 00 02 00 1E 00 00 00 1E 00 00 00 00 00 00 00
```

The two `0x001E` fields establish the initial 30-tick due time. The board-job
constructor copies this record only in demo mode at `0x09E99..0x09EB4`, after
the ordinary board jobs, making the demo controller the last scheduled job.

On every due callback the executable:

1. draws `random(0,30)` and reloads the timer to that result plus 15, producing
   the inclusive 15..44-tick interval;
2. reads the signed word for the Muncher's current board cell;
3. emits Space immediately when that word is positive;
4. for a negative word, emits Space only when `random(0,10)` returns zero;
5. otherwise draws `random(0,4)`, tests the adjacent cells in clockwise
   up/right/down/left order starting at that direction, and selects the first
   in-bounds positive cell; and
6. after four unsuccessful tests, emits the originally drawn direction.

The final key mapping at `0x08714..0x0872F` is `0=I`, `1=K`, `2=M`, `3=J`.
Thus the original demo uses the same letter aliases as ordinary player input;
it does not perform global pathfinding.

The demo start path at `0x08CD3..0x08CE9` draws `random(0,9)`, stores that
zero-based value, and displays level 1..9. This draw precedes board generation
and is therefore part of the authoritative PRNG sequence.

The title scheduler setup at `0x14537..0x1454E` writes `0x01C2`, proving the
450-tick title-idle delay. No demo-local total-duration counter exists. Demo
termination is instead reached through the normal failure/collision routes or
an external key; the former native 60-second cutoff had no source counterpart.

## Native contract and gates

`SuperGame::startDemo`, `SuperGame::updateDemo`, and the physical scheduler
order now implement the controller above with integer scheduler ticks. The
headless `attractDemoRoute` gate covers:

- no launch through tick 449 and launch on tick 450;
- the level-selection draw occurring before board generation;
- the initial 30-tick delay and every 15..44-tick reload;
- positive-cell Space, negative-cell 1-in-10 Space, clockwise neighbor scan,
  and the four-failure fallback direction;
- due-callback PRNG consumption while the player is busy;
- last-in-scheduler placement; and
- external-key exit back to the title with audio stopped.

All branches are deterministic and pass in `munchers_app_headless_test`.
