# The Z3805A FPGA

The reference design: the other four are traced against it.  How the
firmware drives it is `counter.md`; what the design does, from its
decoded bitstream, is here.

## At a glance

| | |
| --- | --- |
| Images that load it | Z3805A 3543B |
| Bitstream | `third_party/firmware/images/fpga/z3805a.bit` |
| Part | XC3042, PLCC84 |
| Logic blocks, I/O blocks, nets | 86 of 144, 36, 165 (`tools/fpga/ref/z3805a.stats`) |
| Clocks | one global clock buffer, GCLK, from P7 through general routing; local clocks from logic (below) |
| Oscillator the divide implies | 10 MHz (a divide by 10,000,000) |
| 1 PPS output pins | P83 (bidirectional: its pad holds the pulse), P78, P45, P14 |
| Other outputs | P2, P72, P76, P77, P79, P81, P84 |
| Differences from the reference | none: this is the reference |

The GPS 1 PPS's rising edge starts the count, and a divider tap
4,000,000 clocks after the internal 1 PPS stops it, through a pipeline
that makes the count's offset 4,000,003; the count is a 24-bit ripple
counter whose preset reads `0x555555`; status bit 1 sets at the start
and bit 0 at the stop; register 3's sync field steps the internal
1 PPS by a clock either way or restarts it on the GPS 1 PPS.  The
design is decoded with the corrections in `outputs.md`, "Netlists":

- 86 of the XC3042's 144 logic blocks, 62 as two functions of four
  inputs and 24 as one of five, with 99 flip-flops; 36 I/O blocks, all
  bonded; 165 nets.  No 3-state buffer, Longline pull-up, crystal
  oscillator or alternate clock buffer is used.

## Method and confidence

How the designs are decoded and simulated, and how far either can be
trusted, is `fpga-comparison.md`'s ("Provenance"), as are the glossary
and the confidence words.  Every statement here is **simulated** unless it is marked
**read** or **inferred**: printed by Icarus Verilog running the netlist
`tools/fpga/netlist.py` writes, with `tools/fpga/z3805a_tb.v` driving
it as the firmware does.

    make -f tools/fpga/Makefile z3805a            # decode, build, the fast run (+seconds=0)
    make -f tools/fpga/Makefile check-readings-z3805a.three
    vvp -n target/fpga/z3805a_tb +seconds=3 +interpolator +probe   # the same run by hand

Every simulated figure quoted here is printed by one of the runs
`make -j16 -f tools/fpga/Makefile check-readings` makes, which diffs
each against `tools/fpga/ref/<image>.<run>.readings`; the runs and their
flags are the Makefile's `READ_` variables, each run with `+probe`.
The Z3805A's are `z3805a.fast`, `.byte_1c`, `.three`, `.late`, `.sync1`, `.sync2`,
`.sync3`, `.low`, `.wide`, `.later` and `.nogps`, and `.sync_3_late`,
on the stimulus file `tools/fpga/z3805a.sync_3_late.stim`
(`tools/fpga/z3805a_stimulus.v`); a figure below names its run,
`make -f tools/fpga/Makefile check-readings-z3805a.<run>`.

The probe removes every pulse under 5 ns, the models' hazards
(`outputs.md`, "Simulation"); of the nets quoted here, JI.X and HH.X
have them ("The 1 PPS divider"), and GJ.X and GJ.Y one each in
`z3805a.sync_3_late` ("The sync field").  Times within a clock, such
as the stop bit 2 ns after the tap or a pulse's nanoseconds, are under
the models' nominal 1 ns a block, not the part's timing (`outputs.md`,
"Simulation").

The other designs are compared in `fpga-comparison.md`.

## Clocks

(**read**) P7 drives the one global clock buffer the design uses,
GCLK, through general routing (not the data sheet's dedicated TCLKIN
pad, P13); GCLK clocks 44 logic blocks and the output flip-flops of
P83, P78, P45 and P14.  The alternate buffer, ACLK, is unused.  Fifteen
logic blocks are clocked other than by GCLK, AH by the pad P82 and
fourteen from logic:

