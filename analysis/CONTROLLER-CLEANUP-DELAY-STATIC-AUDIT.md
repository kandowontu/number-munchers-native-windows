# Controller cleanup-delay static audit

This audit was performed from the preserved unpacked executable images and
headless native controllers. It did not start DOSBox, either supplied DOS
executable, the Win32 GUI, or an audio device.

## Resident-delay callers

Number Munchers' resident delay wrapper is `0x10D2:0223`; Word Munchers' is
the instruction-equivalent `0x1002:022B`. Each executable has exactly three
direct callers in its game image:

| Boundary | Number image | Word image | Literal |
|---|---:|---:|---:|
| selected-cartoon stop to resource/setup install | `0x0F34B` | `0x114EB` | 15 ms |
| cartoon teardown stop 1 to stop 2 | `0x0F499` | `0x11649` | 15 ms |
| main-controller cleanup | `0x12448` | `0x11BCE` | 15 ms |

The first two boundaries are implemented and gated by the cartoon presenter,
score, and OPL-lifetime audits. The third is the subject of this audit.

Number function `0x1243E` pushes `0x000F`, calls the delay wrapper at
`0x12448`, clears `DS:1530`, runs the shared cleanup, and releases six
controller items. Word function `0x11BC4` has the same instruction sequence:
the delay call is at `0x11BCE`, the state word is `DS:22EC`, and six items are
released. Neither function is an inferred visual pause; the millisecond
literal is present in both supplied executables.

## Both high-level callers

Each cleanup function has two high-level callers:

| Controller action | Number | Word | Successor |
|---|---:|---:|---|
| action 3, Demo board terminal to Hall | `0x12928` | `0x11FF9` | shared PCX Wipe, Demo Hall, state-3 job |
| action 6, program exit | `0x12A53` | `0x120F4` | DOS/runtime termination call |

Action 3's 15 ms is already inside the lossless reference's measured
39-restored-board-frame lead-in and its total 46-interval terminal-to-Hall
envelope. Adding another 15 ms to the native transition would double-count an
original operation and break the stronger visible measurement. Native keeps
the measured aggregate unchanged; this static trace explains one operation
contained by that aggregate.

Action 6 was previously immediate in both native title menus. Native now
keeps the title controller alive for exactly 15 ms, blocks keyboard,
translated-character, pointer, and cheat-overlay input during that interval,
and raises the game-level quit flag only at the terminal. `MunchersApp` then
destroys that game controller and returns to its matching launcher selection.
The headless gate checks the initial 15 ms state, a 14 ms nonterminal point,
the exact terminal, blocked input, and both direct-title and confirmed
in-game launcher routes. The persistent app owner additionally retains
consumed left/right press state across that controller destruction, so a
release arriving on the launcher cannot turn the cleanup acknowledgment into
a new launch. Both button paths suppress exactly one paired release in the
headless gate.

The launcher's own `Exit` item remains immediate. It is a native collection
owner outside either supplied DOS controller, so applying action 6's game
cleanup delay there would invent original behavior rather than preserve it.

## Separate host-delay wrapper

The executables also call a general host delay service at Number `0x2617:0002`
and Word `0x21AB:0002`. Relevant live-game literals are independently covered:
the cartoon score start uses 10 ms, while joystick/toggle feedback uses 50 ms.
Installer, network retry, and DOS memory/driver setup calls are not runtime
paths in the self-contained native port. They are not aliases of the three
resident-wrapper callers above.

## Evidence boundary

This closes a concrete source-level timing omission and gives it a deterministic
native gate. It does not promote either whole game to 1:1: the parity ledgers'
remaining live-frame, continuation, physical-controller, scene, CGA, and
physical-audio comparisons are unchanged.
