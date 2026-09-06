# Number Munchers joystick static audit

This audit records the DOS joystick routines recovered from
`NM-unpacked-image.bin` and the corresponding native implementation. It is
static evidence: no visible DOSBox session or original-game process is needed
to reproduce these findings. Image offsets refer to the unpacked DOS load
image.

## Calibration page and prompts

The far routine at image `0x1305D` (entry `1305:000D`) is selected by Options
item 6 at `0x0F04C`–`0x0F051`. Its source strings are:

- `DS:1804` — `\nAttach joystick to computer.\n`
- `DS:1844` — `\nCenter your joystick.\n`
- `DS:185C` — `\nPosition your joystick: left\n`
- `DS:187B` — `\nPosition your joystick: right\n`
- `DS:189B` — `\nPosition your joystick: up\n`
- `DS:18B8` — `\nPosition your joystick: down\n`
- `DS:18D7` — `\nYour joystick is ready for use.\n`
- `DS:18F8` — `Calibrate Joystick`
- `DS:190B` — `Press Space Bar to continue.\nEscape: Options Menu`

The six far pointers at `DS:1824` occur in exactly the prompt order above.
The wait uses the three-key table at `DS:0864`, accepting Escape, Enter, and
Space only. Its shared event converter maps left release to Space and rejects
right release `0xFD`. If detection fails, the attach prompt repeats and detection is
retried after each accepted continuation. Escape returns without committing.

Each prompt contains a leading and trailing line feed, so the shared
flags-`0xC3` modal treats it as three logical rows. The recovered modal formula
and captured four-/six-row instances fix its rectangle at inclusive
`(0,81)..(319,118)`, with black fill, cyan outer/inner double border, and the
one visible white line centered at x=159, baseline y=95. The page uses the
Options-family frame with title `Calibrate Joystick`. The lossless VGA capture
corrects the earlier static-only footer inference: the lower rules are
y=171/y=173 and the footer baselines are y=176/y=185.

## Detection and threshold arithmetic

The detector at image `0x13244`–`0x1330B` reads both axes four times. A zero
axis timer means no joystick. Otherwise it averages each four-sample sum and
installs provisional thresholds at center minus/plus one quarter of center.

The six-step jump table at image `0x13106` dispatches as follows:

| Step | Image target | Captured value / result |
|---:|---:|---|
| 0 | `0x13112` | center X and center Y |
| 1 | `0x13130` | left = center X - ((center X - left X) >> 1) |
| 2 | `0x1314B` | right = center X + ((right X - center X) >> 1) |
| 3 | `0x13162` | up = center Y - ((center Y - up Y) >> 1) |
| 4 | `0x1317E` | down = center Y + ((down Y - center Y) >> 1) |
| 5 | `0x13196` | set detected/calibrated bit 1 and calibrated byte |

Success persists the configuration at `0x131BA`. Calibration does not force
enabled bit 0 on; the user's `Turn Joystick ON/OFF` choice remains independent.
Native preserves that transaction and stores the calibrated flag and all four
thresholds in settings format version 3, while continuing to read versions 1
and 2.

## DOS gameport mapping

The helpers at `0x131CB` and `0x131F2` read port `0x201`: button index N uses
bit `4+N`, while an axis timer has a `0x04FF` timeout. The mapper at
`0x1330C`–`0x133AF` constructs this mask:

| Source | Mask |
|---|---:|
| axis Y below up threshold | `0x01` |
| axis X above right threshold | `0x02` |
| axis Y above down threshold | `0x04` |
| axis X below left threshold | `0x08` |
| button 1 | `0x10` |
| button 0 | `0x20` |

The 64-byte lookup at `DS:054C` maps the exact single-axis masks to Up, Right,
Down, and Left extended keys. Diagonals and conflicting axis masks map to zero.
Indices 16–31 map button 1 to Enter; indices 32–63 map button 0 to Space, so
button 0 wins if both buttons are down.

The poll/event routine at `0x133B0`–`0x13573` is active only when enabled and
detected/calibrated bits are both set. Button presses are edge-triggered and
button releases return zero. The preserved direction-repeat word at `DS:17FC`
is 4, or about 137.3 ms at `1,193,182 / (0x0555 * 0x1E)` Hz. The common input
jump table maps `+`/`=` to decrement and `-`/`_` to increment that word when
either joystick flag is set, clamped to 1–10 public scheduler ticks. See
`KEYBOARD-INPUT-STATIC-AUDIT.md` for the matching Number/Word branches.

