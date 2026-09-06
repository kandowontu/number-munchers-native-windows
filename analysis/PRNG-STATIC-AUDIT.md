# Original PRNG static audit

This audit used only the unpacked executable and headless native tests. No
DOSBox or native game window was launched.

## Generator and seed

The runtime `rand` implementation at image `0x26538` stores a 32-bit state at
`DS:5076`/`DS:5078` and advances it as:

```text
state = state * 0x015A4E35 + 1   (mod 2^32)
rand  = (state >> 16) & 0x7FFF
```

This is the Borland 32-bit linear congruential generator with multiplier
22,695,477. The `srand` entry at image `0x26527` accepts one 16-bit word,
stores it as the low state word, and clears the high word. Startup at image
`0x0CAF5` passes the low word returned by the `time(nullptr)` path at image
`0x26A3D`, so only the low 16 bits of the epoch time seed the DOS stream.

## Range mapping

The game wrapper at image `0x0B024` computes `maximum - minimum`, calls the
generator, reduces the 15-bit result by that width through the signed-remainder
helper at image `0x02586`, and adds `minimum`. Therefore every recovered call
written as `random(minimum, maximum)` has an upper-exclusive maximum.

The native helper presents an inclusive C++ interface because existing call
sites already use `maximum - 1`; internally it performs the same modulo
reduction. A headless test locks the first eight outputs for seed 1:

```text
346, 130, 10982, 1090, 11656, 7117, 17595, 6415
```

It also verifies 16-bit seed truncation and modulo range mapping. The port no
longer uses `std::mt19937` or `uniform_int_distribution`.

## Preserved-demo seed and pre-board call order

The lossless unattended capture was created immediately after a DOS launch.
Its first Multiples-of-5 board has a complete visible 29-value fingerprint.
An exhaustive headless search over all 65,536 possible DOS seeds found one
unique match: seed `62853` (`0xF585`), with seven PRNG outputs consumed before
the first cell's correct/incorrect draw. That seed is the low word of epoch
time `2026-08-15 05:13:41 UTC` (`01:13:41 EDT`), about 1.5 seconds before the
recording began.

Static control flow accounts for all seven outputs:

| Call | Original operation | Raw output | Result |
|---:|---|---:|---:|
| 1 | demo tier `random(0,9)` at `0x089FD` | 8387 | 8; setup advances to pressure tier 9 / visible level 10 |
| 2 | Multiples target `random(2,21)` | 11403 | 5 |
| 3 | Factors target `random(3,100)` | 6999 | 18 |
| 4 | Equality target `random(1,51)` | 24407 | 8 |
| 5 | Inequality target `random(1,51)` | 17360 | 11 |
| 6 | enabled component `random(0,5)` at `0x0FAEE` | 29835 | Multiples |
| 7 | relation `random(0,3)` at `0x0FB2C` | 5656 | 1 |

The last call is easy to miss: the original spends a relation roll for every
mode, even though only Inequality displays it. Inequality target 1 is the sole
forced, no-roll exception. The next output, 16422, is therefore the first
cell's `random(0,2)` draw.

Native now follows this initializer rather than installing a captured board.
With seed 62853, the ordinary 30-second demo entry organically reproduces all
29 labels, Multiples-of-5 target, level/tier, interior player position, answer
count, safe-zone/enemy job counts, and the full captured 320x200 frame hash.
The player-position match also locks the original post-board draw order:
safe-zone initialization, column, then row.

## Remaining boundary

The PRNG algorithm, seed width, range mapping, preserved-run seed, and complete
call order now match through the first board/collision, the second Prime board
initializer at call 271 and its short wrong-answer route, both intervening
feedback/Hall/logo lifecycles, and the pixel-identical Equals-30 initializer at
call 334. That extension also locks the previously omitted random commutative-
operand swap after addition and multiplication. The replay now continues
through the third board's five correct munches and Worker bite: safe-zone row
and column selection consume calls 504/505, the bite terminal's fresh dwell is
call 506, and `0x109E3` selects the captured `Aargh` with call 507. The
An independently seeded initializer constructs the captured Multiples-of-15
board from call 507 through setup boundary 584, so that board remains valid
generator evidence. The former continuous route from there through a
Factors-of-63 setup at call 695 and external key at call 763 is withdrawn: it
admitted a collision while the Muncher was in protected player state 5, which
the recovered standing-only predicate makes impossible. The later
Factors-of-63 board remains useful only as an isolated recovered-seed/frame
oracle—including the dual-Helper window at frames 19530–19590—not as proof of
one synchronized whole-run PRNG trajectory. Native's corrected later route is
a deterministic regression gate without matching post-divergence DOS pixels.
