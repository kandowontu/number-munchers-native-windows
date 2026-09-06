# Gameplay sound-routing static audit

This audit uses `NM-unpacked.ndisasm`, `WM-unpacked.ndisasm`, and the embedded
Number `GSND:12` and Word `GSND:2` banks. All verification is static or runs in
the console test build. It does not start DOSBox, either original executable,
the native GUI, or a Windows audio device.

## Dispatcher class and device gate

The ordinary callers below pass a literal stream number without bit `0x80`.
They are sound effects: the Sound flag gates them and the current driver routes
them to AdLib channel 0 or PC speaker. The Music flag does not suppress them.
The attract conductor is the separate music-class event `0x90`, which selects
stream 16 and is disabled on the PC-speaker driver.

The same high-bit class also carries post-Wipe cartoon event `0xA6`. It selects
stream 38 from the currently loaded scene ADLI bank, not the gameplay GSND
bank, and is likewise Music/AdLib-gated. Its conductor lifecycle is audited in
`CARTOON-SCORE-STATIC-AUDIT.md`.

Consequently, normal board construction is silent. Number's historical
`board-start-adlib.dro` filename does not establish a board-start call site;
its 219 ms duration instead matches stream 12, whose recovered uses are the
collision initializer and the attract Hall-to-logo action.

## Number Munchers direct map

| Stream | Load-image call site(s) | Recovered boundary |
|---:|---|---|
| 3 | `0x09C60` | warned Troggle begins edge entry |
| 4 | `0x09A2F` | Time Out square activates, after displaced actors retire |
| 5 | `0x0997D` | active Time Out square expires |
| 7 / 8 | `0x08A69` / `0x08A76` | correct / wrong chew begins |
| 9 | `0x08EDB` | warning actor is installed |
| 10 | `0x09DD4` | player-bite terminal removes the Muncher |
| 11 | `0x099FC`, `0x09E58`, `0x09F93`, `0x09FDE` | safe displacement, extra player-cell collider, or cannibal victim retires |
| 12 | `0x096AE` | player/Troggle collision job begins |
| 13 / 14 | `0x12338` / `0x1234B` | first two pages of the pre-game Information group |
| 15 | `0x0A1C4` | ordinary non-Demo board completion |

Main-controller action 1 begins at `0x1289D`. It performs the Hall Wipe,
submits stream 12 at `0x128AE`, installs the logo interstitial, and submits
stream 13 at `0x1291D`. Those duplicate uses do not change the collision and
pre-game meanings above.

Stream 15 is submitted once at ordinary completion. A direct advance lets that
channel-0 cue run until ordinary resident replacement/retirement. On every
third board, the selected-cartoon loader later submits GSND 0, cutting stream
15 off before its literal 15 ms quiet load delay. That delay argument is not a
second stream-15 submission. Demo completion and the immediate one-million-
point terminal bypass stream 15 entirely.

The completion predicate's false return also terminates its caller. If a
Troggle trail removes the last positive cell, Number's safe callback
(`0x099E3`–`0x09A3A`) and Word's corresponding callback
(`0x0A5B0`–`0x0A607`) stop after the stream-15 completion route. They do not
append the displaced-actor stream 11 or Time Out activation stream 4 to the
newly generated board.

## Word Munchers correspondence

Word's ordinary map is isomorphic: 3=entry, 4/5=Time Out on/off, 7/8=chew,
9=warning, 10=player-bite terminal, 11=destructive actor retirement,
12=collision start, 13/14=pre-game pager, and 15=ordinary completion. Its
controller action 1 at `0x11F6E` additionally submits stream 12 at `0x11F7F`
and stream 13 at `0x11FEE` around the Hall-to-logo interstitial.

Both native runtimes now dispatch the controller pair. Neither runtime emits
an ordinary cue while constructing a user board.

## Collision terminal ordering

The common terminal order in both executables is:

1. install a fresh survivor dwell, consuming its PRNG value;
2. remove/charge the Muncher and submit stream 10;
3. choose the randomized feedback phrase;
4. on an ordinary nonterminal life only, scan other state-6 actors on the
   collision cell, remove/rearm each one, and submit stream 11 for each.

Demo and final-life terminals skip step 4 because the imminent terminal route
owns the remaining actor records. This differs from cannibalism, whose terminal
always rearms matching state-6 victims and emits stream 11.

## Preserved DRO traces versus event streams

The five embedded DRO files remain lossless preservation and decoder fixtures.
They were recorded from live play, so several span replacements or multiple
game events and must not be replayed as event-local runtime cues:

- `board-start-adlib.dro` is 219 ms and corresponds in duration to stream 12,
  but the ordinary board initializer has no sound call;
- `level-advance-adlib.dro` is a 1,112 ms final-chew/completion trace, whereas
  the completion event itself is stream 15;
- `wrong-munch-adlib.dro` records a live channel replacement and is not proof
  of a synthetic `8 -> 4` runtime pair;
- `troggle-collision-adlib.dro` is a 1,647 ms compound live trace; warning
  stream 9 is separate from collision streams 12 and 10.

Native runtime routing therefore decodes GSND at each recovered call site. The
DRO resources remain embedded and structurally tested, but no gameplay path
uses them as a substitute for resident dispatch.

## Headless regression boundary

The Number and Word suites now cover all ordinary streams 3-15 on both decoded
device paths and exercise their integrated call sites. They specifically lock:

- silent initial board construction and music-independent ordinary cues;
- correct/wrong chew, warning/entry, Time Out, retirement, Information, and
  completion routing;
- exactly one completion stream-15 submission followed, on the cartoon route,
  by GSND-0 retirement and the 15 ms quiet loader boundary;
- trail-triggered completion as stream 15 alone, with no stale stream-11/4
  safe-zone tail in either game;
- collision `12 -> 10 -> 11` on an ordinary surviving life, including the PRNG
  phrase-before-rearm order;
- collision `12 -> 10` with no extra-collider retirement in Demo and on the
  final life;
- the controller action-1 `12 -> 13` pair in both games; and
- stream 15 through the decoded PC-speaker waveform owner.

This closes the event-routing mismatch and the application-controlled audio
boundary. Timestamped YM3812/PIT events, device routing, and synthesized PCM
are exact gates; physical output gain, speaker filtering, and room acoustics
remain optional machine QA rather than game-code behavior. See
`PLATFORM-BOUNDARY-AUDIT.md`.
