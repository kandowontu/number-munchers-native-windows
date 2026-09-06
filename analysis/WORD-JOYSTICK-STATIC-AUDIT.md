# Word Munchers joystick static and live audit

This audit uses `WM-unpacked-image.bin`, `WM-unpacked.ndisasm`, the supplied
`WM.CFG`, and in-memory native tests. A later muted DOSBox-X run captured the
disconnected/attach prompt without accessing a physical joystick. Its exact
original/native comparison is part of
`analysis/word-live/options-gaps/report.json`. Image offsets refer to the
unpacked DOS load image.

## Word calibration routine

Word Options item 5 dispatches to `123e:000b`, image `0x123eb`. The complete
calibration routine ends at `0x12558`. Its six-prompt loop preserves this order:

1. `Center your joystick.`
2. `Position your joystick: left`
3. `Position your joystick: right`
4. `Position your joystick: up`
5. `Position your joystick: down`
6. `Your joystick is ready for use.`

Failed detection instead shows `Attach joystick to computer.` and retries
detection after Space or Enter. Escape at any point returns to Options without
installing the draft. The page title is `Calibrate Joystick`; the footer is
`Press Space Bar to continue.` / `Escape: Options Menu`.

The shared accepted-key event converter also maps left release to Space and
leaves right-release code `0xFD` unmatched. The native prompt loop and headless
gate preserve that distinction.

Each source prompt contains leading and trailing line feeds. The shared
flags-`0xc3` message painter therefore creates a full-width double-cyan modal
at inclusive `(0,81)..(319,118)`, with the visible prompt centered at x=159,
baseline y=95. The native presenter composes that modal on the ordinary Word
Options frame.

## Detection and calibration arithmetic

The axis timer is at image `0x12580`; its timeout is `0x04ff`. The detector at
`0x125d2..0x12699` reads both axes four times, rejects a zero result on either
axis, averages each four-sample sum, and installs provisional thresholds at
center minus/plus one quarter.

The calibration jump table at `0x1248b` selects these cases:

| Step | Image | Result |
|---:|---:|---|
| 0 | `0x124a0` | capture center X and center Y |
| 1 | `0x124be` | left = center X - ((center X - left X) >> 1) |
| 2 | `0x124d9` | right = center X + ((right X - center X) >> 1) |
| 3 | `0x124f0` | up = center Y - ((center Y - up Y) >> 1) |
| 4 | `0x1250c` | down = center Y + ((down Y - center Y) >> 1) |
| 5 | `0x12524` | set calibrated flag bit 1 |

Success reaches the configuration-save call at `0x12548`; it does not set the
independent enabled bit 0. Native preserves that transaction and the supplied
Word configuration field ordering: horizontal-low/right and vertical-low/down
are mapped to left/right/up/down runtime thresholds. Per-user saving of the
new Word values is gated in `WORD-PERSISTENCE-AUDIT.md`.

## Direction and button mapping

The mapper at `0x1269a..0x1273d` constructs the same six-bit mask directly in
the Word executable:

| Input | Mask |
|---|---:|
| Y below up threshold | `0x01` |
| X above right threshold | `0x02` |
| Y above down threshold | `0x04` |
| X below left threshold | `0x08` |
| button 1 | `0x10` |
| button 0 | `0x20` |

The lookup accepts only the four single-axis masks as Up/Right/Down/Left;
diagonals and conflicting directions return no key. Button 1 maps to Enter,
button 0 maps to Space, and button 0 wins when both are held.

The event routine at `0x1273e..0x12901` requires both enabled and calibrated
bits (`flags & 3 == 3`). Button transitions are edge-triggered and releases
emit no key. The repeat word at `DS:24ac` is `4` in the preserved image, so a
held direction repeats after four public scheduler ticks—about 137.3 ms at
`1,193,182 / (0x0555 * 0x1e)` Hz. The Word common-input table sends `+`/`=` to
`0xDE64` (decrement) and `-`/`_` to `0xDE44` (increment) whenever either
joystick flag is set, with inclusive repeat-word bounds 1 and 10. These branches are
isomorphic to Number's `0xD1BA`/`0xD19A` pair.

