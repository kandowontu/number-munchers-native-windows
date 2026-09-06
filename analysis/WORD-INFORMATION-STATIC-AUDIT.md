# Word Munchers Information/instruction static audit

This gate combines the preserved `DATA:1` payload, the unpacked Word
executable, the embedded resource loader, the in-memory renderer, and a muted
live pass through the original's instruction question and all eleven pages.
Raw DOSBox-X screenshots are preserved under
`analysis/word-live/information` and independently hashed by
`tools/audit_word_startup_capture.py`.

## Archived page format

`WM.RES` `DATA:1` is 7,750 bytes. Its little-endian header declares two page
groups and points to offsets `0x0006` and `0x1086`. The first group contains
six main-menu Information pages; the second contains five pages shown after a
Yes answer to `Do you want instructions?`.

Every page is exactly `0x2c0` bytes:

- `0x000..0x2a7`: seventeen NUL-padded 40-byte text records;
- `0x2a8..0x2bf`: six signed `(y,x)` placement pairs; and
- `(-1,-1)`: no graphic in that indexed placement slot.

The native presenter reads this embedded payload at runtime instead of
maintaining a second, hand-transcribed copy. It draws all seventeen records at
`x=12`, with baselines `y=15 + 10 * record`, and the continuation string at
`(50,191)`. Its border uses the recovered magenta `x=0..2/317..319` and
`y=7..9/186..188` bands.

## Graphics and groups

The six graphic slots retain the executable's character ordering. The live
pages correct the prior blanket frame-7 assumption: the Muncher uses record 7,
while each Troggle uses record 15. Four Troggle record-15 rectangles alias
record 7, but Helper record 15 aliases record 13; that hidden descriptor
difference changes its visible pose.

| Slot | Sheet | Character |
|---:|---:|---|
| 0 | 1006 | Word Muncher |
| 1 | 1007 | Reggie |
| 2 | 1008 | Worker |
| 3 | 1009 | Bashful |
| 4 | 1010 | Helper |
| 5 | 1011 | Smarty |

The main Information group has no graphics except zero-based page 1: Muncher
`(120,48)` and Reggie `(120,212)`. The pre-game group places the Muncher at
`(80,136)` on `Meet the Characters`, then places all five Troggles on `The
Troggles`: Reggie `(32,40)`, Worker `(112,212)`, Bashful `(32,212)`, Helper
`(112,40)`, and Smarty `(72,128)`. As in the recovered Number presenter, the
sprite painter applies the character image origin of `(+4,+1)`.

These pages inherit the title palette rather than loading a new full-screen
PCX palette. Five highlight source colors therefore differ from a sprite's
own decoded PCX palette: Reggie `FC2020 -> FF5D5D`, Bashful
`20A8FC -> 5DBEFF`, Worker `B420FC -> CB5DFF`, Helper
`B4FC20 -> D3FF5D`, and Smarty `FCF420 -> FFFB5D`. The first live comparison
exposed both this carry-over and Helper's frame alias; native now applies them
only to Information Troggles.

The six Information pages contain the original overview, scoring/loss,
keyboard/joystick/mouse, safe-zone/audio, Options, and nine-person software
team copy. The five pre-game pages contain `Meet the Characters`, the five
Troggle species, Time Out/safe zones, movement devices, and special keys.
Every line is tested directly against the archive layout, including selected
verbatim records and all non-empty placement pairs.

## Executable routing

The main-menu Information caller at image `0x0f641` and the pre-game Yes
caller at `0x121a1` both reach the `DATA` presenter at `1191:00b0` (image
`0x119c0`). The presenter iterates six pages for group 0 and five for group 1,
processes the six placement records beginning at page offset `0x2a8`, and
draws/waits on the continuation footer at image `0x11b1d`–`0x11b2a`.

Native input now follows the shared pager contract: Enter or Space advances,
unrelated keys do nothing, and Escape cancels either group to the Word title.
Completing group 0 returns to the title; completing group 1 starts level 1.
Space also accepts the instruction question's selected default No, matching
the shared choice widget.

The five-page pre-game presenter also owns two literal gameplay-bank cues:
stream 13 on its first page (`0x11ABA`) and stream 14 on its second
(`0x11ACD`). Native dispatches those at group entry and the first pager
advance; decoding and device-free routing are locked in
`WORD-GAMEPLAY-AUDIO-STATIC-AUDIT.md`.

## Live/native regression gate

`munchers_app_headless_test` validates the payload size/header, representative
lossless text records, every graphic family, non-empty placement records,
input routing, the complete question, and all eleven composed VGA pages. The
question's live/native FNV-64 is `070b08aa80400c33`; page hashes are:

| Group | Page hashes |
|---|---|
| Information | `19132d0ad87ad94f`, `1fea2ed3cff0adf0`, `f7fe41a965eb0a00`, `980eafe6efbce961`, `bd3ec3a29d500521`, `061d6e48d17e1f79` |
| Pre-game instructions | `0c5ee9e438c025a9`, `fcfe7362eb6ea8be`, `d48c0f88e905a11a`, `7ad219a332ba7fcc`, `0239185c800a79dd` |

Every value now matches the corresponding complete live original frame. The
capture route also exercised title selection, the default-No question, Yes
selection, six-page main return, and five-page pre-game progression. CGA
composition remains covered by its separate static/headless gate rather than
this VGA capture.
