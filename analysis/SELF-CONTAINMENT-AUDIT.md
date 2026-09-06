# Production executable self-containment audit

This gate inspects the actual `build/Number Munchers.exe` artifact entirely
headlessly. It does not execute the GUI entry point, open a window or audio
device, or launch DOSBox or either original executable.

## Embedded source-data boundary

The production resource script embeds 31 preservation inputs covering all
three games and both display modes:

- Number `NM.RES`, `NMCGA.RES`, `NCGA.RES`, two fonts, two logos,
  `PRODUCT.PF`, and the supplied `NM.CFG`;
- Word `WLIST.BIN`, `WM.RES`, `MCGA.RES`, `CGA.RES`, two fonts, `WM.CFG`, and
  its distinct `PRODUCT.PF`;
- Super `SM.RES`, `SMCGA.RES`, `SCGA.RES`, two fonts, two logos, `SM.CFG`, and
  its distinct `PRODUCT.PF`; and
- the five lossless Number YM3812/DRO reference cues.

`production_artifact_self_containment_test` copies the built GUI EXE into a
new otherwise-empty temporary directory, opens that isolated copy only with
`LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE`, and
checks the size and stable byte fingerprint of every one of those 31 `RCDATA`
records. The fixed fingerprints come from the preserved inputs, not from a
second linker output. The test removes its isolated copy and directory after
the audit.

At runtime, `loadEmbeddedResource` uses `FindResource`/`LoadResource`; the
Number, Word and Super asset owners select only those embedded archive/font/logo IDs.
Configuration saves use separate validated per-user files and do not turn any
original disk, archive, or DOS executable into a runtime dependency.

## PE dependency boundary

The same test parses the isolated copy's DOS header, PE optional header,
section table, and import directory without loading executable code. It
rejects every import except the Windows components used by the Win32 shell,
renderer, and audio host:

- `KERNEL32`, `USER32`, `GDI32`, `SHELL32`, `OLE32`, and `WINMM`; and
- Windows Universal CRT API-set imports named `api-ms-win-crt-*`.

Consequently a regression to dynamic MinGW sidecars such as `libgcc`,
`libstdc++`, or `libwinpthread`, or to any other third-party DLL, fails the
release-mode suite. CMake also retains `-static`, `-static-libgcc`, and
`-static-libstdc++` for the production MinGW target.

## Result and release boundary

The isolated production-artifact gate passes. This proves that the current
release EXE contains the exact source assets for all three games and imports
only Windows components; it does not by itself prove gameplay or pixel/audio
parity. The separate parity ledgers provide that evidence. The final
`dist/Number Munchers.exe` was also passed directly to this isolated gate.
