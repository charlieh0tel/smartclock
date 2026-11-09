# The Z3816A FPGA

Traced against the Z3805A's design (`z3805a-trace.md`), the reference.
It shares its count chain, its 1 PPS chain and the counting stages of
its P7 divider with the Z3815A's (`z3815a-trace.md`), under the same
tile names; the two differ in how the P7 divider rearms (KD here, GE.DI
in the Z3815A), in the start capture (P82 straight into BH here, a
three-way source select in the Z3815A) and elsewhere.
`python3 -I tools/fpga/tilediff.py target/fpga/z3815a.json
target/fpga/z3816a.json` lists every block configured otherwise, not
all of them different in function (`z3815a-trace.md`).

## At a glance

| | |
| --- | --- |
| Images that load it | Z3816A 4001 |
| Bitstream | `third_party/firmware/images/fpga/z3816a.bit` |
| Part | XC3042, PLCC84 |
| Logic blocks, I/O blocks, nets | 116 of 144, 41, 207 (`tools/fpga/ref/z3816a.stats`) |
| Clocks | ACLK from P53; GCLK, ACLK halved; P7 through general routing |
| Oscillator the divide implies | 9.8304 MHz GCLK (a divide by 9,830,400), P53 19.6608 MHz, locked to a 10 MHz P7 (**inferred**) |
| 1 PPS output pins | P83 every second; P14 and P28 every other second, active Low; P20, a one-ACLK pulse ahead of P83 |
| Other outputs | P38 and P40 (a phase-frequency detector), P51 and P57, P16 (the GPS 1 PPS passed through), P2 (tied Low) and more (`Other outputs`) |
| Differences from the reference | two clocks; a 9,830,400 divide, an ACLK prescaler and a GCLK chain; a PLL's phase-frequency detector and its P7 divider; sync kinds 1 and 2 step the P7 divider, not the 1 PPS; kind 3 makes the GPS 1 PPS the internal 1 PPS |

The count is the Z3805A's kind, a ripple Johnson chain started from
GCLK whose preset reads `0x555555`, started by the GPS 1 PPS and
stopped by a tap of the 1 PPS divider.  A second input, P7, clocks a
separate divider, P7 / 6250, which with the 1 PPS divider's own
prescaled tick, GCLK / 6144, makes a phase-frequency detector out on
P38 and P40: a PLL's, locking the oscillator on P53 to P7
(**inferred**).

## Method and confidence

How the designs are decoded and simulated, and how far either can be
trusted, is `fpga-comparison.md`'s ("Provenance"), as are the glossary
and the confidence words.  Every net here is **read** from the
netlist `tools/fpga/netlist.py` writes (`make -f tools/fpga/Makefile
target/fpga/z3816a.v`), each logic block an instance with its F and G
equations as comments and its inputs named by net (`BA_X` is the net
block BA's X drives).  Statements marked **simulated** are printed by
Icarus Verilog running it, driven by `tools/fpga/z3816a_tb.v`;
**inferred** is reasoning from those.

    make -f tools/fpga/Makefile z3816a                           # decode, build, the fast path
    make -f tools/fpga/Makefile check-readings-z3816a.fast       # +seconds=0 +registers: the preset, the read map
    make -f tools/fpga/Makefile check-readings-z3816a.byte_1c    # +seconds=0 +registers +byte=1c: the read map with the interpolator at 0x1c
    make -f tools/fpga/Makefile check-readings-z3816a.three      # +seconds=3: three readings, the 1 PPS edges
    make -f tools/fpga/Makefile check-readings-z3816a.freerun    # +freerun=2000000 +edges: P38 and P40
    make -f tools/fpga/Makefile check-readings-z3816a.p7_130     # +freerun=5000000 +edges +p7=130
    make -f tools/fpga/Makefile check-readings-z3816a.p7_97      # +freerun=5000000 +edges +p7=97
    make -f tools/fpga/Makefile check-readings-z3816a.osc_48     # +freerun=5000000 +edges +osc=48
    make -f tools/fpga/Makefile check-readings-z3816a.pll        # +freerun=5000000 +edges +osc=50.862630208333
    make -f tools/fpga/Makefile check-readings-z3816a.freerun_4s # +freerun=4000000000 +edges: P14 and P28, P38 and P40
    make -f tools/fpga/Makefile check-readings-z3816a.sync1      # +seconds=2 +sync=1: the sync field (also .sync2, .sync3)

Each `check-readings-z3816a.<run>` runs `vvp -n
target/fpga/z3816a_tb <the run's READ_ flags> +probe` and diffs what it
prints against `tools/fpga/ref/z3816a.<run>.readings`; the flags are
the Makefile's `READ_z3816a.<run>` lines.  Every simulated figure
quoted here is printed by one of these runs.  `+freerun=NS` resets,
then runs NS with no bus traffic, registers at their reset 0; `+p7=NS`
and `+osc=NS` set the P7 and P53 periods (100 and 50 ns, 10 and 20 MHz,
by default); `+edges` prints each P38 and P40 rise.

**Model pulses.**  `probe.v` removes every pulse under 5 ns before it
measures a net, and says how many (`outputs.md`, "Simulation").  Here
that touches KE.Y, whose 1 ns hazard comes once each prescaler cycle
(`check-readings-z3816a.freerun`: "4000 pulses under 5.0 ns removed" in
2 ms, one per rise); JI.Y, the 1 PPS terminal, whose hazard comes
before its rise (`check-readings-z3816a.three`: 2 removed, the first
at 0.884737229 s, once a second); and the lagging side of the phase-frequency
detector, LE.X or LD.Y, whose pulse is the reset's few block delays
(`P38 and P40` below).  BB.Y, a read-multiplexer term, has one in
`z3816a.sync3`.  The
testbench's `+edges` lines are taken on the pins and print every P38
and P40 rise.

**Logic loops.**  Yosys reports two, through LD.QY and through LE.QX
(`tools/fpga/ref/z3816a.structure`, checked by `make -f
tools/fpga/Makefile check`): the detector's two flip-flops, which LD.X
resets and which it reads (`P38 and P40`).

## Clocks

Two clock buffers and a third routed clock, read from the buffer
instances, their periods **simulated** with P53 at 20 MHz and P7 at
10 MHz, the testbench's defaults:

- **ACLK** (`xc3000_buffer ACLK`) is driven straight from `PAD49_I`,
  pin P53, the oscillator: 50 ns period.
- **GCLK** (`xc3000_buffer GCLK`) is driven from AB.Y.  Block AB
  (`F = ~QY`, `K = ACLK_O`) is a lone toggle on ACLK, so GCLK is
  ACLK / 2 (AB.Y's period 100 ns in every run at the default P53).
- **P7** reaches 11 logic blocks through general routing as `PAD7_I`,
  not a clock buffer.  The testbench drives it as its own clock, 10 MHz
  by default; its board source is **inferred** (`P38 and P40`).

By the clock net each block's K pin takes (**read**;
`tools/fpga/ref/z3816a.stats`, `clock_loads`): GCLK clocks 42 blocks
(the register port, the start pipeline, the 1 PPS chain, the count's
tick CG, the read phase AL, LD); ACLK 17 (AB, the 1 PPS divider's
prescaler, KK, KL, LL and JL among its stages, FB for P20, and LK,
behind P51 and P57); P7 11
(CE, DE, EE, FE, GE, HE, IE, JE, KE, LE, KD), the P7 divider; the GPS
1 PPS on P82 two (BH, CH).  The rest are clocked by other blocks'
outputs: the count chain (CG.Y, then each stage), CI by the tap KI.X,
AG by AF.Y.

**The oscillator's frequency.**  The divide, 9,830,400 GCLK clocks a
second, is one second of a 9.8304 MHz GCLK, which is 19.6608 MHz on P53
halved; the phase-frequency detector compares P53 / 12,288 with
P7 / 6250, equal at 19.6608 MHz and 10 MHz (**inferred**).  The
testbench runs GCLK at 10 MHz instead, so its 1 PPS is 0.98304 s.  The
firmware converts the count at 100 ns a tick (`interval.md`), its
conversion scale; at 9.8304 MHz a tick is 101.7 ns, 1.7 % longer.
Which is the board's, and how the firmware reconciles them, is open.
The Z3816A's OCXO is an MTI Series 260, a 5 MHz part (reported by
several sources, not read from a board here), so a 10 MHz on P7 would
be made from it on the board, not be its own output: presumably by a
doubler, on a board that differs from the others' (**inferred**, not
traced).

