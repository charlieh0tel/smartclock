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
- That none is missing is a link's result, not a count: `make
  check-link` links the image from the hand-written C and the assembly
  alone, the decompiled C left out, every function required (below,
  "Other checks").  The Z3805A 3543B's link requires all 2041 of its
  functions and finds each defined: 1555 by hand-written C, 474 by
  assembly and 12 as another routine's name.
- Each hand-written function is run in Unicorn beside the image's own
  copy, from the same state, and must leave the same results, RAM,
  registers and device and kernel calls, in the same order.  1312 of
  the primary's functions agree, 6 differ for known reasons, none
  fail; the installer's 237 agree.
- It is a checked reading of the firmware, not a replacement for it:
  it does not build a flashable image, and it has never run on a board.
- Reproduce with `make check-decompiled` and `make report-decompiled`;
  no Ghidra needed.

## Scope

- Hand-written: 1318 of the primary's functions and 237 of the
  installer's (`third_party/firmware/images/z3805a-3543b.bin`), a file to a
  module.  Kept as the image's instructions, reassembled and checked
  byte for byte: 474 routines -- the pSOS kernel's, the pForth
  kernel's 183, the exception entries, the soft-float library's
  register routines.  Left as the decompiler's: 12 thunks, each a
  branch Ghidra takes for one.  No C is left as the decompiler's, as
  the link check finds ("In short"); the last, `installer_cpu_register_test`, an unreferenced `rts` the three
  images share, is `common/`'s.
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
[`third_party/firmware/analysis/z3805a-3543b/`](../../third_party/firmware/analysis/z3805a-3543b/),
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
   no FPU, so every such cast, and a literal stored to a float, is bits),
   shifts and rotates defined for
   every count, device register bits by name.  A routine that is not C
   is emitted as the image's instructions in a top-level `__asm__`, its
   decompiled C under `#if 0`.

`SmartClockUntyped.java`, optional, surveys what is left untyped.
`NoSwitch.java` and `SwitchPass.java` run before and after auto-analysis
to make it deterministic: the stock Decompiler Switch Analysis applies
its results in a thread-dependent order, so `NoSwitch.java` turns it off
and `SwitchPass.java` runs it once the code is disassembled, one switch at
a time in address order.  With them a bare import reproduces the
checked-in export byte for byte for every image (Reproducing, below).  The
project is `third_party/firmware/ghidra/smartclock.gar` (Git LFS).  The
scripts run under `analyzeHeadless` (Reproducing, below)
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
- A function is named for what it does, not for the image it came
  from: a lead, a pairing the seeding finds changed below its carrying
  threshold, takes its base's name, its note saying how it was paired
  ([`PLAN.md`](../../PLAN.md), "Seeding a port's names"), and how a
  function differs from another image's of its name is its names
  file note's to say.  `make check-plain-names`
  (`tools/firmware/plain_names.py`) fails where the hand-written C or a
  names file's name column names a function with an image's suffix,
  `_58503a`, `_z3805a`, `_z3801a`, `_z3815a`, `_58503b` or `_z3816a`;
  text quoted, a file's name and an image's identifier
  (`58503a-3704`) are not names.  Where a plain name would clash in an
  image's link with a `common/` function of different code, the image
  keeps its own copy under the name, as rule 6 of "Shared C" has it,
  or, a static helper beside `common/`'s, a name of what it does
  (`startup_gpt_init_both_pwm`), never an address.  The function
  check seeds a function's draws by its name, so that a rename draws
  its cases afresh.
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
- A record's type is read from the code that reaches it, never from a
  base's layout alone: `make record-access IMAGE=... RECORD='START SIZE
  [--argument FUNCTION:OFFSET]... [--fit TYPE]'`
  (`tools/firmware/record_access.py`) lists every access the image's
  primary code makes into the record, through its address or a register
  or argument holding one, with its size, function and instruction, and
  with `--fit` each that does not land on a field of the type's of its
  size; `check-layout.c` then asserts the fields at the addresses that
  code gives.
- Firmware faults are kept, commented where each happens
  ([`quirks.md`](quirks.md)): `days_to_date`'s December 31 of a leap
  year as January 0, `gps_to_utc`'s inverted validity test, reads of
  what nothing set.  A register the image reads as its caller left it
  is read by one `cpu.h` helper.

## Shared C

C that more than one image runs lives once, in
`third_party/firmware/rewritten/common/`; everything else stays in the image's
own `rewritten/`.  A function moves there only by these rules, so that
nothing is taken for common that is not:

