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
below, which pins each argument's bytes in custom storage.

## Decompiling with Ghidra

Five GhidraScripts in `tools/ghidra/` take an image imported raw at
address zero as `68000:BE:32:CPU32` to C, under `analyzeHeadless
-postScript` or ghidra-mcp's `run_ghidra_script`.  Each is safe to run
again.  In order:

1. `SmartClockSetup.java`.
2. `SmartClockInstaller.java RECORDS ENTRY`: `0xbbc 0x10135c` for the
   Z3801A 3543 and Z3805A 3543B (`restart.md`, "Forced installer entry
   with an unusable primary").
3. `SmartClockSetup.java` again, now over the installer too, with
   `types=tools/ghidra/types/smartclock.h`,
   `names=tools/ghidra/names/IMAGE.tsv`, `scpi=FILE` from
   `smartclock-cli dump-scpi --handlers IMAGE` and
   `pforth=docs/firmware/pforth/IMAGE.txt`.
4. `SmartClockForth.java`.
5. `SmartClockStackParameters.java`.
6. `SmartClockFormatStrings.java`.
7. `SmartClockExportC.java FILE`.

**Setup** makes the ROM read-only, so values loaded from it fold to
constants; maps RAM, the DUART, EEPROM, the CS4 and CS5 ranges and the
GPT, SIM and QSM, the I/O ones volatile; labels the on-chip registers
from MC68331UM Tables D-2, D-3 and D-13 and the DUART's; types both
vector tables, the boot one at `0x0` and the primary's at `0x10000`,
and makes a function at each handler; and types the string tables,
removing the code analysis decoded in them (a 0x61, `a`, is a `bsr`, so
code begun in a string "calls" on through the table).

It recovers the switches.  The bound check before a table is a `cmpi`,
`cmpa`, `subq` or `subi` and a `bhi`.  After `subq #k` or `subi #k`
the index runs from -k to 0, so the `move.w` displacement points 2k
bytes into the table: the "twelve bytes late" above.  Analysis types
its late copy of the table over the case bodies and decodes code from
the wrong cases.  Starting from the real cases and following their
flow, the script clears what overlaps each real instruction and
disassembles it, rebuilds the owning function's body and records the
targets as a decompiler jump-table override.  It removes the case
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
function's calling convention.

The convention is `key=value` pairs, which the names file's header sets
out: its kind (`c`, `register`, `exception`, `pforth`, `psos-service`,
`other` or `undetermined`), a confidence (`run`, `read`, `documented`,
`derived` or `assumed`, strongest first, with `KEY.confidence` for one
key weaker than the rest), the registers `in` and `out` with their
types and names, `returns=ccr` for the condition codes, the `error`
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
`"convention":"unknown"`.

The exporter also rewrites the signature line of each function that is
not C, so that the C says so as far as C can.  A macro for its kind
leads it -- `__pforth`, `__regcall` (register routines and kernel
services), `__exception`, or `__naked` for `other` and `undetermined` --
each gcc's `__attribute__((naked))`, `__exception` with `interrupt` too,
defined at the head of the file.  Each parameter in a register carries
gcc's m68k `__asm("d0")`, where the convention is `run`, `read` or
`documented`.  A comment after the line gives what C cannot say: its
results, condition codes, errors and preserved registers.  Ghidra's
"Unknown calling convention" warning is dropped from these; it remains
on C functions whose narrow stack arguments were given storage of their
own, where it means only that.

    __regcall double scpi_double_multiply(double a __asm("d0"),double b __asm("d2")) /* out D0:D1 double; returns ccr; error D2 -2 underflow, D2 -1 overflow; preserves A0 */  `jq 'select(.convention != "unknown" and
.confidence != "run")'` lists what is left to confirm.

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

`types/smartclock.h` declares the structures (`structures.md`), parsed
into the program before the names so a row may use them: a label's
row may give its data type, `gps_state` or `uint8_t[6]`.  The header and
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
entry and at each vector's handler.  Boot code jumps into plain RAM,
which the overlay does not serve; the jump is marked a call, so it
reads `func_0x0010135c` rather than flowing into RAM with no code.  The
installer's functions are not labeled at their plain addresses: the
primary keeps its own data there.

**Forth** gives the pForth console's kernel its register convention
(`console.md`, "The kernel's registers"): each function in the span of
the `forth_` words that uses A6, A5, A4 or A3, and has no frame,
takes those registers as parameters -- `sp`, `user`, `dp`, `input` --
with `user` a pointer to the end of a `forth_user` structure, so
`*(A5 - 0xe)` reads as `ADJ(user)->base`.

**StackParameters** gives functions the stack parameters their code
uses, where Ghidra's default convention misplaces them (see "The
calling convention", above) or the decompiler rejects a function
("Cannot properly adjust input varnodes").  The layout comes from the
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

5. `SmartClockStackParameters.java`.
6. `SmartClockFormatStrings.java`.
7. `SmartClockExportC.java FILE`.
, in address order, after the
program's structures (packed, as the image lays them out) and an
`extern` for each named, typed global with its address, so the file
stands alone: a global with a structure's type reads as `foo.bar`, a
pointer to one as `foo->bar`.  The declarations compile, taking `long`
as 32 bits, as on the 68000; a global still accessed as
`DAT_00100c6e._2_1_` is a record that has no type yet.

In the Z3805A 3543B image this gives 1824 functions, and every one
decompiles.  137 switches are recovered; `0x24ff2` and `0x25140`, a
`jmp (d8,PC,D6.w)` with no table load, are not, and need not be: each
enters an unrolled run of shifts part way, all of it code already
(`docs/scpi/README.md`).  1171 of the functions are named.  The output
is `third_party/firmware/decompiled/z3805a-3543b.c`.

The project is kept as `third_party/firmware/ghidra/smartclock.gar`,
a Ghidra archive in Git LFS (install `git-lfs` to fetch it): restore it (File, Restore Project, or ghidra-mcp's
`restore_project`) to work on it, and archive it over that file after
work worth keeping.  The scripts rebuild everything in it from the
image but names given by hand.  An exception in a script does not roll
back what it did, so save the program before each script that changes
it.