## Register port

The Z3805A's (`z3805a-trace.md`, "Register port"), pin for pin: P61
the select, P75 R/W, P69, P68 and P66 ADDR0 to ADDR2, the eight data
bits on P71, P70, P67, P65, P62, P60, P59 and P58, P3 to P6 and P8 to
P11 the interpolator's byte (P7 is the second clock), P82 the GPS
1 PPS.  The write decode (**read**): CL (`F = C + ~E`, `G = ~C·~E`,
the select and R/W) gates the bus, and DL (`DI = CL_Y`,
`G = QX·~QY`) makes a one-GCLK write strobe; DJ.Y decodes register 0,
EJ.Y register 1, EJ.X register 2 and DJ.X register 3, each with ADDR2
Low.  What each register holds:

- **Register 0**: EI.Y the preset, bit 6 (`The count`); EI.X
  `~(DATA0·DATA1)`, Low in mode 3.
- **Register 1**: DH bits 0 and 1 (into the start logic, AI, AF and BI);
  FG, EH and DK the read address, bits 3 to 7.
- **Register 2**: EF bit 0, whose rising edge sets P14 and P28's phase
  (`The 1 PPS divider`).
- **Register 3**: GG bits 0 and 1, the sync field (`The sync field`).

**The read map** (**simulated**, `check-readings-z3816a.fast`): the
status `0x08` at `0x00` to `0x0f`, the interpolator's byte
complemented, low nibble first, at `0x10` and `0x11` (`0x05`, `0x0a`
with the byte at `0x5a`; `0x03`, `0x0e` at `0x1c`,
`check-readings-z3816a.byte_1c`), the count's nibbles `0x05` at `0x12`
to `0x17` under the preset, and 0 at `0x18` to `0x1f`: the Z3805A's
map from `0x10` up, and the Z3815A's throughout with its P84 held
Low; below `0x10` the
Z3805A's bits 2 and 3 are those of `0x10` to `0x17` at `0x00` to
`0x07`, and 0 at `0x08` to `0x0f`.

**The read path** (**read**).  CK and FJ are combinational
(`K` unused): `CK.X = DK_Y ? CF_Y : CJ_X`, `CK.Y = DK_Y ? CF_Y : BH_Y`,
`FJ.X = ~DK_Y + DF_X`, `FJ.Y = DK_Y ? DF_X : GH_Y`, where DK.Y is the
read address's bit 4 (register 1 bit 7).  The bus's low four bits are
registered in their I/O blocks, on the two phases of AL, a two-bit
Johnson counter on GCLK: P71 (bit 0) and P67 (bit 2) on AL.Y, P70
(bit 1) and P65 (bit 3) on AL.X, so CF.Y and DF.X carry two bits each,
one per phase.  Bits 4 to 7 (P62, P60, P59, P58) are II and IJ,
unregistered, the seconds count.  CL.X 3-states all eight.

