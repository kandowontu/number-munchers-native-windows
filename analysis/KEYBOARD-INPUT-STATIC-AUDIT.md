# Original gameplay keyboard-input audit

This audit is static and headless. It uses the preserved Number and Word
resource archives plus `NM-unpacked.ndisasm` and `WM-unpacked.ndisasm`; it does
not start DOSBox, either original executable, the native GUI, or any visible
window.

## Player-facing instructions

The gameplay page in both archived `DATA:1` resources says that the Arrow Keys
move the Muncher, that `I, J, K, M, A, and Z` may also be used, and that the
Space Bar eats the current number or word. This establishes that A and Z are
movement aliases, not alternate munch buttons. The resource text does not say
which direction each letter selects, so that mapping comes from the executable
dispatchers below.

## Number dispatcher

The Number Munchers input dispatcher at image `0xD0CB`-`0xD197` normalizes its
letter keys to the same extended key words used for arrow input:

| Letter key | Normalized word | Direction |
|---|---:|---|
| 8, A/a, or I/i | `0x00C8` | Up |
| 4 or J/j | `0x00CB` | Left |
| 6 or K/k | `0x00CD` | Right |
| 2, M/m, or Z/z | `0x00D0` | Down |

The direct comparisons at `0xD0CE`, `0xD13C`-`0xD168` and the printable-key
jump table reached at `0xD0E7` converge on the four stores at
`0xD17A/0xD182/0xD18A/0xD192`. The resulting words are the DOS extended Up,
Left, Right, and Down key encodings. Space remains on the separate gameplay
action route that initializes the chew dispatcher.

## Word dispatcher

Word Munchers carries the same mapping in its corresponding dispatcher at
image `0xDD75`-`0xDE41`. Direct comparisons and the printable-key table reached
at `0xDD91` converge on stores at `0xDE24/0xDE2C/0xDE34/0xDE3C`, which write
`0x00C8/0x00CB/0x00CD/0x00D0` respectively. Space again uses the independent
chew route.

## Resident-dispatcher ownership

The complete Number resident input routine is image `0xD05B`-`0xD306`. Its
main pump at `0x123DE` calls the scheduler, calls this dispatcher at `0x123E3`,
then services audio and loops. Word is byte-isomorphic: its routine is
`0xDD05`-`0xDFB0`, called by the main pump at `0x11B69` after the scheduler
call at `0x11B64`. Both routines query selector 100 before dispatching its live
states.

That ownership does not extend to the synchronous UI routines. For example,
Word's title routine begins at `0xF48D` and enters its fixed-list widget at
`0xF5CE`; Options begins at `0xFA79` and enters its widget at `0xFBD9`. These
calls do not return to the resident pump while their modal page is open. The
paired Number widgets, information viewers, Hall waiters, prompts, calibration
transaction, and line editors have the same private-loop ownership. Therefore
`+`/`-` and Alt+S/M/P are present only in Demo selector states 1-3, active/
paused/feedback states 4-5, and cartoon state 6—not on startup, title,
Information, Options, Hall, prompts, editors, replay, or other modal pages.

Native exposes this boundary through one predicate shared by the window shell
and both runtimes. Headless regressions enumerate every native modal page,
require all four repeat keys and all three audio toggles to remain local/inert,
require the predicate on every live state, and require the cheat overlay to
retain exclusive input ownership.

## Joystick repeat controls

