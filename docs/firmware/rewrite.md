# The Z3805A 3543B in C: method and verification

The Z3805A 3543B firmware (CPU32, MC68331) as hand-written C, and how
that C is checked against the image.  Each step is detailed elsewhere;
this says what each is and where.

## In short

- Every C function in the Z3805A 3543B's ROM, and in the installer it
  unpacks into RAM, has been rewritten by hand as readable C, starting
  from Ghidra's decompilation.  The routines that are not C (the pSOS
  and pForth kernels, exception entries, soft-float register routines)
  are kept as the image's own assembly, reassembled byte-identical.
- Each hand-written function is run in Unicorn beside the image's own
  copy, from the same state, and must leave the same results, RAM,
  registers and device and kernel calls, in the same order.  1312 of
  the primary's functions agree, 6 differ for known reasons, none
  fail; the installer's 236 agree.
- It is a checked reading of the firmware, not a replacement for it:
  it does not build a flashable image, and it has never run on a board.
- Reproduce with `make check-decompiled` and `make report-decompiled`;
  no Ghidra needed.

## Scope

- Hand-written: 1318 of the primary's functions and 236 of the
  installer's (`third_party/firmware/z3805a-3543b.bin`), a file to a
  module.  Kept as the image's instructions, reassembled and checked
  byte for byte: 474 routines -- the pSOS kernel's, the pForth
  kernel's 183, the exception entries, the soft-float library's
  register routines.  Left as the decompiler's: 12 thunks, and one
  unreferenced `rts` in the installer.
- Not buildable or flashable.  The C is compiled as one translation
  unit with the export, and the installer's copies share static bodies
  with the primary's through headers, which the real installer, alone
  in RAM, could not.
- The decompiler's C is kept beside each assembly routine under
  `#if 0`; a function written by hand has a one-line marker in its
  place, and the sidecar lists its reading's calls.
- The firmware's faults are reproduced, not fixed
  ([`quirks.md`](quirks.md)).

