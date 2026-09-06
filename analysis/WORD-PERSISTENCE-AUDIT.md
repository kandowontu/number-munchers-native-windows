# Word Munchers native persistence audit

The original configuration mapping comes from the static load/save routines at
image `0x0cb72` and `0x0cc84` and the validated 636-byte `WM.CFG` layout. The
native implementation deliberately does not alter that preserved source file.
The state round-trip gate is headless. A later muted original run forced the
supplied DOS configuration read-only and captured the blocking write alert;
`analysis/word-live/options-gaps/report.json` compares that page exactly while
the preserved `WM.CFG` is restored byte-for-byte afterward.

## Native storage contract

On Windows the port stores Word state at:

`%LOCALAPPDATA%\NumberMunchersNative\word-settings.txt`

If Local AppData cannot be resolved, the fallback is
`word-munchers-settings.txt` in the current directory. The text format starts
with `WORD_MUNCHERS_NATIVE_SETTINGS 1` and stores:

- difficulty and all twenty selected-sound flags;
- sound, music, PC-speaker, and joystick-enabled flags;
- joystick-calibrated state and left/right/up/down thresholds;
- quoted password and hint strings; and
- the active ordered Word Hall, including quoted names and scores.

The loader parses into temporary values and rejects an unsupported signature or
version, non-Boolean flags, difficulty outside 0..7, out-of-range thresholds,
invalid calibrated ordering, a password over 10 characters, a hint over 50,
overlong Hall names, unusable sound
selection, excess Hall capacity, unsorted Hall scores, over-maximum scores, or
trailing data. No runtime field is changed until the complete file validates.

## Save points

Native Word state is saved after accepted content changes, Hall deletion, Hall
admission/name entry, password clearing or replacement, joystick enable toggle,
completed calibration, and the three audio toggles. Cancelled editor drafts are
not saved except for the original Set Password return-value quirk: after the
password field has been accepted, cancelling the hint restores both strings but
still rewrites the unchanged configuration (`0x0F7DE`, `0x0F83A`, `0x0FC55`).
Escape from the first password field does not write. Re-entering Word from the
shared launcher reconstructs the runtime from the persisted values and
reconfigures the board generator before play.

The original wrapper at image `0x0FCE7` also checks the result of the physical
configuration write. Failure reaches the common blocking alert at `0x07E21`
with exact strings `DS:1F54`, `DS:1F67`, `DS:1F7D`, empty `DS:1F90`, and
`DS:0370`: `Configuration file`, `cannot be written, it`, `is write protected`,
an empty line, and `    Press any key.`. Its saved rectangle is
`(60,65)..(260,135)` with a white saved-screen outline; visible text begins at
x=68 on baselines 74/84/94/119, with the fourth source row deliberately empty.
Logical color 3 maps to VGA white and CGA light cyan. Native now detects both
open and flushed-write failure, freezes the initiating page beneath this alert,
blocks pointer/shortcut input, consumes one keyboard event, and then reveals the
already-computed destination without leaking a paired character.

This wait is nested inside the resident Alt+M/P toggle call. Those two original
branches rejoin the selector-state controller only after the save wrapper
returns, whereas Alt+S returns directly. A failed M/P save must consequently
consume a later acknowledgment while retaining the cartoon or Demo, then apply
the original shortcut's cartoon-skip/Demo-exit continuation. Native explicitly
defers that continuation until dismissal; its synthetic controller event cannot
dismiss the alert that caused the deferral.

An exhaustive wrapper-xref check also closes the return-value boundary. The
four calls to `0x0FCE7` are at `0x0FC5E`, `0x0FC9C`, `0x1254D`, and
`0x13856`. Every caller discards AL; none branches on the false result after
the wrapper has shown its alert. Native is therefore correct to continue to
the caller's already-computed destination after acknowledgment rather than
inventing a failure-only page branch.

## Headless gate

`munchers_app_headless_test` redirects persistence to a process-specific file
in the system temporary directory. It writes a non-default two-sound/difficulty
configuration, all audio flags, calibrated joystick thresholds, password/hint,
and a two-entry Hall, constructs a fresh Word runtime, reloads the file, and
requires exact state plus a configured board core. It then deletes the temporary
file. It also accepts an exact 50-character hint and rejects a 51-character
one without mutating the fresh runtime. Test builds disable ordinary per-user
persistence by default, so the suite cannot read or overwrite the user's actual
settings. A forced directory-as-file failure separately locks the initiating-
page pixel snapshot, VGA password-editor fixture hash `6328558d63e2cf11`, CGA
Options fixture hash `5cd9d111ff029539`, input ownership, post-dismissal
destination, and Word Alt+M's alert-before-cartoon-skip ordering. The exact
original Options-row-4 failure fixture independently matches native at
`1a6ebbc842249539` with zero differing pixels.

This closes native state survival and source-file isolation. It does not claim
byte identity with `WM.CFG`; the separate versioned file is an intentional
Windows-port policy that preserves the supplied DOS artifact.
