# A CPU32 core for smartclock: what this project needs

A brief for a separate project: a native MC68331 (CPU32) emulator
that `smartclock-emu` and the firmware harness can use in place of
Unicorn.

That project is [mc683xx](https://github.com/charlieh0tel/mc683xx),
on crates.io as `mc683xx-cpu32`, `mc683xx-modules`, `mc683xx-mc68331`,
`mc683xx-board` and `mc683xx-peripherals`, under MIT or Apache-2.0.
`smartclock-emu` pins it to an exact release (its `Cargo.toml`) and
runs on it by default (docs/emulator.md, "The mc683xx backend");
Unicorn stays a development core behind the `unicorn` feature.  The
brief is kept as the statement of what was asked; "Where it stands",
at the end, says what came of each part.

## Why

- Unicorn is GPL-2.0-only.  A build of the emulator with it is a
  feature-gated, local tool because of it (PLAN.md, "The emulator").  A core
  under a permissive license (MIT or Apache-2.0) could ship.
- Unicorn's m68k has bugs we work around: its status register read
  marks QEMU's lazy condition codes computed before computing them,
  and more (docs/emulator.md, "The condition codes").
- Both users want what Unicorn hides: exceptions taken exactly as the
  CPU32 takes them, and a clean interrupt entry.

## Not needed

Cycle accuracy.  The firmware's timing comes from devices (the FPGA's
counts, the periodic interrupt, the DUART's and SCI's character
times, the EEPROM's write cycle), modeled in board time, not from
instruction timing.  An estimate of cycles per instruction, good
enough to pace board time, is all `run` has to return.  Exact timing
can be a later layer if a routine is ever shown to need it.

## The CPU

- **The CPU32 instruction set** (CPU32RM): the 68000's, plus what the
  CPU32 adds: 32-bit multiply and divide, `extb`, `chk2`/`cmp2`,
  `bkpt`, `rtd`, `movec` (VBR, SFC, DFC, USP), `moves`, `lpstop`,
  `tbl`, `tblu`, `tbls`, `tblun`, `tblsn`, `bgnd` (illegal, or a halt
  the host sees; there is no BDM to enter).  No FPU, no MMU, no
  cache, no bit fields.
- **Exception processing exactly as the CPU32 does it**
  (MC68331UM, section 5): stack frame formats 0, 2 and C (the CPU32's
  bus error frame) and `rte` of each; the VBR; double fault halts;
  traps, `chk`, divide by zero, privilege, trace, line A and F.
- **Supervisor and user modes**, two stack pointers, and function codes
  on every access (supervisor or user, program or data), since chip
  selects can key on them.
- **Interrupts:** a level 1 to 7 and a vector (or autovector) from the
  host, taken between instructions when the mask admits it, level 7
  nonmaskable.  `stop` and `lpstop` wait for one.
- **Determinism:** the same inputs give the same run, independent of
  the host's speed.

## The interface

What `smartclock-emu`'s `Cpu` trait (`crates/smartclock-emu/src/cpu/mod.rs`)
already asks for:

- registers: today it reads A0 to A7, PC, SR and VBR; a new core
  should read and write all of them, D0 to D7, SSP and USP included;
- memory regions mapped as plain RAM or ROM (fast, in the core), and
  device regions sent to a host `Bus`: byte, word and long reads and
  writes, each with the board's current time;
- `reset` from the vectors at zero;
- `run(now, budget)`, which runs about `budget` cycles and returns
  cycles used and how it stopped: budget used, `stop`ped, or halted
  with a reason;
- `interrupt(level, vector)`, which says whether it was taken;
- tracing: device accesses, exceptions taken, a hook at a code address
  with the registers and top of stack, and an instruction count.

Nice to have: Python bindings (PyO3), so the firmware harness
(tools/firmware/harness.py and the `*_check.py` tools, Python on
Unicorn's bindings) can change core too.  Without them it stays on
Unicorn, a local tool, which also gives the new core an independent
oracle.  To keep the bindings a thin later layer, design the hooks as
plain callbacks from the start: on a code address, a block, a memory
read or write, an invalid access and an exception; with memory
protection, and saving and restoring the whole state, which the
harness uses.  A subprocess will not do: the hooks fire per
instruction and per access.

## The SoC blocks that move with it

The MC68331's own modules are the chip's, not the board's, so they
belong with the core, configured by the board:

- **SIM** (`board/sim.rs`): the clock synthesizer and its lock, reset
  status, ports E and F, the software watchdog, the periodic interrupt
  timer, the chip selects (kept as written today; could be enforced),
  interrupt arbitration (IARB) among the modules.
- **QSM** (`board/qsm.rs`): the SCI, at character times from SCCR0;
  the QSPI, its queue RAM and SPE/SPIF, with the peripherals on it
  supplied by the board.
- **GPT** (`board/gpt.rs`): the free-running counter and prescaler,
  input captures, output compares, the pulse accumulator and their
  interrupts; the pins it sees come from the board.
- **Board time** (`board/time.rs`): cycles of the CPU clock, from
  SYNCR and the 32.768 kHz reference.

The 68332's TPU and the 68340's DMA, timers and serial module are
other chips' modules.  They can come later if the core is to serve
those chips too.

## What stays in smartclock

The board: the MC68681 DUART, the AT28C64B EEPROM, the FPGA and its
counts, the ADC and DACs on the QSPI, the latches and LEDs, the GPS
engine and sky, the host port, scripts, and the memory map.  These
plug into the core's bus, the QSPI's devices and the modules' pins.

## Checking it

- Differential runs against Unicorn and Musashi, per instruction, on
  the instructions' results, with known Unicorn bugs excluded.
- The harness's per-function cases from both firmware images, as a
  ready corpus: a few thousand functions with known inputs and
  outputs.
- Unicorn's known faults (docs/emulator.md, "The condition codes") as
  regression tests the new core must get right.
- The emulator's boots of the Z3805A 3543B and the 58503A 3704, which
  must reach the same serial transcripts.
- Optionally, a real 68331 over BDM, if a board brings it out.

## Where it stands

- *The license*: MIT or Apache-2.0, so `smartclock-emu` builds with
  mc683xx by default, its `mc683xx` feature, and a build without
  Unicorn can ship.
- *The SoC blocks* live beside the core, not in it: the CPU32 in
  `mc683xx-cpu32`, the SIM, QSM and their parts in `mc683xx-modules`,
  the chip that joins them, the GPT among its parts, in
  `mc683xx-mc68331`.
- *What stays in smartclock* moved too: the board's standard chips,
  the DUART, the EEPROM, the latch, the switch byte's buffer, the ADC
  and the DACs, are `mc683xx-peripherals`' stock types, and a board is
  a file, `boards/*.toml`, that `mc683xx-board` loads.  smartclock
  keeps what is its board's own: the FPGA, the EFC's strobe, the
  58503A's front panel, the OCXO, the GPS engine and the analog
  sources (docs/emulator.md, "The mc683xx device layer").  The Unicorn
  board keeps smartclock's own models of all of them.
- *Python bindings*: mc683xx has them (mc683xx's `docs/python.md`),
  and the firmware harness runs on it alone
  (`tools/firmware/backend.py`).
- *Checking it*: the peripheral checks run each board's peripherals
  through the firmware on both cores, Unicorn the oracle
  (`tools/peripheral_check.py`; docs/emulator.md, "The peripheral
  checks"), and the boots' transcripts agree.
