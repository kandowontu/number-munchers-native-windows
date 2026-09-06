# Menu and instruction-routing static audit

This audit uses only `NM-unpacked-image.bin`, `NM-unpacked.ndisasm`, the
preserved logical screenshots, and headless native tests. It does not execute
DOSBox or either game.

## Instruction question

The DOS startup establishes data segment `0x2734`; therefore the question text
at load-image offset `0x28AF5` is `DS:17B5`. The question handler begins at
image `0x12A6A`, paints that string, and calls `pSeqKey` at `0x12F6F` with two
choices and default selection two (`No`).

`pSeqKey` copies the exact nine-byte key list at `DS:1664`:

```text
C8 D0 CB CD 79 59 6E 4E 00
Up Down Left Right y Y n N terminator
```

Its branches at `0x12FD2`–`0x1302C` prove the routing:

- Up and Down leave the two-choice selection unchanged;
- Left idempotently selects choice one (`Yes`) and Right idempotently selects
  choice two (`No`); repeating either arrow does not toggle the choice;
- `y`/`Y` select Yes and `n`/`N` select No, then remain in the widget loop;
- Enter (or pointer activation of the selected item) accepts the current
  selection; Space is ignored because it is absent from `DS:1664`;
- Escape cancels the Play path.

The wrapper at `0x12AE4` normalizes the result to `Y`, `N`, or Escape. A `Y`
calls the six-page instruction routine at `0x1223A`; accepted No returns to the
caller without showing it.

## Pager completion versus cancellation

Every instruction page calls the key waiter at `0x12F14`. That routine passes
the exact `DS:0864` accepted-key string `{Escape, Enter, Space, 0}`. Arrows and
printable letters are not navigation commands, and the original has no reverse
page operation. The underlying waiter maps left release to Space but leaves
right release as rejected code `0xFD`.

The pager returns nonzero on Escape. The question wrapper at `0x12AFB` converts
that into a canceled Play result; its caller at `0x129D6` returns to the main
menu. Only normal completion of all six pages continues to the enabled-game
selector at `0x12B6D`.

## Native regression coverage

Native now implements selection-only Y/N, exact idempotent arrow behavior, Enter-only
prompt acceptance, ignored prompt Space, Enter/Space/left-release paging with
right release inert, main-menu
routing on pager Escape, and game-selector routing after normal completion.
`game_render_state_test` executes all of those paths and retains the six
existing pixel-identical Information page hashes.

`MENU-POINTER-STATIC-AUDIT.md` separately establishes the exact title-menu hit
rectangles and physical press/release selection semantics. The broader menu
parity row remains partial for the other gaps tracked in `PARITY.md`.
