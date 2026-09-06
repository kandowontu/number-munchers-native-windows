# Win32 translated-input consumption audit

This is a static and headless audit. It compares the recovered DOS event
waiters with `src/main.cpp`, `src/game.cpp`, and `src/word_runtime.cpp`; it does
not launch DOSBox, either original executable, the native GUI, or a visible
window.

## One DOS event versus two Win32 messages

The Win32 loop calls `TranslateMessage` before dispatch. A physical character
key therefore reaches the runtime as `WM_KEYDOWN` followed by `WM_CHAR`, while
the original dispatcher exposes that press to a blocking waiter as one event.
A transition performed on the first message must consume the second message;
otherwise one digit can both dismiss a page and select a row on the newly
visible numbered menu.

Both native runtimes now defer printable `WM_KEYDOWN` events, including Space,
until `WM_CHAR` on the startup and Demo any-event gates. That translated byte
performs exactly one transition and returns. Backspace, Tab, Enter, and Escape
are handled by `WM_KEYDOWN`; their possible trailing control characters are
explicitly rejected by the following startup gate. Pointer events remain one
direct transition and never synthesize a translated character.

Physical mouse input still arrives as a press/release pair. A high-level
press consumed by an active game must suppress its matching release even if
the game's 15 ms terminal cleanup finishes first and destroys that controller.
The persistent `MunchersApp` owner now records consumption independently for
the left and right buttons. The Win32 bridge forwards every press and release
to that owner, including an out-of-client release at neutral coordinates, so
the paired suppression survives the game-to-launcher transition and clears
after exactly one release.

Headless physical-pair regressions prove that:

- Enter moves version to splash, and its trailing `\r` does not skip splash;
- digit 4 leaves splash unchanged on key-down, enters title on `WM_CHAR`, and
  does not select title row 4;
- Space leaves Demo active on key-down and exits on its translated character;
- a Demo `+`/`=`/`-`/`_` character first applies the resident repeat control,
  then exits Demo with the same event and cannot adjust the exposed title;
- Demo exit retires the score/effect logical channels through resident GSND 0
  without resetting the YM3812; and
- pointer acknowledgments still cross exactly one startup gate; and
- left and right presses consumed during controller cleanup cannot leak their
  delayed releases into the newly restored collection launcher.

## Blocking-gate inventory

The relevant recovered contracts are:

| Gate | Accepted DOS event | Native consumption |
|---|---|---|
| Number/Word version | any acknowledgment or the literal 300-tick deadline | one physical key pair/pointer event, or an exact native clock timeout |
| Number/Word splash | any acknowledgment; no deadline | one physical key pair or one pointer event |
| Number/Word Demo | any forwarded input event; repeat control precedes the selector-state exit | one key pair or pointer event; optional one repeat adjustment, then return to title and stop Demo audio |
| Number/Word rejected password | Escape, Enter, or Space; left release becomes Space; right release is rejected | unsupported key-down/character pairs remain on the overlay; an accepted event is consumed before the fresh editor |
| Number/Word initially empty Hall eraser | Escape, Enter, or Space; left release becomes Space; right release is rejected | unsupported key-down/character pairs remain on the message; an accepted event exits without selecting the restored outer menu |
| Number/Word level-complete scene | any forwarded ordinary key; Escape/modifiers excluded | translated character tears down once and cannot move the next-board player |
| Number Set Content Help and Word vowel help | Escape, Enter, or Space only | unsupported key-down and character halves remain on help and have no global shortcut effect; Number arrows are inert |
| Accepted-key pointer waiters | left release becomes Space; right press/release remain absent from the table | Information, user Hall, feedback, calibration, validation/rejection/initial-empty messages, Number help, and Word help/Preview use the same page-owned conversion |

These help rows are not any-key pages. Word's routine at image `0x1499E`
calls the common accepted-key waiter at image `0x0BCC7`; its `DS:14D4` table is
exactly `1B 0D 20 00` (Escape, Enter, Space, terminator). The native page now
ignores letters, arrows, and punctuation. In particular, an ignored `+` or `-`
cannot reach the common joystick-repeat shortcut while help remains active.
Number's Set Content Help makes the same wrapper calls at `0x15111` and
`0x1512D`, so Left/Right/Up/Down do not page backward or forward.

Pages with explicit key tables—Information, ordinary Hall, validation and
password-rejection messages, previews, feedback, and both help owners—remain
governed by their recovered lists rather than any-event routing.

The common accepted-key routine also owns mouse translation. Number image
`0x0B3C1` and Word image `0x0BFA2` rewrite event type 2 to Space before table
matching; neither rewrites right-release type 13/code `0xFD`. Native therefore
keeps the fixed-widget right-release-to-Enter rule out of these pages. See
`ACCEPTED-KEY-POINTER-STATIC-AUDIT.md`.

The same message boundary now respects resident-dispatcher ownership. Alt+S/
M/P are evaluated only during Demo, active/paused/feedback play, or a cartoon.
On a synchronous modal page the shell forwards one non-character event to that
page instead of toggling audio; fixed widgets and editors ignore it, while a
startup any-event gate can still consume it exactly once. The launcher and the
native cheat overlay likewise do not expose the original resident shortcuts.

## Verification boundary

`game_render_state_test` and `munchers_app_headless_test` execute the complete
keydown/character sequences above. The latter also executes the app-level Alt
route across launcher, startup, title, cheat, Demo, and cartoon ownership and
requires the original S-return versus M/P-fallthrough effects. It also counts
Alt+M score submissions across the state-2 board/terminal Wipe, state-3 Hall
Wipe, and state-1 logo/splash Wipe in both games. Its launcher regression
holds a consumed Number left press and Word right press across controller
destruction, rejects the first launcher release, and accepts the next complete
click. This proves
native state and input consumption without taking focus. It is not represented
as a new live DOS capture or a physical keyboard-layout comparison.
