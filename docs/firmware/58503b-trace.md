# The 58503B FPGA

Traced against the Z3805A's design (`z3805a-trace.md`), the reference.
It reads out identically to the 58503A/Z3801A's
(`58503a-z3801a-trace.md`): the same preset value and the same map over
all 32 addresses (simulated).

## At a glance

| | |
| --- | --- |
| Images that load it | 58503B 1.01.04 |
| Bitstream | `third_party/firmware/images/fpga/58503b.bit` |
| Part | XC3042, PLCC84 |
| Logic blocks, I/O blocks, nets | 101 of 144, 36, 187 (`tools/fpga/ref/58503b.stats`) |
| Clocks | One global clock (GCLK), from P7 as in the Z3805A; the count ripples, and P82, the tap and CH.Y clock single blocks |
| Oscillator the divide implies | 10 MHz (a divide by 10,000,000) |
| 1 PPS output pins | P83, P78 |
| Other outputs | P14, a pulse on alternate seconds; P19, GCLK / 65 |
| Differences from the reference | P45 dropped; P14 an alternate-second pulse; P19 added; P83 through a polarity select written at address 6; P72 and P81 swap AH's outputs; the divider's terminal and output stages re-partitioned |

One global clock, as in the Z3805A: GCLK, from P7, with no ACLK and no
clock divider ahead of GCLK.  GCLK clocks 55 blocks
(`tools/fpga/ref/58503b.stats`);
the others are clocked from logic: the count's stages ripple on block
outputs, P82 clocks AH, the tap EH.Y clocks CI and CH.Y clocks BH
(**read**).  Over the 58503A it has one new I/O block, **P19**, carrying
GCLK / 65; **P14**, already an output in the 58503A (a passthrough of
P17), now carries a pulse on alternate seconds.

Package pin P*n* and die pad PAD*n* are different numberings: P19 is
PAD89, and PAD19 is P78.  The prose names package pins; the pads are
in the tables.

## Method and confidence

How the designs are decoded and simulated, and how far either can be
trusted, is `fpga-comparison.md`'s ("Provenance"), as are the glossary
and the confidence words.  Every net here is **read** from the
netlist `tools/fpga/netlist.py` writes (`make -f tools/fpga/Makefile
target/fpga/58503b.v`); statements marked **simulated** are printed by
Icarus Verilog running it, driven by `tools/fpga/58503b_tb.v`;
**inferred** is reasoning from those.

    make -f tools/fpga/Makefile 58503b                            # decode, build, the fast run (+seconds=0)
    make -f tools/fpga/Makefile check-readings-58503b.fast        # +seconds=0 +registers +probe: the preset, the read map, P19
    make -f tools/fpga/Makefile check-readings-58503b.byte_1c     # +seconds=0 +registers +byte=1c +probe: the read map with the interpolator at 0x1c
    make -f tools/fpga/Makefile check-readings-58503b.three       # +seconds=3 +interpolator +probe: three readings, the 1 PPS edges, the tap
    make -f tools/fpga/Makefile check-readings-58503b.freerun_4s  # +freerun=4000000000 +probe: four seconds free, P14
    make -f tools/fpga/Makefile check-readings-58503b.sync1       # +seconds=2 +sync=1 +probe: the sync field (also .sync2, .sync3)
    make -f tools/fpga/Makefile check-readings-58503b.polarity_1  # +freerun=2000000000 +polarity=1 +probe: address 6's DATA0 (also .polarity_2, DATA1)
    make -f tools/fpga/Makefile check-readings-58503b.phase       # +freerun=3500000000 +phase=1500000000 +probe: register 2's bit 0 pulsed at 1.5 s

Every simulated figure quoted here is printed by one of the runs
`make -j16 -f tools/fpga/Makefile check-readings` makes, which diffs
each against `tools/fpga/ref/<image>.<run>.readings`; the runs and their
flags are the Makefile's `READ_` variables, and each run adds +probe.

## Clocks

GCLK (**read**), from P7 through general routing as in the
Z3805A (`z3805a-trace.md`, "Clocks"); the testbench drives it at
10 MHz.  The blocks clocked from logic are named above.

## Register port

**Read interface:** the Z3805A's, on the same pins
(`z3805a-trace.md`, "Register port").