The two jump tables also expose a separate shared input contract. In Number,
the `+` and `=` entries reach `0xD1BA`, while `-` and `_` reach `0xD19A`;
Word reaches the isomorphic branches at `0xDE64` and `0xDE44`. When the
joystick flags word is nonzero, `+`/`=` decrements the direction-repeat word
toward a lower bound of 1, while `-`/`_` increments it toward an upper bound of
10. The preserved repeat word
is 4 (`DS:17FC` in Number and `DS:24AC` in Word). The companion routines at
`0xD307` and `0xDFB1` provide audible feedback after a successful change.
They compute `1000 - 50 * repeat`, call the direct PC-speaker helper at Number
image `0x26941` (Word's linked copy is isomorphic), busy-wait 50 ms, then close
the PIT channel-2 gate. No tone is issued when a bound or disabled flag
prevents the word from changing.

Those repeat words are read by the joystick event routines, so lower values
produce faster held-direction repeats. Native now carries the same non-
persistent default, flag gate, key polarity, inclusive bounds, and live repeat
cadence rather than retaining a hard-coded four-tick interval. It also renders
the direct 950..500 Hz, 50 ms PIT-square-wave feedback independently of the
Sound and Alt+P gameplay-device choices. Device-free tests lock the frequency
formula, exact 4,454-byte 44.1 kHz waveform lifetime, replacement-player
ownership, and bound/disabled tone suppression in both games.

## Pause while the player actor is busy

Number's Enter path at `0xD2A2` tests only selector-100 state 4/5 before calling
the Time out routine at `0xD2B3`. Word is isomorphic at `0xDF4C`-`0xDF5D`.
Neither path queries the separate player actor job, whose movement, chew, and
recovery states therefore do not suppress Enter. Escape is likewise handled
from the same high-level state at `0xD2D1`/`0xDF7B`.

Both native games now allow Enter to pause during movement, chewing, and
post-collision recovery. The pause page freezes the outstanding player timer
as well as board jobs; resuming continues the same operation. Focused tests
lock the busy flags and timer values across a long paused update, then require
the original operation to continue after a second Enter.

The Escape branches have an additional audio boundary. Number reaches GSND 0
at `0xD2DD` after the state-4/5 check and before invoking the quit-prompt
callback; Word is instruction-equivalent at `0xDF87`. Native now retires the
resident AdLib channels and closes any speaker waveform gate before exposing
the prompt. Cancel restores the prior board/feedback and pause state but does
not resume the interrupted cue. Headless tests clock the audio owner on Escape
and require exactly one new resident-driver stop before prompt input.

## Level-complete cartoon gate

The same common dispatchers expose the exact keyboard gate while a cartoon is
active. Number reads selector 100's state at `0xD079`-`0xD087`; Word does so at
`0xDD23`-`0xDD31`. Board completion installs state 6 at Number
`0xA1CB`-`0xA1D8` and Word `0xAD98`-`0xADA5`.

Escape is intercepted only for states 4 and 5 at Number `0xD2D1`-`0xD2F0`
and Word `0xDF7B`-`0xDF9A`, so it is ignored in state 6. Enter reaches the
state-6 forwarding branch at Number `0xD2A2`-`0xD2CF` and Word
`0xDF4C`-`0xDF79`; other ordinary keys use the generic forwarding branches at
`0xD2F2`/`0xDF9C`. The gameplay callbacks switch on state before decoding the
key. Their state-6 targets at Number `0x137AD` and Word `0x12B3B` immediately
call cartoon teardown and restore state 5. Thus an ordinary forwarded key
skips the cartoon, while Escape does not open a quit prompt or leave the game.

Native now follows that distinction in both runtimes and stops the active
scene effect during teardown. Printable Win32 input is deliberately consumed
on `WM_CHAR`, rather than its preceding `WM_KEYDOWN`, so the one physical key
cannot both skip the cartoon and move the newly initialized player. Headless
tests lock ignored Escape/modifiers, arrow/Space skip, printable single-event
consumption, level/cursor advance, and absence of a leaked move or chew.

The repeat-control branches occur earlier in both common dispatchers: Number
reaches `+`/`=` at `0xD1BA` and `-`/`_` at `0xD19A`, while Word reaches
`0xDE64` and `0xDE44`; all four then rejoin the selector-state switch at
`0xD26A`/`0xDF14`. In state 6, an enabled repeat-control key therefore first
changes the 1..10 repeat word and then reaches the cartoon teardown callback.
Both native runtimes now preserve this ordered two-effect route, and their
headless cartoon gates require the `WM_KEYDOWN` half to defer, the `WM_CHAR`
half to adjust the repeat word, and that same character event to advance the
level without leaking into the new board.

The audio shortcuts retain one further control-flow distinction. Number Alt+S
at `0xD1DA` returns directly through `0xD303`, whereas Alt+M at `0xD1E8` and
Alt+P at `0xD214` rejoin the common dispatcher at `0xD26A` after toggling.
Word has the same split at `0xDE84`, `0xDE92`, `0xDEBE`, and `0xDF14`.
Alt+M has a second, narrower branch before that fallthrough. Number tests the
music flag at `0xD1F3` and selector state 2 at `0xD201`; the downstream
music-class dispatcher additionally rejects device zero before `0xD207` can
start score event `0x90`. Word is byte-isomorphic at
`0xDE9D`/`0xDEAB`/`0xDEB1`. State 2 owns the Demo board, feedback hold, and
board-to-Hall Wipe. State 3 owns the Hall and its outgoing Wipe; state 1 owns
the logo/splash and its outgoing Wipe until the next board initializer.
Alt+P's own branch is asymmetric: selecting speaker with music enabled submits
the all-channel stop, while restoring AdLib submits `0x90` only in state 2.
The Win32 shell now forwards an internal non-character event after M/P but not
S, but only while the resident dispatcher owns the current state. Outside that
scope it forwards one inert non-character event to the modal owner; this lets
an any-event startup waiter acknowledge Alt+key without activating an absent
audio shortcut. The live route preserves Demo exit and state-6 cartoon skip
without allowing a separate `WM_SYSCHAR` letter to reach the next board.
That complete decision now lives in `MunchersApp::handleAltShortcut`, rather
than an untestable branch split between the window procedure and game owners.

The toggle call itself can synchronously enter the configuration writer's
raw-key alert (`0x07BE9` Number, `0x07F79` Word). In that failure case M/P cannot
reach `0xD26A`/`0xDF14` until a later keyboard event acknowledges the alert and
the toggle call returns. The native asynchronous bridge now holds its internal
M/P continuation until that physical acknowledgment has dismissed the alert;
Alt+S holds no continuation. This prevents the bridge's own synthetic event
from prematurely closing the alert while retaining the original post-return
Demo exit or cartoon skip.

The shared headless test locks launcher suppression; one-gate Number startup
acknowledgments; Number title and cheat suppression; S-direct-return versus
M/P-fallthrough on both Demos; S-return versus M-fallthrough on Word's state-6
cartoon; and failed-save ordering for Number S/P plus Word M. Number's own
cartoon gate independently locks all three
branches against its level transition. Additional app-level regressions start
with music disabled in all four Demo transition phases, count loop submissions,
and require one state-2 resume but no state-3/1 resume before the same Alt+M
event exits Demo in both games. Parallel Alt+P regressions start with speaker
selected and require the same state-2-only AdLib restoration. The resident
sound/device details are in `SOUND-TOGGLE-STATIC-AUDIT.md`.

## Corrected native behavior

The former native shortcut incorrectly treated A and Z as munch buttons. The
Number runtime also passed incorrect direction identities for some I/J/K/M
moves, which changed their directional BTMP group and their five-versus-six
callback duration even when the destination cell looked plausible. Both
runtimes now implement the recovered routes exactly:

- 8/A/I moves up with direction 0;
- 6/K moves right with direction 1;
- 2/M/Z moves down with direction 2;
- 4/J moves left with direction 3; and
- Space alone starts a keyboard chew.

`game_render_state_test` and `munchers_app_headless_test` exercise all sixteen
digit/lowercase/uppercase movement forms. They require the exact destination and
direction identity, reject a letter-triggered chew, and independently require
Space to start the chew in both games. The same tests require the default
four-tick joystick repeat, disabled-flags no-op, enabled-only and calibrated-
only gates, all `+`/`=`/`-`/`_` forms and bounds, and measured one-tick and
ten-tick repeat deadlines. They also lock the complete modal/live ownership
matrix and Demo's ordered repeat-adjustment-then-exit behavior.

The normalization also establishes queue identity. In selector states 4/5,
`pSeqKey` stores normalized `I/J/K/M/Space` bytes in the same ten-byte circular
ring used by `pSeqmouse`; nine entries are usable. A movement, chew, or
post-collision actor state prevents immediate consumption but does not discard
new gameplay bytes. Movement's terminal callback may consume the next byte in
the same callback, the chew terminal restores standing and leaves consumption
for the next public player callback, and input entered during state-6 recovery
waits until the survivor has left and the player returns to state 4.

Two reset boundaries remain deliberate. Collision setup clears every
pre-collision byte, and Number's common Enter branch at `0xD2A2` calls the ring
reset at `1652:02DB` before entering or leaving `Time out`; Word is
instruction-equivalent. A muted live Number run confirms that a pasted `JJ`
moves two cells and that `Space+J` retains the left move behind a correct
seven-tick chew. A separate pause capture confirms that `J` cannot cross the
Enter reset. Both native games now use one capacity-nine FIFO for physical
arrows, printable aliases, Space, and negated pointer targets; focused tests
lock movement, chew, recovery, capacity, and pause-reset timing. See
`GAMEPLAY-INPUT-RING-STATIC-AUDIT.md`.
