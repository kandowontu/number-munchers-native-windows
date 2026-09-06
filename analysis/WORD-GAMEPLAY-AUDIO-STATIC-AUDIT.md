# Word Munchers gameplay-audio static audit

This gate was recovered from `analysis/WM-unpacked-image.bin`,
`analysis/WM-unpacked.ndisasm`, and the exact embedded `GSND:2` payload. The
work and tests are entirely headless: they do not start DOSBox, `WM.EXE`, the
native GUI, a WinMM audio device, or any visible window. This establishes
resource selection, bytecode decoding, and native event routing; it does not
claim audible equality with a physical AdLib card or PC speaker.

## Resource and dispatcher

Word startup loads archive resource `GSND:2` at image
`0x1014C`-`0x10158`. The common sound dispatcher entered at `1002:0013`
(load-image `0x10033`) sends ordinary game cue numbers to that bank, while
scripted cartoons continue to use their own `ADLI` or `PSND` banks.

The preserved gameplay bank is 2,147 bytes and has SHA-256
`8D5CC45EE4E751450999D8E59B031F4FCFC3A5C93EB226FA7095CA27D69CFBC5`.
Its 256-word stream table and bytecode remain embedded in the self-contained
executable.

## Direct cue map

The following call sites pass literal cue numbers to the common dispatcher:

| Stream | Load-image call | Recovered event | Native boundary |
|---:|---:|---|---|
| 3 | `0x0A82D` | warned Troggle begins its edge-entry actor | `spawnPendingEnemy` |
| 4 | `0x0A5FC` | an inactive Time Out square becomes active | safe-zone activation after occupant processing |
| 5 | `0x0A54A` | an active Time Out square expires | safe-zone deactivation |
| 7 | `0x09636` | chew begins on a positive/correct board record | `beginMunch` |
| 8 | `0x09643` | chew begins on a negative/wrong board record | `beginMunch` |
| 9 | `0x09AA8` | Troggle warning actor is installed | `beginEnemyWarning` |
| 10 | `0x0A9A1` | the 21-tick player-bite sequence removes a Muncher | collision terminal callback |
| 11 | `0x0A5C9`, `0x0AA25`, `0x0AB60`, `0x0ABAB` | destructive Troggle retirement | safe-square displacement, cannibal victim, or extra player-cell collider removal |
| 12 | `0x0A27B`, `0x11F7F` | player/Troggle collision job begins; controller action 1 enters the logo interstitial | `beginTroggleCollision`; Hall-to-logo terminal |
| 13 | `0x11ABA`, `0x11FEE` | first pre-game Information page; controller action 1 installs the logo hold | pre-game group entry; Hall-to-logo terminal |
| 14 | `0x11ACD` | second pre-game Information page | first pager advance in that group |
| 15 | `0x0AD91` | no positive board records remain | board-completion transition |

Initial board construction itself is silent. Stream 3 belongs to the later
Troggle entry transition: the only direct caller of the stream-3 wrapper at
`0x0A7D9` is the edge-entry actor setup ending at `0x09187`.

The duplicate 12/13 calls belong to main-controller action 1 at `0x11F6E`.
It performs the Hall Wipe, submits stream 12, builds/schedules the logo, then
submits stream 13. The native controller now preserves that exact pair.

The stream-11 distinction is also explicit. The slot rearm routine at
`0x09EAA` is followed by stream 11 at destructive removal sites, but its
ordinary edge-exit caller at `0x0AC0D` does not play that cue. Native therefore
does not emit stream 11 for an uneventful Troggle exit.

Trail-triggered board completion is another explicit exception. After the
trail update, the board-live predicate at `0x0AD50` returns false and the
movement (`0x09C2A`) or safe-zone (`0x0A5B1`) callback exits immediately. The
completion cue is therefore stream 15 alone: the stale actor is not rearmed and
neither stream 11 nor stream 4 is submitted afterward.

## Word-only OPL instruments

