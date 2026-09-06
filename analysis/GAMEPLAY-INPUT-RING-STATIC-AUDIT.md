# Gameplay input-ring static and live audit

This audit closes the ownership, capacity, and terminal-timing boundaries for
ordinary board input in Number Munchers and Word Munchers. Static evidence comes
from the preserved unpacked load images/listings. A narrow muted Number DOSBox-X
session validates two FIFO cases and the `Time out` reset. It does not establish
unmeasured physical-pointer or complete-game parity.

## Producers and representation

Number `pSeqKey` begins at load-image offset `0x13688`. In selector states 4/5
it normalizes physical arrows and the documented digit/case aliases to the
original `I`, `J`, `K`, and `M` direction bytes; Space remains Space. The common
enqueue call is at `0x137A1`.

Number `pSeqmouse` begins at `0x137D6`. In the same selector states it maps the
half-open board rectangle to cell 0..29, negates the low byte, and enqueues at
`0x13813`..`0x1385D`. Cell zero is byte zero; the ring count distinguishes it
from an empty buffer.

Both producers call `1652:022E`, load-image offset `0x1674E`. It owns a
ten-byte circular store with nine usable entries. Keyboard and mouse therefore
cannot overtake one another and do not have separate capacity. Word's routines
are instruction-equivalent at the corresponding recovered addresses.

## Consumer and terminal timing

The Number player dispatcher at `0x0944D` selects movement state 3, normal
input state 4, chew state 5, and post-collision state 6. Normal input at
`0x0947F` peeks through `1652:03D5` (`0x168F5`); the pop routine is
`0x1687D`.

- Positive `I/J/K/M` bytes start one ordinary directional movement and Space
  starts the ordinary chew path.
- Non-positive bytes are decoded as negated board targets. The path walks one
  animated cell at a time, consumes the target immediately before its final
  adjacent step, and treats a later/current-cell event as chew.
- Movement terminal `0x094E6` peeks the ring again and may begin the next
  queued action in that same callback.
- Chew terminal `0x093A9` restores player state 4 through `0x09AA1` but does
  not consume the ring. The next public player callback owns the successor.
- State 6 performs no input work. Bytes accepted after feedback therefore wait
  until the survivor leaves and a later player callback sees state 4.

This is one FIFO, not a generic “busy input is rejected” gate.

## Reset boundaries

The ring-reset routine is `1652:02DB` (`0x167FB`). Collision setup at
`0x096A5` invokes it, so pre-collision input cannot survive. New gameplay input
entered after the blocking collision-feedback waiter returns may still enqueue
while the player actor remains in state 6.

The common Number Enter branch at `0x0D2A2` also calls the ring reset before
entering or leaving `Time out`; Word is byte-isomorphic. This explains why an
otherwise valid direction entered on the paused page does not execute after
resume.

## Muted live observations

The reference ran with `nosound=true` and one controlled DOSBox-X window.

- [`input-ring-before.png`](captures/input-ring-before.png) is the starting
  board. [`input-ring-movement-jj.png`](captures/input-ring-movement-jj.png)
  follows one pasted `JJ` burst and shows the Muncher two cells left: the second
  byte survived the first movement.
- [`input-ring-chew-space-j.png`](captures/input-ring-chew-space-j.png) follows
  `Space+J` on a correct cell. The cell is eaten and the queued left movement
  has executed after the seven-tick chew.
- [`input-ring-paused.png`](captures/input-ring-paused.png) and
  [`input-ring-pause-resume-j.png`](captures/input-ring-pause-resume-j.png)
  bracket a `J` sent during `Time out`; the Muncher does not move after resume,
  consistent with the static Enter reset.

A collision-feedback experiment that sent Space and J in one paste burst is
not evidence about recovery: the blocking accepted-key waiter owned the later
byte before gameplay resumed. It is deliberately excluded from the parity
claim.

## Native lock

Both native controllers now route arrows, every printable alias, Space, and
pointer cells through one capacity-nine FIFO. Headless tests lock:

- key/key and key/pointer FIFO order;
- the tenth byte being dropped;
- movement-terminal same-callback chaining;
- chew-terminal next-public-callback chaining;
- state-6 retention and post-survivor timing;
- collision/Enter reset semantics; and
- cell-zero pointer encoding.

The member retains its historical `pointerCellQueue_` name for a minimal source
change, but its documented and tested ownership is the complete mixed gameplay
input ring.
