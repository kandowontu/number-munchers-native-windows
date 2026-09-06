# Cartoon score static audit

This audit is entirely offline. It uses the preserved Number and Word load
images plus their embedded ADLI banks; it does not launch DOSBox, either DOS
executable, the native GUI, or an audio device.

## Event routing and timing

After the opening Wipe and literal 10 ms delay, both every-third-board cartoon
loaders submit event `0xA6`: Number at `0x0F25C`-`0x0F266` and Word at
`0x0FEE0`-`0x0FEEA`.

The resident wrappers prove what that event means. Number's wrapper at
`0x10D2B` tests bit `0x80` at `0x10DB8`, requires an initialized driver, a
nonzero current device, and the Music flag, masks the event with `0x7F`, and
submits the result at `0x10DD8`-`0x10DE4`. Word's byte-isomorphic path is
`0x10033`, with the high-bit branch at `0x100C0`-`0x100EC`. Thus `0xA6`
selects bank-local stream 38. It is Music-class, independent of the Sound flag,
and is deliberately suppressed on the PC-speaker device.

This is separate from an SCPT opcode-`0D` callback. Those callbacks take the
ordinary Sound/device route and add three to their event before selecting a
stream. Stream 38 instead begins the cartoon's background score after the
scene has been installed and exposed.

## Embedded conductors

Stream 38 begins at resource offset `0x03D3` in every supplied cartoon ADLI
bank. Its header selects logical channel 9 with priority 4. The conductor sets
the two OPL depth bits (`AE 01`, `AF 01`), installs a bank-specific fractional
rate with opcode `A6`, launches its parts with opcode `82`, and ends. `FF 88`
records in Number bank 12 are the resident driver's intentional empty-stream
sentinels and are ignored exactly as its launcher does.

| Game | ADLI bank | `A6` rate | launched stream indices |
|---|---:|---:|---|
| Number | 11 | `93` | `C4 C3 C2 C1 C0` |
| Number | 12 | `C2` | `C4 C3 C2 C1 C0 BF BE` |
| Number | 13 | `8C` | `C4 C3 C2 C1 C0 BF` |
| Number | 14 | `9A` | `C4 C3 C2 C1 C0 BF BE BD` |
| Number | 15 | `A8` | `C4 C2 C0 BF BE` |
| Word | 1 | `AF` | `C4 C3 C2 C1 C0 BF` |
| Word | 2 | `A8` | `C4 C3 C2 C1 C0 BF` |
| Word | 3 | `97` | `C4 C3 C2 C1 C0` |
| Word | 4 | `A8` | `C4 C3` |
| Word | 5 | `A8` | `C4 C3 C2 C1 C0 BF` |
| Word | 6 | `C2` | `C4 C3 C1 C0 BF BE BD BC` |

Number bank 15 also executes `9C 06`, resetting the same auxiliary resident
timing state used by the long Demo conductor; it emits no immediate OPL write.
Every real launched part begins with `C8 01`, so its 8-bit phase accumulator
follows that conductor's recovered `A6` rate rather than the default `FF`
rate. Repeated logical-channel launches retain the original priority/replacement
semantics; only its immediate opcode-`82` channel-preparation writes can precede
its successor, while replaced bytecode never dispatches.

The newly reachable parts require these additional exact patch-table keys from
the two executable images:

- Number: `69 98 C5 C6 C7 CF D0 D1 D4 D5 D8 D9`;
- Word: `02 0A C5 C6 C7 CC D0 D1 D2 D6 D7 DD DE DF E3 E4 E5`.

Their 11-byte records are now copied from each game's own table at image
`0x1956`; shared-looking keys are not assumed to have shared records.

## Deterministic decoder evidence

The headless decoder follows each conductor into its live channel parts,
applies the bank's fractional rate, preserves equal-priority logical-channel
replacement, and sorts the resulting timestamped register schedule. The hash
folds the duration, write count, and every `(milliseconds, register, value)`
tuple.

| Game | Bank | Duration (ms) | OPL writes | Static/native FNV-64 |
|---|---:|---:|---:|---|
| Number | 11 | 18,368 | 319 | `ebfa1bcd24fa66f5` |
| Number | 12 | 19,191 | 1,210 | `07c9337c6978f332` |
| Number | 13 | 26,495 | 1,040 | `15a1b65d33897586` |
| Number | 14 | 17,516 | 549 | `f740bb586aae2df8` |
| Number | 15 | 19,562 | 779 | `faaa5af1426219b9` |
| Word | 1 | 11,792 | 544 | `72e0ab09089fa021` |
| Word | 2 | 16,048 | 488 | `d6c5c96bc730d4dc` |
| Word | 3 | 15,622 | 287 | `a5560bea60d78119` |
| Word | 4 | 12,039 | 183 | `3a804757eac7d440` |
| Word | 5 | 14,043 | 587 | `233041229c488dd2` |
| Word | 6 | 28,032 | 1,240 | `4a3f72d537a0df01` |

## Native lifecycle and boundary

The selected-scene loader first retires ordinary completion stream 15 through
GSND 0 and holds the covered frame for the original literal 15 ms. A channel-0
callback started later during scene setup remains live. At the final complete-
scene presenter frame, native retains the separate original 10 ms blocking
delay before dispatching stream 38 and reopening input/scene ticks. The score
is installed as a non-repeating channels-1-8 schedule on the already live
YM3812 without resetting that setup callback; later opcode-`0D` callbacks
continue replacing only channel 0. Scene teardown or skip submits GSND 0 in
the inner helper, waits 15 ms, and submits GSND 0 again in the outer caller,
retiring both logical owners before the next board without resetting or
closing the resident YM3812.

Regression tests decode all eleven conductors, lock the table above, require
the `BD=80` then `BD=C0` depth writes, prove that Music-off and speaker
selection suppress the start while Sound-off does not, preserve an already
queued scene cue, and require the loader's completion-cue cutoff plus teardown
retirement. The integrated gates clock the score, remember its `BD` depth
bits, then require both sets of 36 channel-retirement writes and their masked
`C3` results with a 745-sample gap before the second stop while the chip owner
remains live and silent. These establish
the executable routing, resource selection, schedule, and native ownership
lifecycle. Physical AdLib filtering/output and synchronized audible comparison
with a live DOS run remain open under the no-window constraint.
