# Flashing a 58503A over BDM

A routine that programs the 58503A's flash from its own RAM, loaded and
called over the MC68331's background debug mode (BDM), and the host
logic that drives it.  Over BDM each separate memory access is a
host round trip, while a contiguous transfer is fast; so the host
bulk-writes the routine and a sector pair's data into RAM, sets the
registers and calls it, and the routine makes the scattered command
writes and status polls at bus speed and returns to background mode.

It is a tool for someone who knows what they are loading: it writes
exactly the image it is given, judging nothing of its content, the
reset vectors, boot code and installer below `0x10000` included.  What
it refuses is physical, or protects the way back: an image not the
size of the flash, parts that are not the expected ones, a chip-select
decode with a hazard, and any target error.  It never starts the
firmware.

`pod_target.py` adapts it to the
[mc683xx BDM pod](https://github.com/charlieh0tel/mc683xx-bdm).  The bench trial
below ran on a 58503A through the pod on 2026-11-22, every step
passing ("Seen on the bench").  The tests touch neither pod nor
receiver.

| File | What it is |
| ---- | ---------- |
| `flash_routine.S` | the routine, CPU32 assembly; its interface in its header |
| `build_routine.py` | assembles it with the m68k binutils into `target/bdm/`: bytes, symbols (JSON) and listing |
| `target.py` | the target interface the host needs, the one place an adapter is written against |
| `flash_host.py` | the procedure, its refusals and the chip-select hazard check; the command line: `--plan` offline, `--dry-run`, `--identify` and `--write` on a target |
| `board.py` | the board's flash wiring, which the host and the model share |
| `am29f010.py` | the flash model: the parts' command state machine and the board's selects |
| `model_target.py` | the target interface on mc683xx's MC68331, the model wired to its selects |
| `pod_target.py` | the target interface on the mc683xx BDM pod's Python module, `mc683xx_bdm` |
| `trial_image.py` | a bench trial's image: one word changed, the lane sums fixed |
| `test_pod_target.py` | the pod adapter's tests, against a stand-in for the pod's module |
| `test_flash.py` | the routine's, the host's and the command line's tests, on the model target |

## Running it

```
make bdm-routine     # target/bdm/flash_routine.{bin,json,lst}
make test-bdm        # the pod adapter's tests; assembles the routine, then the tests on the model target
python3 -I tools/bdm/flash_host.py --plan CURRENT.bin IMAGE.bin
```

`--plan` reads a file of what the flash holds and an image and prints
the sector pairs a run would erase and program, or why it would
refuse.  The routine's size and code are in
`target/bdm/flash_routine.json` and `.lst`.

On a target, every run is one reset entry, and the options say
everything; none defaults to writing:

```
flash_host.py (--pod POD | --model FLASH) --image IMAGE MODE [flags]
```

| Option | Meaning |
| ------ | ------- |
| `--pod POD` | the pod, as `mc683xx_bdm.Pod` names it: `usb`, `usb:SERIAL` |
| `--model FLASH` | the modeled board, its flash holding the file FLASH; needs `mc683xx` (`uv run --with mc683xx==0.9.0`, as `test_flash.py` pins it) |
| `--image IMAGE` | the image to program, which the plan is against |
| `--dry-run` | set-up, the dump and its SHA-256, the plan: no flash command, no code run on the CPU |
| `--identify` | the dry run and the routine's identify: the first code the CPU runs, sending only autoselect and read/reset |
| `--write` | the whole procedure |
| `--any-entry-state` | go on from a reset entry whose SIM is not a fresh reset's, reporting what it read |

Every run on a target, and `--plan`, first reports whether each bank's
lane sums hold, the boot check's (`tools/firmware/lane_sums.py`, over
`0x10000`-`0x3ffff` and `0x40000`-`0x7ffff`), and if one does not, warns
that the image will boot into the installer, not the primary.  It
writes the image all the same.

