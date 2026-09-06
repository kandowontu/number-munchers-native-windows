# Gameplay scheduler static audit

This audit was performed from the supplied Number Munchers and Word Munchers
executables without launching either DOS program. It closes the ordering
boundary between player movement/chewing and the recurring Troggle, safe-zone,
and Demo jobs. It does not replace the live-frame comparisons still marked
open in the parity ledgers.

## Shared record model

Both executables use a fixed sixteen-slot array of 20-byte scheduler records.
The first four words are the job ID, callback selector, actor state, and public
event mask; the remaining fields include the recurrence timing and callback
payload. Number's insertion routine at image `0x166CF` rejects a duplicate ID,
copies a new record into the first slot whose ID is `-1`, and updates the
highest occupied slot. Word uses the byte-isomorphic shared engine.

The gameplay templates are:

| Job | Number template | Word template | ID / selector / mask |
|---|---:|---:|---|
| Player | `DS:0674` | `DS:12E6` | fixed ID 4, selector 2, mask 2 |
| Safe zone | `DS:0688` | `DS:12FA` | dynamic ID 12+, selector 6, mask 2 |
| Troggle slot | `DS:06CC` | `DS:133E` | dynamic ID 1+, selector 1, mask 2 |
| Fixed controllers | `DS:06E0`–`071C` | `DS:1352`–`138E` | IDs 8–11, selectors 7–10, mask 1 |
| Demo controller | `DS:0730` | `DS:13A2` | fixed ID 7, selector 5, mask 2 |

Number's board initializer at `0x09789` installs safe-zone jobs through
`0x08CC8`, player ID 4 through `0x08B14`, Troggle slots through `0x08EEF`, the
four fixed records, and finally the optional Demo record. Word's initializer at
`0x0A356` performs the same sequence through `0x09895`, `0x096E1`, and
`0x09ABC`. Because insertion uses the first free slot, this is also the
physical scan order:

`safe zones -> player -> Troggles -> fixed controllers -> Demo`

The mask-2 gameplay pass skips the fixed mask-1 controllers, leaving the
effective callback order `safe zones -> player -> Troggles -> Demo`.

## Due-job and callback boundary

Number's common scanner decrements the current record at `0x165E3`, reloads a
due recurrence at `0x16602`–`0x16618`, and invokes the callback at `0x16654`.
Word does the same at `0x15340`, `0x1535C`–`0x15372`, and `0x153AE`.
The scan then continues forward, so a callback can disable, replace, or create
records that occur later in the same public tick. It cannot retroactively run a
new job in a slot already visited on that tick.

## Ordering within the Troggle records

Waiting, warning, movement, dwell, overlap, and rearm are states of the same
persistent selector-1 record; they are not separate controller and actor jobs.
Number's initializer at `0x08FAA`–`0x09037` installs IDs 1 through N in order.
The dispatcher at `0x09E8A` reads the current scheduler ID and selects its
state handler, while `0x092DD` changes that same record back to arrival state
without deleting or reinserting it. Word's shared engine and Troggle routines
are byte-isomorphic at their corresponding addresses.

Consequently a lower-ID waiting record that becomes due must consume its edge
and coordinate draws before a higher-ID actor consumes an endpoint dwell draw.
The hosts formerly batched every live actor before every waiting/warning slot,
which reversed PRNG ownership for mixed-phase ties. Both runtimes now walk IDs
1 through N once and dispatch whichever phase each record currently owns. A
lower record that rearms a later victim also permits that later record's newly
installed countdown to receive its normal decrement when the scanner reaches
it; a rearmed earlier/current record is not visited twice.

This makes a coalesced Windows presentation update observably different from
subtracting the entire frame duration from independent native timers. Both
native runtimes now accumulate fractional original-clock time and dispatch one
complete public tick at a time in recovered slot order.

The following boundaries are regression gates:

- During a seven-tick wrong-answer chew, a due safe-zone job is visited on all
  seven ticks. The player terminal on tick seven changes board execution before
  the later Troggle slots, so those slots receive only the first six ticks.
- A Troggle endpoint reached while the player is still in chew state 5 is
  harmless. An adversarial 2.25-tick update keeps the actor protected through
  the first endpoint, resolves its saved correct answer on the second tick, and
  preserves the final quarter tick.
- If the player's movement terminal returns to standing on an occupied cell,
  collision setup occurs in the player slot, but the scan still visits later
  Troggle and Demo slots on that same tick. The selected biter remains at
  collision age zero until the next public tick. In the preserved Number Demo
  replay, that tail is what leaves the controller reload at PRNG call 271;
  aborting the tick at the player slot incorrectly moves the second-board
  initializer to call 270.
- A Troggle endpoint collision likewise installs the auxiliary bite record
  without aborting the common scan. Later Troggle records and the Demo record
  still run on that public tick, while the selected biter remains protected
  from its pre-existing dwell callback. Every non-selected Troggle occupying
  the collision cell is put in inert state 6, so a later collocated actor
  cannot move or consume PRNG even though later unrelated jobs still dispatch.
- In a mixed-phase tie, lower job ID 2's waiting callback consumes the two
  edge-entry draws before higher job ID 3 consumes its endpoint dwell draw.
  The paired Number/Word regression fixes this boundary at calls 186–188 of
  seed 62853, yielding the left/row-1 entry and 95-tick native dwell.
- A movement terminal peeks the shared input ring in the same callback and can
  start its next keyboard or pointer action immediately. When that successor
  starts a chew, elapsed time from earlier in the tick is not charged to the
  new seven-tick player job; only later public ticks advance it.
- The seventh chew callback restores standing but does not inspect the ring.
  A keyboard or pointer byte queued behind the chew starts on the next public
  player callback. The live `Space+J` reference and paired native tests lock
  this one-callback difference from movement.
- When the seventh correct-chew callback posts selector-100 completion and
  constructs a direct next board, the generation serial aborts the invalid old
  pass. Only complete post-terminal ticks and the fractional frame remainder
  can age the newly installed jobs.

`game_render_state_test` and `munchers_app_headless_test` exercise these cases
for Number and Word, including the protected-chew collision, safe/player/
Troggle terminal order, mixed Troggle phases, endpoint-collision scan tail,
inert non-selected collision actors, mixed input FIFO movement/chew boundary,
preserved Demo call 271, and final-chew next-board remainder.
