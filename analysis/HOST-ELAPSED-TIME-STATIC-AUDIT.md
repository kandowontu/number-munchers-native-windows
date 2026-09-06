# Host elapsed-time preservation audit

This audit covers the boundary between the Win32 message clock and the two
native game controllers. It was completed through source inspection and
headless tests only; no GUI, original executable, DOSBox process, or audio
device was started.

## Recovered clock ownership

Both supplied games install recurring records on the resident timer driver.
The PIT interrupt path advances the public scheduler clock independently of
when a controller next examines a due record. The recovered gameplay hosts
therefore consume public ticks in order; elapsed ticks are not silently
deleted because the foreground controller was temporarily delayed.

The native Win32 loop measures each `WM_TIMER` interval with
`std::chrono::steady_clock`. It previously clamped that measurement to 100 ms.
Word's controller applied a second identical clamp. When the window thread was
delayed for 350 ms, Number received only 100 ms and Word could discard the
interval twice at different API boundaries. Idle-to-Demo timing, scheduler
jobs, Wipes, scene clocks, and the 15 ms exit boundary could consequently run
slow relative to real elapsed time after a host stall.

## Native correction

The Win32 handoff and both controllers now reject only negative elapsed input;
they no longer impose an upper terminal. `MunchersApp`, the persistent owner,
preserves the complete steady-clock interval and submits it in controller
slices of at most 100 ms. This keeps the previously exercised maximum update
size while visiting synchronous page/controller transitions in order. No
remainder is dropped. If a game reaches its terminal and returns to the
launcher within a slice, the owner stops there because the collection launcher
has no inherited DOS game clock.

The headless launcher gate advances two Word instances to 29.8 seconds of
title idle. One receives a single coalesced 350 ms app update; the other
receives 100, 100, 100, and 50 ms. Both cross the exact 30-second boundary and
enter Word Demo. The former code left the coalesced instance on the title at
29.9 seconds.

## Evidence boundary

This proves that the production clock bridge no longer loses elapsed time and
that a representative synchronous boundary is split/coalesced invariant. It
does not establish live Windows scheduling latency, exact behavior after every
possible multi-second host suspension, or promote either game to 1:1. Those
claims remain governed by `PARITY.md` and `WORD-PARITY.md`.
