# Name-entry and replay static audit

The user-game terminal callback begins at image `0x088B2`. With the demo flag
clear it calls the Hall admission routine at `0x0C62E`; an admitted score calls
the name-entry renderer/editor at `0x0C8D3`.

## Name editor

- `0x0C950` passes a maximum length of 25 to the common line editor.
- The value 1 pushed at `0x0C959` is the line-box constructor's bit-0 geometry
  flag, not a minimum length. The constructor at `0x0AAC3` tests that value at
  `0x0AAEA`; it never stores it in the editor record. The actual record at
  `DS:0A4C` contains zero at `+0x06`. Both Enter checks at
  `0x1405C`–`0x14070` and `0x141C8`–`0x141D5` therefore accept an empty field.
- Escape takes the unconditional editor exit at `0x1403E`–`0x14059`, bypassing
  the Enter threshold entirely.
- Backspace (`0x08`) and canonical Left (`0xCB`) share the trailing-character
  deletion branch at `0x14073`–`0x140DE`.
- Canonical Home (`0xC7`) takes `0x140E1`, resets the length to zero, clears the
  field, and redraws it.
- `DS:0A46` is the empty additional-exit specification and `DS:0A47` is the
  legal-character range `" -~"`. The classifier at `0x13C85` consequently
  marks ASCII Space through tilde as `L`; those bytes append at
  `0x1410D`–`0x14193` until the 25-character bound.
- Right, End, Delete, Up, and Down are neither dedicated editor operations nor
  members of either specification, so they return to the event loop without
  changing the buffer.

Word Munchers uses the byte-isomorphic editor at `0x13270`–`0x13571`. Its name
record at `DS:16BC` likewise stores maximum 25, threshold zero, an empty exit
specification at `DS:16B6`, and `DS:16B7 = " -~"`. The corresponding key
branches are Escape `0x133CC`, Enter `0x133EA`/`0x13556`, Backspace/Left
`0x13401`, Home `0x1346F`, and legal append `0x1349B`.

The Number caller handles an empty result from either Enter or Escape. At
`0x0C6ED`–`0x0C702` it tests the returned buffer and copies `DS:0996`,
`The Unknown Muncher`, when it is empty. A partial name remains intact. The
record is then written through the ordinary Hall insertion path at
`0x0C717`–`0x0C73D`. Word performs the same empty-result substitution at
`0x0D0EC`–`0x0D104`.

## Post-game replay question

After name admission (or no admission), `0x088D1` displays the argument-7 Hall
and `0x088DD` calls the replay prompt at `0x0C759`. That routine constructs
`Do you want to play {selected mode} again?`, passes initial selection 1
(Yes) to the shared Y/N widget at `0x0C8A7`, and returns true only for Y at
`0x0C8AF`–`0x0C8CF`. The caller schedules game state 4 for Yes at
`0x088E7`–`0x088F4`; No or Escape schedules Muncher Menu state 7 at
`0x088F9`–`0x08906`.

Native now implements the 25-character/zero-threshold contract, the exact
printable range, Backspace/Left deletion, Home clear, inert unsupported edit
keys, partial-name Escape, exact empty-Enter/empty-Escape fallback, the
post-game Space-bar Hall, and the default-Yes replay question with
selection-only Y/N, Left/Right selection, Enter acceptance, ignored Space, and
Escape routing. Both games exercise these transitions in headless state tests.

## Lossless typed-name frame

`original-name-entry-mm.rgb` is the complete logical 320x200 RGB24 framebuffer
captured with `mm` in the line editor and its 3x9 underscore cursor at x=71.
The unobscured board records are `12-0`, `34-8`, `2x13`, and `1+5`, with the
Not Equal to 16 header and score 85. Reconstructing those visible records in
the native fixture exposed a one-pixel error in the dynamic score value: the
original first glyph begins at x=56, not x=55. After that correction the
complete native frame—including header, visible board portions, modal, typed
text, cursor, and score field—matches FNV-64 `0xbab9ce334800c231`.

The subsequent preserved screenshot,
`original-hall-after-name-entry-external.png`, contains the exact logical Hall
viewport at `(291,231)`. The admitted 25-character name
`vabcdefghijklmnopqrstuvwx` and score 85 are the first exact pair found in the
Inequality list, so the original paints that row opaque white-on-black. The
native admission path now retains the same pair through Hall repainting,
consumes it on Hall departure, and matches the complete extracted frame at
FNV-64 `0xeb488a361b66de0a`.

The preserved 978x687 replay screenshot contains an exact 2x nearest-neighbor
logical viewport at screen origin (291,256). Its losslessly extracted
`original-score-replay-question-logical.png` frame has FNV-64
`0x30b06f51fc871445`. Static image `0x1056C` centers the question within the
288-pixel board span beginning at x=20, rather than across the full screen.
Native now follows that formula and matches the complete 320x200 reference
hash in the headless render test.

The replay prompt is also a palette boundary. Yes schedules high-level action
4 at `0x088E7`-`0x088F4` (Word: `0x094B4`-`0x094C1`). Those actions call the
ordinary game initializers at `0x089AD` and `0x0957A`. Number then reaches its
content/board initializers at `0x0F8A4`/`0x09789` and the primitive presenter
at `0x1175A`/`0x115B6`; Word follows the instruction-equivalent
`0x082BD`/`0x0A356` and `0x10A66`/`0x108C2` chain. None of those paths loads a
PCXF page or writes the DAC palette. Because the Hall and Y/N prompt precede
them without an intervening title-page load, a Yes replay retains the logical
Hall palette state. A normal title-start instead receives the title state.

Both native runtimes now preserve that distinction. Their headless replay
gates compare complete ordinary-start and post-Hall replay frames and require
pixel identity at the live-measured `0x414100` player slot; a subsequent
ordinary title-start must still reset the retained logical state. The former
`0x413D00 -> 0x414100` delta was native-only. BTMP record 17's exact 36x39
reserve crop contains no index-111 pixels in either raw game sheet.