Button handling precedes the directional gate at `0x1348F`. The gate byte at
`DS:17FE` is set by player initialization (`0x08BB1`) and the movement terminal
(`0x095D8`), but cleared when movement begins (`0x08CB9`). A clear gate returns
at `0x13496` without sampling the axes or rescheduling the direction deadline;
buttons therefore remain live during a walk while directional repeats do not.
When a deadline is due, `0x134E2` obtains a fresh 32-bit public-clock value and
`0x134F0`–`0x134FE` stores `next = now + repeat`. It does not add the interval
to the obsolete deadline, so scheduler/host lateness can emit at most one key
and cannot cause an immediate catch-up burst.

Direction changes are not returned immediately. On a non-due poll,
`0x1351F` refreshes the cached public clock, `0x13543` maps the complete
gameport state, and `0x13562`–`0x13568` stores that result in pending byte
`DS:1802` and last-sample byte `DS:61D8`. A neutral or diagonal sample is zero
and can clear the pending direction without altering the periodic deadline.
At the next due poll, `0x13502`–`0x13518` copies the pending byte to the event
result and clears it; a continuously held direction is sampled again for the
following deadline. Button press/release branches return before this logic and
do not reset the pending byte or deadline. This is a phase-clocked pending-
sample queue, not a direction-change autorepeat timer.

After each actual change, image `0xD307` computes `1000 - 50 * repeat`, starts
the direct PIT channel-2 helper at `0x26941`, waits 50 ms, and calls the gate-off
helper at `0x2696D`. Thus faster settings produce higher feedback pitches from
950 down to 500 Hz. This is unconditional PC-speaker feedback rather than a
selected PSND/AdLib game cue; disabled input and attempts beyond bounds are
silent.

## Native correspondence and tests

`Game::setJoystickState` accepts the platform sample; `main.cpp` supplies it
from `joyGetPosEx(JOYSTICKID1)` without opening a separate UI. The game state
machine implements the attach/six-prompt transaction, threshold formulas,
enabled/calibrated gate, button priority and edges, single-axis lookup, and
mutable 1–10-tick repeat through the ordinary keyboard paths. Its integer
public-clock snapshot/deadline and pending/last-sample bytes mirror the
recovered poller; the native 16 ms presentation update performs two dense
polls at one public-clock value to retain the DOS sample-before-delivery order.
The recovered 50 ms PIT feedback tone now accompanies every successful speed
change even with Sound off or AdLib effects selected.

Static main-pump ownership narrows “ordinary keyboard paths” precisely: this
shortcut runs in Demo, active/paused/feedback play, and cartoons, but not while
a synchronous menu, waiter, or editor owns input. In Demo the repeat branch
precedes selector states 1-3, so one key changes the word, sounds feedback, and
then exits. Native enumerates every modal/live page and locks that ordering.

`game_render_state_test` independently composes every calibration prompt frame,
checks disconnected retry, the left-release continuation/right-release no-op,
exact 1000/2000-center threshold results
600/1400/1200/2800, commit and Escape rollback, settings round trip, four
directions, diagonal rejection, repeat cadence, button priority/edge/release
behavior, all four repeat-key forms and bounds, one/ten-tick deadlines, exact
feedback-pitch/lifetime ownership, bound/disabled tone suppression, overdue
deadline reset, queued rather than immediate direction changes, neutral/
diagonal pending clears without phase reset, button-preserved phase, the
movement-only direction gate, and disabled/disconnected gating. The muted
lossless VGA capture at `analysis/number-live/options` now supplies the missing
original calibration-page comparison: frames 740–938 are stable copies of the
attach page and match native exactly after correcting the footer by nine pixels.
Its reproducible auditor verifies both RGB SHA-256 and FNV-64. This closes the
application-controlled input boundary: WinMM axis/button samples, calibration,
translation, scheduling, and ownership are exact gates. A particular physical
controller's centering and noise are normalized by that calibration and remain
optional machine QA. See `PLATFORM-BOUNDARY-AUDIT.md`.
