# Munchers YM3812 lifetime audit

This audit is static and headless. It uses the preserved Number and Word GSND/
ADLI resources, Number DRO traces, both unpacked executables, and the in-memory native sample renderer. It
does not open DOSBox, either game executable, a native window, or an audio
device.

## Original ownership boundary

The executable loads one AdLib driver and sends ordinary events through its
resident dispatcher. Decoding the complete GSND table proves that audible
ordinary streams 1-15 write only OPL channel 0. Stream 0 is the driver-wide
all-channel stop record; streams 1/2 are the audible descending/ascending
toggle acknowledgements.
The attract conductor is the complementary case: it launches score parts on
channels 1-8 and leaves channel 0 for those same replaceable cues.
The per-scene stream-38 conductors use that same split. They launch finite,
non-repeating cartoon parts on channels 1-8 after the opening Wipe while SCPT
callbacks continue to replace channel 0.

Word uses the same device contract. Its directly called gameplay streams
3-5 and 7-15, plus scene-bank callbacks after the original `+3` bias, are
channel-0 effects. Its two-section conductor independently occupies channels
1-8. Headless decoding rejects any direct Word gameplay cue that writes a
nonzero OPL channel.

There is therefore one hardware YM3812 lifetime, not a new synthesized chip for
each correct munch, wrong answer, board cue, collision, or scene callback.
Replacing channel 0 does not reset the chip's global depth, LFO/noise phase, or
other register history. Alt+S replaces that channel with GSND 1/2 and leaves
the score running. Alt+P changes the driver's current device byte; selecting
speaker while music is enabled submits stream 0 and retires the score without
resetting the YM3812, while restoring AdLib restarts it only in Demo selector
state 2.

## Removed native substitution

The former native ordinary-gameplay paths rendered every DRO/GSND/ADLI cue from
a newly reset YM3812 and submitted the resulting RIFF buffer to `WavePlayer`.
Its individual cue notes were correct, but the reset/mixer lifecycle was not the
original driver's lifecycle.

`OplStreamPlayer::startIdle` now opens one silent, continuously clocked chip on
the first ordinary AdLib event in either game. Direct Number DRO captures,
decoded Number/Word GSND cues, and both games' scene-bank ADLI callbacks all
replace channel 0 on that owner. The owner stays
alive across silent intervals and later cues. Demo installs the decoded
channels-1..8 score on the same class of owner. Cartoon event `0xA6` installs
the active bank's non-repeating stream-38 schedule without resetting either the
chip or a queued channel-0 cue. Scene teardown retires both logical owners with
GSND 0 while leaving the chip clock and register history resident. Sound toggles replace channel 0
with their audible acknowledgement. Music-off stops all streams, waits 50 ms,
then submits the descending acknowledgement. That stop is now the resident
GSND-0 `B3 00..08`/`C3` register sequence on the same live chip, not a native
device close/reset. Effect-device changes use that sequence only on the
AdLib-to-speaker edge with music enabled; the music-disabled branch invents no
stop. The PC-speaker path remains a separately rendered waveform because it is
a distinct original device.

## Internal stops versus native shutdown

The two executable images make this boundary explicit. Number's direct GSND-0
caller families are non-Demo board setup at `0x089C2`, the secondary/main
Alt+P branches at `0x0B26A`/`0x0D235`, selector-state 1-3 departure at
`0x0D27C`, the state-4/5 Escape route at `0x0D2DD`, outer cartoon teardown at
`0x0F326`, selected-cartoon loading at `0x0F340`, and the inner cartoon
teardown helper at `0x0F490`. They reach the no-argument wrapper at `0x10D20`,
which pushes zero and submits GSND entry 0. Word's instruction-equivalent
callers are `0x0958F`, `0x0BE4B`, `0x0DEDF`, `0x0DF26`, `0x0DF87`,
`0x0FFBE`, `0x114E0`, and `0x11640`, using its byte-identical wrapper at
`0x10028`.

The selected-cartoon loaders submit GSND 0 and then call a blocking delay
routine with the literal 15 ms before loading the new scene. That literal is a
delay argument, not a second stream-15 submission. Teardown first submits GSND
0 in the inner helper, waits the same 15 ms, and then reaches the outer
GSND-0 call. The state-4/5 Escape callers likewise stop the resident driver
before entering their quit-prompt callbacks. Native therefore uses the exact
resident `B3 00..08`/`C3` path for board start, Demo exit, pre-prompt silence,
completion-cue cutoff before a cartoon, both teardown calls, and internal
return-to-title instead of closing or resetting YM3812.

The shared collection launcher is a native ownership boundary that did not
exist inside either DOS executable. Returning there destroys the active game.
`OplStreamPlayer` destruction explicitly resets/closes `waveOut`, wakes and
joins its worker, and only then releases the synthesized chip. This prevents a
launcher return from retaining a silent output device or waiting on a worker
that never observes `std::jthread`'s stop token.

## Headless regression boundary

`game_render_state_test` now requires for Number:

- all audible GSND streams 1-15 to avoid nonzero OPL channels;
- an ordinary cue to start a chip with `playing=true` and `looping=false`;
- a second cue to reuse that owner rather than increment a score-loop start;
- two one-second cue intervals, correct then wrong, to hash as signed-16 PCM
  FNV-64 `0x5fa8ed9d1584b20a`; and
- exact GSND-1/2 toggle acknowledgements on both AdLib and speaker routes,
  including Alt+M-off's 50 ms delay;
- sound-off to leave the score running while replacing channel 0; and
- Alt+P's music-dependent all-channel stop plus state-2-only score restoration;
- GSND 0's exact 36 per-channel writes, depth-preserving `BD=C0` cleanup, and
  live-chip score restoration without a reset;
- the state-4/5 Escape route to execute one exact GSND-0 stop before the quit
  prompt, with cancel leaving that cue retired;
- the selected-cartoon loader to retire completion stream 15 before its exact
  15 ms quiet load interval;
- all five Number cartoon conductors to coexist with channel 0 and stop at
  teardown through two exact GSND-0 calls separated by 15 ms/745 generated
  samples while the chip remains live; and
- releasing channel 0 to preserve a queued non-repeating score, while full
  owner teardown clears that queued score before another worker can install it.

`munchers_app_headless_test` applies the parallel Word gate:

- every directly used gameplay stream 3-5 and 7-15 remains on channel 0;
- gameplay and all six scripted-scene hosts submit AdLib events to the live OPL
  owner, while their PSND alternatives continue through the waveform owner;
- all six Word cartoon conductors use the same non-resetting score owner and
  retain the original Music/AdLib gate;
- internal Demo/cartoon/title transitions, pre-prompt Escape, the 15 ms scene
  load boundary, and the two-call/15 ms scene teardown retire all nine driver
  channels and preserve the live chip's masked `BD` depth state;
- correct then wrong one-second intervals produce the same shared-cue PCM hash
  `0x5fa8ed9d1584b20a`; and
- the independent Word Demo score-plus-correct-cue hash remains
  `0xfc21539d1eb4c238`.

The production implementation uses the same four 256-sample WinMM buffers as
the already-gated Demo composite path. The test build clocks samples only in
memory and never opens WinMM.

Physical output-device gain/filtering and real AdLib analog output remain open
in the parity ledger. This audit closes the reset-per-cue Windows-mixer
substitution; it does not claim physical audible identity.

The recovered toggle/device branches and their complete address-level evidence
are recorded in `SOUND-TOGGLE-STATIC-AUDIT.md`.
