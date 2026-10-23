# Restarting

The image has one way to restart the processor from software: `trap
#12`, whose handler at `0x24b28` (vector 44 of the ROM table at
`0x20000`) masks interrupts, runs `FUN_00022172` -- which turns off
the GPT and QSM interrupt sources at `0xfff920` and `0xfffc1a` to
`0xfffc1f`, clears SCCR1 so the SCI stops, and calls `FUN_0002dc72` --
then executes the `RESET` instruction, reloads the VBR, and jumps
through the primary's own reset vector (`0x20004`) to its reset code
at `0x2466e`.  It does not go through the boot ROM's vector at address
4, so the boot code at `0x550`, and the flash checksums it runs, are
skipped.  RSR then shows SYS alone.

Every primary's `trap #12` handler has that shape; the shutdown
routine also stops the PIT (PICR to `0x0042`) and disables both DUART
channels.  `trap #11` masks and shuts down the same way and then jumps
through the boot ROM's vector 43:

| Image | `trap #11` | `trap #12` | Shutdown | Primary reset code |
| ----- | ---------- | ---------- | -------- | ------------------ |
| Z3801A 3543 | `0x1467c` | `0x1468c` | `0x120aa` | `0x141d2` |
| Z3805A 3543B | `0x14974` | `0x14984` | `0x120aa` | `0x144ca` |
| 58503A 3633 | `0x14856` | `0x14866` | `0x12166` | `0x143bc` |
| 58503A 3704 | `0x149ce` | `0x149de` | `0x121f0` | `0x14534` |
| Z3816A 4001 | `0x24b18` | `0x24b28` | `0x22172` | `0x2466e` |
| Z3815A 4010 | `0x24fe0` | `0x24ff0` | `0x22204` | `0x24b36` |
| 58503B 1.01.04 | `0x24a4a` | `0x24a5a` | `0x22246` | `0x245ac` |

The 58503B's `trap #12` also reloads the stack pointer from its
table's first entry (`movea.l (A0),A7`) before jumping through the
reset vector; the others only jump.

The reset code clears RAM above a preserved region at `0x100000` and
restores it -- the τ block, the loop block, the health records -- only
when four things hold: the region's checksum is good, RSR shows
neither an external nor a power-up reset (`RSR & 0xc0` is zero; a
software, halt-monitor or loss-of-clock reset passes), the flag word at
`0x100002` is 1, and, on the 58503A, Z3816A, Z3815A and 58503B, a
settings byte is clear (3633 `0x102270`, 3704 `0x102276`, Z3816A
`0x10262c`, Z3815A `0x102652`, 58503B `0x1029b4`, each tested just
after the first read of RSR).  The settings byte records that the
last start-up found neither EEPROM settings record good: the Z3816A's
loader (`0x42080`) clears it, reads the record at `0x4000c0` and then
the second, rewriting the first from the second when only the second
is good, and sets the byte only when both fail and the ROM defaults
(`FUN_00042448`) are loaded; each of the other images clears and sets
its byte the same way beside its `0x4000c0` load (3633 `0x324f0`,
`0x3251a`; 3704 `0x327b2`, `0x327dc`; Z3815A `0x46f14`, `0x46f3e`;
58503B `0x42aae`, `0x42ada`).  So after a start-up that lost its
settings, the next restart is cold too.  With the checksum good and the flag 0, a
second branch loads the ROM defaults; on those four models that branch
also requires RSR to be exactly SYS, which `RESET` gives.  Anything else is a cold start, which also
pulses PE6.  The checksum is an 8-bit sum stored as sum and
complement, so a zeroed region fails it.  So `:SYSTem:PRESet`, which
clears the flag, restarts into the defaults, and `:SYSTem:PON`, which
zeroes the region, restarts cold.  No console word reaches `trap #12`.

Two actions reach it, and the SCPI handlers that name them, through
six-byte action entries at `0x44c9a` to `0x44cb2` (a word and a
function; see below), are:

| Command | Handler | Action | What it does first |
| ------- | ------- | ------ | ------------------ |
| `:SYSTem:PON` | `FUN_0003ff52`, list at `0x44ca0` | `FUN_000496f8` | zeroes the whole region `0x100000` to `0x100c3b` that a warm restart would restore -- the loop state, the health records, the τ block -- so the restart loads the defaults, as a power-up does |
| `:SYSTem:PRESet` | `FUN_0003ff78`, list at `0x44ca6` | `FUN_000496ca` | writes two settings records to the EEPROM at `0x4000c0` (`FUN_0004206a`, twice through `FUN_0004203e` and `FUN_000408fe`), clears the flag word at `0x100002` and recomputes the region's checksum into `0x100000` |

