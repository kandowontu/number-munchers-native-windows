# Word Munchers native asset bridge

This gate is entirely headless. It embeds and parses the preserved Word
Munchers data without launching DOSBox, `WM.EXE`, or a native game window.
It establishes that the shared native resource layer can now select either
game's source archives; it does **not** claim Word gameplay or visual parity.

## Embedded source set

`GameAssets` now accepts a `GameAssetSet` as well as the VGA/CGA mode. The Word
selection binds the original `WM.RES`, `MCGA.RES`/`CGA.RES`, `BIT8X8.GFT`, and
`BIT5X8.GFT`. The distinct 350-byte Word `PRODUCT.PF`, `WLIST.BIN`, and
`WM.CFG` are embedded as provenance-checked resources. The already embedded
MECC logos are shared because the supplied Number and Word copies are
byte-identical. No loose file or DOS executable is read by the native resource
path.

The Word CGA resolver implements its archive's exact parallel IDs:

- logical actor IDs 1006–1011 select CGA 1000–1005;
- logical scenes 2001/2003/…/2011 select CGA 2000/2002/…/2010;
- continuation sheets 3003/3005 select CGA 3002/3004;
- logical 6001/6003/6005/6009/6051/6053 select their even CGA partners.

These mappings differ from Number Munchers' 2013–2021 and 3013–3021 scene
ranges, so selection remains game-specific at the asset boundary.

Logical BTMP/PCXF 6001 is also active board art, not merely an exhaustively
decoded archive entry. Its 20 records are indexed directly by target sound and
painted transparently at `(260,0)` under the resident 6003 Hall/gameplay VGA
palette; logical 6000 supplies the CGA partner. Lossless `/e/ as in tree` and
`/e/ as in bell` boards gate the tree and bell exemplars at exact pixels.

The runtime now honors the decoded source reference too. Six BTMP records in
logical animations 2003/2005 name continuation PCXF 3003/3005; the former
Word presenter decoded that field but still loaded the logical base sheet.
`WORD-SPRITE-STATIC-AUDIT.md` records the affected frames and their
pixel-for-pixel headless regression gate.

## Exhaustive native checks

`word_content_static_test` now constructs both Word asset modes and verifies:

- exact embedded sizes and FNV-64 fingerprints for all common archives,
  graphics archives, fonts, configuration, product record, logo, and
  `WLIST.BIN`;
- successful parsing of `WM.RES`, both Word GFT fonts, and both graphics
  archives;
- all 20 VGA sheets and all 20 CGA sheets at their source dimensions;
- every one of the 225 VGA and 225 CGA BTMP descriptors, including source
  sheet references and the shared origin-only Muncher/Troggle dimensions;
- confinement of every decoded CGA PCX pixel to black, light cyan, light
  magenta, or white;
- deterministic aggregate source/pixel/geometry snapshots:
  VGA `0x0586883564bda4a5`, CGA `0x473a005451f3cb0c`.

Word's separate executable primitive-color table and complete CGA startup,
title, Options, Hall, and board compositions are closed at the static/headless level
in `WORD-CGA-STATIC-AUDIT.md`.

The archives contain two deliberate geometry oddities that are retained
rather than silently rewritten. BTMP 6051/6053 store a transposed 200×320
descriptor over a 320×200 interstitial PCX, while CGA BTMP 6000 frames 12 and
16 extend one/four pixels beyond its 205-pixel sheet. The whole-screen 605x
pages are selected as PCXF images, not sprite crops. Tests name and lock these
source records so future renderer work cannot mistake them for normalized
geometry.

## Remaining gate

This closes the native archive/font/ID-selection prerequisite only. Exact
scene thread ordering, per-scene graphics, and ADLI/PSND callback dispatch now
have their separate headless gate in `WORD-SCENE-STATIC-AUDIT.md`. Word page
composition, gameplay state, question copy,
persistence, and live original Hall comparison remain open and require their own rendered
evidence. Hall admission/name/replay flow is now separately static/headless-
gated in `WORD-FEEDBACK-TERMINAL-STATIC-AUDIT.md`. The title/board input host,
all-six-scene trigger/painter/audio-resource lifecycle, and shared-launcher
return lifecycle are independently headless-gated in
`WORD-RUNTIME-PRESENTATION-AUDIT.md`.
