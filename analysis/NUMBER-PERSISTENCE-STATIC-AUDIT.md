# Number Munchers persistence and failed-write static audit

This audit uses `NM-unpacked-image.bin`, `NM-unpacked.ndisasm`, and device-free
native tests only. No DOSBox, original executable, native game window, or audio
output path was opened.

## Original save wrapper

The Options callers pass `DS:586C` to the configuration wrapper at image
`0x0F074`. After copying the live password, hint, joystick, content, and Hall
state into the configuration record, `0x0F0C0` calls the physical writer at
`0x0BC9:021D`. A false result reaches the common blocking alert through
`0x0F0C9..0x0F0E7` and makes the wrapper return false.

The five alert pointers are exact:

| Pointer | Text |
|---|---|
| `DS:1151` | `Configuration file` |
| `DS:1164` | `cannot be written, it` |
| `DS:117A` | `is write protected` |
| `DS:118D` | empty string |
| `DS:0370` | `    Press any key.` |

The common routine at image `0x07A91` saves inclusive rectangle
`(60,65)..(260,135)`, paints it black, and draws those strings from x=68 at
baselines 80, 90, 100, 115, and 125. It selects logical color 3, which the
device table at `DS:0464` maps to VGA white and CGA light cyan. The deliberately
empty fourth string creates the larger 15-pixel break before the final prompt.

After painting, `0x07BE9` calls the raw keyboard reader. One event ends the
wait: Escape is retained, while every other keyboard result is normalized to
Space at `0x07BF1..0x07BF9`. The saved region is then restored. No mouse callback
or accepted-key table participates, so pointer input cannot dismiss the alert.

## Password cancellation save boundary

The editor at `0x0EA21` sets its accepted result at `0x0EB56` when the password
field completes. If Escape later cancels the hint, `0x0EBB2` sets the cancellation
flag and `0x0EBF7..0x0EC18` restores both original strings, but it does not clear
the accepted result. The Options caller tests that result at `0x0EFE2` and still
calls `0x0F074`. Escape from the first field never sets the result and performs
no save. The rewrite after hint cancellation is therefore intentional original
control flow even though the stored bytes are unchanged.

## Native policy and gate

The Windows port intentionally keeps versioned settings and six Hall lists in
separate per-user files rather than modifying preserved `NM.CFG`. Both native
writers now detect file-open and flushed-write failure and route it through the
same alert. Because Win32 input is asynchronous, the native implementation
captures the complete initiating 320x200 frame before the caller mutates or
leaves that page, freezes schedulers and shortcuts, ignores pointer input, and
consumes one keyboard event without leaking its paired character into the
already-computed destination.

The alert also preserves a less obvious resident-shortcut ordering boundary.
Alt+S returns directly after its save attempt, but Alt+M and Alt+P rejoin the
selector-state controller only after their toggle/save call returns. If that
save blocks at `0x07BE9`, the later acknowledgment is consumed first and the
original Alt event reaches Demo-exit or cartoon-skip logic only after the alert
restores its background. Native therefore defers M/P's internal controller
continuation while the alert is visible; it never uses that continuation as the
acknowledgment. Alt+S dismisses to the unchanged live state with no continuation.

The wrapper's Boolean return does not add another failure branch. Its five
direct calls are at `0x0EFEB`, `0x0F029`, `0x131BF`, `0x14C72`, and
`0x157D0`; all discard AL after `0x0F074` returns. The blocking alert and its
normal caller continuation are therefore the complete observable contract.

`game_render_state_test` proves the first-field no-write versus hint-cancel
unchanged rewrite, settings and Hall failure routes, initiating-page retention,
pointer/cheat blocking, paired-character ownership, destination restoration,
Alt+S direct return versus Alt+P's post-alert Demo fallthrough, and exact native
alert compositions:

| Mode | FNV-64 |
|---|---|
| VGA | `ebef0f53026e2fc5` |
| CGA | `79a0c1c085714f2b` |

These hashes are static/native gates. They do not replace a live original
failed-write capture, and no broader 1:1 claim follows from them.
