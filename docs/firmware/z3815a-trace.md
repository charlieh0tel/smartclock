# The Z3815A FPGA

Traced against the Z3805A's design (`z3805a-trace.md`), the reference.
The Z3816A's (`z3816a-trace.md`) shares its count chain, the core of
its 1 PPS divider and its P7 divider's counter, under the same tile
names; it differs in the P7 divider's rearm (GE here, KD there), the
start path (BG, BH, CH), the seconds counter, LK
(P51 and P57, which only here XOR P50), EG (register 2's start select,
only here), FI and HJ (the P84 counter and its read multiplexer, only
here) and the P35 counter (only here).  `python3 -I tools/fpga/tilediff.py
target/fpga/z3815a.json target/fpga/z3816a.json` lists the blocks
configured otherwise, which are not all different in function: CI,
the stop capture (BH.Y through its DI here, its table there), FB, DK,
FG and BF differ only in how the same function is encoded.

## At a glance

| | |
| --- | --- |
| Images that load it | Z3815A 4010 |
| Bitstream | `third_party/firmware/images/fpga/z3815a.bit` |
| Part | XC3042, PLCC84 |
| Logic blocks, I/O blocks, nets | 142 of 144, 56, 256 (`tools/fpga/ref/z3815a.stats`) |
| Clocks | ACLK from P53; GCLK, ACLK halved; P7, P84 and P35 through general routing; LB.Y and local clocks in the count, start and stop paths |
| Oscillator the divides imply | P53 19.6608 MHz (GCLK 9.8304 MHz), locked to 10 MHz on P7 (**inferred**) |
| 1 PPS output pins | P83; P20 (a one-ACLK pulse); P14, P28 (active Low, alternate seconds) |
| Other outputs | P38 and P40 (the phase detector); every output pin is listed in "Other outputs" |
| Differences from the reference | three clocks; a 9,830,400 divide through an ACLK prescaler; eight written registers, register 2 its own; a start-source select; the PLL phase detector |

The count is the Z3805A's kind, a ripple Johnson chain whose preset
reads `0x555555`, started by the GPS 1 PPS and stopped by a divider
tap; the divider runs to 9,830,400, one second of a 9.8304 MHz clock,
and its tap, `0x3c0000`, is 0.4 of it, the firmware's 0.3932162 s
calibration.  A second input, P7, clocks a divide by 6250 whose
terminal and the 1 PPS divider's own GCLK / 6144 tick meet in a
two-flip-flop phase-frequency detector out on P38 and P40: by its
ratios the comparison of a PLL that locks a 19.6608 MHz oscillator (the
CDMA clock, 16 x 1.2288 Mcps) to the 10 MHz reference at 1.6 kHz
(10 MHz / 6250 = 19.6608 MHz / 12288; **inferred**).  The port reads
nothing of it.

- 142 of the XC3042's 144 logic blocks (98 as two functions of four
  inputs, 44 as one of five); 56 I/O blocks, all bonded; 256 nets.  No
  3-state buffer or Longline pull-up is used; the alternate clock buffer
  (ACLK) is.

## Method and confidence

How the designs are decoded and simulated, and how far either can be
trusted, is `fpga-comparison.md`'s ("Provenance"), as are the glossary
and the confidence words.  Every net here is **read** from the
netlist `tools/fpga/netlist.py` writes; statements marked
**simulated** are printed by Icarus Verilog running it, driven by
`tools/fpga/z3815a_tb.v`; **inferred** is reasoning from those.

    make -f tools/fpga/Makefile z3815a                         # decode, build, the fast run
    make -f tools/fpga/Makefile check-readings-z3815a.<run>    # one run, diffed against its reference

Every simulated figure quoted here is printed by one of the runs
`make -j16 -f tools/fpga/Makefile check-readings` makes, which diffs
each against `tools/fpga/ref/<image>.<run>.readings`; each run's flags
are its `READ_` line in `tools/fpga/Makefile`, always with `+probe`.
The runs this trace quotes:

| Run | Flags | What it shows |
| --- | ----- | ------------- |
| `z3815a.fast` | `+seconds=0 +registers +interpolator` | the preset, the read map, mode 3 |
| `z3815a.byte_1c` | `+seconds=0 +registers +byte=1c` | the read map with the interpolator at `0x1c` |
| `z3815a.three` | `+seconds=3 +interpolator` | three readings, the 1 PPS edges, the tap |
| `z3815a.freerun` | `+freerun=2000000 +edges` | the P7 divider and P38/P40, P7 GCLK's twin |
| `z3815a.p7_130`, `z3815a.p7_97` | `+freerun=5000000 +edges +p7=130` (`97`) | P38/P40 with P7 slower, faster |
| `z3815a.osc_48` | `+freerun=5000000 +edges +p7=100 +osc=48` | P38/P40 with P53 faster |
| `z3815a.pll` | `+freerun=5000000 +edges +p7=100 +osc=50.862630208333` | P38/P40 at the frequencies the divides imply |
| `z3815a.freerun_4s` | `+freerun=4000000000 +edges` | four seconds free: P14 and P28 on alternate seconds, every P38 and P40 rise |
| `z3815a.sync1`, `.sync2`, `.sync3` | `+seconds=2 +sync=1` (`2`, `3`) | the sync field |
| `z3815a.p35` | `+freerun=2000000000 +p35=130` | two seconds free with P35 a clock: the P35 counter, its resets and its pins |
| `z3815a.p84` | `+seconds=0 +registers +p84=2917` | the read map with P84 a clock: the P84 counter at the odd addresses |

`tools/fpga/probe.v` removes every pulse shorter than 5 ns before it
measures a net (`outputs.md`, "Simulation").  The nets here that have
them: KE.Y and BB.Y, the model's hazards on combinational outputs
(KE.Y every prescaler cycle; BB.Y, a read-multiplexer term, in the fast
run), JI.Y, the terminal AND, once a second (`z3815a.three`: 2 removed,
the first at 0.884736729 s, as a carry ripples in); and LE.X or LD.Y,
whichever is the phase detector's lagging side (see "The phase
detector"), whose pulse is the detector's own reset, as short as its
loop, and not a hazard; and in the sync runs JG.Y (`z3815a.sync1`, one
removed, at 0.000070228 s) and, in `z3815a.sync3`, JG.Y and JD.X (one
each, at 0.250000228 s, as the pending sync clears).  The testbench's `+edges` lines are the pins'
edges, unfiltered.

`make -f tools/fpga/Makefile check` diffs the netlist's Icarus Verilog
`-Wall` warnings and Yosys `check` loops against
`tools/fpga/ref/z3815a.structure`, which lists two "found logic loop in
module z3815a_fpga", one "through \LD.QY" and one "through \LE.QX": LD/LE,
the phase detector's flip-flop pair, which clears itself (`outputs.md`,
"Simulation").  There is no unrouted or undriven net, so nothing the
design uses depends on the two bottom-right PIPs the decoder leaves
undecoded (`tools/fpga/xact/README.md`, "Not covered").  The "bad mux
111111" on the top-left clock line is GCLK taken straight to P83's
output flip-flop, and the "111110" on the left-lower line is ACLK to
P20's (`outputs.md`, "Netlists"), as for the Z3816A: both benign, as
the Z3805A's, and borne out by P83 carrying the internal 1 PPS and P20
pulsing once per internal 1 PPS, 76 ns before P83, in simulation.

