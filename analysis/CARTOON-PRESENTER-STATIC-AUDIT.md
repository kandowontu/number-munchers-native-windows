# Board-to-cartoon presenter audit

The control-flow half uses the preserved unpacked executable images and offline
disassemblies. Five muted lossless Number and six muted lossless Word
reference-control captures additionally measure the production close/load/open
painter and host cadence for every scene bank. No audio device was used.

## Number Munchers loader

Every third non-Demo completion reaches the selected-cartoon branch at Number
image `0x0F1BA`. The branch sets high-level state 6, preserves the prior input
mode, and then performs this synchronous sequence:

1. initialize the transition driver at `0x0F1C7`;
2. call the shared PCX effect with type 2 at `0x0F1D0`;
3. release the preceding image, select the shuffled cartoon, and enter its
   loader; `0x0F340` submits GSND 0 and the following delay helper blocks for
   the literal 15 ms before resource loading continues;
4. load the PCXF/SCPT resources, install the thread list, and reset the scene
   clock in the remainder of the `0x0F1D7`-`0x0F21B` sequence;
5. clear the target page through the VGA/CGA branch at `0x0F21C`-`0x0F246`;
6. call the same effect with type 1 at `0x0F24A`;
7. execute the literal 10 ms delay at `0x0F251`-`0x0F25A`; and
8. submit music-class event `0xA6` at `0x0F25C`-`0x0F266` before returning.

The resident high-bit branch strips `0x80`, so this is per-scene ADLI stream
38, not an SCPT callback. It is gated by Music and a nonzero/AdLib device but
not by Sound. See `CARTOON-SCORE-STATIC-AUDIT.md` for the conductor audit.

The cartoon cursor is still advanced only by teardown at `0x0F315`; the Wipe
does not change the completed visible level.

## Word Munchers correspondence

Word's every-third-board branch at `0x0FE2D` is instruction-equivalent. It
initializes the transition at `0x0FE3A`, invokes type 2 at `0x0FE43`, loads the
selected ordinary or level-18 scene and installs its script through
`0x0FE4A`-`0x0FEA2`. Its selected-scene loader at `0x114E0` first submits
GSND 0 and waits the literal 15 ms at `0x114EB`. The caller then clears the
target page, invokes type 1 at `0x0FECE`, waits 10 ms at
`0x0FED5`-`0x0FEDE`, and submits `0xA6` at `0x0FEE0`-`0x0FEEA`.

Scene teardown has the same nested boundary. Number's helper at `0x0F490`
submits GSND 0 and waits 15 ms before the outer stop at `0x0F326`; Word's
corresponding pair is `0x11640` then `0x0FFBE`.

This corrects the older interpretation of `0x0FE2D` as an ordinary board
painter. Word's ordinary board presenter is instead
`0x0A356 -> 0x10A66 -> 0x108C2`.

## Shared Wipe boundary

The Number/Word image loaders at `0x1FCE8` and `0x1EB04` dispatch byte-
isomorphic effect callbacks. Their type-2 and type-1 branches copy the target
in eight-pixel strips. After each strip they call Number `0x20DE2` or Word
`0x1FBFE`; those routines repeat their delay helper only by the configured
inter-strip count. The initialized count is zero (`DS:493C` / `DS:4E2C`), read
at Number `0x1FC02` and Word `0x1EA1E`. The loader therefore remains a blocking
draw, not a scheduler job, and normal gameplay/scene callbacks cannot advance
inside it.

The five Number ZMBV files in `analysis/number-live/scenes/original` begin
before the same selected-scene branch. Their reference-control executables
alter only the completion predicates and selected-scene index; the Wipe,
resource loader, SCPT owner, and clock remain the production code. The shared
host sample interval is `31250 / 2190197` seconds.

`tools/audit_number_cartoon_transition_capture.py` classifies the completed
transition states in `analysis/number-live/scenes/transition-report.json`:

