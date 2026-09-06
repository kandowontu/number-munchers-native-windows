# Word Munchers scripted-scene audit

The executable/thread/resource recovery remains headless, using the preserved
`WM.RES`, both Word graphics archives, and
`analysis/WM-unpacked-image.bin`. A later controlled, muted DOSBox-X pass adds
lossless live visual/cadence evidence for all six original cartoons. No live
audio claim is made.

## Selector and resource routing

The scene selector at image `0x114DD` accepts indices 0–5. At `0x114F2` it
indexes the six bytes at `DS:228C`, which are exactly `1,2,3,4,5,6`, and passes
that bank ID to the loader at `0x11663`. The selector then dispatches to scene
setups `0x116F0`, `0x11736`, `0x1177C`, `0x117C2`, `0x11808`, and `0x1184E`.
The level-18 branch at `0xFE5B` explicitly selects index 5; the ordinary path
selects one of the shuffled indices 0–4. Selection does not advance the
ordinary cursor. Teardown at `0xFFAD` advances and wraps it; this also happens
after special scene 5, so level 18 consumes the current shuffled slot.

Each setup copies one terminated thread-order table from the data segment and
passes the selected SCPT, display-specific graphic, and frame count to the
common setup at `0x11894`. The exact mappings are:

| Index | Bank | Setup | Thread table/order | SCPT | Logical VGA / CGA graphic | Frames |
|---:|---:|---:|---|---:|---|---:|
| 0 | 1 | `0x116F0` | `DS:2292` — `6,7,5,8,4,0,1,2,3` | 2000 | 2001 / 2000 | 15 |
| 1 | 2 | `0x11736` | `DS:22A6` — `3,4,0,1,2` | 2002 | 2003 / 2002 | 17 |
| 2 | 3 | `0x1177C` | `DS:22B2` — `6,4,3,5,1,0,2` | 2004 | 2005 / 2004 | 22 |
| 3 | 4 | `0x117C2` | `DS:22C2` — `0,3,4,1,2` | 2006 | 2007 / 2006 | 11 |
| 4 | 5 | `0x11808` | `DS:22CE` — `0,1,2,5,3,4,6` | 2008 | 2009 / 2008 | 15 |
| 5 | 6 | `0x1184E` | `DS:22DE` — `5,2,3,0,1,4` | 2010 | 2011 / 2010 | 21 |

Every order is a complete permutation of its SCPT's thread table. The native
state remains expressed with the odd VGA logical IDs; `GameAssets` resolves
the even CGA partner only at the asset boundary.

## Scene callback audio

The loader at `0x11663` selects archive type 5 (`ADLI`) when the AdLib-device
flag at `DS:637E` is set and type 6 (`PSND`) otherwise. Teardown at `0x116AD`
releases the same per-scene bank. Loading sets `DS:1FAC`; the callback at
`0x100F0` adds 3 to every opcode-`0D` event while that flag is active before
calling the common sound dispatcher. Thus scene event `n` selects stream
`n+3` from that scene's own ADLI or PSND bank.

Word and Number Munchers do not have identical resident OPL patch tables.
The Word driver initializes its 256-word patch-pointer table at image
`0x1956`; its stream-15-only key `0x1B` points to `0x1CD0`, whose exact patch
record is:

`F2 32 0C 00 03 1A 0A 80 F0 00 08`

The native decoder now requires an explicit Word sound profile for these
resources. This both adds key `0x1B` and preserves the other Word-specific
pointer-table differences instead of silently applying Number's patches.

The post-Wipe event `0xA6` is a separate music-class route. The resident
wrapper masks it to stream 38 only when Music and the AdLib device are active.
Each Word scene bank carries its own channel-9 conductor and channels-1–8
parts; all six are decoded, installed without resetting channel 0, and retired
at teardown. See `CARTOON-SCORE-STATIC-AUDIT.md` for their exact schedules and
newly reached patch records.

## Deterministic terminal evidence

`word_content_static_test` executes every scene until termination through the
shared bounds-checked native VM. On every tick it resolves every live actor and
diagnostic detach-snapshot frame through both the VGA and CGA Word archives. Each bank-scoped
unique callback is decoded twice: once from its ADLI bank and once from its
matching PSND bank with the exact `+3` bias.

| SCPT | Ticks | Callback occurrences | Unique callback values | Detach snapshots | Timeline FNV-64 | Decoded-audio FNV-64 |
|---:|---:|---:|---|---:|---|---|
| 2000 | 102 | 2 | `3,4` | 5 | `b7149f23389c711c` | `44f5089577b226b0` |
| 2002 | 142 | 9 | `2,5,6,7,8,17,19,20,21` | 3 | `cb193a534abc16b0` | `4c3d256761fa07be` |
| 2004 | 133 | 12 | `1,2,5,9,17,22` | 5 | `5b3c131e43ff1cc1` | `1eab910f11ccc1ff` |
| 2006 | 106 | 7 | `0,1,18` | 2 | `ad060b02d054e261` | `949ba26a364e3224` |
| 2008 | 110 | 4 | `2,10,11,12` | 5 | `3fe87bce9f8c8960` | `064bf84f87655de8` |
| 2010 | 251 | 12 | `13,14,15,16` | 7 | `9bc9f6d0f4e0645b` | `bf431e7cb260f5f7` |

