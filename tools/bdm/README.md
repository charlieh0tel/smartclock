# Flashing a 58503A over BDM

A routine that programs the 58503A's flash from its own RAM, loaded and
called over the MC68331's background debug mode (BDM), and the host
logic that drives it.  Over BDM each separate memory access is a
host round trip, while a contiguous transfer is fast; so the host
bulk-writes the routine and a sector pair's data into RAM, sets the
registers and calls it, and the routine makes the scattered command
writes and status polls at bus speed and returns to background mode.

It has run only on mc683xx's MC68331.  Nothing here touches a serial port, a
pod or a receiver.

| File | What it is |
| ---- | ---------- |
| `flash_routine.S` | the routine, CPU32 assembly; its interface in its header |
| `build_routine.py` | assembles it with the m68k binutils into `target/bdm/`: bytes, symbols (JSON) and listing |
| `target.py` | the target interface the host needs, the one place an adapter is written against |
| `flash_host.py` | the procedure, its refusals and the chip-select hazard check; `--plan` offline |
| `board.py` | the board's flash wiring, which the host and the model share |
| `am29f010.py` | the flash model: the parts' command state machine and the board's selects |
| `model_target.py` | the target interface on mc683xx's MC68331, the model wired to its selects |
| `test_flash.py` | the tests |

## Running it

```
make bdm-routine     # target/bdm/flash_routine.{bin,json,lst}
make test-bdm        # assembles it, then the tests on the model target
python3 -I tools/bdm/flash_host.py --plan CURRENT.bin IMAGE.bin
```

`--plan` reads a dump and an image and prints the sector pairs a run
would erase and program, or why it would refuse.  The routine's size
and code are in `target/bdm/flash_routine.json` and `.lst`.