- AH by P82, the GPS 1 PPS (the start);
- BB by CF.Y, and each of the count's eleven ripple stages by the
  stage before it ("The count");
- CI, the stop bit, by GH.X, a buffer of the divider's tap HG.Y;
- BH by CH.Y.

P83, P78, P45 and P14 carry one signal, EI.Y, with fast slew: the
candidates for the 1 PPS.  P83 is bidirectional: EI reads its pad
back ("The 1 PPS divider").

## Register port

From the logic that decodes it:

| Pin | Role | Why |
| --- | ---- | --- |
| P61 | the port's select, Low | with P75, enables the bus drivers and the write strobe |
| P75 | R/W, High to read | the bus is driven while it is High and P61 Low |
| P69, P68 | ADDR0, ADDR1 | register 0 is 00, 1 is 01, 3 is 11 |
| P66 | ADDR2, Low | every write decode wants it Low |
| P71, P70, P67, P65, P62, P60, P59, P58 | DATA bits 0 to 7 | below |
| P3 to P6, P8 to P11 | the interpolator's byte, an input | read at `0x10` and `0x11` |
| P82 | the GPS 1 PPS | clocks the count's start |

(**read**) An eight-bit bus: P58, P59, P60, P62, P65, P67, P70 and P71
driven together under HL.X's 3-state control, P65, P67, P70 and P71
through flip-flops clocked by AL.X or AL.Y; P67 is never read.  P61 and
P75 feed HL, whose X is the 3-state control; P66, P68 and P69 feed EL
and DL.  The interpolator's inputs, P3 to P6 and P8 to P11, have
pull-ups.

A write is taken on the 10 MHz clock: the select and R/W Low are
sampled twice, and the first clock they are seen makes a one-clock
strobe, which the address decodes.  The bits each register keeps:

- **Register 0:** bits 0 and 1 together, as one flip-flop: whether the
  mode is 3.  So modes 1 and 2 are mode 0 (**read**).  Bit 6, the preset, is a
  flip-flop whose output resets every stage of the count.
- **Register 1:** bit 0 (start), bit 1 (reference), bits 3 to 7 (the
  read address).  Bit 2 is not kept.
- **Register 2** does not exist: its address decodes to nothing.
- **Register 3:** bits 0 and 1, the sync field.

A read drives all eight bits.  Bits 4 to 7 are always the seconds
count, whatever the address; bits 0 to 3 are the addressed nibble,
decoded this way:

- bits 0 and 1 come from CK: the status's stop and start bits, unless
  the address's bit 4 is set, when they are the addressed register's;
- bits 2 and 3 come from EG, which never sees the address's bit 4, so
  `0x00` to `0x07` read bits 2 and 3 of `0x10` to `0x17`;
- the address's bit 3 zeroes DF and EG, so `0x08` to `0x0f` read only
  the status bits and `0x18` to `0x1f` read 0.

**The interpolator** is not in the FPGA: its byte comes in on P3 to P11
and reads back at `0x10` and `0x11` complemented, as
`interpolator_read` expects, low nibble first (with the byte at
`0x5a`, `0x05` at `0x10` and `0x0a` at `0x11`, `z3805a.fast`; at
`0x1c`, whose nibbles are not each other's complement, `0x03` and
`0x0e`, `z3805a.byte_1c`).  In mode 3, a pulse of register
1's start bit sets status bits 0 and 1 at once (`0x07`).

## The count

(**read**) The 24-bit interval count, from bit 0 up:

| Bits | Blocks | Form |
| ---- | ------ | ---- |
| 0 | CF.QY | a toggle on GCLK, gated by FH.Y (the preset), DG.Y (the stop) and AI.Y (the start) |
| 1 | BB.QY | one flip-flop, `G = ~QY`, clocked by CF.Y |
| 2 to 23 | BC, BD, CC, ED, EE, DD, DB, CB, DC, CD, BE | eleven two-bit ripple Johnson stages, two bits each in that order |

