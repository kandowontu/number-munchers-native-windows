# Word Munchers configuration static audit

This audit uses the preserved `WM.CFG` and `WM-unpacked-image.bin` only. No
DOSBox, original executable, or native game window was launched.

## File gate and complete layout

Startup allocates exactly `0x27C` bytes at image `0x0C89A`. The load path at
`0x0CED4` requires a file length of exactly 636 bytes. The format checker at
`0x0CE56` compares the first four bytes with `MECC` and accepts configuration
version words 0 or 1. The supplied file is version 1 and has SHA-256
`644424464FB69197C3CFBD4FF60056C28C5B9A617D4A9D3EBCEBCA8AC4D9BD94`.

The application-specific structure is:

| Offset | Bytes | Recovered meaning |
|---:|---:|---|
| `0x000` | 4 | `MECC` signature |
| `0x004` | 2 | little-endian configuration version |
| `0x006` | 32 | NUL-terminated application name (`Word Munchers`) |
| `0x026` | 2 | zero-based word difficulty |
| `0x028` | 20 | raw vowel-sound states |
| `0x03C` | 1 | joystick/runtime flag byte |
| `0x03D` | 256 | Borland ShortString[255] hint: length, then payload |
| `0x13D` | 11 | Borland ShortString[10] password: length, then payload |
| `0x148` | 2 | opaque saved word (supplied value zero) |
| `0x14A` | 306 | one Hall-of-Fame table |

The runtime loader at image `0x0CB72` copies difficulty from `+0x26`, all 20
sound bytes from `+0x28`, the flag byte from `+0x3C`, hint from `+0x3D` into
`DS:5B0A`, password from `+0x13D` into `DS:5D92`, and the opaque word from
`+0x148`. The Set Password path at image `0x0F6A9` independently confirms the
roles: `DS:5D92` initializes `Enter a new password:` and `DS:5B0A` initializes
`Enter a hint:`. The save path at `0x0CC84` writes the same fields back. This
corrects the formerly reversed labels and directly distinguishes Word's one
Hall table from Number Munchers' six mode-specific lists.

## Empty ShortStrings and inactive payload

Both supplied ShortString length bytes are zero, so the configured password
and hint are empty. Bytes after those zero lengths still contain `234567890`
and other stale payload text. The DOS string assignment reads the length-prefixed
value, so those payload bytes are not live strings. The native model retains
them byte-for-byte but never exposes them as a password or hint. This avoids a
plausible but incorrect interpretation of the raw hex dump.

## Content and joystick state

The supplied difficulty is 0. Sound states 2 and 3 are `1`; the other 18
bytes are zero. The post-load validator at image `0x0CF31` rejects any sound
state at least 3. Separate Set Vowel Sounds code distinguishes raw states
0/1/2, so the parser retains them rather than collapsing them to booleans.
Native safety also bounds difficulty to the eight WLIST count columns before
that value can index content.

Flag bit 0 is the persisted joystick-enabled selection. Bit 1 is the runtime
calibrated state: a valid persisted calibration causes the loader to set it,
and the saver explicitly clears it from the stored flag byte. The four words
at `+0x70/+0x72/+0x74/+0x76` are loaded into the vertical-low,
vertical-high, horizontal-high, and horizontal-low thresholds respectively.
The supplied flag is zero, so its near-center threshold words are inactive.

The word at `+0x148` is copied to/from `DS:5D76`, but the complete disassembly
contains no other program reference to that address. Its supplied value is
zero and it remains opaque rather than receiving a guessed meaning.

## Hall table

The 306-byte table is a six-byte header followed by ten 30-byte records:

```text
uint16 active_count
uint16 capacity       // must be 10
uint16 name_limit     // must be 25
record slot[10] {
    char name[26]     // NUL-terminated, room for 25 characters
    uint32 score
}
```

Image `0x0CB4C` copies this exact block, and `0x0CF52` requires active count
at most 10, capacity 10, and name limit 25. The supplied active count is 7:

| Rank | Name | Score |
|---:|---|---:|
| 1 | The Unknown Muncher | 14,660 |
| 2 | jennine oct.1 | 2,440 |
| 3 | jennine oct.1 | 950 |
| 4 | The Unknown Muncher | 835 |
| 5 | jennine | 515 |
| 6 | The Unknown Muncher | 230 |
| 7 | The Unknown Muncher | 50 |

The three inactive slots retain stale names/scores (175, 55, and 55). They are
preserved but excluded by `active_count`, just as stale ShortString payload is
excluded by its length byte.

## Native gate

`src/word_config.cpp` is a lossless, bounds-checked parser. Its headless test
locks every supplied field and active Hall entry, exact untouched 636-byte
round-trip, empty-versus-stale ShortString behavior, and rejection/separation
of malformed signature, version, size, string bounds, difficulty, sound
states, Hall header, and unterminated names. This establishes source state for
Word Options/Hall integration. Native edits now use a separate validated
per-user file and never rewrite `WM.CFG`; see `WORD-PERSISTENCE-AUDIT.md`.
The Set Password transaction also matches the original save boundary: Escape
from its first field does not write, while accepting that field and cancelling
the hint rewrites the unchanged native settings because the original routine
returns an accepted result to its caller (`0x0F7DE`, `0x0F83A`, `0x0FC55`).
Complete live rendered parity remains open.