Exercising the complete gameplay map and attract conductor exposed seven
previously unreachable resident patch keys. The Word driver's 256-word
patch-pointer table begins at image `0x1956`:

| Key | Pointer | Exact 11-byte record |
|---:|---:|---|
| `50` | `0x1F85` | `00 03 0E 00 00 00 02 FF F8 25 FF` |
| `69` | `0x1C4C` | `00 00 06 00 00 01 00 C9 C4 74 F7` |
| `BA` | `0x1FD2` | `60 60 00 03 00 01 0D F4 F3 E7 3F` |
| `DC` | `0x2497` | `11 31 0C 00 00 2D 00 C8 F5 2F F5` |
| `E8` | `0x2510` | `00 00 06 00 00 01 07 C9 C4 74 F7` |
| `E9` | `0x251B` | `00 00 06 00 00 0A 0A C9 C4 74 F7` |
| `F0` | `0x1CF1` | `12 11 06 01 00 FF FF FF FF FF FF` |

`E8` and `E9` are used by gameplay streams 4 and 5 respectively; the other
five are required by the attract-score parts. `wordPatchFor` now contains all
seven exact records, so the renderer does not silently drop either Time Out
transitions or voices from the long score.

## Attract score conductor

Stream 16 at bank offset `0x342` is the music-class `0x90` attract conductor.
It launches two groups of eight persistent tracks:

| Section | Track indices in conductor order | Channels | Duration |
|---|---|---|---:|
| A | `196,194,192,190,188,186,184,182` | `6,7,8,2,3,4,5,1` | 112,459 ms |
| B | `180,179,178,177,176,175,174,173` | `6,7,8,2,3,4,5,1` | 112,460 ms |

Each section lasts `32 * 256` sequencer ticks. Each part intentionally loops
until the conductor replaces it, so the decoder's bounded `runForTicks` path
accepts a part at its exact section boundary while still rejecting malformed
commands or other decode errors. Section B begins at 112,459 ms; the conductor
jumps back after an exact 224,919 ms full cycle.

The native attract host decodes this exact register schedule into one live
`OplStreamPlayer`. Channels 1-8 carry the loop while replaceable demo effects
write channel 0 on the same continuously clocked YM3812. This preserves shared
LFO/noise phase and register interaction instead of relying on the Windows mixer
to combine two independently reset chip renders. Alt+M-off retires every driver
channel on that live chip, waits 50 ms, and submits audible GSND entry 1 without disabling future
sound effects; Alt+M-on submits entry 2 and resumes the score only in Demo
selector state 2 with a nonzero current device. Test mode validates the exact
generated samples and ownership without opening an audio device. Controller and presentation evidence is in
`WORD-ATTRACT-STATIC-AUDIT.md`.

The two original Alt+M call paths at load-image `0x0BDF4` and `0x0DE92` send
event `0x03E9` to the dispatcher at `0x10033`. When music changes from on to
off, that dispatcher calls the driver with command 0, waits 50 ms, then calls
audible GSND entry 1 before clearing the music flag. The enable branch submits
audible entry 2. Thus music-off terminates any effect already sounding but
replaces it with the descending acknowledgement, rather than leaving the
driver silent. Native now executes the exact GSND-0 `B3`/`C3` retirement on the
live composite OPL owner, stops any PC-speaker waveform, schedules entry 1 at
+50 ms, and restarts the state-2 attract score
only when music is enabled and the current device is nonzero. The resident path
at `0xDE9D` tests the music flag, `0xDEAB` compares controller state 2, and
the dispatcher calls device getter `0x0886` before `0xDEB1` can produce
event `0x90`; Hall/state 3, logo/state 1, and PC-speaker selection skip it.

## Native headless gate

`WordGame::playGameplaySound` resolves `GSND:2`, selects the Word-specific
resident sound profile, and routes through either the shared live YM3812 or the
recovered PIT/PC-speaker path. Both owners are compiled with
`NUMBER_MUNCHERS_TESTING` for console tests, which validates register/sample
timelines, RIFF buffers, and replacement counts without opening an audio device.

