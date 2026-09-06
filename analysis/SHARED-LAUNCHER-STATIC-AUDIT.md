# Shared launcher and window-ownership audit

This audit covers the requested native collection shell without starting the
GUI, DOSBox, or any original executable. The evidence is the Win32 source
and the device-free `munchers_app_headless_test`.

## One-window ownership

`wWinMain` owns one `MunchersApp` and one Win32 window. `MunchersApp` has four
exclusive modes: collection launcher, Number Munchers, Word Munchers, and
Super Munchers. It
constructs only the selected native game controller and destroys that
controller on return to the launcher; it never starts another executable or
creates a game window.

The `WM_TIMER` handler asks the collection owner whether the application should
quit. Only then does the Win32 shell call `DestroyWindow`. A game's own
terminal `shouldQuit` flag is intercepted earlier by `consumeGameQuit`, which
restores launcher mode and the corresponding launcher selection while leaving
the application-level quit flag clear.

The supplied controllers do not raise that terminal request immediately.
Number action 6 calls `0x1243E` at `0x12A53`; Word action 6 calls the
instruction-equivalent `0x11BC4` at `0x120F4`. Both helpers first wait the
literal 15 ms through their resident drivers. The native game controller now
retains the title for that blocking interval and raises `shouldQuit` only at
its terminal, after which `MunchersApp` returns to the launcher. See
`CONTROLLER-CLEANUP-DELAY-STATIC-AUDIT.md`.

## Complete quit routes

The headless integration gate covers both title-level and confirmed in-game
routes:

- Number title Escape waits the original 15 ms, then returns to launcher
  selection 0.
- Number gameplay Escape, Yes, post-game Hall Escape, replay-question Escape,
  and title Escape stays inside Number until the last step, then returns to
  launcher selection 0 without terminating the process.
- Word title Escape waits the original 15 ms, then returns to launcher
  selection 1.
- Word gameplay Escape, Yes, and title Escape first returns to the Word title,
  then returns to launcher selection 1 without terminating the process.
- Super starts at its version/splash/title controller, and terminal title
  Escape returns to launcher selection 2 without terminating the process.
- Selecting the launcher's Exit entry is the tested process-termination route.
  Launcher Escape is an equivalent shell-level exit shortcut.

The gate retains each active game at a 14 ms nonterminal point, confirms that
the title delay owns input, and observes the launcher return only at 15 ms.
The native launcher's own Exit remains immediate because it is not either DOS
controller's action 6.

Mouse ownership also crosses that terminal boundary. `MunchersApp`, rather
than the shorter-lived game controller or Win32 window procedure, remembers
whether each physical left/right press was consumed. If the corresponding
release arrives after the game has returned to the launcher, the collection
owner suppresses exactly that release instead of activating a launcher row.
Headless routes cover Number/left and Word/right transitions and prove that a
subsequent ordinary click remains active.

## Coalesced host time

The window's steady-clock interval is no longer truncated at 100 ms. The
persistent app owner preserves the full interval and feeds it to the active
controller in ordered slices no larger than the formerly gated 100 ms update.
A headless Word route proves that one coalesced 350 ms update crosses the same
30-second idle-to-Demo boundary as 100+100+100+50 ms. See
`HOST-ELAPSED-TIME-STATIC-AUDIT.md`.

These wrapper checks supplement the game-level tests that lock each original
Hall, replay, and quit-confirmation transition.

## Requested shell hotkeys

The Win32 key handler resolves Alt+Enter before forwarding input to
`MunchersApp`, so fullscreen remains a property of the one owner window across
launcher and all three games. It resolves Ctrl+Alt+F1 at the same boundary and
forwards only the level-selector action to the active native game. At the
launcher that action is deliberately inert; a headless check proves that it
cannot change selection, launch a game, or terminate the process.

Both requested toggle shortcuts now accept only a key message whose Win32
previous-key-state bit 30 is clear. The initial Alt+Enter toggles fullscreen
once and the initial Ctrl+Alt+F1 toggles the active cheat overlay once; held-key
repeat messages are consumed without bouncing either state. A headless flag
gate covers plain, Alt-context, repeat, and Alt-context-plus-repeat messages.

Alt+S/M/P now enter one app-level handler from `WM_SYSKEYDOWN`. It exposes the
shortcuts only when the active game's recovered resident dispatcher owns input.
On startup it forwards one inert non-character acknowledgment; title, modal
widgets, the launcher, and the cheat overlay cannot toggle audio. In live
states Alt+S toggles and returns directly, while Alt+M/P toggle and forward the
same event into selector 100. Headless integration tests prove Number and Word
Demo exit, Word cartoon skip, Number cartoon skip for both fallthrough keys,
and non-exit for Alt+S. They additionally prove the original Alt+M state-2-only
score resume: board and board-to-Hall Wipe submit `0x90`, whereas state-3 Hall
and state-1 logo/splash phases do not, before the shared event exits Demo.

## Evidence boundary

This audit verifies ownership and routing statically and with in-memory native
controllers, including physical-message pairing modeled without a window. The
existing fullscreen integration evidence remains in the
parity ledger, but no visible-window test was rerun for this audit. Fullscreen
style ownership is independent of the selected controller; the device-free
gate locks Super's level selector and return lifecycle without taking focus or
mouse input from the desktop.
