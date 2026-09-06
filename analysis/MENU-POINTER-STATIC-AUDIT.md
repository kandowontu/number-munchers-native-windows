# Menu pointer widget static audit

This audit uses `NM-unpacked-image.bin`, `NM-unpacked.ndisasm`, and the
original `MECC8X8.GFT` font. It does not execute Number Munchers or DOSBox.

## Shared event behavior

The DOS mouse callback is installed at image `0x0BC46`–`0x0BC6F` with
INT 33h function `0x0C` and callback mask `0x001E`. The mask excludes motion
and includes left press/release plus right press/release. The callback loop at
`0x0B9D5` translates those physical transitions exactly:

| Callback mask | Physical transition | Event type | Event code |
|---:|---|---:|---:|
| `0x02` | left press | 1 | `0xFC` |
| `0x04` | left release | 2 | `0xFB` |
| `0x08` | right press | 12 | `0xFE` |
| `0x10` | right release | 13 | `0xFD` |

The list widget begins at image `0x0E00B`; its event loop begins at
`0x0E091`. The event jump table at `0x0E259` sends left-press event type 1 to
`0x0E273`, left-release event type 2 to `0x0E364`, right-press event type 12
to `0x0E35D`, and right-release event type 13 to `0x0E3EC`.

- Left press checks only the optional up/down scrolling rectangles. It never
  calls the ordinary item hit tester. Moving over a non-scrolling menu item
  therefore does not change the highlighted selection.
- Left release checks the scrolling rectangles and otherwise calls the item hit
  tester at image `0x0DDA2`. That routine walks the generated eight-byte
  rectangles and returns the first containing item, or zero for no hit.
- The left-release branch at `0x0E43D` restores the previous selection for a zero
  hit. A different nonzero item becomes the new highlight and the loop
  continues. If the hit item already equals the highlighted item,
  `0x0E48C`-`0x0E4A9` changes the event to carriage return and activates it.
- Right press returns `0xFE` without activation. The branch at
  `0x0E4BF`–`0x0E4D1` converts right release (`0xFD`) to carriage return, so
  releasing the right button activates the current selection without an item
  hit test.

That `0xFD` conversion belongs to the fixed-list widget, not to every blocking
input loop. The accepted-key waiter at `0x0B389` instead rewrites only event
type 2 (left release) to Space at `0x0B3C1`–`0x0B3C7`. It has no type-13
override, so right release remains `0xFD` and fails the ordinary
`{Escape, Enter, Space}` table. Word's isomorphic instructions are
`0x0BFA2`–`0x0BFA8`. See `ACCEPTED-KEY-POINTER-STATIC-AUDIT.md` for the caller
inventory and paired native gates.

Thus a click on an unselected item selects it, while a click on the already
selected item activates it. A broad hover-select followed by immediate
activation is not original behavior.

The point-in-rectangle routine at image `0x0BBD7` uses inclusive edges. The
layout routine at `0x0D5DA` creates each rectangle from the caller's item
baseline and left coordinate. With the supplied font and the widget's normal
one-pixel text offset, a row whose text baseline is `y` has the inclusive
vertical hit range `y-1 .. y+8`. The item width is the exact rendered string
width, so the right edges are ragged rather than a full-row menu band.

Flag `0x80` also enables the generic widget's built-in keyboard digit path.
ASCII digits select the corresponding numbered row without activating it; a
currently selected row 1 prefixes rows 10-19. Native now preserves this on all
flagged Number and Word callers. The exact branch, two-digit rule, caller map,
and headless gates are in `NUMBERED-MENU-STATIC-AUDIT.md`.

The adjacent generic keyboard branch maps Left/Up to previous, Right/Down to
next, and Home/End to first/last. Previous/next wrap. Native now preserves all
six keys on every fixed numbered-list caller in both games; custom grids and
editors remain governed by their own dispatchers. The exact jump-table targets
and cross-game caller tests are also recorded in the numbered-menu audit.

## Title menu

The title caller is image `0x0E7E3`. It copies the item pointer table from
`DS:0D48`, flags `0x80`, and the layout records from `DS:0DA8`, then calls the
widget at `0x0E929` with initial selection 1. The layout begins at baseline
`y=91`, `x=49`; subsequent rows advance by 11 pixels. Flag `0x80` produces a
three-character right-aligned numeric prefix such as `" 1."`. The source
strings at `DS:0DD9` onward retain their leading and trailing spaces.

Using the exact fixed-width supplied font gives these inclusive rectangles:

| Item | Rectangle `(left, top) .. (right, bottom)` |
|---|---|
| Play Number Munchers | `(49,90) .. (249,99)` |
| Hall of Fame | `(49,101) .. (185,110)` |
| Information | `(49,112) .. (177,121)` |
| Options | `(49,123) .. (145,132)` |
| Quit | `(49,134) .. (121,143)` |