Each two-bit stage takes QX from its own QY through DI and QY from not
QX, so QX QY run 00, 01, 11, 10, on the falling edge of the stage
before (`K_INVERT`); FH.Y resets all of them, and AL.X, EB.X, EB.Y,
EC.X and EC.Y select their read.  The read is linear: each two-bit
digit reads its state's (n + 1) mod 4, so the count reads `0x555555`
plus the ticks counted (mod 2^24), and read bit 0 is the complement of
CF.QY.  This is the Z3815A's and the Z3816A's structure too, with
CG.QY and BA.QY there for CF.QY and BB.QY (`fpga-comparison.md`).

**The preset.**  With it on the count reads `0x555555` (`z3805a.fast`):
each two-bit stage of the ripple counter is reset, and its read logic
gives `01` for that state.  The status reads `0x04`: bits 0 and 1
clear, and bits 2 and 3 from the interpolator's byte (below).  Bits 14
and 15 reach the port through EF's E input (**read**): EF ORs four of
the counter's read outputs, DC, EE, BE and DB, the last through its E.
The decoder's fault that once hid this is in `fpga-comparison.md`,
"Decoder faults".

Three logic-block outputs reach no enabled pin (DL.Y, EH.Y, GL.Y;
**read**): MakeBits's tie-down routes them, and nothing loads them
(`outputs.md`, "Netlists").

## Start, stop and status bits

**The status bits.**  Bit 1, the start bit, sets on the GPS 1 PPS's
rising edge, when the count starts; bit 0, the stop bit, sets when the
count stops.  The firmware calls bit 1 "counted" and bit 0
"interpolated" (`counter.md`, "The register port"); its order, bit 1
then bit 0, works either way.  With no GPS 1 PPS neither sets: after
512 polls, 5 s, the status reads `0x54`, the seconds at 5, while the
internal 1 PPS ran on (`z3805a.nogps`); so the stop bit waits for a
start, and the firmware's wait ends in "Gate failure".  Bits 2 and 3
are not flags: at address 0 the read multiplexer passes two of the
interpolator's inputs, P5 and P6, complemented (EC.Y is selected, and
nothing gates them by the address's bit 4).  So bit 2 reading 1 and
bit 3 reading 0 hold only for the testbench's byte: with the byte at
`0x5a` the status reads `0x04`, at `0x1c` `0x00` (`z3805a.byte_1c`).

**The start.**  (**read** from the equations; the counts that bear it
out **simulated**) The GPS 1 PPS's rising edge clocks AH, whose Y, the
start's pulse, rises at once.  With register 1's reference bit High,
BI.QY takes AH.Y on the first 10 MHz clock edge after the GPS edge,
AI.QY sets on the second, and CF.QY, the count's bit 0, first toggles
on the third; with the bit Low AI sets on the first edge and the count
on the second.  The interpolated fraction is the time from the GPS edge
to that first clock edge.  A 300 us pulse counts the same as a 100 us
pulse (`z3805a.wide`), and an edge 50 ns later, past a clock edge,
counts one tick fewer (`z3805a.later`).

**The stop.**  The divider's tap, HG.Y, rises 4,000,000 clocks after
the clock edge on which the internal 1 PPS rises, and CI.X, the stop
bit, 2 ns after it (`z3805a.three` prints both as "after reference",
400,000,000 and 400,000,002 ns).  CI is clocked by the tap, through
GH.X, and takes AH.X, which the start sets, so only a tap after a start
stops the count (**read**).  CI.X then passes four flip-flops on GCLK,
CJ.QX, CJ.QY, DG.QX and DG.QY, and DG.Y holds CF: the last edge counted
is the fourth after the tap's, 4,000,004 clocks after the internal
1 PPS's, and the count holds from the fifth (**read**).

So the count runs from the third clock edge after the GPS edge to the
fourth after the tap, both counted, and, the fraction being the time
from the GPS edge to the first clock edge after it,

    count + fraction = 4,000,003 + (t_internal − t_GPS) / 100 ns

