# Word Munchers native runtime/presentation audit

This gate uses `analysis/WM-unpacked-image.bin`,
`analysis/WM-unpacked.ndisasm`, the exact embedded Word archives/fonts, an
in-memory native renderer, and preserved muted startup/gameplay captures. Its
repeatable checks do not start DOSBox, `WM.EXE`, the native GUI, or audio.

## Recovered startup gates

The native Word runtime now starts on the preserved product/version page,
using the exact embedded 350-byte Word `PRODUCT.PF`, fixed font, and shared
byte-identical MECC logo. Executable wrapper `0x1193C` gives its first waiter
the literal 300-tick deadline: one keyboard/pointer acknowledgment may reach
the device-specific PCXF `6004/6005` splash earlier, otherwise the public
clock does so automatically. A second acknowledgment reaches the title; the
splash itself does not time out. `Ctrl+Alt+F1` still skips startup and opens
the level selector, and the normal Word quit path still returns to the shared
launcher. Native VGA hashes are `0f20a0f87c2b1bb8` for the version page and
`4cd75eb6d6fea514` for the splash. The latter includes the Word-only
`07CF00 -> CFC700` yellow-ramp entry recovered in
`WORD-PALETTE-STATIC-AUDIT.md`. See
`WORD-STARTUP-STATIC-AUDIT.md` for the product-field offsets, painter geometry,
CGA hashes, muted live run segmentation, and exact original/native hashes.

## Recovered title widget

The Word main-menu caller begins at image `0x0F48D`. It selects PCXF `6009`
for VGA or `6008` for CGA at `0x0F525`–`0x0F543`, draws the footer copied from
`DS:1B1C` at `(10,170)`, and calls the common numbered-list widget with initial
selection 1.

The exact source strings are:

- `DS:1B45`: ` Play Word Munchers `
- `DS:1B5A`: ` Hall of Fame `
- `DS:1B69`: ` Information `
- `DS:1B77`: ` Options `
- `DS:1B81`: ` Quit `
- `DS:1B1C`: `Use Arrows to move, then press Enter.`

`DS:1AB2` is the `{0xFF,0}` custom-key list. The layout record at `DS:1B14`
starts at baseline `y=91`, `x=49`; subsequent rows advance by 11 pixels. The
widget adds the same three-character numbered prefix used by Number Munchers.
With the supplied fixed-width `BIT8X8.GFT`, Word's inclusive title hit boxes
are therefore:

| Item | Rectangle `(left, top) .. (right, bottom)` |
|---|---|
| Play Word Munchers | `(49,90) .. (233,99)` |
| Hall of Fame | `(49,101) .. (185,110)` |
| Information | `(49,112) .. (177,121)` |
| Options | `(49,123) .. (145,132)` |
| Quit | `(49,134) .. (121,143)` |

The native `WordGame` now composes this page directly from PCXF `6009/6008`
and `BIT8X8.GFT`, preserves motion-excluded menu behavior, and implements the
select-on-first-release/activate-on-second-release rule. Its deterministic
VGA FNV-64 is `0xd0862a8ebebc6146`, matching the muted raw original
framebuffer exactly.

## Board and Muncher presentation

The cell-frame helper at image `0x10388` establishes a `48x30` cell. The 30
row-major coordinate pairs at `DS:1FAE` are the same five `y` positions
`26,56,86,116,146` crossed with the six `x` positions
`20,68,116,164,212,260`. The board painter centers each retained six-byte word
record inside those cells. The target-vowel header is selected by the table at
`DS:2026` in the routine at `0x10CF5`; the score, level, three-reserve display
cap, and pause marker are painted by the following `0x10E84`–`0x111E2`
routines.

The native presenter now joins that geometry to `WordGameCore` and the exact
Word resources. `WORD-BOARD-HUD-STATIC-AUDIT.md` separately records the exact
20-string question table, original centering formula, one-pixel sound-5
exception, Level/Demo branches, score box, and reserve coordinates. A native
headless region comparison gates all 20 headers and both HUD variants.

The remaining board behavior includes:

- all 30 generated word records render in the common `6x5` board;
- arrow input and 8/A/I-up, 4/J-left, 6/K-right, 2/M/Z-down aliases use the four
  `BTMP 1006` direction groups and exact five/six-callback cell moves;
