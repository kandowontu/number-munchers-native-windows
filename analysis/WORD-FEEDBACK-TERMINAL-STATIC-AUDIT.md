# Word Munchers feedback and post-game static audit

This audit uses the preserved Word load image and disassembly plus muted live
VGA captures. `analysis/word-live/feedback/report.json` validates wrong-word,
collision, blank/typed name-entry, admitted Hall, both replay selections, and
the return to title; the lossless collision recording is independently
decoded by `tools/audit_word_collision_capture.py`. Native comparisons and
controller tests run headlessly and do not open an audio device.

## Wrong-word feedback

The chew terminal begins at image `0x09F76`. After the seventh transition, an
incorrect word reaches the branch at `0x09FC5`. It saves the attempted record,
calls the loss routine at `0x09FD2`, and then calls the Word-specific formatter
at `0x09FDD` (far `0D2C:0021`, image `0x0D2E1`).

The loss routine at `0x09E2F` clears only the attempted board record and
decrements `DS:5DAE` at `0x09E70` before the feedback is displayed. It does
not regenerate the board or advance any gameplay job during the blocking user
prompt.

The formatter saves rectangle `(21,87)..(307,115)` at
`0x0D2ED`-`0x0D318`. This is one board-row interior: the grid sides at x=20
and x=308 and horizontal rules at y=86 and y=116 remain present. With the user
branch selected, the two explanation baselines are y=88 and y=98 and the
continuation baseline is y=108. The exact source composition is:

```text
The word "<attempt>" does not have the
same vowel sound as "<active sound label>".
Press SPACE BAR to continue
```

The formatter's branch at `0x0D404` omits the continuation in Demo and shifts
the two explanation lines to demo baselines y=93 and y=103. The native attract
host now follows that branch. Its in-memory gate requires both shifted lines
and the absence of the user Space-bar footer; see
`WORD-ATTRACT-STATIC-AUDIT.md`.

The previous native painter incorrectly erased two row interiors, destroyed
the y=116 grid rule, placed the explanation at y=91/101, and placed the
continuation at y=121. It now follows the traced one-row geometry. For the
captured `/e/ as in tree` failure, original and native match at FNV-64
`0x59cea3cabab4e368` over `(20,86)..(308,116)`. The seeded static fixture
remains separately gated at `0xf8e2b6a72d62d842`.

## Collision feedback and terminal loss

The collision sibling begins at image `0x0D46C` and is called after the
21-tick bite terminal at `0x0A9BD`. It uses the same saved rectangle and user
baselines 88/98/108, with the recovered random exclamation, `! You were eaten
by a`, and `Trogglus <species>.` strings. The common loss path again tests the
signed reserve only after the blocking feedback returns.

For either wrong-word or Troggle loss, a reserve value below zero calls the
game terminal at image `0x0947F`. Nonterminal loss returns to the unchanged
board after Enter or Space, with enemy and safe-zone jobs frozen for the
duration of the blocking feedback. The common `DS:14D4` accepted-key waiter
also rewrites a left release to Space; right release stays `0xFD` and is
rejected. The visible instruction still names the Space bar, but it is not a
Space-only input table.

Escape has a distinct recovered route. The wrong-word waiter at `0x0D42C`
branches to the default-No quit wrapper call at `0x0D43D` (wrapper image
`0x0CFB3`); accepted Yes calls the ordinary scored terminal from `0x0D450`.
The collision waiter is isomorphic at `0x0D5CB`, `0x0D5DC`, and `0x0D5EF`.
No—or Escape from the prompt—restores the saved strip and returns through the
feedback caller, which still applies the signed-reserve decision. Yes reaches
terminal image `0x0947F`, including Hall admission/name entry for a qualifying
score. The wrapper's Y/N keys only change the selection; Enter or pointer
activation accepts it, Left idempotently selects Yes, Right idempotently
selects No, Up/Down do nothing, and Space is ignored.

`ACCEPTED-KEY-POINTER-STATIC-AUDIT.md` records the shared waiter instructions
and the paired Number/Word headless input gates.

## Hall admission and name entry

The user terminal calls Hall admission at `0x09495` (far `0CFB:007E`, image
`0x0D02E`) before displaying the argument-7 Hall. Admission clamps an
overshoot to 1,000,000 and calls the rank routine at `0x0D087`. When admitted,
it selects one of three name-entry messages:

