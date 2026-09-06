# Native audio streaming audit

Date: 2026-08-28

## Reported symptom

The released AdLib path could sound choppy on a busy Windows desktop even
though its generated YM3812 samples and event schedules were exact. The saved
Number Munchers configuration selects AdLib, so the relevant runtime owner is
`OplStreamPlayer`, not the independently rendered PC-speaker wave path.

## Cause

The YM3812 runs at approximately 49.7 kHz. Production previously queued four
256-sample WinMM headers:

- each callback arrived about every 5.1 ms;
- the complete device queue covered only about 20.6 ms; and
- synthesis/refill ran on an ordinary-priority worker.

A short scheduling stall could therefore drain the queue. The generated PCM
remained correct, but the output device inserted an audible gap between
headers—the characteristic choppy result.

## Correction

Production now uses twelve 512-sample headers. This provides about 123.6 ms of
continuous queued audio while retaining the same approximately 10.3 ms
refill/cue granularity. Earlier corrections used four (41.2 ms) and eight
(82.4 ms) such headers, but longer scheduling stalls on the target desktop
could still exhaust those queues. The refill thread registers dynamically with
Windows MMCSS in the `Audio` task class at high priority; if MMCSS is
unavailable, it falls back to `THREAD_PRIORITY_HIGHEST`. Only that sleeping
audio worker is elevated, and the more aggressive `Pro Audio` class is not
used. The WinMM callback remains minimal and never synthesizes or calls
`waveOutWrite`, avoiding callback-thread deadlocks.

No YM3812 clock, register write, effect-replacement rule, score/effect channel
ownership, sample order, or generated sample value changed.

## Gates

`game_render_state_test` now requires 120–128 ms of configured queue coverage in
addition to its existing exact score/effect sample hash, register-shadow,
timing, replacement, and lifecycle checks. The complete six-test headless
suite passes after the change. The production artifact is separately copied to
an isolated temporary directory and verified as resource-complete and
system-DLL-only without opening a window or audio device.
