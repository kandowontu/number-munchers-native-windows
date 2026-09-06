# Word Munchers content and board static audit

This audit uses only `WM-unpacked-image.bin`, its relocation-aware address
mapping, `WLIST.BIN`, and the lossless word-list parser.  It does not launch
DOSBox, `WM.EXE`, or a native window.

## Address basis and file loader

The executable's dominant relocated data segment is paragraph `22C8`, so the
data segment begins at load-image `0x22C80`.  This maps the `wlist.bin` string
at image `0x23DD4` to `DS:1154`, exactly matching the open call at image
`0x0883B`.

The section loader at image `0x087C1` accepts a sound-section index and:

1. finds one of three inactive cache slots at `DS:0464`, each `0x450` bytes;
2. opens `wlist.bin` read-only;
3. seeks to `index * 4` and reads two little-endian 32-bit offsets;
4. seeks to the first offset and reads exactly `next - first` bytes into the
   slot;
5. closes the file and returns a far pointer to the slot.

The largest physical section is 1,094 bytes, so it fits the 1,104-byte slot
without truncation.  The error branch uses the preserved `Missing word list
file.` and `Can't continue!` strings.  This proves the offset-table and raw
section interpretation enforced by `tools/audit_word_list.py`; no content is
being inferred from loose strings alone.

## Section format

The file begins with 22 uint32 offsets, producing 21 sections.  Every section
is:

```text
byte difficulty_count[8]
byte record[][6]
```

The eight counts are nondecreasing and correspond to the eight visible
difficulty choices.  Every section has `difficulty_count[7] + 1` records.
The extra record is required by the original's distractor range, which skips
record 0 and may select through record `difficulty_count[difficulty]`.

The first 20 sections are the displayed vowel-sound families in executable
order.  Section 20 is a six-record long-u fallback (`mule`, `you`, `use`,
`huge`, `cute`, `mule`) with all eight counts equal to 5.

These physical section counts are used by gameplay pool construction. The
Options/Preview presenter separately uses the executable's 20-by-8 target-
availability table at `DS:26F6`; it deliberately marks some low-grade
Group 2/3 targets unavailable even though their WLIST sections retain records
for distractor use. This formerly conflated distinction is recovered and gated
in `WORD-OPTIONS-STATIC-AUDIT.md`.

All six source bytes of every chosen record are copied. The first NUL ends the
visible word, but 54 records have at least one nonzero byte later in the
record: hex `20` occurs 4 times, `53` 9 times, `54` 49 times, and `70` once.

The consumer trace closes their semantics. Image `0x0820A` stores one far
record pointer per board cell at `DS:5A92`; those pointers have exactly two
read sites, `0x105D5` and `0x10616`. They are passed unchanged to BGI
`textwidth` and `outtext`, both NUL-terminated string calls. Correctness,
consumption, completion, and Troggle trails read the independent signed-index
table at `DS:5E1E` instead. No game branch indexes bytes after the first NUL.
The nonzero tails are therefore inert fixed-record padding, not hidden flags.
Native still preserves all six bytes for source fidelity while `text()` stops
at the first NUL exactly like the original display calls.

## Selected-sound tuple construction

The builder at image `0x082BD` reads the 20 selected-sound bytes at
`DS:5E80`, copies the configured difficulty from `DS:5EB8`, and constructs a
shufflable table of three-word tuples at `DS:5A18`.  Each tuple is a target
sound followed by one or two distractor sounds; `FFFF` in the third word
means that only one distractor is present.

- Sounds 0–11 are six natural pairs.  For each enabled target `i`, its
  distractor is `i XOR 1`.
- Long-u sound 8 is not admitted as a target at difficulty 0 or 1 because its
  available target count is below two.
- Sounds 12–14 form the visible second group.  Two selected sounds create both
  target/distractor directions; three selected sounds create three tuples,
  each targeting one sound against the other two.
- Sounds 15–19 form the third group.  Two and three selections use the same
  permutation rules.  With more than three selections, the routine visits
  every selected sound once as the target and randomly chooses two distinct
  other selected sounds as its distractors.