- type 1 for an ordinary admitted rank: `You made it into` / `the Word
  Munchers Hall of Fame!`;
- type 2 for rank zero: `You have the Word Munchers` / `Hall of Fame best
  score!`;
- type 3 for 1,000,000: `A Word Munchers Hall of Fame` / `perfect score!!!`.

All variants begin with `Congratulations!` and end with `Type your name, and
press Enter.`. The editor host at image `0x0D1F1` passes maximum length 25 at
`0x0D26A` and the output buffer at `0x0D272`. The value 1 pushed at `0x0D277`
is the constructor's bit-0 geometry flag, not a minimum length. The actual
editor record at `DS:16BC` stores zero at `+0x06`, so empty Enter exits. Escape
also exits; the caller at `0x0D0EC`-`0x0D104` substitutes
`The Unknown Muncher` whenever the returned buffer is empty. Left shares
trailing-character deletion, Home clears the field, and `DS:16B7 = " -~"`
allows Space through tilde through the common editor recovered for Number
Munchers.

Reaching 1,000,000 does not show a separate gameplay feedback page. The score
routine branches directly to the game terminal at `0x08AAC`, and type 3 is
the name-entry message. The native runtime previously fabricated an extra
perfect-score feedback/Space screen; it now enters the terminal directly.

## Hall and replay route

After admission, `0x0949B` displays the argument-7 Hall. The next call at
`0x094AA` reaches the replay prompt at image `0x0D157`. That routine draws
`Do you want to play again?` at `(55,85)`, initializes the Y/N widget to Yes,
and returns true only after the Yes selection is accepted. Y/N select without
accepting, Enter or pointer activation accepts, Up/Down do nothing, Space is
ignored, and Escape cancels. Accepted Yes schedules a fresh level-1 game;
accepted No or Escape returns to the Word Muncher Menu.

The native runtime now distinguishes title-browse and post-game Hall callers,
implements the 25-character/zero-threshold editor contract, Backspace/Left,
Home, inert unsupported edit keys, partial-name Escape, empty-Enter and
empty-Escape fallback, all three admission messages,
post-game Hall continuation, and the default-Yes replay route. Headless tests
cover a zero-score final wrong answer, ignored ordinary feedback input, Space
resume, Escape/default-No prompt entry, prompt cancel/resume, qualifying-score
Yes/name admission, final-life No/Hall continuation, grid preservation,
nonqualifying Hall routing, selection-only Y/N across replay and quit, both
replay outcomes, ordinary and best-score name admission, both empty fallbacks,
and the perfect-score message route. The live/native regression hashes are:

- collision feedback strip: `0x9d7e227a62f941cc`;
- blank name-entry modal: `0x05eec9c3f1ef5ec3`;
- typed name-entry modal: `0x285a173c1222b63d`;
- admitted highlighted Hall: `0x4c3fda7428cb5224`;
- default-Yes replay frame: `0xdff55d7dfe18637e`;
- selected-No replay frame: `0x4e398cfe7456fd82`;
- replay-No return title: `0xd0862a8ebebc6146`.

The captured text-entry caret alternates at the recovered 16-tick blink
period. Seeded static fixtures for otherwise useful alternate compositions
remain separately gated; they are not substituted for the live hashes above.

## Completed parity boundary

The captured user path covers both feedback kinds, text entry, an admitted
opaque-highlight Hall row, replay Yes/No, and title return. Separate live pages
cover the supplied title-browse Hall and initially empty Hall. Those full-frame
anchors establish the shared painter, palette, modal backing, highlighted-row,
caret, and caller-specific footer geometry used by every parameterized branch.

The remaining variants are not alternate implementations: the disassembly
selects three literal admission-message pairs, one rank field, one score field,
and the same common editor/Hall painter. Deterministic headless gates exercise
ordinary/best/perfect messages, qualifying and nonqualifying terminals, stable
ties and full-list cutoff, no-more-entries and empty-name fallbacks, all editor
keys, all accepted-key and pointer event translations, both replay outcomes,
configuration failures, and every title/user/Demo Hall caller. The separately
verified Word attract route supplies the 150-tick Demo feedback and Wipe.

Physical pointer translation, persistence storage, and sound-device identity
remain tracked by their dedicated rows; they no longer duplicate a material
gap in this feedback/Hall/name/replay row.