**Write map:** addresses 0, 1, 2, 3 and 6 (**read**): the Z3805A's
registers 0, 1 and 3 (ADDR2 Low), and two it lacks.  DK.Y
(`F = FL_Y·~PAD35_I·PAD33_I·~PAD32_I`) is the write decode of address
2, ADDR2 and ADDR0 Low, ADDR1 High: GH.QX holds DATA0 (P71), and GH.Y,
a one-clock pulse on its rise, resets FD, the alternate-second phase
control ("The 1 PPS divider").  DL.Y
(`G = FL_Y·PAD35_I·PAD33_I·~PAD32_I`) is the write decode of address
6, ADDR2 High, ADDR1 High, ADDR0 Low, and loads **FF**: FF.Y latches DATA0 (P71) and FF.X DATA1 (P70).  FF.Y
selects P83's polarity and FF.X P14's ("The 1 PPS divider").  The
Z3805A's write decodes all take ADDR2 Low.  The firmware never writes
address 6 (**read**): `fpga_write` (`0x446f8`) and the read at
`0x4470c` are its only references to `0x304000`, and every caller of
`fpga_write` takes its register from the bit table at `0x45aaa`,
registers 0 to 3, or loops over 0 to 3; so under the firmware FF stays
at its reset 0.  The table is the Z3805A's six entries with two
inserted: register 2, mask `0x01` (entry 5), and register 3, mask
`0x04` (entry 7), with the sync field entry 6 (register 3, mask
`0x03`).  Register 3's bit 2 is DATA2 (P67), whose I/O block is an
output only, so it reaches no logic, as in the 58503A
(`58503a-z3801a-trace.md`); `0x4479c` writes it beside two bits of port F
(`0xfffa19`).  Register 2's bit 0 is FD's reset ("The 1 PPS
divider").  The runs that write address 6 and pulse register 2's bit 0
are `58503b.polarity_1`, `.polarity_2` and `.phase`; the others write
register 2 as 0 and leave address 6 alone.

**Read map** (**read**), the Z3805A's (`z3805a-trace.md`, "Register
port").  Register 1's bits 3 to 7 are the read address, bits A0 to
A4: FI.X (DATA3, P65), FI.Y (DATA4, P62), FG.X (DATA5, P60), FG.Y
(DATA6, P59) and GL.Y (DATA7, P58), each a flip-flop on GCLK that loads
on DL.X, the register-1 write decode (`FI.F = ~DL_X·QX + DL_X·PAD36_I`,
and the same form for the others).  EB and EC decode A0 and A1:
EC.Y is 0, EC.X 1, EB.Y 2 and EB.X 3 (`EB.F = FI_Y·FI_X`,
`EB.G = FI_Y·~FI_X`, `EC.F = ~FI_Y·FI_X`, `EC.G = ~FI_Y·~FI_X`).
HL.X (`F = ~PAD24_I + PAD39_I`) is the bus's 3-state control: it
drives while P75 is High and P61 Low.  The data bits:

| Bit | Pin | Net | I/O block |
| --- | --- | --- | --------- |
| 0 | P71 | CK.X | registered, on AL.Y |
| 1 | P70 | CK.Y | registered, on AL.X |
| 2 | P67 | EG.X | registered, on AL.Y |
| 3 | P65 | EG.X | registered, on AL.X |
| 4 to 7 | P62, P60, P59, P58 | IK.X, IK.Y, IL.X, IL.Y | unregistered |

So bits 0 and 1 share a source when A4 is set, and bits 2 and 3 always
do; they differ by when the I/O blocks latch it.  AL is a two-bit
Johnson stage on GCLK (`QX <- ~QY`, `QY <- QX`, `AL.X = ~QY`,
`AL.Y = QY`): AL.X is AL.Y's complement, a cycle of four GCLKs,
High for two.  Every source that feeds DF and EG takes AL.Y and
gives a two-bit digit's low bit while AL.Y is Low and its high bit
while it is High, so the flip-flops that latch on AL.Y's rise (bits 0
and 2) take the low bit and those that latch on AL.X's rise (bits 1
and 3) the high bit.  A count stage's readout, for one, is
`F = AL_Y·QY·EN + ~AL_Y·~(QX ^ QY)·EN`, EN its address decode.  That
each latch takes the half before its edge, not after, is the model's
order and **inferred** for the die: the data's path (the readout, CE
or DE, DF, CK) is longer than the clock's.

- **Bits 0 and 1**, CK (`CK.F = CJ_X·~GL_Y + DF_X·GL_Y`,
  `CK.G = AH_Y·~GL_Y + DF_X·GL_Y`): with A4 clear the stop bit CJ.X
  and the start bit AH.Y; with A4 set DF.X, at both phases.
- **Bits 2 and 3**, EG.X, which does not take A4, so `0x00` to `0x07`
  read the bits 2 and 3 of `0x10` to `0x17`.
- **DF and EG** (`DF.X = ~FG_Y·(CE_X + FG_X·DE_X)`,
  `EG.X = ~FG_Y·(CE_Y + FG_X·EF_X)`): A3 zeroes both, so `0x08` to
  `0x0f` read only the status bits and `0x18` to `0x1f` read 0.  With
  A2 clear CE selects (`CE.X = ~FG_X·(BD_X + AC_Y)`,
  `CE.Y = ~FG_X·(CC_X + AB_Y)`); with A2 set DE
  (`CD_X + DD_X + CB_X + ED_X`) and EF (`DC_X + EE_X + DB_X + BE_X`),
  ORs of count readouts that each enable at one A1 A0.