- Space alone starts cue 7/8, snapshots and clears the live cell before Word
  `BTMP 1006` record 12, then resolves score/life from that snapshot at the
  exact seven-tick terminal;
- gameplay pointer targets queue and walk one cell at a time, with vertical
  tie-breaking and a separate current-cell click to munch;
- the HUD reads the shared score/reserve state, and the pause/quit overlays
  preserve the common 320x200 composition;
- `Ctrl+Alt+F1` reaches a functional Word level selector (levels 1–20), while
  `Alt+Enter` remains owned by the one-window Win32 shell.

The headless test verifies the complete seven-tick visible chew order and the
tick-7 terminal state. Records 12/14 and 13/15 intentionally contain the same
two pixel poses, so the gate checks the numerical record sequence rather than
allowing duplicate crops to hide record 15. `WORD-SPRITE-STATIC-AUDIT.md` additionally establishes
that Word's PCXF/BTMP 1006 bytes are identical to Number's measured player
resource and locks the eating pose's yellow palette mapping instead of allowing
the archived green ramp to leak into the native board.

The muted live user-game run `analysis/captures/wm_007.avi` now closes this
specific presentation slice. Native matches the stable level-1 board,
right-move, directional target hold, standing arrival, and seven chew-region
hashes. The comparison also recovered the cell label's opaque blue background
box, which clears actor pixels between the white glyph strokes before grid and
safe-corner repaint. `tools/audit_word_gameplay_capture.py` records every run
and identifies capture frames 2465 and 2530 as partial presenter updates rather
than logical poses; see `analysis/word-live/gameplay/report.json`.

## Scripted level-complete host

The presenter now hosts the six recovered Word SCPT cartoons rather than a
fabricated timed overlay. A completed level not divisible by three advances
directly to the next generated board. Levels divisible by three use the exact
ordinary shuffled scene selection from `WordGameCore`; level 18 selects the
special sixth scene. The runtime then:

- binds SCPT 2000/2002/2004/2006/2008/2010 to logical graphic
  2001/2003/2005/2007/2009/2011 and sound bank 1/2/3/4/5/6;
- installs each recovered thread permutation at tick zero behind the covered
  page, opens onto the cleared black target, and performs the first dispatch
  only on the next post-open 10 Hz callback;
- advances cartoon waits and movements at the live-measured 10 Hz callback
  clock, independently of the
  `1,193,182 / (0x0555 * 0x1e)` gameplay/job scheduler, and does not charge
  pre-transition frame time to a newly installed scene;
- retains the original dirty framebuffer: opaque records include black,
  opcode `0E` leaves pixels without a logical baked actor, and later dirty
  rectangles clear to black and redraw only still-active actors in recovered
  order;
- adds the recovered `+3` callback bias and decodes the active scene's ADLI or
  PSND bank with the Word-specific resident sound profile; and
- performs cartoon teardown/cursor advance before generating the next board.

The common Word input dispatcher reads selector-100 state 6 at
`0xDD23`-`0xDD31`. Its Escape branch acts only in states 4/5, while ordinary
forwarded keys reach the unconditional state-6 teardown at `0x12B3B`.
Accordingly native no longer opens quit confirmation on cartoon Escape:
Escape and bare modifiers are ignored, while arrows, Enter, Space, and
printable keys skip. The Win32 printable path consumes the following
`WM_CHAR` before board initialization can interpret it as movement.
The earlier `+`/`=`/`-`/`_` branches still rejoin that state-6 switch, so an
enabled repeat-control key first adjusts Word's 1..10 joystick-repeat word and
then uses the same event to tear down the cartoon. The native presenter and
headless gate now preserve both ordered effects. Successful adjustments also
invoke Word's recovered direct-PC-speaker helper at `0xDFB1`: requested pitch
is `1000 - 50 * repeat` Hz for 50 ms, independently of the selected gameplay
sound device. Scene teardown releases the prior effect owner before native
submits that exact PIT-quantized feedback waveform, so the skip cannot silence
it immediately.
The adjacent state-4/5 Enter path also has no player-actor-state test. Word now
permits Time out during a walk, chew, or recovery and freezes the outstanding
animation timer until resume; it no longer advances a hidden move or chew
behind the pause overlay.

