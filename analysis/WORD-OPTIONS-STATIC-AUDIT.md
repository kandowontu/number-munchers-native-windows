# Word Munchers Options static and live audit

The control-flow audit was produced from `WM-unpacked-image.bin`, the full
static disassembly, embedded `WM.CFG`/`WLIST.BIN`, and the in-memory native
renderer. A later muted, DPI-aware DOSBox-X run added exact physical-window
captures for Options, Set Content, Difficulty, Vowels, Vowel Help, two Preview
states, the transactional Hall eraser, and the supplied Hall. The repeatable
crop/scale/hash gate is `tools/audit_word_options_capture.py`; its valid report
is `analysis/word-live/options/report.json`.

A second muted physical-window run records Set Password, the password gate and
rejection, the disconnected-joystick prompt, all three vowel-validation
messages, the insufficient-target warning, an initially empty Hall, and the
configuration-write alert. `tools/audit_word_options_gap_capture.py` verifies
the 658x553 window geometry, uniform 2x reconstruction, and all 64,000 logical
pixels for every page. Its valid zero-mismatch report is
`analysis/word-live/options-gaps/report.json`.

## Top-level Options menu

The Word Options routine begins at image `0x0fa79`. Its dispatch at
`0x0fc17` exposes five visible entries in this order:

1. `Set Content`
2. `Erase Hall of Fame`
3. `Set Password`
4. `Turn Joystick ON/OFF`
5. `Calibrate Joystick`

`DS:1E8A` contains seven far pointers: an empty index-0 sentinel, the five
strings at `DS:1EE0..1F26`, and an empty result-6 sentinel at `DS:1F3B`.
The branch at `0x0FBFB` explicitly assigns result 6 when the widget returns
Escape. Therefore result 6 means the Escape destination; it is not a visible
`Main Menu` row. The prior native page incorrectly displayed that destination
as a sixth numbered choice.

The recovered layout record at `DS:1EA6` is `(baseline 80,left 50,0,0)`.
The page uses the `Options` heading, cyan double rules, `Choose an option:` at
`(5,32)`, and the shared footer at baselines 167 and 176. Up/Down wrap through
the five visible rows, digits select their numbered row without activating it,
Enter dispatches, and Escape returns to the Word title.
The native runtime hosts Set Content, Hall erasure, password editing/gating,
the joystick-enabled toggle, and calibration. The Word-specific calibration
trace and headless input gates are detailed in
`WORD-JOYSTICK-STATIC-AUDIT.md`.

## Fixed-menu pointer routing

Word calls the common pointer-aware numbered-list widget at image `0x0ECB5`.
As in the independently recovered Number build, the event mask excludes mouse
motion. An inclusive left release on a different item changes only the
selection; a second release on that selected item becomes Enter. Right release
activates the current item without hit-testing, and Space remains inert.

The fixed-width Word font and padded source strings produce these exact
inclusive Options rectangles:

| Entry | Rectangle `(left,top)..(right,bottom)` |
|---|---|
| Set Content | `(50,79)..(178,88)` |
| Erase Hall of Fame | `(50,90)..(234,99)` |
| Set Password | `(50,101)..(186,110)` |
| Turn Joystick ON/OFF | `(50,112)..(226,121)` |
| Calibrate Joystick | `(50,123)..(234,132)` |

The `ON` source retains two trailing spaces while `OFF` retains one, keeping
the dynamic row width stable. There is no hit rectangle at the old fabricated
sixth-row position.

## Set Content and difficulty

The Set Content caller is `1357:002c` and the routine begins at image
`0x1359c`. It displays these live summary fields:

- `Current Difficulty:`
- `Vowel sounds selected:`
- `Target words available:`

The three selectable rows are `Select Word Difficulty`, `Select Vowel Sounds`,
and `Preview Words`. The switch branches at `0x137c2`, `0x137eb`, and
`0x13801`; the hidden Escape/commit result is at `0x1382d`. Native commit
reconfigures the live board generator only when the selected sounds provide at
least one target word.