- **AB and AC**, with AD and AE before them, take the interpolator's
  pads, complemented, at A1 A0 0 and 1, and BC.X and BB.X, the count's
  bits 0 to 3, at 2.

What each address returns, a bit's source at the AL.Y-rise and
AL.X-rise latches; the count's bits are the reading's, each two-bit
stage reading (n + 1) mod 4 (`z3805a-trace.md`, "The count"), and the
interpolator's are its pads, complemented:

| Address | Bits 0, 1 | Bits 2, 3 |
| ------- | --------- | --------- |
| `0x00` | stop CJ.X, start AH.Y | ~P5, ~P6 |
| `0x01` | stop, start | ~P10, ~P11 |
| `0x02` to `0x07` | stop, start | count bits 2 and 3 (BC), 6 and 7 (CC), 10 and 11 (EE), 14 and 15 (DB), 18 and 19 (DC), 22 and 23 (BE) |
| `0x08` to `0x0f` | stop, start | 0 |
| `0x10` | ~P3, ~P4 | ~P5, ~P6 |
| `0x11` | ~P8, ~P9 | ~P10, ~P11 |
| `0x12` | count bits 0, 1 (~CF.X, BB.QY) | 2, 3 (BC) |
| `0x13` | 4, 5 (BD) | 6, 7 (CC) |
| `0x14` | 8, 9 (ED) | 10, 11 (EE) |
| `0x15` | 12, 13 (DD) | 14, 15 (DB) |
| `0x16` | 16, 17 (CB) | 18, 19 (DC) |
| `0x17` | 20, 21 (CD) | 22, 23 (BE) |
| `0x18` to `0x1f` | 0 | 0 |

Bits 4 to 7, at every address, are a seconds count modulo 16: IK and
IL, a four-bit binary counter on GCLK enabled by HH.Y, the 1 PPS
divider's terminal, with no reset but the global one (`IK.F = ~QX`,
`IK.G = QX ^ QY`, IL carrying on IK.X·IK.Y).  The status reads `0x06`,
`0x16` and `0x26` as each of three seconds' start bits sets
(`check-readings-58503b.three`).  Which interpolator output each of P3
to P11 carries is a board fact; the testbench drives them as its byte's
bits 0 to 7, and the firmware's view below takes them so.

**Simulated** (`check-readings-58503b.fast` beside
`check-readings-58503a-z3801a.fast`): both images read, under the
preset, the count `0x555555` and the status `0x04`, and both give the
identical read map over all 32 addresses: the status at `0x00` to
`0x07` (`0x08` at `0x01`), 0 at `0x08` to `0x0f`, the interpolator's
byte complemented at `0x10` and `0x11` (with the byte at `0x5a`, `0x05`
at `0x10` and `0x0a` at `0x11`), the count's nibbles (`0x05`) at `0x12`
to `0x17`, and 0 at `0x18` to `0x1f`.  `0x5a`'s nibbles are each
other's complement, so that run alone does not tell the complemented
low nibble at `0x10` from the high nibble as it stands;
`check-readings-58503b.byte_1c`, the byte at `0x1c`, does: `0x03` at
`0x10`, `0x0e` at `0x11`, `0x00` at `0x00` and `0x0c` at `0x01`, as
the table gives.  So the count reads out the same way in both, and the
58503B's count, read port and interpolator interface read out as the
58503A's (`58503a-z3801a-trace.md`; the bitstream the 58503A and
Z3801A share is in `outputs.md`, "The FPGA bitstreams").

**The firmware's reads** (**read**, the 58503B 1.01.04 image with
`m68k-linux-gnu-objdump -D -b binary -m m68k:cpu32`; the image has no
names file, so the routines are named for the Z3805A's in
`counter.md`, "The register port", whose pattern each repeats).  The
read at `0x4470c` puts the address into register 1's bits 3 to 7 (its
field entry at `0x45aba`: register 1, mask `0xf8`), reads `0x304000`,
and writes register 1 back; `0x44ba8`, the byte read, reads reg + 1
then reg and keeps each low nibble, reg + 1's high.  Its callers read
`0x16`, `0x14` and `0x12`, high byte to low (`0x44c08`, the count),
the byte at `0x10` complemented (`0x44c58`, the interpolator), `0x00`
up to 512 times (`0x4491c`, the ready wait), and `0x00`'s bits 4 to 7
until two reads agree (`0x447fc`, the seconds); a loop at `0x44860`
steps register 1's address field through all 32.  Each read is a row
of the table above, and none of them reads `0x01` to `0x0f`.