`--write` prints each sector pair as it is erased, programmed and
verified, then the whole flash's SHA-256 and either success or the
ranges that differ.  Every run ends with the CPU stopped in background
mode, and says so; none resets or resumes it.  After a target failure
it says instead that the CPU may be running, and that the unit must be
reset into BDM before anything else.

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
  The host passes 0 in D2, the whole flash writable; the floor stays
  in the interface for a caller that wants one.
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
target error is fatal; nothing is retried.  It reports the image's
lane sums and writes it whether they hold or not.  It refuses:

- an image not exactly 512 KiB;
- a reset entry whose SIM is not a fresh reset's (SYPCR unwritten,
  CSBOOT and the pin assignments at their reset values), unless
  `any_entry_state`;
- a SIM write that does not read back;
- a chip-select write that adds a hazard to the decode;
- parts that do not give AMD's and the Am29F010B's codes in both lanes
  of both pairs, and a protected sector pair it would write;
- a return to background mode that is not the routine's BGND, with PC
  just after it.

`dry_run` stops after the dump and reports the plan: it sends the flash
no command at all and runs no code.  `identify_only` stops there too,
after identify.  The host never sets PC outside the routine, so it cannot start the firmware; a pod's "reset and run" is
the person's step, after the host returns.

**The entry state.**  The host reports the SIM's state at its reset
entry (SYPCR, the pin assignments, CSBOOT's registers) and refuses one
that is not a fresh reset's; `--any-entry-state` has it go on, the
hazard check starting from the selects it read and the host writing
every select and both pin assignment registers itself.  SYPCR is
write-once, so after an entry where it was already written to another
value the run stops at SYPCR's read-back.

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
| `read_register(name)` | D0-D7, A0-A7, PC, SR, VBR; A7 the stack pointer SR's S bit selects, PC the return PC (RPC), after a BGND the address after it |
| `write_register(name, value)` | likewise |
| `go()` | leave background mode, running from PC |
| `wait_for_bdm(timeout)` | block until background mode again; returns the entry (BGND, breakpoint, double bus fault, MC68331UM Table 5-4); `TargetTimeout` after timeout seconds, the CPU never halted; a reset meanwhile is a `TargetError` |

Two conventions are stated because both adapters must keep them.  PC
is RPC, where GO resumes (MC68331UM 5.10.2.5.2), so after the routine's
BGND it is the BGND's address plus its one word; the host requires
exactly that, which mc683xx and the pod's bindings both give.
A7 is whichever stack pointer SR selects, so the host writes SR, with
S set, before A7.

## The pod adapter

`pod_target.py` implements the interface on a `Pod` of the pod's
Python module, `mc683xx_bdm` (the pod's repository, `crates/py`: its
README and its type stub, `python/mc683xx_bdm/__init__.pyi`).  It
imports the module only when it opens a pod, so the tests and the
model path need none.

| Method | Pod calls |
| ------ | --------- |
| `reset_into_bdm()` | `reset_with_breakpoint()`, then `background` must not be `None` |
| `read_memory(address, width)` | `bus_read(address, width, 5)` |
| `write_memory(address, width, value)` | `bus_write(address, width, value, 5)` |
| `read_block(address, length)` | `read_memory(address, length, size=, function_code=5)`, longs where the address and length allow, a word before and after otherwise |
| `write_block(address, data)` | `write_memory(address, data, size=, function_code=5)`, likewise |
| `read_register(name)`, `write_register(name, value)` | the same calls by the same names: the pod takes `PC` for RPC and `A7` as SR selects it |
| `go()` | `background` must not be `None`, then `go()` |
| `wait_for_bdm(timeout)` | polls `background`, and while it is `None` `pins().reset`, until an entry, RESET low, or the time-out |

Function code 5 is supervisor data.  Every exception a pod call raises
(`TransportError`, `ProtocolError`, `TargetError`, `BusError`, and
`ValueError` for an argument the pod rejects) becomes the host's
`TargetError`, with the original's type and message and the original
as its cause.  An entry the interface does not name (the pod's
`Entry.Unknown`) is a `TargetError`.  The adapter never calls `halt()`:
on a time-out the host touches the target no more, and a halt of a CPU
that a reset left running with BDM off would reach the firmware as a
breakpoint exception.  The pod reports no reset while the CPU runs, so
the adapter reads the RESET line between polls and calls it low a
reset while running; a reset shorter than a poll goes unseen and ends
in the time-out instead.

On a bench machine the module comes as the pod's repository
documents (`crates/py`, its README): a wheel, installed into an
environment of this checkout's own with a Python the module supports,
for example `uv venv target/bdm-venv` and `uv pip install --python
target/bdm-venv/bin/python WHEEL`.  The bench steps below call that
environment's Python `$PY`.

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
image (its boot region among the pairs written); a dry
run; a low sector pair written; refusals of a short image, unexpected IDs
and a protected sector; bad lane sums reported and the image written; the watchdog reset without SYPCR; an entry
not a fresh reset's refused, and with `any_entry_state` written
through, or refused at SYPCR's read-back if SYPCR was written; the bench order
adding no hazard; the reset decode's hazards; the mirror on the model;
four changed orders refused; the trial image and its refusals;
identify only, each step logged; and the command line on the model
target: a dry run, identify, a trial written, an image
with failing sums written after its warning, and its refusals before
any target.

`test_pod_target.py`, against a stand-in pod that records each call,
answers `background` from a script, reports a RESET level and raises
any of the pod's exceptions: each method's pod calls, the access
widths, each of the wait's outcomes (each entry, an unknown entry, the
time-out with no `halt`, RESET low), and every pod exception from
every call becoming a `TargetError`.

