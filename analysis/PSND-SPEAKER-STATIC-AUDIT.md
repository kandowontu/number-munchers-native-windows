# PSND and PC-speaker static audit

This recovery used the supplied resources, unpacked executable, and headless
console tests only. No DOSBox or game window was launched.

## Device selection and dispatch

The original README describes Alt+P as the effects-device toggle between the
AdLib board and internal PC speaker, while Alt+M controls the saved music flag.
The executable's resident-driver state is more specific. The setter at image
`0x0824` makes argument 3 select device zero and argument 4 restore the saved
detected device; the getter at `0x0886` returns that current byte. The note
handler near `0x0142D` invokes the speaker helper when it is zero, and the
music-class dispatcher rejects new score events in that state.

Alt+P is asymmetric: AdLib-to-speaker submits the all-channel GSND entry 0 when
music is enabled, stopping the score, while speaker-to-AdLib restarts event
`0x90` only in Demo selector state 2. With music disabled neither direction
invents an immediate stop. See `SOUND-TOGGLE-STATIC-AUDIT.md` for the complete
Number/Word branches and the entry-1/2 toggle acknowledgements.

Scene setup sets `DS:138A` to one. The resident wrapper at image `0x10DEF` adds
three to every non-music event while that bank is active. Consequently SCPT
opcode-`0D` event `N` selects stream `N + 3`; the five supplied scenes use
events 1–34 and therefore the shared stream range 4–37. PSND 11 and ADLI banks
11–15 contain byte-identical common streams through that range. This corrects
the previous native direct-index interpretation.

## Recovered speaker synthesis

The 128-word frequency table at image `0x006D4` is copied exactly, including
the anomalous encoded-note value `0x61 = 1009`. Opcode `93`'s adjustment is
zero-extended before addition, matching the driver. For channel-zero notes the
native decoder records the selected table frequency; gate expiry, stream end,
and channel-zero stop commands record speaker-off events.

The tone helper at image `0x26941` divides the literal PIT input clock
`0x1234DD` (1,193,181 Hz) by the requested frequency, programs channel 2 via
ports `43h`/`42h`, and enables bits 0/1 of port `61h`. The helper ignores values
at or below 18 Hz. The stop helper at image `0x2696D` gates the speaker off.
Native renders the same integer-divisor square-wave sequence to embedded PCM,
without accessing hardware ports.

## Exhaustive direct-helper references

The unpacked Number image contains exactly five driver-internal calls to the
start/stop helpers and the two calls in the joystick-speed feedback wrapper:

| Number image | Helper | Recovered role |
|---:|---|---|
| `0x00EE1` | stop | common stream/channel-stop opcode |
| `0x00F73` | start | ordinary note dispatch through the 128-word table |
| `0x01437` | start | note-frequency install/rewrite |
| `0x01497` | stop | note gate-off |
| `0x01769` | start | active-note pitch rewrite |
| `0x0D318` | start | direct joystick feedback wrapper `0xD307` |
| `0x0D32A` | stop | direct joystick feedback wrapper `0xD307` |

Word links the same driver at image `0x22281` (start) and `0x222AD` (stop).
Its seven references occur at the same five driver offsets and at `0xDFC2` /
`0xDFD4` in the isomorphic joystick wrapper. No other far call in either
unpacked image targets either helper. Therefore every ordinary use belongs to
the decoded PSND/GSND stream engine; joystick-speed feedback is the only game-
level route that bypasses the configured effects device. The native split
between `renderMeccSpeaker*` and `renderPcSpeakerToneToWave` covers precisely
those two ownership classes.

GSND 7 provides a concrete regression: the recovered frequency writes are
450, 440, 523, and 554 Hz at 0, 54, 109, and 164 ms. Headless tests require
those events, non-silent RIFF output for single and compound gameplay cues, all
38 common PSND streams to decode, and the persisted Alt+P selection to route
effects through the speaker renderer. The toggle gate additionally compares
speaker GSND 1/2 RIFF bytes and requires Alt+M-off's 50 ms delay to add exactly
4,410 bytes at 44.1 kHz/16-bit mono. Each ordinary gameplay call renders its
recovered resident stream independently: a wrong chew starts stream 8, while a
collision starts stream 12 and later emits stream 10 at the bite terminal.
Safe-zone stream 4 and warning stream 9 remain separate scheduler events. See
`GAMEPLAY-SOUND-ROUTING-STATIC-AUDIT.md` for the complete cross-game call map.

## Remaining verification boundary

The implementation is structurally recovered and exercised, but it is not an
audible 1:1 claim. A Windows PCM square wave and mixer are not the physical PIT
channel-2/speaker circuit, and no side-by-side device capture has been made
under the no-window constraint. Exact perceived level, filtering, hardware
transients, and AdLib/speaker audible comparison remain open in the parity
ledger. The former AdLib single-chip composition gap is closed separately in
`ATTRACT-AUDIO-CAPTURE-AUDIT.md`.
