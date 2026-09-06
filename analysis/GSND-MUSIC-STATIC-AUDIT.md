# GSND long-score static audit

This audit is static and headless. It uses `NM.RES` GSND entry 12 and the
supplied executable's unpacked load image; it does not launch DOSBox or either
game executable.

## Conductor and tracks

GSND table entry 16 points to a priority-4 logical channel-9 stream. Channel 9
is the driver's non-OPL conductor channel. Its eight `0x82` commands start table
entries 196, 192, 188, 186, 185, 183, 181, and 180, which occupy OPL channels
8 through 1. OPL channel 0 therefore remains available for gameplay effects.
Entries 179 and the intervening table entries are shared absolute-jump or
subroutine targets, not additional top-level songs.

After starting the parts, the conductor executes a 30-pass loop of 255-duration
waits and jumps back to the launch sequence. Each logical channel advances from
an 8-bit phase accumulator. The conductor's default `0xFF` rate produces 255
duration updates per 256 scheduler ticks, so its 7,650 duration units occupy
exactly 7,680 scheduler ticks. With PIT divisor `0x0555` and one scheduler update
per 12 interrupts, one cycle is 105,431 milliseconds after integer truncation.

## Driver semantics added to the native decoder

- `0xC8 01` makes a channel follow the driver-global fractional rate set by
  conductor opcode `0xA6`. The score sets that rate to `0xB4`; this stretches
  each 5,280-duration part to about 103 seconds so it aligns with the conductor,
  rather than incorrectly finishing in about 72 seconds.
- A note, `0x89`, or `0xA0` with duration zero does not fail or wait 256 ticks.
  The original handler leaves the duration at zero and immediately dispatches
  the following command in the same sequencer tick.
- Entry 196 ends with a lone `0x88`. DOS `LODSW` reads an unused byte beyond the
  resource, but the end handler ignores it. The bounds-checked native decoder
  accepts that one-byte terminal command without reading out of bounds.
- The conductor's `0xAE` and `0xAF` commands set the two OPL depth bits before
  the eight child streams begin.
- Every accepted `0x82` launch immediately writes the four `0xFF` envelope
  values, `Bchannel=00`, then `Bchannel=20` at driver image `0x16D5`. Those
  writes retain conductor launch order. The ordinary scheduler subsequently
  visits the installed logical records from channel 8 down to channel 0.
- At Word's 8,192-tick section boundary, channel 9 installs the second eight
  records before any replaced first-section record can dispatch on that tick.
  The same rule applies at the looping cycle boundary in both games.

The headless regression suite now requires all eight child tracks and the
conductor to decode, checks the exact 105,431 ms cycle, requires a
non-trivial register stream, and proves note writes occur on OPL channels 1–8.
It also folds the duration, write count, and every timestamped register tuple
into deterministic static/native schedules:

| Game | Cycle (ms) | OPL writes | Schedule FNV-64 |
|---|---:|---:|---|
| Number | 105,431 | 5,822 | `bfa832bbb6f35d8e` |
| Word | 224,919 | 13,400 | `152a3244c88a359b` |

## Original call sites and native lifecycle

The resident dispatcher at image `0x10d2b` assigns bit `0x80` to the music
class, checks the saved music option, masks that bit, and forwards the remaining
seven-bit index to the loaded sound driver. Consequently event `0x90` is GSND
entry 16, not an unrelated event.

`DS:596c` is the demo flag. The original starts event `0x90` at demo entry
(`0x129be`) and, if it is not already marked active, at each demo board setup
(`0x089ea`). Normal board setup instead stops the active driver stream. The
Alt+M paths at `0x0b240`, `0x0b296`, `0x0d20b`, and `0x0d261` restart `0x90`
only when the demo flag is active, the active controller reports state 2, and
the current device getter at `0x0886` returns nonzero. The resident path proves
the selector condition with the compare at `0xD201`; states 3 and 1 (Hall and
logo/splash) and PC-speaker device zero deliberately do not restart the stopped
score. There is no normal-gameplay call to entry 16.

The native port now follows that lifecycle: attract mode starts GSND 16 as an
exact-length loop, later demo boards leave it running, Alt+M always stops it
but resumes it only on the non-speaker state-2 board/feedback/terminal-Wipe
route, and leaving the demo submits GSND 0. That retirement keeps the YM3812
resident; it is not a device close or reset. Alt+M also emits GSND 1/2
acknowledgements; its music-off entry 1 begins after the original 50 ms wait.
`OplStreamPlayer` clocks one YM3812 continuously:
channels 1-8 own the score while decoded GSND/DRO gameplay cues replace channel
0 in that same chip. This preserves global depth, LFO/noise phase, and score
state across both effects and the loop boundary instead of combining two reset
chip waveforms in the Windows mixer. The console build checks score start,
single-chip cue ownership, cue-only release, selector-state-specific
music/device-toggle restart, acknowledgement content/timing, and exact
Demo-exit channel retirement without opening an audio device.

Ordinary gameplay and scene cues now use that continuous-chip lifetime even
when no score is installed. Audible GSND streams 1-15 are independently gated
to channel 0. Stream 0 now executes all four writes from each of its nine `B3`
retirements and the context-sensitive `C3` cleanup while keeping that chip
alive. A two-cue sample hash proves the second event inherits chip history
rather than starting a new PCM render. See `OPL-LIFETIME-STATIC-AUDIT.md`.

The complete Alt+S/M/P wrapper and current-device evidence is recorded in
`SOUND-TOGGLE-STATIC-AUDIT.md`.

## Remaining music work

The PCM stream in `original-demo-full-internal.avi` now supplies an offline
audible reference as detailed in `ATTRACT-AUDIO-CAPTURE-AUDIT.md`. Its first
onset-to-loop span differs from the decoded 105.430997-second cycle by only
1.691 ms. Aligned 10 ms RMS envelopes correlate at `0.9025445222` over the
whole live-effect-bearing cycle and reach `0.9975567806` in an unaffected
five-second window. Physical output gain/filtering and the PC-speaker waveform
remain open; the former two-core Windows-mixer substitution does not.
