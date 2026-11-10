# The SmartClock FPGA designs

The front door to the FPGA write-up: what was done, how far it can be
trusted, the five designs side by side, and how to reproduce it.  Each
design has its own trace with the same sections; `counter.md` is how the
firmware drives the counter, and `outputs.md`, "The FPGA bitstreams",
is how the firmware loads the designs and how they are decoded.

## Provenance

The seven firmware images carry five XC3042 bitstreams between them
(`outputs.md`, "The FPGA bitstreams"); the designs they were built from
are lost.  Each bitstream is decoded with a third-party viewer, the
XC3000 viewer by Arisotura (<https://github.com/Arisotura/xc3000>),
which has no license and is unfinished.  Its faults that XACT's own
designs exposed are corrected in `tools/fpga/netdump.js` ("Decoder
faults", below), and the decode is turned into structural Verilog over
the project's own models of the data sheet's blocks, which model
function, not timing.  Nothing here has been checked on a board, and
every pin's role is read from the logic, not traced on one.

The pipeline pins the viewer to commit `8563397`, the commit `make
check-fpga-decoder` validates against XACT (`tools/fpga/decoder.mk`).
At upstream pull request #3 (`02c1a56`), `netdump.js` decodes the
five designs to the same netlists as at the pin, and XACT's test
designs agree with XACT's LCAs at both commits
(`make -f tools/fpga/Makefile check-decoder-upstream`, which clones
the viewer again at that commit, compares every decode and runs
`check-fpga-decoder`'s comparison there).

Since the viewer has no license, none of its code, formulas or tables
is copied into the project, and the netlists, which are the vendor's designs
re-expressed and need the viewer to make, stay generated in `target/`
(`third_party/NOTICE`).

## Reading order

1. This file, or first the illustrated short version,
   [`fpga.html`](https://charlieh0tel.github.io/smartclock/fpga.html),
   and each design's logic diagram,
   [`fpga-detail.html`](https://charlieh0tel.github.io/smartclock/fpga-detail.html).
2. `z3805a-trace.md`, the reference design, with `counter.md` for the
   firmware's side.
3. The others, each as its differences from the reference:
   `58503a-z3801a-trace.md`, `58503b-trace.md`, `z3815a-trace.md`,
   `z3816a-trace.md`.

## The five designs

The sizes and clock loads are `tools/fpga/ref/<image>.stats`, the
reads `tools/fpga/ref/<image>.readout` (the preset is its six nibbles
`05` at `0x12` to `0x17`, the status its register `0x00`), both checked
by `make -f tools/fpga/Makefile check`; the taps and the 1 PPS pins are
the `check-readings-<image>.three` runs' (`tools/fpga/ref/<image>.three.readings`);
the byte differences are against the 58503A/Z3801A's bitstream
(`outputs.md`, "The FPGA bitstreams").

| | Z3805A | 58503A/Z3801A | 58503B | Z3815A | Z3816A |
| --- | --- | --- | --- | --- | --- |
| Images | Z3805A 3543B | 58503A 3633, 3704; Z3801A 3543 | 58503B 1.01.04 | Z3815A 4010 | Z3816A 4001 |
| Logic blocks, I/O blocks, nets | 86, 36, 165 | 87, 35, 166 | 101, 36, 187 | 142, 56, 256 | 116, 41, 207 |
| Bytes differing from the 58503A/Z3801A's | 1868 | 0 | 2347 | 3392 | 3315 |
| Global clocks, and the logic blocks each clocks | GCLK from P7, 44 | GCLK from P7, 45 | GCLK from P7, 55 | ACLK from P53, 17; GCLK, ACLK halved, 47 | ACLK from P53, 17; GCLK, ACLK halved, 42 |
| Pins that clock logic blocks | P82, 1 | P82, 1 | P82, 1 | P7, 10; P84, 2; P35, 1 | P7, 11; P82, 2 |
| Divide to the 1 PPS | 10,000,000 | 10,000,000 | 10,000,000 | 9,830,400: an ACLK prescaler to GCLK / 6144, then / 1600 | as the Z3815A |
| Oscillator the divide implies (**inferred**) | 10 MHz | 10 MHz | 10 MHz | 9.8304 MHz GCLK | 9.8304 MHz GCLK |
| Count's tap after P83, in clocks; the firmware's calibration | 4,000,000; 0.4000003 s | 4,000,000; 0.4000003 s | 3,999,999, P83 a register later; 0.400000204 s | 3,932,159; 0.3932162 s | 3,932,159; 0.3932162 s |
| Preset read | `0x555555` | `0x555555` | `0x555555` | `0x555555` | `0x555555` |
| Status at `0x00`, under the preset | `0x04` | `0x04` | `0x04` | `0x08` | `0x08` |
| Registers written, and kept | 4 written, 3 kept | 5 written, the Z3805A's 3 kept | the Z3805A's 3; register 2, P14's phase; and FF at address 6 | 8 written, 4 kept | 4 kept, registers 0 to 3; how many written, not read |
| 1 PPS pins | P83, P78, P45, P14 | P83, P78 | P83, P78 | P83; P20 a pulse before it | P83; P20 a pulse before it |
| Pins pulsing on alternate seconds | | | P14 | P14, P28 | P14, P28 |
| Other outputs | "Outputs", below | | | | |

The Z3815A and the Z3816A differ from each other in 1686 bytes.  Besides
the global buffers and the pins in the table, the clocks are block
outputs: the count's own ripple, the tap that clocks the stop capture CI, and a few
more, such as the Z3815A's BG.X, its start select, and LB.Y (**read**).
In the Z3815A P84 clocks FH and FI, a counter the odd read addresses
return in bits 4 to 7 and the firmware discards, and P35 clocks LB,
whose LB.Y clocks the P35 counter, reset at each internal 1 PPS
(`z3815a-trace.md`, "The P84 counter" and "The P35 counter").

The status at `0x00` is the stop and start bits in bits 0 and 1.  In
the 585xx and Z3805A designs bits 2 and 3 are two of the interpolator's
inputs, complemented: the testbenches' byte, `0x5a`, sets bit 2.  In
the Z3815A and Z3816A neither reads the interpolator: bit 3 is a
constant 1 (FJ.X at a status address) and bit 2 is GH.Y, a flag the
internal 1 PPS terminal JI.Y sets and the 1 PPS chain's next prescaler
tick GJ.Y clears (**read**).

Every image's firmware converts the count at 1.0e-7 s a tick
(`interval.md`, "One reading"), a 10 MHz clock's.  The Z3815A's and
Z3816A's divide implies a 9.8304 MHz GCLK, whose tick is 1.7% longer;
the 0.3932162 s calibration is `0x3c0000` + 2 ticks on the firmware's
scale.  Which clock the board runs, and how the scale is reconciled
with it, is open.

The 1 PPS pins repeat every second in the `three` runs.  The 58503B's
P14 is HD.Y, a combinational gate of the internal 1 PPS by FD, a toggle
of it, with FF.X setting its polarity and P14's own pad fed back; the
Z3815A's and Z3816A's P14 and P28 are IB.Y, idle High and Low for half
a clock, IB sampling on GCLK's falling edge, on the seconds FD, a toggle
of the internal 1 PPS, is Low; register 2's bit 0 resets FD, and with
it the phase (**read**).  Four seconds free, registers at their
reset 0, the 58503B's P14 pulses at the first and third of its three internal
1 PPS edges, at 1 s and 3 s, and the Z3815A's and Z3816A's P14 and P28 at the first
and third of theirs (`check-readings-58503b.freerun_4s`,
`z3815a.freerun_4s`, `z3816a.freerun_4s`).  The Z3815A's and Z3816A's
P20 is FB.Y, a pulse one ACLK long on each rise of FC.X, the
register that also feeds P83, and comes 76 ns before P83 at the
testbench's 50 ns ACLK, under the models' delays (`z3815a.three`,
`z3816a.three`: the P83 probe's "after reference", P20 its
reference).

## Outputs

Every enabled output I/O block (**read**, from each netlist's I/O
blocks).  `~` is an I/O block that inverts, "registered" one that
passes the output through its flip-flop on GCLK, a blank a pin that is
not an output.  P2 is a constant-1 block through an inverting I/O
block in all five, so it is tied Low.

| Pin | Z3805A | 58503A/Z3801A | 58503B | Z3815A | Z3816A |
| --- | --- | --- | --- | --- | --- |
| P2 | ~BA.X, tied Low | ~BA.X, tied Low | ~CA.X, tied Low | ~AH.Y, tied Low | ~AH.Y, tied Low |
| P14 | EI.Y, registered: 1 PPS | P17 passed through | HD.Y, registered: alternate seconds | IB.Y: alternate seconds | IB.Y: alternate seconds |
| P16 | P82, the GPS 1 PPS, passed through | the same | the same | the same | the same |
| P19 | | | GB.Y, GCLK / 65 | ~FA.X (the P35 counter) | |
| P20 | | | | FB.Y, registered on ACLK: before each 1 PPS | the same |
| P21, P26 | | | | FA.X; HA.X (the P35 counter: LB.Y / 81,920 and / 512) | |
| P24, P25 | | | | ~HB.Y, HB.Y: P23 while P27 is High, else ~EG.X | |
| P28 | | | | IB.Y, as P14 | the same |
| P29, P30, P34 | | | | KB.Y, ~KB.Y: GCLK / 12 XNOR LB.Y / 5; LB.Y, P35 / 2 | |
| P36 | | | | ~P35 | |
| P38, P40 | | | | LE.X, LD.Y: the phase-frequency detector | the same |
| P45 | EI.Y, registered: 1 PPS | | | | |
| P47, P48 | | | | ~P53, P53 | the same |
| P52 | | | | | P53 |
| P51, P57 | | | | LK.Y, ~LK.Y: the ACLK prescaler's state XOR P50 | LK.Y, ~LK.Y: the ACLK prescaler's state |
| P56 | | | | ~P50 | |
| P58 to P71 | the bus's eight bits, 3-state | the same | the same | the same | the same |
| P72 | AH.X | AH.X | AH.Y | | |
| P76, P77 | ~BL.Y, ~BL.X | the same | the same | ~AK.Y, ~AK.X | the same |
| P78 | EI.Y, registered: 1 PPS | the same | FH.X, registered: 1 PPS | | |
| P79 | BJ.X, a 0, while AI.X is Low, else 3-state | the same | the same | AJ.X, a 0, while AI.Y is Low, else 3-state | the same |
| P81 | ~AH.Y, the start's pulse | the same | ~AH.X | ~BH.X | ~BH.X |
| P83 | EI.Y, registered: 1 PPS | the same | FH.Y, registered: 1 PPS, its polarity FF.Y's | FC.X, registered: 1 PPS | the same |
| P84 | ~P17 | ~P17 | ~P17 | | |

In the Z3815A P78 and P84 are inputs: BG takes the count's start from
P82, P84 or P78 as register 2's field EG selects.  What the board takes
from any of these pins is open.

## Roles and tiles

A tile name is a position, not a function, and the same name has
different roles in different designs: the Z3805A's EI.Y drives the
1 PPS pins, the Z3815A's and Z3816A's EI.Y is the preset.  This is
what does what (**read**; a blank is a role the design does not
have):

| Role | Z3805A | 58503A/Z3801A | 58503B | Z3815A | Z3816A |
| --- | --- | --- | --- | --- | --- |
| Preset net (resets the count) | FH.Y | FH.Y | EI.X | EI.Y | EI.Y |
| Start capture (the GPS 1 PPS) | AH, clocked by P82 | AH, clocked by P82 | AH, clocked by P82 | BH, clocked by BG.X: P82, P84 or P78 | BH, clocked by P82 |
| The GPS edge on GCLK, into the sync | DH, of P82 | DH, its flip-flops exchanged | DH, of P82, its flip-flops exchanged as the 58503A's | DG, of CH.X (CH clocked as BH) | the same |
| Stop capture | CI | CI | CI | CI | CI |
| Tap that stops the count | HG.Y | HG.Y | HG.Y, through EH | KI.X | KI.X |
| Count: bit 0, bit 1, bits 2 to 23 | CF.QY; BB.QY; eleven two-bit stages | the same | CF.QX; BB.QY; eleven two-bit stages | CG.QY; BA.QY; eleven two-bit stages | the same |
| 1 PPS chain's prescaler | GI, then IG, JH, IH and JI | the same | the same | the ACLK counter (EL, GL and others) to FL, then GJ on GCLK | the same |
| 1 PPS terminal | HH.X | HH.X | HH.Y | JI.Y | JI.Y |
| 1 PPS output driver, into the pins' I/O blocks | EI.Y, combinational | EI.Y, combinational, with HJ.X; HK the Z3805A's HJ.Y flip-flop moved | the latch HK, then FH; HD for P14 | FC; IB for P14 and P28 | FC; IB for P14 and P28 |
| Read multiplexer, bits 0 and 1 | CK | CK | CK | CK | CK |
| Read multiplexer, bits 2 and 3 | EG | EG | EG | FJ | FJ |
| Seconds counter | IK, IL, enabled by HH.X | the same | IK, IL, enabled by HH.Y | HH, II, enabled by JI.Y | II, IJ, enabled by JI.Y |
| Sync field register | DI | DI | DI | GG | GG |
| Sync step | GJ, GK | the Z3805A's blocks | the Z3805A's blocks | kinds 1 and 2 through HG, IG, IH (with IF and JF sampling KE.X), JD, JG onto JE; kind 3 through JH | the same |
| P7 divider: prescaler, counter, terminal, rearm | | | | JE, KE; FE, EE, IE, HE, CE, DE; GE; GE.DI = JH.X | the same; KD, on P7, F = JH.X |
| Phase-frequency detector | | | | LE (P38), LD (P40) | the same |

The bus's output registers are I/O blocks, clocked on AL's two phases,
not CK, FJ or EG, which are combinational.

## The family

From the read maps (**simulated**) and the netlists (**read**):

- The **58503A/Z3801A**, the **58503B** and the **Z3805A** read out
  alike: the same preset, the same status, the same map over all 32
  addresses.  The 58503A/Z3801A's design differs from the Z3805A's in
  ten logic blocks configured otherwise, eleven connected to other
  nets and one added, HK, and in six I/O blocks: P14 configured
  otherwise, P71, P70, P67 and P65 connected otherwise, P45 removed
  (`tools/fpga/ref/58503a-z3801a.tilediff`, checked by `check`).  Most
  of that is AL's two outputs exchanged, with the blocks that read AL
  and the bus's output registers, which AL clocks, exchanged to match;
  DH and HK hold the Z3805A's flip-flops in other places.  In behavior
  it differs in P14, which passes P17 through, P45, which it does not
  drive, and the 1 PPS pulse: HJ.X and EI's other input end it at
  26,000 ns against the Z3805A's 31,400 ns (**simulated**, the `three`
  runs).  The 58503B's divider and sync blocks are the Z3805A's block
  by block, with HH's outputs exchanged and LJ for HJ, and so are the
  write decode and the GPS edge detector they read, DH's flip-flops
  exchanged (**read**, checked by `check`:
  `tools/fpga/58503b-z3805a.match`), and
  its sync runs give the 58503A's results a clock later on the pins
  (**simulated**, `58503b-trace.md`, "The sync field"); its count,
  read port and output stages are traced against them there.
- The **Z3815A** and the **Z3816A** share their count, the core of
  their 1 PPS divider, the P7 divider's counting stages and the phase
  detector, and read out alike.  They differ in the P7 divider's rearm
  (GE in the Z3815A, KD in the Z3816A), the start (the Z3815A's BG
  selects among P82, P84 and P78), the seconds counter, and the
  Z3815A's P84 and P35 logic: 142 against 116 logic blocks.  CI, the
  stop capture, differs only in its encoding: it takes BH.Y through
  its DI in the Z3815A and through its table in the Z3816A.
- The two families share the count's kind, a GCLK-gated toggle, a
  one-flip-flop stage and eleven two-bit ripple Johnson stages, 24 bits
  preset to `0x555555` and read as `0x555555` plus the ticks, with read
  bit 0 the toggle's complement; and the start and stop mechanism.  They
  share neither the divider nor the clocks.

Only the 58503A's and Z3801A's bitstream is shared byte for byte.

### The Z3815A's and Z3816A's P7 divider and P38, P40

The P7 divider (**read**): JE and KE are a nominal /5 prescaler (its
cycle has 4 or 6 states for other values of JG.Y and JD.X, which the
sync field sets); KE.Y = JE_X·KE_X is a combinational enable, High once
a prescaler cycle; FE, EE, IE, HE, CE and DE are a synchronous binary
counter, and GE decodes its terminal count, 1249 (1024 + 128 + 64 + 32
+ 1): P7 / 6250 in all, nothing decimal.  FE.Y is P7 / 10; EE.Y, the
counter's bit 3, repeats every 80 P7 clocks but for one 90-clock
interval across the wrap, 78 rises in each 6250 P7 clocks
(`z3815a-trace.md`, "The P7 divider: P7 / 6250").  The Z3815A's GE.QY
clears the stages through LE.Y; the
Z3816A's rearm is KD (on P7, F = JH_X), which resets JE and KE and
feeds LE.E and LD.D, and its GE has DI = 0 and no X output.

P38 and P40 are a phase-frequency detector, by its ratios a PLL's
that locks a 19.6608 MHz oscillator, the CDMA clock (16 x 1.2288 Mcps),
to the 10 MHz reference, compared at 1.6 kHz: 10 MHz / 6250 =
19.6608 MHz / 12288 (**inferred**).  LE (P38, on P7) is set by the P7
divider's terminal; LD (P40,
on GCLK) by GJ.Y, the 1 PPS chain's own GCLK / 6144 = ACLK / 12288
tick; each clears the other (LD.X = GE_X + LD.QY·LE.X in the Z3815A,
KD_X + LD.QY·LE.X in the Z3816A, resets both).
The ratios are **read** from the netlist and **simulated** with P7 and
P53 unequal (`check-readings-<image>.p7_130`, `.p7_97`, `.osc_48`,
`.pll`); the board's sources of P7 and P53 are **inferred** until a
board is probed.  GCLK, ACLK halved, would then be 9.8304 MHz in both
(**inferred**).

Nothing driven by P7 reaches the firmware in either design
(**inferred**: the paths are read, and the sync runs simulate them at
one P7 phase).  P7 reaches the P7
divider, the detector with P38 and P40, and the sync logic that
samples KE.X; the one net out of that set is IH.Y into JH, which no P7
net can move for any fixed field written 0 and then the kind, as
the Z3805A 3543B, 58503A 3704, Z3801A 3543, Z3815A 4010 and 58503B
1.01.04 firmware write it; the Z3816A 4001's is not read.  The internal 1 PPS, the count's
ticks and the tap run on ACLK and GCLK, ACLK halved, alone
(`z3815a-trace.md`, `z3816a-trace.md`, "What of P7 reaches the
firmware").  So on a board the loop is all that ties the firmware's
time to P7: locked, the board drives ACLK at P7 x 12,288 / 6250,
19.6608 MHz from a 10 MHz reference on P7, and the internal 1 PPS is
10,000,000 P7 cycles (**inferred**; no Z3815A or Z3816A board has been
probed).  The Z3816A's board differs from the others', and its 5 MHz
OCXO, an MTI Series 260, is presumably doubled on it to that 10 MHz
(**inferred**, not traced).

## Decoder faults

The viewer's faults that `netdump.js` corrects; the detail is in
`tools/fpga/xact/README.md`, "The viewer's faults".  `make
check-fpga-decoder` decodes the test designs XACT built
(`tools/fpga/xact/designs`) and compares them with XACT's routed LCAs;
`make check-fpga-xapp` does the same for XACT's XAPP003 designs, built
locally (`tools/fpga/xact/README.md`).

| Fault | Symptom | SmartClock designs affected | Correction in `netdump.js` | Checked by |
| --- | --- | --- | --- | --- |
| Input routing cleared before a later block shows it is used | a flip-flop with no clock (CI.K) | all five (CI), and HJ in the 58503B | keep every input's configured routing, EC and RD | not stated |
| Two drivers on a horizontal Longline | loads read both X and Y of one block | none (XC3042 has no input select) | enable the local line's PIP only with the input select at 1 | `check-fpga-xapp` |
| Fourth input of modes F and FGM taken as D | a block reads the wrong net, and the Z3805A's preset read wrong | all five, a few blocks each; the Z3805A's EF | read the configuration bit that selects D or E | `check-fpga-decoder`; `check`'s read port |
| Trace stopped 300 steps deep | a large net loses its far loads | not established | a record of switch-matrix pins passed, no limit | `check-fpga-decoder` |
| Left-edge I/O block's output clock read from the wrong bit | wrong output clock line | not established | read bit 1 of the tile's first row | `check-fpga-decoder` |
| col.A.local.13's PIPs numbered the other way | P20's output register with no clock | Z3815A, Z3816A | swap the line's two PIPs | `check-fpga-decoder` |
| A clock buffer that clocks only I/O blocks dropped | its nets missing | not established | keep the buffer and its die-edge lines | `check-fpga-decoder` |
| Pull-up read where the output is used | pull-ups on the bus pins | the bus pins of every design | read the bit as the 3-state control's | `check-fpga-decoder` |
| Die-edge polarity for input flip-flops | inputs registered inverted | none (no design registers an input) | read the bit the other way for inputs | `check-fpga-decoder` |

## Reproduce

Prerequisites: git and network access to github.com, for the viewer's
clone; node, or headless Chrome; Python 3; Icarus Verilog 12; Yosys 0.52
for `check`'s structure diff (`outputs.md`, "Simulation"); and
`pdftotext`, from poppler-utils, for `check`'s pad tables against the
data sheet (`outputs.md`, "The pins").  From the
repository root:

    make -f tools/fpga/Makefile all       # decode, build and run every image's fast path
    make -f tools/fpga/Makefile check     # the same, failing on drift from tools/fpga/ref
    make -f tools/fpga/Makefile figures   # the waveform figures
    make fpga                             # the explainer page, target/pages/fpga.html
    make fpga-detail                      # the detailed diagrams, target/pages/fpga-detail.html
    make check-fpga-decoder               # the decoder against XACT's test designs
    make -f tools/fpga/Makefile check-decoder-upstream  # the five decodes at upstream PR #3
    make -j16 -f tools/fpga/Makefile check-readings      # the long runs
    cargo test -p smartclock-cli --test fpga_bitstreams   # the bitstreams' offsets, hashes, differences

`make -f tools/fpga/Makefile <image>` and `check` run each testbench's
fast path, `+seconds=0 +registers +interpolator`: the preset, every
address's read and the interpolator's references.  `check` covers
the viewer's clone, at the pin with no local change, and each design's
size, modes, flip-flops and clock loads (`.stats`), its
read port with the preset on (`.readout`), its Icarus and Yosys
diagnostics (`.structure`), the viewer's pad tables (`pins.txt`) and
the 58503A/Z3801A's logic and I/O blocks against the Z3805A's
(`58503a-z3801a.tilediff`), the 58503B's blocks the trace pairs with
the Z3805A's and the 58503A's, as functions (`check-blocks`,
`tools/fpga/*.match`), that the explainer page renders from the
references and figures (`check-explainer`), and that the detail page
renders from the decodes, every pin in use with a role, every
output an element's, every section cited a heading of its
document and neither drawing colliding with itself, with its own test
that each such error is
refused (`check-detail`); firmware CI runs it
(`.github/workflows/firmware.yml`).  It does not cover anything that
waits on the divider: the 1 PPS periods, the readings, the taps, the
sync steps, P19, P38 and P40.  `check-readings` runs them all, each trace naming its runs as
`check-readings-<image>.<run>` (the flags are the Makefile's `READ_`
lines), with the nets `tools/fpga/probe.v` measures, and diffs each
run's output against `tools/fpga/ref/<image>.<run>.readings`.  It is
not part of `check` or CI, and wants `-j`: it is run by hand, whenever a testbench, the models
(`xc3000.v`, `probe.v`), the netlist generator or the decoder pin
changes, and before a doc quotes a new figure.  `time make` gives how long it takes on a machine.  `check-decoder-upstream` is run by hand
too, when `netdump.js` or the pin changes: it needs the network for its
second clone.

The models give every block output the same 1 ns, so a combinational
net whose inputs change on one edge by paths of different depth can
pulse for a nanosecond or two; `probe.v` removes every pulse under
5 ns before it measures and says how many it removed (`outputs.md`,
"Simulation").  Before the filter the probes showed them on the
Z3805A's and 58503A's HH.X, one 0.1 s before each terminal count, the
Z3805A's JI.X and, in the sync runs, GJ.X and GJ.Y, the 58503B's HK.X,
the Z3815A's and Z3816A's JI.Y and KE.Y, the Z3815A's JG.Y and JD.X in
its sync runs, and BB.Y, the Z3815A's and the Z3816A's.  The runs now
report removed pulses on just those nets, and in `z3815a.p35` on P29,
the XNOR of two unrelated clocks, and on the three probes that compare
a pin with its complement, which are the pads' one-delay skews
(`z3815a-trace.md`, "The P35 counter"): the Z3805A's and
58503A's HH.X, once a second; the Z3805A's JI.X, between its
terminals; the Z3805A's GJ.X and GJ.Y, once, in `z3805a.sync_3_late`,
and the 58503A's and 58503B's, once, in their `sync1` (GJ.Y) and
`sync2` (GJ.X) runs; the 58503B's HK.X, once a second; the Z3815A's and Z3816A's
JI.Y, once a second, and KE.Y, once each prescaler cycle; the
Z3815A's JG.Y, once, in `z3815a.sync1`, and its JG.Y and JD.X, once
each, in `z3815a.sync3`; and BB.Y, a read-multiplexer term, the
Z3815A's under the register sweep and in its `three` and sync runs,
and the Z3816A's once in `z3816a.sync3`.  The filter also
removes short pulses that are the design's own: the Z3815A's and
Z3816A's LE.X or LD.Y, whichever lags, at any phase, since the side
that sets second clears both within two block delays, which the
testbenches' unfiltered P38 and P40 edges show.  Each trace names the
nets it quotes that had them.

## Glossary

- **Image, design.**  An image is one of the seven firmware builds
  (`third_party/firmware/images/`); a design is one of the five FPGA
  bitstreams they load.  The 58503A 3633 and 3704 and the Z3801A 3543
  share one.
- **Logic block, tile.**  The XC3042's 12 by 12 array of configurable
  logic blocks (CLBs), named by tile: the first letter the row, the
  second the column, AA at the top left and LL at the bottom right.
  A tile name is a position, not a function ("Roles and tiles").
- **X, Y, QX, QY, F, G, DI, EC, RD, K, K_INVERT.**  A logic block's two
  outputs; its two flip-flops' outputs; its two function generators;
  its flip-flops' data input, clock enable, direct reset and clock, and
  the clock's inversion (the data sheet, Figures 4 to 6).
- **AB.X, AB_X.**  In prose, AB.X is block AB's X output; AB_X, in a
  quoted equation or the Verilog, is the netlist's name for the net it
  drives.
- **P*n*, PAD*n*.**  A package pin and a die pad, numbered differently:
  the 58503B's P19 is PAD89, and its PAD19 is P78.  The prose names
  package pins.
- **IOB, registered, unregistered, fast slew.**  An I/O block; an
  output through its flip-flop or not; its fast output edge.
- **Die-edge clock line, "bad mux 111111".**  The clock lines along the
  die's edges that clock I/O blocks; the viewer's report of a line taken
  straight from a clock buffer, which is not a fault (`outputs.md`,
  "Netlists").
- **GCLK, ACLK, TCLKIN, BCLKIN.**  The global and alternate clock
  buffers, and their dedicated input pads.  In the 585xx and Z3805A
  designs P7 drives GCLK through general routing, not TCLKIN; in the
  Z3815A and Z3816A P53 drives ACLK, and AB.Y, ACLK halved, GCLK.
- **The P7 divider.**  The Z3815A's and Z3816A's P7-clocked blocks, CE
  to LE (and KD in the Z3816A): a nominal /5 prescaler and a binary
  counter, P7 / 6250, setting LE, P38, one side of the phase-frequency
  detector ("The family").  It is not the count.
- **The count, the preset, the tap.**  The 24 bits the port reads: a
  GCLK-gated toggle, a one-flip-flop stage and eleven two-bit ripple
  Johnson stages ("Roles and tiles"); the register 0 bit that resets
  it, reading `0x555555`; the divider net whose edge stops it.
- **Start bit, stop bit.**  Status bit 1, set when the GPS 1 PPS starts
  the count, which the firmware calls "counted"; status bit 0, set when
  the tap stops it, which the firmware calls "interpolated".
- **Internal 1 PPS, GPS 1 PPS, TI.**  The divider's 1 PPS; the GPS
  engine's on P82; the interval `t_internal − t_GPS` the count measures
  (`counter.md`, "The sign, and the loop").
- **Sync kinds 1, 2, 3.**  Register 3's field.  In the Z3805A
  (**simulated**): a step of the internal 1 PPS one clock earlier, one
  clock later, a restart on the GPS 1 PPS (`counter.md`, "The sync
  field"); a clock is 100 ns at the 10 MHz the divide implies.  The
  58503A's and 58503B's blocks are the Z3805A's, and so are their
  results, a clock later on the 58503B's pins (**simulated**,
  `58503a-z3801a-trace.md`, `58503b-trace.md`).  In the Z3815A and Z3816A
  (**simulated**) kinds 1 and 2 step the P7 divider one P7 clock
  earlier and later and leave the 1 PPS alone, and kind 3 makes the
  GPS 1 PPS itself the internal 1 PPS, a few clocks after it, where
  the Z3805A's restarts the divider to give the 1 PPS a second later
  (`z3815a-trace.md`, `z3816a-trace.md`, "The sync field").
- **Mode 0, mode 3.**  Register 0's mode: the count, and the
  interpolator's references.
- **Read, simulated, inferred, open.**  In the firmware docs, **read**
  is taken from the firmware's code; in the traces, from the decoded
  netlist's equations and connections.  **Simulated** is printed by a
  testbench under Icarus Verilog; **inferred** is reasoning from those;
  **open** is not established.
- **XACT, MakeBits, APR, LCA, XAPP003, DSREF2, DSREF3.**  Xilinx's
  1996 tool suite, its bitstream generator and its placer and router;
  its design file format; its application note of counter designs; its
  reference manuals (`tools/fpga/xact/README.md`).
- **PIP, Longline, tie-down.**  A programmable interconnect point; a
  long routing line across the array; MakeBits's `-t`, which ties
  unused interconnect to a known level (DSREF2, 7-16).

## Open, across the designs

- Everything on a board: the pins' roles, and whether the readings are
  what the hardware gives.
- The oscillator frequencies: 10 MHz and 9.8304 MHz are what the
  divides imply, not measurements; the board's sources of the Z3815A's
  and Z3816A's P7 and P53 (10 MHz and 19.6608 MHz, inferred from the
  phase detector's ratios, and ACLK then P7 x 12,288 / 6250); and how
  the firmware's 100 ns scale fits a 9.8304 MHz GCLK.
- What the board takes from the outputs ("Outputs"), and what drives
  the Z3815A's P84 and P35.
- How the interpolator reads the start pulse with the reference bit
  Low (`z3805a-trace.md`, "Confidence").
- In the Z3815A and Z3816A, how a step of the P7 divider, and the P7 prescaler's 4-clock
  cycles while a full sync is pending, move the internal 1 PPS through
  a closed PLL: the runs close none.  The P7 divider's coupling into
  the sync logic (IF, IG, IH, JD, JG) is simulated not to reach JI for
  any fixed field written 0 and then the kind; a field taken straight
  from one nonzero kind to another is not simulated, and the Z3816A
  4001's sync writes are not read.
- The Z3816A's and the 58503B's firmware constants at the grain the
  Z3805A's are read.
- How many registers the Z3816A's firmware writes; the design keeps
  registers 0 to 3 (`z3816a-trace.md`, "Register port").
