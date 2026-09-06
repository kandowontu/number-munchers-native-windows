# Native release audit

Date: 2026-08-28

## Completion gates

- Number Munchers: 30/30 parity rows verified, 0 partial, 0 missing.
- Word Munchers: 15/15 parity rows verified, 0 partial, 0 missing.
- Number uninterrupted attract reconciliation: valid; 1,363 ordered exact
  pages, with all 237 source-only and 71 native-only residuals classified.
- Word uninterrupted attract reconciliation: valid atomic double-buffer parity;
  621/657 eligible pages match exactly in order, and every remaining source or
  native page is classified as a measured partial repaint or completed atomic
  composition.
- Native presentation intentionally excludes captured single-buffer partial
  writes and scanout splices, closing the reported black-flicker defect without
  changing completed logical framebuffers.
- Super Munchers: 17/17 audit domains verified, 0 partial, 0 missing.
- The production AdLib streamer queues approximately 123.6 ms across twelve
  512-sample headers and uses an MMCSS `Audio` refill worker, closing the
  reported underrun/choppiness risk while preserving exact PCM and schedules.

The device-dependent completion boundary is defined in
`PLATFORM-BOUNDARY-AUDIT.md`: the executable owns exact framebuffers,
timestamped digital audio/event streams, synthesized PCM, and Win32/WinMM input
samples. Physical mixer, speaker, controller, monitor, and room properties are
optional machine QA.

## Automated release result

The final three-game working tree passes 6/6 headless checks:

- `game_render_state_test`
- `word_content_static_test`
- `super_content_static_test`
- `super_script_static_test`
- `munchers_app_headless_test`
- `production_artifact_self_containment_test`

The packaged `dist/Number Munchers.exe` was then tested directly by
`ProductionArtifactTests.exe` from an isolated temporary directory. It is
resource-complete and imports Windows system components only; no GUI entry
point, foreground window, or audio device was invoked by that verification.

## Artifact

- Path: `dist/Number Munchers.exe`
- Size: 5,851,370 bytes
- SHA-256: `D706C67B460496C795C98B2B1D803E249011AEDA46F03BAC070334DA315828AB`
- Contents: shared Number Munchers / Word Munchers / Super Munchers launcher
  and all three native game runtimes, with every required resource embedded.
