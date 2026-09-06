# Password subsystem static audit

The original Options-password paths are recovered from
`NM-unpacked.ndisasm` at image offsets `0xEA21`–`0xEDF0`.

- `0xECC4` bypasses the challenge only when `DS:598A` begins with zero.
  Otherwise it displays the saved hint, accepts a password attempt, retries
  after the two-line incorrect-password message, and returns false on Escape.
- `0xEC38` folds the stored and entered bytes before comparison, making the
  challenge case-insensitive. After every equal byte, `0xEC73` increments the
  index and `0xEC7F` tests the byte at `DS:5989 + index`. That address is the
  byte just compared at `DS:598A + old_index`, so the terminating NUL is itself
  compared before the loop stops. The candidate must therefore have exactly
  the stored length; a matching prefix with trailing characters fails.
- The line-editor records at `DS:0F7C` (saved password) and `DS:0F92`
  (challenge) both point to the legal-character specification `DS:0F74 =
  "!-~"`: every printable ASCII byte except Space, with a ten-character cap.
  The hint record at `DS:0FA8` instead points to `DS:0F78 = " -~"`, includes
  Space, and has a fifty-character cap.
- `0xEA21` edits temporary copies. Escape leaves both saved fields unchanged.
  Accepting an empty password clears the hint without presenting the hint
  editor; accepting a non-empty password proceeds to the hint field and then
  commits both values. There is a persistence side effect after that first
  acceptance: a later hint Escape restores both strings at `0x0EBF7`, but the
  result set at `0x0EB56` remains true, so caller `0x0EFE2` rewrites the
  unchanged configuration. First-field Escape still performs no write.

## Challenge drawing and blocking error

The challenge painter at `0xECE0`–`0xEDCF` is now traced through its shared
GEM primitives rather than inferred from the strings alone.

- `DS:0F6B` supplies the ordinary `Options` title/frame. `DS:1026` is drawn at
  `(5,32)`, and the large-font ten-character line editor is seeded at x=80.
  This produces the inclusive box `(80,50)..(238,69)`, text origin `(85,56)`,
  and cursor baseline used by native.
- The saved hint at `DS:5764` is drawn before the input loop with centered-text
  flag `0x40` at baseline 115. `DS:103A` supplies the exact two-line footer.
- A failed comparison at `0xEDB9` calls the generic blocking message routine
  at image `0xAF6A`, not an inline status label. Its source at `DS:0FF2` is the
  six-logical-line string containing a leading blank line, `That password is
  not correct.`, another blank line, `Please try again.`, and two terminating
  line feeds. Flags `0xC3` request the full-width centered, saved-background,
  double-border modal.
- The same flags-`0xC3` primitive draws the already captured Set Content
  validation frames. Their exact hashes calibrate its nine-pixel line step and
  centered sizing. Both the password string and captured Value Range message
  contain five line feeds/six logical rows, so the password modal occupies the
  same `(0,67)..(319,131)` rectangle, with cyan outer/inner borders and large
  white text at baselines 81 and 99. The modal covers the previously drawn hint in that
  band and restores it after dismissal.
- `0xAF88` tests the modal flag. The nonzero branch at `0xAF8E` calls the
  wrapper at image `0xB0E6`; that wrapper pushes `DS:0864 = 1B 0D 20 00` and
  calls the accepted-key waiter at `0xB389`. The overlay therefore accepts
  only Escape, Enter, Space, or a left release rewritten to Space. Letters,
  digits, arrows, and right release `0xFD` remain blocked.
- The accepted acknowledgment is consumed. `0xEDCF` then restarts a fresh,
  empty line editor. Escape dismisses only this overlay and reaches that retry;
  a later editor Escape is what returns false from the password gate.

Word is byte-isomorphic at its corresponding gate. Failure at `0x0FA41`
passes flag 1 and `DS:1E10` to the generic modal at `0x0FA49`. Its wrapper at
image `0x0BCC7` supplies `DS:14D4 = 1B 0D 20 00` to the waiter at `0x0BF6A`,
so it has the same exact keyboard and pointer contract.

Native now implements and headlessly tests the exact prompt composition,
blocking error geometry, exact accepted acknowledgment table, unsupported
physical key-pair retention, left-release acknowledgment, inert right release,
fresh retry, Left/Backspace deletion, Home clear, inert Right/End/Delete/Up/Down,
the password-versus-hint Space distinction,
10/50-character limits, exact-length case-folded comparison, cancellation,
empty-password clearing, and a settings round trip at both maximum field
lengths. The hint-cancel rewrite split and the original failed-write alert are
covered in `NUMBER-PERSISTENCE-STATIC-AUDIT.md`. No DOSBox or visible native
window is used for this coverage.