Ordinary non-Demo AdLib cues and scene-bank callbacks now use that same owner in
idle-score mode. The first cue starts a continuously clocked chip; later cues
replace channel 0 without resetting LFO/noise/register history. Alt+S replaces
channel 0 with entry 1/2 even when the resulting sound flag is off. Alt+P
changes the current driver device; with music enabled its AdLib-to-speaker edge
stops all channels and its state-2 return edge restores the score. The former
reset-per-cue RIFF substitution is documented in
`OPL-LIFETIME-STATIC-AUDIT.md`.

The every-third-board loader additionally submits music-class event `0xA6`
after its opening Wipe and literal 10 ms hold. The resident wrapper selects
stream 38 from the active scene bank only when Music and AdLib are enabled;
Sound does not gate it. Native decodes all six finite channel-9 conductors,
installs their channels-1–8 parts without resetting a live channel-0 callback,
and stops the score at teardown. Exact rates, part lists, schedules, and patch
records are in `CARTOON-SCORE-STATIC-AUDIT.md`.

`munchers_app_headless_test` now:

- decodes and renders streams 3, 4, 5, 7-15 on both device paths;
- decodes stream 16 as a 224,919 ms two-section score and requires second-
  section register writes at the exact 112,459 ms boundary;
- decodes and hashes stream 38 in all six scene ADLI banks, then gates its
  10 ms start, Music/Sound/device classification, channel-0 coexistence, and
  teardown;
- verifies correct and wrong chew dispatch, warning then entry ordering,
  Time Out expiry/activation, and the two pre-game pager cues;
- verifies safe-square and cannibal victim retirement through stream 11;
- runs a two-Troggle player collision through all 21 ticks and locks the
  `12 -> 10 -> 11` start/terminal/extra-collider sequence, including survivor
  dwell, phrase selection, and slot-rearm PRNG order;
- proves Demo and final-life collisions stop at `12 -> 10`, leaving extra
  state-6 actors to the terminal transition rather than inventing stream 11;
- verifies the controller action-1 Hall-to-logo `12 -> 13` pair;
- verifies stream 15 at board completion, speaker routing, and sound-off
  suppression;
- requires Alt+S/M to submit the exact descending/ascending GSND 1/2
  acknowledgements on both devices, including Alt+M-off's literal 50 ms delay;
- requires Alt+M-off to terminate both the attract score and an already-playing
  gameplay cue before entry 1, while Alt+M-on restarts only the non-speaker
  state-2 Demo score and not state-3 Hall or state-1 logo;
- requires Alt+S to retain the channel-1..8 score, Alt+P with music enabled to
  stop it on speaker selection, and only state-2 AdLib restoration to restart
  it;
- locks one second of the actual Word score-plus-stream-7 chip output to signed
  PCM FNV-64 `0xfc21539d1eb4c238`; and
- locks ordinary correct-then-wrong persistent-chip output to signed PCM FNV-64
  `0x5fa8ed9d1584b20a`;
- requires every submitted waveform to pass the native RIFF parser while no
  sound device or window exists.

All four release-mode console tests, including the isolated production-EXE
resource/import gate, pass after this integration.

## Remaining parity boundary

Ordinary gameplay/scene channel-0 ownership, attract class `0x90`, and cartoon
class `0xA6` single-chip score/effect lifecycles are now closed at the
static/decoded/headless-native level. Physical output,
AdLib filtering, speaker level/transients, and side-by-side audible
DOS comparison remain open. The cue and conductor routing in this document
must not be described as sound-device-identical to a live DOS run.

The shared cross-game call map and capture/runtime distinction are recorded in
`GAMEPLAY-SOUND-ROUTING-STATIC-AUDIT.md`. The shared device setter/getter,
both toggle acknowledgements, and Alt+P's
asymmetric branches are audited in `SOUND-TOGGLE-STATIC-AUDIT.md`.