BTMP is also authoritative about the source PCXF for every frame. Word
animations 2003 and 2005 move six late frames to continuation sheets 3003 and
3005. The native Word draw helper formerly ignored that field and cropped the
base sheet. It now follows `SpriteFrame::sheetId`; a headless pixel-for-pixel
gate covers all six continuation records. See `WORD-SPRITE-STATIC-AUDIT.md`.

`munchers_app_headless_test` hashes every internal 320×200 retained framebuffer
of all six scripts: ticks 1–101/141/132/105/109/250 must produce the exact six
timeline hashes recorded in `WORD-SCENE-STATIC-AUDIT.md`. The separate lossless
live gate matches all 586 DOS-presented distinct stable state runs exactly; the
last internal run in each scene already carries signal 99 and is torn down by
the original before presentation. The test then
completes real level-3 boards through pointer walking and the exact chew path
across enough deterministic seeds to host all five ordinary cartoons, and
separately completes level 18 to host scene 5 through PSND. All six must emit at
least one decoded callback to the test audio owner, terminate after exactly
`102,142,133,106,110,251` ticks, clear the pending cartoon, and advance one
level. The audio owner is compiled in device-free test mode, so this gate opens
no sound device or visible window.

## Shared launcher and return lifecycle

`MunchersApp` is now the sole owner used by `wWinMain`. Startup displays one
self-contained collection menu with Number Munchers, Word Munchers, and Exit.
It creates only the selected native controller. A terminal Quit/Escape request
from either game's own main menu is consumed by the owner and reconstructs the
collection menu; it does not destroy the process. Only a collection-menu exit
ends the application.

`munchers_app_headless_test` proves both title-return paths and both complete
confirmed in-game quit paths. Number remains inside its post-game Hall and
replay-question lifecycle until the title requests its terminal exit; Word's
confirmed quit first returns to its title. In each case the shell consumes that
terminal request, restores the corresponding launcher selection, and leaves
the process-level quit flag clear. A separate check proves final launcher Exit.
The test also covers Word title mouse/keyboard routes, movement, chew timing,
level selection, all six scene host lifecycles, both scene sound-resource
routes, and exact CGA four-color compositions without opening a window. The
one-window ownership and hotkey boundary are recorded in
`SHARED-LAUNCHER-STATIC-AUDIT.md`.

## Troggle and safe-zone runtime

The presenter now hosts the statically recovered Word Troggle subsystem rather
than leaving the shared engine disconnected. Board start installs safe-zone
jobs, chooses the player from the twelve interior cells in the original
column-then-row PRNG order and clears that cell, then installs up to three
persistent Troggle slots. The exact twelve-tier count, arrival, speed,
five-species weight, maximum-safe-job, and initially-active-safe tables are
native regression gates. Warning/entry/exit, steering, safe-cell avoidance,
cell trails, overlap/cannibal biting, player collision, the six-record
21-tick eating sequence, deferred reserve loss, and post-collision input lock
are also hosted and headless-tested. See `WORD-TROGGLE-STATIC-AUDIT.md` for
addresses, table values, and the exact evidence boundary.

The Word-specific CGA executable table and full startup/title/Options/Hall/board frame
gates are recorded separately in `WORD-CGA-STATIC-AUDIT.md`.

## Feedback and post-game runtime

Wrong-word and Troggle feedback now use the recovered one-row saved rectangle
and y=88/98/108 user baselines and freeze gameplay jobs during the blocking
wait. Enter, Space, or left release resumes, while right release is inert;
Escape opens the shared default-No quit prompt. Prompt No
or Escape restores the saved strip and then applies the normal reserve route,
while accepted Yes invokes the scored terminal and Hall admission. A fourth
loss still routes through Hall admission. Qualified scores use the original
25-character name editor contract and three rank/perfect-score messages; the
post-game Hall then reaches the default-Yes `Do you want to play again?`
prompt. A one-million score enters its special name prompt directly rather
than passing through a fabricated extra feedback page. See
`WORD-FEEDBACK-TERMINAL-STATIC-AUDIT.md` for the call trace and remaining live
comparison boundary.

## Information and pre-game instructions