A handler posts one entry, not a list: `FUN_00039524` sends its
address, an index and a value to the `pllc` queue (id at `0x103d6a`),
and the loop task checks it in `FUN_00046652`, whose messages name it
`pll_scpi: validate_or_doaction()`.  The entry's first byte is a type
and its second the largest index it takes (`setup index > max index`
otherwise).  Type 2, an action, calls the entry's function with the
index and the value.  Type 0 is a numeric parameter record -- the
records the `:DIAGnostic:ROSCillator:TCOefficient` and antenna-delay
setters name are of this type -- which `FUN_00046560`, `validate_num`,
passes to the validator at `+0x0e`, up to four values when `+0x26` is
2 or 3 (`too many in multiple`), and which `finish_scpi_cmd`
(`0x46a54` on) then stores into the cells at `+0x16`, calls `+0x12`
and calls the setter at `+0x1a`.  Type 1, `toggle_change`
(`FUN_00046514`), takes a value no larger than `+0x06` and marks a
change when it differs from the current one in the table at `+0x0c`.
Type 3 does nothing, and any other is `unknown structure type`.
`FUN_00039552` posts a record as `FUN_00039524` posts an entry, both
through `FUN_000394ac`, which then waits for event `0x20`.  The loop
task's drain, `FUN_00046cc8`, run once a pass at the end of its loop
(`0x4b5ee`), takes one message, handles it and sends that event to
the waiting task (`0x1026e2`); while the count at `0x102c21` is
nonzero it instead counts it down a pass at a time and takes no
message, so an action that sets it -- a manual holdover's start
(`0x494da`), bit 5's (`0x49520`) -- holds its sender, and the queue
behind it, for that many passes.  Every
entry from `0x44c70` to `0x44cb2` is type 2.  The `:GPS:POSition`
handler `FUN_0003c268` posts the entry at `0x44c9a`, six bytes before
`:SYSTem:PON`'s, whose function is `FUN_00049da2`.  `PON` is a keyword of the
Z3816A image (`0x5a280`), both 58503A images (`console.md`, "The
58503A image"), the Z3815A's and the 58503B's (`../scpi/`); the Z3801A's image has the same `:SYSTem:PRESet` action,
`FUN_00045cca` (`0x2f66a` passes its list at `0x41392`), and no `PON`,
nor has the Z3805A's.  Owners report that newer firmware accepts
`:SYSTem:PON` and older refuses it, which matches: on 2026-09-27 the
bench Z3801A (3543) and Z3805A (3543B) both refused it.

Owners report that `*TST?` also restarts the receiver, with a minute
of GPS reacquisition after it.  It restarts the loop, not the
processor.  A node's handler pointer sits at byte 18 of its record, so
its node (`0x5cb36`) names `FUN_00039aa8`, which posts the entry at
`0x44c88` -- `FUN_00049d60` -- to the `pllc` queue (id at `0x103d6a`,
created at `0x23204`) that the loop task drains through
`FUN_00046cc8`, and replies with the word at `0x100eae`.  The entry
sets a request flag at `0x102c61` and stores the argument at
`0x102c64`; the loop task's pass sees the flag (`0x4b25c`) and moves
the stage byte to 7, `diag`.  That stage, `FUN_0004ad3e`, is a
sub-state machine at `0x102850`: it sends the GPS task a reset and
waits up to thirty passes for its acknowledgment (`Error in DIAG GPS
RST ack`, `0x4b806`, otherwise), sends a test message and waits for
that (`Error in DIAG GPS TST ack`, `0x4b822`), records the results at
`0x100e88`, runs `FUN_00048b62`, and in its last state compiles the
self-test result with `FUN_00028c56`, clears the flag and returns 1.
On that return the loop task runs `FUN_0004afb6`: loop state cleared,
events 0x2f and 0x35 posted, and the stage byte set to 1, `powerup`
(`0x4b016`).  So `*TST?` resets the GPS engine and sends the receiver
back through warm-up, coarse and fine acquisition -- the "reboot"
owners see; the loop block, the τ block and the aging fit stay in RAM,
and RSR is untouched.

`:SYSTem:LANGuage "INSTALL"` takes a different exit: `FUN_00023036`
executes `trap #11`, whose handler at `0x24b18` masks interrupts, runs
the same `FUN_00022172`, and jumps through vector 43 of the table at
address 0 -- the boot ROM's own table, not the one at `0x20000` --
which enters the installer in the low half of the image.  Those
addresses are the Z3816A's; the Z3801A, Z3805A and 58503A images put
the primary's table at `0x10000` (Z3801A trap #11 handler `0x1467c`).
"The installer" below has the rest.

The monitoring and generic query paths never send `:SYSTem:PRESet`,
`:SYSTem:PON` or `:SYSTem:LANGuage`.  The command table includes
`:SYSTem:PON` as `system_pon` so that `docs/commands.md` shows it.

`smartclock-cli flash` enters the installer to program flash, and
`read-memory` enters it only as a fallback way back from the console;
the daemon calls neither.

## The watchdog

The software watchdog is on (SYPCR `0xcc`) and, after start-up, is
serviced in one place: a routine of the `hmon` task, priority 200, the
highest of the application's tasks (Z3801A `0x2323e`, Z3805A
`0x2437a`, 3633 `0x2345a`, 3704 `0x2369e`, Z3816A `0x338a4`, Z3815A
`0x36664`, 58503B `0x32eaa`; each image's only SWSR writes outside its
start-up code).  The
PIT, 1024 ticks a second, signals `hmon` every ten ticks, and every
112 of those, about 1.1 s, the routine checks heartbeat bytes the
clock, GPS, loop, monitor and spool tasks set (Z3801A block
`0x1009b0`).  All healthy, it resets a countdown to 8; otherwise it
prints `watchdog:` and counts down, and it writes SWSR only while the
count is above zero.  So the kicks stop about nine seconds after a
task stops reporting, and the hardware resets the unit 8 s after the
last: SYPCR `0xcc` sets SWE, SWP (÷ 512) and SWT 00, a ratio of 2¹⁸
(MC68331UM table 4-4), and the time-out is that ratio over the EXTAL
frequency, 32.768 kHz (`README.md`, SYNCR; PITR `0x0008` gives the
1024 ticks a second above at the same reference).  Every image writes
the same SYPCR and PITR.  That reset goes through the boot code and its checksums, and
with the region good and the flag set it is a warm start, which writes
the pending exception, fatal message or `Watchdog timeout: clk ... gps
... mon ... pll ... spl ...` to the EEPROM log at `0x4001c0`
(`0x23afc`); a `:SYSTem:PRESet` or `PON` restart clears those records
unlogged.

An exception no handler takes prints `<task>: <name>: PC = ..., SR =
...` and `RESET to recover.` and spins with interrupts masked, which
starves `hmon` and so ends in the watchdog.  The console installs its
own handler (`0x18b6a`), which prints the exception and returns to the
prompt.

The console word `n crash` is the same code in every image, a jump
table on n from 0 to 4 (Z3801A `0x1bd84`; above 4 it does nothing):
0 writes to unmapped `0x700000`, 1 writes a long to an odd address, 2
divides by zero, 3 computes 3.0 − 3.0 and then 0.0 / 0.0 in the
floating-point library (`softfloat.md`), and 4 calls the fatal routine
(Z3801A `0x1ee7e`) with `Force crash from pforth`.  The fatal routine
records the message and, with the byte at `0x103658` set, prints
`FATAL ERROR:` and `RESET to recover.` and sets the interrupt mask to
4 (`FUN_00013378`); then it spins.  The library's invalid-operation
trap, which the start-up code enables (`0x7480` into `0x10372e`, at
`0x1463e`), sends 3's 0.0 / 0.0 through `FUN_00013e36`, which names it
`fp operand error: `, and `0x1ec04` to the same fatal routine.

