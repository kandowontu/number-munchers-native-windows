# Hall-of-Fame static audit

The original Hall insertion path is recovered from `NM-unpacked.ndisasm` at
image offsets `0xA208` and `0xA77D`.

- `0xA77D` rejects scores below the unsigned 32-bit value 50.
- It scans the existing list from rank 1 and continues while an existing score
  is greater than or equal to the candidate.
- It therefore returns the first rank with a strictly lower score. A new entry
  is inserted after all existing equal scores.
- If no lower score exists, it appends only while the list contains fewer than
  ten entries. An equal score cannot displace rank 10 in a full list.
- `0xA208` shifts the lower records down, writes the new 30-byte record at the
  returned rank, and caps the stored count at ten.

Native uses the same 50-point floor, strict full-list cutoff, and stable
after-equals insertion. `game_render_state_test` covers all three cases.

## Painter, admitted-row lookup, and score text

The Hall painter at image `0x12478` copies its formatter record from
`DS:154C`, chooses PCXF `6002` for CGA or `6003` for VGA at `0x124CF`, and
draws ranked rows from y=97 in nine-pixel small-font steps. Rank text begins
from the recovered x=57 right edge, the combined rank/name label begins at
the corresponding x=51 position for rank one, and scores are right-aligned at
x=262. At `0x12739`–`0x12775`, scores at the one-million cap are rendered as
`Perfect` rather than decimal digits.

Before drawing, `0x124AE`–`0x124C3` searches the selected mode's list from
rank one for the first record whose name and 32-bit score exactly match the
just-admitted pair at `DS:5970` and `DS:5864/5866`. This is not a hover or
stored-rank selection: identical duplicate pairs highlight the first match.
The matched branch at `0x12630`–`0x12676` disables the formatter's transparent
bit and paints both label and score opaque. VGA uses white on black; CGA uses
white on magenta. Ordinary VGA rows are transparent black, while ordinary CGA
rows are transparent white.

The preserved `original-hall-after-name-entry-external.png` contains the
logical 320x200 post-admission viewport at `(291,231)`, nearest-neighbor scaled
2x. It identifies the exact 25-character name
`vabcdefghijklmnopqrstuvwx`, score 85, and Inequality list. Native retains the
admitted `(name,score)` pair across repaint requests, clears it on Hall
departure, and matches the extracted full-frame FNV-64
`0xeb488a361b66de0a`. Headless CGA gates independently lock the statically
recovered normal-white and selected white-on-magenta branches.

## Selector and return routing

The Hall selector is the shared selector routine at image `0x12B6D`. The Play
path calls it with selector kind 0 at `0x129F9`; the Hall option in the title
menu calls the same routine with selector kind 1 at `0x0E988`. Both reach the
generic widget at `0x12E2A`.

- `DS:1598`, copied as the widget's custom-key list, is the one-byte empty
  string `{0}`.
- The widget searches that list at `0x0E3F4` and otherwise ends keyboard input
  only on Escape or carriage return at `0x0E78C`/`0x0E792`.
- Consequently Up/Down navigate and Enter accepts; keyboard Space is not a
  selector accept key. Pointer activation remains a separate widget event.
- Escape, or a zero result, takes the false return at `0x12E36`–`0x12E4B`.

The title-menu caller at `0x0E981` is local and does not leave the main-menu
loop. A successful selection stores the chosen mode and calls the Hall painter
with argument 7 at `0x0E99A`. Whether the selector is canceled or that Hall
call completes, `0x0E9A5` clears/redraws and the enclosing loop resumes. The
Hall browser therefore returns directly to the Muncher Menu, not to the Hall
selector.

## Argument-7 user Hall versus demo Hall

The Hall painter begins at image `0x12478`. Its tail has two distinct contracts:

- Argument 7 takes the branch at `0x127F4`, prints `DS:175D` (`" Press Space
  Bar to continue."`), and calls the waiter at `0x1280C` with `DS:0864`, the
  exact byte set `{Escape, Enter, Space, 0}`. The waiter converts left release
  to Space and rejects right-release code `0xFD`; other keys remain on the Hall.
- A game-mode argument 0–5 takes `0x1281B` and prints `DS:177B` (`"Press a key
  for Muncher Menu"`). It does not block inside the painter.

The game-end callback at `0x088B2` proves that user play also takes the
argument-7 contract. When the demo flag at `DS:596C` is clear, it calls the
Hall-admission/name routine at `0x0C62E`, then calls the Hall painter with
argument 7 at `0x088D1`. Once the Hall's Escape/Enter/Space wait completes, it
calls the replay prompt at `0x088DD`. Thus voluntary quit, terminal loss, and
completed name entry use the Space-bar Hall and continue to `Do you want to
play {mode} again?`; they do not take the any-key path.

The non-7 painter branch is the demo interstitial. The demo side of `0x088C1`
schedules main state 3 at `0x0891B`; state 3 passes the selected game mode to
the Hall painter at `0x1293A`. The common input dispatcher at `0x0D26A` routes
every keyboard event in state 3 through `0x12B4C`, while the scheduler also
advances the unattended demo lifecycle.

The preserved empty Prime demo Hall additionally isolates a painter-layout
branch hidden by the populated Multiples Hall: its `There are no entries` /
`in the Hall of Fame.` baselines are y=120 and y=129. The VGA argument-7
browser and post-game Hall retain y=100 and y=109. A later live CGA
title-browse capture proves that the four-color argument-7 path instead uses
y=120 and y=129 with the user footer; native selects both caller and graphics
mode and matches that complete CGA page for 140 frames. The demo Hall matches
at FNV-64
`0x2faa0bed13235713`.

Native now keeps all three callers separate: a title-menu browser Hall returns
directly to the title, a user post-game Hall continues to the replay question,
and an attract Hall retains its any-key footer/timed lifecycle. Both
argument-7 callers accept only Escape/Enter/Space. The Play and Hall selectors
accept Enter rather than keyboard Space. Headless transition tests cover each
route, and the preserved Space-bar/any-key Hall hashes cover both painter
contracts. The replay-Yes trace continues through the palette-neutral ordinary
board initializer, so native retains the Hall latch on the replayed user board
and resets it only after the title page is restored. Live first-board evidence
shows that the occupied index-111 sprite pixel itself remains `414100` across
both states.
