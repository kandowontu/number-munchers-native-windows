# Word Munchers PRNG and call-order static audit

This audit was produced from `WM-unpacked-image.bin`,
`WM-unpacked.ndisasm`, and deterministic in-memory native tests. It did not
start DOSBox, `WM.EXE`, the native game, an audio device, or a visible window.
It closes the generator, all executable range-wrapper call sites, and the
native ordering described below at the static/headless level. It is not a live
Word Munchers trajectory capture.

## Generator, seed, and range wrapper

Word's `srand` entry at load image `0x21E67` writes its 16-bit argument to
`DS:532E` and clears the high state word at `DS:5330`. The `rand` entry at
`0x21E78` advances that state with the same Borland generator as Number:

```text
state = state * 0x015A4E35 + 1   (mod 2^32)
rand  = (state >> 16) & 0x7FFF
```

Startup at `0xD79F` seeds it from the low word returned by the time path.
The game wrapper at `0xBC05` subtracts the lower bound from the upper bound,
calls `0x21E78`, takes the signed remainder, and adds the lower bound. Every
range below is therefore upper-exclusive. Native `OriginalRandom` truncates
the seed to 16 bits, advances the same 32-bit state, returns the same high 15
bits, and uses the same modulo mapping.

## Complete wrapper-call inventory

The five-byte far-call signature `9A 45 08 3C 0B` targets `0xBC05`. It occurs
exactly 33 times in the Word load image. Every site is accounted for here:

| Load-image site(s) | Range | Executable purpose | Native owner |
|---|---:|---|---|
| `0x8231` | `0..1` | per-cell correct/distractor choice | `WordBoardGenerator::nextCell` |
| `0x824E` | target-pool count | correct-record index | `WordBoardGenerator::nextCell` |
| `0x8279` | `0..179` | sampled distractor-record index | `WordBoardGenerator::nextCell` |
| `0x83E7` | enabled Group-3 count | first non-self distractor sound | `WordBoardGenerator::configure` |
| `0x83FF` | enabled Group-3 count | second non-self/distinct distractor sound | `WordBoardGenerator::configure` |
| `0x846F` | tuple count | Demo target tuple | `WordBoardGenerator::nextBoard` |
| `0x8636` | available record count | each distractor-pool sample, after record 0 | `WordBoardGenerator::buildPools` |
| `0x89BF` | tuple count | ordinary full-range tuple swap, retrying self | `WordBoardGenerator::shuffleTuples` |
| `0x8CDE` | `0..1` | Smarty's 97–192-pixel conditional chase | `WordGame::moveEnemy` |
| `0x8D1F` | `0..9` | distant Bashful steering | `WordGame::moveEnemy` |
| `0x8D53` | `0..9` | Worker/Helper steering | `WordGame::moveEnemy` |
| `0x9008` | `0..3` | Troggle entry edge | `WordGame::beginEnemyWarning` |
| `0x9041`, `0x907E` | `0..4` | column on the two vertical edges | `WordGame::beginEnemyWarning` |
| `0x90AD`, `0x90E4` | `0..5` | row on the two horizontal edges | `WordGame::beginEnemyWarning` |
| `0x919B` | `0..29` | Demo-player callback reload, then `+15` | `WordGame::updateAttractPlayer` |
| `0x91D7` | `0..9` | one-in-ten negative-cell Demo munch | `WordGame::updateAttractPlayer` |
| `0x91F7` | `0..3` | initial Demo movement direction | `WordGame::updateAttractPlayer` |
| `0x9538` | `0..2` | safe-zone interior row, then `+1` | `WordGame::chooseSafeZoneCell` |
| `0x9558` | `0..3` | safe-zone interior column, then `+1` | `WordGame::chooseSafeZoneCell` |
| `0x95CA` | `0..8` | Demo pressure tier/visible level minus one | `WordGame::startAttract`, `startNextAttractBoard` |
| `0x96F4` | `0..3` | player interior column, then `+1` | `WordGame::initializePlayer` |
| `0x9707` | `0..2` | player interior row, then `+1` | `WordGame::initializePlayer` |
| `0x98B5`, `0x992C` | `0..209` | active/remaining safe-job period, then `+210` | `WordGame::initializeSafeZones` |
| `0x9AF2` | `0..99` | pressure-row Troggle species selection | `WordGame::chooseEnemyType` |
| `0x9B8C` | `0..89` | initial arrival jitter | `WordGame::enemySpawnDelay` |
| `0x9D0D` | `0..1` | left/right retry around a safe cell | `WordGame::moveEnemy` |
| `0x9F54` | `0..89` | retired/exited-slot arrival jitter | `WordGame::rearmEnemySlot` |
| `0xA740` | `0..89` | endpoint dwell, then `+30` | `WordGame::enemyDwellDelay` |
| `0xD4C4` | `0..4` | collision exclamation | `WordGame::updateDeathAnimation` |
| `0xFDB8` | `0..4` | five full-range cartoon-order swaps | `WordGameCore::prepareCartoonForCompletedLevel` |

