# Original difficulty/content presets

This note records the exact content preset applied by each entry on the original **Select Difficulty** page. It comes from static inspection of the preserved unpacked DOS load image; no emulator execution is required to reproduce it.

## Executable evidence

The selected difficulty is held at `DS:5f5e`. On Enter, the selector at load-image offsets `0x157ad`–`0x157d5` indexes `selected * 60 + DS:1d1a`, passes that record to the copy routine at `0x1548f`, stores the new selection, and writes the configuration.

`DS:1d1a` contains eleven consecutive 60-byte records. Each record contains six ten-byte rows in Multiples, Factors, Primes, Equality, Inequality, Challenge order:

| Row offset | Meaning |
|---:|---|
| `+0` | enabled byte |
| `+2` | minimum word |
| `+4` | maximum word |
| `+6` | sequence word: 1 in order, 2 random, 0 unavailable |
| `+8` | Multiples maximum multiplier or Equality/Inequality operation mask |

The copy-to-gameplay routine at `0x15578`–`0x1567f` confirms those interpretations. Operation-mask bits 0/1/2/3 select addition, subtraction, multiplication, and division.

## Recovered presets

The preset's sixth enabled byte controls whether Challenge appears as a menu entry. “Enabled” below lists the component games from which Challenge may choose; every supplied preset also enables Challenge itself.

| Difficulty | Enabled components | Multiples range / sequence / multiplier | Factors range / sequence | Primes range | Equality range / sequence / ops | Inequality range / sequence / ops |
|---|---|---|---|---|---|---|
| 3rd Grade Easy | Equality, Inequality | 2–5 / in order / 5 | 3–5 / in order | 2–25 | 1–20 / in order / `+ - x :` | 1–10 / in order / `+ -` |
| 3rd Grade Advanced | Multiples, Factors, Equality, Inequality | 2–5 / in order / 5 | 3–25 / in order | 2–25 | 1–20 / in order / `+ - x :` | 1–20 / in order / `+ -` |
| 4th Grade Easy | Multiples, Factors, Equality, Inequality | 2–9 / in order / 5 | 3–25 / in order | 2–50 | 1–24 / in order / `+ - x :` | 1–20 / in order / `+ -` |
| 4th Grade Advanced | Multiples, Factors, Equality, Inequality | 2–9 / random / 9 | 3–64 / random | 2–50 | 1–24 / random / `+ - x :` | 1–20 / random / `+ - x` |
| 5th Grade Easy | Multiples, Factors, Equality, Inequality | 2–9 / random / 9 | 3–64 / random | 2–50 | 1–24 / random / `+ - x :` | 1–24 / random / `+ - x :` |
| 5th Grade Advanced | all five | 2–11 / random / 13 | 3–81 / random | 2–50 | 1–50 / random / `+ - x :` | 1–24 / random / `+ - x :` |
| 6th Grade Easy | all five | 2–11 / random / 13 | 3–81 / random | 2–50 | 1–50 / random / `+ - x :` | 1–50 / random / `+ - x :` |
| 6th Grade Advanced | all five | 2–12 / random / 13 | 3–99 / random | 2–99 | 1–50 / random / `+ - x :` | 1–50 / random / `+ - x :` |
| 7th Grade Easy | all five | 2–12 / random / 18 | 3–99 / random | 2–99 | 1–50 / random / `+ - x :` | 1–50 / random / `+ - x :` |
| 7th Grade Advanced | all five | 2–20 / random / 20 | 3–99 / random | 2–199 | 1–50 / random / `+ - x :` | 1–50 / random / `+ - x :` |
| 8th Grade and Above | all five | 2–20 / random / 50 | 3–99 / random | 2–199 | 1–50 / random / `+ - x :` | 1–50 / random / `+ - x :` |

Primes has no sequence field because it has no key value to advance. Challenge has no independent range, sequence, or Other field.

## Challenge selection

At `0x0f989`–`0x0f9ad`, the original builds a compact list from the five component enable bytes at `DS:5a5a`; Challenge randomly selects only from that list. Separately, the Play selector at `0x12be9`–`0x12cdd` walks all six enable bytes, so the sixth byte controls whether Challenge appears. `DS:1cf6` and the Set Content loop at `0x1489c` prove that Challenge's Use field is editable. With only one enabled component, the UI displays `---` and `0x14c68` forces Challenge off on commit; the “no games” validation still inspects the first five rows because Challenge cannot supply a component by itself.

## Native lock

`src/game.cpp` now applies the complete 11×6 table whenever a difficulty is selected, resets ordered-sequence cursors, filters the Play selector by all six enable bytes, and limits Challenge boards to enabled components. `game_render_state_test` checks every recovered row and field, all operation bits, the 3rd Grade Easy filtered selector, Challenge disablement, the one-component forced-off rule, and repeated Challenge choices. Configuration persistence is tracked separately in `PARITY.md`.
