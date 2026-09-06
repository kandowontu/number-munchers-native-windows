# Native platform parity boundary

This audit defines the reproducible boundary for device-dependent parity. The
port can control the framebuffer, digital audio/event streams, and the Win32 or
WinMM values it consumes. It cannot control analog speaker gain/filtering,
Windows mixer policy, USB/gameport electrical behavior, or the mechanics of a
particular mouse/joystick. Those host properties are not DOS game code and are
not included in the self-contained executable's 1:1 claim.

## Audio boundary

The authoritative outputs are the timestamped YM3812 register writes, PIT
frequency/gate events, synthesized PCM/RIFF bytes, logical device selection,
and event ownership. Static call-site recovery covers every GSND/ADLI/PSND
stream, channel, toggle, replacement, stop, score, scene, and 15/50 ms boundary
in all three games. Headless tests compare decoded OPL schedules, register shadows,
speaker RIFF bytes, delay sizes, continuous-chip ownership, every ordinary
stream 3–15 caller, all cartoon conductors, and all Alt+S/M/P routes. Lossless
captures independently lock five live Number gameplay traces, the full Number
attract-score cycle and PCM envelope, all five Number and six Word scene
timelines, and the audio-bearing controller transitions.

An analog microphone or sound-card recording would measure the user's current
Windows mixer, resampler, output device, amplifier, and speakers in addition to
the program. It is useful environmental QA but cannot be a portable executable
parity requirement. Digital schedules/PCM are the completion gate.

The production WinMM delivery path is also jitter-gated. Following a reported
choppy-output defect, `AUDIO-STREAMING-AUDIT.md` raised continuous queue
coverage from approximately 20.6 to 123.6 ms while retaining 10.3 ms refill
granularity, and registered only the refill worker with Windows MMCSS's
`Audio` task class. This changes neither the authoritative PCM nor its
recovered event schedule.

## Pointer and joystick boundary

The authoritative inputs are Win32 mouse press/release coordinates and WinMM
axis/button samples. The native adapter implements the recovered INT 33h event
types, owner-specific left/right-release translations, fixed-menu rectangles,
accepted-key waiters, board target encoding, shared capacity-nine FIFO,
dominant-axis walking, second-click chew, calibration transaction, integer
thresholds, button priority/edges, pending-sample phase, repeat deadlines, and
feedback tone. Headless tests exhaust every event owner and calibration state,
including modal leakage, capacity, mixed pointer/keyboard order, movement and
chew terminal boundaries, pause/collision resets, diagonals, overdue samples,
repeat bounds, and settings round trips. Live captures provide mouse-driven
menu/gameplay state anchors and an exact original calibration page.

A particular physical joystick's centering/noise is precisely what the
six-point calibration transaction normalizes; it is not another branch of the
game. API-sample parity is the completion gate. The port never grabs, clips, or
disables the system cursor and no test requires a foreground DOSBox window.

## VGA/CGA boundary

The authoritative output is the complete 320x200 framebuffer. Both exact
archives, logos, fonts, palettes, 16-to-4 primitive collapse, CGA indexed-PCX
decode, programmable background, all resource-ID translations, and every BTMP
record are hash-gated. Pinned live VGA captures cover every page family across
Number and Word, while controlled Super captures lock its startup, title,
Options, selection flow, ordinary board, chew and Troggle-overlap pages.
Native-resolution CGA captures cover startup, menus and complete gameplay
boards in all three games, every one of the five Number and six Word cartoons,
and each completed close/load/open Wipe. All ordered scene runs match, with the
single Number scene-0 first-refresh state observed during its repeat. Higher
levels, message values, species, and editor values reuse the same tested
primitives/resources and are exhaustively composed by behavior tests.

Desktop scaling and monitor color response are host presentation concerns. The
application-controlled path is separately proven flicker-free: it composes one
private bitmap and performs one no-erase `BitBlt`, including resize and
Alt+Enter transitions. The 320x200 framebuffer and completed atomic painter
states are the parity gate.

## Result

At these explicit interfaces, Number Munchers, Word Munchers and Super Munchers have complete,
repeatable parity evidence. Physical-device spot checks remain optional machine
QA and cannot invalidate or strengthen a different machine's self-contained
binary.