1. **Same code.**  `image_diff.py` classes it *identical* or
   *relocated* in every image that will share it: the same
   instructions, differing only in addresses the linker fills in.  A
   *changed* function, however small the change, stays per image.
2. **Same data.**  Every type it touches is the same in each image:
   it comes from a header in `common/`, and each image's
   `check-layout.c` asserts the same offsets for it, each read at that
   image's own addresses, and every field it reaches through a global,
   whose offset an absolute address in the image's code holds.  A function that
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
   condition.  Since an image that shares `common/` takes all of it,
   a function the new image lacks or changes leaves `common/` for a
   copy in each sharing image's own C before the new image joins: the
   Z3817A 3752 sent back the hex DAC, the interpolator and its
   self-tests, the :OUTPut:PIN1 handlers and the root's unsupported
   entries, which it lacks, and the FPGA's loader and readback check,
   the QSPI's FPGA writes, holdover's TI statistics, the self-tests'
   run over their list, one entry shorter, and the rest its code
   changes or its data lays otherwise (PLAN.md, "a second image is ported by copying the
   first's work").

How the build uses it.  `common/` is laid out as `rewritten/` is
(`bindings/`, `installer/`), and an image that shares it gives the
exporter two folders, `rewritten=third_party/firmware/rewritten/common` before
its own: the export includes `common/`'s headers, then its own, and
`common/`'s files, then its own, so each image compiles the shared C
in its own translation unit, against its own export's types,
prototypes and globals (rules 2 and 3).  One translation unit lets a
file name what a file included before it defines; none does.  A file
names only what the export declares, what the headers it includes
define and its own definitions, so that what two files share, a constant, a
type or a static helper, is a header's, whose first comment says who
shares it: `common/ti.c`'s registers' loads, which each image's
`ti.c` calls, are `common/ti.h`'s, and the Z3815A 4010's
`MONITORS_REFERENCE_VALID`, which its `pll_stages.c` stores, is its
`monitors.h`'s.  `make check-alone` (`tools/firmware/alone_check.py`)
holds every image to it: it compiles each `.c` the export includes on
its own, after the export's declarations and every hand-written
header, a precompiled header compiled once, so that a file naming what
only another file defines fails, which the one translation unit
would let pass.  A shared type is defined once,
in a header in `common/types/`, which each sharing image's
`smartclock.h` includes and the exporter skips, since the export
declares the program's types itself; Setup expands an `#include
"PATH"` in a `types=` header, which Ghidra's parser takes none of.  An enum
whose members differ between the images is split rather than shared
whole: the run of members the images have in common, at the same
values, is a `common/types/` header that each image's definition
includes among its members (`status_scpi.h`, the SCPI standard's head
of `status_condition_code`; `gps_request_sender.h`, the GPS requests' senders to 2;
`satellite_cell_status.h`, the status screen's satellite cells to 4;
`satellite_health_class.h`, `satellite_track_class`'s classes to 3;
`timecode_format.h`, the time code's formats to T2), the rest the image's own, so that no value changes.  A record is not split: one that differs in any image, in a field, its length or its tail, is entirely each image's own, in each image's `smartclock.h`, and `common/types/` holds no shared head of a per-image record (decided 2026-11-06).  A shared function that names such a record leaves `common/` for each image's own file, the same C in each (`boot_selftest_results`, the boot code's self-test results, whose last word the Z3815A 4010 keeps as the output latch's shadow, where the others keep a byte and its pad, and `installer_scpi_query_DIAGnostic_TEST`, which names it; `counter_configuration_fields`, which the Z3815A 4010's eight FPGA registers took from `counter_work_state.h`, and `counter_init`; `fconf_regs`, eight there, took `fpga_read` and `fpga_address_sweep_debug` from `common/fpga.c`).  No shared head of a record is left: `gps_request_entry`, a GPS request, two bytes longer in the Z3815A 4010, is each image's own too, its shared head of fields to `sent` backed out the same day.  The split enums' heads above stay in `common/types/`, by decision, as shared code names their members; two went back to each image's `smartclock.h` when the Z3817A 3752's split left no shared code naming their members, its own enums sharing none of them: `qspi_device`'s devices from 1 and `fpga_bit_index`'s FPGA bits; the more aggressive commonizing pass planned for later revisits them.  To shared code such an enum is the same when its storage is
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
storage the same, each field it reaches through a global asserted in
every `check-layout.c`, and no address.  The layouts compared are each
image's own: `tools/firmware/layout_addresses.py`, which `check-common`
runs first, fails where a seeded image sharing `common/` has a
`check-layout.c` that asserts a layout at its base's addresses, as it
began, rather than at its own,
derived from the image's code (the address an instruction aligned with
the Z3805A's names) and its export (a global's address), so that a
field the image's code reaches elsewhere fails `check-types`; and,
for every image sharing `common/` and every record, the image's own
types too, where a check's last address, its record's start, lies in
RAM inside a global the export types as a record, that global must be
of the check's type or hold one, so that a check left at its base's
addresses, which lie in another of the image's globals, fails
(the Z3815A 4010's `gps_statistics` passed so, asserted at the
58503A's addresses, inside another of its globals); and a check's record
may not lie in a label the names leave untyped, unless
`layout_addresses.py`'s `UNTYPED_HOLDERS` names it with why (none now;
the Z3815A's `pll` until it was typed), since a check there is as
unseen (the Z3815A's status snapshots, until typed, held three of the
base's records' checks).  Rule 1
masks addresses and so cannot see an offset an absolute address holds:
the 58503A's `gps_receive` counted frame and checksum errors at the
Z3805A's offsets while its `check-layout.c` asserted the Z3805A's
addresses; nor a constant in ROM's range, which it masks as one: the
installer's `:DIAGnostic:ERASe?` reads the flash from `0x10000` in the
three and from `0x20000` in the Z3815A 4010, the same instructions, so it
went back to each image's own `installer_scpi_diagnostic.c`.  Nor can it see a call to another routine, a callee's address
masked like any other: the Z3815A 4010's `console_write_polled` polls
the SCI where the others' poll channel B, the same instructions, so it
and four more like it went back to each image's own C
([`z3815a-4010.md`](z3815a-4010.md), "The export and the checks"); the
function check found them.  `check-common` now checks the callees too:
for each function `common/` defines, the routines each sharing image's
code refers to -- its calls and jumps, a fall through to the next
function, an address it takes, the tail of another function it branches
into, a thunk and what it goes to -- must be the routines its C names,
as clang parses it: its calls, the routines of a table it refers to, the
routines its bindings' and asm statements call, through every static
function it reaches, and a routine whose C is the body of a static
function it reaches (`status.h`'s, `scpi_engine.h`'s).  Each way: a
routine the code calls and the C does not name fails, and one the C
names and the code does not call.  At the commit before the five went
back, it fails on all five, and on `installer_system_init`, whose
`installer_parameter_record_read` the SCI wave's seeding then moved.
Where the image and the C differ for a reason, `common_check.py`'s
`CALLEE_EXCEPTIONS` names the function, the routine and the reason, by
kind: the code repeats in place a routine the C calls (`labs`, `abs`'s
code; the `pps` stubs' kernel calls); calls a routine for what the C
does with an operator (`format_duration_short`'s `ldiv`); takes a
routine's address as data (`random_dither`, `start_nothing`'s first
byte); calls the soft-float routines by other entries than the C's
bindings (`log`, `pow`, `sqrt`); or a finding not yet fixed, the
Z3815A's ([`z3815a-4010.md`](z3815a-4010.md), "The export and the
checks").  An exception that no longer matches in each of its images
fails, so that none outlives its cause.  The check reads names: two
images' routines of one name are the same to it, and a misplaced name
shows only where the code calls an address no name or another name
holds; nor does it see the routines a table in ROM holds, which the
code reaches through the table's address.

What is shared, counted by `make check-common` from the files:
`common/` holds 871 functions the exports name and 214
static helpers, in 215 files; the rest is each image's own:
684 functions the Z3805A 3543B's export names, 799 the
58503A 3704's and 666 the Z3801A 3543's.  Each image's
hand-written C is the two together, as its link check counts it.

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
- the pointers to literals the function's own code passes the C
  functions its sidecar lists it calling, by routine and argument
  (`record_calls`, `harness.calls_differences`): each one one copy
  passes the other must pass there too, the same address or, one in
  ROM and one among gcc's constants, the same text; another place in
  ROM, however alike its bytes, is not.  So an argument nothing the
  call leaves shows, a log text an entry is not written with, is
  compared.  They are compared however often each is passed, those
  both pass in the order first passed (`harness.calls_order`), so that
  two texts logged the other way round differ, and only where both
  copies pass such a pointer: a copy may pass a text it built where
  the other passes a literal.  gcc's copy is compiled for the check
  with its inlining of small and called-once callees off
  (`NOT_INLINED`), so that each call the C makes is a call there, as
  each the image makes is, where a hook at the callee's entry sees it;
  a helper the C defines static, which the image has no copy of, makes
  its calls for the function that called it, found back through its
  frames (`RebuiltCode.owner`).  A function declared inline, a
  binding's asm statement or one that must run in its caller's frame,
  stays in line;
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
  ident finds every task, but in a creator's odd cases, a function whose
  calls ask for a task by name and create one, found by its sidecar's
  calls, not named, where it finds none, so that the create and start,
  a task's name and entry, are compared (`creators_ident_fails`).
  Traps #1 and #7 run the image's own handlers.
- Device models from the data sheets, ready at once, busy for one to
  five polls, with input waiting, and failing (`harness.DEVICE_CASES`):
  the DUART, SCI, QSPI, a debounced input port, the Z3815A 4010's
  switch word, a word a case, ports E and F, each pin a discrete
  input's level a case or, an output or a bus pin, its latch
  (`harness.PORTS`; MC68331UM, 4.9), the EEPROM's write
  cycle, and the installer's two pairs of AM29F010 flash parts, each a
  command state machine with status polling (`harness.Flash`), or the
  Z3815A 4010's one 16-bit part, which its primary writes too
  (`harness.WordFlash`).
- Cases: eight generated per function (byte buffers, then pointer
  buffers, one fill making an unterminated list and one a terminated
  one), and every case again, poisoned: the bytes of RAM, in zero
  longs outside the kernel model's segments, that one copy alone read
  in it, or either wrote and neither read, set to values that differ by
  address (`POISONED`), so that a copy reading or writing another cell,
  which zeroed RAM gives the same 0, differs, on whatever path the case
  reaches; a case with no such byte is not run again.  A cell both read
  is left, so that two copies alike keep the case's path; a wrong cell
  read where the other copy reads its own alike shows only through its
  value's effect; given cases where generated ones miss what callers pass
  (`GIVEN_CASES`), the devices a case answers as among them
  (`DEVICES`) and the EEPROM's records each reader reads, good and bad,
  where every other case finds it erased (`RECORD_READERS`); preset
  variables and hooked routines per case (`STATES`), each of which,
  and each given case's preset, hook and record, must name what the
  image's C has, or the function fails, but for those listed as meant
  for another image's C (`MEANT_ELSEWHERE`); variadic argument lists (`VARIADIC_CASES`); SCPI sessions
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
D6 of the clock record); a log text at another image's address, a
task created under another's name or at another's entry, a flag
cleared at another's address, each the 58503A's in the Z3815A's
ported C, which agreed until the check compared them
([`z3815a-4010.md`](z3815a-4010.md), the first link run); a wrong `Scc`; `volatile` removed from a
polled wait; a debounce one read short; a wrong flash unlock address;
two SCPI root children swapped; a pForth diagnostic word's arguments
swapped; a wrong RAM write or console line after a known difference
(`log_erase`, `os_process_report`); a copy stopping differently from
the image's (`installer_status_register_setup`); D1 or A0 out of
`qspi_transfer_raw`'s clobbers, seen only on a completed transfer;
and, each agreeing before the check's coverage pass
([`z3815a-4010.md`](z3815a-4010.md), "The function check's gaps"), a
log text passed through a static helper (the Z3815A's and Z3805A's
`fine`), two alarm texts logged the other way round
(`alarm_hardware_update`), a condition's bit, PORTE's and PORTF0's
masks, the antenna's bounds and a parameter record's field read for
another.  (README.md, "Hand-written C"; PLAN.md.)

**Other checks**, in `make check-decompiled`:

- `decompiled_check.py`: the export compiles with m68k gcc, and every
  assembly piece is the image's bytes, assembled alone and in the link.
- `bridge_check.py`: each binding, called from a gcc-compiled function
  that keeps 14 values live across it, against its routine called from
  registers; `qspi_transfer_raw` also on transfers the QSPI's interrupt
  completes, which the generated cases never reach (`given_cases`), each of
  which must run.
- `module_figures.py`: `modules.md`'s figures.

And `make check-link` (`link_check.py`): the image links from the
export's declarations, the hand-written C and the assembly alone, the
decompiled C left out, `#if 0` or not, with every function of the
export's sidecar required (`ld --require-defined`; a thunk's `.set`
target for the thunk), so that a function neither written by hand nor
assembly fails the link, named, whether anything calls it or not.  Each
piece lies at its address, as in `rebuild_link.py`'s link, since its
short branches reach only there; no byte is compared.  Its summary line
is the one source of a statement here that an image is complete.
For an image that does not link yet, `make report-link IMAGE=...`
(`link_report.py`) lists what the link finds missing by module, the
cheapest first: each function's size and its counterpart in the images
its diffs are against, with the file of that one's hand-written C, a
function identical or relocated there, carried, being a port of that C;
and whether `common/` would fit it, identical or relocated in every
image that shares `common/` (rule 1).  Its first line counts the
classes, the functions still named `FUN_` and those `common/` would fit.

