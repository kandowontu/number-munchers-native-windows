# Wrong-answer and feedback static audit

This audit uses the preserved `NM-unpacked-image.bin`, its matching
`NM-unpacked.ndisasm`, and lossless reference frames. It does not execute the
DOS program.

## Formatter dispatcher

The blocking wrong-answer routine begins at image `0x10582`. It reads the
active board component at `DS:608c` and dispatches through the five-word table
at `0x10604`:

| Active component | Table target | Result |
|---|---:|---|
| Multiples | local `00ae` / image `0x1060e` | `"value" is not a multiple of "target".` |
| Factors | local `00ae` / image `0x1060e` | `"value" is not a factor of "target".` |
| Primes | local `01cd` / image `0x1072d` | `The number "value" is not prime.` |
| Equality | local `034c` / image `0x108ac` | `Oops!  "expression=result"` |
| Inequality | local `034c` / image `0x108ac` | `Oops!  "expression=result"` |

The shared arithmetic branch calls the original expression formatter at
`0x108b2`, appends the evaluated result and closing quote, and retains the
operation's dedicated thin GFT glyph. Addition, subtraction, multiplication,
and division therefore do not have separate feedback templates. Challenge has
no sixth formatter: board setup stores its selected component in `DS:608c`, so
it reaches the corresponding row above.

The relevant data strings are `DS:12b4` (`"`), `DS:12ff` (Multiples suffix),
`DS:1316` (Factors suffix), `DS:132b` (`".`), `DS:132e` (`The number "`),
`DS:133b` (`" is not prime.`), `DS:1359` (`Oops!  "`), and `DS:1362` (`=`).
No difficulty, graphics-driver, sound-driver, or input-device selection is
read by this dispatch. The only presentation branch is the existing Demo flag.

## Overlay geometry and Demo branch

The routine saves the rectangle `(21,87)` through `(307,115)` at
`0x105a1`–`0x105d8`. It therefore replaces only the interior of the two-row
board band while retaining the grid sides at x=20 and x=308 and the horizontal
rules at y=86 and y=116.

At `0x105db` the user-game text base is 88 and the Demo text base is 93. The
one-line explanation is drawn at base+5. A user game draws its continuation at
base+20 and blocks for Space; Demo omits that continuation and takes its
automatic transition path at `0x109cf`.

Troggle feedback uses the sibling routine at image `0x109e3`. Its base-Y
selection is at `0x10a22`: 88 for a user game and 93 for Demo. The exclamation
line is drawn at base, the `Trogglus ...` species line at base+10, and the
user-only Space continuation at base+20. Demo skips the third line at
`0x10b23`–`0x10b78`; the ordinary board footer remains visible instead.

Two lossless crops now gate the native painter:

- `original-live-inequality-wrong-feedback.png`, inner strip
  `(20,86)`–`(308,116)`: FNV-64 `0x873482585c0afb4b`.
- `original-demo-collision-full-sequence/0096.png`, the corresponding Demo
  collision strip: FNV-64 `0x15a745407e98c823`.

Matching those crops corrected four previously untested visual errors: the
left grid side was erased, both wrong-answer rows were two pixels high, the
arithmetic operation used a heavy ASCII glyph, and Demo collision feedback
incorrectly included the user Space instruction. The collision routine's
different two-line vertical placement is also now preserved.

## Munch and resume lifecycle

The chew terminal callback begins at `0x093a9`. After its seventh transition,
the incorrect-answer path removes the attempted answer record and calls the
formatter at `0x09410`. The user branch remains inside the blocking feedback
routine; gameplay jobs and movement cannot advance during that wait. The
shared `DS:0864` table accepts Enter or Space and restores the saved strip
normally; its event-type-2 override also maps a left release to Space, while a
right release remains rejected `0xFD`. Escape leaves the wrong-answer waiter at
`0x1099F`, calls the default-No quit wrapper from `0x109B2`, and, when Yes is
accepted, calls the scored terminal at `0x109C6` / image `0x088B2`. The
collision sibling follows the same structure at `0x10B4B`, `0x10B66`, and
`0x10B6F`.

No—or Escape while the Yes/No prompt is open—restores the saved feedback strip
and returns through the unchanged caller. A nonterminal loss therefore resumes
the same board, while an exhausted reserve still takes the ordinary final-loss
route. Yes calls the scored terminal, including Hall admission/name entry for
a qualifying score; this intentionally differs from an ordinary voluntary
in-game quit. Other feedback keys remain ignored.

`game_render_state_test` exercises that lifecycle independently for
Multiples, Factors, Primes, Equality, Inequality, and Challenge: one reserve is
removed, only the attempted cell is cleared, score/level/target and all other
cells remain unchanged, Troggle and safe-zone timers stay frozen, unsupported
keys are ignored, and Enter, Space, or left release resumes the same board
while right release is inert. It also
locks Escape to the default-No prompt, selection-only Y/N, cancel/resume,
qualifying-score Yes/name admission, and final-life No/Hall routing. It
separately locks all four arithmetic-operation renderings and both lossless
strip hashes.

The shared event conversion and paired Word implementation are traced in
`ACCEPTED-KEY-POINTER-STATIC-AUDIT.md`.