## The count

The Z3805A's kind, a 24-bit ripple chain in rows A to E (**read**).
Bit 0 is **CF.X**, a GCLK toggle (`K = GCLK`,
`F = ~EI_X·QX·DG_Y + ~EI_X·QX·~AI_Y + ~EI_X·~QX·~DG_Y·AI_Y`): it
toggles while the armed gate AI.Y is High and the stop DG.Y Low, and
the preset EI.X clears it on the next clock.  Bit 1 is **BB.QY**, a
single flip-flop toggling on CF.X's rise (`K = CF_X`, `G = ~QY`, no
`K_INVERT`).  Bits 2 to 23 are eleven two-bit Johnson stages, BC, BD,
CC, ED, EE, DD, DB, CB, DC, CD and BE, each `QX <- QY`, `QY <- ~QX`,
clocked on the falling edge (`K_INVERT`) of the previous stage's Y.
BB and the eleven reset on **EI.X**, the preset: EI (`K = GCLK`,
`F = ~EL_Y·QX + EL_Y·PAD42_I`) loads register 0's bit 6, the preset
(DATA6, P59, PAD42), on EL.Y, the register-0 write decode.  EI's other
half, EI.Y (`G = ~EL_Y·QY + EL_Y·~PAD30_I + EL_Y·~PAD29_I`), loads
from the mode bits (DATA0, DATA1 on P71, P70) and is High unless the
mode is 3; it is AH's data.

**CG** is not part of the count: CG.X and CG.Y latch DATA0 and DATA1
on DL.X, the register-1 write decode (`CG.F = QX·PAD29_I +
QX·~DL_X + PAD29_I·DL_X`), register 1's bits 0 and 1; CG.X feeds the
armed gate AI.  The read multiplexer is CK for bits 0 and 1 and EG
for bits 2 and 3 ("Register port").

## Start, stop and status bits

- **Start.**  The GPS 1 PPS on **P82** (`PAD15_I`) clocks **AH**
  directly (`K = PAD15_I`, `DI = EI_Y`, `RD = CF_Y`, CF.Y being EI.X a
  clock later), the start capture; AH.Y is the start bit (status
  bit 1).  **DH** (`K = GCLK`, `DI = PAD15_I`) is the companion GCLK
  capture of the GPS edge.  **AI** (`K = GCLK`, `RD = EI_X`) is the
  armed gate.
- **Stop.**  The tap is **HG.Y**, as in the Z3805A, through **EH**, a
  buffer (`F = HG_Y`, combinational).  **CI** (`F = AH_Y`, `K = EH_Y`,
  `RD = EI_X`) latches the start bit when the tap rises.  **CJ.X**
  (`F = BH_X + CI_X`) is a combinational OR, the stop bit, status
  bit 0, so the stop bit sets with the tap.  The count stops four GCLK
  registers later: CJ.QX samples that OR, then CJ.QY, DG.QX and DG.QY,
  and DG.Y holds CF.  So, as in the Z3805A (`z3805a-trace.md`), the
  count's last tick is on the fourth GCLK edge after the tap and it
  holds from the fifth (**read**).
- **Status.**  **CK** (`F = CJ_X·DF_X + CJ_X·~GL_Y + DF_X·GL_Y`, `G`
  over `AH_Y`) puts the stop and start bits (CJ.X, AH.Y) on bits 0
  and 1 while GL.Y, the read address's bit 4, is Low, and DF.X, the
  addressed nibble's source, while it is High ("Register port"), so
  the start bit sets at the
  GPS edge and the stop bit at the tap: the order `z3805a-trace.md`
  finds for the Z3805A.

**The tap** (**simulated**, `check-readings-58503b.three`): EH.Y rises
399,999,901 ns after P83 each second, and CI.X a nanosecond later.
EH.Y rises a nanosecond after the Z3805A's HG.Y, EH's block delay, so
the tap is where the Z3805A's is; it is P83 that rises a clock later
than the Z3805A's, through one more register ("The 1 PPS divider").
The count with the GPS 1 PPS at 0.250000037 s reads 1,500,012 over the
preset, the Z3805A's reading.

**The calibration** (**inferred**).  The tap and the count are the
Z3805A's and P83 is a clock, 100 ns, later, so measured from this
design's own P83 the count's constant is 100 ns less than the
Z3805A's: 1,500,012 ticks, 0.1500012 s, is 0.4000002 s +
(0.000001052 − 0.250000037) s here, P83 0.000001052 s into the second,
and 0.4000003 s + (0.000000952 − 0.250000037) s in the Z3805A, its P83
0.000000952 s in (both `three` runs).  The 58503B's calibration,
0.400000204 s (`counter_configuration_init`'s double at `0x44f40`,
`0x3fd9999a74a4b16a`; **read**), is that 0.4000002 s and 4 ns; the
Z3805A's, 0.4000003 s (`0x40f2c`), is its own constant.  So the
100 ns between the two images' calibrations is the 58503B's extra
register before P83, and both firmwares measure TI against the 1 PPS
on the pins.  The 4 ns is under a tick, below what the count resolves;
the netlist cannot place it (open).

