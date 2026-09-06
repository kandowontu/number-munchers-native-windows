# Troggle protected-cell retry static audit

This audit compares the two preserved executable images with the shared
native Troggle endpoint rule. It is static and headless: no supplied
executable, DOSBox process, native GUI, or audio device was started.

## Executable control flow

Number Munchers' endpoint function begins at image `0x0904D`. After applying
the prior-cell effect and selecting the species direction, it recomputes the
candidate cell at `0x09096`. An in-bounds protected cell is detected at
`0x0912F`-`0x09137`; the `random(0,2)` call at `0x09139`-`0x09145` chooses a
left or right turn. If the cell remains protected, `0x09170` jumps directly
back to `0x09096`.

Word Munchers is instruction-equivalent. Its endpoint function begins at
`0x09C1A`, recomputes at `0x09C63`, checks the protected byte at
`0x09CFC`-`0x09D04`, chooses the turn at `0x09D06`-`0x09D12`, and jumps from
`0x09D3D` back to `0x09C63`.

Neither loop contains a counter, comparison, fallback move, or retry literal.
It ends only when the chosen direction reaches an unprotected cell or exits
the board. Native previously stopped after 32 attempts and returned without
starting a move. Both runtimes now use the executable's unbounded loop.

## Reachable termination proof

The recovered safe-zone limit tables allow at most two simultaneous jobs, and
each active job owns one distinct interior cell. Consequently an endpoint can
have at most two of its four directions protected in valid play.

Each turn uses `BorlandRand() % 2`. Its result is bit 16 of the post-update LCG
state, and all future values of that bit depend only on the low 17 state bits.
The Number headless gate exhausts all 131,072 low-17-bit states, four initial
directions, and every zero-, one-, or two-protected-direction mask. Every
reachable configuration terminates; the longest chain is 24 random draws
(including low-17 state 12,296, initial direction 1, directions 0 and 1
protected).

That exhaustive result explains why the former 32-attempt cap did not alter a
valid board even though it contradicted the executable. To make the structural
difference testable, both game gates also install a deliberately unreachable
three-protected-neighbor fixture. Borland state 57,606 requires 37 turns to
find its sole open direction. Each native controller must consume all 37 draws
and begin that move; the former guard failed this regression after attempt 32.

## Evidence boundary

This closes a source-level control-flow mismatch and prevents a dormant native
fallback from becoming observable if surrounding invariants change. It does
not supply new live pixels, physical input/audio evidence, or promote either
game to 1:1. The remaining partial rows in `PARITY.md` and `WORD-PARITY.md`
remain open.
