# Number Set Content numeric-editor static audit

## Scope and evidence

This audit covers the Number Munchers Set Content range editor and the
Multiples `Other` numeric editor. The evidence is the preserved unpacked image
and `analysis/NM-unpacked.ndisasm`; no DOS or native GUI process was launched.

## Shared numeric helper

Image `0x14F97` is the bounded-number helper used by both editors. Its caller
passes a field rectangle, character buffer, inclusive minimum and maximum, and
a maximum character count.

- `0x14FCA` clears the character buffer before each attempt.
- `0x14FD1` invokes the line editor; Escape branches through `0x150C2` and
  returns `-1`.
- `0x14FE5` converts the accepted buffer to an integer. Consequently an empty
  Enter becomes zero and follows the validation path; it is not an inert key.
- `0x14FF7`-`0x1500E` accepts only an inclusive in-range value.
- `0x15011`-`0x150BD` paints and waits on the value-range message, then
  `0x150CD` loops to the buffer-clearing entry point.

The range page derives its character cap from the decimal length of the row's
allowable maximum at `0x142E4`-`0x1431B`. That makes Multiples, Factors,
Equality, and Inequality two-character fields and Primes a three-character
field. Multiples `Other` pushes a literal two-character cap at `0x15390`.

## Range transaction

Routine `0x141E4` copies the installed row's lower and upper limits into locals
at `0x14215`-`0x14245`. It does not write a lower candidate as soon as that
field accepts:

- the lower helper call is `0x14447`-`0x14481`;
- the upper helper call is `0x1448D`-`0x144C7`;
- either helper returning `-1` cancels the dialog without changing the row;
- `0x144D0` compares both local candidates;
- only the ordered case writes both row fields at `0x144DA`-`0x14512`;
- a lower-greater-than-upper result paints the ordering message at
  `0x1451C`, restores the field area, and loops to the lower helper at
  `0x14447`.

Therefore the pair is transactional. A range-order failure leaves the visible
`Current` values and draft row unchanged, clears the input, and restarts at the
lower field. A value-range failure clears only the current field and retries
that same stage.

## Multiples Other transaction

The call at `0x15390`-`0x153A7` passes literal bounds `3` and `50` plus a
two-character cap. The minimum is not the edited target-range lower limit; the
screen's source string likewise says `(Allowable range: 3 - 50)`.

A valid value enters state 1 rather than committing. The state redraws the
candidate at `0x153E6` and waits on the local table
`DS:22D0 = {Escape, Enter, 0xFD, NUL}` at `0x15441`. Enter and an untouched
right release commit at `0x1546C`. pAccept rewrites a left release to Space,
which is not in this table and remains inert.

Escape is not an outer-dialog cancel in state 1. `0x15449`-`0x15465` clears the
confirmation area, selects state 0, and loops back to numeric entry. A second
Escape from that numeric helper returns `-1` and closes the editor with the
installed multiplier unchanged.

## Native regression gate

`game_render_state_test` now requires:

- per-row two/three-character limits;
- empty Enter reaching value validation;
- cleared buffers after validation;
- no draft mutation after only the lower value;
- complete lower/upper restart after an ordering failure;
- atomic commit only after both ordered values pass;
- fixed `3..50` Multiples-Other bounds independent of the target range;
- confirmation Escape returning to numeric entry;
- Enter/right-release confirmation and inert left release; and
- the existing full confirmation-frame hash `0xd06b91c92d4be15a`.