`DS:0D46`, the title widget's custom-key list, is `{0xFF,0}`. The widget's
ordinary terminal test at `0x0E78C`/`0x0E792` accepts Escape or carriage
return; keyboard Space is not a title-menu activation key.

## Game/Hall selectors and Yes/No prompts

The shared Play/Hall selector at image `0x12B6D` uses flags `0x80` and the
layout at `DS:159A`: baseline `y=70`, left `x=90`. When only two entries are
available, `0x12D40` adds 20 to the first baseline; with three or four entries,
`0x12D47` adds 10. Five or six remain at 70. The one-entry branch at
`0x12CFA` selects it immediately without displaying the widget. Its empty
custom-key list means keyboard Enter, not Space, accepts.

The common Yes/No wrapper is image `0x12F6F`. Instructions use the layout at
`DS:158C`; replay uses `DS:09AA`; both place `" Yes "` at `(115,105)` and
`" No "` at `(165,105)`. Their inclusive hit rectangles are therefore
`(115,104)..(155,113)` and `(165,104)..(197,113)`. The wrapper's custom list
is `DS:1664` (`Up`, `Down`, `Left`, `Right`, `y`, `Y`, `n`, `N`, zero), so
Space is not an accept key. The dispatch at `0x12FD2`–`0x1302C` is more
specific than an accelerator shortcut: Up/Down are recognized no-ops,
Left/Right select Yes/No, and Y/N select the corresponding item but remain in
the widget loop. Enter (or activating the selected item with the pointer)
accepts the current selection; Escape cancels. Because Home/End are absent from
the custom list, the generic two-row widget maps them to Yes/No without
accepting. The Left branch at
`0x13014` and Right branch at `0x13021` first compare against the desired
choice and only write when different, so repeated Left remains Yes and repeated
Right remains No. Native formerly toggled five prompt handlers on either arrow;
both games now use the idempotent assignment and headless tests press each
horizontal arrow twice, keep Up/Down inert, and select both sides with
Home/End. The in-game quit layout at
`DS:098A` uses the same x coordinates at baseline 110. The erase-all layout at
`DS:22C4` uses baselines 121 and left coordinates 105/155. Number and Word use
byte-isomorphic copies of this wrapper, so the same keyboard contract applies
to their instruction, replay, quit, and applicable erase confirmations.

## Set Content field grid

The Set Content loop begins at image `0x1489C`. Unlike the fixed menus, it
constructs one generic widget for each of six game rows and retains the six
resulting widget records for an all-row pointer hit test.

- `DS:19A6` contains four `(baseline, left)` pairs per row. Baselines are
  `60, 76, 92, 108, 124, 140`; every row uses left coordinates
  `93, 125, 193, 257` for Use, Range, Sequence, and Other.
- `DS:1B2E` contains the corresponding item pointers. The live buffers are
  regenerated at image `0x15961` with their original padding: `" yes "` /
  `" no "`, an eleven-character right/left-padded range, `" in order "` /
  `"  random  "`, and the padded multiplier or operation-glyph string.
- `DS:1CF6` is the six-by-six one-based validity table. After its zero item,
  valid fields by row are `Use/Range/Sequence/Other`,
  `Use/Range/Sequence`, `Use/Range`, `Use/Range/Sequence/Other`,
  `Use/Range/Sequence/Other`, and `Use`. Challenge therefore has a genuine
  Use field; it is not a permanently injected selector entry.
- Event code `0xFC` is the generic widget's left-press result. The Set Content
  branch at `0x14CFE` then waits for left release and calls the same item hit
  tester for all six retained widgets. A different hit updates row/column
  only. The repeated-hit branch at `0x14D72`–`0x14D9C` follows the
  Space-equivalent field activation path.

The original's Challenge behavior is coupled to this grid. Image `0x1456D`
counts enabled component games (the first five rows). With two or more,
Challenge displays and toggles ordinary Yes/No. With one or zero it displays
`---`; on a valid one-component commit, `0x14C68` clears Challenge's Use byte
so it cannot duplicate the only component. The Play selector at
`0x12BE9`–`0x12CDD` independently walks all six enable bytes, while its
one-entry shortcut still bypasses the selection screen.

Keyboard input exposes the same widget composition. `DS:2218` contains
`{0xFC, F1, Space, Up, Down, 0}`: the outer Set Content loop consumes Up/Down
to change game rows, while the active row's flag-clear generic widget handles
Left/Right with wrapping, Home/End as first/last field, and translated
`4/8`-previous plus `2/6`-next. Invalid trailing fields are not widget items,
so End selects Sequence on Factors, Range on Primes, and Use on Challenge.
The operation editor at `0x145AC` has the still smaller custom list
`DS:21AA = {Space, 0xFB, 0}` and therefore inherits those same generic keys
vertically across all four operation rows. Native headless tests lock each
route as selection-only and keep custom vowel/pager/editor loops separate.