And `make check-scpi-tree` (`scpi_tree_check.py`): each record of the
generated trees, as gcc lays it out, is the image's bytes, pointers
compared by target.

And `make check-rom-pointers` (`rom_pointers.py`): the ROM pointers of
the hand-written C, which C ported from another image's can carry from
that image, its address or its text, where the function check compares
a pointer by its target or masked and sees nothing amiss.  Each is
checked by what the image holds where it points: a function, code, a
label, a string's start, inside a string or a label, or other, a label's
extent its type's size as m68k gcc lays it out.  A literal cast to a
pointer into the ROM must point at a string's or a label's start, and
at the text a comment after it gives (`log.c`'s formats).  Where the
image's diff against a base pairs a function both write by hand
identical or relocated, each absolute ROM operand the two copies differ
in must point at the same kind of object, text at the same text, both
at its start or both inside it, or at the same bytes; one that does not
passes only where the image's C gives the image's object: its text as
a literal, a constant of that text or address (`records.h`'s
`RECORDS_TAG`), or the name of the label holding it, which the image's
link places.  A finding that stays, with its reason, is a row of the
image's `rom-pointer-allow.tsv`, kept without line numbers, as
`magic-allow.tsv`'s; a row no finding matches fails too.  The tool's
`--self-test`, which the target runs first, checks the classifier and
those rules on synthetic input, and `--all` prints every pointer it
checks.

**Figures** (`make report-decompiled`, `make check-decompiled`, `make
check-link`, `make check-scpi-tree`):

| Check | Result |
| ----- | ------ |
| assembly (`check-decompiled`) | 474 routines in 494 pieces, all identical, alone and linked |
| completeness (`check-link`) | every function defined, "In short" |
| bindings | 73 of the primary's and 18 of the installer's agree, every `register` routine's |
| SCPI trees (`check-scpi-tree`) | 2055 and 38 objects identical |
| function check, primary | 1312 agree, 6 known, 0 skipped, 0 failed |
| function check, installer | 237 agree, 0 known, 0 skipped, 0 failed |
| pForth console sessions | 8 of 8 agree |
| cases, in the agreeing functions | 26,090 of 26,754 compared, the rest skipped; strict, 26,086 of 26,754 |
| stack headroom | no case deeper than the image's by more than 256 bytes, or 16 above a floor; the console's data stack 56 bytes clear, the image's 58 |

A case is skipped where both copies stop at something the harness does
not model, and for the same reason: the same kind of fault (an
unmapped read, write or fetch, at whatever address each copy's code
reaches first), the same trap and call, or no return.  Stopping for
different reasons fails.  A function is skipped when all its cases
are; none is.  `installer_status_register_setup`, whose image copy
wrote unmapped memory through the generated cases' status models, takes
`status_register_setup`'s given cases, in every image.  The
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
    make -C third_party/firmware check-link IMAGE=z3805a-3543b          # about 5 s
    make -C third_party/firmware report-decompiled IMAGE=z3805a-3543b   # about 2.5 min
    make -C third_party/firmware check-scpi-tree IMAGE=z3805a-3543b
    make -C third_party/firmware check-format-c

Each is also a top-level target (`make check-decompiled`).  Without
`IMAGE` each runs for every image it can: `check-link` and
`report-decompiled` for the four reported images, each one's function
check held to the same standard ([`58503a-3704.md`](58503a-3704.md),
"The function check").  Those two run each image as a target of its
own, `report-decompiled/IMAGE`, so that `make -j4 report-decompiled`
runs the four at once; each image's output goes to its
`report-decompiled.log` (or `check-link.log`), printed in the images'
order once all have run, and the target fails if any image did.  The
logs are in each image's work folder, beside its results, which for
the Z3805A
land in `target/firmware/z3805a-3543b/` (`decompiled.o`,
`link-check.txt`, `report-decompiled.txt`).

To export again, from the repository's root, with `GHIDRA` the Ghidra
install and `PROJECT` a new, empty directory outside the repository
(Ghidra refuses a project path with an element starting with `.`),
import the bare image and run every script in order, in one session,
after auto-analysis, the program saved at the end:

    "$GHIDRA"/support/analyzeHeadless "$PROJECT" smartclock \
      -import third_party/firmware/images/z3805a-3543b.bin \
      -loader BinaryLoader -loader-baseAddr 0 -processor 68000:BE:32:CPU32 \
      -scriptPath tools/ghidra \
      -preScript NoSwitch.java \
      -postScript SwitchPass.java \
      -postScript SmartClockSetup.java \
      -postScript SmartClockInstaller.java 0xbbc 0x10135c \
      -postScript SmartClockSetup.java \
        types=third_party/firmware/rewritten/z3805a-3543b/types/smartclock.h \
        types=third_party/firmware/include/mc68331.h \
        types=third_party/firmware/include/mc68681.h \
        names=third_party/firmware/analysis/z3805a-3543b/names.tsv \
        names=third_party/firmware/analysis/z3805a-3543b/scpi-tree.tsv \
        names=third_party/firmware/analysis/z3805a-3543b/installer-scpi-tree.tsv \
        scpi=third_party/firmware/analysis/z3805a-3543b/handlers.txt \
        pforth=docs/firmware/pforth/z3805a-3543b.txt \
      -postScript SmartClockForth.java \
      -postScript SmartClockStackParameters.java \
      -postScript SmartClockFormatStrings.java \
      -postScript SmartClockExportC.java \
        third_party/firmware/decompiled/z3805a-3543b/z3805a-3543b.c \
        defines=third_party/firmware/include/mc68331.h \
        defines=third_party/firmware/include/mc68681.h \
        defines=third_party/firmware/include/psos-signals.h \
        results=third_party/firmware/analysis/z3805a-3543b/results.tsv \
        rewritten=third_party/firmware/rewritten/common \
        rewritten=third_party/firmware/rewritten/z3805a-3543b
    git diff --stat third_party/firmware/decompiled/z3805a-3543b/

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
z3805a-3543b.bin -noanalysis` (and, since it skips auto-analysis, the
`-preScript NoSwitch.java` line).  That export is identical too once the
archive has been saved from a deterministic bare import, and so is
one run over the project a bare import left: each script is run again
over its own output.  The checked-in archive predates the determinism
scripts; regenerate it from a bare import to match.  What an earlier run or analysis left in a
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
vendors'.  `analysis/`, `rewritten/`, `include/` and the Makefile are the
project's analysis; what they reproduce of the image -- the routines
carried as bytes, the tables copied -- remains the image's copyright
holder's.  The decompiled C is Ghidra's machine output, rewritten where
it would not compile or would not do what the image does.  The pSOS
manual consulted (Integrated Systems, *pSOSystem System Calls*,
000-5070-002) is cited, not included.  The harness runs on Unicorn,
GPL-2.0-only, as a local tool.

The 58503A 3704 port has its own document,
[`58503a-3704.md`](58503a-3704.md), held to this one's method and
checks (its "The function check"), every C function of it written by
hand as this one's are (its "The hand-written C"), 1422 of the
primary's and 248 of the installer's, none left as the decompiler's
reading: of its function check, the primary's 1418
agree, 4 are known and none skipped or failed, and the installer's 248
agree, 0 skipped; its console sessions agree, 8 of 8.  The 58503A
3704's link requires all 2151 of its functions and finds each defined:
1670 by hand-written C, 468 by assembly and 13 as another routine's
name.  The Z3815A 4010's, [`z3815a-4010.md`](z3815a-4010.md), seeded
from the 58503A's, is the fourth image to share `common/`, its own code
read in its waves and link runs, until every C function of it was
written by hand, held to the same standard.  The Z3815A 4010's link
requires all 2403 of its functions and finds each defined: 1912 by
hand-written C, 478 by assembly and 13 as another routine's name.  The Z3801A
3543's, [`z3801a-3543.md`](z3801a-3543.md), seeded from the Z3805A's and
the third image to share `common/`, has its C functions written by
hand too, the primary's 1300 and the installer's 237, none left as the
decompiler's reading, the rest the image's assembly, and is held to
the same standard: of its function check, the primary's 1294 agree, 6
are known and 0
skipped, none failed, and the installer's 237 agree, 0 skipped; its
console sessions agree, 8 of 8.  The Z3801A 3543's link requires all
2017 of its functions and finds each defined: 1537 by hand-written C,
468 by assembly and 12 as another routine's name.
