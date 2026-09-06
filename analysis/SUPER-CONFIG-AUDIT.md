# Super Munchers configuration audit

Date: 2026-08-28

The distributed `SM.CFG` is exactly 2,292 (`0x8F4`) bytes and is embedded in
the native executable. `SuperConfig` retains the complete byte image and
parses it without using the DOS executable at runtime.

## Header and options

| Offset | Size | Meaning | Distributed value |
|---:|---:|---|---:|
| `00` | 4 | signature | `MECC` |
| `04` | 2 | version, accepted 0 or 1 | 1 |
| `06` | 32 | NUL-terminated application | `Super Munchers` |
| `26` | 2 | content quick set, 0..5 | 4 (Ultimate) |
| `28` | 2 | difficulty, 0..3 | 1 (Advanced) |
| `2A` | 1 | six MSB-first topic bits | `FF` |
| `2B` | 30 | six groups of five MSB-first rule bits | all `FF` |
| `49` | 1 | enable negated rules | 0 |
| `4A` | 1 | Answer Vision option | 1 |
| `4C` | 1 | joystick flags | 0 |
| `4D` | 59 | `String[58]` hint | empty, stale payload retained |
| `80` | 8 | four overlaid joystick calibration words | `10 20 30 40` |
| `88` | 11 | `String[10]` password | empty, stale payload retained |
| `94` | 2 | retained saved-state word | 1 |

The loader at `0x0CFFF` has a source loop-bound quirk: it visits ten bytes per
five-byte rule row and therefore recopies overlapping memory through offset
`+4D`. The live rule array is still exactly the first 30 bytes; the saver at
`0x0D164` correctly writes six groups of five. The native parser preserves the
source bytes but exposes only those 30 rule bytes.

Difficulty values 0, 1, and 2 map to board thresholds 10, 20, and 30. Value 3
is Player Chooses and is resolved by the per-game choice before constructing a
board. Topic and rule masks use the common MSB-first bit helper at image
`0x17794`.

## Seven Hall tables

Offset `+96` begins seven consecutive tables, not five. Their count follows
the six category games plus Challenge. Each table is `0x132` (306) bytes:

```text
uint16 active_count
uint16 capacity       // exactly 10
uint16 name_limit     // exactly 25
record slot[10] {
    char name[26]     // NUL-terminated
    uint32 score
}
```

The tables begin at `96`, `1C8`, `2FA`, `42C`, `55E`, `690`, and `7C2` and
end exactly at file offset `8F4`. The distributed second table has one entry,
`Ken Carlo`, score 510; the other six are empty. Image `0x0CF8B/0x0D0EF`
copies one table by `index * 0x132`, and initialization at `0x0D3C0` explicitly
iterates indexes 1 through 6, confirming the seven-table extent.

## Permanent gates

The headless test locks every supplied option, all 240 rule bits, calibration,
both empty ShortStrings versus their stale payload, all seven Hall headers and
the active entry, and an untouched byte-exact round trip. It separately
rejects malformed signature, version, size, ShortString bounds, content
selectors, Hall headers, and unterminated names. The parser exposes a validated
`SuperBoardSettings` bridge, including Player Chooses resolution.