On the bench Z3801A (3543-A, 2026-10-10) `8 crash` returned to the
prompt, and `0 crash` and `4 crash` each restarted the unit: the port
answered `scpi > ` about 9 s after `4 crash`, and the log gained
`Re-boot` each time.  Its second log (`:DIAGnostic:SLOG:READ:ALL?`)
holds what the warm starts recorded: `Exception: p4th, vector C008, PC
= 0001BDB6, SR = 0010, addr = 00700000` for `0 crash`, a bus error
inside `crash`, and `Force crash from pforth, p4th` for each `4
crash`.

Both end in the watchdog.  An exception no handler claims reaches the
common entry (`0x11924`, every vector the Z3801A's table at `0x10000`
names): it asks for the current task's handler (`0x118fe`: the task's
record at +0x82 when `0x103650` is set, else the default at
`0x10364c`), and with none records the exception at `0x10099e`, prints
it when `0x103659` is set, sets the interrupt mask to 7 (`0x11a1a`)
and spins.  With the PIT shut out, `hmon` never runs again, so the
watchdog fires within 8 s of the last kick -- not after the nine-second
countdown a task that stops reporting would cost.  The console's
handler did not claim `0 crash`'s bus error.  The logged `SR = 0010`
has the supervisor bit clear: the console task runs in user mode, so
the fatal routine's `move.w d1,sr` (`0x13394`, in `FUN_00013378`) is a
privilege violation and takes the same path -- which fits `4 crash`
restarting as fast as `0 crash`, though the violation itself was not
seen: the warm start logged the fatal message, and the SIM registers,
RSR among them, read as zeros through the console.
The console is its own task, `p4th`, created at priority 50 in every
image (Z3816A `0x2b038`, a pSOS `t_create` through `trap #3`, from
`0x23138`), below every task whose heartbeat `hmon` checks -- in the
Z3816A `spoo` 75, `gpsm` 100, `klok` 150, `pllp` 175 and `hmon` 200
(`0x2321a` to `0x23288`, `0x2fd36`, `0x209ae`); with the mask raised,
their priority does not save them.
`master_reset` sends the GPS engine `Cf` in every image (see
`console.md`, "Resetting the GPS engine from the console"), and `clear_nv`
invalidates both EEPROM settings records, the first step of
`:SYSTem:PRESet`, without restarting; the next start-up reloads and
rewrites the defaults.

What the settings records hold, in the Z3816A, is a table at `0x42648`
of the parameter structures (above, "Restarting"): 23 numeric records,
then from `0x426a4` 18 toggles.  `FUN_000421a2` packs their cells into
the record at `0x4000c0` (`Warning: Entire nv register is being
used` when they fill it), and `FUN_000424a6` reads them back at
start-up.  The loop task saves after each structure a handler posts
(`0x46b06`, just after `finish_scpi_cmd`), so a setting is written to
the EEPROM when it is set.  `:DIAGnostic:SYSTem:PSTartup`, which makes
the root task start the console instead of SCPI, is the first toggle
(`0x4314c`, cell `0x102617`): it survives a power cycle.

## The installer

Read from all five images; the bench check of entry and exit is below.

- *Where it lives.*  Below `0x10000` (Z3816A: `0x20000`), in flash the
  installer never erases or writes: the writable range runs from
  `0x10000` (Z3816A: `0x20000`) to `0x7ffff`.  Vector 43 (Z3801A
  `0xa58`) unpacks it into RAM and runs it there, a pSOS system of its
  own with its own SCPI parser, on the host port at the EEPROM's line
  settings (`0x400000`, checksum at +0), or 9600 8N1 if that record is
  bad.  The Oman installer can then override either with the byte at
  `0x302000`; see "The switch byte at `0x302000`".
- *Telling it apart.*  `*IDN?` names a place, not a revision: `Peru`
  on the Z3801A, `Oman` on 58503A 3633, `USA` on 3704 and the Z3816A;
  `:SYSTem:LANGuage?` always answers `INSTALL`.  The Z3815A's and
58503B's is `Vatican`.  Each name is the string before the installer's
`Copyright Hewlett-Packard Co.`, read from the unpacked image: the
record stream "Forced installer entry" describes, at `0x8000` to
`0x128ea` in those two images (`0x8000` to `0x128ce` in the Z3816A),
unpacked into `0x100400` to `0x10ac50`.
- *Which share one.*  Compared byte for byte below `0x10000` (the
  Z3816A, Z3815A and 58503B: `0x20000`): the Z3801A's and Z3805A's are
  identical, and the Z3815A's and 58503B's differ only in four bytes at
  `0x4000`, `55 55 aa aa` in the Z3815A where the 58503B's are erased,
  outside the record stream, so the two unpack to the same installer.
  The rest differ from each other: 3633 and 3704 share 37 % of their
  bytes, the Z3816A and Z3815A 70 %.  Unpacked, the Vatican installer
  holds the USA installer's strings but for its name, its code at
  addresses 28 bytes on.
- *Commands.*  `*IDN?`, `*CLS`, `:SYSTem:LANGuage`, `:SYSTem:ERRor?`,
  `:DIAGnostic:TEST? n` (0 summary, 1 checksum flags, 2 CPU, 3 RAM,
  4 DUART), `:DIAGnostic:ERASe`, `:DIAGnostic:ERASe?` (1 when the
  writable range is blank) and `:DIAGnostic:DOWNload "<S-record>"`,
  one Motorola S-record per command, checked record by record and
  refused outside the writable range.  As 097-59551-02, 4-15 and
  5-89 to 5-91.