This inventory also distinguishes repeated operations that share one native
helper. For example, the four edge-coordinate sites collapse to the one
conditional coordinate draw in `beginEnemyWarning`, while the two safe-period
sites collapse to the active-first loop order in `initializeSafeZones`.

## Integrated board-start order

The original new-board caller orders the consumers as board generation,
safe-job initialization, player initialization, then Troggle initialization.
Native now locks the same complete sequence from seed `0x1234` using the
supplied two-sound configuration:

1. 244 calls construct the ordinary board, including forced-non-self tuple
   swaps, 180 sampled distractors, and the 30 two-draw cells;
2. four calls initialize level-1 safe jobs: first period, active cell row and
   column, then the second period;
3. two calls choose player column before row; and
4. two calls choose the one level-1 Troggle species before its arrival jitter.

The resulting 252-call state is `0x0D0015F8`. A structural FNV-64 snapshot of
the complete board records/classifications, eaten spawn cell, safe-job
periods/state, player position, and enemy type/arrival is
`0x8732896F1FCF0F06`. Forcing the first warning consumes exactly edge then
coordinate, reaches state `0x2490FC6E`, and selects edge/row/column `2/0/0`.
Resolving that entry endpoint consumes one dwell draw and reaches
`0xEB99C6C7`. The collision terminal separately consumes exactly the dwell
and five-way exclamation draws.

Selected-sound construction is independently locked. With all sounds enabled,
difficulty 0, and seed `0x574F5244` (low word `0x5244`), the five randomized
Group-3 tuples are:

```text
15/17/18  16/19/17  17/19/18  18/17/16  19/17/18
```

The two rejection loops consume 13 calls and leave state `0xEA11E0D9`.

## Corrected Demo-completion boundary

The board-empty test at `0xAD76` sends Demo directly to the unattended
terminal at `0x947F`. The only call to cartoon selector `0xFD74` is ordinary
main-screen action `0x120F9`. Native previously invoked the selector inside
the shared core for every completion; although it suppressed the actual Demo
cartoon, it still spent five `random(0,5)` shuffle draws. That shifted every
later Demo board, Troggle, safe-zone, and autonomous-player result.

`WordGameCore::prepareCartoonForCompletedLevel` now exits before any draw when
the core is in Demo mode. Both direct correct-word completion and integrated
attract routing require zero calls at that boundary. Ordinary completed levels
retain the five full-range swaps whenever the cartoon cursor is zero.

The independent scored-terminal boundary is also locked: when level 18's last
correct word raises the score to 1,000,000, image `0x08AAC` reaches the scored
terminal before the chew caller's later board-empty path. Core and presenter
tests require zero cartoon draws, no pending scene, and no stream-15 replacement
of the correct-word cue in that simultaneous maximum/final-answer case.

## Evidence boundary

The generator, range mapping, all 33 wrapper sites, conditional consumption,
board-start order, warning/dwell/collision boundaries, tuple rejection loops,
and Demo-completion bypass are closed at the static/headless level. Muted live
startup and one user gameplay slice now exist, but no organic DOS-to-native
whole-session PRNG trajectory or live trajectory/audio comparison was captured.
This audit does not claim those broader live comparisons.
