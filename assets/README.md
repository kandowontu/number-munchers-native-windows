# Asset catalog

All assets came from the supplied Number Munchers and Word Munchers VHDs; no
replacement artwork or generated imagery is used.

## Inventory

- `original/` contains exact copies of `NM.RES`, `NMCGA.RES`, `NCGA.RES`, both MECC GEM fonts, both logos, product metadata, and the original readme.
- `ripped/raw/` contains all 91 archive payloads plus the three extraction manifests:
  - `NM`: `CONF` ×1, `DATA` ×2, `GSND` ×1, `PSND` ×1, `ADLI` ×5, `SCPT` ×5
  - `NMCGA`: `BTMP` ×16 and `PCXF` ×21
  - `NCGA`: `BTMP` ×17 and `PCXF` ×22
- `ripped/vga/` and `ripped/cga/` contain lossless PNG conversions of every PCX sheet and every BTMP frame, including the origin-only Muncher life and Troggle records, plus contact sheets and geometry manifests. The catalogs contain 282 VGA and 302 CGA frame PNGs with no unresolved sentinels.
- `word/ripped/raw/` contains all 98 Word Munchers archive payloads plus three extraction manifests: 22 common `WM.RES` entries and 38 entries from each graphics archive.
- `word/ripped/vga/` and `word/ripped/cga/` contain all 20 sheets and 225 resolved frame PNGs per display family, plus contact sheets and geometry manifests.
- `ASSET-MANIFEST.json` records relative path, size, and SHA-256 for all 1,336 cataloged files, including 595 Word Munchers artifacts.

The native executable embeds both games' exact VGA archives, CGA archives,
game-data archives, fonts, configurations, the Word list, logos, and Number
product metadata as Windows `RCDATA`. `GameAssets` selects a game/display
pair and decodes graphics from those bytes at runtime; no loose PNG or DOS
executable is required. `analysis/WORD-ASSET-BRIDGE.md` records the exhaustive
Word-side native gate.

## Decoded formats

The MECC resource container begins with a data offset and a zero-based type count. Each 8-byte type descriptor holds a four-character tag, zero-based entry count, and record-table offset. Records are 16 bytes: resource ID, payload offset, payload size, and flags.

`PCXF` data is PCX with RLE encoding. The extractor handles 8-bit VGA, packed 2/4-bit CGA, and planar 1-bit images. Two-bit MECC CGA PCX headers contain red-only placeholder triples, so the converter renders their indexes through the actual BGI mode-1 black/light-cyan/light-magenta/white hardware palette. `BTMP` records associate a sheet ID with inclusive `y1, x1, y2, x2` crop coordinates. Origin-only sentinel records use measured fixed tiles: 36×39 for the final three Muncher life poses, and 45×40/30 for Troggle arrival/normal poses. The raw records remain preserved and every derived rectangle is marked in the geometry manifest.

The `.GFT` files use the GEM font header and offset table. Glyphs are bit ranges in a single row-major 1-bit strip, using the font's byte stride and height. The port renders directly from these original files.

## Reproduce

```powershell
python .\tools\extract_fat12.py ".\original-media\Number Munchers.vhd" ".\extracted\disk" --manifest ".\analysis\disk-manifest.json"
python .\tools\extract_mecc_res.py ".\extracted\disk\NMUNCH\NM.RES" ".\extracted\resources\NM"
python .\tools\extract_mecc_res.py ".\extracted\disk\NMUNCH\NMCGA.RES" ".\extracted\resources\NMCGA"
python .\tools\extract_mecc_res.py ".\extracted\disk\NMUNCH\NCGA.RES" ".\extracted\resources\NCGA"
python .\tools\convert_graphics.py ".\assets\ripped\raw\NMCGA" ".\assets\ripped\vga"
python .\tools\convert_graphics.py ".\assets\ripped\raw\NCGA" ".\assets\ripped\cga"
python .\tools\extract_fat12.py ".\original-media\Word Munchers.vhd" ".\extracted\word-munchers" --manifest ".\analysis\word-disk-manifest.json"
python .\tools\extract_mecc_res.py ".\extracted\word-munchers\WM\WM.RES" ".\assets\word\ripped\raw\WM"
python .\tools\extract_mecc_res.py ".\extracted\word-munchers\WM\MCGA.RES" ".\assets\word\ripped\raw\MCGA"
python .\tools\extract_mecc_res.py ".\extracted\word-munchers\WM\CGA.RES" ".\assets\word\ripped\raw\CGA"
python .\tools\convert_graphics.py ".\assets\word\ripped\raw\MCGA" ".\assets\word\ripped\vga"
python .\tools\convert_graphics.py ".\assets\word\ripped\raw\CGA" ".\assets\word\ripped\cga"
python .\tools\audit_word_list.py ".\extracted\word-munchers\WM\WLIST.BIN" --output ".\analysis\word-list-audit.json"
.\tools\build_asset_manifest.ps1
```