The helpers at `0x086BE`, `0x086F3`, and `0x0876B` make those exact rotations
and append six-byte tuple records.  These rules explain the Set Content
validation text requiring at least two sounds in the higher groups.

## Board vocabulary construction

The generator at image `0x0845B` chooses one tuple.  Ordinary play advances
through the tuple table and reshuffles on wrap; the Demo flag at `DS:5D74`
chooses a random tuple instead.  The tuple's first sound becomes the displayed
target family at `DS:5EB2`.

For the target sound, the generator reads the current difficulty count and
copies exactly that many six-byte records beginning with record 1 into the
correct-word pool. Record 0 is the target exemplar and is skipped by the live
board chooser. For its one or two distractor sounds, it fills a fixed
180-record wrong-word pool:

- one distractor contributes 180 random records;
- two distractors contribute 90 random records each;
- sampling is with replacement;
- distractor record 0 is skipped, and the random range selects records 1
  through the current difficulty count inclusive.

When sound 8 is used as a distractor below difficulty 2, the explicit branch
at `0x0858F` loads section 20 and uses its five-word range.  This closes the
previously provisional purpose of the final section.

## Thirty-cell board selection

The cell chooser at image `0x08224` makes an independent two-way random choice
for each cell.  One branch selects uniformly from the correct pool and stores
a positive one-based source index; the other selects uniformly from all 180
wrong-pool records and stores a negative one-based index.  The signed results
are retained at `DS:5E1E`, while the complete six-byte records back the board
labels.

After filling all 30 cells, image `0x08687` counts positive entries and
regenerates the entire board until there are at least two correct targets.
Thus the original rule is a per-cell 50/50 draw plus a two-target whole-board
minimum, not a fixed quota.  Duplicate words are possible because all pool
selection is with replacement.

## Native implementation gate

`src/word_content.cpp` now implements the recovered parser, tuple builder,
pool builder, and thirty-cell generator against the embedded original
`WLIST.BIN`.  `word_content_static_test` independently enforces:

- exact 21-section parsing and all eight counts;
- lossless six-byte records, including inert post-NUL padding, while visible
  text stops at the first NUL;
- pair/group tuple construction and low-difficulty long-u exclusion;
- cyclic/shuffled user tuple selection and random Demo selection;
- correct-pool range, 180-entry sampled distractor pool, and section-20
  fallback;
- signed per-cell classification, independent 50/50 draws, and full-board
  retry below two targets.

The test also locks complete source provenance (13,474 bytes and FNV-64
`0x3277179d8e2823de`), all 2,203 records, the 54 records containing nonzero
post-NUL bytes, malformed-input rejection, exact pair/triple ordering, and a
seed-`0x1234` deterministic board snapshot at FNV-64
`0xf13ec2ff889dad0f` after 244 original-PRNG calls.  The shared
`OriginalRandom` type is now used by Number Munchers too, preserving its prior
headless replay tests. The generator also retains the current correct and
distractor pools for `nextCell()`: a Troggle trail can replace one cell using
the ordinary two per-cell PRNG calls without rebuilding a tuple or whole
board. The integrated Troggle gate tests that call count and the resulting
cell classification.

This audit establishes content mechanics, not visual or timing parity. The
shared score/life rules and a non-presentation board owner are now separately
gated in `WORD-GAME-CORE-STATIC-AUDIT.md`. Exact target-question and HUD
composition are now statically recovered and headless-tested in
`WORD-BOARD-HUD-STATIC-AUDIT.md`. All 33 executable random-wrapper call sites,
tuple rejection loops, integrated board/job/player/Troggle startup order, and
the zero-draw Demo-completion boundary are now closed at the static/headless
level in `WORD-PRNG-STATIC-AUDIT.md`. The known Troggle per-cell call path and
scheduling lifecycle are separately closed in
`WORD-TROGGLE-STATIC-AUDIT.md`. Live original-frame comparison and an organic
whole-session DOS/native PRNG trajectory remain open.