## The count

### What reads out as the count: the ripple Johnson chain

The firmware reads the 24-bit count as six nibbles at `0x12` to `0x17`,
low nibble of each read (`counter.md`, "The register port").  Those
nibbles reach bus bits 0 to 3 through CF.Y and DF.X (**read**): CF, DF,
BE, ED and DD are combinational, and read each stage's read term, a
decode over AL.Y and DB or EB, the address selects from FG: the X
output of each ordinary stage, and the Y output of the mirrored BB and
CD.  BE reads CD.Y and BD.X; AA, AC, AD and AE, unclocked blocks, merge
the interpolator's pins with the head's terms, BA.X for bit 1 (AE) and
BB.Y for bits 2 and 3 (AD).  EH and DK, the address, choose among
them.

So the count the port returns is the **ripple Johnson chain**: CG, BA,
BB, CD, BD, EC, DC, EA, DA, CA, CB, CC, BC, started from GCLK at CG,
the structure `z3805a-trace.md` traces for the Z3805A.  It is not the
P7 divider.

**The preset bit.**  EI (`K = GCLK`, `G = ~A·QY + A·C`, `A = DJ_Y`,
`C = PAD42_I`, P59, DATA6) loads register 0's bit 6 into EI.QY on a
register-0 write: EI.Y is the preset.  EI's F term
(`~A·QX + A·~B + A·~E`, `B = PAD30_I`, `E = PAD29_I`, DATA1 and DATA0)
is the mode-3 flip-flop EI.X, not the preset.  EI.Y resets every
count stage, the start pipeline AF, AG, AI, AJ, AK, BI, BJ, BK, and the
stop CI; it clears the tick CG through CG's own G term (CG has no
reset), and CG.X, EI.Y registered on GCLK (`F = EI_Y`), resets the
start capture BH.  Clearing it lets the next count run.  This is the
preset of `counter.md`'s "What a model needs".

The stages (**read** from the clock and reset nets):

| Bits | Stage | Clock | Clock edge | Reset | Form |
| ---- | ----- | ----- | ---------- | ----- | ---- |
| 0 | CG | GCLK | rising | none (cleared by its G term while EI.Y) | the gated GCLK toggle |
| 1 | BA | CG.Y | rising | EI.Y | one flip-flop, `G = ~QY` |
| 2, 3 | BB | BA.Y | falling | EI.Y | mirrored: QX takes `~QY`, QY takes DI, its own QX |
| 4, 5 | CD | BB.X | falling | EI.Y | mirrored, as BB |
| 6, 7 | BD | CD.X | falling | EI.Y | QX takes DI, its own QY; QY takes `~QX` |
| 8, 9 | EC | BD.Y | falling | EI.Y | as BD |
| 10, 11 | DC | EC.Y | falling | EI.Y | as BD |
| 12, 13 | EA | DC.Y | falling | EI.Y | as BD |
| 14, 15 | DA | EA.Y | falling | EI.Y | as BD |
| 16, 17 | CA | DA.Y | falling | EI.Y | as BD |
| 18, 19 | CB | CA.Y | falling | EI.Y | as BD |
| 20, 21 | CC | CB.Y | falling | EI.Y | as BD |
| 22, 23 | BC | CC.Y | falling | EI.Y | as BD |

- **24 bits: a toggle, a one-flip-flop head and eleven two-bit
  stages.**  Bit 0 is CG.QY, bit 1 BA.QY, bits 2 to 23 the eleven
  two-bit ripple Johnson stages in chain order, as in the Z3805A
  (CF.QY, BB.QY and eleven stages) and the Z3815A.  The read is linear:
  `0x555555` plus the ticks counted, modulo 2^24, read bit 0 the
  complement of CG.QY (**read**).
- **The chain is ripple, not synchronous.**  Each stage clocks the next
  from its own output, and every stage after BA has `K_INVERT`, so it
  advances on the falling edge of the one before.  There is no carry
  net across the stages.
- **Two Johnson forms.**  BB and CD are mirrored; the other nine have
  the form the Z3805A's stages have.
- **The tick, CG, is gated on GCLK.**  CG.Y (`G = ~A·~B·QY + ~A·QY·D +
  ~A·B·~QY·~D`, `A = EI_Y`, `B = AI_X`, `D = DI_Y`) is cleared while
  the preset EI.Y is High, toggles while the start gate AI.X is High
  and DI.Y Low, and holds once DI.Y, the stop, goes High: the Z3805A's
  CF.Y.

**The preset reads `0x555555`** (**simulated**,
`check-readings-z3816a.fast`: "with the preset the count reads
555555", status `0x08`, and `0x12` to `0x17` each read `0x05`), the
same preset the Z3805A reads (`z3805a-trace.md`; `z3815a-trace.md`,
"The count").  The status's `0x08` differs from the Z3805A's `0x04`
because the Z3816A's status bits 2 and 3 are not the interpolator's
(`Start, stop and status bits`).

The testbench arms with the Z3805A's `counter_configuration_init`
registers: the Z3816A image's own are read only at `interval.md`'s
grain.  Whether the Z3816A firmware subtracts `0x555555` in its
`counter_offset_apply`, as the Z3805A's does, is open.

## Start, stop and status bits

The start and stop are the Z3805A's mechanism (`z3805a-trace.md`,
"Start, stop and status bits"), here (**read**):

