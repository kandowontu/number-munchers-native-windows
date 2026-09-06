# Super Munchers parity ledger

Date: 2026-08-28

Status meanings: **verified** is protected by an exact automated gate;
**recovered** has authoritative source evidence and working native coverage but
still lacks one final parity gate; **open** has not reached measured parity.

| Domain | Source evidence | Native status | Status / remaining audit |
|---|---|---|---|
| Distribution provenance | pinned ZIP and per-file SHA-256 inventory | preservation copy exists | **verified** |
| Executable structure | LZEXE-unpacked MZ, relocated image, full ndisasm and strings | audit-only inputs excluded from product | **verified** |
| RES archive layout | exact 38/42/42 entry inventories | `ResArchive` reads all three archives | **verified** |
| VGA/CGA palettes and sheets | `EGAT:1`, header palettes, and 23 PCXF sheets per mode | embedded decode, dimensions, palette confinement and exact hashes | **verified** |
| BTMP tables | 19 tables / 336 records per mode | every record, continuation-sheet selection, rectangle, source pixel and overhang sentinel is aggregate-hash gated in both modes | **verified** |
| Fonts/logos/config/product data | original files and hashes | embedded, isolated and byte-exact | **verified** |
| Category rules and word lists | CATG/WLST/INDX plus image `0x10A2F..0x110BD` | strict parser covers 3,771 words, 157 rules, every membership, eligibility, shuffle, negation, both 60-record pools, retry quota and PRNG state | **verified** |
| Normal board controller | DATA instructions and disassembly | exact 30-cell generation, scoring/lives, 20-answer transformation, placement, Answer Vision and progression | **verified** headlessly |
| Player, safe zones and Troggles | BTMP plus shared scheduler and actor blocks | exact movement/chew callbacks, collision gate, entry/movement/exit jobs, trails, transformed attack and safe-zone lifecycle | **verified** headlessly; visible composites are included in the presentation task below |
| Five missions | SCPT 2022-2030, constructor lists, FE handlers, DATA 2 and art | bounded VM, all FE handlers, cadence/input, progression, per-bank audio, five timeline hashes and 17 intro/result framebuffer hashes | **verified** headlessly |
| Audio | GSND/PSND/ADLI 21-25 and static call sites | 98 streams decode to the exact aggregate hash; mission/gameplay dispatch, ownership, replacement, toggles and continuous OPL output are gated | **verified** |
| Attract demo | image `0x085E1..0x08739`, `DS:03B6`, title scheduler | exact 450-tick launch, level draw, 30-tick first action, 15..44 reload, wrong munch, clockwise scan and external-key exit | **verified**; see `SUPER-DEMO-STATIC-AUDIT.md` |
| Title, Information, Options and gameplay presentation | DATA strings, title/Hall sheets, render routines, menu disassembly and controlled live VGA/CGA captures | startup, title, menus, ordinary VGA/CGA boards, chew poses and Troggle overlap have exact full-frame gates | **verified**; see `SUPER-PRESENTATION-LIVE-AUDIT.md` |
| Save/config/Hall/password | 2,292-byte SM.CFG and image `0x0CF8B..0x0D51F` | lossless options/masks/ShortStrings/calibration/seven Hall tables, transactional persistence, password hint, Hall admission and erasure | **verified** headlessly |
| Launcher/lifecycle | collection launcher contract | third entry launches native Super in the existing window and terminal Escape returns to the collection | **verified** headlessly |
| Host controls | collection Alt+Enter and Ctrl+Alt+F1 contract | shared fullscreen dispatcher precedes game input; Super level-select and launcher-return lifecycle are headless gated | **verified** |
| Final artifact | nine Super runtime assets embedded; DOS EXE excluded | final `dist` executable passes the isolated resource/system-DLL gate | **verified**; SHA-256 `D706C67B460496C795C98B2B1D803E249011AEDA46F03BAC070334DA315828AB` |

## Completion boundary

All seventeen audit domains are verified. The release suite combines static
source inventories, deterministic controller/audio timelines, exhaustive
resource decoding, exact 320x200 live-frame oracles, launcher/hotkey lifecycle
checks, and an isolated inspection of the packaged executable. Physical
speaker, controller, mixer and monitor characteristics remain optional machine
QA under `PLATFORM-BOUNDARY-AUDIT.md`; they are not missing program behavior.