All six finish validly at synchronization signal 99. Together they emit 46
callback occurrences and cover event values 0–22; repeated values remain
bank-specific. The timeline hash includes every tick's signal, callback list,
and complete live/baked actor state. The audio hash includes each unique
event's decoded duration, timestamped OPL writes, and PC-speaker writes from
both device resources. Detach snapshots are VM diagnostics, not permanent
sprites redrawn by the retained presenter.

The integrated Word presenter locks every internal frame before VM
termination. The FNV-64 accumulator folds the one-based scene tick and
complete 320×200 retained framebuffer. It covers opaque PCXF records,
continuation sheets, dirty-region erasure/repaint, opcode-`0E` detach, overlap,
positive-edge saturation, and same-tick removal through the real host
renderer:

| SCPT | Internal ticks hashed | Native retained-framebuffer FNV-64 |
|---:|---:|---|
| 2000 | 1–101 | `216013727f6b146d` |
| 2002 | 1–141 | `0664474ce3e4cd74` |
| 2004 | 1–132 | `ac74cee9fca9def0` |
| 2006 | 1–105 | `51ee06e104dba5d` |
| 2008 | 1–109 | `f6e7b5b7919e4b07` |
| 2010 | 1–250 | `6e54c08189e15fdc` |

## Lossless live framebuffer gate

The six muted ZMBV captures under `analysis/word-live/scenes/original` contain
1,463, 1,754, 1,756, 1,478, 1,470, and 2,572 decoded 640×400 frames at exactly
`2190197/31250` frames per second. `tools/audit_word_scene_capture.py` verifies
the codec/size, reconstructs the logical 320×200 VGA surface, hashes every
original and native frame with SHA-256, and compares run-length-encoded stable
states in order. The captures establish a 10 Hz cartoon callback clock; this
is separate from the 29.1375 Hz gameplay/job clock.

DOS exposes occasional partially painted single-buffer refreshes. Native keeps
the requested no-flicker double-buffer contract and presents only complete
frames. The exact gate therefore compares completed stable states, not the
original's transient dirty refreshes. Every DOS-presented distinct stable
state matches native exactly and every matched capture state has zero
nonuniform 2× blocks:

| SCPT | Native distinct runs | DOS-presented runs | Exact live/native runs |
|---:|---:|---:|---:|
| 2000 | 88 | 87 | 87 |
| 2002 | 104 | 103 | 103 |
| 2004 | 78 | 77 | 77 |
| 2006 | 70 | 69 | 69 |
| 2008 | 66 | 65 | 65 |
| 2010 | 186 | 185 | 185 |

That is 586/586 DOS-presented distinct stable framebuffer runs at exact pixels.
The final native run in each row starts on the callback that publishes signal
99. The original scene owner tears down from that signal before presenting the
resulting internal framebuffer; the headless regression suite independently
requires signal 99 on each such run. The complete machine-readable evidence is
`analysis/word-live/scenes/report.json` (`all_scenes_exact: true`).

The live comparison exposed the former stateless renderer's central error.
Scene records are opaque rectangles, including black pixels. Opcode `0E`
removes an actor from dirty updates without erasing its current pixels. A later
dirty rectangle clears to black and redraws only still-active actors, so
detached art can be punched out as another actor crosses it. Native now keeps
that retained framebuffer instead of rebuilding a permanent chronological
list of whole baked sprites. It also reproduces the original clipper's
right-edge saturation and the last scene-only title-DAC mappings reached by
the captures.

## Parity boundary

This closes the interpreter/thread/asset/audio-resource and live VGA
framebuffer/cadence gates for all six Word scenes. The integrated presenter
additionally headless-tests the
host-game trigger and teardown lifecycle, original tick counts, every visible
full-frame timeline, retained dirty painter, all
five shuffled ordinary selections, the level-18 special selection, and
ADLI/PSND callback delivery; see
`WORD-RUNTIME-PRESENTATION-AUDIT.md`. The same gates cover all six stream-38
scores, the 10 ms post-Wipe start boundary, and teardown. Six additional
native-resolution recordings close every CGA cartoon state at exact pixels;
see `CGA-CARTOON-LIVE-AUDIT.md`. Synchronized audible comparison and physical
PC-speaker/output behavior remain separate open rows. The same six VGA captures now close the Wipe's completed
close/cyan/upward-open/black states, bank-specific covered cadence, and
black-to-first-tick boundary in
`analysis/word-live/scenes/transition-report.json`; an organically completed
live source board remains tracked separately because the reference-control
captures force the completion predicate on fresh boards.