Both original `DATA:1` page groups are now presented directly from the
embedded archive: six main-menu Information pages and five pre-game
character/control pages. The exact 17-by-40-byte text-record layout, all
Muncher/Troggle placements, page counts, Enter/Space pager, Escape route, and
completion destinations are native headless gates. The former fabricated
single-page summary has been removed. See `WORD-INFORMATION-STATIC-AUDIT.md`
for the payload format, executable addresses, page inventory, and twelve
complete live/native full-frame hashes. That comparison corrected the five
Troggle title-palette highlights and record-15/Helper-record-13-alias pose.

## Options and content editing

The fabricated Word Options placeholder has been replaced by the recovered
five-entry menu and functional Set Content, eight-choice difficulty, fourteen-
choice/twenty-sound vowel editor, word preview, transactional Hall eraser, and
password editor/gate routes. Accepted content immediately reconfigures the
native board generator; cancelled difficulty, vowel, Hall, and password edits
leave live state unchanged. Wrong passwords now reach the recovered
acknowledgement modal and matching is case-insensitive. See
`WORD-OPTIONS-STATIC-AUDIT.md` for executable addresses, input contracts,
corrected fixed-menu pointer/layout records, nineteen live original/native
frame matches, additional native fixtures, and the remaining presentation
boundaries.
Word's own `0x123eb` calibration path is also integrated: four-sample
detection, center/extreme midpoint thresholds, transactional Escape, enabled/
calibrated gating, single-axis directions, button priority/edges, and the
default-four mutable 1–10-tick repeat all have native tests. See
`WORD-JOYSTICK-STATIC-AUDIT.md`.

## Native persistence

Word configuration, Hall, password, audio flags, and joystick thresholds now
survive shared-launcher destruction/recreation through a validated per-user
file that never modifies the preserved `WM.CFG`. See
`WORD-PERSISTENCE-AUDIT.md` for the format, save points, validation, and
temporary-file headless test.

## Gameplay effects audio

The presenter now routes ordinary Word effects through exact embedded
`GSND:2` bytecode. Static call sites lock Troggle entry, Time Out activation/
expiry, correct/wrong chew, warning, collision start and tick-21 terminal,
destructive Troggle retirement, the first two pre-game Information pages, and
board completion. Both the Word-resident AdLib patch path and recovered
PC-speaker path are exercised without an audio device. The two original
AdLib gameplay and cartoon paths now retain one continuously clocked YM3812
across events instead of submitting reset-per-cue RIFF buffers. The two original
Alt+M handlers and dispatcher now also lock an easily missed lifecycle detail:
turning music off stops both the score and any cue currently sounding, then
leaves ordinary effects enabled; turning music on during Demo restarts only
the score. See
`WORD-GAMEPLAY-AUDIO-STATIC-AUDIT.md` for the cue map and physical-output
boundary.

## Attract/demo runtime

After the recovered 30-second title idle, the presenter now enters Word's
autonomous demo instead of remaining indefinitely on the menu. It starts the
stream-16 score, selects a random visible level 1-9, installs the
safe/Troggle jobs followed by the exact selector-5 autonomous-player callback,
and renders the original demo-only HUD. The callback preserves the executable's
PRNG order, positive-cell munch, one-in-ten negative-cell munch, and clockwise
direction search.

The first wrong word or completed Troggle bite terminates the demo even though
reserves remain. Feedback lasts 150 public scheduler ticks, then the common
PCX driver retains the restored board for 39 measured shared-driver intervals
and closes it through a seven-interval clean or six-interval collision Wipe.
The demo Hall and PCXF `6005` logo each retain their literal 450-tick records,
separated by a six-interval Wipe; the next live Word board is painted within a
nine-interval close/paint/open Wipe. A completed board follows the same route
without ordinary stream 15, the feedback hold, or the ordinary five-draw
cartoon shuffle. Any key or pointer button returns to the title and stops the
score. The two-section original score
decodes to an exact 224,919 ms loop. Its channels 1-8 and replaceable channel-0
gameplay effects now run through one continuous YM3812 while selectable
PC-speaker effects remain a separate device path. See
`WORD-ATTRACT-STATIC-AUDIT.md` for controller addresses, record bytes, shared
Wipe evidence, exact audio conductor, headless gates, and the live-comparison
boundary. `WORD-PRNG-STATIC-AUDIT.md` records the executable-wide random-call
inventory and the corrected zero-draw Demo completion path.