- *Flash.*  Z3801A, Z3805A and 58503A: AM29F010s in word-interleaved
  pairs, labeled 1L, 1M, 2L and 2M (U12, U14, U11 and U13 on a
  58503A): the M part holds the even bytes and the L part the odd;
  pair 1 is `0x0`--`0x3ffff` and pair 2 `0x40000`--`0x7ffff`, the
  order that reproduces `third_party/firmware/58503a-3633.bin` and
  `58503a-3704.bin` from their chip dumps (`third_party/NOTICE`).
  `smartclock-cli join-chips` puts four dumps together that way and
  `split-chips` takes an image apart, whatever the bytes hold, and
  each then says what it finds rather than refusing: whose reset vector
  the image starts with, which boot checksum below holds, and, when
  neither does, which chips look swapped.  Lanes swapped within a pair
  keep the lane sums, since the stored sums swap with the bytes they
  cover, but byte-swap the reset vector; pairs swapped fail the sums.
  Nor do the lane sums notice bytes shifted by an even count within a
  bank, which keeps each byte in its lane.  Z3816A: an Intel-style
  part, one 16-bit wide.
- *Boot check.*  The AMD-flash reset code (`0x550`) sums each byte
  lane of each pair against the bytes at `0x3fffc` and `0x7fffc`.  A
  pass boots the primary; a failure stays in the installer.
  `LANG "PRIMARY"` re-runs the reset code, so a unit with a bad image
  comes back to the installer.  The Z3816A instead sums big-endian
  words from `0x20000` through `0x7fffc`, modulo 65536, against the
  word at `0x7fffe` (reset code `0x526`--`0x53a`).  The 58503B
  1.01.04 and the Z3815A 4010 carry the same routine, instruction for
  instruction, at `0x45e`.  Its erase handler
  starts at `0x20000` and erases three 128 KiB blocks.  All five
  images pass their own sums.
- *Erase and download (Peru installer, read).*  A command word goes
  to both parts of a pair at once, the command in both bytes, at the
  pair's base plus twice the parts' unlock addresses: `0xaaaa` and
  `0x5554` for pair 1, `0x4aaaa` and `0x45554` for pair 2, `0xaaaa`
  then `0x5555` written to them, then the command at the first.
  `:DIAGnostic:ERASe` (`installer_scpi_set_DIAGnostic_ERASe`,
  `0x107e6e`) masks interrupts (`installer_enter_supervisor_mode`),
  sends each pair erase setup, `0x8080`, unlocks it again and writes
  sector erase, `0x3030`, at each 32 KiB sector pair of the writable
  range: `0x10000` to `0x38000` in pair 1, sectors 0 and 1 kept, and
  `0x40000` to `0x78000` in pair 2; then restores the mask and polls
  the words at `0x10000` and `0x40000`, up to a million reads, until
  each byte of both shows DQ7 or DQ5 (`0xa000`, `0xa0`).  It makes the
  whole sequence up to five times and answers -200, Execution error,
  once the fifth is made, even when it succeeds (`quirks.md`).
  `:DIAGnostic:ERASe?` (`0x107f5c`) answers 1 when every word from
  `0x10000` to `0x7fffe` reads `0xffff`.  `:DIAGnostic:DOWNload`
  (`0x107cb8`) copies its string into its frame, takes `S1` to `S3`
  and `S7` to `S9` (4, 6 and 8 address digits; `S0` and `S5` it accepts
  and ignores, `S4` and `S6` it refuses, -222, as any other text),
  converts each pair of hex digits with `installer_strtol`, checks the
  ones' complement of their sum's low byte against the last pair and
  the count against the text's length, and programs the data a word at
  a time, high byte first, from the record's address, refusing,
  -222, an address outside `0x10000` to `0x7ffff`; an odd last byte is
  paired with `0xff`, which leaves its neighbor erased.  The word
  programmer, `installer_flash_program_word` (`0x107c18`), refuses an
  address outside that range, sends program, `0xa0a0`, writes the
  word, reads it back up to 100 times until it reads the word or both
  bytes show DQ5 (`0x2020`), up to five tries, then sends read/reset,
  `0xf0f0`, and answers -200 once the fifth try is made, as the erase
  does.  The command words are the AM29F010's (AMD's Am29F010B data sheet,
  `third_party/amd/Am29F010B.pdf`); the decompiled reading names the polled word at
  `0x40000` `_interpolator_calibration_init`, the primary's code that
  lies there.  The harness runs both to their end on a model of the
  parts' commands and status, comparing the flash's contents after
  (README.md, "Hand-written C"; `harness.Flash`).

On 2026-09-28 the bench Z3801A was taken into the installer and back,
with queries only: `*IDN?` answered `Peru-A`, `:DIAGnostic:TEST? 1`
`+0,+0,+0`, `:DIAGnostic:ERASe?` `+0`, and after `"PRIMARY"` it came
back on 3543-A with its settings unchanged.

So a load interrupted part way that leaves invalid primary checksums
can be retried over the serial port, from the installer the next
power-up lands in.  Valid checksums do not show that the primary can
run or accept the command to enter INSTALL; that case is below.  A
dump written back a lane per part carries its own valid sums.
`:DIAGnostic:TEST? 1` names the failing lane.  Not established: which
part is which lane on the board, and how the new primary resets the
settings after an upgrade, which 097-59551-02 appendix C says it does.

## Forced installer entry with an unusable primary

The Z3801A 3543 and Z3805A 3543B reset paths are byte-for-byte
identical from `0x550` through `0x745`.  All 502 bytes were
disassembled: register setup, four flash lane checksum comparisons,
then the primary-vector loads at `0x738` and `0x73e` and jump at
`0x744`.  The failure branches enter the installer startup at
`0x746`.  The path has no switch input read, serial-break poll or
separate force-INSTALL test.  This does not establish what the board's
eight-position S1 does.

A separate entry in protected flash at `0xa58` is addressed by vector
43 at `0xac`.  It masks interrupts, resets VBR and SP, unpacks the
installer through `0xaa0`, copies its vectors to RAM at `0x100000`,
sets VBR there and jumps to `0x10135c`.  The unpacker at `0xaa0` reads
records from `0xbbc`: an `S`, then records of a `C`, a 32-bit count, a
32-bit RAM destination and that many bytes inline, up to an `E` at
`0xb328`: 17 records, 42861 bytes in all, the length the unpacker
loads at `0xaa8` before the address.  So everything it copies comes from below `0x10000`, into
`0x100400`--`0x10aad2`.  This entry and unpacker are identical in the
two dumps.  The normal PRIMARY trap handler reaches this entry, but
the entry itself calls no PRIMARY code.