## Clocks

The Z3815A and the Z3816A are the two two-buffer designs (`outputs.md`,
"The FPGA bitstreams").  Read from the clock buffers, AB and the blocks'
K pins (`clock_loads` in `tools/fpga/ref/z3815a.stats`):

- **ACLK** is P53 (`PAD49_I`), taken straight into the alternate clock
  buffer.  It clocks AB, the 1 PPS divider's prescaler and FK, its
  reset ("The 1 PPS divider"), FB (P20) and LK (P51, P57), and P20's
  output flip-flop.
- **GCLK** is AB.Y.  AB is a lone toggle on ACLK (`F = ~QY`,
  `K = ACLK_O`), so GCLK is P53 halved.  It clocks the register port,
  the count's tick CG, the start and stop pipelines, the 1 PPS divider
  after its prescaler, the phase detector's LD, the bus's output phases
  AL, and P83's output flip-flop.
- **P7** (`PAD7_I`), through general routing, clocks ten blocks: the
  P7 divider and LE ("The phase detector").
- **P84** (`PAD13_I`) clocks FH and FI, a four-bit counter of P84 rises
  with no reset, read in bus bits 4 to 7 at odd read addresses ("The
  P84 counter").
- **P35** (`PAD68_I`) clocks LB, a toggle (`F = ~QY`), and **LB.Y**
  clocks thirteen blocks (FA, GA, HA, HC, HD, IA, IC, JA, JC, KA, KC,
  LA, LC) and drives P34: the P35 counter, reset from the internal
  1 PPS (FC.X into JA) and out on P19, P21, P26, P29 and P30 ("The P35
  counter").
- **Local clocks**: BG.X, the selected start source, clocks BH and CH;
  CG.Y clocks the count's head BA and each count stage the next ("The
  count"); the tap KI.X clocks CI; AF.Y clocks AG.

`z3815a_tb.v` drives P53 at 20 MHz, so GCLK runs at 10 MHz and the
count's ticks at the firmware's 100 ns (`counter.md`).  By default it
derives P7 as its own divide-by-two of P53, reset as AB is, so P7 is
GCLK's twin: a test choice, under which the phase detector's two sides
run at different rates (P7 / 6250 against GCLK / 6144) and cannot be
told apart by their clocks.  `+p7=NS` makes P7 a clock of its own and
`+osc=NS` sets P53's period, which the `p7_*`, `osc_48` and `pll` runs
use.  On a board the divider's ratio puts GCLK at 9.8304 MHz (P53 at
19.6608 MHz); the logic is the same at either rate (**inferred**; "The
1 PPS divider").  The board's sources of P7 and P53 are **inferred**
until a board is probed.  P78, P23, P27 and P50 are held Low
throughout, and P35 and P84 but in the runs that clock them:
`+p35=NS` and `+p84=NS` make each a clock of its own, which the `p35`
and `p84` runs use.

## Register port

The Z3805A's (`z3805a-trace.md`, "Register port"), on the same pins
(P58 to P71 the bus, P61 the select, P75 R/W, P69, P68 and P66 ADDR0
to ADDR2), with these differences (**read**):

- The address is three bits: the firmware writes eight registers
  (`counter_load_registers` loops to 8), and only registers 0 to 3
  decode.  CL (over P61 and P75) gates the bus and, through DL, makes
  the write strobe DL.Y; DJ decodes registers 3 (DJ.X) and 0 (DJ.Y),
  EJ registers 2 (EJ.X) and 1 (EJ.Y).  Registers 4 to 7 decode to
  nothing: every write strobe's term wants ADDR2 Low, so the firmware's
  writes of them land nowhere and they hold no state.
- Register 0: bit 6 (`0x40`, P59) is the preset, into EI.Y; bits 0 and
  1 (P71, P70) the mode, EI.X their NAND, Low in mode 3.
- Register 1: the read address in bits 3 to 7 (DK.Y bit 7, EH bits 5
  and 6, FG.Y bit 3, FG.X bit 4), and bits 0 and 1 in DH, the
  references, as the Z3805A's.
- Register 2 is the Z3815A's own.  Bit 0 (`0x01`, EF) resets FD on its
  rise, through BF and EF.X, and so sets which seconds P14 and P28
  pulse on ("The 1 PPS divider").  Bits 2 and 3 (`0x0c`, EG.X and EG.Y)
  select the start source in BG: `0x00` P82 (the GPS 1 PPS), `0x04`
  P84, `0x08` and `0x0c` P78.  EG.X also feeds HB, out on P24 and P25.
- Register 3: bits 0 and 1 the sync field, latched in GG on DJ.X.

**The bus's output.**  The read multiplexer is combinational (CK, FJ,
CF, DF and the rest have no clock); the eight bus pins' I/O blocks
register it, bits 0 and 2 on AL.Y and bits 1 and 3 on AL.X (bits 4 to
7 unregistered), AL being a two-bit Johnson counter on GCLK whose
phases also select the count's read terms.

**The interpolator** is external, as the Z3805A's: its byte comes in
on P3 to P11 and reads back complemented at `0x10` and `0x11`.

**Bus bits 4 to 7** read the seconds count (HH, II, enabled by JI.Y)
when register 1 bit 3, the read address's bit 0, is clear, and the
P84 counter FH/FI when it is set (GI, HI, IJ and HJ, each a
multiplexer on FG.Y; "The P84 counter").

**The read map** (**simulated**, `check-readings-z3815a.fast`): a sweep
of all 32 read addresses gives the status, `0x08`, at `0x00` to `0x0f`,
the interpolator's byte complemented, low nibble first, at `0x10` and
`0x11` (with the byte at `0x5a`, `0x05` and `0x0a`; at `0x1c`, `0x03`
and `0x0e`, `check-readings-z3815a.byte_1c`), the count's nibbles at
`0x12` to `0x17`, and 0 at `0x18` to `0x1f`: the Z3805A's map from
`0x10` up.  Below
`0x10` it is not the Z3805A's, whose bits 2 and 3 at `0x00` to `0x07`
are those of `0x10` to `0x17` and at `0x08` to `0x0f` are 0
(`z3805a-trace.md`, "Register port"): here every address reads the
status, bit 3 the constant 1.  Bits 4 to 7
read 0 at every address there because P84 is held; with P84 a clock
they carry the P84 counter at the odd addresses ("The P84 counter").

### The P84 counter

FH and FI, both `K = PAD13_I` (P84) and neither with a reset but
configuration's, are a four-bit binary counter of P84's rises
(**read**): FH.QX toggles (`F = ~QX`), FH.QY toggles on FH.QX
(`G = QX·~QY + ~QX·QY`), FI.QY on FH.QX·FH.QY and FI.QX on
FH.QX·FH.QY·FI.QY, so FH.QX is bit 0, FH.QY bit 1, FI.QY bit 2 and
FI.QX bit 3.  Each bit meets a seconds bit in a multiplexer on FG.Y,
register 1 bit 3, the read address's bit 0, out on the bus unregistered:

| Bus bit, pin | Block | FG.Y set (odd address) | FG.Y clear (even address) |
| ------------ | ----- | ---------------------- | ------------------------- |
| 4, P62 | GI.X | FH.QX, bit 0 | HH.QY, the seconds' bit 0 |
| 5, P60 | HI.X | FH.QY, bit 1 | HH.QX, bit 1 |
| 6, P59 | IJ.X | FI.QY, bit 2 | II.QX, bit 2 |
| 7, P58 | HJ.Y | FI.QX, bit 3 | II.QY, bit 3 |

So every odd read address returns, in its upper nibble, P84's rises
modulo 16, whatever its lower nibble returns.  Nothing else reads FH or
FI.  P84 also reaches BG, the start select, at register 2's `0x04`
("Start, stop and status bits").

**Simulated** (`check-readings-z3815a.p84`, P84 at 2917 ns, a period no
bus time is a multiple of): over the 32-address sweep every odd
address's upper nibble is the testbench's own count of P84's rises
before the read, modulo 16 (`0x01` reads `0x78` after 23 rises, `0x03`
`0xb8` after 27, `0x11` `0x6a` after 54), and every even address's is 0,
the seconds; the lower nibbles are the fast run's, and the count still
reads `0x555555`, its bytes taken from the low nibbles as the
firmware's are.

## The count

### What reads out as the count: the ripple Johnson chain

The firmware reads the 24-bit count as six nibbles at `0x12` to `0x17`,
low nibble of each (`counter.md`).  Those nibbles reach bus bits 0 to 3
through CK (bits 0, 1) and FJ (bits 2, 3).  Tracing the read
multiplexer back (**read**):

- CK, combinational: `CK.F = CJ_X·CF_Y + CJ_X·~DK_Y + CF_Y·DK_Y`,
  `CK.G` the same over `BH_Y`.  DK.Y, register 1's bit 7
  (`DK.F = DK.QY·~EJ_Y + DK.QY·PAD44_I + EJ_Y·PAD44_I`), is the
  selector: clear, the low two read bits are the status (CJ.X the stop
  bit, BH.Y the start bit); set, they are CF.Y, the addressed nibble's
  data, in the two AL phases.
- `CF.F = BE_X·~EH_Y + ~BE_X·EH_X·~EH_Y·ED_Y`, from the readout decode
  BE, ED, DD and DF (in rows B to E).
- `BE.F` reads `AC_X`, `CD_Y`, `AA_Y`, `BD_X`; `ED.F = EC_X + CA_X +
  EA_X + CC_X`; `DD.F = CB_X + DC_X + DA_X + BC_X`; `FJ.F = ~DK_Y +
  DF_X` with `DF.F` over `BE_Y`, `DD_Y`.  AA and AC are combinational
  terms over the interpolator's pads, and each stage's own F or G
  output is its read term.

So the count the port returns is the **ripple Johnson chain**: CG on
GCLK, then BA, BB, CD, BD, EC, DC, EA, DA, CA, CB, CC, BC, each clocked
by the one before, as `z3805a-trace.md` traces for the Z3805A and
`z3816a-trace.md` for the Z3816A.  It is not the P7 divider; no input
of CK, FJ, CF, ED or DD is a P7 divider net.  The stages (**read**
from the clock and reset nets), with the count bits each holds:

| Stage | Clock | Clock edge | Reset | Bits | Role |
| ----- | ----- | ---------- | ----- | ---- | ---- |
| CG | GCLK | rising | none (`CG.G` clears QY while EI.Y) | 0 (QY) | the gated tick: toggles while AI.X arms it and DI.Y does not stop it |
| BA | CG.Y | rising | EI.Y | 1 (QY) | the head: one flip-flop, `G = ~QY` |
| BB | BA.Y | falling | EI.Y | 2, 3 | two-bit Johnson stage, mirrored |
| CD | BB.X | falling | EI.Y | 4, 5 | two-bit Johnson stage, mirrored |
| BD | CD.X | falling | EI.Y | 6, 7 | two-bit Johnson stage |
| EC | BD.Y | falling | EI.Y | 8, 9 | two-bit Johnson stage |
| DC | EC.Y | falling | EI.Y | 10, 11 | two-bit Johnson stage |
| EA | DC.Y | falling | EI.Y | 12, 13 | two-bit Johnson stage |
| DA | EA.Y | falling | EI.Y | 14, 15 | two-bit Johnson stage |
| CA | DA.Y | falling | EI.Y | 16, 17 | two-bit Johnson stage |
| CB | CA.Y | falling | EI.Y | 18, 19 | two-bit Johnson stage |
| CC | CB.Y | falling | EI.Y | 20, 21 | two-bit Johnson stage |
| BC | CC.Y | falling | EI.Y | 22, 23 | two-bit Johnson stage |

Bit 0 is the gated GCLK toggle, bit 1 a one-flip-flop head, and bits
2 to 23 eleven two-bit ripple Johnson stages: 1 + 1 + 22.  Each stage
clocks the next from its own output, and every one after BA carries
`K_INVERT`, so it advances on the falling edge of the one before: a
ripple, no carry net.  Nine stages have the documented form (`QX <- DI`, DI tied to the
stage's own QY; `QY <- G = ~QX`; QY clocks the next).  BB and CD are
mirrored (`QX <- F = ~QY`; `QY <- DI`, DI the stage's own QX; QX
clocks the next), and their Y outputs are read terms, not stage bits.
CG.Y is count bit 0, not an enable.  **Every stage but CG resets on
EI.Y, the preset; CG clears through its G.**  The read is linear,
`0x555555` plus the ticks (mod 2^24), and read bit 0 is the complement
of CG.QY (**read**).

**EI.Y is the preset.**  EI (`K = GCLK`,
`F = ~DJ_Y·QX + DJ_Y·~PAD30_I + DJ_Y·~PAD29_I`,
`G = ~DJ_Y·QY + DJ_Y·PAD42_I`) is register 0's: DJ.Y
(`DJ.G = ~PAD35_I·~PAD33_I·DL_Y·~PAD32_I`) is the register-0 write
decode over the address pins, and EI.G loads PAD42, P59, data bit 6,
into EI.Y: the preset.  EI.F loads the NAND of data bits 0 and 1 (P71,
P70) into EI.X, the mode-3 bit, which BH captures and AF and AG read.
CG.X is EI.Y registered on GCLK (`CG.F = EI_Y`) and resets BH.
Asserting EI.Y resets the ripple chain, the start pipeline, the stop
CI and clears the tick CG; clearing it lets the next count run.

**The preset reads `0x555555`, the status `0x08`** (**simulated**,
`check-readings-z3815a.fast`: "with the preset the count reads 555555",
"status 08", `0x12` to `0x17` each `0x05`).  The reset state of each
Johnson stage reads `01` per two bits, the Z3805A's `0x555555`.  The
status's `0x08`, where the Z3805A reads `0x04`, is bit 3 tied High and
bit 2 the internal 1 PPS flag GH.Y, clear ("Start, stop and status
bits"); it does not change with the interpolator's byte.

**The readings** (**simulated**, `check-readings-z3815a.three`): the
first, the GPS 1 PPS at 0.250000037 s, reads the count `0x6b2fbe`,
1,432,169 ticks over the preset.  The GPS 1 PPS's rising edge starts
the count and a divider tap stops it, as in the Z3805A.  With GCLK at
the testbench's 10 MHz the internal 1 PPS's period is 0.98304 s,
1.696% short of 1 s (1.725% fast), so the count falls 169,600 ticks a second (1,432,169, then
1,262,569, then 1,092,969); on a board, at 9.8304 MHz, the internal
1 PPS is a true 1 s and the count holds (**inferred**).  The seconds
nibble counts the internal 1 PPS (0, then 1, then 2 over three
readings).

## Start, stop and status bits

- **Start.**  BG (`F` over `PAD15_I`, `PAD19_I`, `PAD13_I`, `EG_X`,
  `EG_Y`) is a three-way start-source select on register 2's bits 2
  and 3: **P82** (`PAD15_I`, the GPS 1 PPS) at `0x00`, **P84** at
  `0x04`, **P78** at `0x08` or `0x0c` (**read**; the testbench writes
  `0x00` and holds P78 and P84 Low).  BG.X clocks BH's flip-flop, the
  start's capture (`K = BG_X`, `RD = CG_X`, `DI = EI_X`), and CH, which
  DG turns into a one-GCLK pulse, DG.X, for the full sync.  BH.X
  (`F = ~AI_X·~AJ_Y·QX + ~AI_X·~AJ_Y·AG_Y`, combinational over the
  capture) is the interval the interpolator measures, out on **P81**
  (`PAD16`, inverted, so a Low pulse); BH.Y is the start bit, status
  bit 1, read through CK.Y.  AI (`K = GCLK`, `RD = EI_Y`) is the armed
  gate that lets the tick CG run.
- **Stop.**  The divider tap is the rising edge of KI.X, a stage bit
  of the 1 PPS divider (`KI.F = ~KH_X·QX + QX·~LH_Y + KH_X·~QX·LH_Y`,
  enabled by `KI.G = LH_X·KG_X`).  It clocks CI (`F = 0`, `DI = BH_Y`,
  `K = KI_X`, `RD = EI_Y`): when the tap fires, CI latches the start
  bit.  `CJ.F = AG_X + CI_X` is CJ.X, the stop bit, status bit 0, read
  through CK.X; four flip-flops on GCLK, CJ.QX, CJ.QY, DI.QX and
  DI.QY, carry it to DI.Y, which holds CG and freezes the count.  So **the divider tap that stops the count is the
  same event that sets the stop bit**, through CI, CJ and CK, and the
  start bit sets at the GPS edge, the stop bit at the tap: the order
  `z3805a-trace.md` finds for the Z3805A.
- **Status bits 2 and 3** are not the interpolator's inputs, as they
  are on the Z3805A.  At a status address (DK.Y clear) `FJ.F = ~DK_Y +
  DF_X` is 1, so bit 3 (P65) reads 1, and FJ.G gives GH.Y, so bit 2
  (P67) is the internal 1 PPS flag: set by the terminal JI.Y, cleared
  by the next prescaler tick GJ.Y (**read**).  Hence the status `0x08`
  whatever the interpolator's byte.
- **Mode 3.**  A pulse of the start bit sets both status bits 0 and 1
  (status `0x0b`), the interpolator's two references (**simulated**,
  `check-readings-z3815a.fast`, "reference 0 measured: status 0b" and
  "reference 1").

## The 1 PPS divider

The internal 1 PPS leaves on **P83** (`PAD14`, `O = FC_X`, output
register on GCLK, top-left clock line), **P20** (`PAD86`, `O = FB_Y`,
output register on ACLK, left-lower line) and **P14** and **P28**
(`PAD95`, `PAD78`, both `O = IB_Y`, unregistered).  Tracing back
(**read**):

- **The prescaler, on ACLK.**  EK, EL, GK, GL, HK, HL, IK, JK, JL, KJ,
  KK, KL and LL, all `K = ACLK_O` (EK, EL, GK, GL, HK and IK with
  `RD = FK_Y`), form a counter whose terminal FL.Y
  (`FL.F = ~EL_Y + ~GL_Y`, `FL.G = ~FL_X`) GJ samples on GCLK
  (`GJ.DI = FL_Y`) and turns into GJ.Y, one GCLK pulse every
  12,288 ACLK = 6144 GCLK.
- **The GCLK chain.**  GJ.Y clock-enables LF and KF (`EC = GJ_Y`), and
  KG (`F = LF_X·KF_X·GJ_Y·LF_Y·KF_Y`) is their terminal carry, which
  enables LI and LJ; KI, KH and LH follow (`KI.EC = KI_Y`, and so on).
  JI (`F = JH_Y + LH_X·KG_X·LH_Y·KH_Y`), combinational, is the terminal
  count, every 1600 GJ.Y pulses: 6144 x 1600 = 9,830,400.  JI.Y enables
  the seconds counter HH/II, so it is the internal 1 PPS the seconds
  nibble counts.
- GH (`F = JI_Y + ~GK_Y·FC_X`) passes JI.Y, held through FC.X until the
  prescaler's GK.Y; FC (`K = GCLK`, `DI = GH_X`) registers it, and
  P83's output flip-flop registers FC.X again.
- **P20**: FB (`K = ACLK`, `DI = FC_X`, `F = ~QY·FC_X`) makes FB.Y a
  one-ACLK pulse at each rise of FC.X, registered into P20 on ACLK.
- **P14, P28**: FC.Y (`G = QX·~QY`, QY being FC.X one clock late) is a
  one-GCLK pulse at FC.X's rise.  FD (`F = QY·~FC_Y + ~QY·FC_Y`,
  `RD = EF_X`) is a toggle: it changes at each internal 1 PPS, and the
  rise of register 2's bit 0 resets it.  IB (`F = FD_Y + ~FC_Y + ~QY`,
  `K = GCLK` falling) makes IB.Y Low from FC.Y's rise to the next
  falling GCLK edge, only on seconds when FD.Y is Low: an active-Low,
  half-clock pulse on alternate internal 1 PPS edges, a 2 s period,
  whose phase register 2's bit 0 sets.