The compiler is not identified; its calling convention, read from the
image's 5125 calls, matches no documented one (`char` and `short` in
2-byte stack slots, value at the slot's start;
[`README.md`, "The calling convention"](README.md#the-calling-convention)).
Hence the target is C checked by execution, not a byte-identical
rebuild.

## The pipeline

Each image's work lives in
[`third_party/firmware/z3805a-3543b/`](../../third_party/firmware/z3805a-3543b/),
beside the image.

| File | What it is |
| ---- | ---------- |
| `names.tsv` | every established name, each row citing its note, with prototype or type and calling convention (`kind`, `confidence`, `in`/`out` registers, `preserves`, `returns=ccr`, ...) |
| `smartclock.h`, `check-layout.c` | structures and enums, `<stdint.h>` types, packed; `make check-types` fails on any size or offset not the image's ([`structures.md`](structures.md)) |
| `scpi-tree.tsv`, `installer-scpi-tree.tsv`, `handlers.txt` | the SCPI trees' records and handlers, from `smartclock-cli dump-scpi` |
| `results.tsv`, `modules.tsv` | names for call results; each function's module ([`modules.md`](modules.md)) |
| `untyped.tsv` | ROM that is neither code nor typed data |
| `decompiled/` | the export: `z3805a-3543b.c`, the conventions sidecar `.conventions.jsonl`, `.symbols.ld` |
| `rewritten/` | the hand-written C |

Image-independent headers (MC68331 modules, 68681 DUART, kernel
signals) are in
[`third_party/firmware/include/`](../../third_party/firmware/include/).

The Ghidra scripts, [`tools/ghidra/`](../../tools/ghidra/), run in
order over the image imported raw at 0 as `68000:BE:32:CPU32`
([`README.md`, "Decompiling with Ghidra"](README.md#decompiling-with-ghidra)):

1. `SmartClockSetup.java` -- memory map (ROM read-only so loads fold;
   devices volatile), vector tables, string tables; recovers the
   compiler's switch tables, which Ghidra resolves 12 bytes late, and
   the pSOS service dispatch, which analysis never follows; ends flow
   at `stop` and at `trap #5`, the kernel's return from interrupt.
2. `SmartClockInstaller.java` -- unpacks the installer's records into
   the overlay `installer::`, with its vectors and entry
   ([`restart.md`](restart.md), "Forced installer entry with an
   unusable primary").
3. `SmartClockSetup.java` again, with the types, names, SCPI handlers
   and pForth words.  A prototype is built only from a convention of
   confidence `run`, `read` or `documented`; below that a function gets
   a comment, never guessed arguments.
4. `SmartClockForth.java` -- the pForth kernel's register convention
   (A6 data stack, A5 user area, A4 dictionary, A3 input).
5. `SmartClockStackParameters.java` -- stack parameters in custom
   storage, from callers' pushes or the callee's reads; Ghidra's 68000
   language cannot model 2-byte slots.
6. `SmartClockFormatStrings.java` -- Ghidra's variadic analyzer over
   the printf family.
7. `SmartClockExportC.java` -- the export, printed from the
   decompiler's tokens plus what the p-code carries and the text drops:
   signedness of pieces, unsigned constants, switch case labels from
   the real table, float/integer bit reinterpretations (`__ghidra_as`;
   no FPU, so every such cast is bits), shifts and rotates defined for
   every count, device register bits by name.  A routine that is not C
   is emitted as the image's instructions in a top-level `__asm__`, its
   decompiled C under `#if 0`.

`SmartClockUntyped.java`, optional, surveys what is left untyped.  The
project is `third_party/firmware/ghidra/smartclock.gar` (Git LFS); run
over the bare image or over it, the scripts reproduce the checked-in
export byte for byte (Reproducing, below).  The scripts run under `analyzeHeadless` (Reproducing, below)
or [ghidra-mcp](https://github.com/bethington/ghidra-mcp)'s
`run_ghidra_script` (7.0.0, at `cf8b12d`).

The sidecar, `z3805a-3543b.conventions.jsonl`, gives every function's
address, note and convention, and for a stack-argument function each
argument's offset and size in the original's layout.  The harness lays
out the image's arguments from it.

## The rewrite

[`README.md`, "Hand-written C"](README.md#hand-written-c);
[`modules.md`](modules.md), "The hand-written files".

- One file to a module, in four layers (platform, devices, services,
  applications), each calling its own or a lower one but for the calls
  `modules.md` lists.  SCPI handlers go a file to a command tree.  The
  exporter includes every file of `rewritten/` and leaves the
  decompiler's version of each function found there out, a
  `/* NAME: rewritten in FILE */` line in its place.
- Installer copies are in `rewritten/installer/`.  A copy that reads
  none of the installer's own state shares its twin's body, a static
  function in the group's header (`libc.h`, `scpi_*.h`, `status.h`,
  `os.h`, ...); the rest are written on the installer's globals.
- The SCPI command trees are generated C data
  (`scpi_tree_data.c`, `installer/installer_scpi_tree_data.c`;
  [`../scpi/README.md`, "As C"](../scpi/README.md#as-c)).
- Coding rules, from [`AGENTS.md`](../../AGENTS.md) and "Hand-written
  C": typed constants, coded values as enums shared through
  `smartclock.h`, `static const` tables with their image address, US
  English, Google style by `clang-format` (`make check-format-c`).
- Register-convention routines are called through bindings: a header in
  `rewritten/bindings/` defines a function under the routine's name and
  prototype that binds each argument with a local register variable and
  does the `jsr` in one asm statement, whose outputs and clobbers are
  exactly the registers the routine and its callees change.  A routine
  returning in the condition codes or extra registers is called in an
  asm statement of its own, a flag read by `Scc`.  pSOS calls are
  `os.h`'s `os_psos_call` and `os_psos_io_call`, one shape per service
  (`OS_SERVICES`): a register loaded only if the service reads it,
  clobbered only if its handler changes it ([`psos.md`](psos.md)).
- Firmware faults are kept, commented where each happens
  ([`quirks.md`](quirks.md)): `days_to_date`'s December 31 of a leap
  year as January 0, `gps_to_utc`'s inverted validity test, reads of
  what nothing set.  A register the image reads as its caller left it
  is read by one `cpu.h` helper.

## Shared C

C that more than one image runs lives once, in
`third_party/firmware/common/`; everything else stays in the image's
own `rewritten/`.  A function moves there only by these rules, so that
nothing is taken for common that is not:

1. **Same code.**  `image_diff.py` classes it *identical* or
   *relocated* in every image that will share it: the same
   instructions, differing only in addresses the linker fills in.  A
   *changed* function, however small the change, stays per image.
2. **Same data.**  Every type it touches is the same in each image:
   it comes from a header in `common/`, and each image's
   `check-layout.c` asserts the same offsets for it.  A function that
   touches a structure laid out differently in some image stays per
   image, or is compiled once per image against that image's own
   header; it is never compiled once for all.
3. **Addresses stay the image's.**  Globals and callees resolve per
   image at link time, through each image's symbol file; `common/`
   holds no address.
4. **Every image proves the move.**  After a move, each image that
   shares the function reruns its function check and its console
   sessions, and its figures must not change.  A function wrongly taken for common fails one of them.
5. **Small steps.**  The libraries first (soft-float, the pSOS glue,
   pForth, the SCPI engine), then module by module, each move a commit
   of its own, so that a failure points at one move and reverts
   cleanly.
6. **A difference found later splits it.**  When a new image's copy of
   a common function differs, that image gets its own copy in
   `rewritten/`; `common/` is not edited to cover both with a
   condition.

How the build uses it.  `common/` is laid out as `rewritten/` is
(`bindings/`, `installer/`), and an image that shares it gives the
exporter two folders, `rewritten=third_party/firmware/common` before
its own: the export includes `common/`'s headers, then its own, and
`common/`'s files, then its own, so each image compiles the shared C
in its own translation unit, against its own export's types,
prototypes and globals (rules 2 and 3).  A shared type is defined once,
in a header in `common/types/`, which each sharing image's
`smartclock.h` includes and the exporter skips, since the export
declares the program's types itself; Setup expands an `#include
"PATH"` in a `types=` header, which Ghidra's parser takes none of.  An enum
whose members differ between the images is split rather than shared
whole: the run of members the images have in common, at the same
values, is a `common/types/` header that each image's definition
includes among its members (`status_scpi.h`, the SCPI standard's head
of `status_condition_code`; `qspi_device.h`, the QSPI's devices from
1), the rest the image's own, so that no value changes; to shared code such an enum is the same when its storage is
and the members the code names are the shared run's; one whose members it
names none of, a field's type in a shared record (`gps_request_code`
in `scpi_numeric_param`), need only have the same storage.  A comment in `common/` cites no address: a
function by its name, a place in it as `name+0xoff`; a device's
register, the board's, is no image's address.  `make
check-common` (`tools/firmware/common_check.py`) checks rules 1 to 3
for every file there: the diff's class of each function it defines or
binds, the same declaration in each sharing export of all its code
names (types, enumerators, constants, macros, callees' and globals'
types), each shared type in a `common/types/` header every sharing
`smartclock.h` includes or in `include/`, with the same layout in each
`check-layout.c`, each enumerator it names in one too, a split enum's
storage the same, and no address.

## The verification

Three tools in [`tools/firmware/`](../../tools/firmware/), Python under
uv on Unicorn.

**The rebuilt link** (`rebuild_link.py`).  Each assembly piece at its
own address, where it must be the image's bytes; gcc's code at
`0x600000`, above every chip select, its data at `0x6f0000`; ROM data
in place.  The image's copy of each C function stays in place, renamed
`__image_NAME`, so both copies share one address space and the
assembly keeps calling the original.

**The function check** (`function_check.py` on `harness.py`; `make
report-decompiled`).  Each C function's image copy and gcc copy are
called in Unicorn from the same constructed state: the image's
arguments where the sidecar says the original reads them, gcc's in
4-byte slots.  Compared after each call:

- the result, pointers by target (identical text in ROM and in gcc's
  constants is equal; a C object and its ROM twin are equal,
  `harness.Twins`);
- RAM, the EEPROM, the harness's pointee memory, the installer's flash;
- effects, in order: every kernel call with the registers its service
  reads, messages by content, device reads and writes, QSPI transfers
  by the words queued, calls to routines the harness passes in;
- callee-saved registers (D2-D7, A2-A6 and any `preserves`), the status
  register's system byte, and the condition codes where the convention
  returns in them.

The constructed state:

- RAM zeroed but for what start-up makes, by the image's own code: the
  kernel's configuration and data area (`kernel_init`), the exchanges
  `root_task` creates, the status system (`status_init`); the installer
  on its own profile, its RAM as its entry at `0xa58` leaves it.
- A kernel model at traps #3 and #4, from the bindings' stubs: each
  service's results set to a sentinel at an unmapped address, each other
  register its code changes set to another, the rest kept; segments,
  task records and the messages a receiver expects modeled further.
  Traps #1 and #7 run the image's own handlers.
- Device models from the data sheets, ready at once, busy for one to
  five polls, with input waiting, and failing (`harness.DEVICE_CASES`):
  the DUART, SCI, QSPI, a debounced input port, the EEPROM's write
  cycle, and the installer's two pairs of AM29F010 flash parts, each a
  command state machine with status polling (`harness.Flash`).
- Cases: eight generated per function (byte buffers, then pointer
  buffers, one fill making an unterminated list and one a terminated
  one); given cases where generated ones miss what callers pass
  (`GIVEN_CASES`); preset variables and hooked routines per case
  (`STATES`); variadic argument lists (`VARIADIC_CASES`); SCPI sessions
  through the parser's loop, a query below every child list of the tree
  (`SCPI_SESSIONS`); pForth console sessions, each copy's own
  interpreter and dictionary on typed lines (`CONSOLE_SESSIONS`).
- Context: a function reached from a task's entry runs in user mode,
  one also reached from supervisor code in both.  A case may take an
  interrupt part way, at the same point in each copy, with a CPU32
  exception frame (`harness.Interrupt`).

A difference whose cause is found and is not the C's is known
(`KNOWN`), and counts so only while every difference of every failing
case matches its pattern: each case's comparisons list everything that
differs, every run of differing memory and every differing effect, so
that a known difference hides no other:

- `os_report_process` and its three callers print D5, which
  `os_report_processes` leaves holding the address of `"%s"`
  (`0x16768`): a firmware fault.
- `log_event` pushes only its message's format (`0x31a0a`), so the
  entry's two longs are whatever lies above it, its return address and
  the long holding its `char`, each copy's own: they differ, with their
  page's checksum, and nothing else does (`LOG_EVENT_FILL`).  The C
  logs the image's own texts (`log_text_*` in `names.tsv`), so that an
  entry's format, an address kept in the EEPROM, is the image's.
- `gps_debug_status` clears the byte below an empty text, the lowest of
  its saved A4 (`0x49384`): a firmware fault.
- `satellite_status_list` keeps the visible satellites not in its set
  in a local list of 12 (`0x4985e`), which more overflow: in the image
  over its end pointer, so that its blank entries run to the end of RAM
  (`0x499de`), in gcc's frame over whatever gcc put there.  A firmware
  fault, in each copy's own frame.

**Planted bugs.**  Each part of the harness catches a bug planted in
the hand-written C that the generated cases alone miss, for example: a
clobber left out of a binding (A1 of `float_add`, D7 of the mutexes,
D6 of the clock record); a wrong `Scc`; `volatile` removed from a
polled wait; a debounce one read short; a wrong flash unlock address;
two SCPI root children swapped; a pForth diagnostic word's arguments
swapped; a wrong RAM write or console line after a known difference
(`log_erase`, `os_process_report`); a copy stopping differently from
the image's (`installer_status_register_setup`); D1 or A0 out of
`qspi_transfer_raw`'s clobbers, seen only on a completed transfer.  (README.md, "Hand-written C"; PLAN.md.)

**Other checks**, in `make check-decompiled`:

- `decompiled_check.py`: the export compiles with m68k gcc, and every
  assembly piece is the image's bytes, assembled alone and in the link.
- `bridge_check.py`: each binding, called from a gcc-compiled function
  that keeps 14 values live across it, against its routine called from
  registers; `qspi_transfer_raw` also on transfers the QSPI's interrupt
  completes, which the generated cases never reach (`given_cases`), each of
  which must run.
- `module_figures.py`: `modules.md`'s figures.

And `make check-scpi-tree` (`scpi_tree_check.py`): each record of the
generated trees, as gcc lays it out, is the image's bytes, pointers
compared by target.

**Figures** (`make report-decompiled`, `make check-decompiled`, `make
check-scpi-tree`):

| Check | Result |
| ----- | ------ |
| assembly (`check-decompiled`) | 474 routines in 494 pieces, all identical, alone and linked |
| bindings | 73 of the primary's and 18 of the installer's agree, every `register` routine's |
| SCPI trees (`check-scpi-tree`) | 2055 and 38 objects identical |
| function check, primary | 1312 agree, 6 known, 0 skipped, 0 failed |
| function check, installer | 236 agree, 0 known, 1 skipped, 0 failed |
| pForth console sessions | 8 of 8 agree |
| cases, in the agreeing functions | 17,848 of 18,447 compared, the rest skipped |
| stack headroom | no case deeper than the image's by more than 192 bytes, or 16 above a floor; the console's data stack 56 bytes clear, the image's 58 |

A case is skipped where both copies stop at something the harness does
not model, and for the same reason: the same kind of fault (an
unmapped read, write or fetch, at whatever address each copy's code
reaches first), the same trap and call, or no return.  Stopping for
different reasons fails.  A function is skipped when all its cases
are: `installer_status_register_setup`,
whose image copy writes unmapped memory on the harness's state.  The
user-mode counts are in [`README.md`](README.md#hand-written-c),
checked by `make check-report-figures`.

**What the figures mean.**  On every case run, the hand-written C does
what the image does, as far as the comparisons above see.  Coverage is
what the generated, given and session cases reach; the report gives
each function's compared cases ("6 of 8 cases run").  Each function is
checked alone, the rest of the system the image's or a model.  Device
and kernel models, written from data sheets and the image, are shared
by both copies, so a wrong model hides no difference between them, but
the check is against the model, not the board.

## Limits

- **Concurrency.**  Tasks never interleave; the only preemption is an
  interrupt a case injects at a chosen point.  A race between tasks, or
  a kernel call overwriting what another task holds, is not seen.
- **Timing.**  No time is modeled: a poll's count and order are
  compared, its duration is not.  Unicorn has no CPU32; the 68020
  stands in, the image using none of the instructions where they differ
  (PLAN.md).  Unicorn's read of the status register spoils the
  condition codes, and so does a write of PC from a code hook; the
  harness reads them with a probe of its own and takes an interrupt
  between runs ([`README.md`, "Hand-written C"](README.md#hand-written-c)).
- **Untraced hardware.**  Which wires reach what (the DUART's interrupt
  level, the EEPROM's select line, the SCI's connector, the switch word
  at `0x302000`) is inferred from the firmware, not traced on a board:
  [`../hardware-investigations.md`](../hardware-investigations.md)
  lists each with its bench test.  The EEPROM's part is identified, from
  photographs: U4, an AT28C64B-20JC, chip select 9's by elimination.
  The primary's own stray writes to the flash range are ended as bus
  errors, not modeled.
- **Inferred.**  The calling convention, the pSOS 4.1 service
  semantics (no 4.1 manual at hand; [`psos.md`](psos.md)), and every
  name and type are readings of the image, each row of `names.tsv`
  citing its evidence and confidence.
- **Not run.**  The whole system: no rebuilt image has booted on a
  board.

## Reproducing

Tools: Ghidra 12.1.4 (only to export again), `git-lfs` for the archive;
`m68k-linux-gnu-gcc` and binutils; `clang-format`; Python 3.11+ with uv
(Unicorn, Capstone); Rust (`cargo`, for `smartclock-cli dump-scpi`).
The checks need no Ghidra: the export is checked in.

    make -C third_party/firmware check-types IMAGE=z3805a-3543b
    make -C third_party/firmware check-decompiled IMAGE=z3805a-3543b    # about 15 s
    make -C third_party/firmware report-decompiled IMAGE=z3805a-3543b   # about 2.5 min
    make -C third_party/firmware check-scpi-tree IMAGE=z3805a-3543b
    make -C third_party/firmware check-format-c

Each is also a top-level target (`make check-decompiled`).  Without
`IMAGE` each runs for every image folder it can: `report-decompiled`
for the 58503A 3704's too, its function check held to the same
standard ([`58503a-3704.md`](58503a-3704.md), "The function check").  Results
land in `target/firmware/z3805a-3543b/` (`decompiled.o`,
`report-decompiled.txt`).

To export again, from the repository's root, with `GHIDRA` the Ghidra
install and `PROJECT` a new, empty directory outside the repository
(Ghidra refuses a project path with an element starting with `.`),
import the bare image and run every script in order, in one session,
after auto-analysis, the program saved at the end:

    "$GHIDRA"/support/analyzeHeadless "$PROJECT" smartclock \
      -import third_party/firmware/z3805a-3543b.bin \
      -loader BinaryLoader -loader-baseAddr 0 -processor 68000:BE:32:CPU32 \
      -scriptPath tools/ghidra \
      -postScript SmartClockSetup.java \
      -postScript SmartClockInstaller.java 0xbbc 0x10135c \
      -postScript SmartClockSetup.java \
        types=third_party/firmware/z3805a-3543b/smartclock.h \
        types=third_party/firmware/include/mc68331.h \
        types=third_party/firmware/include/mc68681.h \
        names=third_party/firmware/z3805a-3543b/names.tsv \
        names=third_party/firmware/z3805a-3543b/scpi-tree.tsv \
        names=third_party/firmware/z3805a-3543b/installer-scpi-tree.tsv \
        scpi=third_party/firmware/z3805a-3543b/handlers.txt \
        pforth=docs/firmware/pforth/z3805a-3543b.txt \
      -postScript SmartClockForth.java \
      -postScript SmartClockStackParameters.java \
      -postScript SmartClockFormatStrings.java \
      -postScript SmartClockExportC.java \
        third_party/firmware/z3805a-3543b/decompiled/z3805a-3543b.c \
        defines=third_party/firmware/include/mc68331.h \
        defines=third_party/firmware/include/mc68681.h \
        defines=third_party/firmware/include/psos-signals.h \
        results=third_party/firmware/z3805a-3543b/results.tsv \
        rewritten=third_party/firmware/common \
        rewritten=third_party/firmware/z3805a-3543b/rewritten
    git diff --stat third_party/firmware/z3805a-3543b/decompiled/

The export is written in place, its `#include`s of `rewritten/` being
relative to it.  `SmartClockForth.java` prints `kernel span 00018ed6 -
0001ad27`.  The C, the sidecar and `.symbols.ld` come out identical to
the checked-in ones, and the same from a second import.

Over the archive instead, unpack it as File, Restore Project would:

    mkdir -p "$PROJECT"/smartclock.rep
    unzip -q third_party/firmware/ghidra/smartclock.gar -d "$PROJECT"/smartclock.rep
    mv "$PROJECT"/smartclock.rep/smartclock.gpr "$PROJECT"/
    rm "$PROJECT"/smartclock.rep/JAR_FORMAT

and replace the `-import` and `-loader` lines with `-process
z3805a-3543b.bin -noanalysis`.  That export is identical too, and so is
one run over the project a bare import left: each script is run again
over its own output.  What an earlier run or analysis left in a
project does not reach the export.  Setup removes the shared tails and
the functions with no bytes or no code that analysis makes, and the
labels and types an earlier run's names and headers gave that no row
or header gives now (`dmes_gpsm`, `robin_format`,
`scpi_root_children`), and
recomputes every body.  The names file types what was left to history,
the code runs the pForth control words compile, `watch`'s register
blocks and the boot code's self-test constants, and gives the
prototypes the hand-written C defines, which the decompiler had
committed in earlier passes.  `SmartClockStackParameters.java` keeps a
function that is not C at the unknown convention Setup gives it
([`README.md`, "Decompiling with Ghidra"](README.md#decompiling-with-ghidra)).

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

The 58503A 3704 port has its own document,
[`58503a-3704.md`](58503a-3704.md), held to this one's method and
checks (its "The function check"): of its function check, the primary's 1415
agree, 5 are known and none skipped or failed, and the installer's 247
agree, 1 skipped; its console sessions agree, 8 of 8.  The Z3801A
3543's, [`z3801a-3543.md`](z3801a-3543.md), seeded from the Z3805A's and
the third image to share `common/`, is held to the same standard: of
its function check, the primary's 1290 agree, 6 are known and 5
skipped, none failed, and the installer's 236 agree, 1 skipped; its
console sessions agree, 8 of 8.
