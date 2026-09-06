# Super Munchers category-content audit

Date: 2026-08-28

The native `SuperContent` parser reads the six embedded CATG/WLST/INDX
triplets directly. No category, rule, word, difficulty rank, or membership is
transcribed into source code.

## CATG

Every CATG entry is an exact array of 68-byte records:

- record 0: a NUL-terminated title in bytes 0-63, little-endian word count in
  bytes 64-65, and little-endian rule count in bytes 66-67;
- records 1..N: a NUL-terminated rule in bytes 0-63 and four losslessly
  retained control bytes in bytes 64-67.

Three final source records contain non-zero legacy workspace bytes after the
first NUL. The DOS string contract terminates at that NUL; the parser accepts
the authoritative residue but never presents it as text.

## WLST

Each WLST contains exactly the header-declared number of NUL-terminated words
followed by one non-NUL source trailer byte. The trailers are preserved as
`36 09 62 09 69 20` for sets 1-6. The apparent one-byte suffix is not admitted
as an extra word.

## INDX

For `R` rules, an index record is `1 + ceil(R / 4)` bytes:

- byte 0 is exactly 10, 20, or 30 and supplies the word's difficulty rank;
- the remaining bytes store four two-bit rule classifications each, most
  significant pair first: 0 unavailable, 1 match, 2 non-match;
- value 3 does not occur and is rejected;
- one all-zero record follows the declared word records and is validated as a
  sentinel rather than exposed as content.

## Permanent gates

The test covers all 3,771 words, 157 rules, and 100,169 packed classifications via
per-set counts and an ordered semantic FNV-1a hash of
`C4FF1679DDF79B10`. It additionally gates every title, record count, rank
distribution, membership distribution, and source trailer.

## Native board controller

`SuperBoardGenerator` implements the recovered image `0x10A2F..0x110BD`
contract directly:

- eligible `(topic, rule)` pairs retain ascending source order and require the
  topic bit, rule bit, and rule minimum-difficulty byte;
- ordinary play advances cyclically and, on every wrap including the first
  board, draws one full-range swap index per pair. A self draw is skipped, not
  retried. Demo chooses a pair randomly and first chooses difficulty 10 or 20;
- the negation draw occurs only when both the option and rule flag allow it;
- Match and NonMatch pools are independently rejection-sampled from the full
  word list until each contains exactly 60 records. Duplicate records and all
  rejected PRNG draws are retained as original behavior;
- every one of the 30 cells consumes a correct/wrong draw and a 60-record pool
  draw. Boards with fewer than two correct cells are discarded and rebuilt;
- the two pools remain live for transformation-trail cell replacement.

Four independent raw-resource snapshots gate ordinary category play,
Challenge with negation, Demo difficulty/rule selection, and selective topic/
rule filtering. They lock eligible-table hashes, shuffled-table hashes, rule,
display text, all cells and signed source indexes, pool sizes, correct quota,
and final Borland PRNG call count/state.

This closes category parsing and normal board construction. Gameplay timing,
scoring, the 20-answer transformation cycle, Troggle combat, and Answer Vision
remain controller-level work.