## A bench trial

The rule: the firmware never runs mid-procedure.  Every run begins
with its own reset entry (RESET with BKPT held), and none resets the
unit to run or resumes it; between runs the unit stays stopped, and no
one starts it until the last step.  If the firmware does run between
steps, the next run's reset entry still starts clean, but the run that
was interrupted is suspect: begin again at step 1.

It needs: `make bdm-routine`; the pod module in an environment (above);
the owner's choice of unit, a spare first being the safer; and the
unit's own image, which step 1 confirms.  With the unit's image (the
3704, say) and a folder for the trial image:

```
PY=target/bdm-venv/bin/python
IMAGE=third_party/firmware/images/58503a-3704.bin
SHA=$(sha256sum "$IMAGE" | cut -d' ' -f1)
mkdir -p captures/bdm
```

1. **Dry run.**

   ```
   $PY -I tools/bdm/flash_host.py --pod usb --image "$IMAGE" --dry-run
   ```

   Success: the SIM's entry state and every chip select read back, the
   routine and vector table read back, the image's lane sums holding,
   the dump's SHA-256 `$SHA`, "sector pairs to write: none",
   and the last line saying the CPU is stopped in background mode.  No
   flash command, no code run.  If the SHA-256 is another image's
   (`sha256sum third_party/firmware/images/58503a-*.bin`), that image
   is the unit's: use it as `$IMAGE` from here on.

2. **Identify.**

   ```
   $PY -I tools/bdm/flash_host.py --pod usb --image "$IMAGE" --identify
   ```

   Success: as step 1, and for both pairs manufacturer `0x0101`,
   device `0x2020` and protection all zero.  It is the first code the
   CPU runs, sending only autoselect and read/reset, and proves the
   call: registers, VBR, the run, BGND and the PC after it.

3. **A trial write**: the unit's image with the first word of the blank
   sector pair `0x70000` changed and the lane sums fixed.  Step 1 showed
   the flash holds `$IMAGE`, so the plan is against it.

   ```
   python3 -I tools/bdm/trial_image.py "$IMAGE" captures/bdm/trial.bin
   python3 -I tools/bdm/flash_host.py --plan "$IMAGE" captures/bdm/trial.bin
   $PY -I tools/bdm/flash_host.py --pod usb --image captures/bdm/trial.bin --write
   ```

   Success: the lane sums hold; the dump's SHA-256 `$SHA`;
   the plan is `0x70000` and `0x78000`, pair 2's blank pair and the one
   holding its sums; each is erased, programmed and verified; the flash's SHA-256 is the trial image's and the run says
   success, then that the CPU is stopped.

