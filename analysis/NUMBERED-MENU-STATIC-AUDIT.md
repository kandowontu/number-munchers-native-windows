# Common-list keyboard static audit

This audit uses `NM-unpacked.ndisasm` and `WM-unpacked.ndisasm`. It does not
execute DOSBox, either original executable, the native GUI, or a visible
window.

## Common widget digit path

The Number generic list widget tests flag bit `0x80` at image `0x0E6DF`.
When the bit is set, it calls the DOS digit predicate at `0x0E6EE` and maps a
valid ASCII digit to a one-based row at `0x0E71E`-`0x0E747`. The byte-isomorphic
Word path is `0x0F389`-`0x0F3F1`.

This path changes the selected row only. The event is still the original digit,
so it redraws the highlight and returns to the widget loop; only Escape or
carriage return terminates at Number `0x0E78C`/`0x0E792` and Word
`0x0F436`/`0x0F43C`. Number keys therefore select but never activate a row.

The special branch at Number `0x0E6FC`-`0x0E71B` (Word
`0x0F3A6`-`0x0F3C5`) provides rows 10-19 without a buffered text field. If row
1 is already selected and `digit + 10` exists, the widget adds ten before its
range check. On Number's eleven-row difficulty list this means `1,0` selects
row 10 and `1,1` selects row 11 when beginning away from row 1. If row 1 is
already selected, `0` or `1` directly selects row 10 or 11. Zero is otherwise
inert, and an out-of-range digit leaves the current selection unchanged.

Because the predicate consumes the translated character rather than a scan
code, shifted punctuation is not a digit. Native implements the route in its
`character` handlers rather than raw virtual-key handlers, preserving that
distinction as well as NumLock-on keypad digits and NumLock-off arrow behavior.

## Common widget directional path

The same fixed-list widget provides more navigation than the formerly native
Up/Down-only handlers. Number's dispatch at `0x0E604`-`0x0E6DD` and Word's
byte-isomorphic dispatch at `0x0F2AE`-`0x0F387` map the canonical DOS extended
keys as follows:

| Key | Number target | Word target | Result |
|---|---:|---:|---|
| Up (`0xC8`) | `0x0E69F` | `0x0F349` | previous row, wrapping |
| Left (`0xCB`) | `0x0E69F` | `0x0F349` | previous row, wrapping |
| Right (`0xCD`) | `0x0E669` | `0x0F313` | next row, wrapping |
| Down (`0xD0`) | `0x0E669` | `0x0F313` | next row, wrapping |
| Home (`0xC7`) | `0x0E6C3` | `0x0F36D` | first row |
| End (`0xCF`) | `0x0E6CE` | `0x0F378` | last row |

The inline six-word jump tables beginning at Number `0x0E64B` and Word
`0x0F2F5` also send the unused extended codes `0xCC` and `0xCE` back to the
loop unchanged. ASCII `2`/`6` and `4`/`8` act like next/previous only when flag
`0x80` is clear; on the numbered lists audited here that flag routes them to
the digit-selection path instead.

The Yes/No wrapper is the important exception. Its custom key list contains
Up, Down, Left, Right, and both cases of Y/N, so those events return to the
wrapper before generic navigation: Up/Down are no-ops, Left/Y select Yes, and
Right/N select No. Home and End are not in that list, so the generic two-row
widget selects Yes and No respectively and remains in its loop. None of these
selection keys accepts a prompt; only Enter or pointer activation does.

## Flag-clear character aliases and callers

The branches feeding next at Number `0x0E657`-`0x0E666` and previous at
`0x0E68E`-`0x0E69D` first inspect flag `0x80`. When that bit is clear, ASCII
`2` and `6` select next while `4` and `8` select previous, with the same wrap
and non-activating behavior as the arrows. When it is set, those characters
continue to the numbered-row path instead. Word's byte-isomorphic branches are
`0x0F301`-`0x0F310` and `0x0F338`-`0x0F347`. Because this is an ASCII path,
native handles it after character translation; shifted punctuation is inert.