- JH (`F = IH_Y·GG_X·GG_Y·DG_X`) is the full-sync strobe: it fires only
  with register 3's sync field at 3 and a start-source edge (DG.X), and
  JH.X resets the GCLK chain (KF, LF, KH, KI, LH, LI, LJ carry
  `RD = JH_X`), the prescaler (through FK) and, through GE, the P7
  divider ("The sync field").  With the field at 0, as every run but
  the `sync` runs leaves it, JH never fires and the chain rolls over on
  its own terminal.

**The ratio and the tap** (**simulated**, `check-readings-z3815a.three`):
P83 rises every 983,040,000 ns exactly, first at 0.983040828 s, a
divide by 9,830,400 (`0x960000`), where the Z3805A's is 10,000,000.
The tap, KI.X, rises 393,215,900 ns after P83, 3,932,159 clocks; with
P83's own register it is 3,932,160 clocks (`0x3c0000`) after the
internal 1 PPS, plus two of pipeline: 0.4 of the divide, as the
Z3805A's `0x3d0900` is of `0x989680`, and the firmware's 0.3932162 s
calibration treated as 100 ns ticks (`counter.md`, "Which 1 PPS
edges").  So 9,830,400 is one second of a 9.8304 MHz oscillator, a
telecom frequency (P53 then 19.6608 MHz), where the Z3805A's 10,000,000
is one second of 10 MHz.  The divide ratio and the tap are the logic,
the same at any clock; the oscillator's frequency, and so the reading's
absolute scale, is a board fact the netlist cannot settle.  The
prescaler's period is simulated too: P40, which GJ.Y sets, rises every
614,400 ns at 10 MHz GCLK, 6144 clocks (`check-readings-z3815a.freerun`),
and every 589,824 ns with P53 at 48 ns, 12,288 ACLK
(`check-readings-z3815a.osc_48`).

**P20 and the alternate seconds** (**simulated**): P20 rises 76 ns
before each P83 rise, at the testbench's 50 ns ACLK and under the
models' delays (`check-readings-z3815a.three`, the P83 probe's "after reference",
P20 its reference).  P14 and P28 idle High and go Low 49 ns,
half a GCLK, at the first and third of the four internal 1 PPS edges
P83 rises at, 0.983040730 s and 2.949120730 s, 1,966,080,000 ns apart,
and at neither the second nor the fourth
(`check-readings-z3815a.freerun_4s`, each P14, ~P14, P28 and ~P28
pulse printed).  The same run prints every P38 rise: "P38 6399 rises"
in the four seconds, each after the first "625000.0 ns after the
last", across all four 1 PPS edges: with the sync field 0 nothing
rearms the P7 divider.  P40, faster with P7 GCLK's twin, has "P40 6400
rises", some "1228800.0 ns after the last", where the GCLK side sets
twice before the P7 side once.  P14's and P28's "rise" at 0.000000003 s in
every run is the power-up unknown settling to High, not a pulse.