- **Start.**  The GPS 1 PPS on P82 (`PAD15_I`) clocks BH directly (no
  routing block): BH.QX takes EI.X (`DI = EI_X`, `RD = CG_X`) on the
  GPS edge.  BH.X (`F = ~AI_X·~AJ_Y·(QX + AG_Y)`) is the interval the
  external interpolator measures, out on P81 inverted (`PAD16`,
  `OUT_INVERT`: a Low pulse), and feeds AI and BI.
- **Arm gate.**  AI (`K = GCLK`, `RD = EI_Y`, reading DH.X, AF.X,
  BH.X, BI.Y, AJ.Y) is the start-armed flag; AI.X gates the tick CG.
  AF, AG, AJ, AK, BI, BJ, BK are its pipeline, all reset by EI.Y.
- **Stop capture.**  CI (`F = B = BH_Y`, `RD = EI_Y`) is **clocked by
  KI.X**, the divider tap: when the tap rises, CI captures the start
  state.  CI.X feeds CJ (`QX` takes `AG_X + CI_X`, `QY` takes `QX`,
  `K = GCLK`); CJ.X is the stop bit, and CJ.Y feeds DI (`QX` takes
  CJ.Y, `QY` takes `QX`, `K = GCLK`), whose DI.Y stops CG: DI is the
  stop pipeline, not a register decode.
- **Status bits** (a status read, DK.Y Low).  Bit 0 is CJ.X (CK.X), the
  stop, set at the tap; bit 1 is BH.Y (`G = QX + AG_Y`, CK.Y), the
  start, set at the GPS edge, the order the Z3805A's has.  Bit 2 is
  GH.Y (FJ.Y), a flag the internal 1 PPS terminal JI.Y sets and the
  next prescaler tick GJ.Y clears; bit 3 is FJ.X, `~DK_Y + DF_X`, 1 at
  every status address.  Neither reads the interpolator, so the status
  reads `0x08`, whatever the interpolator's byte (the fast run's
  `0x08`; with the Z3805A's `0x04` its bits 2 and 3 are interpolator
  inputs).  Bits 4 to 7 are the seconds count.

## The 1 PPS divider

The internal 1 PPS leaves on P83 (`PAD14`, `O = FC_X`, output register
clocked by GCLK), on P14 and P28 (`PAD95`, `PAD78`, both `O = IB_Y`,
unregistered) every other second, and on P20 (`PAD86`, `O = FB_Y`,
output register on ACLK's falling edge) as a one-ACLK pulse.

### The chain: an ACLK prescaler and a GCLK divider by 1600

Tracing back from P83 (**read**):

- **The ACLK prescaler.**  EK, EL, GK, GL, HK and IK are clocked by
  ACLK and reset by FK.Y (FK's registered terminal, or the rise of
  JH.X), with HL, JK and KJ, also on ACLK, among their enables, and
  KK, KL, LL and JL, on ACLK, the stages KJ reads (`KJ.F =
  KL_Y·JL_Y·~LL_Y·LL_X`), whose KJ.X is FK's terminal's; FL
  (`FL.Y = EL_Y·GL_Y`) is its terminal.  It
  divides ACLK by 12,288, GCLK by 6144 (**simulated**: P40, which it
  sets, rises every 614,400 ns at the 50 ns ACLK of
  `check-readings-z3816a.freerun` and every 589,824 ns at the 48 ns of
  `check-readings-z3816a.osc_48`).
- **GJ** (`K = GCLK`, `DI = FL_Y`, `G = QX·~QY`) takes FL.Y onto GCLK
  and makes GJ.Y a one-GCLK tick at each prescaler cycle: the
  prescaled tick, which also sets P40.
- **The GCLK divider by 1600.**  LF and KF (`EC = GJ_Y`) count GJ's
  ticks; KG (`F = LF_X·KF_X·GJ_Y·LF_Y·KF_Y`, combinational) is their
  carry into LI and LJ (`EC = KG_X`); KI.Y (`G = LH_X·KG_X`) enables
  KI, KH and LH.  KF, KH, KI, LF, LH, LI and LJ carry `RD = JH_X`; KG
  has no reset.  The chain is synchronous on GCLK, every stage on GJ's
  ticks, unlike the count.
- **JI** (`F = E + A·B·C·D`, `A = LH_X`, `B = KG_X`, `C = LH_Y`,
  `D = KH_Y`, `E = JH_Y`) is the terminal: `JI_Y = JH_Y +
  LH_X·KG_X·LH_Y·KH_Y`, combinational.  JI.Y also enables the seconds
  counter II/IJ and sets GH.Y, status bit 2.
- **GH** (`GH.X = JI_Y + ~GK_Y·FC_X`) passes JI.Y to FC and holds FC.X
  until the prescaler's GK.Y; **FC** (`K = GCLK`, `DI = GH_X`)
  registers it, and P83's output register takes FC.X a GCLK later.

So 9,830,400 = 6144 x 1600: the 1 PPS divide is not all on GCLK, and
ACLK's counter is the prescaler, its first stage.  The tap, `0x3c0000` = 3,932,160 = 6144 x 640, is the GCLK
chain's count 640 of 1600, 0.4 of the divide (**read**, arithmetic).

**P83** (**simulated**, `check-readings-z3816a.three`): P83 rises
every 983,040,000 ns, first at 0.983041328 s, a divide by 9,830,400
(`0x960000`), the Z3815A's.  The tap, KI.X, rises 393,215,900 ns after P83, as the Z3815A's; its
tie to the firmware's 0.3932162 s calibration, which is `0x3c0000`
plus two ticks at the firmware's 100 ns, is the Z3815A's
(`z3815a-trace.md`, "The 1 PPS divider"; **inferred**).

### P14 and P28: every other second

IB and FD make P14 and P28 (**read**): FC.Y (`G = QX·~QY`) is a
one-GCLK pulse at FC.X's rise; FD (`F = QY·~E + ~QY·E`, `E = FC_Y`,
`RD = EF_X`) is a **toggle**, flipping at each internal 1 PPS; IB
(`F = A + ~B + ~QY`, `A = FD_Y`, `B = FC_Y`, `K_INVERT`, so IB.QY is
taken on the falling GCLK edge) gives `IB.Y = FD_Y + ~FC_Y + ~IB.QY`.
So P14 and P28 idle High and go Low for half a GCLK at the internal
1 PPS only on the seconds FD.Y is Low: an active-Low 0.5 Hz output.
EF.X (`EF.QY·~BF_X`, BF.X EF.QY a GCLK late) is a pulse on the rising
edge of register 2's bit 0, which resets FD and so sets which seconds
pulse.  The "P14 rises" and "P28 rises" at 3 ns in every run are the
power-up unknown settling High, not a pulse.

