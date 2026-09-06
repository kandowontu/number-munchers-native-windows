# SCPT scene VM static audit

The Number resource/control-flow recovery was performed from the five supplied
SCPT resources and the unpacked executable. The shared painter interpretation
was first resolved against the muted lossless Word scene captures documented in
`WORD-SCENE-STATIC-AUDIT.md`, then checked against five muted lossless Number
scene captures in `analysis/number-live/scenes/original`.

## Container and thread order

An SCPT resource begins with a 16-bit thread-table count followed by that many
32-bit resource-relative offsets. Commands are length-prefixed records:
`byte length`, `byte opcode`, then `length - 2` payload bytes. A thread ends at
opcode `FF`.

The executable copies these exact valid-thread lists before constructing the
scene objects; zero offsets in the raw tables are intentionally unused:

| SCPT | VGA BTMP/PCXF | valid thread order |
|---|---:|---|
| 2012 | 2013 | 8, 5, 7, 6, 9, 10, 11, 0, 1, 2, 3, 4 |
| 2014 | 2015 | 11, 4, 5, 6, 2, 1, 3, 7, 8, 9 |
| 2016 | 2017 | 7, 9, 10, 12, 4, 0, 5, 1, 6 |
| 2018 | 2019 | 10, 9, 4, 5, 6, 0, 1, 3, 2 |
| 2020 | 2021 | 8, 7, 1, 2, 4, 3, 0 |

## Recovered commands

The dispatcher at image `0x6907` and its jump tables recover the commands used
by Number Munchers:

| opcode | length | behavior |
|---:|---:|---|
| 01 | 4 | wait for the supplied scene-tick count |
| 02 | 8 | relative movement over N ticks |
| 03 | 8 | absolute movement over N ticks |
| 04 | 4 | select bitmap frame |
| 05 | 2 | next bitmap frame |
| 06 | 2 | previous bitmap frame |
| 07 | 8 | counted backward branch |
| 08 | 2 | erase/hide actor |
| 09 | 2 | show actor |
| 0A | 4 | publish a synchronization signal |
| 0B | 4 | wait for a synchronization signal |
| 0D | 4 | invoke the host audio callback with an event value |
| 0E | 2 | detach from active dirty-rectangle updates |
| FE | 4 | invoke the optional game-specific actor callback; advance when it returns true |
| FF | 2 | end thread |

The controller thread in each cartoon eventually publishes signal 99. Other
threads wait for that value before ending, providing an unambiguous structural
completion condition.

## Scheduler, movement, painter order, and detach

The dispatcher is image `0x06907`–`0x06E6B`. Wait/movement commands store a
one-based current tick at object offset `+0x7C` and their target tick at
`+0x7E`. A wait of N therefore resumes the following command exactly N public
scene callbacks later.

