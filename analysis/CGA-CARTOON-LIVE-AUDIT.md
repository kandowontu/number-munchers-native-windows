# Native-resolution CGA cartoon live audit

This audit compares the retained native cartoon framebuffers with minimized,
muted DOSBox-X recordings of the supplied Number Munchers and Word Munchers
programs in hardware CGA mode. It closes the live CGA cartoon slice only; it
does not establish parity for every gameplay or parameterized UI composition.

## Evidence and method

`tools/capture_original_cga_sequence.ps1` mounts a scene-selector variant that
changes only the completion predicate and selected scene, starts DOSBox-X
minimized with `nosound=true`, records its native 320x200 ZMBV surface, stops
recording, and closes only the process created by that capture. The launcher
uses a per-capture PID file so concurrent emulator processes cannot be mistaken
for the owned process.

`MUNCHERS_DUMP_NUMBER_SCENE_CGA=1` and
`MUNCHERS_DUMP_WORD_SCENE_CGA=1` select the CGA asset/palette path in the
integrated headless timeline gates. The resulting 320x200 PPM files include
every internal scene tick. VGA and CGA timeline hashes are pinned separately,
so a CGA correction cannot silently change an established VGA result.

`tools/audit_cga_scene_capture.py` pins every accepted AVI by path, SHA-256,
decoded frame count, exact `14980681/250000` frame rate, ZMBV codec, 320x200
dimensions, and audio-stream count. It decodes every video frame without
scaling, run-length encodes the exact RGB surfaces, and compares them with the
native timeline in order. The AVIs contain DOSBox's emulated PCM stream, but
host playback was disabled throughout capture; this is a visual audit, not a
synchronized audible comparison.

## Number Munchers result

| Scene | Pinned source | Decoded frames | Ordered source/native runs | Distinct source states |
|---:|---|---:|---:|---:|
| 0 | `original-number-scene-0-full.avi` | 1,730 | 181/182 | 118/118 |
| 1 | `original-number-scene-1-full.avi` | 1,756 | 116/116 | 108/108 |
| 2 | `original-number-scene-2-full.avi` | 1,940 | 135/135 | 124/124 |
| 3 | `original-number-scene-3-retry.avi` | 1,696 | 165/165 | 97/97 |
| 4 | `original-number-scene-4-full.avi` | 1,755 | 118/118 | 116/116 |

Scenes 1-4 are exact ordered comparisons. Scene 0's first callback completes
between two CGA scanouts after the blocking Wipe, so the original recording
does not sample tick 1 in its first timeline slot. Ticks 2-182 occur in exact
order, and tick 1's exact framebuffer occurs when the animation repeats;
therefore every one of its 118 distinct presented RGB states is independently
observed. `analysis/cga-live/number-scene-0-report.json` records this narrow
presentation-equivalence exception instead of reporting a false 182/182
ordered match. The other reports are `number-scene-1-report.json` through
`number-scene-4-report.json` in the same directory.

The live comparisons exposed the original CGA presenter's packed-byte
destination alignment, source-crop offset, complete-byte width truncation,
three measured offscreen records, scene-1 right-edge copy boundary, scene-2
retained trailing strip, and scene-3 negative-coordinate truncation. These
rules are restricted to CGA cartoon presentation and have independent native
timeline hashes.

## Word Munchers result

| Scene | Pinned source | Decoded frames | Ordered source/native runs | Distinct source states |
|---:|---|---:|---:|---:|
| 0 | `original-word-scene-0-full.avi` | 1,347 | 87/87 | 87/87 |
| 1 | `original-word-scene-1-full.avi` | 1,593 | 103/103 | 102/102 |
| 2 | `original-word-scene-2-full.avi` | 1,189 | 77/77 | 71/71 |
| 3 | `original-word-scene-3-full.avi` | 1,336 | 69/69 | 61/61 |
| 4 | `original-word-scene-4-full.avi` | 1,339 | 64/64 | 57/57 |
| 5 | `original-word-scene-5-retry.avi` | 2,314 | 183/183 | 183/183 |

All 583 DOS-presented Word CGA state runs and all 561 per-scene distinct RGB
states match native at every pixel. Reports are
`analysis/cga-live/word-scene-0-report.json` through
`word-scene-5-report.json`.

The Word captures independently confirm the packed four-pixel-byte destination
and width rules. Scene 4 adds one measured boundary: logical graphic 2009,
record 4 begins at x=324 and is completely offscreen in CGA, rather than using
the VGA clipper's x=319 saturation; its following x=316 record is visible.

## CGA close/load/open transition result

The same eleven recordings also contain the production board-to-cartoon Wipe.
`tools/audit_cga_cartoon_transition_capture.py` pins the complete transition
around each scene and writes
`analysis/cga-live/cartoon-transition-report.json`. Every transition has:

- a stable source board followed by exactly three or four intermediate close
  samples;
- an exact full-screen light-cyan covered page held for 34 or 35 CGA samples;
- exactly three or four intermediate open samples;
- an exact full-screen black scene page held for five to eight samples; and
- the pinned initial-paint inventory followed by an exact native scene state.

The covered-page sample spans agree with the already measured bank-specific
native timers within the two-frame endpoint-phase bound imposed by the
59.922724 Hz CGA capture clock. Scenes 1-4 in Number and all six Word scenes
hand off to native tick 1. Number scene 0 hands off to tick 2, matching the
separately proven fact that tick 1 completes between scanouts and is observed
later during the repeated animation.

Always-on headless gates now instantiate the actual Number and Word CGA
board-to-cartoon presenters. They pin all five complete native transition
framebuffers, loader timing, frozen input, scene installation, the black
opening page, and the zero-tick handoff separately from the established VGA
hash arrays. Original partial Wipe refreshes remain capture evidence rather
than being reintroduced as native flicker.

## Repeatable gate

After building the two test executables, the complete live audit can be rerun
with:

```powershell
$env:MUNCHERS_DUMP_NUMBER_SCENE_CGA = '1'
$env:MUNCHERS_DUMP_NUMBER_SCENE_DIR = "$PWD\analysis\number-live\cga\scenes\native"
.\build\NumberMunchersRenderTests.exe
$env:MUNCHERS_DUMP_WORD_SCENE_CGA = '1'
$env:MUNCHERS_DUMP_WORD_SCENE_DIR = "$PWD\analysis\word-live\cga\scenes\native"
.\build\MunchersAppTests.exe
Remove-Item Env:MUNCHERS_DUMP_NUMBER_SCENE_CGA,Env:MUNCHERS_DUMP_NUMBER_SCENE_DIR,
    Env:MUNCHERS_DUMP_WORD_SCENE_CGA,Env:MUNCHERS_DUMP_WORD_SCENE_DIR
foreach ($scene in 0..4) {
    python .\tools\audit_cga_scene_capture.py --game Number --scene $scene `
        --output ".\analysis\cga-live\number-scene-$scene-report.json"
}
foreach ($scene in 0..5) {
    python .\tools\audit_cga_scene_capture.py --game Word --scene $scene `
        --output ".\analysis\cga-live\word-scene-$scene-report.json"
}
python .\tools\audit_cga_cartoon_transition_capture.py
```

The current accepted result is eleven valid scene reports plus one valid
aggregate transition report: ten exact ordered scene comparisons, Number scene
0's explicitly measured first-refresh exception, and all eleven exact
completed close/load/open paths.