**Simulated** (`check-readings-z3816a.freerun_4s`, four seconds with
register 2 at 0): P83 rises 4 times; P14 goes Low 2 times, at
0.983041230 s and 2.949121230 s, the first and third of them, each
49 ns, and P28 the same.  The same run prints every P38 rise: "P38
6399 rises" in the four seconds, each after the first "625000.0 ns
after the last", across all four 1 PPS edges: with the sync field 0
nothing rearms the P7 divider.  P40, faster at the default clocks, has
"P40 6400 rises", some "1228800.0 ns after the last", where the GCLK
side sets twice before the P7 side once.  The three-reading
run, which ends before a third internal 1 PPS, shows one such pulse,
49 ns Low from 0.983041230 s, at the first of its two.

### P20

P20 is FB.Y (`G = ~QX·C`, `C = FC_X`, FB.QX taking FC.X on ACLK),
`FC_X·~(FC_X one ACLK late)`, registered in P20's I/O block on ACLK's
falling edge: a one-ACLK pulse at each internal 1 PPS, ahead of P83
(**read**).  **Simulated** (`check-readings-z3816a.three`, P83's probe
with P20 as its reference): P83 rises 76 ns after P20, at the
testbench's 50 ns ACLK and under the models' delays.  It is not ACLK's
counter.

### The readings

**Simulated** (`check-readings-z3816a.three`): the count reads `0x6b2fc3`,
`0x689943` and `0x6602c3`.  With GCLK at the testbench's
10 MHz the internal 1 PPS is 0.98304 s, so the count falls
10,000,000 - 9,830,400 = 169,600 ticks a second, as the Z3815A's.  The
testbench prints each count less `0x3c0000`, the tap, not the preset
(3,092,419, 2,922,819 and 2,753,219); less the
preset `0x555555` they are the ticks counted, comparable with the
Z3815A's.  The Z3816A's counts run 5 ticks above the Z3815A's
(`z3815a-trace.md`, "The count") because of the testbenches, not the
designs: `z3816a_tb.v` releases RESET after `10 * CLOCK_PERIOD`,
1000 ns, `z3815a_tb.v` after `10 * osc_period`, 500 ns, so the
Z3816A's 1 PPS divider, and its tap, start 500 ns, 5 GCLK ticks, later
(**read** from the testbenches).

## The sync field

Register 3's sync field (`z3805a-trace.md`, "The sync field") is
latched in GG (`K = GCLK`; on DJ.X, the register-3 write, GG.X takes
DATA0 and GG.Y DATA1) and acts through GG's fanout (**read**):

- **Pending.**  HG (`HG.X = GG_X + GG_Y`, HG.Y that registered) and IH
  make IH.Y, set on the GCLK after the field turns non-zero and cleared
  by IG.X or JH.Y: the pending sync.
- **Kinds 1 and 2: the P7 divider.**  IF and JF sample KE.X, the P7
  prescaler's state, on GCLK; IG detects the sample's edge.  JG
  (with GG.X, field bit 0) and JD (with GG.Y, field bit 1), both gated
  by IH.Y, drive JE's A and B, the head of the P7 divider's /5
  prescaler, so a sync of kind 1 or 2 lengthens or shortens one
  prescaler cycle: it steps the P7 divider, not the 1 PPS divider.
- **Kind 3: the full sync.**  CH (`F = 1`, `K = PAD15_I`,
  `RD = DG_X`) catches the GPS edge and DG (`DI = CH_X`, `K = GCLK`,
  `DG.X = ~QX·QY`) makes it a one-GCLK pulse.  JH
  (`F = IH_Y·GG_X·GG_Y·DG_X`) fires only when the field is 3, a sync
  is pending and that GPS pulse comes: the full-sync strobe.  JH.X
  resets the GCLK chain (KF, KH, KI, LF, LH, LI, LJ), the ACLK
  prescaler through FK, and, through KD, the P7 divider and the
  phase-frequency detector; JH.Y reaches JI directly.