4. **The original back.**

   ```
   $PY -I tools/bdm/flash_host.py --pod usb --image "$IMAGE" --write
   ```

   Success: the dump's SHA-256 the trial image's, the same
   two sector pairs, the flash's SHA-256 `$SHA`, success, the CPU
   stopped.

5. Then, by hand and only then, the pod's reset and run: the primary
   boots.

**Recovery.**  These steps write only pair 2's sector pairs
`0x70000` and `0x78000`, never the vectors, boot code and installer
below `0x10000`.  A step that fails leaves those two at worst erased
or half written and the sums wrong: the boot check then fails, and the
unit boots into the installer, which `smartclock-cli flash` can
reflash.  An image that differs below `0x10000` is written there too;
a failed write of those sector pairs leaves a unit that only BDM, or
pulling the socketed parts, can recover.  BDM does not need valid
flash: a reset entry stops the CPU before its first instruction, so
the host can always start again from step 1 and write the image back
with step 4's command.  After a time-out or a reset while running, the
CPU may still run the routine and a part may still be erasing, which
a reset does not stop (the parts have no RESET pin): reset into BDM,
and trust no read until a dry run's dump is stable.  Last resort: the
parts are socketed.

## Seen on the bench

On 2026-11-22 steps 1 to 5 ran in order, each passing, on
a 58503A with firmware 3704-C, through the mc683xx BDM pod: its Python
module `mc683xx_bdm` 0.3.0, protocol 11, opened on Linux as
`Pod("usb")`, DSCLK at the pod's default, 1 MHz (this unit's limit is
3 MHz).

1. Dry run: the dump's SHA-256 that of `58503a-3704.bin`.
2. Identify: for both pairs manufacturer `0x0101`, device `0x2020`,
   protection all zero.
3. Trial write: sector pairs `0x70000` and `0x78000` erased,
   programmed and verified; the flash's SHA-256 the trial image's.
4. The original back: the same two pairs; the flash's SHA-256 the
   3704 image's again, which a further dry run confirmed.
5. The pod's reset: the primary booted and answered `*IDN?`.

SIMCR read `0x60cf` at every reset entry; the system clock was
8388608 Hz.  In that run a dry run or an identify, each with its
512 KiB dump, took about 10 s, and each two-pair write, with its dump
before and the whole flash's hash after, about 25 s: times seen at the
bench, not figures any check gives.

The pod's behavior, unknown until then: the routine's BGND comes back
as an entry of its own, distinct from a breakpoint and a double bus
fault; closing the pod leaves the CPU stopped in background mode, as
it did across the five runs; and `go` after a reset entry needs
nothing beyond the host's own setup.

Not yet seen: a reset while the routine runs (the adapter's polling of
the RESET line), any failure path on hardware, a write below
`0x10000`, a whole-image write.  Nor anything a passing run cannot
show: the poll and erase margins and the DQ7 lag the routine allows
for, the wiring of WE#, an interrupt at level 7, electrical behavior.

## Adversarial review

**What breaks.**  Running code at all: once the CPU runs, the pod has
no control until BGND.  Against that: SYPCR (watchdog off, bus monitor
on), interrupts masked, every vector to BGND, finish's guard against
its own faults, and the host's timeout.  A routine that never returns
(a fault in the handler's own stack, a level-7 storm) leaves the host
waiting out its timeout and then touching nothing; only a reset ends
it, and a part erasing then keeps erasing.  Nothing guards the boot
region below `0x10000`: an image that differs there is written there,
and a failure mid-write leaves the unit to BDM or the socketed parts.
Nothing checks that an image is a 58503A's: the tool writes what it is
given, a Z3801A's included, and the lane sums, which prove an image
only self-consistent, are reported, not enforced.

**What it costs.**  Each trial is a reset entry, a cold start of the
oscillator loop.  The poll limits assume the fastest possible poll, so
without DQ5 a failed erase runs several times its 25 s before the
routine gives up, and the host's wait is longer still.  Building needs
the m68k binutils; the tests need uv and mc683xx.

**What it forecloses.**  Little: the routine uses the same chip-select
map as direct BDM writes, which stay possible.  The register interface
is a contract between the routine and the host, versioned together
through the routine's JSON.
