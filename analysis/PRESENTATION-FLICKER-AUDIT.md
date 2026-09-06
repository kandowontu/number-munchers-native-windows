# Native presentation/flicker audit

This audit covers the shared Win32 presentation shell used by the launcher,
Number Munchers, and Word Munchers. The application-controlled erase paths were
first recovered statically and gated through the headless renderer. A later
muted native-window probe records the actual desktop presentation path; no DOS
reference or audio device is involved in that probe.

## Complete-frame ownership

Every controller composes a full 320×200 frame into `Renderer::pixels_`.
`Renderer::paint` then:

1. obtains a client-sized private compatible bitmap;
2. clears that private bitmap for intentional letterbox bars;
3. scales the complete 320×200 frame into it; and
4. commits the entire client area with one `BitBlt(..., SRCCOPY)`.

The client device context never receives the intermediate clear or individual
game drawing operations. The only visible GDI operation is the final complete
bitmap copy.

## Windows erase paths

The window procedure invalidates timer frames with `InvalidateRect(...,
FALSE)`, handles `WM_ERASEBKGND` without painting, and registers no class
background brush. The class now uses only `CS_OWNDC`; `CS_HREDRAW` and
`CS_VREDRAW` were removed because those flags can schedule a whole-client
background erase during size changes even when ordinary timer invalidations
request no erase.

Resize and Alt+Enter transitions use `RedrawWindow` with
`RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE`. This synchronously presents a
complete newly sized backbuffer rather than exposing a black class background
until the next 16 ms timer message. The fullscreen helper performs the same
no-erase redraw after either style transition, including a restoration whose
client dimensions happen not to change.

The Alt+Enter bridge additionally rejects `WM_SYSKEYDOWN` repeat messages by
their previous-key-state bit. Holding the chord can no longer churn repeatedly
between windowed and fullscreen styles, eliminating that separate source of
rapid resize/redraw transitions.

## Visible GDI/DWM probe

`tools/native_flicker_probe.ps1` temporarily forces Number's saved Sound and
Music flags off, restores the exact settings bytes in `finally`, pins one
native game window above unrelated applications, and records an interior
desktop rectangle rather than calling `PrintWindow`. This observes the pixels
actually presented by DWM, not merely the application's backing surface.

The seven-second run in `analysis/native-flicker/native-presentation-probe.mkv`
keeps a live level-1 board under 16 ms timer invalidation while moving the
Muncher, making two Alt+Enter fullscreen round trips, and resizing the window
out and back. FFmpeg requested 120 frames per second and delivered 761 visible
frames. The automated scan excludes a 40-pixel outer margin and evaluates the
inner 800×420 client area. Its darkest frame still has luma maximum 235; zero
frames meet the conservative near-black test `YMAX <= 24`. The exact report is
preserved as `analysis/native-flicker/report.json`, with five extracted samples
covering the windowed, fullscreen, resized, and restored states.

This directly revalidates the reported black-flicker defect on the current
machine and build. It is not evidence for unrelated original/native pixel or
cadence rows.

## Regression boundary

The headless render tests lock static board/header pixels throughout the chew
animation and complete-frame hashes across the major launcher, menu, board,
feedback, Hall, and transition states. The production artifact test builds and
inspects the same GUI executable. Those gates prove persistent frame
composition and the no-loose-resource release contract, but they cannot observe
Desktop Window Manager presentation between GDI calls.

The current executable is both statically hardened against every
application-controlled erase path and visibly revalidated through continuous
timer painting, live movement, resize, and fullscreen transitions. Re-running
the guarded probe requires explicit `-AllowVisibleWindow`; it also rejects any
near-black inner-client frame and always closes the GUI in `finally`.

The same contract now covers the retained SCPT cartoon painter. Original Word
captures expose partial single-buffer dirty-rectangle refreshes between some
stable scene states. Native applies those clears and construction-order actor
repaints to its private 320×200 retained surface, then presents only the
completed frame. It therefore matches all 586 DOS-presented distinct stable
Word scene framebuffers without deliberately reproducing the original's
transient dirty clears or black flicker.

The live board-to-cartoon audit applies the same rule at the surrounding Wipe.
Each original close contains one torn 2× refresh, each open contains one or
two, and three first actor paints contain one more. Native atomically presents
the completed close, cyan, upward-open, black, and first-tick states instead.
The 41–44-sample covered hold and 6–8-sample cleared-black interval remain
measured timing; only the torn intermediate refreshes are suppressed. See
`analysis/word-live/scenes/transition-report.json`.
