# Original player-movement callback audit

This note records the executable evidence for Number Munchers player movement,
the corresponding Word Munchers implementation, the existing Number
four-direction capture, and the muted live Word right-move capture. Re-running
the offline checks does not require a live DOS session.

## Move initialization

The player move initializer begins at image offset `0x08BBA`. It derives the
current row and column from the six-column cell index, changes one coordinate
according to direction `0/1/2/3 = up/right/down/left`, and rejects destinations
outside the 5x6 board. The initializer writes initial BTMP records
`2/5/8/11`, the third record in each directional group, at
`0x08C01`-`0x08C18`. It then stores animation counter 2 at `0x08C7D`, installs
actor state 3 at `0x08C8C`, mode 2 at `0x08C9B`, and a one-tick callback
interval at `0x08CAA`.

The directional groups are therefore fixed, not inferred from the art:

| Direction | BTMP 1006 records | Initial record |
|---|---:|---:|
| Up | 0-2 | 2 |
| Right | 3-5 | 5 |
| Down | 6-8 | 8 |
| Left | 9-11 | 11 |

The standing player is the separate fixed record 7. Movement does not begin by
displaying the first record of a directional group.

## Position callback

The common actor movement primitive at `0x0871A` advances the live pixel
coordinate using the recovered low-speed deltas at `DS:5AB4/5AB6`: 8 pixels
horizontally and 6 pixels vertically. A board cell is 48x30 pixels, so a full
horizontal move has six visible position phases and a vertical move has five.
The existing lossless four-direction capture independently shows those exact
8/6-pixel increments.

When the target has not yet been reached, `0x0882F` calls the animator at
`0x07F5B`. The dirty-cell presenter paints the translated actor before that
callback's record mutation becomes visible. Starting from record offset 2 and
counter 2, the six visible horizontal positions—including the terminal
position—therefore use:

```
2, 1, 0, 1, 2, 1
```

The five visible vertical positions consume the first five entries. In
complete-frame native state, phase 0 retains the initializer's offset 2 and
the five nonterminal translated phases use `2,1,0,1,2`; the terminal callback
at `0x0883A` snaps both coordinates, does not call the animator, and retains
offset 1 horizontally or offset 2 vertically for one public interval. The
following interval switches to standing record 7. This rules out both smooth
percentage interpolation, the former native `0,1,2,...` pose cycle, and a
premature standing pose at the target.

The live Word run in `analysis/captures/wm_007.avi` isolates a right move from
row 3/column 3 to row 3/column 4. Logical actor x positions are
`176,184,192,200,208,216`; the target retains directional frame 4 before
standing frame 7. Stable union-region FNV-64 hashes from capture frames
2466-2478 match native exactly. The destination label is repainted with an
opaque 32x8 blue GFT character box followed by its white glyph pixels, so the
word visibly cuts through the actor; grid and safe-corner foreground records
follow. Capture frame 2465 catches the first dirty-cell refresh partway through
and is missing 20 white and two magenta foreground pixels in only its top six
rows. Native intentionally presents the completed logical phase instead of
reproducing that one-refresh partial frame. The distinction is automatically
checked by `tools/audit_word_gameplay_capture.py`.

At the recovered scheduler rate of about 29.1375 ticks per second, the visible
callback spans are approximately 205.9 ms horizontally and 171.6 ms
vertically. Native retains an actor-local movement timer rather than inserting
the player into the Troggle/safe-zone update loop; doing the latter changes the
already capture-matched attract-controller ordering. The timer nevertheless
uses the exact five/six callback counts, positions, record order, and terminal
behavior above.

## Keyboard normalization

The Number gameplay dispatcher at image `0xD0CB`-`0xD197` and the corresponding
Word dispatcher at `0xDD75`-`0xDE41` normalize the same printable aliases to
DOS extended arrow words: 8/A/I to Up (`0x00C8`), 4/J to Left (`0x00CB`), 6/K
to Right (`0x00CD`), and 2/M/Z to Down (`0x00D0`). Uppercase and lowercase take
the same routes. Space is the separate chew action; A and Z do not chew.

This matters beyond the target square: direction identity selects the BTMP
group above and the vertical five-callback versus horizontal six-callback
duration. The native Number and Word runtimes now preserve those exact routes.
See `KEYBOARD-INPUT-STATIC-AUDIT.md` for the dispatcher trace and tests.

Because this conversion occurs before enqueue, aliases and physical arrow keys
have identical FIFO behavior. They cannot start a movement immediately while
the Muncher is moving, chewing, dying, or in state-6 post-collision recovery,
but they are not normally discarded: their normalized bytes wait in the same
nine-entry ring as negated pointer targets. Collision setup clears old bytes;
new bytes accepted after its blocking feedback waiter survive recovery and are
consumed on the next public player callback after the survivor leaves. Enter's
common branch clears the ring on both entry to and return from `Time out`.

## Shared Word implementation

The extracted Number and Word player resources are byte-identical for both
PCXF 1006 and BTMP 1006. Word Munchers therefore uses the same recovered
movement callback model and record sequence. Its player and eating continuation
sheets are decoded through the same exact resource path rather than substitute
art.

## Native lock

`game_render_state_test` and `munchers_app_headless_test` directly lock the
five/six durations, the complete phase-0 mapping `2,2,1,0,1,2`, the retained
horizontal/vertical terminal records, the stable live Word composite hashes,
all digit/uppercase/lowercase routes, and direction-specific rendered
coordinates. Their shared-ring fixtures require FIFO movement,
movement queued behind a correct chew, capacity nine, retained state-6 input,
next-public-tick recovery consumption, and Enter reset for mixed keyboard and
pointer bytes. The collision fixture additionally proves that two new inputs
remain queued while the surviving Troggle occupies the player cell, survive
the same tick on which it leaves, and start only on the following player slot.
At 0.10 seconds the
phase-2 yellow-pixel minima are `(141,96)` right, `(109,96)` left, `(125,84)`
up, and `(125,108)` down. The full deterministic Number Munchers replay remains
unchanged through the supplied capture after this correction.