## The sync field

Register 3's sync field, latched in GG (`GG.X` data bit 0, `GG.Y` data
bit 1, on DJ.X), acts through GG's fanout (**read**):

- **Kind 3** (both bits): JH, the full-sync strobe, fires on the next
  start-source edge (DG.X, through BG and CH), resets the 1 PPS
  divider's GCLK chain and prescaler, and through JH.Y into JI makes
  that edge the internal 1 PPS.  GE captures JH.X on P7 and resets the
  P7 divider's prescaler JE, KE and, through LE.Y and LD.X, its
  counter and the phase detector.
- **Kinds 1 and 2** (one bit each): HG and IH make IH.Y, the pending
  sync, on the field's change from 0, and IG.X, an edge of IF's sample
  of KE.X, clears it; JG (with GG.X) and JD (with GG.Y), while it is
  pending, drive JE's A and B, the P7 divider's /5 prescaler.
- **The P7 divider in the sync path.**  IF and JF sample KE.X, a P7
  net, on GCLK, and feed IG and IH, whose IH.Y feeds JH and so JI, and
  JD and JG read KE.X too: P7-divider state is coupled into the sync
  logic structurally.

**Simulated** (`check-readings-z3815a.sync1`, `.sync2` and `.sync3`:
`+seconds=2 +sync=KIND`, the field written 0 then the kind on the first
arm, as `counter_arm` does; P7 GCLK's twin, the testbench's default):

