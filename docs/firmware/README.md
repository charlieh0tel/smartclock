# What the firmware shows

What `third_party/firmware/z3816a-4001.bin` shows about how the receiver
measures the 1 PPS time interval, how it disciplines the oscillator
from it, and whether the Oncore's sawtooth correction enters either.
Addresses are in that image, loaded at address zero, except where a
section names another: the Z3801A's (`z3801a-3543.bin`), the Z3805A's
(`z3805a-3543b.bin`) or the 58503A's (`58503a-3633.bin`,
`58503a-3704.bin`).

The processor is a CPU32 part: the reset code at `0x400` sets the
vector base with `movec` and programs the System Integration Module's
registers at `0xfffa00`; the QSM at `0xfffc00` carries the SCI and
QSPI.  It also initializes and uses a register block at `0xfff900`
(`0x22118` onwards; a port whose bit 5 the firmware switches, and a
counter at `0xfff90c`/`0xfff90d` it reads to count 1 PPS edges).  That
is the General-Purpose Timer module, which the 68331 has alongside its
SIM and QSM (NXP's MC68331 page, <https://www.nxp.com/products/MC68331>)
and the 68332 lacks -- it has a TPU there.  The MC68331 User's Manual
(`third_party/motorola/MC68331UM.pdf`) places the SIM at $YFFA00 (Table D-3),
the GPT at $YFF900 with the port GP data register PORTGP at $YFF907
(Table D-2, D.5) and the QSM at $YFFC00 (Table D-13), where this image
finds them; bit 5 of PORTGP is the pin PGP5/OC3/OC1 (section 7.2).  An
owner's description of the Z3801A's outer-oven circuit names the main
CPU as "U33 / MC68331" and the oven's control line as PGP5 (see
`ovens.md`).

The reset code at `0x2466e` sets the SIM up as follows (register
names and encodings from MC68331UM appendix D; block sizes from Table
D-11):

| Register | Value | Meaning |
| -------- | ----- | ------- |
| SYPCR `0xfffa21` | `0xcc` | SWE, SWP, HME, BME: software watchdog on and prescaled by 512, halt and bus monitors on (D.3.12) |
| CSBARBT `0xfffa48` | `0x0006` | boot chip select: `0x000000`, 512 KB -- the ROM this image is |
| CSBAR0, 2, 3 | `0x1003` | `0x100000`, 64 KB -- the RAM: CSOR0 `0x6830` reads both bytes, CSOR2 `0x5030` writes the upper, CSOR3 `0x3030` the lower |
| CSBAR1 | `0x3000` | `0x300000`, 2 KB |
| CSBAR4 | `0xfff8` | `0xfff800`, 2 KB |
| CSBAR5 | `0x3040` | `0x304000`, 2 KB |
| CSBAR6 | `0x0006` | `0x000000`, 512 KB, CSOR6 `0x7070`: write only -- the boot ROM's own range, for writing it |
| CSBAR7 | `0x3020` | `0x302000`, 2 KB -- the word G is chosen by; CSOR7 `0x6870`: both bytes, read only, one wait state, and CSPAR1 `0x2af` makes CS7 a 16-bit port (Tables D-9, D-10, D-12) |
| CSBAR8 | `0x2000` | `0x200000`, 2 KB -- the DUART |
| CSBAR9 | `0x4001` | `0x400000`, 8 KB -- the checksummed block at `0x400080` |
| CSBAR10 | `0x5000` | `0x500000`, 2 KB |
| SYNCR `0xfffa04` | `0xcf80` | W = 1, X = 1, Y = 15: f = f_ref · 4 · 16 · 8 (section 4.3.2): 16.777 MHz from the 32.768 kHz reference the manual's frequency tables assume (4.3.2, appendix A) |
| PORTE0 `0xfffa11` | `0x40` | port E bit 6 high; `0x22e30` later pulses it low and high |

The SCI's baud rate is f / (32 · SCBR), SCBR in SCCR0 at `0xfffc08`
(section 6.4.3.3).  `FUN_0002e610` sets it from a setting byte: 437,
218, 55 and 27 for settings 0 to 3, which at 16.777 MHz are 1200,
2405, 9533 and 19418 baud -- the manual's four rates, each within
0.7 %.  `FUN_0002ed56` sets PE and PT in SCCR1 (`0xfffc0a`) from the
byte at `0x10261d`: no parity for 0, even for 1, odd for 2.

The Z3816A image describes the design family; the 58503A's own code is
in `console.md`, "The 58503A image".

Two images came later: the Z3815A's (`z3815a-4010.bin`), whose GPS
engine is a Furuno GT-74 rather than an Oncore, and the 58503B's
(`58503b-1.01.04.bin`), rebuilt from a console dump of another owner's
unit; their provenance is in `third_party/NOTICE`.  Both are
checksummed as the Z3816A's is, a sum of words, not as the 58503A's two
lanes: each carries the Z3816A's routine, instruction for instruction,
at `0x45e` (`restart.md`, "Boot check").  `interval.md`, `loop.md`,
`ovens.md`, `console.md` and `gps.md` each end with an "In every image"
section setting all seven images side by side, `restart.md`'s tables
cover all seven, their command trees are in `scpi/`, their console
words in `pforth/` and the six Oncore images' message tables and
request scripts in `oncore/`.

## Contents

- [The GPS receiver link](gps.md)
- [Oncore message tables](oncore/README.md)
- [The interval](interval.md)
- [The disciplining loop](loop.md)
- [The ovens](ovens.md)
- [The debug console](console.md)
- [Restarting](restart.md)
- [Pulse, time-tag and output commands](outputs.md)
- [58503A 3633 against 3704](revisions.md)
- [The floating-point library](softfloat.md)
- [The C library](libc.md)
- [The real-time kernel](psos.md)
- [Structures](structures.md)
- [Modules](modules.md)

## Summary

- The reported interval is the mean of ten one-second readings, a
  single-precision float in seconds.  It changes every ten seconds.
- Each reading comes from an FPGA counter: a coarse count of 100 ns
  ticks plus an interpolator, with calibration terms applied.
- The firmware decodes the Oncore's negative sawtooth from the Time
  RAIM message and prints it on a status page.  No path was found from
  it into the interval measurement.
- A proportional-integral loop on the same ten-second mean steers the
  oscillator every ten seconds.  Both gains follow from a time constant
  τ and a gain G: proportional 1/(Gτ), integral 1/(4Gτ²).  τ defaults
  to 500 s in the Z3816A and 1000 s in the Z3801A, from a block of
  defaults in ROM; after power-up the loop starts at 150 s and
  lengthens by 5 s each update until it reaches τ.  Its phase input is
  the interval mean; no read of the sawtooth was found in it.
- A separate task fits a + b·t + c·ln t to 45-minute means of the
  EFC, up to 64 of them (48 hours); the loop adds the fitted slope to
  its integrator each update as predicted drift.  `startup_pll` sets
  its first EFC from the same curve.
- The firmware switches, not regulates, the outer oven of a
  double-oven oscillator: one port bit, set when the oscillator current
  has stopped falling and cleared only from the console.  No loop in
  either image drives a heater.
- The firmware carries a Forth interpreter that calls itself pForth,
  whose words include the loop's diagnostics.  It is a shell over
  compiled code: no word in the image is written in Forth.
- Every image carries the 59551A's pulse, time-tag and output
  commands, and stores or ignores their settings: the routine that
  would program the pulse is empty, and no 58503A image drives a
  second pulse channel, a pulse period or a time-tag input
  (`outputs.md`).

## What is not established

`hardware-investigations.md` lists what a bench would settle of the
following, and how.

- Whether the port's second open matters.
- What PE6 drives, pulsed only on a cold start; what 3704's PWMA pin,
  low while the holdover-recovery flag is set, is wired to (`loop.md`).
- Whether anything reaches the rest of the ROM defaults block by a
  computed address: no reader of it was found (`loop.md`, "τ and G").