## The 1 PPS divider

The divider runs on GCLK (**read**).  Its stages are the Z3805A's
block by block (`z3805a-trace.md`, "The 1 PPS divider"): GI, the
prescaler IG, JH, IH and JI with II and HI, FJ, KH to KK with JJ and
IJ, JK, KF and KG, JG, GE and GF, and HF, GG, HG and HH each give
every output the same function of the same nets as the Z3805A's
block, with the same flip-flop data, clock, clock enable and reset
(**read**, each pair of equations compared as truth tables over every
input; `make -f tools/fpga/Makefile check-blocks` checks it, the pairs
and the renaming in `tools/fpga/58503b-z3805a.match`, which also pairs
what these blocks read from outside them, back to the pads: DH with
its flip-flops exchanged, and EL, FL, FK and HL, the register-3 write
decode and strobe),
under two renamings: HH's outputs are exchanged, the terminal on JG.X
being HH.Y here and HH.X there; and **LJ** (`F = JI_X`, registered on
GCLK) is the Z3805A's HJ, the prescaler's terminal a clock later into
FJ (`F = EJ_Y + LJ_Y`), the flip-flop the 58503A moves to its HK
(`58503a-z3801a-trace.md`); the pair is in the match file too.  So the divide is the Z3805A's: GI by 4,
the prescaler to 38, 625 clocks in 4, KH to KK by 64, the three
decades, HH.Y every 10,000,000 clocks, and the tap HG.Y the top
decade's states 4 to 7.  Its terminal is **HH.Y**
(`G = HF_Y·GG_X·HF_X·JG_X`, combinational); HH.X
(`F = HF_Y·GG_X·HF_X·JG_Y`) is the same product on JG.Y, to GJ.
**HK** (`F = HH_Y + ~HJ_X·FH_X + ~KH_X·FH_X`, combinational) is a
set-and-hold: HH.Y sets HK.X, FH.X, HK.X registered, holds it, and
HJ.X·KH.X clears it, which sets the pulse's width (HJ
`F = IG_Y·GI_Y·JH_Y·IH_X`, registered on GCLK; KH a two-bit stage,
`EC = FJ_X`, `RD = EJ_X`).  No stage comes between HH.Y and HK: HH.Y
is an input of HK's look-up table.  HK is the 58503A's EI
(`HH_X + ~HJ_X·PAD14_I + PAD14_I·~KH_X`) with HH.Y for the terminal
and FH.X for P83's own pad, each the 1 PPS a register after the latch,
and HJ is the 58503A's HJ, so the width is the 58503A's, 26,000 ns
(below), where the Z3805A's is 31,400 ns (**read**; `check-blocks`
checks HK against the 58503A's EI, and HJ and DH, below, against its
own, `tools/fpga/58503b-58503a-z3801a.match`).
HK.X drives two blocks, FH and HD, and through them three registered
output pins, all registered in the I/O block on GCLK (**read**):

| Pin | Pad | Output net | Registered | Role |
| --- | --- | ---------- | ---------- | ---- |
| P83 | PAD14 | FH.Y | yes (top-left) | internal 1 PPS, FH.QX XOR FF.Y (`FH.G = QX·~FF_Y + ~QX·FF_Y`, combinational) |
| P78 | PAD19 | FH.X | yes (top-left) | internal 1 PPS (`FH.F = HK_X`, registered on GCLK) |
| P14 | PAD95 | HD.Y | yes (left-upper) | a pulse on alternate seconds (`HD.F`, combinational) |

**P83 and P78.**  FH.QX registers HK.X on GCLK and drives P78.  P83 is
FH.QX XOR FF.Y, a polarity select the CPU writes at address 6
("Register port"); at FF.Y's reset 0, P83 is P78.  Either pin is two
registers after HK.X, FH and the I/O block, where the Z3805A's P83 is
one (its EI.Y is combinational): the clock P83 lags the Z3805A's.

