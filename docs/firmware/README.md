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

## Decompiling with Ghidra

Four GhidraScripts in `tools/ghidra/` take an image imported raw at
address zero as `68000:BE:32:CPU32` to C, under `analyzeHeadless
-postScript` or ghidra-mcp's `run_ghidra_script`.  Each is safe to run
again.  In order:

1. `SmartClockSetup.java`.
2. `SmartClockInstaller.java RECORDS ENTRY`: `0xbbc 0x10135c` for the
   Z3801A 3543 and Z3805A 3543B (`restart.md`, "Forced installer entry
   with an unusable primary").
3. `SmartClockSetup.java` again, now over the installer too, with
   `names=tools/ghidra/names/IMAGE.tsv`, `scpi=FILE` from
   `smartclock-cli dump-scpi --handlers IMAGE` and
   `pforth=docs/firmware/pforth/IMAGE.txt`.
4. `SmartClockStackParameters.java`.
5. `SmartClockExportC.java FILE`.

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
service calls (`psos.md`), the interval's
routine and cells (`interval.md`), the SCPI task and the installer's
entry (`restart.md`), the console's hook (`console.md`).  An address a
note gives only for another image is not carried over.  SCPI handlers
are named `scpi_query_PATH` and `scpi_set_PATH`, console words
`forth_WORD` and `diag_WORD`.  A name replaces one Ghidra made up, and
one from the names file also one these tables gave, but none replaces a
name given by hand.  A table vouches that its entries are code, so the
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

**Installer** unpacks the records the boot code copies to RAM, after
its vector table, into an overlay named `installer`, writable so that
cells the installer changes do not fold, and makes a function at its
entry and at each vector's handler.  Boot code jumps into plain RAM,
which the overlay does not serve; the jump is marked a call, so it
reads `func_0x0010135c` rather than flowing into RAM with no code.  The
installer's functions are not labeled at their plain addresses: the
primary keeps its own data there.

**StackParameters** repairs functions the decompiler rejects with
"Cannot properly adjust input varnodes".  This compiler pushes a char
or short argument in a 2-byte slot, while Ghidra's 68000 compiler spec
assumes 4-byte slots and infers overlapping inputs.  Each such function
gets the stack inputs the decompiler reports once it has no
parameters, in custom storage at their real offsets.

**ExportC** writes every function's C, in address order.

In the Z3805A 3543B image this gives 1752 functions, 1448 of them before
the names, and every one decompiles.  137 switches are recovered;
`0x24ff2` and `0x25140`, a `jmp (d8,PC,D6.w)` with no table load, are
not.  About half the functions are named.  The output is
`third_party/firmware/decompiled/z3805a-3543b.c`.

The project is kept as `third_party/firmware/ghidra/smartclock.gar`,
a Ghidra archive in Git LFS (install `git-lfs` to fetch it): restore it (File, Restore Project, or ghidra-mcp's
`restore_project`) to work on it, and archive it over that file after
work worth keeping.  The scripts rebuild everything in it from the
image but names given by hand.  An exception in a script does not roll
back what it did, so save the program before each script that changes
it.