- What the letters HQ stand for.  It is the predicted holdover
  uncertainty, in seconds, that `TUNCertainty:PREDicted?` and the
  status screen report (`loop.md`, "The fit").
- The console's `current drift = %.1e / day` (`0x2c3bf`) prints the
  float at `0x102bcc` times 5.4 × 10⁻⁸.  Nothing in the image writes
  `0x102bcc`, and it lies in the RAM the start-up code clears, so the
  word prints zero.
- What a Z380x engine does with `@@Ci` format 0, which the init
  scripts send and `VPCommands.pdf` does not define (`gps.md`,
  "Requests").
- What the Z3801A's `Oven` and `Secondary oven voltage` channels
  measure, beyond the ADC inputs and coefficients above.
- The units of the oscillator current: nominal 250 and limit 650 after
  a scale of 4.489 per ADC count.
- That `0xfff907` bit 5 reaches P2/8 and that the ADC is an ADC0838
  are an owner's report of a Z3801A board, not traced here; how the
  Z3801A firmware's "Oven" and "Secondary oven voltage" values relate
  to the volts at P2/9 was not worked out.
- Which CPU32 part this is: the register map matches the MC68331
  manual's and the `0xfff900` block rules out the 68332, but the part
  name comes from an owner's description of the board, not the image.
- What drives bit 8 of the read-only port at `0x302000`, which
  chooses between the Z3816A's two values of G.  The image reads it
  once and examines nothing else in that block.
- That the SCI is the port wired to J3: the firmware's SCPI port is the
  one it creates `sciR` and `sciW` for, but the board was not traced.
- What drives the 59551A's PORT 2.  DUART channel B, idle here, is the
  device a single-port unit would leave spare, but no image of a
  59551A's firmware is at hand.
## Checked as far as it goes

Not pursued further, each with the limit of what was checked.