- **Kind 1** steps the P7 divider one P7 clock, 100 ns, earlier and
  leaves the 1 PPS divider alone.  IH.Y is High 400.0 ns from
  0.000069828 s, until IG.X clears it (IG.X with IH.Y High 99.0 ns
  from 0.000070129 s); JG.Y is High once, 300.0 ns, and KE.Y's period
  is 400.0 to 500.0 ns: one prescaler cycle of 4 P7 clocks for 5.
  P38's first edge, which LE.X's first removed pulse marks, is at
  0.000625427 s.  P83 still rises at 0.983040828 s, and the counts read
  1,432,169 and 1,262,569 over the preset, as `z3815a.three`'s.
- **Kind 2** steps it one P7 clock later: JD.X High once, 300.0 ns,
  KE.Y's period 500.0 to 600.0 ns, a cycle of 6, P38's first edge at
  0.000625627 s; P83 and the counts as kind 1's.
- **Kind 3** makes the next GPS 1 PPS the internal 1 PPS: JH.Y, one
  GCLK long (High 100.0 ns), rises 92.0 ns after the GPS 1 PPS at
  0.250000037 s, GE.X, its capture on P7, 290.0 ns after it, and P83
  rises at 0.250000328 s, 291.0 ns after the GPS edge, under three GCLK
  clocks, where the Z3805A's kind 3 gives no 1 PPS until a second after
  it.  P14 and P28 pulse with it, and the seconds nibble counts it (1
  at 0.647264 s).  The next P83 comes 983,040,200.0 ns later, at
  1.233040528 s, two clocks over the divide, and the counts after the
  sync read 3,932,166 and 3,762,566 over the preset.  IH.Y stays High
  249,930,400.0 ns, from the field's write to the GPS edge: IG.X never
  clears it (IG.X with IH.Y never rises).  While it is pending JG.Y and
  JD.X both pulse every 399.0 to 400.0 ns (624,826 rises each, the
  last at 0.249999828 s), so every P7 prescaler cycle is 4 P7 clocks
  and the P7 divider's terminal comes every 500,000 ns, ahead of the
  GCLK side's 614,400: P38 rises every 500,000.0 or 1,000,000.0 ns, a
  set lost where the terminal comes again before the GCLK side has
  cleared the last (LE.X's period 500,000.0 to 1,000,000.0 ns, 407
  rises, the last at 0.249514427 s), until GE.X rearms the P7 divider.