| Number scene | Stable cyan samples | Sample-span seconds | Stable white samples | Sample-span seconds |
|---:|---:|---:|---:|---:|
| 0 | 43 | 0.613529 | 6 | 0.085609 |
| 1 | 43 | 0.613529 | 4 | 0.057072 |
| 2 | 44 | 0.627797 | 6 | 0.085609 |
| 3 | 43 | 0.613529 | 5 | 0.071340 |
| 4 | 44 | 0.627797 | 6 | 0.085609 |

Each close contains one torn refresh; each open contains one or two; and four
first actor paints contain one more. All five first stable scene states match
native exactly. Native uses the measured 43/43/44/43/44 covered-sample counts,
opens onto Number's cleared white target, holds for the 10 ms score boundary,
and advances the scene only on the following 10 Hz callback.

The six Word ZMBV files in `analysis/word-live/scenes/original` begin before
the selected-scene branch, so they also contain the real type-2 close, covered
resource load, type-1 open, cleared page, and first 10 Hz scene callback. The
reference-control patches alter only predicates and the selected-scene index;
they do not alter the production Wipe, resource loader, scene owner, or clock.
`tools/audit_word_cartoon_transition_capture.py` classifies the transition in
`analysis/word-live/scenes/transition-report.json`:

| Word scene | Stable cyan samples | Sample-span seconds | Stable black samples | Sample-span seconds |
|---:|---:|---:|---:|---:|
| 0 | 42 | 0.599261 | 7 | 0.099877 |
| 1 | 44 | 0.627797 | 6 | 0.085609 |
| 2 | 43 | 0.613529 | 8 | 0.114145 |
| 3 | 41 | 0.584993 | 6 | 0.085609 |
| 4 | 42 | 0.599261 | 6 | 0.085609 |
| 5 | 42 | 0.599261 | 8 | 0.114145 |

Every Word close contains one torn DOS refresh; each open contains one or two;
and three first actor paints contain one more. Together with the corresponding
Number samples above, these are single-buffer presenter artifacts, not
completed logical states. Native preserves the requested no-flicker contract:
it atomically presents the recovered completed close, cyan
(`0x2cc687e766578183`), upward-from-bottom open, and the game-specific white or
black target (`0xf75309bcac42bb83` for Word). Word's bank-specific timers
reproduce the 41–44 captured samples. The VM is installed at tick zero behind
cyan, the Wipe opens onto the cleared target page, the 10 ms score boundary
completes, and only the next 10 Hz callback paints scene tick 1. All eleven
first stable scene states match native exactly. The organically completed-board
source pixels are still established by the headless final-chew test rather
than these predicate-forced live boards.

## Native lifecycle and gates

Both runtimes snapshot the completed 320x200 board before replacing its
logical page, initialize the selected real SCPT scene, and expose it only
through the close/covered/open sequence. Keyboard, translated-character,
pointer, common Alt-dispatcher, cheat-menu, gameplay-job, and scene-clock input
are frozen until the blocking presentation completes. A host update spanning
the final seven-tick chew transfers only its post-terminal remainder into the
Wipe; Word's new scene remains at tick zero until its first post-open 10 Hz
callback.

Fixed-seed headless gates lock source-buffer identity and disposal, all five
completed full-frame states, Number's five and Word's six measured covered
timers, input freeze, scene-clock freeze, the white/black tick-zero targets,
exact 99/100 ms first-callback boundary, and direct-versus-spanning final-chew
equivalence. They also require
the 10 ms final hold, Music/device-gated stream-38 start, the pre-load GSND-0
cutoff of completion stream 15 and executable-proven 15 ms minimum before the
synchronous measured resource-load span, preservation of channel 0, and two
exact resident GSND-0 retirements at teardown separated by 15 ms/745 generated
samples without a chip reset.
Number's current fixed-seed native hashes are:

`e571a6555ee4012d, 2cc687e766578183, 2cc687e766578183,
cda121acfd56f70b, c8dbeff5c5b238b0`.

Word's current fixed-seed native hashes are:

`2003da8702d7be2b, 2cc687e766578183, 2cc687e766578183,
0c5c7de5097a6fee, f75309bcac42bb83`.