So JH is not the 1 PPS rollover: with the field at 0, GG holds 0, JH
never fires and the chain rolls over on its own terminal JI.  EJ is the
register-1 and -2 write decode and EK and GK are prescaler stages, not
sync logic.

**Simulated** (`check-readings-z3816a.sync1`, `.sync2` and `.sync3`:
`+seconds=2 +sync=KIND`, the field written 0 then the kind on the first
arm, as `counter_arm` does; P7 at 10 MHz, its own clock):

- **Kind 1** steps the P7 divider one P7 clock, 100 ns, earlier and
  leaves the 1 PPS divider alone.  IH.Y, the pending sync, is High
  400.0 ns from 0.000060828 s, until IG.X clears it (IG.X with IH.Y
  High 99.0 ns from 0.000061129 s); JG.Y is High once, 276.0 ns, and
  KE.Y's period is 400.0 to 500.0 ns: one prescaler cycle of 4 P7
  clocks for 5.  P38's first edge, which LE.X's first removed pulse
  marks, is at 0.000625952 s.  P83 still rises at 0.983041328 s and
  the counts read `0x6b2fc3` and `0x689943`, as `z3816a.three`'s.
- **Kind 2** steps it one P7 clock later: JD.X High once, 276.0 ns,
  KE.Y's period 500.0 to 600.0 ns, a cycle of 6, P38's first edge at
  0.000626152 s; P83 and the counts as kind 1's.
- **Kind 3** restarts the 1 PPS divider on the next GPS 1 PPS and makes
  that edge the internal 1 PPS: JH.Y, one GCLK long (High 100.0 ns),
  rises 92.0 ns after the GPS 1 PPS at 0.250000037 s, KD.X, its capture
  on P7, 215.0 ns after it, and P83 rises at 0.250000328 s, 291.0 ns
  after the GPS edge, under three GCLK clocks, where the Z3805A's kind 3
  gives no 1 PPS until a second after it.  P20, P14 and P28 pulse with
  it, and the seconds nibble counts it (1 at 0.647255 s).  The next P83
  comes 983,040,200.0 ns later, at 1.233040528 s, two clocks over the
  divide, and the count after the sync reads `0x91555b`.  IH.Y stays
  High 249,939,400.0 ns, from the field's write to the GPS edge: IG.X
  never clears it (IG.X with IH.Y never rises).  While it is pending
  JG.Y and JD.X both pulse every 400.0 ns (624,849 rises each, the last
  at 0.250000053 s), so every P7 prescaler cycle is 4 P7 clocks and the
  P7 divider's terminal comes every 500,000 ns, ahead of the GCLK
  side's 614,400: P38 rises every 500,000.0 or 1,000,000.0 ns, a set
  lost where the terminal comes again before the GCLK side has cleared
  the last (LE.X's period 500,000.0 to 1,000,000.0 ns, 407 rises, the
  last at 0.249513052 s), until KD.X rearms the P7 divider.

**The P7 divider's coupling into JI is not reachable** for any fixed
field written 0 and then the kind, as the equations say and these runs show: every path from IH to
JI goes through JH, and `JH = IH_Y·GG_X·GG_Y·DG_X` wants both field
bits and a GPS edge, so with field 1 or 2 JH never fires (JH.Y never
rises in `z3816a.sync1` or `.sync2`); with field 3,
`IG = ~IF_X·IF_Y·(~GG_X + ~GG_Y)` is 0, so only JH.Y clears IH
(`IH.F = HG_X·~QY·~HG_Y + ~IG_X·QY·~JH_Y`), and KE.X cannot move JH,
which fires on the GPS edge alone.  KE.X does reach JE, through JD and
JG: in kinds 1 and 2 the one step, and in kind 3 the 4-clock prescaler
cycles while the sync is pending.  How a step of the P7 divider moves
the internal 1 PPS through a closed PLL is not simulated: the runs
close no loop.

## Other outputs

### Every output pin

Every I/O block with its output enabled (**read**; `INV` an inverting
I/O block, `REG` an output register):

| Pin | Driven by | What it is |
| --- | --------- | ---------- |
| P2 | AH.Y, `INV`; AH `F = 1`, no inputs | tied Low |
| P14, P28 | IB.Y | the internal 1 PPS, every other second, active Low |
| P16 | `PAD15_I`, P82 | the GPS 1 PPS passed through |
| P20 | FB.Y, `REG` on ACLK's falling edge | one ACLK at each internal 1 PPS |
| P38 | LE.X | the detector's P7 side |
| P40 | LD.Y | the detector's GCLK side |
| P47 | `PAD49_I`, P53, `INV` | the oscillator, inverted |
| P48, P52 | `PAD49_I`, P53 | the oscillator passed through |
| P51 | LK.Y | LK.QY, registered on ACLK from the prescaler's stages KL and LL; board use not traced |
| P57 | LK.Y, `INV` | P51 inverted |
| P58, P59, P60, P62 | IJ.Y, IJ.X, II.X, II.Y, 3-stated by CL.X | bus bits 7 to 4, the seconds count |
| P65, P67, P70, P71 | FJ.X, FJ.Y, CK.Y, CK.X, `REG` on AL, 3-stated by CL.X | bus bits 3 to 0 |
| P76, P77 | AK.Y, AK.X, `INV` | the start pipeline's AK; board use not traced |
| P79 | AJ.X (`F = 0`), 3-stated by AI.Y | Low until AI.Y sets, then released; board use not traced |
| P81 | BH.X, `INV` | the start interval, a Low pulse |
| P83 | FC.X, `REG` on GCLK | the internal 1 PPS |

