# Word Munchers Hall-of-Fame static and live audit

The rank/control audit is headless and uses the preserved Word executable and
configuration. A later muted physical-window capture adds the supplied
seven-entry title-browse Hall as a complete 320x200 live/native comparison;
`tools/audit_word_options_capture.py` reproduces it in
`analysis/word-live/options/report.json`.

## Rank routine

The Word Hall rank routine is at image `0xB35E` (far target `0ADD:058E` from
the game terminal at `0xD087`). It receives the candidate as a 32-bit score:

- `0xB364` rejects a non-positive high word; when the high word is zero,
  `0xB36C` rejects a low word below 50;
- the scan at `0xB37E` visits the active 30-byte records in rank order and
  continues while an existing 32-bit score is greater than or equal to the
  candidate;
- it therefore returns the first strictly lower rank, placing a new equal
  score after every existing equal score;
- if no lower score exists, `0xB3B5` appends only while the active count is
  below ten; a full list returns `FFFF`.

The caller clamps scores above 1,000,000 to exactly that value at
`0xD06A`–`0xD081` before rank lookup. The insertion path uses the returned
rank and the configuration's 30-byte name/score records, with capacity ten
and a 25-character visible name limit.

## Native state gate

`WordHall` loads only the active count from the preserved `WM.CFG`; inactive
stale records remain available through the lossless `WordConfig` but do not
become live ranks. It implements the exact 50-point floor, stable after-equals
rank, strict full-list cutoff, ten-entry cap, one-million clamp, 25-character
name limit, and the original empty-name fallback `The Unknown Muncher`.

The headless test starts from all seven supplied active entries, checks a tie
with the 14,660-point leader, clamps an overshoot to 1,000,000, fills the list,
rejects an equal score at the full-list floor, admits a strictly higher score,
and rejects malformed Hall metadata.

The native presenter now also integrates admission after terminal loss and
one-million completion, all three recovered name-entry message variants, the
25-character/zero-threshold editor contract, empty-Enter and empty-Escape
fallback, post-game Hall context,
and the default-Yes replay question. See
`WORD-FEEDBACK-TERMINAL-STATIC-AUDIT.md` for that call trace and headless gate.

## Hall painter

The Hall presenter begins at image `0x11BFE`. It chooses Word PCXF `6002` for
CGA or `6003` for VGA, rather than drawing a synthetic panel. The copied
flags-`0x140` formatter record at `DS:2306` supplies centered, unboxed text.
Tracing its vertical clamp and five-pixel inset establishes these baselines:

- `WORD MUNCHERS` at y=35 and `Hall of Fame` at y=62, from the caller's y=30
  clamp and exact source `WORD MUNCHERS\n\n\nHall of Fame`;
- an empty Hall's two lines at y=120 and y=129, after the source's two leading
  line feeds and the caller's y=97 clamp;
- ranked rows at y=97 through y=178 in the small font, nine pixels apart;
- the continuation footer at y=185.

For each ranked row, the original right-aligns the rank at x=57, continues
with `. ` and the name, and right-aligns the score at x=262. A score of
1,000,000 is displayed as `Perfect`. User title browsing and post-game Hall
use `Press Space Bar to continue.`; only the unattended Demo Hall uses the
centered `Press a key for Muncher Menu` footer.

The call at `0x11C40` is not a pointer-hover lookup. It searches the Hall from
the beginning for the exact `(name,score)` pair retained in `DS:5D78` and
`DS:5C0A/5C0C` after admission. A match is painted opaque rather than
transparent: VGA swaps to white-on-black, while CGA uses white-on-magenta.
The admission path clears that pair when a score does not qualify. Native
keeps the pair rather than merely an insertion index, preserving the
original's first-match behavior for identical duplicate entries and removing
the highlight naturally if the entry is erased. The original painter clears
the lookup score after drawing; native must retain the pair across ordinary
Windows repaint requests, so it consumes the pair when the Hall is departed.
Headless transition coverage prevents it from leaking into a later browse.

The native presenter now follows that composition over the dedicated Hall
artwork. Deterministic VGA full-frame FNV-64 gates cover a populated ten-row
Hall including `Perfect`, the post-game footer variant, and the empty-list
variant:

| VGA case | Native FNV-64 |
|---|---:|
| user browse, ten rows | `00c2d7de350c280d` |
| post-game, ten rows, no admission | `00c2d7de350c280d` |
| post-game, admitted `Perfect` row highlighted | `f5a395708aba6a1d` |
| user browse, empty | `20cf339fef12ba37` |

The supplied seven-entry title-browse page is an exact original/native match
at `0x04616f92e6dfa290`. It proved the user-browse Space footer and corrected
the former native-only any-key footer on that route.

The complete CGA supplied-Hall composition is gated separately in
`WORD-CGA-STATIC-AUDIT.md`.

This closes Hall ranking/state, terminal routing, admission highlight, and
painter composition at the static/headless level, plus the supplied browse
page at the live VGA level. There is no row-hover or mouse-selected Hall state
in this routine. Live post-admission/empty-Hall comparison and physical
input/audio validation remain outside this gate; the supplied Hall eraser is
live-matched separately in `WORD-OPTIONS-STATIC-AUDIT.md`.