A running primary has one way there.  Each Z3801A, Z3805A and 58503A
primary contains a single `trap #11` instruction, and no primary
contains the word `0x0a58`:

| Image | SCPI task | Language byte | Call | `trap #11` |
| ----- | --------- | ------------- | ---- | ---------- |
| Z3801A 3543 | `0x28f0e` | `0x102c34` | `0x28ffa` | `0x12f2c` |
| Z3805A 3543B | `0x2a04a` | `0x102c62` | `0x2a136` | `0x12f2c` |
| 58503A 3633 | `0x2931a` | `0x102f66` | `0x2940c` to `0x13028` | `0x1304e` |
| 58503A 3704 | `0x293c2` | `0x102f76` | `0x294b4` to `0x130da` | `0x13100` |

The 58503A images have a second `0x4e4b` word (`0x29854`, `0x298fc`),
but it is inside the string `UNKNOWN`, not code.  In the Z3801A, the
SCPI task runs the parser and, when the parser returns, tests the
language byte: 0 starts the pForth console (`0x12faa`), and 1 calls
`0x12f2c`, which executes `trap #11`.  The `:SYSTem:LANGuage` handler
`0x2f57a` sets the byte to 0 for `PFORTH` and 1 for `INSTALL`.  So
nothing in the primary diverts to the installer at startup, from an
EEPROM flag or anything else.  The installer is entered only when the
SCPI parser exits after `LANG "INSTALL"`, or when the reset code's
checksums fail.

The pForth console reaches the same `trap #11` without that command.
Its `execute` (Z3801A `0x19b90`: `movea.l (A6)+,A0`,
`movea.l (A0),A0`, `jsr (A0)`) calls the address held in the cell it
is given.  The operand of each call in the table above is such a cell:

| Image | Cell | Holds | Console phrase |
| ----- | ---- | ----- | -------------- |
| Z3801A 3543 | `0x28ffc` | `0x12f2c` | `167932 execute` |
| Z3805A 3543B | `0x2a138` | `0x12f2c` | `172344 execute` |
| 58503A 3633 | `0x2940e` | `0x13028` | `168974 execute` |
| 58503A 3704 | `0x294b6` | `0x130da` | `169142 execute` |

On 2026-09-28 the bench Z3801A (3542A01548, 3543-A) was taken this
way, at 19200 7O1: `:SYSTem:LANGuage "PFORTH"` gave the `p4th D > `
prompt, `167932 @ u.` printed `77612`, and `167932 execute` answered
`scpi > `, with `*IDN?` then `Peru-A` and `:SYSTem:LANGuage?`
`"INSTALL"`.  `:SYSTem:LANGuage "PRIMARY"` from there brought it back
on 3543-A in PRIMARY with an empty error queue, and its position,
elevation mask and antenna delay read back as before.  So this also
returns the port from the console to SCPI without a power cycle.  The
same day `smartclock-cli read-memory` took the bench Z3805A
(3625A01487, 3543B-A, 19200 8N1) out through its exit and back to
3543B-A in PRIMARY with an empty error queue.  The 58503A phrases have
not been tried.

The console is itself reached only through `LANG "PFORTH"` from the
SCPI parser (see `console.md`, "Getting to it"), so it helps only a primary whose
parser still runs.