**P14.**  HD.QX registers HK.X on GCLK (`HD.DI = HK_X`), and HD.Y is
combinational,
`F = ~QX·FF_X + FD_Y·QX·PAD95_I + ~FD_Y·QX·~HE_X·~FF_X + ~FD_Y·QX·HE_X·PAD95_I`,
over HD.QX, FD.Y, HE.X (HD.QX a clock later), FF.X and P14's own pad.
**FD** (`F = QY·~HD_X + QY·HE_X + ~QY·HD_X·~HE_X`) toggles on each
rise of HD.QX: a divide by 2 of the second.  With FF.X Low,
`HD.Y = QX·(PAD95_I + ~FD_Y·~HE_X)`: P14 rises a clock after HD.QX
only when FD.Y is Low, before FD toggles, then holds itself High
through its own pad until HD.QX falls.  So P14 carries the 1 PPS pulse on
alternate seconds.  With FF.X High it idles High and pulses Low on the
same seconds (**read**).  FD's reset is GH.Y (`G = GH_X·~HE_Y`), a
one-clock pulse when GH.X, register 2's bit 0 (DATA0 on DK.Y, the
address-2 write decode), rises: the CPU sets which seconds by writing
that bit High.

**The firmware's pulse** (**read**).  `0x4af88`, called from
`0x22f38`, writes register 2's bit 0 High and then Low (`fpga_set_bit`,
`0x449a6`, entry 5, 1 then 0) when bit 0 of `0x21200`'s result is set
and the byte at `0x1032a9` is not 0.  `0x21200` takes a date and time
(`0x100f2e`, `0x100f32`) to seconds from 1980: it rejects a year
before 1980 and multiplies days by 86,400.  So, while that byte is
set, the firmware resets FD in a second whose count is odd, and P14
then pulses at the next internal 1 PPS (the `phase` run below): P14's
alternate seconds tied to the count's parity, a reset that changes
nothing once FD is in step (**inferred**).  What sets `0x1032a9`, and
in which second `0x22f38` runs, are not traced.

**The controls** (**simulated**, free runs with the bus quiet but for
the one write):

- `58503b.polarity_1`, address 6 written `0x01` (DATA0, FF.Y) just
  after RESET: P83 rises 1,552 ns after time 0, with the write, idles
  High, and is Low 26,000 ns from 1.000001052 s; P78 and P14 rise at
  1.000001052 s as in the free run.  So FF.Y inverts P83 alone.
- `58503b.polarity_2`, `0x02` (DATA1, FF.X): P14 rises with the write
  and idles High, Low 26,000 ns from 1.000001052 s, while P83 and P78
  rise at 1.000001052 s.  So FF.X inverts P14 alone, on the same
  seconds.
- `58503b.phase`, register 2's bit 0 pulsed at 1.5 s: GH.Y, FD's
  reset, is High 101.0 ns from 1.500001452 s, and P14 rises at
  1.000001052 s and 2.000001052 s and not at 3.000001052 s, where in
  the free run (`58503b.freerun_4s`) it rises at 1 s and 3 s: the
  reset after the first second's pulse moves the alternate seconds by
  one.

**The divide** (**simulated**, `check-readings-58503b.three`): P83 and
P78 rise together exactly 1,000,000,000 ns apart, the first at
1.000001052 s, a clock after the Z3805A's first, a divide by
10,000,000, and P83 stays High 26,000 ns.  That run ends at 2.40 s,
with two internal 1 PPS edges, and P14 rises at the first only
(1.000001052 s).

**P14 free** (**simulated**, `check-readings-58503b.freerun_4s`: four
seconds with no bus traffic, the registers at their reset 0): P14
rises at 1.000001052 s and 3.000001052 s, 2,000,000,000 ns apart, and
at no other second, with P83 rising at each of 1, 2 and 3 s; it is
High 26,000 ns each time, P83's own width, and Low between (the ~P14
probe's High times, 1,000,001,051 and 1,999,974,000 ns).

**Model hazards.**  The probe of HK.X in `check-readings-58503b.three`
removes 2 pulses under 5 ns, the first at 0.900000954 s, one a second
(four in the four-second free run): hazards of the 1 ns-per-block model on the
combinational HH.Y and HK.X, not a second pulse (**inferred**).
HK.X's loads, FH and HD, sample it on GCLK.

## The sync field

**The firmware's writes** (**read**, the 58503B 1.01.04 image; the
routines named for the Z3805A's, `counter.md`, "The sync field", whose
pattern each repeats).  `counter_set_sync` (`0x44a86`) writes register
3 twice through the bit table's entry 6, register 3 mask `0x03`: the
field 0, then the kind.  `counter_arm` (`0x44c6c`) calls it, with the
preset on, on each arm while `sync_request` (`0x44a66`) has a sync
wanted, then turns the preset off.  The kinds are the Z3805A's:
`fine_sync_adjust` (`0x49f76`) asks for kind 1 for a TI of 100 ns to
300 ns and kind 2 for one of −100 ns to −300 ns (its constants 1e-7 and
3e-7, `0x33d6bf95` and `0x34a10fb0`), and `0x4a0a6`, `0x4a1ca`,
`0x4a238` and the console's `0x2b3d4` ask for kind 3; `0x2b3c0` passes
its arguments through.  `counter_arm` also sets register 1's reference
bit on every arm (entry 3, at `0x44cf6`), which the Z3805A's leaves to
the last interpolator measurement; the testbench's arms have it High
too.