## Multiples Other confirmation

Number's numeric Multiples editor begins at image `0x15228`. A valid number
does not commit directly. Result state 1 clears the lower editor region at
`0x153CB`, draws `New max. multiplier:` at `(5,57)`, and calls `0x1513B` with
baseline 73 to draw the candidate's highest munch-able number and formula at
y=73/82. `DS:29CF` supplies `Press Enter to accept.` / `Escape: Cancel change`.

The confirmation copies `DS:22D0 = 1B 0D FD 00` and calls pAccept at
`0x15441`. Enter commits. Right release retains `0xFD` and commits, while left
release is rewritten to Space and rejected. Escape clears state 1 and loops
back to the state-0 numeric editor at `0x15449`-`0x15465`; only Escape from
that numeric editor closes the subdialog with the installed value unchanged.
Native now retains the candidate separately until this state is accepted,
returns to cleared numeric entry on confirmation Escape, and headless-locks the
confirmation frame at FNV-64 `0xd06b91c92d4be15a` plus the asymmetric pointer
contract. The full numeric transaction is traced separately in
`CONTENT-NUMERIC-EDITOR-STATIC-AUDIT.md`.

## Hall-entry eraser

The outer seven-item `Select Hall of Fame` erasure selector is a deliberate
keyboard exception among the Options fixed lists. Its far routine starts at
image `0x157EE`; after the generic widget returns, the local dispatch at
`0x158EB`-`0x15907` sends both carriage return (`0x0D`) and Space (`0x20`) to
the same activation branch. Selection seven calls the erase-all confirmation;
the other six selections enter the corresponding name-list editor. Native
therefore retains Space activation on this selector even though the main
Options and Select Difficulty fixed lists reject Space. Headless regressions
exercise both the ordinary-Hall and erase-all Space routes.

The editor itself has two distinct empty states. At Number `0x0A3B0` the
entry count is already zero on entry, so the routine paints `There are
currently no entries in the` / `Hall of Fame.` at baselines 81/90, calls the
ordinary `DS:0864` Escape/Enter/Space accepted-key wrapper at `0x0A3E3`, and
exits without constructing the list widget. Left release becomes Space; right
release and arbitrary keys remain blocked. If deletion reaches zero
inside the editor, the branch at `0x0A41E` instead paints `There are no more
entries in the` / `Hall of Fame.` at the captured baselines 90/99 and retains the Enter-commit/Escape-cancel
loop; Space is inert there. Native Number formerly collapsed both cases into
the second state. It now keeps the initial message/footer/routing separate and
rejects a translated digit rather than exposing the numbered outer menu. The
captured deleted-last-entry frame remains pixel-identical, while a separate
static full-frame hash gates the previously uncaptured initially-empty branch.
Word's byte-isomorphic states at `0x0AF4D` and `0x0AFC2` use that distinction;
its initial branch calls the `DS:14D4` wrapper at `0x0AF80`. Both regressions
lock unsupported key pairs plus the left-release/right-release split.

The shared eraser's numbered-name widget uses the same select-versus-activate
left-release rule, but its caller deliberately interprets activation as Space,
not Enter. At `0x0A5B6` the original distinguishes left release `0xFB` from
right release `0xFD`. A left release only reaches deletion when the returned
selection is nonzero and equals the prior selection (`0x0A5D9`–`0x0A5EC`);
the shared deletion branch then removes that draft row. Right release instead
joins Enter at `0x0A5D2` and commits the draft. The native handler previously
sent a repeated left click through Enter, committing without deletion; it now
uses Space and has a regression that separately checks select, repeated-click
delete, and right-release commit.

## Native parity boundary

The native menus now use the recovered generated rectangles, ignore ordinary
hover motion, reject clicks in gaps or beyond a rendered-string right edge,
and preserve select-versus-activate behavior on left release. Win32 maps a
right press through the original `0xFE` high-level route during gameplay,
Demo, and cartoons, then suppresses that physical click's release so it cannot
become a second Enter. Unconsumed fixed-list widgets retain their
right-release-to-Enter rule. Accepted-key waiter pages instead map left release
to Space and reject right release, matching their separate DOS event loop.
Headless tests exercise press-phase Time out/resume and cartoon skip, right-
release menu activation, the motion-independent attract timeout, the exact title,
selector, selection-only Y/N/Enter prompt contract, Options, difficulty,
Hall-erasure, score-entry, and all Set
Content field-grid boundaries, including repeated-click Hall-entry deletion,
Challenge enablement, and the one-component forced-off rule. Joystick calibration is audited separately in
`JOYSTICK-STATIC-AUDIT.md`; its physical WinMM validation remains open.