The same effect is also the ordinary user-board presenter, not an attract-only
special case. Static xrefs from `0x0957A` and high-level action 5 reach
`0x0A356 -> 0x10A66 -> 0x108C2`; native therefore applies the measured
nine-interval close/clear/open/dynamic-paint lifecycle to title/Information
starts, Replay Yes, direct board advances, and post-cartoon advances. See
`BOARD-PRESENTER-STATIC-AUDIT.md` for the corrected `0x0FE2D` attribution and
the fixed-seed user-transition hashes.

That corrected `0x0FE2D` branch is now implemented as its own synchronous
board-to-cartoon presenter. Static disassembly proves type-2 close, selected
scene PCXF/SCPT initialization, target clear, type-1 open, a literal 10 ms
delay, and only then return to ordinary scheduling. The six lossless scene
captures independently measure the same branch: their bank-specific covered
cyan holds are 41–44 samples (0.585–0.628 s), their cleared-black holds are
6–8 samples (0.086–0.114 s), and all first stable scene ticks are exact. Native
snapshots the completed Word board, freezes all user/common-dispatch input,
opens upward from the bottom onto black, and leaves the VM at tick zero until
the first post-open 10 Hz callback. Headless gates lock the five completed
states, the six timers, 99/100 ms callback boundary, and fractional final-chew
handoff. DOS's torn close/open/initial-paint refreshes remain classified as
single-buffer artifacts and are not presented by native; see
`analysis/word-live/scenes/transition-report.json` and
`CARTOON-PRESENTER-STATIC-AUDIT.md`.

## Remaining parity boundary

This is an integrated Word gameplay/presentation slice, not a 1:1 Word port
claim. Information and pre-game instruction content/routing now have complete
live VGA comparison plus static/headless input gates. Options routing and the
functional content, Hall, and password transactions are native gates. Muted
physical-window comparisons also cover Options, Set Content, Difficulty, all
four Vowels states, F1 Help, two Preview densities, the supplied Hall eraser,
the title-browse Hall, Set Password, the password gate/rejection, joystick
attach, three validation dialogs, the insufficient-target warning, an initially
empty Hall, and a forced configuration-write alert. The second batch exposed
and corrected exact visible-state validation, modal/footer geometry, password
field/hint placement, Hall baselines, calibration footer, and the write-alert
saved-screen border/text. The captured terminal path covers wrong-word and
collision feedback, blank and typed name entry with the 16-tick caret blink,
an admitted highlighted Hall, replay Yes/No, and return to title. Password
pages, the attach calibration page, warning/validation, initially empty Hall,
and the persistence-failure alert now have exact live comparisons. The five detected calibration
steps and ready page, admission-message/rank variants, no-more-entries Hall,
and exhaustive parameterized editor/pointer behavior remain static/headless
rather than live-comparison gates. Attract mode is closed at
the static/controller/headless-renderer level and now has a short organic live
Demo oracle: native matches the entire stable 320x200 framebuffer with zero
pixel differences, including the BTMP 6001 bell target exemplar, centered Demo
footer, and target-sound breve. Title-to-Demo entry, autonomous movement/chew/feedback,
terminal Wipes, and the long-run host/audio cadence remain open. The shared index-111 audit now uses
the independently measured `414100` first-board value in both games; the old
native-only `413D00` split is rejected. Its logical Hall/logo/exit latch and
post-game Yes versus normal title-start paths remain covered even though that
sprite slot is pixel-neutral across them.
Physical joystick validation,
physical gameplay/scene/attract-audio validation, and other uncaptured original
full-frame paths remain open. The captured level-1 board/HUD/four-direction
movement/chew slice is live-verified. Reggie's ordinary movement and complete
21-tick collision bite also match live lossless evidence tick-for-tick; other
generated boards, species/levels, enemy overlap cases, and transitions remain
open. Troggle/safe-zone lifecycle outside that captured Reggie slice remains a
static/headless native gate. Scripted-scene pixels, retained painter, 10 Hz
cadence, and the close/load/open stable-state boundary now have lossless live
evidence; callback audio-resource routing and teardown remain executable-backed,
while synchronized audible/physical output is still open.
