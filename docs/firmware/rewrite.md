# The Z3805A 3543B in C: method and verification

The Z3805A 3543B firmware, an HP SmartClock GPS receiver's CPU32
(MC68331) image, as hand-written C, and how that C is checked against
the image.  For a reader who knows C and the 68000 family but not this
project.  Each step is detailed elsewhere; this says what each is and
where.

## In short

- Every C function in the Z3805A 3543B's ROM, and in the installer it
  unpacks into RAM, has been rewritten by hand as readable C, starting
  from Ghidra's decompilation.  The routines that are not C (the pSOS
  and pForth kernels, exception entries, soft-float register routines)
  are kept as the image's own assembly, reassembled byte-identical.
- Each hand-written function is run in Unicorn beside the image's own
  copy, from the same state, and must leave the same results, RAM,
  registers and device and kernel calls, in the same order.  1313 of
  the primary's functions agree, 5 differ for known reasons, none
  fail; the installer's 236 agree.
- It is a checked reading of the firmware, not a replacement for it:
  it does not build a flashable image, and it has never run on a board.
- Reproduce with `make check-decompiled` and `make report-decompiled`;
  no Ghidra needed.

## What it is and is not

- **Is:** every C function of the image (`third_party/firmware/z3805a-3543b.bin`)
  and of the installer the boot code unpacks into RAM, decompiled by
  Ghidra through the project's scripts, then written again by hand, a
  file to a module, and each function run against the image's own
  code.  1318 of the primary's functions and 236 of the installer's are
  hand-written; the routines that are not C -- the pSOS kernel's, the
  pForth kernel's 183, the exception entries, the soft-float library's
  register routines, 474 in all -- are the image's own instructions,
  reassembled and checked byte for byte.  Left as the decompiler's: 12
  thunks, and one bare `rts` in the installer nothing calls.
- **Is not:** a buildable or flashable image.  The C is a *reading* of
  the firmware, checked function by function; it is compiled as one
  translation unit with the export, and the installer's copies share
  static bodies with the primary's through headers, which the real
  installer, alone in RAM, could not.
- The decompiler's own C is kept beside each function, inside `#if 0`.
- Faults of the firmware are reproduced, not fixed
  ([`quirks.md`](quirks.md)).