which is the firmware's calibration exactly: 4,000,003 is the net
offset (4,000,000 to the tap, 4 more to the last counted edge, 1 for
counting both ends, less the 2 edges the start does not count), not
the time of any one edge.  Here t_internal is the divider's epoch: the
clock edge on which the internal 1 PPS rises, or would rise, ahead of
the tap that stops the count (P83, P78, P45 and P14 rise together on
it, 10,000,000 clocks apart).  Three readings:

| GPS 1 PPS | Divider epoch, t_internal | Count less the preset | Run |
| --------- | ------------------------- | --------------------- | --- |
| 0.250000037 s | 0.000000952 s, derived: no pin edge (the first P83 rise, 1.000000952 s, less 1 s) | 1,500,012 | `z3805a.three` |
| 0.700000037 s | 1.000000952 s, a P83 rise | 7,000,012 | `z3805a.late` |
| 0.250000037 s | 0.250000152 s after a sync of kind 3, derived: no pin edge (the P83 rise at 1.250000152 s less 1 s; "The sync field") | 4,000,004 | `z3805a.sync3` |

The GPS times are the testbench's, the counts and the P83 rises
printed; the first and third epochs are not edges of any pin.

These are with register 1's reference bit High, as
`counter_configuration_init` leaves it.  With it Low the count is one
tick more, 1,500,013 for the first reading, and the start's pulse
(AH.Y) one clock shorter (`z3805a.low`): the reference bit moves the
gate's opening by one clock, so 4,000,003 is the reference-High case.
Between arms register 1 keeps whichever reference the last
interpolator measurement left (`counter.md`, "Each second"), and the
firmware does count with it Low (**read**).  `counter_arm`
(`0x40c6a`) writes the bit only through its configuration reload:
`ti_registers_load_wanted`, while `registers_state` is
`COUNTER_WORK_WANTED`, writes the configuration's four registers,
register 1 `0x02`, the reference High, and marks them done;
`counter_configuration_init` asks for that, and nothing else in the
rewrite does (`third_party/firmware/rewritten/z3805a-3543b/ti.c`,
`third_party/firmware/rewritten/common/ti.c`, `ti.h`), so the first arm after
it counts with the reference High and later arms with what the last
measurement left.  `interpolator_calibrate` (`0x40248`) leaves it High
only when its first low-then-high pair is in range and exactly 200
apart; its searches and its nudge loop, which accepts 199 or 201, end
on a Low measurement (`interpolator_verify`, `0x4047c`, ends High).
Nothing corrects the count for the tick.

In a count AH.Y, the start's pulse, is High 114 ns with the reference
High (`z3805a.three`) and 14 ns with it Low (`z3805a.low`), where mode
3's two references give 100 ns and 200 ns (`z3805a.fast`); P81 carries
it inverted, a Low pulse (its I/O block inverts; **read**), and the
probes measure AH.Y, not the pin.  The widths are under the models'
nominal 1 ns a block, not the part's timing: the one clock between them
is the finding, not the nanoseconds.  The arithmetic absorbs the tick
only if the interpolator reads a 14 ns pulse 86 ns below its 100 ns low
reference, which the calibration trims to read 1 to 25 (`counter.md`,
"The interpolator's calibration"), with the byte unable to go below 0, so
it probably clips (**inferred**).  Whether the readings are off is for
a board.

So TI is t_internal − t_GPS, with t_internal the divider epoch whose
tap is the first after the GPS edge: TI runs from −0.4 s to +0.6 s.
This is the order `counter.md`'s "Which 1 PPS edges" reads best.

The count's other edges:

- **The first second.**  After RESET the internal 1 PPS rises on the
  10,000,000th clock edge after its release, and the tap on the
  4,000,000th stops a count started in the first second: the divider
  starts as if a 1 PPS had come at the release (the first epoch
  above).