### The P7 divider

CE, DE, EE, FE, GE, HE, IE, JE, KE, LE and KD are clocked by P7 and are
**not** reset by the preset EI.Y.  Their structure (**read**):

- **A nominal /5 prescaler**, JE and KE (`RD = KD_X` on both).  KE.Y
  (`G = JE_X·KE_X`) is a combinational enable, High once per prescaler
  cycle.  With other constant JG.Y and JD.X, which the sync field sets,
  the JE/KE cycle has 4 or 6 states (`The sync field`).
- **A synchronous binary counter**, FE, EE, IE, HE, CE, DE, enabled by
  KE.Y and the carries GF (`F = KE_X·FE_Y`), FF and HF (five-input
  ANDs), and reset by LE.Y (`G = GE_Y + KD_X`).
- **GE** (`F = DE_Y·IE_Y·GF_X·HE_Y·HE_X`, registered on P7, GE.Y its
  QY) decodes terminal count 1249 (1024 + 128 + 64 + 32 + 1) and clears
  the counter through LE.Y: 1250 states.  Here GE has DI = 0 and no X
  output.
- **KD** (`F = A`, `A = JH_X`, `K = PAD7_I`) takes the full-sync strobe
  onto P7: KD.X resets JE and KE, clears the counter through LE.Y, and
  resets the detector through LE.E and LD.D.  The Z3815A's KD is a
  different block, and its P7 divider rearms through GE.DI.

So the divide is 5 x 1250 = **P7 / 6250**; nothing in it is decimal
(**read**).  **Simulated** (`check-readings-z3816a.freerun`, P7
100 ns): KE.Y period 500 ns, High 99 ns; FE.Y period 1000 ns, P7 / 10;
EE.Y, the counter's bit 3, period 8000 to 9000 ns, High 4000 ns: every
80 P7 clocks but for one 90-clock interval across the wrap at 1249, 78
rises in each 6250 P7 clocks, not P7 / 80 (`z3816a.freerun_4s`, 6400
P7 divider cycles: EE.Y "499200 rises", 78 each, arithmetic).

The P7 divider holds nothing the read port returns, and only JH.X, the
full sync, through KD, rearms it: it is not rearmed each second.  It is
coupled to the sync logic, but not to the 1 PPS: KE.X reaches IG and
so IH, and JD and JG step JE, but no fixed sync field written 0 and
then the kind lets it reach JI (`The sync field`, simulated).

### P38 and P40: a phase-frequency detector

LE (on P7) and LD (on GCLK) are the two flip-flops of a
phase-frequency detector (**read**):

- **LE.QX, P38** (`F = GE_Y + QX`, `RD = LD_X`) is set by GE.Y, the
  P7 divider's terminal, once every 6250 P7 clocks.
- **LD.QY, P40** (`G = GJ_Y + QY`, `RD = LD_X`) is set by GJ.Y, the
  1 PPS divider's own prescaled tick, once every 6144 GCLK = 12,288
  ACLK clocks.
- **LD.X** (`F = KD_X + LD.QY·LE.X`, combinational) resets both: once
  both are set, each clears the other.  So the side that sets first
  stays High until the other sets, by the phase between them, and the
  lagging side's pulse is only the reset's few block delays.  If the
  leading side sets twice before the other once, the second set is
  lost: the detector's frequency side.

At 10 MHz on P7 and 19.6608 MHz on P53 both sides run at 1.6 kHz
(10 MHz / 6250 = 19.6608 MHz / 12,288): a PLL's comparison, locking a
19.6608 MHz oscillator, the CDMA clock (16 x 1.2288 Mcps), to a 10 MHz
reference.  The divide ratios are **read** from the netlist and
**simulated** in the unequal-clock runs below; that P7 and P53 carry
the 10 MHz reference and that oscillator on a board, and what P38 and
P40 drive, are **inferred**, until a board is probed.

**Simulated** (each a 2 or 5 ms free run, the `+edges` lines):

| Run | P7, P53 periods | P38 rises every | P40 rises every |
| --- | --------------- | --------------- | --------------- |
| `freerun` | 100, 50 ns | 625,000 ns | 614,400 ns |
| `p7_130` | 130, 50 ns | 812,500 ns | 614,400 ns, once 1,228,800 |
| `p7_97` | 97, 50 ns | 606,250 ns | 614,400 ns |
| `osc_48` | 100, 48 ns | 625,000 ns | 589,824 ns |
| `pll` | 100, 50.862630208333 ns | 625,000 ns | 625,000 ns |

P38's rises fall on P7's grid alone and P40's on P53's alone; the
pins are not independent of each other, since the shared reset ends
both pulses and a side that sets twice before the other once loses
the second set (`p7_130`, below).  In `freerun` P40 leads: LD.Y
is High 10,426 to 31,626 ns, the phase gap growing each cycle, and
LE.X's pulses are under 5 ns ("3 pulses under 5.0 ns removed").  In
`p7_97` P38 leads, so LE.X carries the gap (High 8358.5 to 65408.5 ns)
and LD.Y's pulses are the short ones.  In `p7_130` P40 skips an edge
(1,228,800 ns) where LD sets twice between two P38s.  In `pll`, the
frequencies the ratios imply, both sides run at 1.6 kHz and LE.X is
High 198.1 to 198.1 ns, a fixed phase between them: the run closes no
loop, so the phase is the one the two dividers start at.  P53's period
there is 625,000 / 12,288 ns, which the 1 ps time precision cannot
hold; the testbench places each edge at its multiple of the half
period, rounded on its own, so the edges jitter by under half a
picosecond and P40 keeps 625,000 ns.  A half period repeated as one
rounded delay (25.431 ns for 25.431315) would make it 12,288 x
50.862 ns = 624,992.3 ns (arithmetic, not a run) and the phase drift.