**The P7 divider's coupling into JI is not reachable** for any fixed
field written 0 and then the kind, as the equations say and these runs show: every path from IH to
JI goes through JH, and `JH = IH_Y·GG_X·GG_Y·DG_X` wants both field
bits and a GPS edge, so with field 1 or 2 JH never fires (JH.Y never
rises in `z3815a.sync1` or `.sync2`); with field 3,
`IG = ~IF_X·IF_Y·(~GG_X + ~GG_Y)` is 0, so only JH.Y clears IH
(`IH.F = HG_X·~QY·~HG_Y + ~IG_X·QY·~JH_Y`), and KE.X cannot move JH,
which fires on the GPS edge alone.  KE.X does reach JE, through JD and
JG: in kinds 1 and 2 the one step, and in kind 3 the 4-clock prescaler
cycles while the sync is pending.  The Z3816A's IF, IG, IH, JF, JD, JG
and JH are these, and its runs agree (`z3816a-trace.md`, "The sync
field").  How a step of the P7 divider moves the internal 1 PPS
through a closed PLL is not simulated: the runs close no loop.

## The P35 counter

Fourteen blocks on P35, and four on GCLK beside them, are a counter of
P35 that the internal 1 PPS resets and whose only outputs are pins
(**read**; every net of them goes to another of them or to a pin, none
to the read multiplexer or to the rest of the design):

- **LB**, `K = PAD68_I` (P35), `F = ~QY`, no reset but configuration's:
  a toggle, so LB.Y is P35 halved.  It drives P34 and clocks the other
  thirteen.  P36 is P35 itself, inverted.
- **The /5, LA and KA.QY.**  LA is a two-bit binary counter (`F =
  ~KA_Y·~QX`, `G = ~KA_Y·QX·~QY + ~KA_Y·~QX·QY`) cleared while KA.Y is
  High, and `KA.G = ~QY·LA_X·LA_Y` sets KA.QY at LA's 3 and clears it
  the clock after: five states, LA 0 to 3 then KA.Y, so KA.Y is High
  one LB.Y period in five and LA.Y + KA.Y three in five.
- **A thirteen-bit binary count**, every stage on LB.Y: KA.QX (bit 0,
  `KA.F = JA_Y + ~QX`), HD.QY (bit 1, `HD.G = ~JA_Y·(KA_X XOR QY)`),
  IA (bits 2, 3), JC (4, 5), IC (6 in QY, 7 in QX), HC (8), LC (9, 10)
  and KC (11 in QY, 12 in QX), each a two-bit stage or a toggle enabled
  by the carry below it: HD.X (`KA_X·QY`, bits 0 and 1), JB.Y (bits 0
  to 3), ID.Y (0 to 7), HA.Y (`ID_Y·HC_Y`, 0 to 8) and KD.Y (0 to 12).
- **A decade on KD.Y**, GA and FA (`EC = KD_Y`): GA.QX bit 0, GA.QY bit
  1, FA.QY bit 2 and FA.QX bit 3, which step 0 to 9 and back to 0 (the
  four next-state equations, stepped from 0).  So
  the whole count is LB.Y / 81,920 (8192 x 10), P35 / 163,840.
- **The reset, JA.**  `JA.F = ~FC_X + QY·~KA_Y` into both its
  flip-flops, `JA.G = QY·FC_X·KA_Y` its Y: while the internal 1 PPS
  FC.X is Low JA.QY arms; JA.Y is High from the first moment within
  FC.X's High time that KA.Y is also High until the next LB.Y edge,
  which clears JA.QY.  JA.Y resets IA, JC, IC, HC, LC, KC, GA and FA
  (`RD = JA_Y`) and at that edge sets KA.QX (`JA_Y + ~QX`) and clears
  HD.QY; KA.Y being High, the same edge clears LA and KA.QY.  The count
  restarts at 1 with the /5 at its first state, once per internal
  1 PPS, as long as FC.X is High for a KA.Y.
- **P26, HA.X** (`F = QX·~KA_X + ID_Y·HC_Y·~QX`): set by the carry out
  of bit 8, HA.Y, and cleared at the next LB.Y edge with KA.X High: two
  LB.Y periods High in every 512.
- **P21 and P19, FA.X and its complement**: the decade's bit 3, High at
  its 8 and 9, two-tenths of each 81,920 LB.Y periods.
- **GCLK / 12, GB, GC and GD**, on GCLK: six flip-flops in a twisted
  ring (`GB.G = ~GD_Y·~FC_Y` into GB.QY, then GB.QX, GC.QY, GC.QX, GD.QX
  and GD.QY, each `·~FC_Y`), all cleared by FC.Y, the one-GCLK pulse at
  FC.X's rise: GC.X is GCLK / 12, its phase set by the internal 1 PPS.
  9,830,400 / 12 is 819,200, so after the first clear each clear finds
  the ring in the state it clears it to (arithmetic; **inferred**).
- **P29 and P30, KB.Y and its complement**: KB.QY registers GC.X on
  GCLK (`DI = GC_X`), and `KB.F = QY·LA_Y + QY·KA_Y + ~QY·~LA_Y·~KA_Y`
  is the XNOR of KB.QY with LA.Y + KA.Y: GCLK / 12 against LB.Y / 5.

**Simulated** (`check-readings-z3815a.p35`, P35 at 130 ns, two seconds
free; every figure below is in the run's probe lines):

- LB.Y, on P34, every 260.0 ns, High 130.0 ns: P35 halved.  KA.Y every
  1300.0 ns, High 260.0 ns: LB.Y / 5, one period in five.
- FC.X, the internal 1 PPS, is High 20,400.0 ns each time, far longer
  than the 1300.0 ns the reset needs to find a KA.Y.  JA.Y rises twice,
  once per internal 1 PPS, 1.0 ns and then 341.0 ns after FC.X (High
  99.0 and 259.0 ns, until the next LB.Y edge), as the /5 stood at
  each.
- At each reset the run prints the counter's own flip-flops and its
  own count of LB.Y's rises: "JA.Y resets the P35 counter at decade 1,
  binary 4412; LB.Y rose 3780924 times since the last" at 0.983040729 s
  (since configuration), and "decade 1, binary 4413; ... 3780925" at
  1.966081069 s.  1 x 8192 + 4412 = 12,604 and 3,780,924 modulo 81,920
  is 12,604; 12,605 likewise (arithmetic): the counter holds LB.Y's
  rises since its last reset, the reset's own edge counting 1, modulo
  81,920.  A second holds 983,040,000 / 260 = 3,780,923.08 LB.Y periods
  here (arithmetic), so the reset cuts the last cycle short.
- P21 every 21,299,200.0 ns (81,920 x 260) but across a reset, where
  the interval is up to 24,576,500.0 ns; High 4,259,840.0 ns, 16,384
  LB.Y periods, two-tenths.  It rises 17,039,201.0 to 17,039,701.0 ns
  after FC.X, 65,535 LB.Y periods after the reset's edge: at the count
  65,536, the decade's 8.
- P26 every 133,120.0 ns (512 x 260) but across a reset (up to
  215,540.0 ns), High 520.0 ns, two LB.Y periods.
- GC.X every 1200.0 ns, 12 GCLK, High 600.0 ns, but for an interval of
  1600.0 ns at a clear (the probe prints the extremes only), rising
  500.0 ns after FC.X at both resets.  P29, the XNOR of two clocks of
  different rates here, rises every 520.0 to 1300.0 ns, High 21.0 to
  1121.0 ns.
- P19 is P21's complement, P30 P29's and P36 P35's throughout: none of
  the three "equal" probes rises once its pulses under 5 ns, the pads'
  one-delay skews, are removed.

So the counter's outputs are P35's alone between resets and take their
phase from the internal 1 PPS at each; what P35 is, and what the board
does with P19, P21, P26, P29, P30 and P34, the netlist does not say.

## What the firmware takes from P35 and P84

Nothing (**read**, from the image's code at the addresses given,
`third_party/firmware/images/z3815a-4010.bin`; the hand-written C,
`third_party/firmware/rewritten/common/fpga.c` and `ti.c` and
`rewritten/z3815a-4010/fpga.c`, `ti.c` and `pll_stages.c`, says the same).

- **The P84 counter.**  The image's only read of the port is
  `fpga_read` (`0x4929e`): the port's base, `0x304000`, appears in the
  image twice, there (`0x492a8`) and in `fpga_write` (`0x4928a`).
  `fpga_read` is called by `counter_wait_ready` (`0x4948a`) and
  `fpga_read_seconds` (`0x4932e`), both with register 0, by
  `fpga_bit_test` (`0x4930e`), which nothing calls, and by
  `fpga_read_byte` (`0x49706`), with its register and the next.
  `fpga_read_byte`'s callers pass `0x16`, `0x14` and `0x12`
  (`counter_read_raw`, from `0x4977a`) and `0x10` (`interpolator_read`,
  `0x497b6`), so the odd addresses the firmware reads are `0x11`,
  `0x13`, `0x15` and `0x17`, and of each it keeps the low nibble only
  (`andb` with 15, then `lslb #4`, `0x4971c` to `0x4971e`): the P84
  counter's nibble is discarded.  `fpga_address_sweep_debug`
  (`0x49392`), which nothing calls, steps the address field but reads
  through `fpga_read_seconds`, register 0.
- **P84 as a start source.**  Register 2's bits 2 and 3 are the
  firmware's `FPGA_BIT_ALARM` (mask `0x0c`), `fpga_set_field`'s
  (`0x4956e`) field 6.  The one call with field 6 is
  `counter_set_alarm`'s (`0x494fa`), from `counter_arm` (`0x498a0`), of
  the value `counter_alarm_request` (`0x494d8`) stored, and its eight
  callers (`0x51b5a` to `0x52696`) pass `0x08`, `0x0c` and `0x00`.  The
  image's other writes of register 2 copy all eight register images
  (`counter_load_registers`, `0x49a2c`, and the loops at `0x48ce4` and
  `0x49806`), which `counter_configuration_init` builds with the field
  0; no write of those images' field other than its is traced.  BG
  takes P82 at `0x00` and P78 at `0x08` and `0x0c` ("Start, stop and
  status bits"), so the firmware's values never select P84 (`0x04`).
  The two it steers with differ only in EG.X, which also sets P24 and
  P25 while P27 is Low ("Every output pin").
- **The P35 counter** drives pins alone; no net of it reaches the read
  multiplexer ("The P35 counter"), so no read sees P35.

What P84's and P35's sources are, and what the board does with P19,
P21, P26, P29, P30 and P34, the netlist and the firmware do not say.

## Other outputs

### The P7 divider: P7 / 6250

CE, DE, EE, FE, GE, HE, IE, JE, KE, LE are clocked by P7 (`K = PAD7_I`
on every one) and are **not** reset by the preset EI.Y.  The structure
(**read**):

- JE and KE are a nominal /5 prescaler: with JG.Y and JD.X Low (the
  sync field 0) the JE/KE cycle has five states; for other constant
  JG.Y, JD.X values it has 4 or 6.  `KE.G = JE_X·KE_X` is KE.Y, a
  combinational enable, High once per prescaler cycle.
- FE, EE, IE, HE, CE and DE are a synchronous binary counter, enabled
  by KE.Y and the carries FF.Y and HF.Y.
- GE (`F = DE_Y·IE_Y·GF_X·HE_Y·HE_X`, `GF = KE_X·FE_Y`) decodes its
  terminal count, 1249 (1024 + 128 + 64 + 32 + 1), into GE.QY.
  `LE.G = GE_Y + GE_X` is LE.Y, which resets CE, DE, EE, FE, HE and IE:
  a count of 1250 prescaler cycles, so the total is 5 x 1250 =
  **P7 / 6250**.  Nothing in it is decimal.
- GE.QX takes JH.X (`GE.DI = JH_X`) and resets JE and KE: the P7
  divider is rearmed only by a full sync, and otherwise free-runs.

FE.Y is P7 / 10.  EE.Y, the counter's bit 3, repeats every 80 P7
clocks but for one 90-clock interval across the wrap at the terminal
1249, so it rises 78 times in each 6250 P7 clocks, not P7 / 80
(**simulated**, `check-readings-z3815a.freerun`, P7 at 100 ns: KE.Y
every 500.0 ns, High 99.0 ns, once its 1 ns hazard pulses, one per
cycle, are removed; FE.Y every 1000.0 ns; EE.Y every 8000.0 to
9000.0 ns, High 4000.0 ns; LE.X, P38, every 625,000 ns; and
`z3815a.freerun_4s`, 4 s of P7, 6400 P7 divider cycles: EE.Y
"499200 rises", 78 each, arithmetic).  Over
the 2 ms free run, which writes nothing, KE.Y rises 4000 times, FE.Y
2000 and EE.Y 250, while CA.Y, CC.Y and BB.Y never rise and BA.Y rises
twice, 400 ns apart, by 0.000001129 s, then holds: the P7 divider runs
on its own while the count, unarmed, does not.

### The phase detector: P38 and P40

LE (on P7) and LD (on GCLK) are a two-flip-flop phase-frequency
detector (**read**):

- `LE.F = GE_Y + QX`: the P7 divider's terminal GE.Y sets LE.X, out
  on **P38**.
- `LD.G = GJ_Y + QY`: GJ.Y, the 1 PPS chain's own GCLK / 6144 =
  ACLK / 12288 tick, sets LD.QY, out on **P40**.
- `LD.F = GE_X + QY·LE_X` is LD.X, the reset of both (`LD.RD`,
  `LE.RD`): once both are set, each clears the other.  This is the
  combinational loop the structure check reports.

So P38 is High from the P7 side's edge until the GCLK side's, or P40
from the GCLK side's until the P7 side's: whichever leads is High for
the phase difference, and the lagging side's pulse is the reset loop's
width only.  With P7 at 10 MHz and P53 at 19.6608 MHz both sides run at
1.6 kHz, a PLL's comparison frequency: the two pins are the phase
detector of a loop that locks the 19.6608 MHz oscillator to the 10 MHz
reference (role **inferred**; the divide ratios are **read** and the
unequal-clock runs below **simulated**; the board-level sources of P7
and P53 are **inferred**).

**Simulated** (the `+edges` lines, each pin's rises; the probes'
High times):

- **P7 GCLK's twin** (`check-readings-z3815a.freerun`): P40 rises every
  614,400.0 ns, first at 0.000615129 s, and P38 every 625,000.0 ns,
  first at 0.000625528 s.  P40 leads, LD.Y High 10,401.0 to
  31,601.0 ns, growing; LE.X's pulses are all under 5 ns and removed.
- **P7 slower, 130 ns** (`check-readings-z3815a.p7_130`): P38 every
  812,500.0 ns, P40 every 614,400.0 ns and once 1,228,800.0 ns, the
  GCLK side setting LD again before the P7 side clears it: the
  frequency detection.
- **P7 faster, 97 ns** (`check-readings-z3815a.p7_97`): P38 every
  606,250.0 ns, P40 every 614,400.0 ns; P38 leads, LE.X High 8343.5 to
  65,393.5 ns, and LD.Y's pulses are removed.
- **P53 faster, 48 ns** (`check-readings-z3815a.osc_48`): P40 every
  589,824.0 ns, P38 every 625,000.0 ns.
- **At the implied frequencies** (`check-readings-z3815a.pll`, P7 at
  100 ns, P53 at 50.862630208333 ns, 625,000 / 12,288): P38 and P40
  each every 625,000.0 ns, P38 leading by LE.X's High time, 189.5 to
  189.5 ns: the same rate, at a fixed phase.  The run closes no loop,
  so the phase is the one the two dividers start at.  The testbench
  places each P53 edge at its multiple of the half period, rounded to
  the 1 ps time precision on its own, so the edges jitter by under
  half a picosecond and do not accumulate an error; a half period
  repeated as one rounded delay (25.431 ns for 25.431315) would make
  P40's period 12,288 x 50.862 ns = 624,992.3 ns (arithmetic, not a
  run) and the phase drift.

So P38's rises fall on P7's grid alone (6250 P7 clocks) and P40's on
GCLK's alone (6144 GCLK clocks), and the detector's output is their
phase difference.  The two pins are not independent of each other:
the shared reset ends both pulses, and when one side sets twice before
the other once, the second set is lost (`p7_130`'s one P40 interval of
1,228,800.0 ns).  P38 and
P40 are unregistered outputs and hold nothing the read port returns.

### What of P7 reaches the firmware

Nothing (**inferred**: the paths are read, and the sync runs simulate
them at one P7 phase).  The blocks P7 reaches, following every net from `PAD7_I` onward, are the
P7 divider (CE, DE, EE, FE, GE, HE, IE, JE, KE, with the carries FF,
GF and HF), the phase detector LE and LD with P38 and P40, and the
sync logic that samples KE.X: IF, JF, IG, IH, JD and JG.  One net
leaves that set, IH.Y, and it goes only into JH, which no P7 net can
move for any fixed sync field written 0 and then the kind, as
the Z3805A 3543B, 58503A 3704, Z3801A 3543, Z3815A 4010 and 58503B
1.01.04 firmware write it; the Z3816A 4001's is not read (`counter.md`, "The sync field"; here "The sync
field").  Straight from one nonzero kind to another, IH could hold a
pending state set under the first, which P7-derived samples clear.  So the bus (the
count, the status, the interpolator's byte, the seconds), P83, P20,
P14, P28 and P81 take nothing from P7.  The internal 1 PPS, the
count's ticks and the tap are timed by ACLK and GCLK, ACLK halved,
alone.  The other clocks of what the firmware reads are the start
sources BG selects (P82, P84, P78) and P84 again, which clocks the
counter that bus bits 4 to 7 can read and the firmware discards ("What
the firmware takes from P35 and P84").  The firmware sees P7 only
through what the board does with P38 and P40.

On a board, then, the loop the detector belongs to is all that ties
the firmware's time to P7 (**inferred**; no Z3815A board has been
probed).  Locked, P7 / 6250 = P53 / 12,288, so the board drives ACLK
at P7 x 12,288 / 6250, 19.6608 MHz from a 10 MHz OCXO on P7, and
GCLK at P7 x 6144 / 6250.  The internal 1 PPS, 9,830,400 GCLK clocks,
is then 9,830,400 x 6250 / 6144 = 10,000,000 P7 cycles (arithmetic): a
second of the OCXO exactly, so steering the OCXO steers the 1 PPS
through the loop, as it does directly on the 10 MHz designs.  The image's hardware alarm names include
"10 MHz Fail" and "19.6608 MHz Fail"
(`strings -t x third_party/firmware/images/z3815a-4010.bin`); what
each one watches is not traced.

### Every output pin

The enabled outputs (**read**; the bus pins are "Register port"'s):

| Pin | Source | What |
| --- | ------ | ---- |
| P2 | ~AH.Y, AH `F = 1` | tied Low |
| P14, P28 | IB.Y | the alternate-second 1 PPS, active Low |
| P16 | P82 | the GPS 1 PPS, passed through |
| P19, P21 | ~FA.X, FA.X | the P35 counter's decade bit 3: LB.Y / 81,920, High two-tenths of it |
| P20 | FB.Y, registered on ACLK | a one-ACLK pulse at each internal 1 PPS |
| P24, P25 | ~HB.Y, HB.Y | `HB.F = ~PAD80_I·~EG_X + PAD80_I·PAD84_I`: P27 High passes P23, Low gives ~EG.X (register 2 bit 2) |
| P26 | HA.X | the P35 counter: High two LB.Y periods in 512 |
| P29, P30 | KB.Y, ~KB.Y | GC.X (GCLK / 12) registered on GCLK, XNOR LA.Y + KA.Y (LB.Y / 5) |
| P34 | LB.Y | P35 halved, the P35 counter's clock |
| P36 | ~P35 | passed through, inverted |
| P38 | LE.X | the phase detector's P7 side |
| P40 | LD.Y | the phase detector's GCLK side |
| P47, P48 | ~P53, P53 | the oscillator, passed through |
| P51, P57 | LK.Y, ~LK.Y | `LK.G = QY·~PAD52_I + ~QY·PAD52_I`: an ACLK-prescaler state (KL, LL) XOR P50 |
| P56 | ~P50 | passed through, inverted |
| P76, P77 | ~AK.Y, ~AK.X | AK on GCLK over BI, BJ, BK, the start pipeline; reset by EI.Y (untraced) |
| P79 | AJ.X (`F = 0`), 3-state on AI.Y | Low while AI.Y is Low, released otherwise |
| P81 | ~BH.X | the start's interval, a Low pulse |
| P83 | FC.X, registered on GCLK | the internal 1 PPS |

## Confidence

**Simulated** (`z3815a_tb.v`, the `check-readings-z3815a.*` runs
above): the preset read `0x555555` and status `0x08`; the full read map
over `0x00` to `0x1f`; mode 3 setting both status bits (`0x0b`); the
three readings and the seconds nibble; the divide by 9,830,400 on P83
and the `0x3c0000` tap; P20 before P83; P14 and P28 on alternate
seconds; the P7 divider's stage periods while the count holds; P38
rising on P7 / 6250 and P40 on GCLK / 6144 (ACLK / 12288), each on its
own clock's grid, and the detector's lead and skipped edge; the P35
counter's divides, its reset at each internal 1 PPS and its pins; the
P84 counter at the odd read addresses; sync kinds 1 and 2 stepping the P7 divider a P7 clock earlier
and later and leaving the 1 PPS alone; kind 3 making the GPS 1 PPS
the internal 1 PPS, 291.0 ns after it, and running the P7 prescaler at
4 clocks while it is pending; the P7 divider's coupling never reaching
JI.

**Read:** the count is the ripple Johnson chain, CG then BA and eleven
two-bit stages, read by CK, FJ and CF and reset by the preset EI.Y
(register 0 bit 6); the 1 PPS divider is the ACLK prescaler, GJ and
the /1600 GCLK chain to JI; FC to P83, FB to P20, FD and IB to P14 and
P28; the stop tap KI.X through CI, CJ and CK; the start select BG, BH
and P81; status bit 3 tied High and bit 2 GH.Y; register 2's FD reset
and start-source field; the P7 divider's /5 prescaler and binary
counter to 1249; LE and LD a phase-frequency detector; JH the full-sync
strobe; the register port's eight addresses, of which four decode;
P7's reach, the P7 divider, the detector and the sync logic, whose
only exit is IH.Y into JH; the P35 counter, LB, the /5, the thirteen
binary bits and the decade, JA's reset, GCLK / 12 and KB, whose only
exits are pins; the P84 counter FH/FI and its multiplexer onto bus
bits 4 to 7; the firmware's reads, which keep the low nibble of every
odd address, and its start-select values, which never select P84.

**Inferred:** nothing driven by P7 reaches the firmware, which sees
only ACLK's and GCLK's timing, for a sync field written 0 and then the
kind ("What of P7 reaches the firmware"); locked, the board drives
ACLK at
P7 x 12,288 / 6250, 19.6608 MHz from a 10 MHz OCXO on P7, and the
internal 1 PPS is 10,000,000 P7 cycles.

**Open:**

- The board sources of P7 and P53, and so that the loop is a PLL
  locking 19.6608 MHz to 10 MHz.
- How a step of the P7 divider moves the 1 PPS through a closed PLL,
  and what the P38 disturbance while a full sync is pending does to
  the loop; the runs close none.
- What drives P35 and P84, and what the board takes from P19, P21,
  P24, P25, P26, P29, P30, P34, P51, P57, P76, P77, P79 and the
  passed-through pins.
- Everything on a board, and the pins' roles, read from the logic.