The button branches occur before the direction-active check at `0x1281D`.
`DS:24ae` is initialized/restored at `0x0977E` and `0x0A1A5`, and movement
start clears it at `0x09886`. While it is clear, `0x12824` returns without an
axis sample or direction-deadline update; button edges are still observable.
The due branch at `0x12870` then reads the current 32-bit public clock and
`0x1287E`–`0x1288C` writes `next = now + repeat`. Thus a late presentation
frame produces one repeat followed by a complete new interval, never a burst
caused by carrying an overdue remainder forward.

Word also has the same pending-sample phase. On non-due polls, `0x128AD`
refreshes the cached clock and `0x128D1` maps the gameport into pending byte
`DS:24b2`, retaining the last sampled value at `DS:63a0`. The due branch
`0x12890`–`0x128A6` emits that pending byte and clears it without sampling a
new direction. Zero from neutral/diagonal input therefore clears only the
pending event, not the deadline; held input is re-sampled for the next period.
Neither button-edge branch resets these direction-phase fields.

Each successful change calls Word image `0xDFB1`. That is an isomorphic direct
PC-speaker helper: it computes `1000 - 50 * repeat`, starts PIT channel 2,
busy-waits 50 ms, and closes the gate. The resulting settings 1..10 therefore
sound at requested pitches 950..500 Hz regardless of the Sound/Alt+P device
selection; no tone occurs when the flag gate or a clamp prevents a change.

## Native headless gate

`WordGame::setJoystickState` accepts the existing WinMM sample already routed
by the one-window shell. The Word runtime now implements four-sample detection,
the six-step transaction, rollback, exact midpoint thresholds, enabled/
calibrated/disconnected gates, single-axis direction mapping, button priority
and edges, the public-clock pending-sample phase, and the mutable 1–10-tick
repeat through ordinary Word key routes.
It also reproduces the unconditional 50 ms PIT feedback tone.

The shortcut belongs specifically to Word's resident main-loop dispatcher:
Demo, active/paused/feedback play, and cartoons execute it, while synchronous
menus, waiters, and editors do not. The branch precedes Demo selector states
1-3, so one translated repeat key adjusts and sounds before that same event
returns to Title. Headless tests enumerate the full modal/live boundary.

`munchers_app_headless_test` locks the attach and six calibration states with
these native VGA FNV-64 frame hashes:

| State | FNV-64 |
|---|---|
| Attach, exact live fixture | `761c0d49d5d02fa6` |
| Center | `27ff5a6703f2c142` |
| Left | `46644e94b2fe9619` |
| Right | `dffe93105501d39b` |
| Up | `97830740c746ee01` |
| Down | `e30346353ed1593e` |
| Ready | `72aab5108b701944` |

The same test independently proves the 1000/2000-center example produces
600/1400/1200/2800 thresholds, Escape rollback, all four directions, diagonal
rejection, repeat timing, Space-over-Enter button priority, button edges, and
disabled/disconnected suppression. It additionally locks the default 4,
disabled-flags no-op, enabled-only/calibrated-only adjustment gates, exact
all four repeat-key forms and bounds, one/ten-tick deadlines, the exact
feedback-frequency formula and 4,454-byte waveform, bound/disabled tone
suppression, schedule-from-now behavior after host overrun, and the player-
movement direction gate with button processing left ahead of it. It also
requires sample-before-delivery direction changes, deadline phase across zero
samples and button edges, and held-direction requeue after each delivery. The
attach page is an exact live original/native gate; the five detected-direction
prompts and ready page remain static/native. This closes the
application-controlled input boundary: WinMM samples, calibration, translation,
and timing are exact gates. Physical-stick centering and noise are normalized by
that transaction and remain optional machine QA. See
`PLATFORM-BOUNDARY-AUDIT.md`.