The local changed byte is `[bp-0x8F]`. Accepting a different difficulty sets
it at `0x137E3`; a successful vowel-editor Enter returns `AL=1` at `0x1472A`
even if the accepted bit pattern equals its entry snapshot, and the parent sets
the same byte at `0x137F9`. Escape with the byte still zero branches directly
to the Set Content exit at `0x13878`, without rebuilding or saving content.
Escape after an accepted edit calls the target-count helper at `0x13834`. A
zero result, or the following configuration-build failure, branches to
`0x13865` and displays this exact padded source text from `DS:2836`:

```text

The current content settings provide  
insufficient target words for the     
Muncher.                              
You must select a different difficulty
level or select from available vowel  
sounds.                               

```

The call at `0x1386E` passes flag 1 to the generic modal. Its `0xC3` display
record counts the leading and trailing newlines as eight logical rows, uses
the nine-pixel `BIT8x8.GFT` line advance, adds ten pixels, and vertically
centers the resulting 83-pixel saved-screen strip at y `58..140`. The strip
spans x `0..319`; its eight-row baseline schedule begins at y=63 and the six
nonempty padded rows paint at `72,81,90,99,108,117`. Flag 1 replaces the page
footer with centered `Press Space Bar to continue.` at y=176. It also selects
the exact
`DS:14D4 = {Escape, Enter, Space, NUL}` waiter: those keys dismiss the warning
and return to the still-invalid Set Content loop, left release becomes Space,
and unsupported keys/right release remain inert. Native now preserves this
dirty transaction, warning text/layout, dismissal loop, and pointer split.
The Preview branch independently calls the same target-count helper at
`0x13801`; its zero branch at `0x1381A` displays that same local message with
flag 1 before any Preview page is entered. Native now uses the shared warning
state there too instead of silently remaining on Set Content.

The difficulty editor begins at image `0x14017`. Its exact choices are:

1. `1st Grade Easy`
2. `1st Grade Advanced`
3. `2nd Grade Easy`
4. `2nd Grade Advanced`
5. `3rd Grade Easy`
6. `3rd Grade Advanced`
7. `4th Grade`
8. `5th Grade and Above`

Up/Down wrap, Enter accepts, and Escape restores the editor's original value.
Space is inert on these fixed menus. Set Content rewrites the widget/hit layout
at `DS:2938` to `(baseline 96,left 50)`; its three pointer rectangles are
`(50,95)..(266,104)`, `(50,106)..(242,115)`, and
`(50,117)..(194,126)`. The difficulty editor's copied layout is rewritten to
`(baseline 64,left 50)` and advances eleven pixels per row through the final
`(50,140)..(242,149)` rectangle. Native headless tests cover the inclusive
edges, one-pixel misses, selection-only first release, and repeated-release
activation for both menus. The live painter's visible text/highlight baselines
are y=104 for Set Content and y=68 for Difficulty; these display insets do not
change the executable's pointer rectangles. The same captures fix Set
Content's summary rows at y=32/41/50, `Choose an option:` at y=86, and
Difficulty's lower divider/footer at y=171/173 and y=176/185.
Their flag-`0x80` digit-selection behavior is separately traced and gated in
`NUMBERED-MENU-STATIC-AUDIT.md`.

