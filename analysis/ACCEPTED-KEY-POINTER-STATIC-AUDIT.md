# Accepted-key waiter pointer static audit

This audit compares the unpacked Number and Word Munchers executables with the
native input bridge. It is entirely static/headless: neither original game,
DOSBox, the native GUI, nor an audio device was started.

## Shared DOS event conversion

The mouse callback records left release as event type 2/code `0xFB` and right
release as event type 13/code `0xFD`. The fixed-list widget has its own branch
that converts `0xFD` to carriage return, but the accepted-key waiter is a
different input owner.

Number's accepted-key waiter begins at image `0x0B389`. After obtaining and
translating one event, `0x0B3C1` compares the event type with 2 and `0x0B3C7`
replaces its translated byte with `0x20` (Space). It then searches the caller's
NUL-terminated accepted-byte table at `0x0B3D5`–`0x0B3F9`. No corresponding
type-13 override exists, so a right release remains `0xFD` and is rejected
unless a caller explicitly includes that byte.

Word contains the isomorphic waiter at image `0x0BF6A`: its type-2 test is at
`0x0BFA2`, the Space replacement at `0x0BFA8`, and the accepted-table search
at `0x0BFB6`–`0x0BFDA`. Again, event type 13 is not rewritten.

Both games' ordinary waiter table is exactly:

```text
1B 0D 20 00
Escape, Enter, Space, terminator
```

Number's modal wrapper at image `0x0B0E6` pushes `DS:0864` and calls the
waiter at `0x0B389`. Word's isomorphic wrapper at `0x0BCC7` pushes `DS:14D4`
and calls `0x0BF6A`. These are accepted-key wrappers, not any-event waits.

Consequently the exact contract is:

- keyboard Escape, Enter, and Space are accepted;
- a left press is ignored, then its left release is accepted as Space;
- right press `0xFE` and right release `0xFD` are both ignored; and
- the broad fixed-menu rule “right release activates the current selection”
  does not apply inside this waiter.

## Proven caller pages

Number uses this contract for its six Information pages (`0x12F14`), the
argument-7 Hall (`0x12811`), both user feedback waiters (`0x1099F` and
`0x10B4B` through `0x12F14`), and the joystick calibration prompts whose
accepted table is `DS:0864`. The two Set Content Help pages call the wrapper
at `0x15111` and `0x1512D`; their arrows are therefore inert. The Set Content
validation messages pass flag 1 to the generic modal, including the call at
`0x15095`, which selects the same wrapper. The failed-password caller at
`0x0EDB9` likewise passes flag 1 through `0x0AF6A` to `0x0B0E6`. The
initially-empty Hall eraser calls the wrapper directly at `0x0A3E3`.

Word uses the same contract for Information (`0x122A2`), the argument-7 Hall
(`0x11EE2`), wrong-word and collision feedback (`0x0D42C` and `0x0D5CB`),
vowel help (`0x1499E` through `0x0BCC7`), Preview Words (`0x13CB0`), and the
joystick calibration prompts. Its table is `DS:14D4`. All three vowel
validation branches pass flag 1 to the generic modal at `0x146C3`, `0x146F2`,
and `0x14720`. A failed password does the same at `0x0FA41`–`0x0FA49`.
The Set Content Preview zero-target branch passes flag 1 at `0x1381A`; the
changed-draft insufficient-target branch does the same at `0x13865`, and both
call the generic modal. Word's byte-isomorphic initially-empty Hall branch
calls the wrapper at `0x0AF80`.

The native bridge now distinguishes these pages from fixed list widgets.
Their left release enters the normal key dispatcher as Space, their right
release is inert, and feedback accepts ordinary keyboard Enter as well as
Space. Cheat-overlay pointer handling remains above this page-specific bridge,
and Demo retains its separate any-event input owner.

## Headless verification

`game_render_state_test` locks Number feedback Enter/left/right behavior,
Information and Hall left/right releases, and the calibration prompt's pointer
transition. It also locks Set Content Help's inert arrows, left-release advance,
right-release no-op, the validation-message split, and the rejected-password
table. Both initially-empty Hall messages reject arbitrary keys/right release
and accept left release. `munchers_app_headless_test` applies the same checks to
Word and also covers vowel help, vowel validation, Preview Words, and rejected
passwords. It now additionally locks the Set Content insufficient-target
warning's unsupported-key/right-release blocking, Escape/Enter/Space
dismissals, left-release-as-Space route, and same-loop retry.
The complete Release test suite passes without opening a window or producing
device audio.
