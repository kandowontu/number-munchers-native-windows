# Board-generator static audit

This audit records generator behavior recovered directly from
`analysis/NM-unpacked.ndisasm`. Image offsets below refer to the unpacked DOS
load image; the 92-byte Prime table is independently preserved as
`extracted/resources/NM/DATA/00002.data`.

## Board construction

- `0x0f787` draws upper-exclusive `random(0,2)` independently for each cell,
  selecting a correct or incorrect mode-specific generator with equal
  probability.
- `0x0fb52` fills all 30 cells, counts positive/correct records, and retries the
  complete board only when it produced fewer than two correct records.
- Safe-zone initialization and the interior player-position initializer run
  afterward. The selected player cell is cleared, so a displayed board can
  contain one fewer answer than the accepted generated board.
- `0x0fa23` advances all ordered/random content streams before every board,
  not merely the stream selected for play. Random streams reject an immediate
  repeat. The Prime ceiling advances by one table entry and stops at the
  configured upper limit. Challenge chooses its component only after these
  updates.
- `0x0fb16` then assigns the relation word after every component selection.
  All modes spend a `random(0,3)` call even when the relation is unused; only
  Inequality target 1 bypasses the call and forces Greater Than.

## Multiples and Factors

- Correct Multiples (`0x0fecd`) are `target * random(1,maxMultiplier)`.
- Incorrect Multiples (`0x0ff01`) rejection-sample uniformly from
  `1..target*maxMultiplier` until the value has a nonzero remainder.
- Correct Factors (`0x0fc5a`) choose uniformly from the complete descending
  divisor table built by `0x0100f2`.
- Incorrect Factors (`0x0fc74`) repeatedly choose an adjacent divisor gap
  until it is nonempty, then choose uniformly from the integers strictly
  inside that gap. This never produces a value above the target.

## Prime Numbers

- Startup at `0x0caa0` loads resource type 7, id 2. Its 46 little-endian words
  are exactly the primes from 2 through 199.
- `0x0f8a4` converts the configured lower and upper values into an initial
  maximum-table cursor and a final cursor limit. `0x0fa23` advances the current
  maximum once per board, so a default `2..199` game begins with only 2 and
  grows through one additional prime per board.
- Correct cells (`0x0ff35`) uniformly choose table indexes `0..currentMaximum`.
- For ceilings below the fourth table entry, incorrect cells (`0x0ff53`) use
  1, except that a ceiling of 5 chooses 1 or 4 equally.
- At larger ceilings the incorrect generator selects a gap index. Index zero
  still yields 1; other indexes call `0x010151`, which samples between the
  primes two positions apart and rejects the middle prime. The result is
  therefore a composite drawn from the two adjacent gaps and never exceeds
  the current Prime ceiling.

## Equality and Inequality results

The nearby-offset table at `DS:122e` is `{1,2,3,4,10}`; `DS:1238` is the same
table with a leading zero.

- Incorrect Equality (`0x0fb99`) forces a result above target 1. Other targets
  choose lower or upper equally. For targets below 11, lower results remain
  positive; larger targets use the five strict offsets. Upper results always
  use the five strict offsets.
- Inequality relation values are 0 Less Than, 1 Not Equal, and 2 Greater Than.
  The selector at `0x0fb1d` forces Greater Than for target 1 and otherwise
  chooses all three equally.
- Correct Inequality (`0x0fce3`) uses a strict lower result for Less Than, a
  50/50 strict lower/upper result for Not Equal, and a strict upper result for
  Greater Than.
- Incorrect Inequality (`0x0fe22`) uses upper-or-equal for Less Than, exactly
  equal for Not Equal, and lower-or-equal for Greater Than.

## Expression operands

`0x0ffd1` uniformly selects among the enabled operation indexes and constructs
an integer expression for the already-selected result:

- addition chooses one operand from `0..min(result,9)` and derives the other;
- subtraction chooses the right operand from `0..9`;
- multiplication uniformly chooses a divisor of the result from the exact
  descending table `result ... 1`; the selected divisor is the left operand
  (and result 1 retains the original duplicate `{1,1}` table entries);
- division chooses the divisor from `1..min(floor(100/result),9)`, keeping the
  dividend at or below 100.

After constructing addition and multiplication operands, helper `0x10531`
always spends `random(0,2)` and swaps the two operands when it returns zero.
Subtraction and division do not call the helper. This random commutative swap
is observable both in labels and in every later PRNG decision.

The native implementation and `game_render_state_test` now enforce these
distributions, the Prime growth/cap behavior, immediate-repeat rejection, all
stream advancement, and the relation-number mapping/call. The recovered DOS
seed and call order reproduce the captured first and third attract frames and
the independently seeded Multiples-of-15 initializer bit-for-bit. The later
Factors-of-63 frame remains a direct isolated oracle for the descending
correct-divisor table: the same random indexes would produce different labels
from an ascending table. It is not continuous trajectory evidence. The former
call-695/call-763 route admitted an impossible player-state-5 collision and is
withdrawn; the corrected post-divergence native route is deterministic-only.