The direct pointer-aware-widget calls establish the complete flag-clear scope:

| Game / caller | Widget call | Flag/custom-list evidence | Native selection |
|---|---:|---|---|
| Number Yes/No wrapper | `0x12FC2` | zero pushed at `0x12FB5`; `DS:1664` intercepts arrows and Y/N only | shared Yes/No selection |
| Number operation chooser | `0x147AB` | zero initialized at `0x145D8`; `DS:21AA = {Space, 0xFB, 0}` | four operation rows |
| Number Set Content active row | `0x14BFD` | zero stored at `0x14BC9`; `DS:2218 = {0xFC, F1, Space, Up, Down, 0}` | valid fields in current row |
| Word Yes/No wrapper | `0x12350` | zero pushed at `0x12343`; `DS:2352` is the byte-identical arrow/Y/N list | shared Yes/No selection |

The remaining direct common-list calls carry `0x80` or `0xC0` and are covered
by the numbered caller tables below. The custom lists explain the caller-level
split. Yes/No consumes Up/Down as no-ops but leaves `2/4/6/8` to the generic
two-row widget. Set Content consumes Up/Down in its outer loop to change game
rows, while generic Left/Right, Home/End, and the character aliases move among
the current row's valid fields. The operation chooser leaves all six canonical
navigation keys and the four character aliases to its generic four-row widget.
The Word vowel editor, pagers, and text/numeric editors use separate event
loops and are not changed by this branch.

## Flagged Number callers

| Native page | Original evidence | Rows |
|---|---|---:|
| Muncher Menu | `0x0E821` stores `0x80` | 5 |
| Play/Hall game selector | shared `0x12B6D`; `0x12B8D` stores `0x80` | 1-6 enabled rows |
| Options | `0x0EE29` stores `0x80` | 6 |
| Select Difficulty | `0x1568D` stores `0xC0` | 11 |
| Select Hall of Fame to erase | `0x1580A` stores `0xC0` | 7 |
| Hall name eraser | `0x0A583` pushes `0x80` | 1-10 names |

`0xC0` contains the same numbered-list `0x80` bit plus the caller's painter
flag. The filtered Play selector applies digits to the visible, renumbered
row, after which Enter maps that row back to its enabled game.

## Flagged Word callers

| Native page | Original evidence | Rows |
|---|---|---:|
| Muncher Menu | `0x0F4CB` stores `0x80` | 5 |
| Options | `0x0FAB1` stores `0x80` | 5 |
| Set Content | `0x135E7` stores `0x80` | 3 |
| Select Word Difficulty | `0x14021` stores `0xC0` | 8 |
| Hall name eraser | `0x0B120` pushes `0x80` | 1-10 names |

The vowel editor, Yes/No questions, pagers, and text/numeric editors do not use
this numbered-list route. Word's vowel editor still embeds the flag-clear
generic widget; its complete arrow/Home/End/digit graph is audited separately
in `WORD-OPTIONS-STATIC-AUDIT.md`.

## Native regression gate

`game_render_state_test` covers Number title, filtered Play, Hall, Options,
difficulty, outer Hall erasure, and ten-entry Hall editing. It locks all four
wrapping arrows, Home/End, shifted-punctuation rejection, inert zero, and both
two-keystroke difficulty rows 10/11. It also proves a digit is rejected by the
initially-empty Hall message and cannot reach the outer numbered selector.
Every Number Yes/No wrapper is tested for Up/Down no-op, Left/Right assignment,
Home/End selection, and `2/4/6/8` selection without activation. The same test
also covers Number Set Content's three-field and one-field rows plus the
four-row operation chooser across arrows, Home/End, all four character aliases,
inert punctuation, and non-activation. `munchers_app_headless_test` applies
the same directional and prompt matrix to Word title, Options, Set Content,
difficulty, ten-entry Hall editing, and all three Word Yes/No wrappers, along
with the digit and initially-empty-message boundaries. Ordinary Enter/Space
contracts remain independently gated.
