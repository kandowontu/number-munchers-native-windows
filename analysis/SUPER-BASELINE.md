# Super Munchers source baseline

Date: 2026-08-28

## Acquisition

- Source archive: `original-media/Super Munchers DOS EN.zip`
- Size: 281,876 bytes
- SHA-256: `B6EF148C59CE8C2F88157897CA3814EE74AC4F90D9537847347DE6B2F3B5142D`
- The pinned archive is the preservation source. The native product must not
  read the Downloads copy or any extracted working directory.

## Executable audit image

- Packed `SM.EXE`: 94,707 bytes,
  `76897E49B96D829315B98AC49A1A7FB9E4CF4D7F58E09957DE1CD23C26D8C73`
- Packer: LZEXE 0.91
- Audit-only unpacked image: `analysis/SM-unpacked.exe`, 194,608 bytes,
  `E18D89F77EB8AA1486DC6832112EFF8969F50F0193570167E508D9D38B4E6624`
- Relocated load image: `analysis/SM-unpacked-image.bin`, 181,808 bytes,
  `2BF165458E9DCFB62EF030E0D3ED689917A570DAD0AC4AE736C559CD738EDAB5`
- Static listing: `analysis/SM-unpacked.ndisasm`, 73,404 instructions,
  3,607 calls, and 212 interrupts.
- The DOS executable is an audit source only and is deliberately not embedded
  in the native Windows executable.

## Resource inventory

`SM.RES` contains 38 entries:

| Type | Entries | Role |
|---|---:|---|
| CATG | 6 | category/rule records |
| CONF | 1 | embedded 2,292-byte configuration |
| DATA | 2 | instructions and mission/controller text |
| EGAT | 1 | exact 256-byte VGA index-to-header-palette map |
| INDX | 6 | packed category-membership indexes |
| GSND | 1 | common sound bank |
| PSND | 5 | PC-speaker banks 21-25 |
| ADLI | 5 | AdLib banks 21-25 |
| SCPT | 5 | mission scripts 2022-2030 |
| WLST | 6 | category word lists |

`SMCGA.RES` and `SCGA.RES` each contain 19 BTMP animation tables and 23
PCXF sheets, totaling 336 animation records per graphics mode. The VGA PCX
files intentionally omit a 256-color palette trailer. `SM.RES/EGAT:1` provides
the decoded header-palette bridge, while live pixels prove that the presenter
keeps the original eight-bit indexes and displays them through a resident DAC.
The full resident ramp rule, including its repeated `+4/+6` entries and
Muncher-block alias, is independently consistent with every measured startup,
board, chew and Troggle color. Both the decoded sheets and all 336 BTMP crops
per mode are exact aggregate-hash gates.

All non-executable distribution files are retained byte-for-byte under
`assets/super/original`. The nine assets required by the native runtime are
also embedded as PE resources and verified in an isolated artifact test:
`SM.RES`, `SMCGA.RES`, `SCGA.RES`, both fonts, both startup logos, `SM.CFG`,
and `PRODUCT.PF`.

## Recovered game contract

- Six categorization sets: Animals, Famous Americans, Food and Health,
  Geography, Music, and Odds 'n' Ends.
- The normal board is 6 columns by 5 rows. The player eats entries matching
  the rule shown at the top.
- Twenty consecutive correct answers create a transformation cell. Entering
  it changes the player into a Super Muncher that can destroy Troggles.
- Answer Vision (`V`) freezes the action and removes wrong answers while the
  player is transformed.
- Five recurring mission controllers cover the map, key, password, vertical
  flight, and Frankentroggle laboratory sequences.
- Movement, Space/mouse chewing, Enter pause, Escape quit, and the original
  Alt sound/music controls are present in the recovered instruction stream.

This baseline records provenance and static evidence. Behavioral parity is
tracked separately in `SUPER-PARITY.md`; extraction alone is not treated as a
completed port.
