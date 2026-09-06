# Super Munchers SCPT and mission-callback audit

Date: 2026-08-28

## Container and selected threads

Super uses the shared length/opcode/payload SCPT VM recovered for Number and
Word. The five scripts contain only opcodes `01` through `0E`, `FE`, and `FF`.
The controller's constructor calls at image `0x128B8`-`0x12DAE` recover these
exact construction lists:

| SCPT | raw threads | selected construction order | terminal tick |
|---:|---:|---|---:|
| 2022 | 7 | 0, 1, 2, 3, 4, 5, 6 | 59 |
| 2024 | 7 | 1, 0, 5, 6 | 104 |
| 2026 | 7 | 3, 4, 5, 6, 0, 1, 2 | 180 |
| 2028 | 10 | 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 | 321 |
| 2030 | 10 | 4, 7, 3, 5, 6, 8, 0, 1, 2, 9 | 185 |

The lists are backed by the contiguous signed-word tables at `DS:13AE` through
`DS:13F7`. Script 2024 deliberately leaves its nonzero threads 2, 3, and 4
unconstructed. All five selected timelines finish normally at signal 99.

## Opcode FE

The shared dispatcher invokes an optional far host callback for opcode `FE`.
It passes the command's word payload, the live actor, and the first-visit flag;
the returned byte decides whether the VM advances. A null callback advances
without mutation. Super's controller switch at image `0x13905` installs these
mission handlers:

| mission | handler | recovered behavior |
|---:|---:|---|
| 0 | `0x139EB` | choose a three-frame random member of a six-frame variant group, or its fourth frame |
| 1 | none | selected threads contain no `FE` |
| 2 | `0x13B16` | consume four distinct preselected digits and choose frames 5-14 |
| 3, difficulty 6 | `0x13982` | per-actor toggle; every second call chooses frame 7/9 from whether the right edge exceeds x=96 |
| 3, other difficulties | `0x13A49` | choose x=`rand()%24*8`, choose frame 12-19 with the original 1-in-4 branch, and teleport to y=-12 |
| 4 | `0x13AE9` | choose frame `61 + rand()%6` |

The initialization path consumes the same Borland generator already shared by
the native games: missions 0 and 1 draw one variant modulo four; mission 2
draws four unique values modulo ten with retry-on-duplicate, then one variant
modulo four. The executable's general range helper at image `0x0C016` uses a
signed remainder, confirming the exclusive-upper-bound modulo contract.

## Native gates

`SceneScript` exposes the recovered optional callback contract without
hard-coding Super into the shared VM. `SuperSceneCallbacks` implements all five
controller bodies and is checked independently for PRNG calls/state, variant
and shuffle initialization, frame changes, expert toggles, and teleport
coordinates.

The static SCPT test then runs the executable-backed selected thread lists and
locks raw thread counts, terminal ticks, signal 99, ordinary opcode-`0D` event
order, opcode-`FE` event order/cardinality, and complete ordered actor-state
timeline hashes. Across the five scripts it observes 38 `FE` callback visits
and 23 opcode-`0D` events. This closes the script VM and host-callback audit;
full mission presentation, controller input, and audio ownership remain runtime
work in the parity ledger.