How S1, a flash-read fault or the CPU's background debug mode might
reach the installer without a working primary is speculative; see
[the recovery hypotheses](../hardware-investigations.md#recovery-hypotheses).

## The chip selects

In the Z3805A 3543B the boot code's reset (`boot_reset`, `0x550`)
programs the MC68331's chip selects after only the SYPCR write, and
the primary's reset code (`primary_reset`, `0x144ca`) writes the same
values again; the table below gives both runs.  Nothing else in the
image writes them: the SIM's chip-select addresses, `0xfffa44` to
`0xfffa77`, appear in its bytes only in those two runs, and the only
instruction that puts a SIM address in an address register is for
PICR (`0x1200a`, `0xfffa22`).  The installer's packed records have
no such write either, so the installer runs on the boot code's map.
`tools/firmware/chip_selects.py` reads every image's map from its
reset code and writes `chip-selects.md` with each image's tables and
how each map differs from the Z3805A's (`make chip-selects`; `make
check-chip-selects` fails when the tables here, in `README.md` and
there drift from the images).  The Z3801A 3543's map is the
Z3805A's.  The 58503A writes no CSBAR4 or CSOR4, so CS4 keeps its
reset state, its BYTE field disabled (MC68331UM 4.8.4), and 3633's
boot code makes the CS4 pin a discrete output, PC1.  The Z3816A 4001
and Z3815A 4010 use another map (`README.md`'s table): there
`0x300000` is chip select 1, `0x302000` chip select 7 and `0x304000`
chip select 5, which none of the Z3801A, Z3805A and 58503A has, and
their boot code's map is not their primary's.  The Z3815A's and the
58503B's boot code first sets CSBOOT and the RAM's selects alone, to
1 MiB blocks, sums the primary under them and jumps into it, and sets
its own map only when the sum fails.

*Pins* (the CSPAR line below; MC68331UM D.3.23, D.3.24, Tables D-8
to D-10, 4.8.1.1).  CSBOOT, CS0
to CS3, CS6, CS7 and CS10 are 16-bit chip selects, CS4, CS5, CS8 and
CS9 8-bit ones.  Every pin that could instead be BR, BG, BGACK, FC0
to FC2 or ADDR19 to ADDR23 is a chip select, so the board's address
bus is ADDR0 to ADDR18, no function code or bus arbitration line
leaves the processor, and any further decode on the board takes
ADDR18 and below inside one chip select.  Pin numbers are the 132-pin
QFP's (Figure 3-2); CS3 to CS10 as `../hardware-investigations.md`'s
pin table reads that figure.

*Option fields* (Table D-12, 4.8.1.3).  Every select is asynchronous
(MODE 0), asserted on the address strobe (STRB 0) and asks no
autovector (AVEC 0); all but CS10 match supervisor and user space
(SPACE 11) at any level (IPL 0).

<!-- chip_selects.py z3805a-3543b boot -->
Pins: CSPAR0 `0x2bff` at `0x568`, CSPAR1 `0x03af` at `0x570`.

| Window | Select | Pin | CSBAR, CSOR; written at | Port | BYTE, R/W, DSACK | Device |
| ------ | ------ | --- | ----------------------- | ---- | ---------------- | ------ |
| `0x000000`--`0x03ffff`, 256 KiB | CSBOOT | 112 | `0x0005`, `0x6870`; `0x578`, `0x580` | 16-bit | both, read, 1 wait | flash, lower bank: two of U11 to U14 (Am29F010, 128 K × 8), a word's two bytes |
| the same | CS6 | 121, PC3/ADDR19 | `0x0005`, `0x7070`; `0x5e8`, `0x5f0` | 16-bit | both, write, 1 wait | the same parts' writes: command cycles and programming |
| `0x040000`--`0x07ffff`, 256 KiB | CS1 | 114, BG | `0x0405`, `0x6870`; `0x598`, `0x5a0` | 16-bit | both, read, 1 wait | flash, upper bank: the other two |
| the same | CS7 | 122, PC4/ADDR20 | `0x0405`, `0x7070`; `0x5f8`, `0x600` | 16-bit | both, write, 1 wait | the upper bank's writes |
| `0x100000`--`0x10ffff`, 64 KiB | CS0 | 113, BR | `0x1003`, `0x6830`; `0x588`, `0x590` | 16-bit | both, read, 0 wait | RAM, read: the two M5M51008B (128 K × 8), of whose 256 KiB the selects reach 64 KiB (*The RAM*, below) |
| the same | CS2 | 115, BGACK | `0x1003`, `0x5030`; `0x5a8`, `0x5b0` | 16-bit | upper, write, 0 wait | RAM, writes to the upper byte (even addresses, DATA15 to DATA8) |
| the same | CS3 | 118, PC0/FC0 | `0x1003`, `0x3030`; `0x5b8`, `0x5c0` | 16-bit | lower, write, 0 wait | RAM, writes to the lower byte |
| `0x200000`--`0x2007ff`, 2 KiB | CS8 | 123, PC5/ADDR21 | `0x2000`, `0x3bf0`; `0x608`, `0x610` | 8-bit | lower, read and write, external | U3, the SCC68692 DUART, driven as a 68681; on an 8-bit port any BYTE but 00 enables the select (4.8.1.3) |
| `0x300000`--`0x30ffff`, 64 KiB | CS5 | 120, PC2/FC2 | `0x3003`, `0x7b30`; `0x5d8`, `0x5e0` | 8-bit | both, read and write, 12 wait | shared: the latch, the switch byte, the FPGA and the EFC strobe (below) |
| `0x400000`--`0x401fff`, 8 KiB | CS9 | 124, PC6/ADDR22 | `0x4001`, `0x78f0`; `0x618`, `0x620` | 8-bit | both, read and write, 3 wait | U4, the AT28C64B EEPROM, by elimination (`../z3805a-board.html`) |
| `0x500000`--`0x5007ff`, 2 KiB | CS4 | 119, PC1/FC1 | `0x5000`, `0x7830`; `0x5c8`, `0x5d0` | 8-bit | both, read and write, 0 wait | none: no instruction addresses it and no part is tied to it |
| CPU space, interrupt acknowledge at level 2 | CS10 | 125, ECLK/ADDR23 | `0xfff8`, `0x2bc4`; `0x628`, `0x630` | 16-bit | lower, read, external; CPU space, IPL 2 | the DUART's acknowledge: it answers with the vector its IVR holds, so its interrupt is level 2, vectored |

The primary (`0x144d2` to `0x1459a`) writes the same 26 values, each `0x13f6a` above.
<!-- end chip_selects.py -->

CS10 is set up as 4.8.3 asks for a 16-bit port: base all ones, a
2 KiB block so that ADDR[19:16] are compared, read only, the lower
byte, the one an acknowledge at an odd address is.  Its DSACK is
external, so the device's acknowledge sets the cycle's width (Table
4-12); `../hardware-investigations.md` item 21 expects the DUART's
/DTACK on DSACK0, which would make it an 8-bit cycle on DATA15 to
DATA8; not yet read on the board.  The flash and
RAM selects split one window between reads and writes, or between
the bytes of a word, so that each part's /OE and /WE can take a chip
select directly; that wiring was not traced.  Which pair of U11 to
U14 is the lower bank was not read from the photos.

*The RAM.*  The Z3805A carries 256 KiB, two M5M51008B (128 K × 8),
a word's two bytes, of which CS0, CS2 and CS3 reach 64 KiB, `0x100000`
to `0x10ffff` (CSBAR `0x1003`).  No image maps more, and none uses
more.  Every map of every image gives the three selects that block
(`chip-selects.md`) but the first run of the Z3815A's and 58503B's
boot code, which makes them 1 MiB (`0x1007`) only while it sums the
primary, and writes RAM there once, a flag at `0x10ac50` (`0x458`).
Inside the block:

| Image | Start-up stack | pSOS configuration: region 1, region 2 | RAM test's end |
| ----- | -------------- | -------------------------------------- | -------------- |
| Z3801A 3543 | `0x10fffe` (`0x1462a`) | `0x1037c4` to `0x10fffe`, none (`0x511e8`) | `0x110000` (`0x91c`) |
| Z3805A 3543B | `0x10fffe` (`0x14922`) | `0x103948` to `0x10fffe`, none (`0x5122a`) | `0x110000` (`0x91c`, `0x1461c`) |
| 58503A 3633 | `0x10fffe` (`0x14804`) | `0x103b0c` to `0x10fffe`, none (`0x540dc`) | `0x110000` (`0x90c`) |
| 58503A 3704 | `0x10fffe` (`0x1497c`) | `0x103b24` to `0x10fffe`, none (`0x53f6c`) | `0x110000` (`0x908`) |
| Z3816A 4001 | `0x10fffe` (`0x24ac6`) | `0x103eb0` to `0x10fffe`, none (`0x581bc`) | `0x110000` (`0x738`) |
| Z3815A 4010 | `0x10fffe` (`0x24f8e`) | `0x104594` to `0x10fffe`, none (`0x5ed0a`) | `0x110000` (`0x78e`) |
| 58503B 1.01.04 | `0x10fffe` (`0x249f8`) | `0x10469c` to `0x10fffe`, none (`0x5bb8a`) | `0x110000` (`0x78e`) |

The stack is the `lea` into SP before the primary's start; the
configuration (`structures.md`'s `psos_configuration`) is the ROM
table the kernel's configuration is copied from, region 1 from its
data area to `data_end`, and region 2's start and end both 0, so that
`kernel_init` leaves it empty; the RAM test's end is its
`movea.l #0x110000`.  The Z3805A's RAM test (`0x916` to `0xa1a`) runs
its patterns from `0x100000` to that end and its address-line test
over offsets 2 to `0x8000`, ADDR1 to ADDR15: the firmware is written
for 64 KiB, 32 K words.  In a disassembly of each whole image the
only operands from `0x110000` to `0x1fffff` but the RAM test's end
are constants, none an address: `0x112000`, `0x114000` and `0x130200`
pushed for `float_raise` and the installer's copy (in the Z3805A
`0x5e2a2`; `0x9282`, `0x9294` in the installer's records), `0x180000` stored to a local (`0x1067e`), 1800000 and
`0x199999` compared, and `0x1fffff` a mask.  So the parts'
upper address pins, beyond A14, are held by the board, tied or on
ADDR16 and above, which the block holds at 0.  Why the board carries
four times the RAM it maps the image does not show.

*CS5's window.*  It is the only chip select the image uses for more
than one device:

| Address | Name | In the Z3805A image | Device |
| ------- | ---- | ------------------- | ------ |
| `0x300000` | `lamp_latch` | written whole by `leds_update` (`0x464de`) and `lamp_test` (`0x184ee`) | the lamp latch (`outputs.md`, "The Z3805A's lamps"); U44 the candidate |
| `0x302000` | -- | never accessed; only 58503A 3633's Oman installer reads it (below) | the switch byte; whether it is S1 is open |
| `0x304000` | `fpga_port` | read by `fpga_read` (`0x4076a`), written by `fpga_write` (`0x40756`) | U32, the XC3042 FPGA (`interval.md`) |
| `0x306000` | `efc_strobe` | written 0 by `efc_write` (`0x1f646`) after the EFC goes out to QSPI device 7 | not identified |

The four lie 8 KiB apart, so an access's device is ADDR13 and ADDR14;
no access in the image sets ADDR15, so the image does not show
whether `0x308000` to `0x30e000` decode to more devices, alias these,
or select nothing.  A 74AC138 enabled by CS5 with ADDR13 to ADDR15 on
its A inputs would give these four as /Y0 to /Y3; which of U22, U23,
U25 and U27 it is, if any, is for the bench
(`../hardware-investigations.md` item 20).  Every device in the
window takes CS5's 12 wait states.  Nothing in the chip selects says
what the other '138s decode; the QSPI's PCS0 to PCS3 carry a device
number the board decodes (`structures.md`, "The QSPI's devices"),
which wants one too.

## The switch byte at `0x302000`

In the Z3801A, Z3805A and 58503A the byte lies in CS5's window ("The
chip selects", above); in the Z3816A and Z3815A primaries it is chip
select 7's own block (CSBAR7 `0x3020`, CSOR7 `0x6870`: Z3816A
`0x24706`, `0x2470e`; Z3815A `0x24bce`, `0x24bd6`).

