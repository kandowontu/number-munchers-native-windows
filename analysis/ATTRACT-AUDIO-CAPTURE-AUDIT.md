# Number Munchers attract-audio capture audit

This audit is entirely offline. It reads the preserved lossless AVI audio,
embedded GSND data, and a headless native waveform; it does not launch DOSBox,
the original executable, the native GUI, or an audio device.

## Preserved PCM evidence

`analysis/original-demo-full-internal.avi` contains a ZMBV 640x400 video stream
and a 48 kHz stereo signed-16-bit PCM stream. The latter is not empty: across
313.956 seconds its peak is -5.940 dBFS and RMS is -23.613 dBFS.

FFmpeg `silencedetect=noise=-50dB:d=0.5` finds the first attract-score onset at
33.958312 seconds. The score's quiet tail starts at 135.209375, and the next
cycle becomes audible at 139.391000. The onset-to-restart span is therefore
105.432688 seconds. Static GSND decoding independently produces 7,680 driver
ticks and a 105.430997-second YMFM waveform, a 1.691 ms difference at this
capture/audio-clock boundary.

The test-only `NumberMunchersAudioProbe` target reproducibly renders embedded
GSND 12 stream 16 without opening an output device:

```powershell
cmake --build build --config Release --target NumberMunchersAudioProbe
.\build\NumberMunchersAudioProbe.exe 16 .\tmp\number-score-reference-probe.wav
```

After downmixing/resampling both signals to 48 kHz, 10 ms RMS envelopes select
an alignment of 0.460 seconds from a reference slice beginning at 33.5 seconds.
Across the complete decoded cycle their Pearson correlation is
`0.9025445222`, despite live channel-0 gameplay cues remaining in the original
track. The median of 21 non-overlapping five-second windows is `0.9066460933`;
the best unaffected window reaches `0.9975567806`. This supplies direct PCM
evidence for the recovered score, tempo, rests, and loop duration rather than
only bytecode plausibility.

Absolute amplitude is intentionally not equated: the original track includes
DOSBox mixer gain, stereo duplication, live effects, and the capture pipeline,
while the native probe is raw mono YM3812 output.

## Single-chip native composition

The original conductor occupies OPL channels 1-8 and leaves channel 0 for
replaceable effects. The previous native build pre-rendered the score and cue
through separate chip instances, then asked the Windows mixer to add their PCM.
That preserved concurrency but reset each cue's global LFO/noise phase and
reset the score chip at every loop.

`OplStreamPlayer` now owns one continuously clocked YM3812. It schedules the
looping GSND score and decoded GSND/DRO cue writes on their original channel
registers, carries chip state across loop boundaries, and replaces/release
channel 0 without interrupting channels 1-8. Four 256-sample WinMM buffers keep
the production stream bounded; the test build validates ownership, replacement,
music-toggle restart, effect-only release, and full shutdown without opening
WinMM. It also clocks one exact second of score-plus-correct-cue samples through
that runtime scheduler and locks their signed-16-bit FNV-64 at
`0xead26e1488df4680`. `decodeDroSound` exposes the five captured DRO register
timelines to the same scheduler instead of forcing them through a second PCM
owner.

The same owner now remains continuously clocked for ordinary gameplay and
scene cues even when no score loop is installed. That broader driver lifetime,
including its two-cue sample fingerprint, is gated separately in
`OPL-LIFETIME-STATIC-AUDIT.md`.

Physical output-device gain/filtering and the real PIT-speaker waveform remain
open. They are not inferred from the high envelope correlation above.