The compiler that built the image is not known; its calling
convention, read from the image's 5125 calls, matches no documented
one (`char` and `short` in 2-byte stack slots, value at the slot's
start; [`README.md`, "The calling convention"](README.md#the-calling-convention)).
So the target is readable C checked by running it, not a
byte-identical rebuild.

## The pipeline

Everything an image's work is lives in
[`third_party/firmware/z3805a-3543b/`](../../third_party/firmware/z3805a-3543b/),
beside the image.

| File | What it is |
| ---- | ---------- |
| `names.tsv` | every name the project has established, each row citing the note that shows it, with its prototype or type and calling convention (`kind`, `confidence`, `in`/`out` registers, `preserves`, `returns=ccr`, ...) |
| `smartclock.h`, `check-layout.c` | the structures and enums, `<stdint.h>` types, packed; `make check-types` fails if a size or offset is not the image's ([`structures.md`](structures.md)) |
| `scpi-tree.tsv`, `installer-scpi-tree.tsv`, `handlers.txt` | the SCPI trees' records and handlers, generated from the image by `smartclock-cli dump-scpi` |
| `results.tsv`, `modules.tsv` | names for call results; each function's module ([`modules.md`](modules.md)) |
| `untyped.tsv` | the ROM that is neither code nor typed data |
| `decompiled/` | the export: `z3805a-3543b.c`, the conventions sidecar `.conventions.jsonl`, and `.symbols.ld` |
| `rewritten/` | the hand-written C |

The image-independent headers, the MC68331's modules, the 68681 DUART
and the kernel's signals, are in
[`third_party/firmware/include/`](../../third_party/firmware/include/).

The Ghidra scripts, in [`tools/ghidra/`](../../tools/ghidra/), run in
order over the image imported raw at address 0 as `68000:BE:32:CPU32`
([`README.md`, "Decompiling with Ghidra"](README.md#decompiling-with-ghidra)):

1. `SmartClockSetup.java` -- memory map (ROM read-only so loads fold;
   devices volatile), vector tables, string tables; recovers the
   compiler's switch tables, which Ghidra resolves 12 bytes late, and
   the pSOS service dispatch, which analysis never follows; ends flow
   at `stop` and at `trap #5`, the kernel's return from interrupt.
2. `SmartClockInstaller.java` -- unpacks the installer's records into
   an overlay, `installer::`, its vectors and entry
   ([`restart.md`](restart.md), "Forced installer entry with an
   unusable primary").
3. `SmartClockSetup.java` again, now with the types, names, SCPI
   handlers and pForth words: names, prototypes, conventions and types
   go in.  A prototype is built only from a convention of confidence
   `run`, `read` or `documented`; below that a function gets a comment,
   never guessed arguments.
4. `SmartClockForth.java` -- the pForth kernel's register convention
   (A6 data stack, A5 user area, A4 dictionary, A3 input).
5. `SmartClockStackParameters.java` -- each function's stack
   parameters in custom storage, from its callers' pushes or its own
   reads, since Ghidra's 68000 language cannot model this compiler's
   2-byte slots.
6. `SmartClockFormatStrings.java` -- Ghidra's variadic analyzer over
   the printf family.
7. `SmartClockExportC.java` -- the export.  It prints each function
   from the decompiler's tokens and adds what the p-code says and the
   text does not: signedness of pieces, unsigned constants, switch case
   labels from the real table, bit reinterpretations between float and
   integer (there is no FPU, so every such cast is bits,
   `__ghidra_as`), shifts and rotates defined for every count, device
   register bits by name.  A routine that is not C is written as the
   image's instructions in a top-level `__asm__`, its decompiled C kept
   under `#if 0`.

`SmartClockUntyped.java`, optional, surveys what is left untyped.  The
project itself is `third_party/firmware/ghidra/smartclock.gar` (Git
LFS); the scripts rebuild everything in it from the image but names
given by hand.

The sidecar, `z3805a-3543b.conventions.jsonl`, gives every function's
address, note and convention, and for a stack-argument function each
argument's offset and size as the original lays them.  The harness lays
the image's arguments from it.

## The rewrite

[`README.md`, "Hand-written C"](README.md#hand-written-c);
[`modules.md`](modules.md), "The hand-written files".

- One file to a module, by subject, in four layers (platform, devices,
  services, applications), each calling its own or a lower one but for
  the calls `modules.md` lists.  The SCPI handlers go a file to a
  command tree.  The exporter includes every file of `rewritten/` and
  keeps the decompiler's reading of each function it finds there under
  `#if 0`.
- The installer's copies are in `rewritten/installer/`.  A copy that
  reads nothing of the installer's own shares its twin's body, a static
  function in the group's header (`libc.h`, `scpi_*.h`, `status.h`,
  `os.h`, ...); the rest are written on the installer's globals.
- The SCPI command trees are C data, generated, not written by hand
  (`scpi_tree_data.c`, `installer/installer_scpi_tree_data.c`;
  [`../scpi/README.md`, "As C"](../scpi/README.md#as-c)).
- Coding rules, from [`AGENTS.md`](../../AGENTS.md) and "Hand-written
  C": typed constants, coded values as enums shared through
  `smartclock.h`, `static const` tables with their image address, US
  English, Google style by `clang-format` (`make check-format-c`).
- Register-convention routines are called by binding: a header of
  `rewritten/bindings/` defines a function under the routine's own name
  and prototype that binds each argument to its register with a local
  register variable and makes the `jsr` in one asm statement, naming as
  outputs or clobbers exactly the registers the routine changes, read
  from its code and its callees'.  So gcc keeps its own values in the
  rest, as the image's callers do.  A routine returning in the condition
  codes or in extra registers is called in an asm statement of its own,
  a flag read by `Scc`.  The pSOS calls are `os.h`'s `os_psos_call` and
  `os_psos_io_call`, a shape to each service (`OS_SERVICES`): a register
  loaded only if the service reads it, named changed only if its handler
  changes it ([`psos.md`](psos.md)).
- The image's faults are kept, with a comment where each happens
  ([`quirks.md`](quirks.md)): `days_to_date`'s December 31 of a leap
  year as January 0, `gps_to_utc`'s inverted validity test, reads of
  what nothing set.  A register the image reads as its caller left it
  is read by one `cpu.h` helper.

## The verification

Three tools in [`tools/firmware/`](../../tools/firmware/), Python run
with uv on Unicorn.

**The rebuilt link** (`rebuild_link.py`).  Every assembly piece at its
own address, where it must be the image's bytes; gcc's code at
`0x600000`, above every chip select, its data at `0x6f0000`; ROM's data
where it was.  The image's copy of each C function stays in place,
renamed `__image_NAME`, so both copies of every function are in one
address space and the assembly keeps calling the original.

**The function check** (`function_check.py` on `harness.py`; `make
report-decompiled`).  For each C function, the image's copy and gcc's
copy are called in Unicorn from the same constructed state: the image's
arguments where the sidecar says the original reads them, gcc's in its
4-byte slots.  Compared after each call:

- the result, and pointers by where they point (the same text in ROM
  and among gcc's constants is the same; a C object and its twin in ROM
  are the same, `harness.Twins`);
- RAM, the EEPROM, the harness's pointee memory, and the installer's
  flash;
- effects, in order: every kernel call with the registers its service
  reads, messages by content, device reads and writes, QSPI transfers
  by the words queued, calls to routines the harness passes in;
- the registers the convention keeps for the caller (D2-D7, A2-A6 and
  any `preserves`), the status register's system byte, and the
  condition codes where the convention returns in them.

The constructed state:

- RAM zeroed but for what start-up makes, by the image's own code: the
  kernel's configuration and data area (`kernel_init`), the exchanges
  `root_task` creates, the status system (`status_init`); the installer
  on a profile of its own, its RAM as its entry at `0xa58` leaves it.
- A kernel model at traps #3 and #4, from the binding's stubs: each
  service's results set to a sentinel nothing is mapped at, each other
  register its code changes set to another, the rest kept; segments,
  task records and the messages a receiver expects modeled further.
  Traps #1 and #7 run the image's own handlers.
- Device models by the data sheets, ready at once, busy for one to five
  polls, with input waiting, and failing (`harness.DEVICE_CASES`): the
  DUART, SCI, QSPI, a debounced input port, the EEPROM's write cycle,
  and the installer's two pairs of AM29F010 flash parts, a command state
  machine each, with status polling (`harness.Flash`).
- Cases: eight generated per function (buffers of bytes, then of
  pointers, one fill making a list that never ends and one that does);
  given cases where the generated ones do not reach what callers pass
  (`GIVEN_CASES`); preset variables and hooked routines by case
  (`STATES`); variadic argument lists (`VARIADIC_CASES`); SCPI sessions
  through the parser's loop, a query below every child list of the tree
  (`SCPI_SESSIONS`); pForth console sessions, each copy's own
  interpreter and dictionary on typed lines (`CONSOLE_SESSIONS`).
- Context: a function reached from a task's entry runs in user mode,
  one also reached from supervisor code in both modes.  A case may take
  an interrupt part way, at the same point of each copy, framed as the
  CPU32 frames one (`harness.Interrupt`).

A difference whose cause is found and is not the C's is known
(`KNOWN`), and counts so only while every failing case matches its
pattern:

- `os_report_process` and its three callers print D5, which
  `os_report_processes` leaves holding the address of `"%s"`
  (`0x16768`): a firmware fault.
- `log_erase` checksums a pointer's bytes, which differ between the two
  copies.
- `gps_debug_status` clears the byte below an empty text, the lowest of
  its saved A4 (`0x49384`): a firmware fault.

**Planted bugs.**  Each part of the harness has a bug, planted in the
hand-written C, that it catches and the generated cases alone miss, for
example: a clobber left out of a binding (A1 of `float_add`,
D7 of the mutexes, D6 of the clock record); a wrong `Scc`; `volatile`
removed from a polled wait; a debounce one read short; a wrong flash
unlock address; two SCPI root children swapped; a pForth diagnostic
word's arguments swapped.  (README.md, "Hand-written C"; PLAN.md.)

**Other checks**, in `make check-decompiled`:

- `decompiled_check.py`: the export compiles with m68k gcc, and every
  assembly piece is the image's bytes, assembled alone and in the link.
- `bridge_check.py`: each binding, called through a function gcc
  compiles that keeps 14 values across it, against its routine called
  from registers.
- `module_figures.py`: `modules.md`'s figures.

And `make check-scpi-tree` (`scpi_tree_check.py`): each record of the
generated trees, as gcc lays it out, is the image's bytes, pointers
compared by what they point at.

**Figures** (`make report-decompiled`, `make check-decompiled`, `make
check-scpi-tree`):

| Check | Result |
| ----- | ------ |
| assembly (`check-decompiled`) | 474 routines in 494 pieces, all identical, alone and linked |
| bindings | 55 of the primary's and 11 of the installer's agree |
| SCPI trees (`check-scpi-tree`) | 2055 and 38 objects identical |
| function check, primary | 1313 agree, 5 known, 0 failed |
| function check, installer | 236 agree, 1 skipped, 0 failed |
| pForth console sessions | 8 of 8 agree |
| cases, in the agreeing functions | 17,750 of 18,355 compared, the rest skipped |

A case is skipped where both copies stop at what the harness does not
model (an unmapped address, a service it does not answer); a function
is skipped when all its cases are: `installer_status_register_setup`,
whose image copy writes unmapped memory on the harness's state.  The user-mode
counts are in [`README.md`](README.md#hand-written-c), checked by `make
check-report-figures`.

**What the figures mean.**  On every case run, the hand-written C does
what the image does, as far as the comparisons above see.  They do not
mean every input: a case reaches what the generated, given and session
cases reach, and the report says how many of each function's cases were
compared ("6 of 8 cases run").  They are one function at a time, the
rest of the system the image's or a model.  And the devices and the
kernel are models: written from data sheets and the image, the same
for both copies, so a wrong model is wrong for both and hides nothing
between them, but a function checked against one is checked against the
model, not the board.

## Limits

- **Concurrency.**  Tasks never interleave; the only preemption is an
  interrupt a case injects at a chosen point.  A race between tasks, or
  a kernel call overwriting what another task holds, is not seen.
- **Timing.**  The harness models no time: a poll's count and order are
  compared, its duration is not.  Unicorn has no CPU32; the 68020
  stands in, the image using none of the instructions where they differ
  (PLAN.md).
- **Untraced hardware.**  Which wires reach what (the DUART's interrupt
  level, the EEPROM's part, the SCI's connector, the switch word at
  `0x302000`) is inferred from the firmware, not traced on a board:
  [`../hardware-investigations.md`](../hardware-investigations.md)
  lists each with its bench test.  The primary's own stray writes to
  the flash range are ended as bus errors, not modeled.
- **Inferred.**  The calling convention, the pSOS 4.1 service
  semantics (no manual for 4.1 is at hand; [`psos.md`](psos.md)), and
  every name and type are readings of the image, each row of
  `names.tsv` citing its evidence and confidence.
- **Not run.**  The whole system: no rebuilt image has booted.

## Reproducing

Tools: Ghidra 12.1.4, under `analyzeHeadless` or ghidra-mcp's
`run_ghidra_script` (only to export again), `git-lfs` for the archive; `m68k-linux-gnu-gcc` and
binutils; `clang-format`; Python 3.11+ with uv (Unicorn, capstone);
Rust (`cargo`, for `smartclock-cli dump-scpi`).  No Ghidra is needed
for the checks: the export is checked in.

    make -C third_party/firmware check-types IMAGE=z3805a-3543b
    make -C third_party/firmware check-decompiled IMAGE=z3805a-3543b    # about 15 s
    make -C third_party/firmware report-decompiled IMAGE=z3805a-3543b   # about 2.5 min
    make -C third_party/firmware check-scpi-tree IMAGE=z3805a-3543b
    make -C third_party/firmware check-format-c

Each is also a top-level target (`make check-decompiled`).  Results
land in `target/firmware/z3805a-3543b/` (`decompiled.o`,
`report-decompiled.txt`).  To export again: restore
`smartclock.gar`, run the scripts in order with the arguments
"Decompiling with Ghidra" gives, and diff the export.

## Provenance and licensing

[`third_party/NOTICE`](../../third_party/NOTICE) splits it.  The image,
the EEPROM dumps, the FPGA bitstreams and the manuals are the
vendors'.  The image folder, `include/` and the Makefile are the
project's analysis; what they reproduce of the image -- the routines
carried as bytes, the tables copied -- remains the image's copyright
holder's.  The decompiled C is Ghidra's machine output, rewritten where
it would not compile or would not do what the image does.  The pSOS
manual consulted (Integrated Systems, *pSOSystem System Calls*,
000-5070-002) is cited, not included.  The harness runs on Unicorn,
GPL-2.0-only, as a local tool.

The 58503A 3704 port ([`58503a-3704.md`](58503a-3704.md)) and the
emulator that boots the image ([`../emulator.md`](../emulator.md)) are
in progress and not covered here.