The only read of `0x302000` in any installer is in the Oman installer
that 58503A 3633 carries (unpacked address `0x108290`).  The installer
first loads its host-port settings from the EEPROM record or its
defaults, then reads the byte and overrides them:

```
108290  move.b  $302000,d1
        andi.b  #1,d0 / bne -> rts        bit 0 set: keep the settings
        (d1 & 6) >> 1   -> 0x10ac7e       bits 2:1: baud index
        bit 3 set       -> 0x10ac7f = 2, 0x10ac82 = 0
        bit 3 clear     -> 0x10ac7f = 0, 0x10ac82 = 1
        bit 4           -> 0x10ac80
        clr.b 0x10ac83
```

Bit 0 clear enables the override.  Bits 2:1 index the DUART channel B
clock select values `0x66`, `0x88`, `0xbb`, `0xcc`.  Bit 3 set selects
7 data bits, odd parity; clear, 8 bits, no parity.  Bit 4 turns on
XON/XOFF pacing.  One stop bit is forced; bits 5 to 7 are not tested.
The installer writes the ACR once, `0xb0` (`0x346e`, `0x3476` in the
unpacked image), selecting the 68681's baud-rate set 2, in which those
codes are 1200, 2400, 9600 and 19200 baud at the part's standard
3.6864 MHz clock; the board's crystal was not checked.  The byte is
read each time the installer starts and is not stored.

The Peru installer (Z3801A 3543, Z3805A 3543B) and the USA installer
(58503A 3704, Z3816A 4001) do not read `0x302000`, nor does the
Vatican installer (Z3815A 4010, 58503B 1.01.04).  Of the primaries, the Z3801A,
Z3805A and both 58503As never read it.  The Z3816A reads bit 8 of the
word there once, to pick G (see `loop.md`, "τ and G").  The Z3815A
reads the word in ten places (`0x33c04` to `0x50bf4`), testing bits 7,
12, 13 and 14; bit 12 picks its τ and G (`loop.md`, "In every image").
The 58503B reads the byte once in its EFC writer, `0x2dba0`, after
sending the value to the DAC over the QSPI and pulsing port E bit 7;
the console word `efc_write` and the loop's EFC conversion (`0x32cd0`)
call it, so the byte is read at every EFC write, and what that read
does was not traced.  So no firmware on the bench units reads this
byte.  Whether it is S1 has not been established.
097-55300-01 figures 3-14 and 3-15A give the related 55300A's S1 B1 as
"Preset All Serial Ports at Powerup", the same kind of function.

## The flasher

`smartclock-cli flash` uses the serial port directly.  Stop the daemon
for that port first; the flasher has no daemon socket mode and does
not discover or probe other ports.  It finds the port's line settings
as the other commands do (`smartclock::attach`): the given `--baud`
and `--framing` first, then the others these receivers use, reading
the probe's errors off the queue before its checks.  `--device` also
takes `tcp://host:port`.  `--capture` records the exchange to a new
file, as for every command; an existing transcript is refused, not
overwritten.  Replace the placeholders below with the image path,
device and receiver serial.

