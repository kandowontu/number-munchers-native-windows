# Super Munchers game-core audit

Date: 2026-08-28

The native `SuperGameCore` owns every board mutation used by the runtime. It
uses the already-gated `SuperBoardGenerator`; the presenter cannot invent,
reclassify, or score a cell independently.

## Recovered behavior

- The 16-entry level table is `5,5,5,10,15,...,70`. Advanced adds five and
  Genius adds ten through level 16; every difficulty scores 75 above level 16.
- A fresh session has one active Muncher and three reserves. The shared
  million-point cap and bonus-Muncher thresholds remain in `MuncherScore`.
- Correct answers increment the transformation meter after the score and
  board-empty probes. Demo begins at 16; normal games begin at zero.
- At 20, image `0x089E5` searches interior cells in three passes: empty,
  incorrect, then any. It always excludes the player's cell and performs one
  random selection from the first nonempty pass.
- Eating that replacement activates Super form. Answer Vision is legal only
  in Super form and consumes two meter units per drain callback instead of
  one. The form ends only when the recovered subtraction becomes negative.
- Losing a Muncher clears the entire transformation state. A transformed
  collision defeats the Troggle and adds 50 through the source's direct-score
  branch.

`super_content_static_test` fixes the first two boards, their PRNG call count
and state, all correct-answer awards, the transformation-cell choice, Answer
Vision drain boundary, transformed collision award, and wrong-answer life
loss.
