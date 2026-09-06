# Word Munchers sprite-source and Muncher-frame static audit

This audit reads the preserved Word and Number resource archives, their
extracted lossless records, the preserved Word load image/disassembly, the
native in-memory renderer, and the muted live Word movement/chew fixture. Its
repeatable checks do not launch DOSBox, either executable, the GUI, or audio.

## Muncher artwork and eating records

Word and Number Munchers carry byte-identical VGA player resources:

| Record | SHA-256 in both games |
|---|---|
| `PCXF 1006` | `A94DD5339AEB7F97753317639F82E43852CDCBBB11629D7DB7E3EBFE11451D39` |
| `BTMP 1006` | `629D30338D4C6024DF8685C7CA9D099EC49ACAB7148063A44E2324B814D04A7D` |

The four archived eating records are:

| Frame | Source sheet | Inclusive source rectangle `(left,top)..(right,bottom)` |
|---:|---:|---|
| 12 | 1006 | `(132,137)..(176,166)` |
| 13 | 1006 | `(108,17)..(152,46)` |
| 14 | 1006 | `(132,137)..(176,166)` |
| 15 | 1006 | `(108,17)..(152,46)` |

Thus the original table intentionally represents two visible poses through
four record numbers. Static recovery of Word's shared animator and chew
dispatcher proves the seven visible one-tick records
`12,13,14,13,12,13,14`; its tick-7 terminal callback reaches record 13 and
resolves immediately. Record 15 is not selected. The duplicate 12/14 and
13/15 crops explain why the former linear order looked correct in framebuffer
hashes. See `PLAYER-CHEW-STATIC-AUDIT.md`.

An independent decode of frame 12 finds the dominant nonblack source color
`00E400` (180 pixels), followed by the four darker green ramp entries and
white. These bytes are identical to the Number resource whose active-board
yellow palette was measured from the supplied DOS reference. Word uses the
same actor resource/table and common actor presentation path. The native Word
gate requires mapped `E7DB00`, rejects an unremapped `00E400`, and matches the
two stable live first-board chew cell hashes `0x1c94da58fe5737fd` and
`0x3fb6cdbad7ba0655` in their seven-interval order. See
`analysis/word-live/gameplay/report.json`.

## Continuation-sheet defect

Each BTMP record owns a 32-bit source-PCXF ID; the logical animation ID is not
always that source ID. The Word archive contains six continuation records:

| Logical animation | Frames | Referenced PCXF | Rectangles |
|---:|---|---:|---|
| 2003 | 13, 14, 15, 16 | 3003 | `(232,2)..(319,50)`, `(232,53)..(319,101)`, `(0,4)..(56,78)`, `(36,4)..(228,78)` |
| 2005 | 20, 21 | 3005 | `(1,0)..(149,160)`, `(172,0)..(319,160)` |

The asset bridge already preserved these source IDs, but
`WordGame::drawSprite` formerly loaded the logical 2003/2005 sheet and ignored
the frame's 3003/3005 reference. That selected unrelated pixels and clipped
some late cartoon frames. The renderer now resolves the frame first and loads
`SpriteFrame::sheetId`, matching Number's established continuation behavior.

The headless app test composes all six records through the private Word draw
path and compares every 320x200 output pixel with a direct crop of the exact
referenced continuation sheet. The independent SCPT timeline test also requires
all four 2003 continuation frames and both 2005 continuation frames to become
visible during their original scripts, proving these are exercised artwork
rather than unused table records. Together with the all-scene lifecycle gate
and exact seven-tick chew-dispatch gate, this prevents both source-sheet and
player-eating regressions without opening a window. The chew gate now also
asserts all four record-12–15 source rectangles, including the intentional
12/14 and 13/15 aliases, before the runtime animation is exercised.