- The sawtooth is displayed and not applied in every image: the only
  reader of the field is the Time RAIM page (`gps.md`, "Where the
  sawtooth goes").  A sweep cannot see a reader that steps a pointer
  through the record or copies it whole.  Recording the 1 PPS
  interval against the displayed sawtooth would settle it.
- The restarts after `0 crash` and `4 crash` on the bench Z3801A were
  warm, so neither an external nor a power-up reset, and the fault
  path they reach spins with interrupts masked, which starves the
  watchdog (`restart.md`, "The watchdog").  Which internal reset it
  was is in RSR, which the reset code stores nowhere and the pForth
  debug console, in user mode, reads as zero; reading it takes
  supervisor access, such as the CPU's background debug port on an
  open board.

## Note on the disassembly

A node of the SCPI tree is a record whose long at +0 points at its
keyword -- stored as the short form, a NUL, and the rest of the long
form -- whose long at +4 points at its child list in `0x59000` to
`0x5b000`, and whose handler is the long at +18 (common commands such
as `*IDN?` keep theirs there too).  A node's parent is the node whose
child list holds it, which is how the paths above were resolved.

Ghidra resolves the table address of this compiler's switch idiom,
`move.w (d8,PC,Dn*2),Dn` followed by `jmp (d8,PC,Dn)`, twelve bytes
late.  The table starts at the `jmp`'s base, and offsets are taken
from there.  Case bodies reached only through such a table stay
undisassembled until disassembly is forced at the resolved targets.

## The calling convention

Read from the Z3805A 3543B image's 5125 calls, not from a document: no
compiler banner is in the image, and the compiler is not known.

**Observed.**

- Arguments are pushed right to left and the caller pops them, often
  two calls' worth at once (`lea (n,SP),SP`).  No `rtd` appears.
- A long, pointer or float argument takes 4 bytes and a double 8;
  a `char` is pushed with `move.b` (926 times) and a `short` with
  `move.w` (337), each in a 2-byte slot, and the argument after it
  starts 2 bytes on.  The 68000 keeps SP even, so `move.b -(SP)`
  drops it by 2 and stores the byte at the new, even address: the
  value is in the slot's first byte, the high byte of the word, and
  the callee reads it there (`tst.b (0xc,A6)`).
- Calls to the variadic functions (`printf`, `sprintf`,
  `debug_printf`, ...) push no byte or word at all: 342 longs and 384
  addresses.  A float passed to one is first converted to double.
- Results come back in D0, a double in D0:D1, and a `char` result is
  used straight from D0's low byte.  A float or double result is in
  D0 or D0:D1 too: there is no FPU.
- Functions save D2--D7 and A2--A4 when they use them, and keep A6 as
  the frame pointer (`link A6`); D0, D1, A0 and A1 are scratch.

**Against the documented conventions.**  The System V m68k ABI, which
GCC and LLVM follow and Ghidra's 68000 convention models, promotes
every `char` and `short` argument to a 32-bit `int` in a 4-byte slot
([M68k LLVM ABI notes](https://m680x0.github.io/doc/abi.html), [LLVM
M68kCallingConv.td](https://cgit.freebsd.org/src/tree/contrib/llvm-project/llvm/lib/Target/M68k/M68kCallingConv.td),
[GNUPro m68k calling
conventions](https://users.informatik.haw-hamburg.de/~krabat/FH-Labor/gnupro/7_GNUPro_Embedded_Development/embCalling_conventions_for_m68k.html)).
GCC's `-mshort` aligns stack arguments to 2 bytes but makes `int` 16
bits ([GCC M680x0
options](https://gcc.gnu.org/onlinedocs/gcc-4.1.2/gcc/M680x0-Options.html)).
The classic Mac 68K convention also uses 2-byte slots, but puts a
single byte in the slot's low half, the pad byte most significant
([Apple, Classic 68K Stack Structure and Calling
Conventions](https://dev.os9.ca/techpubs/mac/runtimehtml/RTArch-132.html)).
None matches.

**What it must be.**  The rest is inference from the above.  `int` is
32 bits: the variadic calls promote a `char` to a long, which is the
default argument promotion to `int`, and a float to double.  A call
with a prototype in scope does not promote: ANSI C converts a
prototyped argument as if by assignment to the parameter's type and
does not require widening it, and this compiler passes the parameter's
own size, rounded up to the 68000's 2-byte stack granule.  So the
convention is "ANSI with narrow prototyped arguments": the function's
declared types decide each slot -- 2 bytes for `char` and `short`, the
value at the slot's start; 4 for `int`, `long`, pointers and `float`;
8 for `double` -- and an unprototyped or variadic call falls back to
the default promotions.  A `char` result is not extended, so the
caller extends it if it needs to.  How a structure is passed or
returned is not established.

Ghidra cannot model this convention: its 68000 language justifies a
small value to the end of its slot, a property of the language, not of
a convention a program can add.  Hence `SmartClockStackParameters.java`,
below, which pins each argument's bytes in custom storage.  A pointer's
prototype cannot carry custom storage, so a call through one -- the
self-test tables at `0x18458` and `0x17fe0`, the measurements
`dac_search_bisect` and `dac_search_step` are given -- passes its `char`
as `char_slot` (`tools/ghidra/types/smartclock.h`), a 4-byte structure
whose first byte, its value, lies where this compiler puts a `char`'s,
at the slot's start, for Ghidra and gcc alike; gcc pads a 2-byte one to
the slot's end.  The routines called that way take it too, and a `call`
row gives each such call its callee's prototype.

## Decompiling with Ghidra

Six GhidraScripts in `tools/ghidra/` take an image imported raw at
address zero as `68000:BE:32:CPU32` to C, under `analyzeHeadless
-postScript` or ghidra-mcp's `run_ghidra_script`.  Each is meant to be
run again over its own earlier output; their dry-run modes cover only
part of what they change.  In order:

1. `SmartClockSetup.java`.
2. `SmartClockInstaller.java RECORDS ENTRY`: `0xbbc 0x10135c` for the
   Z3801A 3543 and Z3805A 3543B (`restart.md`, "Forced installer entry
   with an unusable primary").
3. `SmartClockSetup.java` again, now over the installer too, with
   `types=tools/ghidra/types/smartclock.h`,
   `types=tools/ghidra/types/mc68331.h`,
   `types=tools/ghidra/types/mc68681.h`,
   `names=tools/ghidra/names/IMAGE.tsv`, `scpi=FILE` from
   `smartclock-cli dump-scpi --handlers IMAGE` and
   `pforth=docs/firmware/pforth/IMAGE.txt`.
4. `SmartClockForth.java`.
5. `SmartClockStackParameters.java`.
6. `SmartClockFormatStrings.java`.
7. `SmartClockExportC.java FILE defines=tools/ghidra/types/mc68331.h
   defines=tools/ghidra/types/mc68681.h
   defines=tools/ghidra/types/psos-signals.h
   results=tools/ghidra/names/results.tsv rewritten=third_party/firmware/rewritten/IMAGE`.

**Setup** makes the ROM read-only, so values loaded from it fold to
constants; maps RAM, the DUART, EEPROM, the CS4 and CS5 ranges and the
GPT, SIM and QSM, the I/O ones volatile; types both
vector tables, the boot one at `0x0` and the primary's at `0x10000`,
and makes a function at each handler; and types the string tables,
removing the code analysis decoded in them (a 0x61, `a`, is a `bsr`, so
code begun in a string "calls" on through the table), but not text inside
a table the names file types as an array or structure (the installer's
records at `0xbbc`).

It recovers the switches.  The bound check before a table is a `cmpi`,
`cmpa`, `subq` or `subi` and a `bhi`.  After `subq #k` or `subi #k`
the index runs from -k to 0, so the `move.w` displacement points 2k
bytes into the table: the "twelve bytes late" above.  Analysis types
its late copy of the table over the case bodies and decodes code from
the wrong cases.  Starting from the real cases and following their
flow, the script clears what overlaps each real instruction and
disassembles it, rebuilds the owning function's body and records the
targets as a decompiler jump-table override, and keeps each table's
address, first index and length for the exporter, which labels the
cases from them (below).  It labels each table `switchD_JMP` and, in
the primary image, the entry for index 0 that a late table load points
at `switchD_JMP_index_zero`, which the decompiler reads the case value
from.  It removes the case
labels analysis put at its late reading's targets; one that had become
a function's name gives the function back its default name rather than
deleting it, since deleting a function's name deletes the function.

It recovers the pSOS kernel's service dispatch, in the primary and in
the installer: a `jsr (d8,PC,D7.w)` into a table of `bra.w`, one a
service, bounded by the `cmpi.w` before it (`psos.md`).  Analysis
follows none of them, so without this the kernel's 39 handlers are
never disassembled.  It disassembles the table, records the `jsr` as
calling each handler and makes a function of each.

It ends flow at the two instructions that do not come back, by pointing
each one's fall-through at itself, which the C shows as a loop with a
comment: `stop`, which ends failure paths, and `trap #5`, which ends
every interrupt handler in the primary and the installer in place of
`rte` -- the pSOS kernel's return from interrupt, read from those
handlers rather than documented.  It clears word and long data at odd
addresses, which the 68000 cannot read, and resumes flow such data or
stray code blocked, clearing only what is in no function.  It removes
unnamed functions made in data: one at an odd address, or one whose
own code reaches an odd address or bytes that do not decode, that no
function calls.  It does this in one pass, since removing one function
leaves its callees looking stray; `dry-run` reports them and removes
nothing, and more than 20 is refused as a rule gone wrong.

Names come from what the project has established.  `names/IMAGE.tsv`
holds those the notes give for that image, each row citing its note:
the float and C libraries (`softfloat.md`, `libc.md`), the kernel's
service calls (`psos.md`), the interval's routine and cells
(`interval.md`), the SCPI task and the installer's entry (`restart.md`),
the console's hook (`console.md`).  An address a note gives only for
another image is not carried over.  An address in the installer's
overlay is written `installer::0x...`.  A row's fields are the address,
`function` or `label`, the name and its source -- the note it cites,
with `run` or `read` for how a routine was confirmed, or `context` for a
name read from its callers -- and then, each optional, a note saying
what it is or does, a C function's prototype or a label's type, and a
function's calling convention.  A `local` row instead types a function's
stack variable, which the decompiler would otherwise split or leave
untyped: its name, and `TYPE @ OFFSET` in the prototype field, Ghidra's
offset from the stack pointer at entry (`sprintf_output`,
`structures.md`).  A `call` row gives the call through a pointer at its
address the prototype of what it reaches, which the decompiler takes as
a call to code with no arguments when it loads the pointer from a
table: the self-tests' `char_slot run`.  It also gives a variadic call
its arguments' types, `print`'s or `sprintf`'s: the decompiler sizes
a variadic argument by the stack location it is pushed to, so where a
`move.b` or `move.w` push for another call writes that location, it
passes an address's low byte or word (`log_print`, `status_screen`,
`status_screen_position`).  Every SCPI handler takes what `scpi_call_handler`
gives it, `int NAME(void *parameters, void *response, void *call)`,
unless its row gives a prototype.

The convention is `key=value` pairs, which the names file's header sets
out: its kind (`c`, `register`, `exception`, `pforth`, `psos-service`,
`other` or `undetermined`), a confidence (`run`, `read`, `documented`,
`derived` or `assumed`, strongest first, with `KEY.confidence` for one
key weaker than the rest), the registers `in` and `out` with their
types and names, an `in` item without a register a stack argument
(`console_task_create`'s, which a C caller pushes), the registers a
result is also left in, `copies=A0:A1`
for each double routine but the in-place ones (`softfloat.md`; every
exit of their shared bodies ends `movea.l D0,A0`, `movea.l D1,A1`,
`rts`, at `0x5d47e`, `0x5d60c`, `0x5db90` and `0x5e7da`), and
`copies=D0:D1` for `float_raise` and `double_truncate_sixteenths`,
whose result in D0:D1 the decompiler does not take at `pow`'s calls
through a register, `returns=ccr` for the condition codes, the `error`
values, the registers it `preserves` and its `family`.  A function
without one is unknown.  At `run`, `read` or `documented`, the
function's prototype is built from `in` and `out`, so its C shows its
real arguments; below that, it gets only the comment and the tag, so
the C never shows guessed ones.  A prototype field takes no `@`: the
registers go in the convention.

A function's note, its convention's line and its source make the
comment the decompiler prints above its C, `context` left out.  Every
function that is not C opens that line "Not a C function", with its
kind and confidence, and carries the Ghidra tag `not-c`.
`SmartClockForth.java` gives each routine of the pForth kernel that uses
its registers the convention `pforth`, and `SmartClockSetup.java` each
exception handler `exception`, naming the vectors that enter it and how
it returns, unless the names file gives one.  `SmartClockExportC.java`
writes them all beside the C, in
`third_party/firmware/decompiled/z3805a-3543b.conventions.jsonl`: one
JSON object a line for every function, its address, name, note and
convention, with its lists split into register, type and name, or
`"convention":"unknown"`, and, for a function whose arguments are all on
the stack, `"stack"`: each one's offset from the stack pointer at entry
and its size, from the decompiler's prototype.  The original packs a
`char` or `short` in 2 bytes, or several into one long (`format_date`
takes its date so), where gcc gives each 4; the emulator harness lays
the image's arguments from it.

A function that is not C is not written as C.  Its decompiled C stays
in the file for reading, under `#if 0`, its signature led by a macro
for its kind (`__pforth`, `__regcall`, `__exception` or `__naked`), each
parameter's register in a comment, and a comment line before it with
what C cannot say: its results, condition codes, errors and preserved
registers.  After it comes the routine itself as a top-level
`__asm__`, translated from Ghidra's operands into gas's m68k syntax: a
local label at each address in it an operand names, the routine or
global at each address outside, and for any other address a symbol
`__ghidra_ADDRESS` that `z3805a-3543b.symbols.ld` places, since gas
gives a PC-relative operand its short form only against a symbol it
does not know.  A short branch to another routine is its opcode byte
and an explicit `R_68K_PC8` relocation, a few instructions gas cannot
spell are their bytes, and a routine whose code lies about another's
is in pieces, each a section `.text.NAME.ADDRESS`.  Each piece carries
the bytes after it up to the next routine, at most 256, which are its
tables.  C calls these routines through `int NAME();`.
`tools/firmware/decompiled_check.py` (`make check-decompiled`)
assembles them alone and compares every piece with the image, every
relocated field resolved at the image's own addresses: all 400
routines, in 418 pieces, are the image's bytes.  `jq 'select(.convention
!= "unknown" and .confidence != "run")'` over the sidecar lists what is
left to confirm.

SCPI handlers are named `scpi_query_PATH` and `scpi_set_PATH`, console
words `forth_WORD` and `diag_WORD`, and each has a note saying so -- the
query handler for its path, the setter, the pForth word or diagnostic
word -- unless the names file gives it one.  A name replaces one Ghidra made
up, and one from the names file also one these tables gave, but none
replaces a name given by hand.  A table vouches that its entries are code, so the
script disassembles each, and splits a function whose body ran into
it.

A row may give a C prototype, which types the function's arguments at
every call.  The float library passes its operands in registers, which
C cannot say, so a prototype may put a value in registers with `@`:
`float float_add(float a @ D0, float b @ D1) @ D0`, a double in a pair,
high register first, `@ D0:D1`.  An argument without `@` goes on the
stack.  Ghidra's 68000 convention returns a float or double in the FPU
register FP0, but these images have no FPU and return one in D0 or
D0:D1, so every function returning one has its return annotated.

`types/psos-signals.h` names, as `#define`s, the signals the drivers
send their tasks, bits of the events `signal_v` ORs into a task's
pending word, by the firmware's own messages: `"wait_v SIG_SIQ error in
getc"` beside `psos_wait_v(0x4000,...)`.  A bit is a task's -- 0x800 is
`SIGINTR` to the console's and `SIG_SOQA` to the GPS task, 0x200 the
SCI's output queue and the interpolator's reading -- so each name lists
the functions of the driver its message comes from, and the exporter
names a constant signal a call to `signal_v`, `wait_v` or `get_v`
passes only there: `psos_wait_v(SIG_SIQ,0,0,(ulong *)0x0)`.  1, 0x20
and 0x100, which no message names, stay numbers.

`types/mc68331.h` lays out the CPU's own modules as the names file
types them: `gpt` (`0xfff900`), `sim` (`0xfffa00`) and `qsm`
(`0xfffc00`), each register a field at its offset in MC68331UM Tables
D-2, D-3 and D-13; and it names their bits, `SCCR1_TIE`, from the
registers' pages in Appendix D, as `#define`s, which Setup leaves out of
what Ghidra parses and the exporter copies into the C (`defines=`).
The exporter then names the constants written to or tested in a
register by those bits, where every bit has a name, so that the C
reads `qsm.sccr1 = uVar1 | SCCR1_TIE` and `uVar1 & ~SCCR1_PE`.  A pair
of byte registers the firmware only ever moves as a word is one field,
named for both, with bits named across the word: `gpt.tmsk` for TMSK1
and TMSK2, so that `gpt.tmsk = uVar2 | TMSK_OCI3`, and `gpt.tflg`.  The
ports' bits, the board's wiring, stay numbers.  A register the
decompiler reads into a local first, as it does a volatile one, names
the local's tests too, `uVar1 = qsm.scsr;` then `(uVar1 & SCSR_TDRE)
== 0`, where every copy into the local is of that one register or the
test is the statement after the copy: `duarta_isr` reads `sr_csr` and
`rb_tb` into one local.  `types/mc68681.h` does
the same for the DUART, `duart` (`0x200000`), from Motorola's MC68681
data sheet (Tables 4-1 and 4-5): a channel's registers are a
`duart_channel`, `duart.a` and `duart.b`, and a register read and the
one written at its address are one field, named read then write,
`sr_csr`, whose constant a store names by the written register's bits
and a test by the read one's.  A register whose codes are not single
bits, the command register's, is named only by a code it equals, so
that `duart.b.cr = CR_RESET_RECEIVER`, and a code is never split into
bits that do not mean it.  The code's shadow of the DUART's write-only
registers, `duart_shadow`, is named by the same bits:
`duart_shadow.imr & ~IMR_TXRDYB`.

`types/smartclock.h` declares the structures (`structures.md`), parsed
into the program before the names so a row may use them: a label's
row may give its data type, `gps_state` or `uint8_t[6]`.  Parsing adds
and replaces types but removes none, and they sit beside Ghidra's own,
so a type renamed or dropped from the header stays in the project, and
in the exported C, until it is deleted there by hand.  The header and
the firmware's own prototypes use `<stdint.h>`'s types, the header
defining them, since Ghidra's C parser reads no includes; a published
API keeps its own spelling, the C library's and pSOS's.  The script
then makes `uint8_t` and `int8_t` Ghidra's `byte` and `sbyte`: the
decompiler prints a char type's constants as characters, so
`fpga_set_bit('\x01','\b')`, and these two's as numbers,
`fpga_set_bit(1,8)`; the exporter prints any other that is no text as a number too.  The header's coded values are enums, declared
`__attribute__((packed))` so gcc makes each the smallest integer that
holds its values; Ghidra's parser makes every enum 4 bytes, so the
script resizes each to 1, 2 or 4 bytes the same way, and the packed
structures lay out again around it.  A field or argument with an
enum's type prints its values by name: `pll.stage ==
PLL_STAGE_HOLDOVER`, `adc_average(ADC_5V)`.

**Installer** unpacks the records the boot code copies to RAM, after
its vector table, into an overlay named `installer`, writable so that
cells the installer changes do not fold, and makes a function at its
entry and at each vector's handler, and clears the code analysis found
in the packed records themselves, which never run where they lie, so
that the overlay is the installer's only copy.  Boot code jumps into plain RAM,
which the overlay does not serve; the jump is marked a call, so it
reads `func_0x0010135c` rather than flowing into RAM with no code.  The
installer's functions are not labeled at their plain addresses: the
primary keeps its own data there.

**Forth** gives the pForth console's kernel its register convention
(`console.md`, "The kernel's registers"): each function in the span of
the `forth_` words that uses A6, A5, A4 or A3, and has no frame,
takes those registers as parameters -- `sp`, `user`, `dp`, `input` --
with `user` a pointer to the end of a `forth_user_area` structure, so
`*(A5 - 0xe)` reads as `ADJ(user)->base`.

Setup last gives every function that is C Ghidra's default convention,
the stack one the image's compiler used, and leaves one tagged `not-c`
"unknown", as Forth does the kernel's: Ghidra has no model of theirs.
A prototype applied from a signature, or laid out in custom storage,
names none, and the decompiler, which takes the default for it anyway,
heads its C with "WARNING: Unknown calling convention".

**StackParameters** gives functions the stack parameters their code
uses, where Ghidra's default convention misplaces them (see "The
calling convention", above), where the decompiler rejects a function
("Cannot properly adjust input varnodes"), or where its signature is
still the analyzer's, with none, though it takes arguments: each
caller's decompilation would then guess its own from what it pushes
(`(short, char, char)` for `robin_parse`, which takes three pointers),
and the C would call it unlike its definition.  The layout comes from the
callers where their pushes agree and add up to what they pop -- a
`move.b` or `clr.b` push a 2-byte slot holding a byte at its start --
and otherwise from the stack references on the function's own
instructions, sized by each instruction; a lone byte or word read at
the end of a 4-byte slot is taken as part of a 4-byte `int`.  The
parameters go into custom storage at those offsets and sizes, which
pins each value's bytes as no calling convention here can, and the
decompiler's types for them are committed.  The decompiler's own list
of a function's stack inputs is not used: it drops an argument that is
only copied into a local buffer whose address escapes, as the `pllc`
requests' are.  A signature from the names file or given by hand is
kept, but checked: one whose parameters do not match its callers'
pushes is reported (a double may span two pushed longs; pushes past
the last parameter, a variadic call's or ones the function ignores,
are not compared).  "Unchanged (set earlier)" counts the functions an
earlier run gave parameters that still match.

**FormatStrings** runs Ghidra's own "Variadic Function Signature
Override" analyzer, which reads the format string at each call to a
function whose name contains `printf` and that takes a `char *` and
varargs -- `printf`, `sprintf`, `debug_printf`, `debug_sprintf`,
`printf_polled` -- and overrides the call's signature with the types
the format gives.  Without it a variadic argument has no type: an
address passed to `%s` prints as a number, `printf("Copyright Notice:
%s", 0x101059)` rather than `gps.receiver_id.copyright`, and a double
as its two halves.

**ExportC** writes every function's C, in address order, after the
program's structures (packed, as the image lays them out, enums too,
each type's size asserted), the decompiler's own vocabulary (its
`CONCAT`, `SUB`, `ZEXT` and `SEXT` operations, `code`, `undefined3`),
an `extern` and a `.set` to its address for every global the C names,
`volatile` in the peripherals' blocks, and a prototype for every
function, so that the file stands alone for `m68k-linux-gnu-gcc
-std=gnu17 -mcpu=cpu32 -fno-toplevel-reorder`: top-level order matters,
since a routine can fall into the next.  A global with a structure's
type reads as `foo.bar`, a pointer to one as `foo->bar`; a piece of a
variable the decompiler writes as `x._4_4_` becomes a cast at its
offset, read signed where its p-code reads it signed -- a signed
compare, extension, shift or division, so that `datetime_subtract`'s
`tst.b`, `bmi` reads `__ghidra_piece(signed char, local_8, 2) < '\0'`
-- or under a signed cast, `(int)x._2_2_`, which is how the decompiler
writes a sign extension, and unsigned elsewhere.  A thunk is a comment, since the decompiler gives it its
target's name, and one named otherwise an alias of its target.  A
routine that is not C and whose prototype has every argument and its
result in registers is declared under that prototype as an assembler
label, `c_NAME`: a generated wrapper that moves the arguments from
gcc's stack into the routine's registers and its result into D0, D0:D1
or A0.  `tools/firmware/bridge_check.py` runs each wrapper in Unicorn
against its routine called directly, from the registers the sidecar
names, the installer's on the installer's machine (below): of the 91,
87 give the same result, RAM and effects and keep gcc's callee-saved
registers, 83 the primary's and 4 the installer's, and 4 are not run:
two with a register the sidecar does not name, and
`kernel_dispatch_io`'s and `installer_kernel_dispatch_io`'s, exception
handlers that `trap #4` enters with a frame for its `rte`, which a
wrapper's `jsr` cannot give.  The
whole file compiles to an object (`make check-decompiled`, whose flags
add `-fno-builtin -fno-tree-loop-distribute-patterns`, since the image's
C library has no `puts`, `strlen` or `strcmp` for gcc to call, and
`-fwrapv`: the decompiler's C counts on signed arithmetic wrapping, as
the 68k's does, where C leaves overflow undefined and gcc may make a
loop that doubles a count, `sqrt`'s and `log`'s on a subnormal, one
that never ends).  The
decompiler's views of the stack at entry, `stack0xN`, are macros N + 4
above gcc's frame pointer, which is how the printf family reaches its
variable arguments; a view into a routine's own frame, or of a
register, has no C to stand for, and is an extern at `0xfffff000`, which
the harness leaves unmapped, so that a use faults there.

The decompiler moves bits where C converts values.  These images have
no FPU, so it never sees a conversion between a floating type and an
integer -- the float library makes those by calls -- and every cast it
writes between them, `(double)CONCAT44(...)`, `(ulonglong)x & mask`,
means the same bits as the other type.  The exporter writes each as
`__ghidra_as(type, v)`, an integer's bits as a float or double, or
`(type)__ghidra_bits(v)`, a floating value's bits as an integer.  A
cast to a structure, which C has none of, is `__ghidra_as` too: the
decompiler builds a `char_slot` argument from the bytes pushed.  It
then asks gcc (`-Wfloat-conversion -Wconversion`, as JSON) for the
implicit conversions that remain, a double assigned to an `undefined8`
or an integer passed as a float, and wraps each the same way, leaving a
literal to convert; it reports any it could not, and without gcc leaves
them.  A float or double result is in D0 or D0:D1 whatever its
prototype's source, never FP0.  A shift by a count that is not a
literal is `__ghidra_shl(x, n)` or `__ghidra_shr(x, n)`: the
decompiler's shift, like the 68k's by a register (which it writes
`x << (n & 0x3f)`, modulo 64), gives 0 for a count as wide as `x` or
wider, or every bit the sign shifting a negative value right, where C
leaves it undefined and gcc may test the bit modulo 32.

The exporter prints each function from the decompiler's tokens, as the
decompiler does, and adds there what the p-code says and the text does
not; a function whose tokens do not print its text is left as the
decompiler wrote it, and reported.

- An unsigned compare whose other operand is arithmetic printed inline
  gets an unsigned constant, `param_1 - 1 < 3U`: the decompiler types
  the expression by the compare, but C types arithmetic on a narrower
  value int, so that, of a byte, `param_1 - 1 < 3` is true for 0, which
  the image's `subq`, `bhi` (`status_screen_tfom`,
  `status_screen_position`) excludes.
- A call to a routine whose convention has `copies` assigns the
  variables the C reads those registers from after it, the
  decompiler's `extraout_A0` and `extraout_A1`, which are otherwise
  uninitialized: `({ __auto_type __c = call; __ghidra_also(extraout_A0,
  __c, 4); __c; })` is the call's result and gives the variable its
  four bytes 4 up from the least significant end.  The call is written
  out, not a macro's argument, since gcc places a diagnostic inside a
  macro's argument at the macro, and the conversion pass would wrap the
  whole call.  `log` and `pow` pass a double routine's result to the
  next that way.  Where the C reads the pair back only as a `CONCAT44` of the two, the
  call's result is kept instead in a variable of its own, `result_n =
  call(...)`, and the `CONCAT` reads it; so too a double the decompiler
  reads from D0:D1 after a call whose prototype it took to return
  elsewhere (`sqrt` through `jsr (A4)`).
- A call to a routine whose convention has `returns=ccr` -- the pSOS
  bindings, `clock_record_sum` and the SCPI engine's number routines --
  gives the flags the C reads after it the routine's.  The decompiler
  takes the call to keep the flags, an `INDIRECT` at the call, so the C
  tested a compare made before it (`bVar6 = param_4 == 0;` then the
  call, then `if (bVar6)`); the bridge keeps the condition codes in
  `__ghidra_ccr` right after its `jsr`, and each flag variable is
  assigned its bit after the call, `bVar6 = __ghidra_flag(4)` for Z.  A
  scan of the image for a call followed by a conditional branch found
  the routines that return flags; those called only from assembly
  (the kernel's checks, the Forth words, the boot code) need none.
- A call to a routine whose convention lists `results` -- the pSOS
  bindings, `psos_call` and `psos_io_call`, whose services return in D0
  to D3, A0 and A1 past the error in D7 -- gives the registers the C
  reads after it the routine's.  The decompiler takes each as a register
  the call happens to change, `extraout_A0`, which nothing set
  (`psos_ident`'s id, `psos_my_pid`); the bridge keeps each listed
  register in `__ghidra_result_d0` and the like right after its `jsr`,
  and each `extraout_` variable the C reads is assigned from it after
  the call, as is an argument in such a register -- the callee's
  parameter's register -- whose variable the C reads again after the
  call, which the decompiler takes the call to keep (`psos_io_read`'s
  length).  A call whose convention has `copies` assigns any variable
  the C reads a copied register from, a byte piece of it from its own
  bytes (`format_duration`'s `ldiv`), and one to a routine that
  `preserves` a register assigns an `extraout_` variable for it the
  value before the call, where the C names that value.  Where the
  decompiler made the register's value up, having tracked none, it is
  what the instruction that last wrote the register gave it, a stack
  variable or an immediate, or a `dbf`'s counter less one in its low
  word, found back along every path, through fall-throughs and
  branches, where all of them reach the same value (`modf`'s A0, the
  `iptr` it was loaded from, which two branches reach;
  `pforth_define_word`'s loop count around `forth_compile_code`).  A
  register a function reads at entry whose declaration names it a
  pointer is read the same way, and A6, the caller's frame pointer,
  from where gcc's `link` saved it (`setjmp`, `installer_float_raise`).
- A rotate of a long by a register, which the decompiler writes as two
  shifts, `x << (n & 0x3f) | x >> (0x20 - (n & 0x3f))`, is
  `__ghidra_rotl(x, n)`: the 68k's `rol` rotates by n modulo 32, where
  the shifts give 0 for a count from 32 to 63 (`rotate_left`).
- A negative constant an equality test compares with a narrower
  unsigned operand is that operand's value, `!= 0xff`: the decompiler
  prints the byte as -1, but C promotes the operand to int
  (`nv_slot_erase`, on an erased EEPROM).
- The address of a view of the routine's own frame that ends a local
  is the local's end, `(void *)((char *)&record + sizeof record)`: the
  decompiler writes a loop to a stack record's end as
  `&stack0xfffffffc`, the frame offset where it ends, which has no C
  (`nv_save`, `nv_restore`, once their record is typed).
- A local the decompiler reads a register's value at entry from,
  `unaff_D2b`, is given that value by a register variable at its
  declaration, before the function's code sets the register: a switch
  with no case for a value passes its caller's D2 on, which C left
  unset (the SCPI setters' `setting`).  A6 and A7 are left alone.
- A local the decompiler reads from the caller's stack above the
  arguments, `in_stack_N`, is the long N + 4 above gcc's frame pointer,
  where every argument is a long in its own slot, so that gcc's frame
  holds the same bytes there (`program_end_report`'s caller's return
  address).
- An infinity or NaN assigned to an integer variable becomes its bits,
  `local_14 = __ghidra_bits(-__builtin_inf())`: the decompiler copies
  the constant's bits, where C converts its value, undefined for an
  infinity, and gcc gives no diagnostic for the conversion pass to find
  (`log` of 0, `pow`).
- A recovered switch's cases get their own labels.  The override holds
  only the targets, and the decompiler labels them by trying index
  values from 0 up, ten misses in a row allowed, until it has seen every
  target (`JumpBasicOverride::trialNorm`, in the decompiler's
  `jumptable.cc`; Ghidra's Java writes no starting value).  An index
  below 0, which every table after `subq #k` has, is never tried, so its
  case falls to default; an entry after the last new target is not
  tried either; and code read as entries past the table labels a case with
  values the guard excludes.  The exporter repeats the trial over the
  image, and, where it gives the decompiler's labels less a constant,
  labels each case with every index whose entry targets it, less that
  constant: `enabled_string` gets `case -1`, "Disabled", for 0, where it
  had a `case 2`; `leap_opportunity_date` gets November and December,
  which fell to default.  A target with no case is the default's.
  Where the trial misses a target, the decompiler switches on the
  table's entry instead, labeled by its value, as the machine does,
  and that is left.
- A local variable a call's result is assigned to is named as
  `names/results.tsv` names the callee's result, `uptime =
  get_uptime()`, so that a result reads the same at every call: only a
  variable every value of which is a result so named, whose name is the
  decompiler's own rather than one given on purpose, and where no other
  name in the function is the same.  The table is keyed by the
  functions' names, which the images share.
- An integer type named by its size on the machine is the
  `<stdint.h>` type it is on this target, where `int` and `long` are
  32 bits: the decompiler's `undefined4` and `uint`, and C's `unsigned
  int` and `long`, are `uint32_t` and `int32_t`.  `char` stays, the
  type of text, and `bool`; only the odd sizes, `undefined3` and the
  like, keep their names.
- A character constant that is no text is an integer: the decompiler
  prints every constant of a char type as a character, so a flag's 0
  and 1 read `'\0'` and `'\x01'`.  A printable character and `\n`,
  `\r`, `\t` stay.
- A value the code pushes on the stack and pops again gets a variable
  of its own, `pushed_n`.  Where the slot is also written at other
  widths, a byte pushed for a call or half a double, the decompiler
  takes it a piece at a time: the C stores each byte of the value in a
  variable and reads it back as their `CONCAT`, `holdover`'s
  `(float)CONCAT31(CONCAT21(CONCAT11(uVar12,uVar13),uVar14),uVar15)`
  for a float kept while it converts another.  Where the p-code shows
  every piece is the `SUBPIECE` at its offset of one value, kept across
  the calls between (`INDIRECT`), and nothing else reads the pieces,
  the first piece's statement becomes `pushed_1 = fVar2;`, the others
  go, and the `CONCAT` is `pushed_1` (283 of the 1546 idioms in the
  file's C).
- A structure the decompiler reads as an integer, which C has no
  conversion for, reads its bytes as the integer they make: an operand
  -- a variable and the members after it -- of a structure type under
  a cast to another type, or compared or computed with, is
  `__ghidra_aggregate_bits(operand)`, so that
  `(uint32_t)__ghidra_aggregate_bits(value.time)` of a `time_of_day`;
  and a cast of an integer to a structure only to compare it goes.  The
  tokens give each operand's type, which the text cannot.
- A double the code moves as its two longs is moved whole: read as
  `__ghidra_as(double, CONCAT44(...))` of its own two halves it is the
  double, stored a finite constant's halves the constant
  (`interval_reading.value = 9.91E37`, SCPI's NaN), and stored another
  double's, that double.  Only a floating operand, a double where its
  halves are 4 bytes apart.
- A sign extension the decompiler builds of `CONCAT`s is a cast:
  `s = x >> 7` is x's sign, and `CONCAT31(CONCAT21(CONCAT11(s,s),s),x)`
  is `(int)(signed char)x`, where the p-code shows every high piece is
  x shifted right signed by its width less one; the sign's statement
  goes where nothing else reads it.

## Hand-written C

A function rewritten by hand lives in
`third_party/firmware/rewritten/IMAGE/`, beside the decompiled C, a file to a
subsystem (`clock.c`).  The exporter finds the functions those files
define (`rewritten=`), keeps the decompiler's reading of each in the
exported C inside `#if 0`, for reference, and includes the files after
its own C, so that they see the types, globals and prototypes it
declares, and before them the folder's `.h` files, what the files
share (`cpu.h`, the status register's helpers); a function keeps its
generated prototype.  The files are
included in sorted order and compiled as one translation unit, so each
file-scope name takes its file's name as a prefix (`DUART_`,
`duart_`), and one that several files share lives in a header or in
the one that sorts first among them (`duart.c`'s control characters,
which `sci.c` uses).  The checks run
the hand-written function as they ran the decompiled one, against the
image's own.

A constant is typed: `static const uint32_t SECONDS_PER_MINUTE = 60;`,
inside the function that uses it when only one does, at file scope with
the file's prefix when several do.  A set of coded values, an ADC
channel or a task's state, is an `enum`.  A `#define` names a constant
only where C wants a constant expression, a `case` label, an array's
size or a static initializer, which a `const` object is not before
C23.  A string used in one place is a literal there.
A table in ROM that only hand-written code reads is a `static const`
the same way, its values taken from the image and its comment naming
the image's label and address: `fpga.c`'s `fpga_bits` is
`fpga_bit_table` (`0x41310`).  A table the decompiler's functions
read too keeps the image's name and is `const` but not `static`:
`libc.c` defines `ctype_table`, which `gps_receive` and `doprnt` read,
and the exporter, finding the definition, declares its extern `const`
and gives it no `.set`, so that they read the C's.  Not where the
image reads past the
table: `clock_days_before_month`'s unchecked month reads the word
before `days_before_month` and those after it, `exception_name` reads
`exception_names` by an unchecked, unaligned offset, and
`status_negative_conditions` and `status_report_messages` are indexed
by any condition, so they keep the image's; `random_dither` reads the
byte after `dither_table`, which its copy keeps as a 129th entry.  Not
where a table's address outlives the call in RAM, which the harness
compares: the DUART's loopback test leaves pointers into its strings,
and `status_init` keeps `status_conditions`' in the status model.  A
returned string is compared by its text, so `error_message_lookup`'s
table is its own.

The hand-written C and the Ghidra type headers are in Google's style
(`.clang-format`), with braces on every control statement's body:
`make format-c` formats them and `make check-format-c` fails on one
that is not.  A signal's `#define` in `psos-signals.h` stays on one
line, which is how the exporter reads it.

`tools/firmware/rebuild_link.py` links it into a rebuilt image: each
piece of assembly at its own address, where it must be, and is, the
image's bytes, and gcc's code -- the C, the bridges, its constants -- at
`0x600000`, above every chip select the reset code programs, and its
data at `0x6f0000`, with libgcc for the float comparisons the decompiler wrote
as operators.  A C function the assembly calls is the image's own copy,
`__image_NAME`, so that the original's callers keep the original's
callees and argument layout until a function is replaced; gcc's C calls
gcc's.  `tools/firmware/function_check.py` (`make report-decompiled`)
then calls each C function's image copy and gcc's copy in Unicorn, from
the same RAM, and compares result and RAM: a measure of the decompiled
C, not a gate (PLAN.md, "A rebuild that runs, in readable C").  A
difference whose cause is found and is not the C's is known
(`KNOWN`): the function counts as known while each failing case
matches the difference's pattern, and fails as any does otherwise.  Both
checks (`tools/firmware/harness.py`) take a pointer left in RAM, at an
even address, as the same when the two point at the same place in the
same text, in ROM or among gcc's constants: gcc keeps its
own copy of a literal the image keeps in ROM.  An empty text is the
same only when one pointer is in ROM and the other among gcc's
constants: gcc keeps `""` as any literal's end, so nothing before it
tells (`loopback_expect_next` and `sci.transmit_next` reset to `""`);
and a pointer to a C
function's image copy and one to gcc's copy are the same (a driver's
echo routine).  And a call starts with
A6 at a caller's frame on the harness's stack, its saved frame pointer
0, its return address the harness's and its first argument a name,
`"gpsx"` as `root_task` passes it, since the image's code may walk its
caller's frame (`float_raise` reads the return address there).
gcc keeps a frame pointer too (`-fno-omit-frame-pointer`), as the
original compiler did: `psos_pack_name` reads its caller's first
argument, the task or exchange creator's name, through the caller's A6.  The stack is filled with one byte
at a call's start, and each frame a `link` makes is filled with it
again, so that a local read before it is written, a message's bytes
its sender leaves unset among them, reads the same in both copies,
not what an earlier callee left where each copy's frames happen to
lie.  The EEPROM, chip select 9's 8 KB at
`0x400000`, is erased for each call and compared after it as RAM is;
gcc's code is above every chip select so that a read of the EEPROM
finds no code, and its data, a bridge's `__ghidra_ccr` and libgcc's
`_fpCCR`, apart from its code, which the function check keeps
read-only.  libgcc's float routines are PIC and reach `_fpCCR` through
the GOT, which the link gives an output section of its own, as ld's
default script does (ld takes a GOT entry's offset from that section's
start), and the harness loads.

What lies outside memory the harness models, the same for both calls,
at its boundary rather than routine by routine.  `trap #7`, and
`trap #1`, which saves or restores a status register for `setjmp` and
`longjmp`, take the exception through the primary's vector table to
the image's own handler, on the supervisor stack, which setting the
supervisor bit makes A7 from a task's user stack, and `rte` returns from
it, which Unicorn leaves to its interrupt hook; and `rtr`, which ends
pForth's code words (`forth_compile_code`, `0x19338`) and which Unicorn
takes for an illegal instruction, pops the condition codes and the
return address as the processor does (MC68331UM, table 5-1).  The kernel's traps, `#3` and `#4`, are
answered: for each service and I/O function (`psos.md`) the harness
knows, from the binding's stubs, the registers it reads and returns
in; a call records the service and those registers, a message by the
20 bytes from its `+4` the kernel copies and a write's buffer by its
contents, since a caller's buffer on its stack lies where its own
frame puts it, and returns no error and `0x6b000000`, an address
nothing is mapped at, in each result register; `request_x` fills its
buffer with that value, a read reads nothing and a write writes all it
is given.  But a case starts with an id, another such address, in
`spoo_exchange` and `pllc_exchange`, as `root_task`'s creates leave
one, and `request_x` from either receives the message its receiver
expects (`structures.md`): a `spoo_request` for `scpi_task_create` at
priority `0x19`, as `scpi_task_start` sends it, and a `pllc_request`
setting `:SAMPle:INTeger` (descriptor `0x334de`) to 7.  The write of DUART channel B, the console's string
device and the SCI (devices 0, 2 and 3: `drtb_write`, `sci_write`)
sends the string at A0 whatever D2 holds and returns its length in D2,
as the routines do; service 8, `alloc_seg`, returns a segment of the
size in D1 from RAM's top quarter, and service 3, `ident`, a task
record there, one per name, each from the same place in both calls, so
that a caller uses it and RAM compares it.  RAM starts each case zeroed
but for the kernel's configuration and I/O table, which
`psos_config_init` copies from ROM at start-up (`structures.md`, "The
kernel's configuration"); the data area the configuration names as
`kernel_init` (`0x15cd8`) makes it, the image's own code run once on
those tables but for its calls of each device's function 0 and its
creates of ROOT and IDLE: the supervisor stack, the free lists of
exchanges, message buffers and task records, regions 0 and 1 and the
empty queue of tasks waiting on a region at `+0x64`, which
`os_report_regions` walks; the exchanges `root_task` creates, `gpsx`,
`plla` and `pllc`, each made by the kernel's own code for service 20,
`kernel_create_x` (`0x1537e`), on the list at the data area's `+0x14`
that `os_report_exchanges` walks; and the first long of the data area, the
running task's record, which is `ident`'s for the name 0.  And as the
image's own `status_init` (`0x2f480`) makes them, run once as a call
is, the segments it allocates kept: `status_object` and its seven
registers, its error queue and `status_ready` (`structures.md`, "The
status system"), which `root_task` sets up before it creates the tasks.  The devices -- the DUART
at `0x200000`, `0x300000` to `0x30ffff`, `0x500000` and the MC68331's
modules from `0xfff000` -- are memory zeroed for each call, and each
write to them is recorded; but the registers the polled routines wait
on read as the data sheets say they do once the wait is over: the
DUART's SRA and SRB with TxRDY, the SCI's SCSR with TDRE, the QSPI's
SPSR with SPIF and its SPCR1 with SPE clear (MC68681; MC68331UM
D.4.6, D.4.13, 6.3.5.1).  The kernel calls and device writes, in
order, are a call's effects, which the two calls must make alike; a
service the harness does not model stops a call, as an unmapped
address does.  A trap whose handler never comes back -- `#5`, the
kernel's return from interrupt, and `#11` and `#12`, which restart --
ends a call, as does a zero divide, which the common exception entry,
with no task's handler set, records before it spins (`restart.md`,
"Both end in the watchdog"), and a write to code, which on the board
raises a bus error (the boot chip select is read only, CSORBT
`0x6870`, and the bus monitor on, SYPCR `0xcc`).  A branch to itself,
`fatal_error`'s spin, ends a call halted, since no interrupt comes;
and a call still running at its 1000th kernel call, a task's loop,
ends there, before making it.  Two calls that end the same way are
compared as if they had returned, but for their result.  And the stack is filled with `0xa5` at a call's start,
so that a local read before it is written reads the same in both.
Each function runs eight cases: in the first six a pointer's buffer
holds bytes, and in the last two longs that are pointers, so that a
routine given a record of pointers runs on: into memory of the
harness's own, 64 KB at each address the fill's longs point into from
`0x31300000`, filled the same way, each 16 bytes text and a NULL.  The
seventh case's fill is `1010` over, every long of it `0x31303130`,
which holds itself, so that a list linked through a record's first
three longs never ends; the eighth's, `10141018101<`, points each of
those longs at the next one's place and the third at the NULL, so that
such a list ends (`list_unlink`, `klok_insert_sorted`,
`region_segment_containing`, `os_report_process`).  That memory, as
the EEPROM, is filled for each call, compared after it as RAM is, and
holds no code, so that a jump into either faults.  Some parameters are
given the image's own object their callers pass: by its name, an SCPI
formatter's `output` and the lexer's `input`, the console port's
object (`0x29e18`), and a header `path`, a buffer set back to the main
tree's root list (`0x53034`), as `scpi_path_reset` does; and by its
type, `status_object`'s model, one of its registers by the case, and
by the name `error_queue` its queue, `aging_curve` for an
`aging_curve_state *`, `status_screen_snapshot` for a
`status_snapshot_record *`, and for a `scpi_step *` the first step of
`:SAMPle:INTeger`'s descriptor, the request's; and
`os_report_exchange` an exchange from the kernel's list, the case'th,
as `os_report_exchanges` passes each.  An SCPI handler,
named for its path, is given what `scpi_call_handler` (`0x29642`)
passes from the call record `scpi_interpret` builds: its parameters
start with the word at `+0x22` of each node of its path that holds
one, as `scpi_header_resolve` (`0x27d80`) lays them, the register
indexes of the `:STATus` queries (`:OPERation:HARDware` is 2 and 6);
and its record holds its node, its index, 0 for the setter or 1 for
the query, an arena, the parameters, the response, the console port as
input and output and the status model, as `scpi_task` passes them.
The parser's own routines get such a record for
`:STATus:OPERation:HARDware:ENABle?` by the names `call`, `items` (its
node's response items, for `scpi_format_response`), `response` (the
record's response slot, for `scpi_call_handler`) and `context` (a parser
context as `scpi_context_init` fills it, for
`scpi_parse_message_unit`).  And
`function_check.py` starts the functions that read what their callers
set with it: `qsort`'s element size and comparator for its sorts and
swaps, as its one call gives them (2 and `satellite_compare`), and the
pllc task's request for those that read it after `get_setup_vars`,
the image's own run on the request `request_x` receives, or for
`pll_scpi_do_action` on `*TST?`'s action, and for
`validate_or_doaction`, which `pll_scpi_command` calls on every
request, each in turn; and the pForth dictionary for
`pforth_define_word` and `diag_words_define`, the image's own
`pforth_dictionary_init` run as `console_init` runs it first, since
with the dictionary pointer null a definition returns at once.  After
its eight cases a function may run cases of its own (`GIVEN_CASES`),
where the generated arguments never reach what its callers pass or
what its body treats apart: `pforth_define_word`'s argument and result
counts from `diag_words_define`'s table (`hdac_write` takes 2, which
found its loop's count read back from a register nothing set),
`bytes_equal_n`'s two strings of one length, `double_to_digits`'
digit count and mode as `doprnt` passes them, and a subnormal, an
infinity and a NaN for `frexp`, `modf`, `sqrt`, `log` and `fabs`.
And the stack between and above the arguments where the original reads
them holds the same pointers into the pointees in both calls, the
seventh case's fill, so that a function whose prototype leaves out an
argument its body reads, a local `in_stack_N` in gcc's C, differs.
A parameter that
points at a routine, of a function typedef whose parameters the C
declares (`printf_function`, `spoo_function`, `dac_measure`,
`write_function`), is given a routine of the harness's own at
`0x31400000`, one for each position, the same in both calls, whose call
is an effect, compared as kernel calls are: a `printf_function` formats
its arguments with the image's own `vsprintf` (`0x608fe`) into a buffer
of its own, records the text and returns its length; any other records
each argument where the image's routines read it, a char or `char_slot`
at a 2-byte slot's start and a char pointer by its text, and returns
`0x6b000000`, as a kernel service does.

The installer's functions (`installer::` in the sidecar) run on a
machine of their own, a profile of the harness's (`harness.Profile`)
in place of the primary's constants, and are counted apart.  Its RAM
starts as the entry at `0xa58` leaves it (`restart.md`, "Forced
installer entry with an unusable primary"): the records unpacked to
`0x100400`--`0x10aad2`, `decompiled_check.py`'s `installer_records`,
and ROM's first `0x400` bytes, the boot vector table, copied to
`0x100000`, its vector base, so that trap #1 and trap #7 take
`installer_trap_1` (`0x108466`) and `installer_trap_7` (`0x100f32`).
Its kernel's tables are as `installer_psos_config_init` (`0x1083b6`)
copies them, from `0x1084da` and `0x108578` to `0x10ab84` and
`0x10ab60`, and its data area, from `0x10b0c8`, as
`installer_kernel_init` (`0x1026de`) makes it, run to the same places
as `kernel_init`, whose bytes it is; its status object, `0x10ad06`, as
its root task (`0x100d40`) has `0x107a00` make it, passing its address
to `status_create`'s copy (`0x105d98`), whose model, a register of it
and its error queue a parameter is given by type and name as the
primary's are.  The root task creates no exchange
itself; its spool's id, at `0x10afbe`, which `0x103efe` creates as
`"spoo"`, is a distinct id, from which `request_x` receives the SCPI
task's creator, `0x107886`, at priority `0x19`, as the root task sends
it (`0x100d20`), and a call's caller's first argument is that name,
`0x104088`.  Its one device, DUART channel B, writes through
`0x10380a`, `drtb_write`'s code, and a `printf_function` formats with
`installer_vsprintf` (`0x10aaa6`).  The arguments' buffers lie at
`0x900000`, outside RAM, which the installer's code fills, and are
compared as RAM is, pointers to the same literal or function the same;
the unpacked image is a place literals lie, as ROM is, and is kept
read-only: Unicorn protects whole 4 KB pages, and the image shares its
first and last with the vectors and its data, so a write there is
undone and ends the call as one to ROM does.  The primary's other objects,
the console port to the SCPI handlers' records, are not given.

In the Z3805A 3543B image this gives 1813 functions, and every one but
the ten thunks decompiles.  137 switches are recovered; `0x24ff2` and `0x25140`, a
`jmp (d8,PC,D6.w)` with no table load, are not, and need not be: each
enters an unrolled run of shifts part way, all of it code already
(`docs/scpi/README.md`).  1813 of the functions are named.  The output
is `third_party/firmware/decompiled/z3805a-3543b.c`.

The project is kept as `third_party/firmware/ghidra/smartclock.gar`,
a Ghidra archive in Git LFS (install `git-lfs` to fetch it): restore it (File, Restore Project, or ghidra-mcp's
`restore_project`) to work on it, and archive it over that file after
work worth keeping.  The scripts rebuild everything in it from the
image but names given by hand.  An exception in a script does not roll
back what it did, so save the program before each script that changes
it.