`make test-bdm` needs the m68k binutils (`m68k-linux-gnu-gcc`, as the
firmware's checks do; [`docs/firmware/README.md`](../../docs/firmware/README.md))
and uv, which takes `mc683xx` from PyPI at the version the firmware's
checks pin (the `# dependencies` headers of `tools/firmware/*_check.py`).

## The routine

Position independent, it refers to its own code only PC-relative;
`build_routine.py` refuses an object with a relocation.  Its only
absolute addresses are the board's two flash pairs.  It uses nothing of
the firmware and no RAM but the stack the host gives it.

Operations: identify (autoselect: both codes and the eight sector
pairs' protection words, into the buffer), erase one sector pair
(then a read of every word for `0xffff`), program a run of words from
the buffer within one pair, and verify a run against the buffer.

The register interface:

| | Register | Meaning |
| - | -------- | ------- |
| in | D0 | operation, `OPERATION_*` |
| in | A0 | flash address, even: the sector pair to erase, the first word to program or verify, any address in the pair to identify |
| in | A1 | RAM buffer, even |
| in | D1 | words to program or verify, 1 to a pair's, all in A0's pair |
| in | D2 | the lowest address erase or program may touch |
| in | D3 | the poll limit, at least 1 |
| out | D0 | status, `STATUS_*` |
| out | A0 | the failing word's address; on success the address after the last word |
| out | D1 | the word last read there (low word) |
| out | D2 | the word expected there (low word) |

An exception instead returns `STATUS_EXCEPTION`, the stacked PC in A0,
the frame's format and vector word in D1 and a bus error's fault
address in D2.  Every path ends at one place: read/reset to both
pairs, then BGND.

- **Lanes.**  A word is two parts, one per byte lane, each finishing
  on its own schedule.  The poll judges each lane: done when its DQ7 is
  the datum's; a lane not done showing DQ5 is read again, since DQ7 may
  change with DQ5, and fails only if still not done; once both are
  done the word is read once more, since DQ6-DQ0 may lag DQ7
  (Am29F010B, Figure 3 and its note 2, p. 16; DQ5, p. 18).
- **0 to 1.**  The routine refuses, before it programs anything, a run
  in which any word asks a 1 where the flash holds a 0
  (`STATUS_NEEDS_ERASE`): it alone sees the flash as it is when it
  programs.  The host never asks it to, since it programs only sector
  pairs it has just erased and blank-checked.
- **Equal words** are skipped.
- **Range.**  The routine refuses an erase or program below D2, an
  odd address, an erase not on a sector pair, and a run past its pair.
  The host passes `0x10000` in D2 unless told otherwise: two checks of
  one rule.
- **Failure.**  Any failure stops at once; there are no tries.

## Exceptions, the watchdog, interrupts

The host builds a 256-vector table in RAM, every vector the routine's
`exception` entry, and sets VBR to it over BDM.  The handler reads the
vector from the frame, so one entry serves all.  If finish's own
writes fault, the handler goes straight to BGND rather than back to
them.  A fault while stacking a frame is a double bus fault, which with
BDM enabled enters background mode (MC68331UM 5.10.2.2.3); the host
reports that entry as a failure.

The host, not the routine, writes SYPCR, once, before any code runs:
`0x04`.  SYPCR can be written once after reset (MC68331UM D.3.12).  At
a reset entry the watchdog is enabled with SWP 0 on this board
(bench SYPCR `0x80`), a time-out of 2^9 crystal periods, about 16 ms
(Table D-6), far short of an erase.  The bench pod's entry left FRZSW
set, so the watchdog is frozen in background mode (D.3.1), but it
counts as soon as the routine runs.  Writing it from the host lets the
host read it back before anything runs, and keeps the routine free of
a write that only the first call could make.  BME is set so that a
cycle no select answers ends in a bus error, and so at BGND, rather
than a hang; 64 clocks (BMT 00) is far past the selects' one wait
state.  HME stays 0: a double bus fault enters BDM regardless.  A test
shows the run without the SYPCR write ending in a watchdog reset.

SR is `0x2700` for a call: interrupts masked to level 7, which is not
maskable; a level-7 interrupt reaches the vector table and ends the
call.

## The host

`flash_host.flash()` runs the procedure in its docstring.  Each
target error is fatal; nothing is retried.  It refuses:

- an image not exactly 512 KiB;
- an image whose lane sums (`tools/firmware/lane_sums.py`, over
  `0x10000`-`0x3ffff` and `0x40000`-`0x7ffff`) do not hold, unless
  `unchecked` says the image deliberately does not;
- any sector pair below `0x10000` that would change, unless
  `allow_low`: the vectors, boot code and installer, which only a
  programmer could restore;
- a reset entry whose SIM is not a fresh reset's (SYPCR unwritten,
  CSBOOT and the pin assignments at their reset values);
- a SIM write that does not read back;
- a chip-select write that adds a hazard to the decode;
- parts that do not give AMD's and the Am29F010B's codes in both lanes
  of both pairs, and a protected sector pair it would write;
- a return to background mode that is not the routine's BGND.

`dry_run` stops after the dump and reports the plan: it sends the flash
no command at all and runs no code.  The host never sets PC outside the
routine, so it cannot start the firmware; a pod's "reset and run" is
the person's step, after the host returns.

**The chip-select order.**  The host writes the 3704 boot code's flash
and RAM selects in the order that worked on the bench: CSORBT read-only
first, so that no write asserts the parts' output enable; CSBARBT
narrowed next, before CS1 and CS7 open pair 2's range, so that
CSBOOT's 1 MiB block at reset never answers there.  `hazards()`
decodes the selects as the SIM does (MC68331UM D.3.23-D.3.26) and
names what a decode does wrong in the flash's space: a flash read
select that asserts on writes, a flash select outside its pair, two
selects driving one read, two taking one write.  The reset decode has
the first two.  Before each write the host refuses one that would add
a hazard, and after the last it refuses any left.  The tests move
CSBARBT and CSORBT later and see the host refuse.

**Timeouts.**  The routine counts polls; the host converts seconds to
a poll limit from the clock SYNCR gives, assuming the least time a
poll can take, one read with the selects' one wait state, so the
routine never gives up before the time asked: 1 ms a word, 25 s a
sector pair (the data sheet's 300 us and 15 s plus the preprogram,
rounded up).  A real poll takes several times that least time, so the
routine's true limit is that many times longer.  The host waits for a
call as long as the routine's own worst case, from an upper bound on
the clocks a step takes, plus a second.  `test_flash.py` holds both
bounds to mc683xx's clock count.

## The target interface

`target.py` is the whole of it.  An adapter for a pod is a class with
these methods; every one acts at once or raises `TargetError`.

| Method | Does |
| ------ | ---- |
| `reset_into_bdm()` | RESET with BKPT held low: the CPU in background mode before its first instruction |
| `read_memory(address, width)` | one read of 1, 2 or 4 bytes, supervisor data space |
| `write_memory(address, width, value)` | one write likewise |
| `read_block(address, length)` | a bulk read (DUMP), even address and length |
| `write_block(address, data)` | a bulk write (FILL), even address and length |
| `read_register(name)` | D0-D7, A0-A7 (A7 the supervisor stack pointer), PC, SR, VBR |
| `write_register(name, value)` | likewise |
| `go()` | leave background mode, running from PC |
| `wait_for_bdm(timeout)` | block until background mode again; returns the entry (BGND, breakpoint, double bus fault, MC68331UM Table 5-4); `TargetTimeout` after timeout seconds; a reset meanwhile is a `TargetError` |

## The model target and the flash model

The test target is mc683xx's MC68331 from its Python module: the real
SIM decodes the chip selects, BDM enters background mode at a reset
with BKPT held and at BGND, the CPU32 runs the routine, and the
watchdog counts.  Wired to it as on the board: the flash model on
CSBOOT, CS1, CS6 and CS7, and 64 KiB of RAM on CS0, CS2 and CS3.
Nothing else of the board is modeled.  mc683xx's reset SIMCR is the
manual's, `0x00cf`; the bench pod's reset entry read `0x60cf`; the
host depends on neither.

The flash model, `am29f010.py`, imports nothing of mc683xx.  It
implements, per part: the two unlock cycles; autoselect (manufacturer,
device, sector protection); byte program with status (DQ7 the
complement of the datum, DQ6 toggling, DQ5 on a time-out) for a set
time; sector erase with its 50 us window and status (DQ7 0, DQ6
toggling, DQ3) for a set time; read/reset, and its being ignored while
a part is busy; a program that can only clear bits, a 1 asked over a 0
ending in DQ5; protected sectors; a stuck part that never completes
and shows DQ5; and wrong sequences returning to read.  Per board: the
even part on D15-D8, part addresses on A1-A17, the read and write
selects, and two hazards it records: a read select outside its pair
(CSBOOT's mirror) and a read select asserted on a write.

It does not model: real timing margins (a cycle is accepted whatever
its timing), DQ7 changing asynchronously with DQ6-DQ0, the toggle-bit
algorithm, erase suspend, chip erase, high-voltage autoselect and
protection, further sectors in the 50 us window, anything electrical
(contention, Vcc), the board's other peripherals, or the pod.  Whether
a write that asserts CSBOOT would land on pair 1 depends on the parts'
WE# wiring, which is not traced: the model records the hazard and
drops the write.

## The tests

`make test-bdm`: the routine built position independent and agreeing
with `board.py`'s geometry; identify; bad operations and arguments,
the floor among them; 0 to 1 refused before any program; equal words
skipped; verify's mismatch; lanes finishing at different times; a
stuck lane in erase and in program; a time-out; a bus error mid-erase
returning through the vector table; the poll and step clock bounds; a
whole image from blank, from a variant of itself and from the 3633
image (refused below `0x10000`, then written with `allow_low`); a dry
run; refusals of a low sector pair, a short image, bad lane sums (and
the same image written with `unchecked`), unexpected IDs and a
protected sector; the watchdog reset without SYPCR; the bench order
adding no hazard; the reset decode's hazards; the mirror on the model;
and four changed orders refused.

## A bench trial

It needs: a pod adapter for `target.py`; `make bdm-routine`; the
owner's choice of unit, a spare first being the safer; the
unit's own image as the restore image, checked by SHA-256 against the
catalog's.  One reset entry, in this order:

1. A dry run with the unit's own image: SYPCR, the chip selects read
   back, the routine and table loaded and read back, the dump's SHA-256
   the catalog's, nothing to write.  No flash command, no code run.
2. Identify, through the routine: the first code the CPU runs, sending
   only autoselect and read/reset.  It proves the call: registers,
   VBR, BGND, the return PC.
3. The unit's image with one word of the blank sector pair `0x70000`
   changed and the lane sums fixed: a plan of `0x70000` and `0x78000`,
   pair 2's blank pair and its sums, the sector pairs the bench's
   manual stages B and C already erased and programmed.
4. The unit's own image again: the same two sector pairs back, the
   final SHA-256 the catalog's.
5. Then, by hand, the pod's reset and run: the primary boots.

Recovery: the host never writes below `0x10000`, so the vectors, boot
code and installer stay; a failure leaves pair 2's sums wrong at worst,
and the unit boots into the installer, which `smartclock-cli flash`
can reflash, or BDM can again from a reset entry.  A part busy after a
time-out finishes on its own (no RESET pin): reset into BDM and poll
before reading.  Last resort: the parts are socketed.

What the model cannot show: the pod's semantics (register names, A7
against SSP, the PC after BGND, FILL and DUMP widths, whether `go`
after a reset entry needs anything more); real poll and erase timing,
and the DQ7 lag the routine already allows for; the wiring of WE#;
any interrupt line asserted at level 7; electrical behavior.

## Adversarial review

**What breaks.**  Running code at all: once the CPU runs, the pod has
no control until BGND.  Against that: SYPCR (watchdog off, bus monitor
on), interrupts masked, every vector to BGND, finish's guard against
its own faults, and the host's timeout.  A routine that never returns
(a fault in the handler's own stack, a level-7 storm) leaves the host
waiting out its timeout and then touching nothing; only a reset ends
it, and a part erasing then keeps erasing.  A bug in the range checks
could erase below `0x10000`: the host's plan and the routine's floor
each forbid it, and the tests hold both.  The lane sums prove an image
self-consistent, not that it is a 58503A's: unlike `smartclock-cli
flash`, this takes any image whose sums hold, a Z3801A's included.

**What it costs.**  Each trial is a reset entry, a cold start of the
oscillator loop.  The poll limits assume the fastest possible poll, so
without DQ5 a failed erase runs several times its 25 s before the
routine gives up, and the host's wait is longer still.  Building needs
the m68k binutils; the tests need uv and mc683xx.

**What it forecloses.**  Little: the routine uses the same chip-select
map as direct BDM writes, which stay possible.  The register interface
is a contract between the routine and the host, versioned together
through the routine's JSON.