**The logic** (**read**): the Z3805A's (`z3805a-trace.md`, "The sync
field"), block by block.  EL.F, the register-3 write decode
(`F = ~PAD35_I·PAD32_I·PAD33_I·FL_Y`, ADDR2 Low, ADDR1 and ADDR0
High), loads DI with DATA0 and DATA1 (P71, P70), DI.Y and DI.X; DJ
follows a clock behind; EK, the pending sync, sets on the field's
change from 0 and clears on the terminal HH.Y or the restart EJ.Y;
GJ.Y and GJ.X (`HH_X·EK_X·JK_X·DI_Y`, `HH_X·EK_X·JK_X·DI_X`), kind 1's
and kind 2's step lines, into GK, the prescaler's carry; EJ.X
(`DH_X·EK_X·DI_X·DI_Y`), kind 3's restart on the GPS edge, resets the
divider's stages, and EJ.Y, through FJ, its prescaler.  DI, DJ, EJ,
EK, GJ and GK give each output the Z3805A's function of the same nets,
with HH's outputs exchanged ("The 1 PPS divider"; `check-blocks`): EK clears on HH.Y
and GJ takes HH.X here, the Z3805A's HH.X and HH.Y.  DH, the GPS
1 PPS's rising-edge detector, is the 58503A's, which is the Z3805A's
with its two flip-flops exchanged (`58503a-z3801a-trace.md`, "The sync
field"; `check-blocks` pairs it both ways).  So a sync acts once, at
the next internal 1 PPS or GPS edge, and register 3 left at its kind
does nothing more, as EK's equation, the Z3805A's, says
(`z3805a-trace.md`, "The sync field"; **read**).

**Simulated** (`check-readings-58503b.sync1`, `.sync2` and `.sync3`:
`+seconds=2 +sync=KIND`, the field written 0 then the kind on the
first arm, as `counter_arm` does), the Z3805A's steps, each pin a
clock after the Z3805A's as in `58503b.three`:

- **Kind 1** moves the next internal 1 PPS one clock, 100 ns, earlier:
  P83, P78 and P14 rise at 1.000000952 s, where `58503b.three`'s rise
  at 1.000001052 s, and the count after it reads 1,500,011 over the
  preset for 1,500,012; the tap follows, 999,999,900 ns after the
  first (EH.Y's period).  GJ.Y is High once, 15,498.0 ns from
  0.999985355 s (one pulse under 5 ns removed, at 0.999750954 s, a
  model hazard), and EK.X 999,940,000.0 ns from 0.000060852 s, until
  that terminal clears it.
- **Kind 2** moves it one clock later: the pins at 1.000001152 s, the
  count 1,500,013; GJ.X High once, 15,698.0 ns from 0.999985355 s (one
  pulse under 5 ns removed, at 0.999750954 s); EK.X High
  999,940,200.0 ns.
- **Kind 3** restarts the divider on the next GPS 1 PPS: EJ.X is High
  100.0 ns, 115.0 ns after the GPS 1 PPS at 0.250000037 s, and EK.X
  clears with it (High 249,939,300.0 ns).  No internal 1 PPS comes at
  1.0 s; the pins rise at 1.250000252 s, 215.0 ns after that second's
  GPS 1 PPS, where the Z3805A's rise 115.0 ns after it, and both counts
  read 4,000,004 over the preset.

These are the figures of `58503a-z3801a.sync1` to `.sync3` with every
pin edge 100 ns later.  So the 10 MHz designs' sync field steps the
1 PPS a clock either way and restarts it on a GPS edge in the 58503B
as in the Z3805A and 58503A.  What a field changed from 3 does while
it holds the divider, open in the Z3805A, is open here too.

## Other outputs

**P19 is a new I/O block, but not a 1 PPS output.**  P19 (PAD89) is a
new I/O block, absent from the 58503A (which has no PAD89), but it is
an output, unregistered, driven by GB.Y.  GB (`K = GCLK`, `EC = GD_Y`)
is a two-bit counter stage in a small GCLK divider GA, GB, GC, GD (GA a
two-bit binary counter enabled by GD.Y, `GD = GC_X·GC_Y`, GC off FB.X), so GB.Y is a
mid-divider tap, not the terminal.

**The divide is GCLK / 65** (**simulated**,
`check-readings-58503b.fast`, whose probe prints P19's period): P19
rises every 6500 ns, and GCLK is 100 ns (`CLOCK_PERIOD` in
`58503b_tb.v`), so the GA, GB, GC, GD counter divides GCLK by 65 (6500 /
100), repeatedly, while the 1 PPS pins rise once a second.  65 = 5 x 13.
The one-second divide is 10,000,000 (a 10 MHz GCLK = 2^7 x 5^7), and
65 does not divide it (13 is not a factor), so P19 is a free-running
÷65 tap, not phase-locked to the 1 PPS, structurally distinct from the
HK.X 1 PPS path and not a sub-harmonic of the second (**inferred**).
P19 is High 3200 ns of its 6500 (**simulated**, the same run).  What
P19 drives on the board (a ~153.8 kHz reference at a 10 MHz GCLK)
stays open, a board fact the netlist cannot settle.

**The rest** (**read** from the I/O blocks), beside the Z3805A's:

| Pin | 58503B | Z3805A |
| --- | ------ | ------ |
| P2 | CA.X, inverted | BA.X, inverted |
| P16 | P82 (the GPS 1 PPS) passed through | the same |
| P72 | AH.Y | AH.X |
| P76, P77 | BL.Y, BL.X, inverted | the same |
| P79 | BJ.X, three-state | the same |
| P81 | AH.X, inverted | AH.Y, inverted |
| P84 | P17 passed through, inverted | the same |

P72 and P81 swap AH's two outputs; in the 58503B AH.X and AH.Y are
both combinational (`AH.F = ~BJ_Y·QX·~AI_Y + ~BJ_Y·~AI_Y·BH_Y`,
`AH.G = QX + BH_Y`), so what either pin means on a board is open.

## Confidence

**Simulated** (`58503b_tb.v`, the runs above): the preset read
`0x555555` and status `0x04`, and the full read map `0x00` to `0x1f`,
identical to the 58503A/Z3801A's, and with the interpolator's byte at
`0x1c` its byte complemented, low nibble first; P19 rising every 6500 ns, GCLK / 65;
the divide by 10,000,000 on P83 and P78; the tap EH.Y 399,999,901 ns
after P83; the readings, the Z3805A's; P14 pulsing on alternate
seconds in a four-second free run; sync kinds 1 and 2 stepping the
internal 1 PPS a clock earlier and later, and kind 3 restarting it on
the GPS 1 PPS, the 58503A's results a clock later on the pins; address
6's DATA0 inverting P83 alone and DATA1 P14 alone; register 2's bit 0,
pulsed, moving P14's alternate seconds by one.

**Read** (from the netlist): the count is CF.X, BB.QY and eleven
two-bit ripple Johnson stages in rows A to E, reset by the preset EI.X
(register 0's bit 6); CG is register 1's bits 0 and 1, and FI, FG and
GL its bits 3 to 7, the read address; the read map, bit by bit:
CK and EG, latched on AL's two phases, carry a digit's low and high
bits, and IK and IL, the seconds, bits 4 to 7; the divider's
stages, the Z3805A's block by block under two renamings (HH's outputs
exchanged, LJ for HJ); the terminal HH.Y straight into the latch HK,
the 58503A's EI with FH.X for P83's pad; then FH and HD drive P83,
P78 and P14, all registered in the I/O block on GCLK; P83 is FH.QX XOR FF.Y; P14 is
gated by FD's divide by 2 (reset from register 2's bit 0) with FF.X
its polarity; FF is a register at address 6 the Z3805A lacks; P19 is a
new unregistered output on the mid-divider tap GB.Y; the tap HG.Y,
through EH, clocks CI, CJ.X gives the stop bit (0) and AH.Y the start
bit (1), and the count's last tick is four GCLK registers after the
tap; the sync field's blocks, the Z3805A's.  From the firmware: it
writes sync kinds 1, 2 and 3 to register 3's bits 0 and 1, pulses
register 2's bit 0 on a condition, writes register 3's bit 2, which
reaches no logic, and never writes address 6.

**Inferred:** the calibration's 100 ns below the Z3805A's is the extra
register before P83.

**Untested:** a sync with the GPS 1 PPS after the tap, and kind 3
with the internal 1 PPS before the GPS edge, which the Z3805A's
`z3805a.sync_3_late` run and the emulator's differential cases (coming
soon) cover and these runs do not;
P14 inverted over more than one internal 1 PPS.

**Open:**

- P19's board purpose;
- the calibration's 4 ns over 0.4000002 s, under a tick, which the
  netlist cannot place;
- what P14's alternate-second pulse, and the polarity selects at
  address 6, do on a board;
- what sets the byte at `0x1032a9` under which the firmware pulses
  register 2's bit 0, and in which second `0x22f38` runs;
- what a sync field changed from 3 does while it holds the divider;
- which interpolator output each of P3 to P11 carries, and that on the
  die each bus latch takes the AL half before its edge, as the model
  does;
- the 58503B firmware's preset constant at the grain the Z3805A's
  is read (the FPGA reads `0x555555`);
- everything on a board, and the pins' roles, read from the logic.