The target-count helper at image `0x148e2` sums selected rows from the exact
20-by-8 word table at `DS:26F6`. This is a target-availability table, not a
copy of WLIST's physical cumulative record counts. In particular, several
Group 2/3 sounds retain low-grade WLIST records for distractor use while their
target count is zero. Native Set Content now embeds this exact table and uses
it for the displayed total, commit validation, vowel availability, and Preview
pagination. A headless discriminator locks sound 0/difficulty 4 to 114 (not
WLIST's physical 144) and sound 12 to 0/13 at difficulties 0/1.

## Vowel-sound editor

The editor begins at image `0x14290`. Its fourteen visible choices map to the
twenty configuration sound slots as follows:

| Group | Visible choice | Sound slots |
|---:|---|---|
| 1 | `a, a` | 0, 1 |
| 1 | `e, e` | 2, 3 |
| 1 | `i, i` | 4, 5 |
| 1 | `o, o` | 6, 7 |
| 1 | `u, u` | 8, 9 |
| 1 | `oo, oo` | 10, 11 |
| 2 | `ou/ow` | 12 |
| 2 | `au/aw` | 13 |
| 2 | `oi/oy` | 14 |
| 3 | `ar` | 15 |
| 3 | `air` | 16 |
| 3 | `eer` | 17 |
| 3 | `er/ir/ur` | 18 |
| 3 | `or` | 19 |

The native route implements the recovered three-column arrow graph, Space
toggle, F1 help, F2 select all, F3 deselect all, Enter validation/accept, and
transactional Escape. It also presents the recovered no-selection and
single-selection Group 2/3 validation messages. Those three failure branches
pass flag 1 to the shared modal at `0x146C3`, `0x146F2`, and `0x14720`; the
flag selects the exact `DS:14D4` accepted-key wrapper. Left release therefore
dismisses a validation message as Space, while right release and unsupported
keys are inert. The help routine at image
`0x1499e` supplies the selection rules and F2/F3 legend, then calls the shared
accepted-key waiter at image `0x0bcc7`. Its `DS:14D4` record is exactly
`1B 0D 20 00`, so only Escape, Enter, or Space dismisses help. Native now keeps
letters, arrows, and punctuation inert across both halves of a translated
Win32 key event; an ignored `+`/`-` cannot change joystick repeat behind the
blocking page. The waiter also maps left release to Space and rejects right
release; headless pointer gates lock both halves.

The validator at `0x13D65` compares each visible choice state exactly with 1.
Consequently only selected-and-available choices count; state 3
(selected-but-unavailable) does not. The all-sounds branch, Group 2 choices
6..8, and Group 3 choices 9..13 are checked at `0x146B1`, `0x146D7`, and
`0x14705`. Native now preserves this otherwise non-obvious state distinction.
The no-selection message uses the full-width double-cyan frame y=`76..122`
and visible baselines 90/99. The Group 2 and Group 3 messages use y=`67..131`
and visible baselines 81/90/99/108. All three release widget focus and replace
the ordinary footer with the centered y=176 continuation prompt.

The arrow graph is now exhaustive rather than sampled. The executable keeps a
one-based current item at `0x1452C`/`0x14579`. The ten-byte returned-key table
at `DS:2998` is exactly `FB 20 BB BC BD CB CD 34 36 00`: extended
`0xCB`/`0xCD` and ASCII `4`/`6` therefore bypass generic movement and reach
the same custom Left/Right branches. ASCII `2`/`8` are absent from that table;
the generic widget's comparisons at `0x0F2C4`/`0x0F2D1` consume them through
the same Down/Up paths as extended `0xD0`/`0xC8`. The editor initializes flag
word `[record+0x56]` to `0x0002` and later ORs it to `0x0013`, so its `0x0080`
digit-suppression branches at `0x0F304`/`0x0F33B` are not active.
Home (`0xC7`) and End (`0xCF`) are also absent from `DS:2998`; they remain in
the generic widget and select the one-based first/fourteenth items through
`0x0F36D`/`0x0F378`.

Converted to native zero-based indices, Left maps the fourteen choices to
`9,10,11,12,13,13,0,1,2,6,7,8,8,8`, while Right maps them to
`6,7,8,8,8,8,9,10,11,0,1,2,3,4`. The Group-3 Left branch compares
`current-3` with 9 and selects the lesser value. Native formerly used `max`
instead of `min`, sending four Group-3 rows to the wrong checkbox and leaving
the last two in the same column. Native also formerly discarded all four
numeric navigation aliases and ignored Home/End. A 140-edge headless gate now
locks every Up/Down/Left/Right/Home/End source plus the corresponding
`2`/`4`/`6`/`8` digit source and proves navigation never changes a vowel
selection bit.

The former `*` placeholders are gone. `DS:1D22` supplies the fourteen exact
inclusive 12-by-12 checkbox rectangles and `DS:1CE6` supplies their label
coordinates. State bit 0 is selection and bit 1 is unavailable, preserving all
four documented states: available, unavailable, selected/available, and
selected/unavailable. Unavailable boxes use BGI `LTSLASH_FILL`; the exact
`01 02 04 08 10 20 40 80` driver pattern occurs at `VGA256.BGI:0x6E8` and
`CGA.BGI:0xB68`. Selection calls the `getimage`/`NOT_PUT` helper through
`0x14977`, inverting the inclusive 8-by-8 interior at `(left+2,top+2)`.

The live page places the three group titles at `(26,56)`, `(106,56)`, and
`(196,56)`. Choice
labels begin at x `45/125/215`, with y positions `73,86,99,112,125,138` for
Group 1, `73,86,99` for Group 2, and `73,86,99,112,125` for Group 3. The help
legend's four demonstration boxes are independently locked at
`(2,110)..(12,121)`, `(150,110)..(161,121)`,
`(1,128)..(12,139)`, and `(150,128)..(161,139)`.

The custom event loop handles left press `0xFC` at `0x145D7`, waits for its
release, and reads the hit index produced from the fourteen `DS:1D22`
rectangles. A hit selects that choice and immediately toggles its raw selection
bit at `0x145F8`–`0x14639`; the label text outside the 12×12 box is not a hit
target. The loop has no right-release `0xFD` branch, so a right release is inert
rather than accepting the page. Native boundary tests cover the inclusive
corners, one-pixel and label misses, unavailable selected state 3, motion
inertia, and right-release no-op.

The captured editor additionally recovers two painter details that were not
represented by the raw checkbox table: Group 1 uses the driver's macron/breve
overstrikes, and the focused label backing box extends two pixels left and
three pixels above the ordinary text origin. Native reproduces both and matches
the complete original frame at `0xa57416c9f7e33637`. The captured F1 page fixes
its body at y `33..102`, its compact-font legend at y `114/132/150`, and the
previously omitted y=176 continuation footer; its complete hash is
`0x33598e143043cb06`.

## Preview Words

The Preview routine begins at image `0x13e21`. The exact grid rectangle is
top 59, left 12, bottom 149, right 316. Its per-sound helper switches to
`BIT5X8.GFT`; ten rows and six 48-pixel columns therefore hold 60 words per
page. A live difficulty-1 short-a page displays all 50 available words in five
columns and matches native at `0x0c2b7fb6cac342ba`, rejecting the former
40-word interpretation. The pronunciation label is based at y=45, hides the
slash notation, and retains the original macron/breve plus underline marks.

Record 0 is the exemplar used in that centered `/sound/ as in word` label.
The grid begins at the executable's section offset `+0x0E`, record 1, and
displays the exact `DS:26F6` target count. Native previously repeated record 0
in the grid, omitted the last target, and used 76-pixel columns; all three are
corrected. The accepted-key record at `DS:14D4` is
`1B 0D 20 00`, proving Escape, Enter, and Space. Escape returns to Set Content;
Enter and Space advance. The native gate also locks the 114-word
difficulty-4 route to offsets 0 and 60 and skips selected sounds whose
target-availability count is zero. Preview's use of the same waiter means left
release advances as Space while right release is inert; both are tested.

## Hall eraser

The shared Hall eraser begins at image `0x0aebe`; Word passes its one Hall table.
For a nonempty Hall it draws numbered name rows without scores, starting at
baseline 38 with an 11-pixel step. The number ends at x=64, the name begins at
x=72, and the selected row inverts a fill beginning at x=62. Its footer is
`Use arrows to move, Space Bar to delete.` at y=167, followed by Enter accept
and Escape cancel at y=176/185.

An initially empty Hall displays `There are currently no entries in the` at
y=90 and `Hall of Fame.` at y=99, followed by centered
`Press Space Bar to continue.` at y=176. Its branch calls the exact `DS:14D4`
Escape/Enter/Space wrapper at image `0x0AF80`; left release returns as Space,
while arbitrary keys and right release remain blocked. Deleting the
last draft row instead displays `There are no more entries in the` at y=85 and
`Hall of Fame.` at y=94, with only the Enter/Escape footer. Space is inert in
that state, Enter commits empty, and Escape cancels. Native editing is
transactional and preserves descending score order plus the ten-entry/
25-character bounds. Exact layout and key-state parity are gated, and the
supplied seven-entry live editor matches native at `0xcda041489b7a9b2b`. Native Hall
persistence is closed separately in `WORD-PERSISTENCE-AUDIT.md`. The Win32
input bridge keeps both halves of an unsupported translated digit on the
initially-empty page; an accepted event returns without selecting an Options
row.

The nonempty shared editor calls the pointer-aware widget at `0x0B12F` with
the numbered-name layout `(left 40, first baseline 38)`. Left release `0xFB`
selects a different name; a repeated hit joins Space at `0x0B189` and deletes
that draft entry. Right release `0xFD` joins Enter at `0x0B16F` and commits.
Native tests all three transitions plus the one-pixel left miss.
The widget call pushes flag `0x80` at `0x0B120`, so digits also select ranked
names; `1,0` reaches rank 10 without deleting it.

## Password editor and gate

The setting route is near image `0x0f6a9`; the title Options password gate is
near `0x0f94c`, with case-insensitive comparison at `0x0f8c0`. Native now hosts
the recovered `Set Password`, `Enter a new password:`, `Enter a hint:`,
`Enter the password:`, and wrong-password modal strings. An empty new password
clears both password and hint. A non-empty password advances to the hint field;
accept commits both. Future title Options requests are gated, wrong attempts
require acknowledgement, and mixed-case equivalents succeed. The compare loop
increments its index at `0x0F8FB`, then tests `DS:5D91 + index` at `0x0F907`.
That is the stored byte just compared at `DS:5D92 + old_index`, so the loop
compares the terminating NUL as well as the visible bytes. Matching prefixes
with trailing characters fail; candidate length must be exact.

The field constructor calls at image `0x0F717` and `0x0F774` pass capacities
10 and 50 respectively. The generic editor at image `0x13270` derives its last
accepted character position from that stored capacity minus one. Therefore the
exact limits are ten characters for the password and fifty for the hint. The
configuration mapping agrees: `+0x03D` is the 256-byte hint ShortString copied
to `DS:5B0A`, while `+0x13D` is the 11-byte password ShortString copied to
`DS:5D92`. Native input and persistence now enforce those distinct limits.
The saved-password and challenge records at `DS:1D9A` and `DS:1DB0` point to
`DS:1CDE = "!-~"`, excluding Space. The hint record at `DS:1DC6` points to
`DS:1CE2 = " -~"`, including Space. Their empty extra-exit specification is
`DS:1CDD`.

The shared editor key table makes Backspace and Left delete the trailing byte, Home
clear the field, Escape cancel the editor/gate, and Right/End/Delete/Up/Down
inert. A rejected-password message consumes its acknowledging event before a
fresh attempt. Failure at `0x0FA41` passes flag 1 to the generic modal at
`0x0FA49`; the wrapper at `0x0BCC7` supplies exact table
`DS:14D4 = 1B 0D 20 00` to `0x0BF6A`. Unsupported physical key-down/character
pairs therefore leave the message visible. Escape, Enter, Space, and left
release are consumed acknowledgments; right release remains inert and none
can leak into the retry.

The live gate fixes the challenge field to the 99-by-20 outline at `(110,80)`,
with text at `(115,86)` and the caret at y=91. A nonempty hint is centered in
`BIT5X8.GFT` at y=120. The ordinary page uses its divider at y=171/173 and
footer baselines 176/185. Rejection instead moves the divider to y=162/164,
paints the full-width double-cyan y=`67..131` message, and replaces the normal
footer with the centered y=176 continuation prompt.

The editor's return value has a less visible persistence consequence. Accepting
the password field sets `[bp-0x03]` at image `0x0F7DE`. If the user then presses
Escape in the hint field, `0x0F83A` marks the transaction cancelled and
`0x0F87F..0x0F8A0` restores both stored strings, but the accepted byte remains
set. The Options caller consequently reaches its configuration save at
`0x0FC59`. Escape from the first field never sets the byte and does not save.
Native now preserves that exact split and a filesystem-backed headless test
proves both the absent first-field write and unchanged hint-cancel rewrite.
The same gate now covers the write-failure branch immediately following Options.
`0x0FCE7..0x0FD70` calls the common `0x07E21` saved-screen alert when the
configuration writer returns false. Native detects open/flush failure, retains
the initiating editor beneath the exact five-line `(60,65)..(260,135)` alert,
freezes controller/pointer/shortcut input, consumes one keyboard event, and
continues to the destination page without paired-character leakage.
The common alert includes the original saved-screen white outline and visible
text baselines 74/84/94/119. The live Options-row-4 failure matches native at
all 64,000 pixels.

## Headless regression gate

`munchers_app_headless_test` locks the recovered routes, transactions, content
effects, validation, password comparison, and the following native VGA frame
compositions:

| Page/state | FNV-64 |
|---|---|
| Options | `caf1504d701741dc` |
| Set Content | `2c1f159c04211bbf` |
| Set Content, insufficient targets, live fixture | `607c6b32d1a263c9` |
| Set Content, insufficient targets, alternate static fixture | `1b663d673bf6f73d` |
| Difficulty | `9fceccd8c9179a03` |
| Vowel editor | `a57416c9f7e33637` |
| Vowel help | `33598e143043cb06` |
| Vowel no-selection validation | `1853b48ff3866660` |
| Vowel Group-2 singleton validation | `00476e702721659c` |
| Vowel Group-3 singleton validation | `8d3fc297b93bd8ac` |
| Preview, sound 0 native/static fixture | `d25d03ab58d3a5a9` |
| Preview, supplied sound 2 live state | `b6b873dcd7ab0ec7` |
| Preview, 50-word sound 1 live state | `0c2b7fb6cac342ba` |
| Hall eraser | `cda041489b7a9b2b` |
| Hall eraser, no entries left | `4159caa222636bbf` |
| Hall eraser, initially empty | `0bfc01284bfe5e29` |
| Set Password | `e0d28b7d3e121749` |
| Password gate, live `test`/`hint` fixture | `f77f4595e849048f` |
| Password gate, alternate static `h` fixture | `4626c6b56719b3e1` |
| Rejected-password modal | `00694bb174f63f0c` |
| Joystick calibration, disconnected/attach | `761c0d49d5d02fa6` |
| Configuration write error, live Options-row-4 fixture | `1a6ebbc842249539` |

The three fixed menus also have their executable layout records, pointer
rectangles, sentinel count, and input behavior gated. Nineteen Options/Hall
states have captured original/native full-frame matches, including every
distinct editor/modal painter, all three validation layouts, the password
transaction/rejection, disconnected calibration, initially empty Hall, and the
saved-screen write alert.

The detected center/left/right/up/down/ready calibration pages reuse that exact
captured calibration frame with only the statically recovered prompt text and
sample indicator changed; native renders and behavior-tests all seven states.
The no-more-entries page is the common Hall painter and is covered in the Hall
row. Other editor values are parameter changes inside the exhaustively gated
140-edge state graph rather than unaudited page types. This closes Options and
persistence. A physical WinMM device run remains solely in the pointer/joystick
row and is not counted twice as an Options visual gap.
