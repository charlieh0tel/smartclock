# What the firmware shows

What `third_party/firmware/images/z3816a-4001.bin` shows about how the receiver
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
finds them; bit 5 of PORTGP is the pin PGP5/OC3/OC1 (section 7.2).  Stefan
Hegnauer's description of the Z3801A's outer-oven circuit names the main
CPU as "U33 / MC68331" and the oven's control line as PGP5 (see
`ovens.md`).

The reset code at `0x2466e` sets the SIM up as follows (register
names and encodings from MC68331UM appendix D):

| Register | Value | Meaning |
| -------- | ----- | ------- |
| SYPCR `0xfffa21` | `0xcc` | SWE, SWP, HME, BME: software watchdog on and prescaled by 512, halt and bus monitors on (D.3.12) |
| SYNCR `0xfffa04` | `0xcf80` | W = 1, X = 1, Y = 15: f = f_ref · 4 · 16 · 8 (section 4.3.2): 16.777 MHz from the 32.768 kHz reference the manual's frequency tables assume (4.3.2, appendix A); EDIV 1, ECLK f / 16 (D.3.3) |
| PORTE0 `0xfffa11` | `0x40` | port E bit 6 high, the FPGA's PROGRAM line (`outputs.md`, "The FPGA bitstreams"); a cold start pulses it low and high (`0x22e30`), and the FPGA loader holds it low until INIT goes low (`0x32812` to `0x3285a`) |

and its chip selects so, from `tools/firmware/chip_selects.py`
(`restart.md`, "The chip selects"; block sizes from Table D-11, the
fields from Table D-12).  This is the Z3816A's map, and the Z3815A's
(below); the Z3801A, Z3805A and 58503A use another, `0x300000`
to `0x30ffff` one chip select, CS5, and the flash two banks, and the
Z3816A's boot code a third, which `chip-selects.md` gives with every
other image's:

<!-- chip_selects.py z3816a-4001 primary z3815a-4010 primary -->
Pins: CSPAR0 `0x2fff` at `0x24676`, CSPAR1 `0x02af` at `0x2467e`.

| Window | Select | Pin | CSBAR, CSOR; written at | Port | BYTE, R/W, DSACK | Device |
| ------ | ------ | --- | ----------------------- | ---- | ---------------- | ------ |
| `0x000000`--`0x07ffff`, 512 KiB | CSBOOT | 112 | `0x0006`, `0x6870`; `0x24686`, `0x2468e` | 16-bit | both, read, 1 wait | the ROM this image is |
| the same | CS6 | 121, PC3/ADDR19 | `0x0006`, `0x7070`; `0x246f6`, `0x246fe` | 16-bit | both, write, 1 wait | the boot ROM's own range, for writing it |
| `0x100000`--`0x10ffff`, 64 KiB | CS0 | 113, BR | `0x1003`, `0x6830`; `0x24696`, `0x2469e` | 16-bit | both, read, 0 wait | the RAM, read |
| the same | CS2 | 115, BGACK | `0x1003`, `0x5030`; `0x246b6`, `0x246be` | 16-bit | upper, write, 0 wait | the RAM, writes to the upper byte |
| the same | CS3 | 118, PC0/FC0 | `0x1003`, `0x3030`; `0x246c6`, `0x246ce` | 16-bit | lower, write, 0 wait | the RAM, writes to the lower byte |
| `0x200000`--`0x2007ff`, 2 KiB | CS8 | 123, PC5/ADDR21 | `0x2000`, `0x3bf0`; `0x24716`, `0x2471e` | 8-bit | lower, read and write, external | the DUART |
| `0x300000`--`0x3007ff`, 2 KiB | CS1 | 114, BG | `0x3000`, `0x7030`; `0x246a6`, `0x246ae` | 16-bit | both, write, 0 wait |  |
| `0x302000`--`0x3027ff`, 2 KiB | CS7 | 122, PC4/ADDR20 | `0x3020`, `0x6870`; `0x24706`, `0x2470e` | 16-bit | both, read, 1 wait | the word G is chosen by (`loop.md`, "τ and G") |
| `0x304000`--`0x3047ff`, 2 KiB | CS5 | 120, PC2/FC2 | `0x3040`, `0x7930`; `0x246e6`, `0x246ee` | 8-bit | both, read and write, 4 wait |  |
| `0x400000`--`0x401fff`, 8 KiB | CS9 | 124, PC6/ADDR22 | `0x4001`, `0x78f0`; `0x24726`, `0x2472e` | 8-bit | both, read and write, 3 wait | the checksummed block at `0x400080` |
| `0x500000`--`0x5007ff`, 2 KiB | CS10 | 125, ECLK/ADDR23 | `0x5000`, `0x7bf0`; `0x24736`, `0x2473e` | 8-bit | both, read and write, external |  |
| CPU space, interrupt acknowledge at level 2 | CS4 | 119, PC1/FC1 | `0xfff8`, `0x2bc4`; `0x246d6`, `0x246de` | 16-bit | lower, read, external; CPU space, IPL 2 | the interrupt acknowledge at level 2, as CS10 is in the Z3805A's map |

The boot code (`0x418` to `0x4d0`) writes CSPAR1 `0x03ab`, CSOR4 `0x7805`, CSBAR7 `0x3060`, CSOR7 `0x7070`, CSOR8 `0xb830` and writes no CSBAR10, CSOR10.

The Z3815A 4010's primary (`0x24b3e` to `0x24c06`) writes the same 26 values.
<!-- end chip_selects.py -->

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

- [The Z3805A 3543B in C: method and verification](rewrite.md), the
  front door to the decompilation, the hand-written C and its checks,
  which "Decompiling with Ghidra" and "Hand-written C" below detail
- [The GPS receiver link](gps.md)
- [Oncore message tables](oncore/README.md)
- [The interval](interval.md)
- [The time-interval counter](counter.md), the Z3805A's FPGA counter
  as the firmware drives it
- [The disciplining loop](loop.md)
- [The ovens](ovens.md)
- [The debug console](console.md)
- [Restarting](restart.md)
- [Chip selects](chip-selects.md), every image's, generated from its
  reset code
