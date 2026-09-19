# Munchers Collection for Windows

A native Windows preservation port of three classic MECC games:

- Number Munchers
- Word Munchers
- Super Munchers

All three games run inside one Win32 application. DOSBox and the original DOS
executables are not required.

## Download

Download the ready-to-run Windows build from the
**[1.1 release page](https://github.com/kandowontu/number-munchers-native-windows/releases/tag/v1.1)**.

The release contains a single self-contained executable:
`Number.Munchers.exe`. No installer or adjacent asset folder is needed.

Version 1.1 SHA-256:

```text
0852AF6CBF8536E16F1D24D07373B9E0453F339072C55553D923AFB26D82B413
```

## Highlights

- Native Win32/C++20 implementation—not an emulator wrapper
- Shared launcher for Number, Word, and Super Munchers
- Original VGA artwork, CGA mode, fonts, sound effects, music, and PC-speaker audio
- Original gameplay rules, scoring, difficulty settings, Hall of Fame, demos,
  cartoons, and Super Munchers missions
- Mouse, keyboard, XInput gamepad, and WinMM joystick support
- Integer-scaled windowed rendering and borderless fullscreen
- Atomic double-buffered presentation to eliminate black flicker
- Buffered AdLib playback designed to avoid underruns on a busy desktop
- Per-user saves under `%LOCALAPPDATA%\NumberMunchersNative`; bundled source
  configuration files are never modified

## Controls

| Action | Input |
|---|---|
| Move | Arrow keys; `8`, `A`, or `I` for up; `4` or `J` for left; `6` or `K` for right; `2`, `M`, or `Z` for down; or click a square |
| Munch | Space, or click the occupied square again |
| Pause / resume | Enter or right mouse button |
| Leave the current screen or game | Escape |
| Navigate menus | Arrow keys, Home/End, number keys where shown, Enter, or mouse |
| Fullscreen | **Alt+Enter** |
| Level select / cheat menu | **Ctrl+Alt+F1** |
| Toggle sound effects | Alt+S |
| Toggle music | Alt+M |
| Switch AdLib / PC speaker | Alt+P |

### Gamepad

Xbox-compatible controllers work automatically; no in-game calibration is
needed. The first connected XInput controller takes priority. If none is
connected, the original configurable WinMM joystick path remains available.

| Action | Gamepad input |
|---|---|
| Move | D-pad or left stick |
| Munch | A or X |
| Leave / back | View/Back (Select) or B |
| Pause / confirm | Menu (Start) or Y |

The left stick uses a dead zone and resolves diagonal input to its strongest
cardinal direction. Menu buttons fire once per press; held movement repeats at
a controlled rate.

The developer menu provides level selection and game-specific testing helpers.
The fullscreen and developer-menu shortcuts respond once per physical key
press, so holding the keys does not repeatedly toggle them.

## Running the game

Launch `Number.Munchers.exe` normally for VGA graphics.

To use the original CGA presentation, run:

```powershell
& '.\Number.Munchers.exe' --cga
```

Leaving a game returns to the collection launcher. Only the launcher's Exit
item closes the application.

## Building from source

Requirements:

- Windows
- CMake 3.20 or newer
- Ninja
- A MinGW-w64 compiler with C++20 support

Build and test with:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable is written to `build\Number Munchers.exe`. MinGW builds link
the C++ runtime statically.

## Verification

Version 1.1 passes all seven automated release tests. They cover:

- game state, rendering, animation, timing, and audio
- Word Munchers content and board rules
- Super Munchers content and mission scripts
- the shared launcher and input lifecycle
- XInput mappings, dead zone, repeat timing, and button edge handling
- all 31 embedded runtime resources
- the production executable's Windows-system-DLL-only dependency boundary

The three parity ledgers contain 62 verified rows with no open rows:

- [Number Munchers parity](analysis/PARITY.md)
- [Word Munchers parity](analysis/WORD-PARITY.md)
- [Super Munchers parity](analysis/SUPER-PARITY.md)

Useful focused reports include the
[black-flicker audit](analysis/PRESENTATION-FLICKER-AUDIT.md),
[audio-streaming audit](analysis/AUDIO-STREAMING-AUDIT.md),
[Super Munchers live-presentation audit](analysis/SUPER-PRESENTATION-LIVE-AUDIT.md),
and [self-containment audit](analysis/SELF-CONTAINMENT-AUDIT.md).

## Project layout

| Path | Contents |
|---|---|
| `src/` | Native game controllers, renderer, audio, assets, and Win32 shell |
| `assets/` | Embedded source resources and decoded artwork |
| `tests/` | Headless content, behavior, rendering, and artifact tests |
| `analysis/` | Reverse-engineering notes, disassemblies, and parity evidence |
| `tools/` | Extraction, conversion, capture, and verification utilities |
| `third_party/ymfm/` | Vendored BSD-3-Clause YM3812 emulator used for AdLib playback |

The public repository intentionally omits several gigabytes of raw capture
videos and temporary decoded frames. Those files are not needed to build or
run the application. See the
[public-repository evidence boundary](analysis/PUBLIC-REPOSITORY-EVIDENCE.md)
for details.

## Preservation notes

The port was reconstructed from the supplied game media using static binary
analysis, resource extraction, controlled reference captures, and deterministic
native tests. The original DOS executables are audit inputs only and are not
included in or loaded by the Windows application.

The Munchers games and their original assets remain the property of their
respective rights holders. This preservation project is not affiliated with or
endorsed by MECC.