Movement does not recompute `delta * step / duration` as an integer. At
`0x06A58`–`0x06BFA` it stores each starting coordinate as signed 16.16, divides
`delta << 16` by the duration once with signed truncation, adds that increment
on every callback (including the command's first callback), adds `0x8000`, and
uses the high word as the visible coordinate. This matters for small moves: a
relative `(2,-2)` over three ticks visits `(1,-1)`, `(1,-1)`, `(2,-2)`.

Object construction at image `0x0F69E` walks each recovered thread array from
its first entry to its `-1` sentinel. The allocator/list routine at
`0x0650F` appends each object. Each callback first snapshots all actors, then
advances every script, then visits changed actors in construction order. For
each dirty rectangle the painter clears the affected retained pixels and
recomposes intersecting active actors back-to-front, in reverse construction
order. The PCXF record is opaque in this path: black source pixels paint black.
Number scene 4 exposes one additional record-size rule: a same-position frame
change cleans with the newly installed BTMP dimensions. When frame 15 shrinks
to frame 16 at tick 129, the old lower 16 rows remain until the taller frame 21
covers them at tick 141. Native preserves that exact retained tail.

Opcode `0E` is not a no-op and does not create a permanently composited sprite.
Its `0x06DA3` branch detaches the actor from later dirty updates without first
erasing its current pixels. Those pixels remain in the retained framebuffer
until another dirty rectangle covers them, at which point they are cleared and
only active actors are repainted. Scripts can then reuse the same actor for a
new placement. Opcode `FF` sets the removal flag at `+0x96`; the cleanup pass at
`0x06421`–`0x0648E` unlinks it in the same callback, so an ended visible actor
must no longer be painted.

With those VM rules, the supplied scenes reach their signal-99 terminal states
at these public scene ticks (counting the first immediate tick). The final
column is the diagnostic detach-snapshot count retained by the VM tests; those
snapshots are not independently redrawn by the presenter:

| SCPT | terminal tick | detach snapshots |
|---:|---:|---:|
| 2012 | 183 | 6 |
| 2014 | 199 | 7 |
| 2016 | 223 | 7 |
| 2018 | 194 | 68 |
| 2020 | 186 | 43 |

## Native implementation and checks

The scene launcher initializes opcode `0D`'s host callback to the resident sound
driver and loads ADLI bank 11, 12, 13, 14, or 15 alongside the five cartoons.
Across those scenes the scripts emit 6, 12, 6, 9, and 10 unique cue indices.
The loaded-bank flag at `DS:138A` makes the resident wrapper at image `0x10DEF`
add three before dispatch, so a script event `N` selects ADLI/PSND stream
`N + 3`, not stream `N`. The emitted event range 1–34 therefore selects common
streams 4–37. Their actual OPL instruments are `00`, `06`, `09`, `0B`, `0E`,
`62`, `6A`, `96`, `99`, and `B2`; the executable's pointer table at image
`0x1956` supplies the exact 11-byte patches. The common resource range also
uses instrument `4B`, now recovered even though these five scripts do not call
its stream.

After the opening Wipe and 10 ms hold, the loader separately submits high-bit
event `0xA6`. The resident music branch converts it to bank-local stream 38, a
channel-9 conductor that launches the cartoon score on channels 1–8. Native
decodes all five conductors, keeps their non-repeating score beside replaceable
channel 0, and retires both through non-resetting resident GSND 0 at scene
teardown; see
`CARTOON-SCORE-STATIC-AUDIT.md`.

`src/scene_script.cpp` implements the bounded VM without executing source code
or reading outside the embedded resource. It uses the recovered signed 16.16
accumulator and emits construction-order paint events carrying previous/current
actor bounds plus detach state. `Game` applies those events to its retained
scene surface and removes opcode-`FF` actors rather than rebuilding a stateless
list of detached placements every frame. BTMP records can name continuation
PCXF sheets: for example frame 22 in table 2013 correctly resolves to sheet
3013.

The console regression suite loads every scene, uses the exact thread lists,
requires every selected and detach-snapshot frame to stay in range, locks the
five exact terminal tick/detach-snapshot counts, adds the recovered three-stream bias,
decodes every unique callback through both its matching ADLI bank and the
common PSND 11 bank, locks all five stream-38 conductor schedules, and requires
every VM to reach signal 99. A synthetic
fixture independently locks positive/negative 16.16 rounding, detach/reuse,
later dirty erasure, and same-tick actor removal. All 38 common PSND streams and the newly reached
patch mappings are also regression-tested. The integrated level-3 test proves
the rendered scene changes over time. The presenter gate now goes further and
hashes every frame the DOS host can actually expose—tick 1 through the callback
before signal 99—for all five scenes. Each timeline folds the tick number and
complete 320×200 frame hash into a second FNV-64 accumulator:

| SCPT | Visible ticks | Static/native framebuffer timeline FNV-64 |
|---:|---:|---|
| 2012 | 1–182 | `c0d1952ade7496e9` |
| 2014 | 1–198 | `cf0893ea88f18845` |
| 2016 | 1–222 | `b706c9f88cdb6d97` |
| 2018 | 1–193 | `abfe60bbd905cdf7` |
| 2020 | 1–185 | `ff411c4488c2cdea` |

This exercises every detach/reuse event, live actor, continuation-sheet crop,
overlap, opaque black record, later dirty clear, and same-tick removal through the actual `Game`
renderer. They remain executable-backed full-timeline gates, while the live
captures independently compare the DOS-presented stable states. Separate
state-6 input gates in both
runtimes require Escape/modifiers to leave the scene active, ordinary forwarded
keys to tear it down immediately, and a printable Win32 key to be consumed
without leaking into movement on the newly generated board.

The live audit observes all 568 distinct DOS-presented RGB states and all 721
ordered state runs across the five cartoons at exact pixels. Scenes 0-4 match
their complete timelines at 182/182, 120/120, 136/136, 165/165, and 118/118.
The longer muted scene-3 recording closes the former 16-run terminal-loop tail
without changing any native hash or script timing. All five preceding close/
cyan/open/white transitions and their 43/43/44/43/44 covered-sample counts are
also measured. Synchronized physical audio remains in the separate Number
sound parity row; the scripted-scene visual/behavioral row is now verified.