- [Pulse, time-tag and output commands](outputs.md)
- [58503A 3633 against 3704](revisions.md)
- [58503A 3704 against Z3805A 3543B](58503a-3704.md)
- [Z3801A 3543 against Z3805A 3543B](z3801a-3543.md)
- [The floating-point library](softfloat.md)
- [The C library](libc.md)
- [The real-time kernel](psos.md)
- [Structures](structures.md)
- [Modules](modules.md)
- [The firmware's own faults](quirks.md)

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
- Which of the FPGA's pins PE6 reaches.  The code makes it the FPGA's
  PROGRAM line (`outputs.md`, "The FPGA bitstreams"): an output (DDRE
  `0x40`, `0x2476e`), which the loader drives low with DONE (PF3) and
  holds low until INIT reads low ("FPGA INIT did not go low"), then
  raises before it waits for INIT high ("FPGA INIT did not go high";
  `0x32812` to `0x3285a`), and which a cold start pulses (`0x22e30`;
  `restart.md`).  A Low on DONE/PROGRAM and RESET together is how the
  XC3000 clears its configuration (`third_party/xilinx/xc3000.pdf`,
  Figure 20, 7-19), so PE6 is taken to be the part's RESET; that is
  inferred, not traced on a board.
- What 3704's PWMA pin, low while the holdover-recovery flag is set,
  is wired to (`loop.md`).
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
- That `0xfff907` bit 5 reaches P2/8 is an owner's report of a Z3801A
  board, not traced here; how the Z3801A firmware's "Oven" and
  "Secondary oven voltage" values relate to the volts at P2/9 was not
  worked out.  The ADC part is settled by the bench's photos: U35 is a
  National ADC0838CCV on the Z3805A and 58503A boards
  (`hardware-investigations.md`, item 5).
- Which CPU32 part this is: the register map matches the MC68331
  manual's and the `0xfff900` block rules out the 68332, but the part
  name comes from an owner's description of the board, not the image.
  The Z3801A and Z3805A boards on the bench carry an MC68331CFC16 at
  U33 (`z3801a-main-board.jpg`, `z3805a-cpu-fpga-flash.jpg`); the
  58503A's was not photographed, and no Z3816A board has been seen.
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
- The DUART's command registers are written fast enough apart.  The
  SCC68692's data sheet (`third_party/philips/SCC68692.pdf`, 2004 Mar
  03) asks "at least three edges of the X1 clock between writes" to
  the same command register (AC characteristics, note 5, page 8), and
  that "access to the upper four bits of the command register should
  be separated by three (3) edges of the X1 clock" (CRA, page 15; the
  miscellaneous commands, page 13): three edges, a period and a half,
  407 ns at 3.6864 MHz, not three periods (814 ns).  The six images
  that drive a 68681 (not the 58503B, whose part at `0x200000` is
  written 3 and read at `0x200002` on a synchronous select,
  `chip-selects.md`) write a command register twice with nothing
  between in 79 places, 15 in the Z3805A 3543B, all from ROM: two
  `move.b #imm,(xxx).l`, 8 bytes each, reset receiver then
  transmitter (`0x20`, `0x30`; `duartb_loopback_check` at `0x1ec88`,
  `duartb.c`'s end), transmitter then MR pointer (`0x30`, `0x10`;
  `duart_early_init` at `0x1ecf6`), or, the lower bits only, receiver
  then transmitter disabled (`0x02`, `0x08`; `duart_stop` at
  `0x1ebbc`, `0x1ebcc`); any other two writes to one
  register have more code between.  Counted at 16.777 MHz (59.6 ns a
  clock), assuming of the CPU32's pipeline, whose reference manual
  (CPU32RM) is not in `third_party/`, only that at most two of the
  second instruction's four words are fetched before the first one's
  write: the other two are fetched between the writes, each a bus
  cycle of at least three clocks (MC68331UM 4.5.1) and one wait state
  (CSORBT and CSOR1, DSACK `0001`), 8 clocks, 477 ns, more than three
  edges.  With all four fetched between, 16 clocks, 954 ns, it is more
  than three periods too.  The write itself is not counted: CS8 takes
  external DSACK in every map these writes run under, and if U3's
  DTACKN is that DSACK (`../hardware-investigations.md` item 21), each
  write lasts until DTACKN, two rising X1 edges after /CS (Figure 5,
  page 19), which only adds to the gap from one write's latching edge
  to the next.  The installers, packed in the image, were not swept;
  under the Z3815A's and Z3816A's boot map, where CS8 is synchronous
  (`chip-selects.md`) and SYNCR's EDIV makes ECLK f / 16 (D.3.3), each
  access is its own 954 ns E cycle (Table A-8).

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
small value to the end of its slot, a property of the language, not of a
convention a program can add.  Hence `SmartClockStackParameters.java`,
below, which pins each argument's bytes in custom storage.  A pointer's
prototype cannot carry custom storage, so a call through one -- the
self-test tables at `0x18458` and `0x17fe0`, the measurements
`dac_search_bisect` and `dac_search_step` are given -- passes its `char`
as `char_slot` (the image's `smartclock.h`), a 4-byte structure whose
first byte, its value, lies where this compiler puts a `char`'s, at the
slot's start, for Ghidra and gcc alike; gcc pads a 2-byte one to the
slot's end.  The routines called that way take it too, and a `call` row
gives each such call its callee's prototype.  Where the hand-written C
calls the image's own routine through a pointer and keeps its
prototype, it lays the slots itself: the GPS task's argument encoders,
from `oncore_messages` (`0x4f9c4`), take the index and the output
pointer as one structure at the 2-byte slot's start
(`gps_requests_encode`); and a port's echo, which holds the image's
`drtb_putc` or `sci_putc` where the image's reconfiguration stored it
(`0x1dd54`) and gcc's where the C's did, is given its character at both
ends of a 4-byte slot (`duart.h`, `duart_echo_slot`), where either
copy reads it.

## Decompiling with Ghidra

Six GhidraScripts in `tools/ghidra/` take an image imported raw at
address zero as `68000:BE:32:CPU32` to C, under `analyzeHeadless
-postScript` or [ghidra-mcp](https://github.com/bethington/ghidra-mcp)'s
`run_ghidra_script` (7.0.0, at `cf8b12d`), and a seventh,
optional, surveys what they leave untyped.  Each is meant to be
run again over its own earlier output; their dry-run modes cover only
part of what they change.  In order:

1. `SmartClockSetup.java`.
2. `SmartClockInstaller.java RECORDS ENTRY`: `0xbbc 0x10135c` for the
   Z3801A 3543 and Z3805A 3543B (`restart.md`, "Forced installer entry
   with an unusable primary"); `0xbc4 0x10136c` for the 58503A 3704,
   whose boot vector 43 is `0xa46`, which calls the unpacker at `0xaa8`
   (its records at `0xbc4`, 43025 bytes, the count it loads at
   `0xab0`) and jumps to `0x10136c` (`0xaa2`); its overlay is
   `installer::0x100000` to `0x10ab75`.
3. `SmartClockSetup.java` again, now over the installer too, with
   `types=third_party/firmware/rewritten/IMAGE/types/smartclock.h`,
   `types=third_party/firmware/include/mc68331.h`,
   `types=third_party/firmware/include/mc68681.h`,
   `names=third_party/firmware/analysis/IMAGE/names.tsv`, and
   `names=third_party/firmware/analysis/IMAGE/scpi-tree.tsv` and
   `names=third_party/firmware/analysis/IMAGE/installer-scpi-tree.tsv`, which
   `make scpi-tree` makes (the SCPI trees' records, and the
   installer's handlers),
   `scpi=third_party/firmware/analysis/IMAGE/handlers.txt`, which `make handlers`
   writes from `smartclock-cli dump-scpi --handlers IMAGE`, and
   `pforth=docs/firmware/pforth/IMAGE.txt`.
4. `SmartClockForth.java`.
5. `SmartClockStackParameters.java`.
6. `SmartClockFormatStrings.java`.
7. `SmartClockExportC.java third_party/firmware/decompiled/IMAGE/IMAGE.c
   defines=third_party/firmware/include/mc68331.h
   defines=third_party/firmware/include/mc68681.h
   defines=third_party/firmware/include/psos-signals.h
   results=third_party/firmware/analysis/IMAGE/results.tsv
   rewritten=third_party/firmware/rewritten/common
   rewritten=third_party/firmware/rewritten/IMAGE`, `common/` for an
   image that shares it.

Each path is from the repository's root, and IMAGE is the image's
name, `z3805a-3543b`: its folder, `third_party/firmware/analysis/IMAGE/`, beside
`IMAGE.bin`, holds everything that image's work is.  The 58503A
3704's folder, `third_party/firmware/analysis/58503a-3704/`, holds the same:
its `names.tsv` and `modules.tsv`, seeded from the Z3805A's through
`image_diff.py`'s matches (`58503a-3704.md`, "Method"; the names file's
header gives the rules) and then named and typed from the 58503A's own
code, its `handlers.txt`, SCPI trees, `smartclock.h`, `check-layout.c`
and `results.tsv`, its export, from a bare import as above with the
58503A's inputs (`58503a-3704.md`, "The export"), and its
`rewritten/`, begun as the Z3805A's and written to the 58503A's code,
with its SCPI trees' C, which `make scpi-tree IMAGE=58503a-3704` writes
as it does the Z3805A's; the C the images share lies in
`third_party/firmware/rewritten/common/` (`rewrite.md`, "Shared C").  `make
image-diff` regenerates the matches, the TSV `58503a-3704.md`
describes, that document's figures and, through `make seed-names`
(`tools/firmware/seed_names.py`), the 58503A's seeded `names.tsv` and
`modules.tsv`; `make check-image-diff` fails while any is stale.  The
Z3801A 3543's folder, `third_party/firmware/analysis/z3801a-3543/`, is seeded
the same way from the Z3805A's ([`z3801a-3543.md`](z3801a-3543.md)),
its `rewritten/` the Z3805A's own C less the SCI's, beside `common/`,
which it shares with the other two.  A change to the Z3805A's `names.tsv`,
`modules.tsv` or export regenerates them with `make image-diff` in the
same commit, and the 58503A's tree rows, which its names file shapes,
with `make scpi-tree IMAGE=58503a-3704`; a seeding rule or exception
is changed in `seed_names.py`, not in a row.  The make targets that
check and generate it are `third_party/firmware/Makefile`'s, which
runs alone, `make -C third_party/firmware TARGET IMAGE=z3805a-3543b`,
for every image without `IMAGE=`, and to which the top Makefile hands each target by name, so that `make
check-decompiled` is the same.

Optionally, after step 3 or any later one,
`SmartClockUntyped.java output=third_party/firmware/analysis/IMAGE/untyped.tsv`
lists the ROM that is neither code nor typed data, and the installer's
overlay's: each run of 4 or more undefined bytes (`min=N` for another
length), with the references into it, the defined item on each side,
and a class, `padding`, `erased`, `code-like` (a prologue or `rts` that
decodes cleanly, decoded in memory only), `referenced` or
`unreferenced`, and at the end, `#` lines, each class's runs and bytes.
It changes nothing in the program.  The TSV is checked in, rewritten
after the names change, so its diff shows what a change typed
(`structures.md`, "What is left untyped").

**Setup** makes the ROM read-only, so values loaded from it fold to
constants; maps RAM, the DUART, EEPROM, the CS4 and CS5 ranges and the
GPT, SIM and QSM, the I/O ones volatile; types both
vector tables, the boot one at `0x0` and the primary's, the first at
a multiple of `0x10000` above it that starts with the boot table's
stack pointer (`0x10000` in the Z3801A, Z3805A and 58503A, `0x20000`
in the Z3816A, Z3815A and 58503B, whose boot code and installer fill
the first 128 KiB), and makes a function at each handler; and types the string tables,
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
deleting it, since deleting a function's name deletes the function.  So
does any other label of the switch's that names a function, a table
being no function: analysis's reading of the 58503A 3704's switch at
`0x2eb22` labeled `0x32b62` `switchdataD_00032b62`, and the function
later made there, which `0x327e4` calls, took it.

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
handlers rather than documented; one an earlier run ended without the
comment gets it.  It clears word and long data at odd
addresses, which the 68000 cannot read, and resumes flow such data or
stray code blocked, clearing only what is in no function.  It removes
unnamed functions made in data: one at an odd address, or one whose
own code reaches an odd address or bytes that do not decode, that no
function calls.  It does this in one pass, since removing one function
leaves its callees looking stray; `dry-run` reports them and removes
nothing, and more than 20 is refused as a rule gone wrong.

After the names it removes two more kinds of unnamed function analysis
makes.  One is a shared tail: code the function before it reaches by a
conditional branch, which other functions jump into and nothing calls,
`forth_equal`'s `move.l #-1,(A6)` and `rts` at `0x192c8`, which
`forth_greater`, `forth_less` and `forth_0_less` branch to.  Analysis's
shared-return pass makes it a function and marks the jumps to it
calls; it goes back into `forth_equal`'s body, and the jumps are
jumps again.  The other is a function in memory with no bytes, at
`0x10135c` in plain RAM, where the boot code's jump into the
installer leads, or with no instruction at its entry, where analysis
typed a string or data over the code it began (the 58503A 3704's
`0x1c74e`, inside `"Log test message %d"`, and `0x5a634`, called by
stray code decoded from a table).  Then each unconditional jump from a function to
another function's entry is marked a call that returns, a tail call,
as the shared-return pass does only where the target was already a
function when it ran; the string tables are typed again, since the
repairs can leave stray code that a table's text ran into; and every
function's body is recomputed from its entry, so that it is what the
function's flow reaches, whatever the repairs before it left.

Names come from what the project has established.  `analysis/IMAGE/names.tsv`
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
gives it, `int NAME(void *parameters, void *response, scpi_call *call)`,
unless its row gives a prototype.

An `entry` row, its name field empty, makes a function at its address
that no wave has named yet, which keeps Ghidra's name: the 58503A
3704's new functions, which `image_diff.py` finds and `seed_names.py`
lists, their note the extent and how each was found, many of them
reached only through a pointer, which analysis does not follow.  Its
address is image_diff's reading, not a table's, so nothing in the way
is cleared as it is for a named entry: one that would be inside an
instruction, inside typed data such as a string, or inside another
function's body, or whose bytes do not disassemble, is reported
skipped, with why.  Undefined bytes are disassembled from it.  The
removals of unnamed functions above leave the functions entry rows
give.  A wave that names one makes its row a `function` row; an
`entry` row that gives a name is refused.

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
`third_party/firmware/decompiled/z3805a-3543b/z3805a-3543b.conventions.jsonl`:
one JSON object a line for every function, its address, name, note and
convention, with its lists split into register, type and name, or
`"convention":"unknown"`, and, for a function whose arguments are all on
the stack, `"stack"`: each one's offset from the stack pointer at entry
and its size, from the decompiler's prototype.  The original packs a
`char` or `short` in 2 bytes, or several into one long (`format_date`
takes its date so), where gcc gives each 4; the emulator harness lays
the image's arguments from it.  A function written by hand ("Hand-written
C", below) has `"rewritten"`, its file, `"calls"`, the functions its
decompiled reading calls by name, one entry to a call, and `"passed"`,
any `[callee, function]` pairs of a function its reading gives a call
as an argument (`psos_start`'s task entry, `spoo_run`'s function): the
image's calls, which `tools/firmware/module_figures.py` and the
function check's user-mode reach read, since the C leaves the reading
out.  A reading's local and label names vary between bare imports
(`switchD_` against `override_jmp_`, `local_54` against `acStack_54`);
its calls do not.

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
tables.  How C calls these routines depends on where they take their
arguments: one with any on the stack through `int NAME();`; one with
every argument and its result in registers by a real prototype when it
is in gcc's convention already (no arguments, the result in D0 or
D0:D1, gcc's callee-saved registers kept), and otherwise through a
typed register binding in `rewritten/bindings/`, or not at all where
none binds it (below, **ExportC**, and "Hand-written C", register-bound
calls).
`tools/firmware/decompiled_check.py` (`make check-decompiled`)
assembles them alone and compares every piece with the image, every
relocated field resolved at the image's own addresses: all 474
routines, in 494 pieces, are the image's bytes.  `jq 'select(.convention
!= "unknown" and .confidence != "run")'` over the sidecar lists what is
left to confirm.

SCPI handlers are named `scpi_query_PATH` and `scpi_set_PATH`, console
words `forth_WORD` and `diag_WORD`, and each has a note saying so -- the
query handler for its path, the setter, the pForth word or diagnostic
word -- unless the names file gives it one.  A name replaces one Ghidra made
up, and one from the names file also one these tables gave, but none
replaces a name given by hand.  A routine on several paths or words
keeps the first one's name, the rest further labels:
`scpi_query_STATus_OPERation_EVENt` also answers
`:STATus:QUEStionable:EVENt?`, and `forth_endif` is `then` too.  A
table vouches that its entries are code, so the
script disassembles each, and splits a function whose body ran into
it.  A string over an entry is cleared first, unless a names-file
label typed it: the 58503A 3704's `decimal_digit_pairs`, `"00"` to
`"99"`, runs on to the NUL in `scpi_parse_text`'s first instruction
at `0x24f9e`, which nothing calls, and the string tables typed it so.
Nothing an earlier run named outlives its row: each label the
naming arguments gave (Ghidra's `IMPORTED`) that no row, path, word
or word header gives now is removed, as `dmes_gpsm`, the old name of
the `dmes` array, was.

A row may give a C prototype, which types the function's arguments at
every call.  The float library passes its operands in registers, which
C cannot say, so a prototype may put a value in registers with `@`:
`float float_add(float a @ D0, float b @ D1) @ D0`, a double in a pair,
high register first, `@ D0:D1`.  An argument without `@` goes on the
stack: in this compiler's slots ("The calling convention", above), or,
where a prototype has a `char` or `short`, at the offsets the function
reads, when it reads as many arguments as the prototype has, each of
its size or a whole long for a narrower one, which then lies at that
slot's end.  A routine compiled to take its arguments promoted reads
them that way: the console's diagnostic words, which the code
`pforth_define_word` compiles calls with the data stack's 4-byte
cells, `diag_adc_read` its `uint8_t` at `+7`; and `log_status` reads
its `char` at `+12`, past an argument it ignores.  Ghidra's 68000 convention returns a float or double in the FPU
register FP0, but these images have no FPU and return one in D0 or
D0:D1, so every function returning one has its return annotated.

`third_party/firmware/include/psos-signals.h` names, as `#define`s, the
signals the drivers send their tasks, bits of the events `signal_v` ORs
into a task's pending word, by the firmware's own messages: `"wait_v
SIG_SIQ error in getc"` beside `psos_wait_v(0x4000,...)`.  A bit is a
task's -- 0x800 is `SIGINTR` to the console's and `SIG_SOQA` to the GPS
task, 0x200 the SCI's output queue and the interpolator's reading -- so
each name lists the functions of the driver its message comes from, and
the exporter names a constant signal a call to `signal_v`, `wait_v` or
`get_v` passes only there: `psos_wait_v(SIG_SIQ,0,0,(ulong *)0x0)`.  1,
0x20 and 0x100, which no message names, stay numbers.

`third_party/firmware/include/mc68331.h` lays out the CPU's own modules
as the names file types them: `gpt` (`0xfff900`), `sim` (`0xfffa00`) and
`qsm` (`0xfffc00`), each register a field at its offset in MC68331UM
Tables D-2, D-3 and D-13; and it names their bits, `SCCR1_TIE`, from the
registers' pages in Appendix D, as `#define`s, which Setup leaves out of
what Ghidra parses and the exporter copies into the C (`defines=`).  The
exporter then names the constants written to or tested in a register by
those bits, where every bit has a name, so that the C reads `qsm.sccr1 =
uVar1 | SCCR1_TIE` and `uVar1 & ~SCCR1_PE`.  A pair of byte registers
the firmware only ever moves as a word is one field, named for both,
with bits named across the word: `gpt.tmsk` for TMSK1 and TMSK2, so that
`gpt.tmsk = uVar2 | TMSK_OCI3`, and `gpt.tflg`.  The ports' bits, the
board's wiring, stay numbers.  A register the decompiler reads into a
local first, as it does a volatile one, names the local's tests too,
`uVar1 = qsm.scsr;` then `(uVar1 & SCSR_TDRE) == 0`, where every copy
into the local is of that one register or the test is the statement
after the copy: `duarta_isr` reads `sr_csr` and `rb_tb` into one local.
`third_party/firmware/include/mc68681.h` does the same for the DUART,
`duart` (`0x200000`), from Motorola's MC68681 data sheet (Tables 4-1 and
4-5): a channel's registers are a `duart_channel`, `duart.a` and
`duart.b`, and a register read and the one written at its address are
one field, named read then write, `sr_csr`, whose constant a store names
by the written register's bits and a test by the read one's.  A register
whose codes are not single bits, the command register's, is named only
by a code it equals, so that `duart.b.cr = CR_RESET_RECEIVER`, and a
code is never split into bits that do not mean it.  The code's shadow of
the DUART's write-only registers, `duart_shadow`, is named by the same
bits: `duart_shadow.imr & ~IMR_TXRDYB`.

The image's `smartclock.h` declares the structures (`structures.md`), parsed
into the program before the names so a row may use them: a label's
row may give its data type, `gps_state` or `uint8_t[6]`.  Parsing adds
and replaces types, beside Ghidra's own, and a structure, union, enum
or typedef at the top category that no header declares any more is
removed, so that a type renamed or dropped from the header leaves the
project and the exported C: `robin_format`, now `data_format`, and
`scpi_root_children`, now `scpi_child_list`.  The header and
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
cells the installer changes do not fold, types its copy of the vector
table as pointers, the initial SP's too, as Setup types the primary's,
makes a function at its entry and at each vector's handler, and clears
the code analysis found in the packed records themselves, which never
run where they lie, so that the overlay is the installer's only copy.
Boot code jumps into plain RAM,
which the overlay does not serve; the jump is marked a call, so it
reads `func_0x0010135c` rather than flowing into RAM with no code.
The boot vector table at `0x0` is the one the boot code copies to RAM
for the installer, so Setup, once the overlay exists, refers each of
its vectors that points into the overlay (`0x10c`'s `0x103d74`,
`installer_duart_interrupt`) to the overlay, not to plain RAM.  The
installer's functions are not labeled at their plain addresses: the
primary keeps its own data there.  Run again over an existing overlay,
it types the vectors and marks the calls again, leaving the overlay's
bytes as they are.

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
decompiler's types for them are committed; the commit would give a
function that is not C the default convention, so it keeps the unknown
one Setup gave it.  The decompiler's own list
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
result in registers has no prototype and no wrapper from gcc's
convention: a header of `rewritten/bindings/`, which the exported C
includes after its prototypes, in their place, defines a function under
the routine's own name and prototype that binds each argument to its
register with a local register variable and makes the `jsr` in one asm
statement, naming as outputs or clobbers exactly the registers the
routine changes, so that gcc keeps its own values in the rest across
the call, as the image's callers keep theirs ("Hand-written C",
register-bound calls); the C has a comment naming the header, or saying
that none binds the routine, and then no C can call it.  `softfloat.h`
binds the soft-float library, `mutex.h` the mutexes, and `stack.h`,
`clock_record.h`, `pforth.h`, `sci_token.h` and `scpi_decimal.h` the
rest the C calls; those and `kernel.h`, `drtb.h`, `string_device.h`
and `qspi.h` the 25 that no C calls, the kernel's checks and tick
count, the I/O devices' entries and the like, so that every routine of
the sidecar's `register` convention has a binding, checked whether
called or not: 73 of the primary's and 18 of the installer's.  Six have
none, none of them that convention: `kernel_dispatch_io` and its
installer copy, trap #4's handler, which returns with `rte`;
`psos_pack_name` and its copy, which read their caller's first stack
argument through A6 (`0x16f50`), and which `os.h`'s `OS_PACK_NAME`
calls in line; and `forth_print_string` and `forth_pack_name`, pForth
words' routines that work on the console's user area through A5.
One that takes no argument, gives its result in D0 and keeps gcc's
callee-saved registers itself is declared and called directly.
`enter_supervisor` (`0x13422`) and the installer's copy are such:
called from a task in user mode, they return through trap #7's `rte`
on the supervisor stack, from which a wrapper would restore the
registers it saved on the user stack, and pop its own return.  A
routine whose convention returns in the condition codes
(`returns=ccr`) or lists `results` in registers beside its result's --
the pSOS binding's `psos_call` and `psos_io_call`, `clock_record_sum`,
`pforth_link_word`, the SCPI engine's number routines, and the
installer's copies -- has a comment saying so: the hand-written C calls
each in an asm statement of its own, its results staying in its
caller's registers as in the image (`installer::0x102eda` to
`0x102eee`), where a wrapper could hand them on only in globals, which
another task's kernel call may overwrite between the call and its
caller's reads.  `tools/firmware/bridge_check.py` runs each binding in
Unicorn, through a check function gcc compiles in its own convention,
against its routine called directly, from the registers the sidecar
names, the installer's on the installer's machine (below): all 91 give
the same result, RAM and effects, keep gcc's callee-saved registers,
and keep the 14 values the check function holds across the call, which
gcc puts in every register the statement leaves it, so that a register
the routine changes and the statement does not name loses one.  It
stores them to an array of their own, zeroed before the call, not back
where it loaded them: gcc may keep all 14 in its frame and only its
pointer to the arrays in a register, so that a clobber left out showed
only as a store through that pointer gone astray
(`scpi_decimal_to_text_unused` with A2 left out passed until then).
Its generated cases never complete a `qspi_transfer_raw` transfer: they
name devices the QSPI's table does not take, or wait for an interrupt
none takes, so its given cases (`given_cases`) send three words to devices 2,
7 and 12 and take the QSPI's end of queue after `qspi_start`'s first
read of `qspi.running`, whose handler copies the received words and
clears the flag the routine waits on (`0x20aa6`); a given case must
run, and with D1 or A0 left out of its clobbers it fails.  The
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
- A call to a routine whose convention has `returns=ccr` or lists
  `results` is left as the decompiler reads it: the flags it tests and
  the `extraout_` registers it reads after the call are what it took the
  call to keep, which the routine does not.  Every caller of one is
  written by hand (above), so the reading is only a reading, under
  `#if 0`.  A scan of the image for a call followed by a conditional
  branch found the routines that return flags; those called only from
  assembly (the kernel's checks, the Forth words, the boot code) need
  none.
- A call whose convention has `copies` assigns any variable
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
  `analysis/IMAGE/results.tsv` names the callee's result, `uptime =
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
`third_party/firmware/rewritten/IMAGE/`, apart from the decompiled C in
`decompiled/IMAGE/`, a file to a subsystem (`clock.c`), or, when
several images run it, once in `third_party/firmware/rewritten/common/`
([`rewrite.md`, "Shared C"](rewrite.md#shared-c)): `common/` holds 1011
functions and 232 static helpers (`make check-common`).  The exporter
finds the functions those files define (`rewritten=`), leaves the
decompiler's reading of each out of the exported C, a line
`/* NAME: rewritten in FILE */` in its place, and includes the files
after its own C, so that they see
the types it declares, the image's `smartclock.h`'s, and its globals
and prototypes, and before them the folder's `.h` files, what the files
share (`cpu.h`, the status register's helpers), but `bindings/`'s,
which it includes after its prototypes, in the place of the register
routines' (register-bound calls, below); a function keeps its
generated prototype.  The files are
included in sorted order and compiled as one translation unit, so each
file-scope name takes its file's name as a prefix (`DUART_`,
`duart_`), and one that several files share lives in a header
(`duart.h`'s control characters, which `duartb.c` and `sci.c`
use), as does a body the installer's copy of a function shares with
the primary's.  A file is a module's (`docs/firmware/modules.md`, "The
hand-written files").  The checks run
the hand-written function as they ran the decompiled one, against the
image's own.

That every function is written, by hand or as assembly, is a link's
result, not a count: `make check-link` (`tools/firmware/link_check.py`)
compiles the export's declarations, everything before its first
function, with the hand-written C it includes, the decompiled C left
out whether under `#if 0` or not, assembles the export's assembly and
links the two with the CPU32's libgcc, `ld --require-defined` naming
every function of the export's `.conventions.jsonl`, so that one
missing fails the link, named, whether anything calls it or not.  A
thunk that a `.set` makes another routine's name requires that
routine; a thunk the export carries as assembly counts as assembly.
The link's layout is `rebuild_link.py`'s, each piece at its address,
since its short branches reach only there, and no byte is compared:
the function check shows that what is there agrees, this that nothing
is missing.  Its summary line, each image's functions required and how
each is defined, saved as `target/firmware/IMAGE/link-check.txt`, is
the one source of a statement that an image is complete; `rewrite.md`
quotes each image's, and `check-report-figures` checks them.

A constant is typed: `static const uint32_t SECONDS_PER_MINUTE = 60;`,
inside the function that uses it when only one does, at file scope with
the file's prefix when several do.  A set of coded values, an ADC
channel or a task's state, is an `enum`.  A `#define` names a constant
only where C wants a constant expression, a `case` label, an array's
size or a static initializer, which a `const` object is not before
C23.  A string used in one place is a literal there.

`make check-magic-numbers` holds the C to that
(`tools/firmware/magic_check.py`).  It reads the hand-written C as
clang parses it within the export, its JSON syntax tree for
`--target=m68k-linux-gnu` and `DECOMPILED_CFLAGS`' parsing flags, the
generated SCPI trees and the decompiler's `#if 0` readings left out,
and fails on a literal compared with, assigned to, returned as,
passed as or a case label of something typed with an enum that has an
enumerator of it (or none, a missing enumerator), or of an integer
field or global whose `smartclock.h` comment or `names.tsv` note
names the enum; a hex literal masking or set into a register where
`mc68331.h` or `mc68681.h` names that bit; a literal given to a field
or variable a named constant of its value is given to elsewhere; and
a constant of one name and value defined in two functions or files,
which belongs in the header both see (`cpu.h`'s `CPU_BYTE_BITS`,
`duart.h`'s `DUART_CSR_1200`).  0 for an enum of bits is the empty
set.  A finding that stays, with its reason, is a row of the image's
`magic-allow.tsv`, kept without line numbers; a row no finding
matches fails too.  `make magic-numbers` also prints what it only
reports: a literal passed as a parameter a constant is passed as
elsewhere, whose meaning may differ by call (`pllc_request_b`'s
index), a function's constant of a file-scope constant's value and
name, and a literal given to the same thing in several functions.
A table in ROM that only hand-written code reads is a `static const`
the same way, its values taken from the image and its comment naming
the image's label and address: `fpga.c`'s `fpga_bits` is
`fpga_bit_table` (`0x41310`).  A table the decompiler's functions
read too keeps the image's name and is `const` but not `static`, and
the exporter, finding the definition in a file of the folder, all of
which it includes, declares its extern `const` and gives it no `.set`,
so that they read the C's; a table of pointers is itself `const`,
`T *const name[n]`, as its definition.  So with the generated
SCPI tree, `scpi_tree_data.c`, a file of data alone, every record of
it, and the installer's, `installer/installer_scpi_tree_data.c`
(docs/scpi/README.md, "As C"): the parser's routines, decompiled
and hand-written, the installer's copies too, read the C's tree, not
ROM's or the installer's RAM's (PLAN.md, "The firmware in C").  Not where the
image reads past the
table: `clock_days_before_month`'s unchecked month reads the word
before `days_before_month` and those after it, `exception_name` reads
`exception_names` by an unchecked, unaligned offset, and
`status_negative_conditions` is indexed by any negative condition, so
it keeps the image's, and `status_report_messages` with it; `atoi`
reads `ctype_table` (`0x5ed98`) and the SCPI lexer `scpi_char_classes`
(`0x277ec`) by a sign-extended character, a high-bit one before the
table (`0x5ed0a`, `0x26f02`), so both are the image's; `random_dither` reads the
byte after `dither_table`, which its copy keeps as a 129th entry.  Not
where a table's address outlives the call in RAM, which the harness
compares: the DUART's loopback test leaves pointers into its strings,
and `status_init` keeps `status_conditions`' in the status model.  A
returned string is compared by its text, so `error_message_lookup`'s
table is its own.

A routine that takes its arguments in registers is called in one asm
statement that binds each argument to its register with a local
register variable, `register uint32_t d0 __asm__("d0")`, makes the
`jsr`, and names as outputs or clobbers exactly the registers the
routine changes, read from its code and its callees' (its entry's and
exit's `movem`, what it moves aside and back), so that gcc keeps its
own values in the others across the call, as the image's callers do,
and what the routine returns stays in its caller's registers and
locals, not in globals another task's call could overwrite.  One that
returns only its result is bound under its own name and prototype, a
function of `rewritten/bindings/` (the exporter, "Decompiling with
Ghidra"), so that its callers, decompiled or hand-written, call it as
before: `softfloat.h`'s, a shape macro to each register layout and its
changed registers as clobbers (`float_add` changes D1 and A1 beside its
result, `float_multiply` D1 alone, `double_compare` D1 and A0 and keeps
A1, so `b`'s halves are bound apart), the installer's copies by the
same macros; `mutex.h`'s, which change D7 alone, saving what their
kernel calls change; `clock_record.h`'s, with the record in A6, the
frame pointer, which the statement saves around the call, D6 among the
clobbers.  One that returns in the condition codes or beside its result
reads a flag with `Scc` before anything sets the condition codes again.
`os.h` has the pSOS binding's: `os_psos_call` and `os_psos_io_call`,
and `os_installer_` copies, take the service's registers as an
`os_registers` and give back D0 to D3, A0 and A1, D7, and `failed`, D7
not 0: the stubs branch on Z as set from D7, by the binding's own last
move to it (`bne` at `0x16fda`) or by their move of it to D0
(`0x171ea`), so the C reads D7 as they do, not the flag through
`sne`, whose own data register gcc took from those the stubs leave
alone, D2, D3 or D4, and saved, in 15 of the 16 stubs whose saves
differed from the image's (`psos_create_x` at `0x171d2` saves D2 and
D7 alone; `psos_get_time` still differs, gcc keeping a pointer in A2
where the image uses D3).  Each service has a shape of its own
(`OS_SERVICES`, `OS_IO_FUNCTIONS`): a register is loaded only if the
service reads it and named changed only if its handler changes it, read
from the handler (the table at `0x14b6e`) and its calls -- its results,
and A0 where a check gives the running task's record for an id of 0
(`0x15b68`), D0 where a wait counts ticks down in it (`0x15be2`) -- and
an I/O function's the union of its four devices' routines (the table
at `0x5171a`), so that gcc keeps D2 and D3 across a service that keeps
them and saves them only where it uses them, as the image's stubs do
(`psos_pause` saves D7 alone).  `os_psos_call` switches on its
service, always a constant, which folds to the one call.
`OS_PACK_NAME` calls `psos_pack_name`, which reads the stub's name at
`8(A6)`, in line in the stub.  `clock.c` calls `clock_record_sum`
with the record in A6 too, `console.c` `pforth_link_word` with A4, and
`scpi_convert.c` the engine's number routines, a flag `seq` stores in
memory where the routine changes every data register.  In the image
`psos_ident`, `psos_service_21`, `psos_broadcast_x` and
`scpi_real_to_text` are assembly a C caller calls, their callees' flags
and registers read in place; they stay hand-written C, which says the
same.  The harness's kernel sets each register a service returns in,
and each other register its code changes to a value of its own
(`harness.Service.changes`), so that a register left out of a shape's
changed ones shows where gcc keeps a value in it; it keeps the rest, as
the kernel does: the image's `mutex_acquire` keeps its lock in A1
across ident (`0x10442` to `0x1044a`), and a model that changed them
stopped 32 functions there.  A bug planted in each family fails: A1
left out of `float_add`'s clobbers loses a value the binding check
keeps in it and fails four of its callers in the function check; D7
left out of the mutexes' outputs fails `mutex_lock`, `mutex_unlock`,
`drtb_putc` and `drtb_try_lock_write` by the preserved registers, and
D6 out of the clock record's clobbers fails `clock_init`,
`clock_record_refresh` and `clock_tick` so; attach exchange's D2 taken
as kept fails `psos_service_21`.  An input register taken as kept that
the routine changes cannot fail where nothing reads the variable after
the call, as none of the bindings does.

The hand-written C and the Ghidra type headers are in Google's style
(`.clang-format`), with braces on every control statement's body:
`make format-c` formats them and `make check-format-c` fails on one
that is not.  A signal's `#define` in `psos-signals.h` stays on one
line, which is how the exporter reads it.

`tools/firmware/rebuild_link.py` links it into a rebuilt image: each
piece of assembly at its own address, where it must be, and is, the
image's bytes, and gcc's code -- the C, its constants -- at
`0x600000`, above every chip select the reset code programs, and its
data at `0x6f0000`, with libgcc for the float comparisons the decompiler wrote
as operators.  The libgcc is the CPU32's, with soft float, which
`tools/firmware/cpu32_libgcc.py` builds from GCC's source with the m68k
gcc, as m68k-elf's gcc builds its CPU32 multilib: the toolchain has no
such multilib, and its own libgcc is a 68020's with a 68881, whose
float conversions use the FPU and whose 64-bit division and counts of
leading zeros use `bfffo`, which a CPU32 traps on and Unicorn's 68020
runs.  The source, GCC 15.2.0's files the build reads and no more, is
vendored in `third_party/gcc/` (its `README.md`), so that the checks
need no package beyond the m68k gcc; the build warns when that gcc is
another version.  The build, and `rebuild_link.py`'s link, fail on any
instruction the CPU32 does not have.  A C function the assembly calls is the image's own copy,
`__image_NAME`, so that the original's callers keep the original's
callees and argument layout until a function is replaced; gcc's C calls
gcc's.  `tools/firmware/function_check.py` (`make report-decompiled`)
then calls each C function's image copy and gcc's copy in Unicorn, from
the same RAM, and compares result and RAM; any function failing, in
either image, fails it, and so CI (PLAN.md, "A rebuild that runs, in
readable C").  Each case lists everything that differs, every run of
differing memory and every differing effect, not the first alone.  A
difference whose cause is found and is not the C's is known
(`KNOWN`): the function counts as known while every difference of each
failing case matches the difference's pattern, and fails as any does
otherwise.  A case where both copies stop at what the harness does not
model is skipped only when they stop for the same reason, the same
kind of fault, trap and call, or no return; otherwise it fails.  A
pattern names only what differs: `os_report_process`'s callers print
its stack share from D5, which `os_report_processes` leaves holding the
address of `"%s"` (`0x16768`), so their pattern takes a line alike but
for that column, and its memory column, which D6 starts, is compared.  Both
checks (`tools/firmware/harness.py`) take a pointer left in RAM, at an
even address, as the same when the two point at the same place in the
same text, in ROM or among gcc's constants: gcc keeps its
own copy of a literal the image keeps in ROM.  An empty text is the
same only when one pointer is in ROM and the other among gcc's
constants: gcc keeps `""` as any literal's end, so nothing before it
tells (`loopback_expect_next` and `sci.transmit_next` reset to `""`);
and a pointer to a C
function's image copy and one to gcc's copy are the same (a driver's
echo routine); and so are pointers into the same place of an image
object and its twin among gcc's constants, an object the C defines that
a names file labels in ROM (`third_party/firmware/analysis/IMAGE/names.tsv`,
`scpi-tree.tsv` and `installer-scpi-tree.tsv`, the generated trees'
rows; `harness.Twins`):
a node `scpi_node_find` returns, a list `scpi_header_step` leaves in
the path.  So too in a call's pointer result, the memory beside RAM
compared after it, and its effects, a kernel call's argument or a
routine's of the harness's recorded as the image's copy would pass it.
Where the harness gives a call one of the image's objects that has a
twin, the main tree's root list or a node, as below, gcc's copy is
given the twin, in its arguments and in the memory it starts with, so
that it reads the C's tree as the firmware's C does.  Each profile
gives the parser's objects of its own tree (`harness.ScpiObjects`):
the installer's functions get its console port, its tree's lists and
keywords, so that they read the installer's generated tree.  And a call starts with
A6 at a caller's frame on the harness's stack, its saved frame pointer
0, its return address the harness's and its first argument a name,
`"gpsx"` as `root_task` passes it, since the image's code may walk its
caller's frame (`float_raise` reads the return address there).
gcc keeps a frame pointer too (`-fno-omit-frame-pointer`), as the
original compiler did: `psos_pack_name` reads its caller's first
argument, the task or exchange creator's name, through the caller's A6;
and it addresses its locals through it, which stay where they are when
a task's function enters supervisor mode and A7 becomes the supervisor
stack.  And it pops each call's arguments right after the call
(`-fno-defer-pop`), where by default it may leave them and pop several
calls' at once, so that it never pops from one stack what it pushed
on the other, across `enter_supervisor` or a move back of the status
register.  The stack is filled with one byte
at a call's start, and each frame a `link` makes is filled with it
again, so that a local read before it is written, a message's bytes
its sender leaves unset among them, reads the same in both copies,
not what an earlier callee left where each copy's frames happen to
lie.  The EEPROM, chip select 9's 8 KB at
`0x400000`, is erased for each call and compared after it as RAM is;
gcc's code is above every chip select so that a read of the EEPROM
finds no code, and its data, libgcc's `_fpCCR` among it, apart from
its code, which the function check keeps
read-only.  The CPU32's libgcc is not PIC, so the link has no GOT, which
the toolchain's PIC float routines reached `_fpCCR` through.

gcc does not keep a word or long of its own at an even address
everywhere, `-mstrict-align` notwithstanding: it stores a packed
structure of four bytes, or fills a local text, by longs wherever the
object lies, and accesses a packed table's members by their offsets
from wherever the table does.  So an object gcc would put at an odd
address, where the image's is at an even one, is
`__attribute__((aligned(2)))`: the 58503A's `hmon_channels`, the unit
multipliers, the OS reports' local texts, `timecode_update`'s next
time of day, `reset_record_write`'s record and `gpsm_task`'s two
request entries.  A run on a model of the
CPU32 that takes an address error for a misaligned word or long, as
the CPU32 does, found those before the last; `rebuild_link.py` finds
an access at an odd offset from the frame or stack pointer in gcc's
code, as linked and compiled again with no function inlined
(`-fno-inline`), since where a frame's objects lie changes with what
is inlined into it, and fails on one; a switch's table of offsets,
which objdump reads as code, is left out, and the code after it read
again from the table's end.  `reset_record_write`'s record
lay at an odd offset beside its `full` flag only where `log_count` was
not inlined, as where a link calls the image's copy of it.

What lies outside memory the harness models, the same for both calls,
at its boundary rather than routine by routine.  `trap #7`, and
`trap #1`, which saves or restores a status register for `setjmp` and
`longjmp`, take the exception through the primary's vector table to
the image's own handler, on the supervisor stack, which setting the
supervisor bit makes A7 from a task's user stack, and `rte` returns from
it, which Unicorn leaves to its interrupt hook; and `rtr`, which ends
pForth's code words (`forth_compile_code`, `0x19338`) and which Unicorn
takes for an illegal instruction, pops the condition codes and the
return address as the processor does (MC68331UM, table 5-1).  Nor
does the harness read the status register through Unicorn: its m68k
read (`qemu/target/m68k/unicorn.c`) marks QEMU's lazy condition codes
as computed before it computes them, so that it returns the operands
of the last compare read as flags and, from a trap's hook or after a
run stops, leaves the CPU to go on with them; and a write of PC from
a code hook loses them too.  The harness reads the system byte from a
copy of the CPU's state and the condition codes with a probe of its
own, `move.w %ccr` to memory (`harness.PROBE`): a trap's hook sends
the CPU through it before it takes the exception or answers the
kernel, a call's status register after it is read through it, and an
interrupt is taken between two runs, the first stopped before the
instruction the interrupt comes before.  Before, the `returns=ccr`
compare would have read `1 cmp 2` and `2 cmp 1` both as `00`, and
kernel service 33 or an interrupt between a compare and its branch
changed the branch; no figure changed with the fix.
`tools/firmware/unicorn_check.py` (`make check-unicorn`, which `make
check-decompiled` runs first, and so CI) keeps both bugs in view, by
what Unicorn does rather than by its version, which a `dev` build
reports as 2.1.4 too: on a compare of 2 with 2 and of 1 with 2, each
then recorded by `scs` and `seq`, each case says whether Unicorn shows
its bug or the fix, and fails where that is not what the workarounds
expect, today both bugs present, printing what to do: for the read
fixed, drop the probe and keep the stopped run.  Bug 1's cases are the
read after a stop and in a trap's hook; bug 2's, all from a code hook
part way through a block after the compare, a write of PC, a read of
SR, which returns stale codes, a write of SR, which the rest of the
block reads as the compare's operands, and a write of SR on a load
that then faults, which the fault undoes.  It also checks that the
harness's `system_status`, probe and stopped run, and a write of SR
with PC from a code hook as `move (SP)+,SR` does, give the right
codes.  Nothing here reads SR's codes or writes SR without PC from a
code hook.  Unicorn's `dev` branch already drops the read's
assignment (`5d5da58`, "Don't clobber condition codes when reading
SR", #2420), which no release has yet, and keeps bug 2.  The kernel's traps, `#3` and `#4`, are
answered: for each service and I/O function (`psos.md`) the harness
knows, from the binding's stubs, the registers it reads and returns
in; a call records the service and those registers, a message by the
20 bytes from its `+4` the kernel copies and a write's buffer by its
contents, since a caller's buffer on its stack lies where its own
frame puts it, and returns no error and `0x6b000000`, an address
nothing is mapped at, in each result register, and `0x6a000000` in
each other register its code changes (`os.h`, the services' shapes:
A0 for a service that checks an id, D0 for one that may wait, D0, D1,
A0 and A1 for a device's read, write and control), keeping the
others; `request_x` fills its
buffer with that value, a read reads nothing and a write writes all it
is given.  A write to the QSPI (device 1) is recorded by the transfers
`qspi_queue_setup` (`0x20578`) queues from its buffer for the minor
device in D0's low byte, each at its width: a word of the buffer is a
transmit RAM entry, sent right-justified (MC68331UM 6.3.2.2), its low
bits as many as the transfer takes, 8, or SPCR0's BITS, 8 to 16, where
its command byte sets BITSE (6.3.5.1, Table 6-3; D.4.10, D.4.16), and a
transfer is recorded as those bits in the fewest bytes that hold them
(`harness.qspi_write_data`), so that bits it does not send are not
compared: three 12-bit transfers for the hex DAC and device 0, one
8-bit for device 11, two 16-bit for devices 5 and 9, one 16-bit for the
others, D2's count of 16-bit ones, up to 16, for the FPGA's block, and
one of D2's count of bits, 8 to 16, for its readback (`structures.md`,
"The QSPI's devices").  With the hex DAC's words recorded as 16 bits,
a planted 0xf000 in an unsent nibble failed `hdac_write_all`; at 12 it
passes, and a planted 0x0800, a sent bit, fails.  But a case starts with an id, another such address, in
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
modules from `0xfff000`, the QSPI's RAM and the FPGA's ports behind it
among them -- are memory zeroed for each call, and each read and each
write of them is recorded; but the registers the polled routines wait
on read as the data sheets say the device shows them, by what it has
done (`harness.Status`): the DUART's SRA and SRB with TxRDY and TxEMT,
the SCI's SCSR with TDRE and TC, the QSPI's SPSR with SPIF and its
SPCR1 with SPE clear (MC68681; MC68331UM D.4.6, D.4.13, 6.3.5.1), each
after the case's busy polls of it since the call's start or the write
that starts the device again (TB, SCDR, SPCR1), and SRA, SRB and SCSR
with RxRDY or RDRF for the case's first reads, a framing error beside
it in some cases; and the DUART's input port reads a case's sequence,
one that bounces before it settles among them, which
`duart_input_debounced` reads until ten reads agree.  The EEPROM's
write cycle is modeled too, its part not identified: after a write,
the case's busy reads of the cell give its complement and the next
its value, or, in a failing case, every read the complement; its
writes are effects, and its reads from the call's first write on,
where the routines poll it.  The cases' devices
(`harness.DEVICE_CASES`), by the case, are ready at once with nothing
waiting in the first, as before any was modeled, then busy for one to
five polls, with characters waiting, and failing.  So a poll's count
and order, `qspi_send`'s reads of SPSR, the debounce's reads and the
EEPROM's polls, are compared, not only what the routine leaves.  The
kernel calls and device reads and writes, in order, are a call's
effects, which the two calls must make alike; a
service the harness does not model stops a call, as an unmapped
address does.  A trap whose handler never comes back -- `#5`, the
kernel's return from interrupt, but from an interrupt the harness has
the case take, below, and `#11` and `#12`, which restart --
ends a call, as does a zero divide, which the common exception entry,
with no task's handler set, records before it spins (`restart.md`,
"Both end in the watchdog"), and a write to code.  On the board the
boot chip select is read only (CSORBT `0x6870`), but chip select 6
is write only over the same range, and in the Z3805A chip select 7
over the upper bank's, which CS1 reads (`restart.md`, "The chip
selects"), so a stray write by the primary reaches the flash's command decoder rather than raising
a bus error; with no unlock cycle before it the flash ignores it.  The
harness ends it as a bus error, which keeps a stray pointer from
passing unseen.  The installer's
writes to ROM's range are to the flash, as chip selects 6 and 7,
write only over the two banks' ranges, make them on the board,
and the flash is modeled (`harness.Flash`): its two pairs of
AM29F010s, a pair's parts a word's two bytes, each a command state
machine as AMD's Am29F010B data sheet gives it
(`third_party/amd/Am29F010B.pdf`;
`restart.md`, "Erase and download"): the unlock cycles, program,
sector erase with more sectors in its window, chip erase and
read/reset, and while a program or erase runs, its status, DQ7's
data polling, DQ6 toggling, DQ3 for an erase, done after the case's
busy reads, or timed out, DQ5, in a failing case, until a reset.  So
`installer_flash_program_word`, `:DIAGnostic:ERASe` and
`:DIAGnostic:DOWNload` run to their end, the flash's contents compared
after a call as RAM is and its writes, and its reads from the call's
first write on, effects; the primary's ROM stays read only.  A write
the parts ignore, to a part reading its array that starts no command,
a stray pointer's, ends the call there, as one to the primary's ROM
does.  A case may take
an interrupt part way (`harness.Interrupt`), in both copies at the
same point of each one's own instructions: after the k'th read or
write of a variable by the routine itself, not by a handler, and every
so many after, before the next instruction once the mask lets its
level in, as the CPU32 takes one (MC68331UM 5.8.2): the status
register copied, supervisor mode, trace off and the mask at the
level, a four-word frame of format 0 on the supervisor stack, and the
handler from the vector table at the profile's vector base, the
device's own vector; and the kernel's return from interrupt, `trap
#5`, which the firmware's handlers end with, returns from it as `rte`
does, as the kernel does when the interrupt readied no task to switch
to.  The interrupt may present registers to its handler and make a
status register ready.  The QSPI's end of queue (vector 65, level 4,
as `qsm_init` sets QILR and QIVR) is taken after the first, second or
third read of `qspi.running` in `qspi_start`'s wait, its handler
`primary_user_65` clearing it through `qspi_isr`; the DUART's (vector
67, `duart_ivr_test`'s IVR, its level, not set by software, taken as
2, the mask the firmware raises around its DUART accesses) after each
read of `loopback_expect_next` in the loopback test's wait, ISR
showing TxRDY and then RxRDY as well, TB and RB one register of the
harness's memory, as local loopback has them, until the text is all
back; and the periodic interrupt (vector 66, level 5) after
`interval_wait_reading` sets `interval_tick_wait`, a tick short of the
one that signals its wait.  Without the `volatile` in `qspi_start`'s
wait or the loopback's, gcc reads the variable once and spins, and
the case fails, as does storing `interval_tick_wait` after the wait.
A case may also lay its arguments at the stack's edge, the caller's
frame unmapped, so that a read past those the image reads faults:
`float_raise`'s, each with only the operands its cause reads.  A case
may start registers its function reads as its caller left them
(`cpu.h`'s `CPU_CALLERS_REGISTER`) at values it can use, so that one
read for another shows, and give an SCPI handler's call record another
input port than the console's: `:DIAGnostic:QUERy:RESPonse?` from
another port copies the count at A3 of the text at A4 to A2, each a
buffer of its own, and `os_report_process` and its callers start D5 and
D6 a kilobyte and more apart.  With `CPU_CALLERS_REGISTER`'s asm
statement not `volatile`, as before `90b768e`, gcc reads one register
for two or three and all four fail; they pass with it.

Each case also measures the stack: a write on a stack is its own when
it lies at or above the stack pointer less 64 bytes, the most one
instruction pushes (`movem.l` of sixteen registers), so that a pForth
push on the data stack through A6 is not, and each copy's depth, from
where its call started the stack, so that gcc's 4-byte argument slots
do not count, is taken on its own stack and on the supervisor stack a
task's exceptions take.  A case differs when gcc's goes more than 192
bytes deeper than the image's, the next 64 bytes above the deepest it
goes now (`float_raise`'s soft-float frames, 176), or, on a stack with
a floor, memory of another use below it, more than 16 bytes deeper or
past a floor the image's stays above.  A console session's floor is the
pForth data stack's base, `0x70fbc4`, below its 1 KB return stack: the
image's `mem_rep` leaves 58 bytes of it, gcc's 56, and with
`libc_doprnt` called, not in line, 4 (and none under gcc 13), which
fails; a planted 256-byte frame fails anywhere.  The report counts
the cases deeper than the image's and lists the ten functions that go
furthest beyond it, each by its deepest case, and the room each console
session leaves above its floor.  A
branch to itself, `fatal_error`'s spin, ends a call halted, since no
interrupt comes but those a case takes;
and a call still running at its 1000th kernel call, a task's loop,
ends there, before making it.  A loop that makes no kernel call ends
where it counts to a count of the function's (`COUNTED_LOOPS`):
`root_task`'s idle loop at `idle_loops`' 101st write, odd, so that a
loop counting by more passes it.  Once the loop has read its counter, a
write of it by any instruction but the one that read it ends the call
too, as a split increment: the image's is one instruction, `addq.l #1`
to memory (`0x131dc`), which no interrupt divides, and a load, an add
and a store would lose a clear made between them, `klok_take_pending`'s
from the klok task at each 1PPS.  The C increments with `cpu.h`'s
`cpu_long_increment`, the same instruction.  Two calls that end the same way are
compared as if they had returned, but for their result, and both
leave the status register's system byte, its supervisor bit and
interrupt mask, the same.  Two that return leave its condition codes
the same too where the function's sidecar row returns in them
(`returns=ccr`), and nowhere else: no function the harness runs has
one now, the routines that do being register routines, not C
(`condition codes compared` in the report).  Two calls that return leave alike the
registers the function's convention keeps for its caller
(`harness.preserved_registers`): a C function's, the m68k C
convention's D2 to D7 and A2 to A6, and those its sidecar row's
`preserves` adds, `float_raise`'s A0 and A1, which its `movem` saves
(`0x5e940`) for the soft-float routines that call it; another
convention's only those its `preserves` names, or every one for
`all`.  A function that runs in a task starts as a task does, in user mode with its interrupts unmasked
(`TASK_STATUS`), its exceptions taken on a supervisor stack of the
harness's, so that its `trap #7` and move back of its status register
show (`root_task`'s, `0x13082` to `0x13098`).  Every task starts so:
its status register is its record's `+0x44`, which `kernel_create`
clears (`0x14e5c`) and the kernel's start, service 2, does not set,
since it takes no mode (`kernel_start`, `0x14ea0`, reads the task and
its entry); only IDLE's is set, to supervisor mode (`0x15eb4`).  Which
functions those are the export's direct calls give (`task_context`):
those reached from a task's entry -- the configuration's `root_entry`,
`root_task`, each entry `psos_start` is given, the two the pForth code
starts with service 2 in line, `console_task` (`0x1aedc`, from
`console_task_create`'s trap at `0x1aec6`) and `forth_spawned_task`
(`0x19f42`, from `spawn`'s at `0x1a012`), and each SCPI handler, which
the engine calls through the tree in the SCPI task.  One that nothing
else reaches runs every case in user mode; one that code in supervisor
mode may reach too runs every other case in user mode and the rest in
supervisor mode: a function that nothing calls, an interrupt handler,
the reset, a console word or a routine reached only through a table, a
task that opens with service 33 and runs on in supervisor mode,
`klok_task` (`0x10b1e`) and `spoo_task` (`0x20e5e`) and the
installer's `spoo_task`, and a function `spoo_run` (`0x20d66`) is
given, which it calls in its caller's mode before the spool task
starts and sends to the spool task after (`console_init`, from
`root_task` and spooled from `scpi_task`).  A function written by
hand is reached through its decompiled reading's calls, which the
sidecar lists ("calls", "passed"), not through the hand's.
Of the functions run, 606 of the primary's and 55 of
the installer's run in user mode alone, and 224 and 23 in both, figures the report prints and `make check-report-figures` checks against the last one it saved.  The image refers to a handler only from its node's setter
or query slot, 499 slots in 459 nodes, which only `scpi_call_handler`
(`0x29642`) calls through, from `scpi_interpret`, from `scpi_task`: no
console word, spooled request or timer reaches one.  A case of one whose copies both stop at a
privilege violation is run again in supervisor mode, as a caller in
supervisor mode would run it; none does.  Two of the processor's ways
the harness keeps for such a task: kernel service 33, which
`klok_task` and `spoo_task` open with, returns in supervisor mode, the
old status register in D0's low word (`kernel_supervisor`,
`0x158f6`); and `move (SP)+,SR`, after which Unicorn's QEMU
increments A7, so that a move back to user mode increments the user
stack in place of the supervisor's (`set_gps_utc_offset`, `0x139c6`),
is done in its place.  A kernel call's argument, or a message's long, that is
a C function's gcc copy is recorded as its image copy, the same
routine: a task's entry for `psos_start`, `scpi_task_create` in
`scpi_task_start`'s message.  ident finds every name, but in the
cases `IDENT_FAILS` gives a function that asks before it creates:
there it finds none but the running task's, error 1 and no record
(`kernel_ident`, `0x14eda` to `0x14eec`), so that `root_task` goes on
past `spoo_task_create` to create and start every task.  And the stack is filled with `0xa5` at a call's start,
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
object (`0x29e18`), and a header `path`, a buffer set back to a list
of the tree, as `scpi_path_reset` does, and a `scpi_child_list *`
that list; and a header's `keyword`, a `scpi_keyword *`, one looked
up in it: the list and the keyword drawn from the profile's tree as
the image holds it, every child list from the root down
(`harness.tree_lists`), by the function's name and the case
(`harness.header_target`), the keyword one of the list's nodes', short
or long, one of the list its default child holds, which a lookup
reaches through the default, or of none, so that a lookup finds a
node, directly or through the default, or none, below the root as
much as at it, and a generated tree that differs from ROM's shows (the
header resolvers' query, which indexes a node's two handlers, is in
range, 0 or 1).  `scpi_header_step`, which steps into a node's
children, and the installer's copy run a case more for every node of
their tree that has children, its list and its keyword
(`harness.tree_steps`, `STEPPERS`), so that each node's child list
pointer is read: 147 in the primary's tree and 2 in the installer's.
A parameter's token, a `scpi_keyword *` named otherwise, is by the
case a keyword of the main tree's root, short or long, of `:SOURce`,
or of neither.  A pointer into an object with a twin is the same only
as one to its twin's place, never as a literal, so that a list or node
of the C's that is not the twin of the image's differs, where both
start with a NUL, an empty text.  And by its
type, `status_object`'s model, one of its registers by the case, its
queue for a `status_error_queue *`, `aging_curve` for an
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
After the path's words, a parameter the node's handler entry takes
from a choice list holds its kind word, its converter's index, and an
index into the list's keywords in range, by the case, as
`scpi_parse_choice` (`0x26460`) gives it, `:DIAGnostic:TEST?`'s test
among them; and the installer's `:DIAGnostic:TEST?` one of its five
tests (`HANDLER_VALUES`).  The installer's handlers get such a record
too, from its tree, its console port and its status model.
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
with the dictionary pointer null a definition returns at once.  A
function's states (`STATES`) are what its zeroed RAM never reaches:
variables each case starts with at a value drawn for it, from a
generator seeded by the function and the case -- an lvalue of the C,
`pll.fine_state`, whose address and size gcc finds, compiled into the
link as probes -- and routines both copies' calls of which the harness
answers in their place, leaving variables at drawn values and each
call's arguments recorded as an effect: `fine` and `enter_holdover`
start from the stage's states, the fit's sums and the counter's
reading, `counter_read` gives a drawn reading and `log_event` records
its code, over 2000 cases of their own each; `root_task` draws
`pforth_at_startup`, the SCPI task or the pForth console.  And the
stack report (`task_stack_report`, its SCPI handler and `s_rep_word`)
starts with the running task's stacks, in turn both, part used
against the `0xabcd` fill, a user stack alone in a segment, and none,
the kernel's own supervisor stack part used or all zero; with a user
stack alone, each frame a `link` makes is filled with the segment's
address in place of `0xa5`, so that `task_stack_info`'s two unset
locals hold it wherever each copy puts them (`quirks.md`).  After
its eight cases a function may run cases of its own (`GIVEN_CASES`),
where the generated arguments never reach what its callers pass or
what its body treats apart: `pforth_define_word`'s argument and result
counts from `diag_words_define`'s table (`hdac_write` takes 2, which
found its loop's count read back from a register nothing set),
`bytes_equal_n`'s two strings of one length, `double_to_digits`'
digit count and mode as `doprnt` passes them, and a subnormal, an
infinity and a NaN for `frexp`, `modf`, `sqrt`, `log` and `fabs`.
And, each found by a bug planted in the hand-written C that the
generated cases let pass: the suffix matchers', primary's and
installer's, units from the generated tree's tables in ROM and
suffixes of each length of multiplier, known and not; the number
converters' numbers about each type's limits, past the tenths each
tests and past what its arithmetic wraps to; the parameter parsers'
numbers with a suffix, with and without units; `scpi_parse_text`'s
tokens for the installer's copy too; a status model of two registers
below the top for `status_clear_events`, and an error queue in each
state, its tail wrapping, for `error_queue_pop`; `:SYSTem:LANGuage`'s
texts in each case, `:DIAGnostic:TEST?`'s every choice and one past,
the installer's with the boot code's results set; `longjmp`'s buffer
and values, the status register it restores from the buffer, in every
case, its trace bits cleared (`JUMP_FUNCTIONS`), as `setjmp`, untraced,
would leave them, so that the CPU32 does not trace on from it;
`atexit`'s NULL and full table, `sign_extend_bits`' every kind of
width, and `console_port_read`'s read errors; each GPS message
through `gps_request_build`'s encoders; the line readers' echo as a
routine reading where the image's do, off and 0; `atoi`'s high-bit
characters; and SCPI lines with high-bit characters, before and in
a keyword and after a number.  `setjmp`
leaves D1, A0 and A1 as it found them too (`KEPT_BESIDE`), and the
kernel's priority service returns an old priority of `0x40`, so that
`task_set_priority`'s step from it shows.
A variadic function runs too, its list after its declared arguments,
each a 4-byte slot, laid out alike for both copies as its callers push
it (`VARIADIC_CASES`): the printf family is given `vsprintf`'s
formats with their arguments, those whose output 80 bytes holds for
the routines whose buffer is 80, and its output is compared where it
goes, a kernel write's text, the polled DUART's or SCI's writes, or
the buffer; `log_write` an entry's format and its two longs, with the
log's cursor and lock set up; `float_raise` each cause's code, float
and double, arithmetic and math library, with its operands, NaNs quiet
and not, and underflows' shift counts and mantissas that round, with
`float_control`'s enables drawn.  And the parser's loop,
`scpi_interpret` and the installer's copy, runs SCPI sessions
(`SCPI_SESSIONS`): the console port as its input and output, an arena,
the trees, and a line of messages in the console's line buffer, which
the port reads before an empty read of the console, so that the input
ends; most end the session with `:SYSTem:LANGuage`, whose handler
returns, `"PFORTH"` for the primary and `"PRIMARY"` for the installer
(`installer::0x107f92`), and one runs to the kernel call limit.  Each
session has lines of its own, the primary's `"PRIMARY"`, which does
nothing, a query whose handler fails, `:LED:ALARm:MINor?`, and what
follows it, and `:DIAGnostic:TEST?`, the installer's `"PFORTH"`, an
execution error, and its `:DIAGnostic:TEST?` in range and out; and
lines from its tree, a query of a node of every child list below the
root, one with a query handler where the list has one, each by the
shortest header that reaches it, short forms and every third long,
each followed by `:SYSTem:ERRor?`, each a message of its own, so that
a header the C's tree resolves otherwise shows in what the session
writes (`tree_lines`).  The installer's tree pair is the image's, the
C's twin, gcc's copy given the C's.
And `console_init` runs pForth console sessions (`CONSOLE_SESSIONS`,
`CONSOLE_LINES`), the real interpreter on lines typed at it: each copy
runs its own `console_init`, which builds the dictionary with its own
`pforth_define_word` and `diag_words_define` and creates the console's
task, then its own copy of that task, `console_task`, entered as the
kernel starts it, its stack the words the create call gave it, in user
mode.  The kernel words are the image's assembly in both, and the
diagnostic words each copy's own C, through the wrappers its
`pforth_define_word` compiled.  The harness models the two devices the
console reads as their drivers read (`harness.READ`): the string
device gives the phrase `console_init` set, `0 !iodev`, and the
console's port, DUART channel B, then gives the session's lines, one a
read, as `drtb_read` gives a line; the session ends when the lines run
out or the task deletes itself, as `halt`'s hook has it do.  The output
is the trap #4 writes, compared in order as any kernel call is, and the
port's echo of each line, as `drtb_gets` echoes it through
`duartb.echo` (`0x10313c`): its characters, then CR and LF where the
line ends within the read's length, nothing when the echo is
`duartb_echo_none` (`0x1d570`), the settings' echo off (a session
starts with it unset, so its lines are echoed); each read records the
echo, the dictionary pointer, the data stack and the user area
(`harness.ConsoleLine`), compared as effects; and the dictionary, in
RAM, is compared as RAM.  The report counts the sessions on a line of
their own: of the 8 console sessions, 8 agree (`make
check-report-figures` checks the figures).  The sessions: arithmetic and printing in
decimal and hex, the stack left, a definition used, an unknown word
part way along a line and a word on the empty stack after it, hex
numbers, a negative one, the diagnostic words `adc_read` (1 -- 1),
`hdac_all` (6 -- 0), `hdac_write` (2 -- 0), `dmes_all` (1 -- 0),
`pr_efc` and `mem_rep` (0 -- 0) and `wr_eeprom` (2 -- 1), `words`, and
`halt` with a line after it, never read.  No word that would send to the
GPS engine, reset the unit or halt it is run.
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
and its error queue a parameter is given by type as the primary's
are.  The root task creates no exchange
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
from `aging_curve` to the kernel's exchanges, are not given; an SCPI
handler's record is, from the installer's tree.

Each image has its two profiles, chosen by the image's name
(`harness.IMAGE_PROFILES`, `harness.image_profiles`), the addresses
read from that image's own code: the Z3805A 3543B's above, and the
58503A 3704's (`58503a-3704.md`, "The function check").  The installer's
records are found in the image wherever they lie (`image_diff.py`'s
`installer_records`), not at the Z3805A's `0xbbd`.  The tables of
`function_check.py` and `bridge_check.py` were written for the Z3805A,
its `BASE_IMAGE`: another image runs every case with them, its
profile's objects given in place of the Z3805A's, but for what its C
does not name -- a Preset, or a field one names, an interrupt's watch,
a hook's routine, a region, a session's buffers, left out -- and for
`KNOWN` and
`SET_UP_REQUESTS`, which name the Z3805A's ROM and are its alone; a
given case's other ROM addresses are still the Z3805A's, given to both
copies alike.  Another image's known differences are its own, each
found in its code (`IMAGE_KNOWN`): the 58503A 3704's are the process
report's D5 share, as the Z3805A's, and its SCPI session's, which
reaches `:DIAG:OS:PROC?` and whose memory column also starts from the
D6 each copy of `scpi_interpret` leaves.

In the Z3805A 3543B image this gives 2041 functions, 1700 the
primary's and 341 the installer's, and every one but the 12 thunks
decompiles.  137 switches are recovered; `0x24ff2` and `0x25140`, a
`jmp (d8,PC,D6.w)` with no table load, are not, and need not be: each
enters an unrolled run of shifts part way, all of it code already
(`docs/scpi/README.md`).  Every function is named.  The output
is `third_party/firmware/decompiled/z3805a-3543b/z3805a-3543b.c`.

The project is kept as `third_party/firmware/ghidra/smartclock.gar`,
a Ghidra archive in Git LFS (install `git-lfs` to fetch it): restore it (File, Restore Project, or ghidra-mcp's
`restore_project`) to work on it, and archive it over that file after
work worth keeping.  The archive is made from a bare import done in a
neutral directory (`/tmp/smartclock/`), since a program records the path
it was imported from; `tools/ghidra/ArchiveProject.java` archives a
project without the GUI, as File, Archive Current Project does, run as
a pre-script of any headless run with the archive to write, the
projects directory and the project's name.  Run over the bare image, imported raw at zero
and analyzed, or over the archive, the scripts reproduce the
checked-in export, sidecar and `.symbols.ld` byte for byte; from the
repository's root, with `GHIDRA` the install and `PROJECT` a new
directory outside the repository:

    "$GHIDRA"/support/analyzeHeadless "$PROJECT" smartclock \
      -import third_party/firmware/images/z3805a-3543b.bin \
      -loader BinaryLoader -loader-baseAddr 0 -processor 68000:BE:32:CPU32 \
      -scriptPath tools/ghidra \
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

[`rewrite.md`, "Reproducing"](rewrite.md#reproducing) gives the
archive's form too.  An exception in a script does not roll
back what it did, so save the program before each script that changes
it.

## The checks in CI

`.github/workflows/firmware.yml` runs the firmware's checks for the
three images on every push and pull request, a job of its own beside
the Rust CI so that each fails alone: `make check-format-c`,
`check-types`, `check-module-figures`, `check-scpi-tree`,
`check-pforth-words`, `check-image-diff`, `check-chip-selects`,
`check-loop-trace`, `check-magic-numbers`, `check-comment-addresses`,
`check-common`, `check-decompiled`, which runs `check-unicorn` first,
`check-link`, which fails if an image does not link from its
hand-written C and assembly alone, every function required ("Hand-written
C"), and `report-decompiled`, which fails if any function fails and checks
the figures this document, `rewrite.md`, `58503a-3704.md`,
`z3801a-3543.md` and the top `README.md` give from each image's
function check -- the user-mode and console counts, the agreeing,
known, skipped and failed functions and the agreeing ones' cases --
and from its link check against the report it writes and the link
check's summary, so that every function agreeing is
checked in public.  `IMAGE=z3805a-3543b` is set for the job;
`check-types`, `check-scpi-tree`, `check-magic-numbers` and
`check-decompiled` run again with `IMAGE=58503a-3704` and
`IMAGE=z3801a-3543`, and `check-link` and `report-decompiled` run
without `IMAGE`, for the three, each failing if any of its functions fails (`58503a-3704.md`
and `z3801a-3543.md`, "The function check").  `check-image-diff`
checks the 58503A's and the Z3801A's diffs against the Z3805A, their
seeded names and the 58503A's document's figures;
`check-comment-addresses` checks the addresses their C's comments
cite; and `check-common` the shared C, `common/`, and that the
seeded images' `check-layout.c` assert their layouts at their own
addresses, which its rule 2 compares, and the figures of what
`common/` holds that the documents quote (`rewrite.md`, "Shared C").  None of the targets builds the workspace;
`check-chip-selects` reads every image's reset code (`restart.md`,
"The chip selects").
`check-loop-trace` runs both images' own `startup_pll` and
`pll_normal` in Unicorn on synthetic readings, the routines that reach
outside the loop's arithmetic stood in for
(`tools/firmware/loop_trace.py`), and fails if the traces
`crates/smartclock/tests/loop_firmware.rs` replays against `loop.md`'s
update law are not what the images give.

On an `ubuntu-26.04` runner it installs `gcc-m68k-linux-gnu` (gcc 15),
`binutils-m68k-linux-gnu` and `clang-21`, which `check-magic-numbers`
parses the C with, from the archive and uv 0.13.2 through
`astral-sh/setup-uv`, whose cache holds Unicorn 2.1.4 and Capstone
5.0.9, each tool's inline dependencies, pinned in the scripts, keyed
on `tools/firmware/*.py`.  26.04,
not 24.04, for gcc 15, the one the C is developed with; 24.04's gcc 13
gives the same figures.  It did not until the C stopped depending on
what gcc happened to do: the condition codes before an asm statement
(`quirks.md`, `set_interrupt_mask`), a local never set
(`fit_quality`), and the stack a pForth word's printing takes, which
reached the console's data stack (`libc.h`'s `libc_doprnt`).  The
function check measures the C as a given gcc compiles it, so a gcc
that changes its code can change the figures.  clang-format is
`clang-format-21`, named through `CLANG_FORMAT`: 19, 20 and 21 format
the C alike, and 18 formats three of the files otherwise.  The job
takes about ten minutes, five of them `report-decompiled`'s.