### What of P7 reaches the firmware

Nothing (**inferred**: the paths are read, and the sync runs simulate
them at one P7 phase), as in the Z3815A (`z3815a-trace.md`, "What of P7 reaches the firmware").
Following every net from `PAD7_I` onward reaches the P7 divider (CE,
DE, EE, FE, GE, HE, IE, JE, KE and KD, with the carries FF, GF and
HF), the detector LE and LD with P38 and P40, and the sync logic IF,
JF, IG, IH, JD and JG.  Its one exit is IH.Y into JH, which no P7 net
can move for any fixed field written 0 and then the kind, as the
Z3815A's firmware writes it; the Z3816A 4001's own writes are not
read (`counter.md`, "The sync field"; here `The sync field`).  So the bus, P83,
P20, P14, P28 and P81 take nothing from P7: the internal 1 PPS, the
count's ticks and the tap run on ACLK and GCLK alone, and the GPS
1 PPS on P82, which clocks BH and CH, is the only other clock of what
the firmware reads.

On a board, locked, P7 / 6250 = P53 / 12,288: the board drives ACLK at
P7 x 12,288 / 6250, 19.6608 MHz from a 10 MHz reference on P7, and the
internal 1 PPS, 9,830,400 GCLK clocks, is 10,000,000 P7 cycles
(**inferred**; arithmetic; no Z3816A board has been probed).  The
Z3816A's board differs from the others', and its 5 MHz OCXO, an MTI
Series 260, is presumably doubled on it to that 10 MHz (**inferred**,
not traced).

## Confidence

**Simulated** (`z3816a_tb.v`, the runs above):

- the clock periods (ACLK 50 ns, GCLK 100 ns, AB halving ACLK);
- the preset read `0x555555`, status `0x08`, and the full read map
  `0x00` to `0x1f`;
- the P7 divider's periods, KE.Y 500 ns, FE.Y 1000 ns, EE.Y 8000 to
  9000 ns;
- the divide by 9,830,400 on P83, the tap KI.X, and P20 ahead of P83;
- P14 and P28 pulsing Low on alternate seconds;
- P38 rising on P7 / 6250 and P40 on P53 / 12,288, each on its own
  clock's grid, and the detector's phase and skipped edges;
- sync kinds 1 and 2 stepping the P7 divider a P7 clock earlier and
  later and leaving the 1 PPS alone; kind 3 making the GPS 1 PPS the
  internal 1 PPS, 291.0 ns after it, and running the P7 prescaler at 4
  clocks while it is pending; the P7 divider's coupling never reaching
  JI.

**Read:**

- the count is the ripple Johnson chain (CG bit 0, BA bit 1, eleven
  two-bit stages, two of them mirrored, ripple on falling edges, reset
  by EI.Y), read through CF, DF and the AL-phased bus registers;
- the preset is EI.Y, register 0's bit 6; EI.X is mode 3;
- the 1 PPS divider is an ACLK prescaler by 12,288 into GJ and a GCLK
  chain by 1600 (JI terminal through GH, FC to P83; FD, IB to P14 and
  P28; FB to P20; KF, KH, KI, LF, LH, LI, LJ reset by JH.X);
- JH is the full-sync strobe; GG is the sync field; kinds 1 and 2 act
  on JE;
- the P7 divider is a /5 prescaler and a binary counter of 1250
  states, 0 to 1249, rearmed only through KD;
- LE and LD are a phase-frequency detector between P7 / 6250 and
  GCLK / 6144;
- P7 reaches only the P7 divider, the detector and the sync logic,
  whose one exit is IH.Y into JH;
- the stop: the GPS edge clocks BH, the tap clocks CI through KI.X,
  CJ.X is the stop bit and DI the stop pipeline into CG; status bit 2
  is GH.Y and bit 3 a constant 1; EI.Y arms and resets everything.

**Inferred:** nothing driven by P7 reaches the firmware, for a sync
field written 0 and then the kind ("What of P7 reaches the
firmware"); locked, the board drives ACLK at P7 x 12,288 / 6250,
19.6608 MHz from a 10 MHz reference on P7, and the internal 1 PPS is
10,000,000 P7 cycles; the Z3816A's board, which differs from the
others', presumably doubles its 5 MHz OCXO to that 10 MHz, not traced.

**Open:**

- the board's clocks: P53's and P7's sources and frequencies (19.6608
  and 10 MHz inferred; the OCXO is an MTI Series 260 at 5 MHz, by
  several sources, presumably doubled to P7's 10 MHz, not traced),
  and so the tick, 101.7 ns at 9.8304 MHz against
  the firmware's 100 ns conversion scale;
- the tap's tie to the firmware's calibration, inferred from the
  Z3815A's;
- the Z3816A firmware's preset constant, read from the image (the FPGA
  reads `0x555555`; whether the firmware subtracts that in
  `counter_offset_apply` the way the Z3805A does is not read at this
  grain; `interval.md`);
- how a step of the P7 divider moves the 1 PPS through a closed PLL,
  and what the P38 disturbance while a full sync is pending does to
  the loop; the runs close none;
- what P38, P40, P51, P57, P76, P77 and P79 drive on the board;
- everything on a board, and the pins' roles, which are read from the
  logic.
