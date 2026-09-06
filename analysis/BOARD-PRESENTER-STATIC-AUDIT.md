# Ordinary board presenter static audit

This audit uses the preserved unpacked Number and Word executable images and
their offline disassemblies. It does not launch DOSBox, either original game,
the native GUI, or an audio device.

## Recovered call chains

The ordinary board transition is not Demo-only. Number's fresh-game
initializer at `0x089AD` and high-level action 5 at `0x12A49` both reach the
next-board initializer at `0x09789`. Its final call at `0x0988A` targets the
presenter at `0x1175A`. That presenter performs:

1. transition setup through `0x10FBF`;
2. effect type 2 through `0x10FD2` at `0x1175F`-`0x11768`;
3. the mode-dependent screen clear at `0x1176A`-`0x11793`;
4. effect type 1 through the same routine at `0x11793`-`0x1179D`; and
5. the dynamic board/HUD painter at `0x115B6` from `0x1179F`.

Word is instruction-equivalent. Fresh-game initializer `0x0957A` reaches the
content initializer at `0x082BD` and next-board initializer at `0x0A356`;
high-level action 5 at `0x120EA` also calls `0x0A356`. Its last call at
`0x0A457` targets presenter `0x10A66`, which performs the same type-2 Wipe,
mode-dependent clear, type-1 Wipe, and then calls dynamic board/HUD painter
`0x108C2` at `0x10AAB`.

The Wipe loaders at Number `0x10FD2` and Word `0x102DA` select their paired
PCXF transition resources and dispatch the shared effect engines at
`0x1FCE:0008` and `0x1EB0:0004`. The existing lossless Number Demo capture
measures this close/clear/open/painter lifecycle as nine host-visible
intervals at `31250 / 2190197` seconds each. Because all ordinary initial and
next-board paths above end in the same presenters, the lifecycle applies to:

- a normal first board after the menu or pre-game Information;
- a post-Hall Replay Yes first board;
- a direct next board after an ordinary completion; and
- the next board after cartoon teardown.

The earlier attribution of Word `0x0FE2D` as an ordinary board painter was
wrong. It is the every-third-board, non-Demo cartoon-entry branch inside
selector `0x0FD74`; its own type-2/type-1 calls prove a separate board-to-
cartoon transition, not the logo-to-board or ordinary next-board presenter.

## Native correction and regression gates

Both native runtimes now preserve the outgoing 320x200 framebuffer, execute
the shared nine-interval Wipe/painter sequence, and discard the saved pixels
when the final full board becomes visible. Preserving the pixels is required:
the native initializer updates logical board state immediately, while the DOS
presenter closes the pixels that were already visible before painting the new
board.

Keyboard, translated-character, pointer, common Alt-dispatcher, and cheat-menu
input are frozen for the user transition, matching the synchronous DOS effect.
Fresh safe-zone, player, and Troggle jobs are also frozen. If the terminal
callback occurs partway through a coalesced Windows update, only the
post-callback remainder advances the Wipe; it cannot age a new board job.

Focused Number and Word tests lock all nine full-frame hashes for a fixed-seed
title-to-board case, exact frame/state termination, source-buffer disposal,
the input gate, direct advancement, and post-cartoon advancement. The Number
timeline is:

`dcf1b974b9f9916f, 2cc687e766578183, 2cc687e766578183,
8887db592e3c9ca1, b1a8f948642db583, b1a8f948642db583,
b1a8f948642db583, 28e9f37d90d3cd0b, ded8296b313be0bb`.

The Word timeline is:

`ed78e0058b63147a, 2cc687e766578183, 2cc687e766578183,
8887db592e3c9ca1, b1a8f948642db583, b1a8f948642db583,
b1a8f948642db583, 181949d64685500d, 5fc8caa941191c3b`.

These user-transition hashes are deterministic native/static regression
evidence. No user-play DOS transition was opened or captured under the current
no-window constraint. The distinct board-to-cartoon close/load/open path is now
implemented and statically gated; its unmeasured host cadence remains an
explicit boundary in `CARTOON-PRESENTER-STATIC-AUDIT.md`.