Inspect the file without opening hardware:

```
smartclock-cli flash <firmware.bin>
```

Check the connected receiver without changing language or flash:

```
smartclock-cli --device <device> flash <firmware.bin> \
  --serial <serial>
```

Reinstall that dump:

```
smartclock-cli --device <device> \
  flash <firmware.bin> --serial <serial> --write
```

The image catalog accepts these full 512 KiB dumps by exact SHA-256:

| Model | Primary revision | Installer in the dump | Writable start |
| --- | --- | --- | --- |
| Z3801A | 3543 | Peru | `0x10000` |
| Z3805A | 3543B | Peru | `0x10000` |
| 58503A | 3633 | Oman | `0x10000` |
| 58503A | 3704 | USA | `0x10000` |
| Z3816A | 4001 | USA | `0x20000` |

It rejects modified files, chip dumps (`join-chips` makes an image of
them) and S-record input.  Model and
running primary/installer revision must match an audited profile; the
candidate's model must match the receiver.  Writing requires the
expected serial, and the identity's revision suffix must not change on
entering the installer.  After programming, a changed suffix is
reported separately from a successful primary boot; its behavior
across upgrades is not established.  This relies on the receiver's
EEPROM identity, not independent board identification.  A model
without a dump (such as 59551A) is refused until its image and layout
can be audited.

The installer is checked only against the model/layout allowlist, not
against the primary revision it was entered from: a 58503A running
3704 can keep the Oman installer from 3633 after an upgrade.  The
table gives the installer bundled in each dump; not every allowlisted
installer/primary pairing has been tested.  The CLI reports this
policy in check-only mode and immediately before erase.

Errors the receiver held from before the run stop even check-only
mode, and all are listed.  The session reads them off the queue as it
meets them, so no command is judged by an older error (`protocol.md`,
"The error prompt"); run again once they are understood.  The tool
never sends `*CLS`.

Only the model's writable range is downloaded, as word-aligned 64-byte
S2 records, each passed as a quoted SCPI string.  A bare S-record is
parsed as a mnemonic; the bench Z3801A rejects it with -112, "Program
mnemonic too long".  The tool does not write the boot area or EEPROM.
It checks for blank flash after erasing, waits for each record's
prompt and checks the error queue before continuing.  The installer's
word programmer (`installer_flash_program_word`, `0x107c18` in the
unpacked Z3801A and Z3805A installer; "The installer" above) compares
the flash word to the requested word and reports failure if
programming fails.

For the AMD-flash models, four lane sums cover `0x10000`--`0x3fffb` and
`0x40000`--`0x7fffb`, even and odd bytes separately, modulo 65536.
Each pair's sums are stored interleaved in its last four bytes.
`:DIAGnostic:TEST? 1` reads the flags saved at boot (`0x1080ec`); it
does not recompute them after download.  Final verification therefore
switches to PRIMARY, which runs the model's boot checksum checks, then
requires the same serial and the candidate revision in PRIMARY.  Then,
unless given `--no-readback`, it reads the whole 512 KiB back through
the debug console, as `smartclock-cli read-flash` does, requires it to
equal the image, protected boot region included, and returns the port
to SCPI as `read-flash` does (see `console.md`, "Reading memory through it").  That
adds about eleven minutes.  For an image with no known installer exit,
such as the Z3816A's, it says so and relies on `halt` alone.
Simulator tests compare all bytes after programming for each catalog
image, protected boot region included; the simulator has no console,
so the readback is not tested there.

An error stops the transfer, with no automatic write retries or
reboot.  Keep the daemon stopped and rerun the same command with a new
transcript path to restart from erase in the surviving installer.
Ctrl-C during a write stops before the next record, never inside one,
and prints that command, with the transcript renamed; before the
erase it stops with nothing written.  During the readback it stops
before the next kilobyte and still returns the port to SCPI.  A second
Ctrl-C exits at once.  `read-memory` and its kin stop the same way.
The tool does not restore settings.

Cross-revision flashing is allowed between catalog images of the same
model and layout, but has not been tried on hardware.  Simulator tests
cover 58503A 3633 to 3704 and 3704 to 3633, comparing the downloaded
primary bytes and the original protected boot bytes.  097-59551-02
appendix C, page C-3, says new firmware resets settings to
system-preset defaults; the same-revision Z3801A reinstall below kept
every setting checked.  Whether a revision change resets settings is
unverified on hardware.  Record settings before and after the first
real upgrade.

Bench validation on 2026-09-28: the Z3801A `3542A01548` was reflashed
with its own `z3801a-3543.bin` dump at 19200 7O1.  All 7,168 quoted
records (448 KiB) were accepted.  The transfer finished in 891 seconds
with `3543-A` and `PRIMARY` verified.  Position, survey state,
survey-at-power-up setting, elevation mask, antenna delay, ignored
satellites and timezone all matched their pre-flash queries.  That run
predates the readback.  The same unit was reflashed the same way again
that day with the readback: the primary booted on `3543-A`, all
`0x80000` bytes read back in 646 seconds identical to the image, and
the port came back to SCPI in PRIMARY.  Position, elevation mask,
antenna delay, ignored satellites and timezone again matched their
pre-flash queries.  A third reflash that day, through `smartclock-cli
flash` after the flasher moved into the CLI, gave the same results,
with a readback of 645 seconds and a 3.9 MB transcript holding all
7,168 records.  Other models have simulator coverage and firmware
analysis, not a hardware flashing test.

## `:DIAGnostic:GPSystem:UTC`

The node's handler `FUN_0003a878` passes the record at `0x4322c`,
whose subject is the byte at `0x102616`, with the same getter and
setter as the τ-block bytes.  The GPS task reads that byte to choose
between two paths in `FUN_0002075a` and `FUN_00049dfc`, requires it in
`FUN_0002130c` when it parses a message, and folds it into bit 0x40 of
a status byte in `FUN_00023bbc`.  Which Oncore message each path sends
was not traced; the command table already carries the query form as
`:DIAGnostic:GPSystem:UTC?`, discovered on a 58503A, and the set form
with 0 or 1 is what owners describe.
