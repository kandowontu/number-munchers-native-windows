# Resident sound-toggle and device-state static audit

This audit uses the preserved Number and Word load images, their byte-identical
resident sound-driver branches, the embedded GSND banks, and native headless
tests. It does not launch DOSBox, either original executable, the native GUI,
or an audio device.

## Driver device state

The common driver setter at load-image 0x0824 controls the current device byte
at CS:03AE and saved detected-device byte at CS:03AF:

- argument 3 stores zero in the current byte, selecting the internal speaker;
- argument 4 restores the saved detected device to the current byte;
- argument 2 leaves the current selection unchanged; and
- every other value stores both the current and saved device.

The getter at 0x0886 returns CS:03AE. The note handler at 0x142D checks that
same byte: zero invokes the PIT channel-2 start helper. The music-class
dispatcher also calls the getter and rejects a new music event while the
current device is zero. Consequently Alt+P does not merely reroute effects
while an AdLib score continues; selecting speaker suppresses new score starts.

## GSND control and acknowledgement records

Both relevant banks—Number GSND:12 and Word GSND:2—begin with the same
records:

| Entry | Header | Recovered role |
|---:|---|---|
| 0 | channel 9, priority 10 | commands B3 00 through B3 08, then C3 88: stop all logical channels and clean up rhythm state |
| 1 | channel 0, priority 4 | descending audible acknowledgement using patch 73 |
| 2 | channel 0, priority 4 | ascending audible acknowledgement using patch 72 |

Entry 0 is therefore the all-channel stop command. Entries 1 and 2 are real
audible channel-0 streams, not generic enable/disable control records.

The physical writes are also exact. Each `B3 n` reaches the handler at image
`0x0EE1`, which writes `C0+n=00`, carrier `43+operator[n]=3F`, release
`83+operator[n]=FF`, then `B0+n=00`. Nine channels therefore produce 36 writes
in argument order. `C3` reaches `0x1169`, clears the rhythm-active shadow, and
writes `BD=(previous BD & C0)`: it preserves the two global depth bits rather
than resetting the chip.

## Alt+S and Alt+M

Number's resident wrapper is at 0x10D2B; Word's byte-isomorphic wrapper is at
0x10033.

For event 0x03E8 (Alt+S), the on-to-off branch submits entry 1 and then clears
the sound flag. The off-to-on branch sets the flag and submits entry 2. The
low-level submission bypasses the changed sound flag, so both edges are
audible. Replacing logical channel 0 leaves an active channel-1..8 Demo score
running.

For event 0x03E9 (Alt+M), the on-to-off branch submits entry 0, waits a
literal 50 ms, submits entry 1, and then clears the music flag. The off-to-on
branch sets the flag and submits entry 2. The later high-level branch may also
submit score event 0x90, but only when all of these conditions hold:

- Demo is active;
- the selector is in state 2 (board, feedback hold, or board-to-Hall Wipe);
- music is enabled; and
- the current device returned by 0x0886 is nonzero.

Hall/state 3 and logo-or-splash/state 1 do not restart the score.

## Alt+P asymmetry

The common Alt+P branches are Number 0xD214–0xD268 and Word
0xDEBE–0xDF12; the corresponding secondary input routes occur around
Number 0xB249 and Word 0xBE2A.

- When the current device is nonzero, the handler calls setter argument 3. If
  music is enabled, it then submits entry 0, stopping the score and every
  currently sounding driver channel.
- When the current device is zero, the handler calls setter argument 4. If
  music is enabled and Demo's selector state is 2, it submits score event
  0x90. State 3/1 restoration does not start the score.
- Neither branch emits the entry-1/2 acknowledgement used by Alt+S/M.
- Alt+P then rejoins the common selector dispatcher, so the same event exits
  Demo or skips a cartoon.

When music is disabled, neither Alt+P direction submits entry 0. An already
scheduled cue is therefore not synchronously stopped merely because the
current device byte changed.

## Native implementation and headless gate

Both runtimes now submit GSND entry 1/2 on every accepted Alt+S and Alt+M edge,
including when the resulting option flag is off. Music-off executes the exact
36-write `B3` retirement plus depth-preserving `C3` cleanup on the live YM3812,
then schedules entry 1 with a 50 ms start offset. It does not close or reset the
chip. The AdLib path shifts the decoded OPL schedule; the speaker renderer
shifts every PIT gate event and lengthens its 44.1 kHz PCM buffer by exactly
4,410 bytes.

Score starts are gated by the selected device. AdLib-to-speaker with music
enabled retires every composite OPL channel but retains the resident chip and
its register/LFO history. Speaker-to-AdLib restarts the score on that owner only
in Demo state 2 and deliberately leaves an already playing speaker waveform
alone. With music disabled, Alt+P changes the routing byte without an invented
blanket stop.

game_render_state_test and munchers_app_headless_test independently compare
the Number and Word Alt+S/M OPL samples with directly decoded entry 1/2
schedules. They also compare the speaker RIFF bytes, require the music-off
path's first audible OPL sample to occur no earlier than the 50 ms boundary,
gate the 4,410-byte speaker delay, and inspect the live register shadow after
GSND 0: all nine connection/key-on values are zero, all nine carrier/release
values are `3F/FF`, and `BD` retains `C0`. App-level tests run Alt+M and
speaker-to-AdLib Alt+P through state-2 board, state-2 board-to-Hall Wipe,
state-3 Hall transition, and state-1 logo/splash transition cases in both
games, then verify the same event exits Demo.

These static and in-memory tests establish the executable control flow,
resource choice, timing offset, and native ownership transitions. They close
the application-controlled audio boundary: timestamped YM3812/PIT events and
synthesized PCM/RIFF bytes are the authoritative outputs. Physical AdLib
hardware, Windows mixer gain, and speaker filtering/transients remain optional
machine QA rather than executable behavior. See
`PLATFORM-BOUNDARY-AUDIT.md`.
