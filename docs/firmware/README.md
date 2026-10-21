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
the slot's end.  The routines called that way take it too.

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
   defines=tools/ghidra/types/mc68681.h`.

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
cases from them (below).  It removes the case
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
`structures.md`).  Every SCPI handler takes what `scpi_call_handler`
gives it, `int NAME(void *parameters, void *response, void *call)`,
unless its row gives a prototype.

The convention is `key=value` pairs, which the names file's header sets
out: its kind (`c`, `register`, `exception`, `pforth`, `psos-service`,
`other` or `undetermined`), a confidence (`run`, `read`, `documented`,
`derived` or `assumed`, strongest first, with `KEY.confidence` for one
key weaker than the rest), the registers `in` and `out` with their
types and names, the registers a result is also left in, `copies=A0:A1`
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

`types/mc68331.h` lays out the CPU's own modules as the names file
types them: `gpt` (`0xfff900`), `sim` (`0xfffa00`) and `qsm`
(`0xfffc00`), each register a field at its offset in MC68331UM Tables
D-2, D-3 and D-13; and it names their bits, `SCCR1_TIE`, from the
registers' pages in Appendix D, as `#define`s, which Setup leaves out of
what Ghidra parses and the exporter copies into the C (`defines=`).
The exporter then names the constants written to or tested in a
register by those bits, where every bit has a name, so that the C
reads `qsm.sccr1 = uVar1 | SCCR1_TIE` and `uVar1 & ~SCCR1_PE`; the
ports' bits, the board's wiring, stay numbers.  `types/mc68681.h` does
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
`fpga_set_bit(1,8)`.  The header's coded values are enums, declared
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
names: of the 85, 74 give the same result, RAM and effects and keep
gcc's callee-saved registers, 4 reach what the harness does not model
and are skipped, and 7 (the installer's) are not run.  The
whole file compiles to an object (`make check-decompiled`, whose flags
add `-fno-builtin -fno-tree-loop-distribute-patterns`, since the image's
C library has no `puts`, `strlen` or `strcmp` for gcc to call).  The
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
`(type)__ghidra_bits(v)`, a floating value's bits as an integer.  It
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
  next that way.
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
  the call, as is an argument in such a register the C reads again
  after the call, which the decompiler takes the call to keep.
- A negative constant an equality test compares with a narrower
  unsigned operand is that operand's value, `!= 0xff`: the decompiler
  prints the byte as -1, but C promotes the operand to int
  (`nv_slot_erase`, on an erased EEPROM).
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
C, not a gate (PLAN.md, "A rebuild that runs, in readable C").  Both
checks (`tools/firmware/harness.py`) take a pointer left in RAM, at an
even address, as the same when the two point at the same place in the
same text, not empty, in ROM or among gcc's constants: gcc keeps its
own copy of a literal the image keeps in ROM; and a pointer to a C
function's image copy and one to gcc's copy are the same (a driver's
echo routine).  And a call starts with
A6 at a caller's frame on the harness's stack, its saved frame pointer
0 and its return address the harness's, since the image's code may
walk its caller's frame (`float_raise` reads the return address there).
gcc keeps a frame pointer too (`-fno-omit-frame-pointer`), as the
original compiler did: `psos_pack_name` reads its caller's first
argument through the caller's A6.  The EEPROM, chip select 9's 8 KB at
`0x400000`, is erased for each call and compared after it as RAM is;
gcc's code is above every chip select so that a read of the EEPROM
finds no code, and its data, a bridge's `__ghidra_ccr` and libgcc's
`_fpCCR`, apart from its code, which the function check keeps
read-only.  libgcc's float routines are PIC and reach `_fpCCR` through
the GOT, which the link gives an output section of its own, as ld's
default script does (ld takes a GOT entry's offset from that section's
start), and the harness loads.

What lies outside memory the harness models, the same for both calls,
at its boundary rather than routine by routine.  `trap #7` takes the
exception through the primary's vector table to the image's own
handler, and `rte` returns from it, which Unicorn leaves to its
interrupt hook.  The kernel's traps, `#3` and `#4`, are answered: for
each service and I/O function (`psos.md`) the harness knows, from the
binding's stubs, the registers it reads and returns in; a call records
the service and those registers, a message by the 20 bytes from its `+4` the kernel copies and a
write's buffer by its contents, since a caller's buffer on its stack
lies where its own frame puts it, and returns no error and
`0x6b000000`, an address nothing is mapped at, in each result register;
`request_x` fills its buffer with that value, a read reads nothing and
a write writes all it is given.  The devices -- the DUART at
`0x200000`, `0x300000` to `0x30ffff`, `0x500000` and the MC68331's
modules from `0xfff000` -- are memory zeroed for each call, and each
write to them is recorded.  The kernel calls and device writes, in
order, are a call's effects, which the two calls must make alike; a
service the harness does not model stops a call, as an unmapped
address does.  And the stack is filled with `0xa5` at a call's start,
so that a local read before it is written reads the same in both.

In the Z3805A 3543B image this gives 1811 functions, and every one but
the ten thunks decompiles.  136 switches are recovered; `0x24ff2` and `0x25140`, a
`jmp (d8,PC,D6.w)` with no table load, are not, and need not be: each
enters an unrolled run of shifts part way, all of it code already
(`docs/scpi/README.md`).  1811 of the functions are named.  The output
is `third_party/firmware/decompiled/z3805a-3543b.c`.

The project is kept as `third_party/firmware/ghidra/smartclock.gar`,
a Ghidra archive in Git LFS (install `git-lfs` to fetch it): restore it (File, Restore Project, or ghidra-mcp's
`restore_project`) to work on it, and archive it over that file after
work worth keeping.  The scripts rebuild everything in it from the
image but names given by hand.  An exception in a script does not roll
back what it did, so save the program before each script that changes
it.