- **Mode 3.**  With register 1's start bit High, the preset's release
  starts the count on a clock edge, its write's strobe (**read**: AI.QY
  takes `G = CG_X + ~CH_X·QY + ~CH_X·BI_Y`, CG.X the start bit, and
  FH.Y, the preset, resets AI).  The start
  bit's fall, written after it, makes the start's pulse, AH.Y, which
  lasts one clock with the reference bit Low and two with it High, the
  interpolator's two references, and status bits 0 and 1 both set.  RESET leaves the design in mode 3 and makes one such
  start, one clock long, on its first edge.
- **A GPS 1 PPS after the stop** does nothing: the count waits for the
  next preset.  One after the tap but before the arm's preset comes off
  is ignored, and the next starts the count, which the next second's
  tap stops.
- **The preset after a start** gives AH.Y a pulse one clock long: it
  resets AI and BJ at once and AH a clock later (CF.X).  The
  interpolator would measure it, but the firmware reads the
  interpolator only after the next start.

## The 1 PPS divider

(**read** from the equations, its nets' periods simulated) GI divides
by 4, and FJ.X, the prescaler's terminal JI.X a clock later through
HJ, clears it once a cycle, so GI.Y's period is 400 or 500 ns and
GI.X's 200 or 300 ns (`z3805a.fast`); a 6-bit counter on IG, JH, IH
and JI runs to 38, a cycle of 156 or 157 clocks, 625 clocks in 4
(**read**), which JI.X's period of 15,600 to 15,700 ns bears out
(`z3805a.fast`); KH to KK divide by 64, so JK.Y fires every 10,000
clocks; three decades follow, KF and KG, GE and GF, then HF, GG and HG,
their enables every 100,000 and 1,000,000 clocks and HH.X, the
terminal count, every 10,000,000; HG.Y is the top decade's states 4 to
7.

**Model hazards.**  JI.X and HH.X are terminal-count ANDs, and under
the models' 1 ns a block each pulses for a nanosecond when a carry
ripples into it: JI.X between its terminals (`z3805a.fast`: 8 pulses
under 5 ns removed), HH.X once a second, at the top
decade's carry 0.1 s before each terminal count
(`z3805a.three`: 2 pulses under 5 ns removed, the first at
0.900000953 s).
They are the model's, not the design's (`outputs.md`, "Simulation"):
every load of either samples on a GCLK edge after it settles (JI.X
loads HJ and JK; HH.X loads EK, IK's and IL's enables, and EI, which
feeds P83's output flip-flop), and the probe removes them before it
measures.

P83, P78, P45 and P14 rise together exactly 1,000,000,000 ns apart, the
first at 1.000000952 s with RESET risen at 1 us, and stay High 31.4 us
(314 ticks; `z3805a.three`, `z3805a.nogps`): a divider of exactly
10,000,000.  (**read**) The width is held through P83's own pad:
EI.Y = HH.X + P83·~KH.Y, with P83 the pad's input, so HH.X, the
terminal count, sets the pins, P83's registered output read back holds
them, and KH.Y's rise releases them.  A board load that pulled P83
could so change the width on all four pins (**inferred**).  The tap,
HG.Y, is 4,000,000 clocks into the divider, and with the stop's
pipeline it is the firmware's 0.4000003 s calibration (`counter.md`,
"The 0.4000003 s calibration").

**The seconds nibble** counts the internal 1 PPS: it reads 0 at 0.4 s,
after a GPS 1 PPS at 0.25 s, and 1 and then 2 after the internal
1 PPS at 1 s and 2 s (`z3805a.three`).

## The sync field

Written 0 then the kind with the preset on, as `counter_arm` does:

- kind 1 moves the next internal 1 PPS 100 ns earlier (1.000000852 s),
  and the count one tick lower (`z3805a.sync1`);
- kind 2 moves it 100 ns later (1.000001052 s), the count one higher
  (`z3805a.sync2`);
- kind 3 restarts the divider on the next GPS 1 PPS: the internal
  1 PPS then rises 1 s and 115 ns after it (one clock after the first
  clock edge after it), and the count reads 4,000,004 (`z3805a.sync3`);
  the 1 PPS the divider would have given 115 ns after the GPS edge does
  not come, and the seconds do not count one.

The step is GJ's, once, in the second's last millisecond: kind 2 makes
GK wait for GI's 3, one clock longer; kind 1 takes GI's 1, one clock
shorter; kind 3 resets the divider through EJ.X on the next GPS edge.

The field's change from 0 (DJ, a clock behind DI) marks a sync pending
(EK), which the divider's terminal count (HH.X, the internal 1 PPS)
clears, or kind 3's restart (EJ).  So a step acts once, at the next
internal 1 PPS, and a field left at its kind does nothing more
(**read**: EK.QX sets only where DJ.X is High and DJ.Y Low, and holds
until HH.X or EJ.Y; `F = ~EJ_Y·QX·~HH_X + DJ_X·~QX·~DJ_Y`).  Kind 3 with the internal 1 PPS coming before the
GPS 1 PPS is different: both step lines (GJ.X, GJ.Y) are asserted near
the terminal count, GK then holds back the prescaler's carry, and the
divider stops short of the internal 1 PPS, by a prescaler cycle
(**inferred** from the equations; no run prints it), the sync still
pending, until the GPS 1 PPS restarts it (`z3805a.sync_3_late`: no
1 PPS at 1.0 s; the GPS edge at 1.2 s clears EK, EJ.X restarts the
divider on its second clock edge, and the tap follows 0.4 s on).  What
a field changed from 3 while the divider is held does is not
simulated.

## Other outputs

(**read**) P2, P72, P76, P77, P79 (3-state, under AI.X), P81 and P84
are outputs:

- P81 is AH.Y inverted, the start's pulse the interpolator measures,
  Low on the pin; P72 is AH.X.
- P2 is tied Low: BA, a block whose F is the constant 1, through an
  inverting I/O block.
- P76 and P77 are BL.Y and BL.X, inverted.
- P79 is BJ.X, a constant 0 (`F = 0`), driven while AI.X is Low and
  3-state otherwise.
- P82, the GPS 1 PPS, is also P16's output, and P17, held High by the
  testbench, is P84's, inverted.

P83 is an input as well as an output ("The 1 PPS divider").  What the
board takes from P2, P76, P77, P79 and P84 is not traced.  No register
bit drives an alarm output: register 3 keeps only the sync field.

## Confidence

**Simulated** (the `check-readings-z3805a.<run>` runs above): the GPS 1 PPS's rising edge
starts the count and a divider tap 4,000,000 clocks after the internal
1 PPS stops it, a net 4,000,003 with the reference High; the divide
by 10,000,000; the seconds nibble counts the internal 1 PPS; kind 1
steps the internal 1 PPS 100 ns earlier, kind 2 100 ns later, kind 3
restarts it on the GPS 1 PPS; kind 3 with the internal 1 PPS first holds the divider until the
GPS 1 PPS; status bits 2 and 3 are two interpolator inputs, P5 and
P6, complemented; register 1's reference
bit moves the gate's opening by one clock.

**Read:** the count's bits (CF.QY, BB.QY, eleven two-bit stages) and
their resets; the start's three edges and the stop's four flip-flops;
modes 1 and 2 are mode 0; a sync field left at its kind does nothing
more; the clocks; the divider's stages; P83's pad holding the 1 PPS width;
P81, P2, P76 and P77 inverted, P2 tied Low; the register bits the
design keeps; the firmware counts with the reference bit Low too
(`counter.md`).

**Open:**

- All of the above on a board, and the pins' roles, which are read
  from the logic.
- What a sync field changed from 3 does while it holds the divider.
- How the interpolator reads a start pulse under 100 ns, as with the
  reference bit Low in count mode, where the firmware leaves it after
  some calibrations: AH.Y then lasts less than the low reference, one
  clock; and so whether such counts are a tick off.
