# Gameplay pointer static audit

This audit uses the preserved load image in `NM-unpacked-image.bin` and the
corresponding 16-bit listing in `NM-unpacked.ndisasm`. A later muted DOSBox-X
session supplies the narrow live FIFO checks described below; it does not turn
the remaining physical-pointer work into a broad parity claim.

## Click-to-cell mapping

The mouse dispatcher calls `pSeqmouse` at far address `1357:0266`, load-image
offset `0x137D6`. In its gameplay states, the callback accepts coordinates in
the half-open rectangle

```text
x: 20 .. 307
y: 26 .. 175
```

and computes

```text
cell = ((y - 26) / 30) * 6 + ((x - 20) / 48)
```

This independently confirms the measured 6-by-5 board and its exact live
pointer bounds. The callback negates the low byte of `cell` and calls the
common input enqueue routine at `1652:022E` (image `0x1674E`). That routine is
a ten-byte circular buffer with nine usable entries. Cell zero is encoded as
byte zero; the buffer's head/tail/count state distinguishes it from no input.

There is no coordinate assignment to the Muncher in this callback and no
teleport path.

## Target walking and the second click

The normal-play input branch begins at image `0x0947F`. It peeks at the common
ring through `1652:03D5` (`0x168F5`). A non-positive cell event that does not
name the current Muncher cell is passed to `0x07FD4`, then to the ordinary
movement routine at `0x08BBA`.

`0x07FD4` compares the target row/column with the current row/column. It starts
with the vertical direction and switches to horizontal only when the absolute
horizontal distance is *strictly* larger. Consequently, equal row/column
distances take the vertical step first. It adjusts the queued target by one
cell toward the current position and consumes the event when that adjusted
target equals the current cell—immediately before beginning the final adjacent
step.

Therefore a first click:

1. queues a board target;
2. walks toward it one ordinary animated cell at a time;
3. consumes the target before the final step; and
4. arrives without eating.

A click naming the current cell is consumed at `0x094AE`; the `<= 0` branch at
`0x094BA` calls the ordinary munch routine at `0x08A31`. A second click is
therefore required to eat. Because clicks share the nine-entry ring with
keyboard bytes, a rapid second click made during the walk waits behind the
target and becomes the eat action as soon as arrival finishes. Keys and targets
retain arrival order rather than using independent native queues.

## Native parity boundary

The native port now uses those exact board bounds, shared nine-entry capacity,
dominant-axis/vertical-tie path, ordinary 200 ms movement steps, arrival-only
first click, and current-cell chew routing. The FIFO holds normalized
`I/J/K/M/Space` bytes and negated cell indices together. Movement can consume
its successor at its terminal callback; a correct chew leaves its successor
for the next public player callback. Collision setup and Enter's `Time out`
branch reset the complete ring. Headless tests in both games cover mixed FIFO
order, capacity, recovery retention, pause reset, a multi-axis route, vertical
ties, a queued rapid second click, scoring through the ordinary seven-tick
munch sequence, and the cell-zero encoding edge case.

The live Number captures preserved as `input-ring-before.png`,
`input-ring-movement-jj.png`, and `input-ring-chew-space-j.png` show two `J`
events crossing one movement and `J` crossing one correct chew. The paired
`input-ring-paused.png` and `input-ring-pause-resume-j.png` frames show that an
event entered during `Time out` does not survive resume. Static disassembly
identifies the reason: the common Enter branch calls the ring reset on both
sides. See `GAMEPLAY-INPUT-RING-STATIC-AUDIT.md` for addresses and scope.

The shared event trace in `MENU-POINTER-STATIC-AUDIT.md` now proves that left
release is event type 2 and right press/release are event types 12/13. Fixed
list widgets convert type 13 to carriage return; the separate accepted-key
waiter converts type 2 to Space and rejects type 13. The native Win32 adapter
follows the owning loop rather than applying either conversion globally. See
`ACCEPTED-KEY-POINTER-STATIC-AUDIT.md`. Joystick calibration is audited separately
in `JOYSTICK-STATIC-AUDIT.md`. Together these close the application-controlled
Win32/WinMM event boundary; the mechanics of a particular mouse or stick remain
optional machine QA. See `PLATFORM-BOUNDARY-AUDIT.md`.
