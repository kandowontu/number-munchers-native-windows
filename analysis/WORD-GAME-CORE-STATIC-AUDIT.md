# Word Munchers native game-core static audit

This gate is headless. It joins the already recovered `WLIST.BIN` board
generator to exact shared score/life/progression state without launching
DOSBox, either DOS executable, or the native GUI. It is a runtime-core
prerequisite, not a claim that Word presentation, input, enemies, or menus are
finished.

## Shared scoring extraction

`MuncherScore` is now the single native implementation of the score rules
which the supplied Number and Word executables share:

- visible levels 1–16 award
  `5,5,5,10,15,20,25,30,35,40,45,50,55,60,65,70` points;
- level 17 and above awards 75;
- crossing 1,000 once, then each 10,000 remainder wrap, adds an uncapped
  reserve Muncher;
- reaching or overshooting 1,000,000 clamps to exactly 1,000,000 and returns
  before the bonus branch;
- the state stores three fresh-game reserves, displays at most three reserve
  icons, and ends only when the fourth loss decrements the reserve count below
  zero.

Number's existing `Game::addScore` now translates its total-Muncher field to
the shared reserve form at one boundary and back again. Its full headless
suite remains unchanged and passing, which prevents this extraction from
silently changing the already measured Number behavior.

## Word board owner

`WordGameCore` now owns the non-presentation state for one Word game:

- the configured 20-sound selection and one of eight word difficulties;
- exact `WordBoardGenerator` output and all 30 per-cell eaten flags;
- zero signed-source rejection before the gameplay cue, followed by chew-start
  staging that immediately marks the live cell eaten without changing the
  remaining count, snapshot-based correct/wrong terminal resolution, and
  repeated-cell rejection;
- remaining-target count, shared point awards, bonus reserves, four-Muncher
  game-over state, and one-million termination;
- visible level starting at 1 and the separate pressure tier starting at 0;
- board advance increments the visible level every time and caps pressure at
  tier 11, preserving twelve pressure tiers;
- cell clearing, regeneration, restoration, target recount, and deferred life
  loss exposed at explicit host boundaries for the recovered Word Troggle
  trails and collision job;
- the five-slot cartoon order's completed-level-1 reset, five full-range swaps
  whenever its cursor is zero, every-third-level trigger, teardown-only cursor
  advance/wrap, and level-18 scene-5 override that still consumes a cursor
  slot.

The class deliberately leaves movement, chew/death timing, Troggle job
scheduling, scene rendering/audio teardown, and page routing to the presenter.
The presenter now hosts the statically recovered Troggle and safe-zone rules;
their source addresses and separate headless gate are recorded in
`WORD-TROGGLE-STATIC-AUDIT.md`.

## Regression evidence

`word_content_static_test` now enforces the exact 20 board labels from the
executable, every level point value, both bonus boundaries, the one-million
early terminal—including a level-18 final-answer collision with board
completion/cartoon work—the uncapped/internal versus three-icon reserve behavior, and
all four fresh-game losses. A seed-`0x1234` two-sound game still produces its
first board after exactly 244 original PRNG calls. The test stages a correct
cell, first rejects a synthetic zero record with its auxiliary flag clear,
verifies the immediate eaten flag with an unchanged remaining count, resolves
it, rejects a second munch of it, consumes a wrong cell with one reserve loss,
finishes the board, and advances through completed level 18 while
proving the pressure tier stops at 11. It also enforces each five-call shuffle,
blocks board advance until cartoon teardown, checks teardown-only cursor
advance, and requires scene index 5 exactly at level 18.

The adjacent `WordHall` state also loads the seven active supplied ranks and
implements the statically recovered 50-point floor, stable tie ordering,
strict full-list cutoff, one-million clamp, name limit, and ten-entry cap; see
`WORD-HALL-STATIC-AUDIT.md`.

This establishes reusable state mechanics only. The adjacent runtime gates now
cover native player animation, Troggle/safe-zone scheduling, scripted scenes,
feedback/post-game routing, and shared-launcher lifecycle at the
static/headless level. They do not prove live Word full-frame pixels, gameplay
audio, or Hall presentation/persistence.
