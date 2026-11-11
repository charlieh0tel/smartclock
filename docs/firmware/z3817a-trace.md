# The Z3817A FPGA

Traced against the Z3805A's design (`z3805a-trace.md`), the reference.
The Z3817A 3752's bitstream is a sixth design, not a variant of one of
the five: it differs from every other bitstream in most of its 3860
bytes (`outputs.md`, "The FPGA bitstreams"), and its blocks sit in
other tiles, so `tilediff.py`, which compares tile by tile, has nothing
to say about it.  What it shares with the 585xx and Z3805A designs is
their kinds of logic, placed anew: the register port, the start and
stop, the 1 PPS chain's prescaler and decades, and a count of ripple
Johnson stages.  What it lacks is the interpolator.

## At a glance

| | |
| --- | --- |
| Images that load it | Z3817A 3752 |
| Bitstream | `third_party/firmware/images/fpga/z3817a.bit` |
| Part | XC3042, PLCC84 |
| Logic blocks, I/O blocks, nets | `tools/fpga/ref/z3817a.stats` |
| Clocks | one global clock buffer, GCLK, from P7 through general routing; P84 clocks the P84 counter |
| Oscillator the divide implies | 10 MHz (a divide by 10,000,000) |
| 1 PPS input pins | P82 and P84, register 4's bit 0 selecting which starts the count |
| 1 PPS output pin | P78 |
| Other outputs | P16 (P82 passed through), P19 (GCLK / 65), P72 (a pulse every 10,000 clocks) |
| Differences from the reference | no interpolator inputs, no start pulse out; a 32-bit count; a fifth register, the start's select; a second 1 PPS input with a counter of its own |

## Method and confidence

How the designs are decoded and simulated, and how far either can be
trusted, is `fpga-comparison.md`'s ("Provenance"), as are the glossary
and the confidence words.  Every net here is **read** from the netlist
`tools/fpga/netlist.py` writes (`make -f tools/fpga/Makefile
target/fpga/z3817a.v`).  Statements marked **simulated** are printed by
Icarus Verilog running it, driven by `tools/fpga/z3817a_tb.v`, which
plays the register port as the 3752 firmware drives it (below);
**inferred** is reasoning from those; **open** is not established.
What the firmware does is read from the image, `third_party/firmware/images/z3817a-3752.bin`,
by disassembly; no decompiled C or names file exists for it.

    make -f tools/fpga/Makefile z3817a                         # decode, build, the fast path
    make -f tools/fpga/Makefile check-readings-z3817a.fast     # +seconds=0 +registers: the preset, the read map
    make -f tools/fpga/Makefile check-readings-z3817a.three    # +seconds=3: three readings, the 1 PPS edges
    make -f tools/fpga/Makefile check-readings-z3817a.select   # +seconds=2 +select=1 +p84=600000037: P84 starts the count

Every simulated figure quoted here is printed by one of those runs,
which diff against `tools/fpga/ref/z3817a.<run>.readings`.  Times
within a clock are under the models' nominal 1 ns a block, not the
part's timing (`outputs.md`, "Simulation").

## Clocks

(**read**) P7 drives the one global clock buffer the design uses,
GCLK, through general routing, as in the Z3805A; ACLK is unused.  P84
clocks two logic blocks, HG and HH, the P84 counter (below).  Other
blocks are clocked from logic: BI, the start, by AG.X, the selected
1 PPS; BD and the count's fifteen ripple stages by the stage before;
BJ, the stop, by EH.Y, a buffer of the divider's tap GH.Y; AH by AI.X.

## Register port

(**read**) The port is the Z3805A's pin for pin: P61 the select, Low;
P75 R/W, High to read; P69, P68 and P66 ADDR0 to ADDR2; P71, P70, P67,
P65, P62, P60, P59 and P58 DATA bits 0 to 7, driven together under
GL.X's 3-state control, P71, P70, P67 and P65 through output flip-flops
clocked by AL.X or AL.Y, P67 never read.  GL.Y, the select and R/W
both Low, is sampled into GK, whose GK.Y is a one-clock write strobe;
FL and EL decode it with the address:

- **Register 0** (FL.X): bit 6 alone, LH, the preset, which resets
  every stage of the count, the start and the stop.  There is no mode
  field.
- **Register 1** (FL.Y): bits 3 to 7, the read address, in FJ (bits 3
  and 4), DJ (5 and 6) and EJ (7).  Bits 0 to 2 are not kept: no start
  or reference bit.
- **Register 2** decodes to nothing.
- **Register 3** (EL.X): bits 0 and 1, IG, the sync field.
- **Register 4** (EL.Y, ADDR2 High): bit 0, AG.QY, the start's select.
  `AG.X = AG.QY ? P84 : P82`.

The 3752 firmware keeps the images at `0x1024f7`, five bytes, and its
field table at `0x45d7a` has five entries (register, mask, kind): the
preset (0, `0x40`, flag), the address (1, `0xf8`, field), the sync (3,
`0x03`, field), register 4's bit 0 (4, `0x01`, flag), and (1, `0x01`,
flag), whose register byte `0x4539e` uses as the address it reads.
`fpga_write` is `0x451c8`, `fpga_read` `0x451dc`, the byte read
`0x4569c` (reg + 1's low nibble high, then reg's), the bit setter
`0x4549a` (**read**).  Register 4 is written only through `0x4538c`,
which sets entry 3 to its argument; its two callers, `0x4cacc` and
`0x4ccd8`, pass 0, so the firmware starts the count from P82 alone,
as far as these callers go (**read**).  The register loads (`0x457ac`,
`0x4595c`) write registers 0 to 3, not 4.

A read drives all eight bits (**read**):

- bits 4 to 7: with the address's bit 0 clear, the seconds counter, JH
  and JI, enabled by GE.X, the internal 1 PPS; with it set, the P84
  counter, HG and HH, a four-bit binary counter of P84's rises that
  nothing resets.  This is the Z3815A's arrangement ("The P84
  counter" there).  The firmware reads the second at address 1
  (`0x4539e`), twice over until two reads agree, up to twenty times;
  what it does with it is not traced (**open**).
- bits 0 and 1: with the address's bit 4 clear, the stop bit (CI.X)
  and the start bit (BI.X), as in the Z3805A; with it set, DH.Y, the
  count's read multiplexer, on AL's two phases.
- bits 2 and 3: with the address's bit 4 clear, FI.X and GJ.X, two
  flags of the 1 PPS chain (below); with it set, CG.X, the count's
  other read multiplexer.

## The count

(**read**) A 32-bit count:

| Bits | Blocks | Form |
| ---- | ------ | ---- |
| 0 | BH.QY | a toggle on GCLK, while AI.Y (the start) is High and DI.Y (the stop) Low; LH resets it |
| 1 | BD.QY | one flip-flop, `G = ~QY`, clocked by BH.Y |
| 2 to 31 | BE, AF, AE, AC, BC, DB, CB, BB, AB, BA, AA, AD, AJ, AK, BL | fifteen two-bit ripple Johnson stages, each clocked by the one before |

The stages are the Z3805A's kind, eleven of them there and fifteen
here, and the read the same: with the preset on, every nibble from
`0x12` to `0x19` reads 5, the count `0x55555555`
(`z3817a.fast`), which is the preset the firmware subtracts (the
64-bit constant at `0x45a92`, **read**).  Register 1's bits 3 and 4,
in FJ, the address's bits 0 and 1, pick a group of stages (DD and DF
decode them), AL.Y one of each stage's two bits.
`0x10` and `0x11`, the Z3805A's interpolator byte, read `0x0f`
(`z3817a.fast`).

The firmware reads the count as four bytes, `0x18`, `0x16`, `0x14` and
`0x12` (`0x456fc`), subtracts the preset as a 64-bit integer, and
converts it (`0x45852` to `0x458b0`, **read**): the count times 1.0e-7
(`0x3e7ad7f29abcaf48`), plus 5.0e-8 (`0x3e6ad7f29abcaf48`), half a tick,
less the calibration, then the long at `0x10221a` added.  The
calibration is 0.4000003 s (`0x3fd9999adbb8f8da`, stored by `0x459bc`),
the Z3805A's.  The routines' operations are read from their bodies
(`0x6677e` multiplies in place, `0x654d0` adds, `0x6547a` subtracts);
which is which is **inferred** from their sign handling.  There is no
interpolator term: an interpolator read, `0x4574c`, reads `0x10` and
complements it, and nothing calls it (**read**).

## Start, stop and status bits

(**read**) The selected 1 PPS, AG.X, clocks BI, whose QY, the start
bit, sets on its rising edge (its DI is CH.X, a constant 1; LH, the
preset, through BH.X, resets it).  AI.QY takes it on the next GCLK edge
and holds it, and BH, bit 0, counts from then.  AH, the Z3805A's start
pulse, is here two flip-flops whose data is 0: its outputs are a
constant Low, so CI.F is BJ.X alone and no pin carries a start pulse.
CD samples AG.X on GCLK, and CD.X, its rising edge a clock wide, feeds
IF, sync kind 3's restart.

The stop is the Z3805A's: BJ, clocked by the tap through EH, takes
BI.X, so only a tap after a start stops the count; CI.QX, CI.QY, DI.QX
and DI.QY carry it on GCLK, and DI.Y holds BH.  BJ, DI and BH are the
Z3805A's CI, DG and CF block for block (`check-blocks`,
`tools/fpga/z3817a-z3805a.match`); CI is its CJ but for the input it
ORs in, AH.Y here, constant Low, and the start, BI and AI, is not its
AH, BI and AI, which have the reference bit and the start's pulse.

(**simulated**) The start bit sets on the 1 PPS's rise, and AI.Y on
the first GCLK edge after it, 15 ns after an edge 37 ns into a 100 ns
clock (`z3817a.three`, the AI.Y probe's "after reference"); the tap,
GH.Y, rises 400,000,000 ns after P78 and the stop bit, CI.X, 3 ns after
that (`z3817a.three`).  With the P82 1 PPS at 0.250000037 s and the
internal 1 PPS at 1.000000952 s, each of the three readings is
1,500,013 ticks over the preset (`z3817a.three`): one more than the
Z3805A's 1,500,012 with its reference bit High, and the Z3805A's own
figure with the bit Low (`z3805a-trace.md`, "Start, stop and status
bits").  So here, with no reference bit, the count opens on the first
clock edge after the 1 PPS, as the Z3805A's does with the bit Low, and

    count + fraction = 4,000,004 + (t_internal − t_1PPS) / 100 ns

(**inferred**, from those counts and the Z3805A's derivation), with
the fraction the time from the 1 PPS edge to the next clock edge,
which nothing measures.  TI runs from −0.4 s to +0.6 s, as in the
Z3805A (**inferred**).

**The start's select.**  With register 4's bit 0 set and a 1 PPS on
P84 at 0.600000037 s, P84 starts the count: AI.Y rises at
0.600000052 s and the reading is 8,000,013 ticks over the preset, the
first tap after it being the one 0.4 s after the internal 1 PPS at
1.000000952 s (`z3817a.select`), which is the formula above.  The
`0x01` read's high nibble counts P84's rises, 1 and then 2
(`z3817a.select`); with no P84 it stays 0 (`z3817a.three`).

**The status bits.**  Bits 0 and 1 are the stop and start bits, as in
the Z3805A; the firmware's wait, `0x45410`, tests bit 1 (its caller
passes 2) and then bit 0 (1), polling every 10 ms up to 512 times
(**read**, `0x45834` to `0x45850`).  Bits 2 and 3 are not the
interpolator's: they are FI.X and GJ.X, flags of the 1 PPS chain
(below); a poll that lands while GJ.X is High reads bit 3 set
(`z3817a.three`: `0x0a` at the start, `0x0b` at the stop).  The
firmware does not test them.

## The 1 PPS divider

(**read**) The Z3805A's chain, re-placed and re-encoded: JA, a
two-bit counter on GCLK, and JB.X, its terminal, a prescaler of 4 or 5
clocks; IC, KC, JC and ID, a counter enabled by JB.X whose terminal
ID.Y, through KD and LD, restarts JA and the counter, HC folding in the
sync step; FA, GA, FC and GC, a six-bit counter enabled by LD.X, and
HD.Y its terminal; then three decades, GF and GG (HF.Y), FE and FF
(GH.X), FG and FH, and GE.X, the terminal count, the internal 1 PPS.
GH.Y, the top decade's late states, is the tap.

**Block by block.**  `make -f tools/fpga/Makefile check-blocks` checks
each of these blocks as the same function of the same nets as its
Z3805A counterpart, in `tools/fpga/z3817a-z3805a.match`: JA, JB, IC,
KC, JC, ID, JD, KD and LD are the Z3805A's GI, HI, IG, JH, IH, JI, II,
HJ and FJ; FA, GA, FC, GC, FB and HD its KH, KI, KJ, KK, JJ and JK;
GF, GG, HF, FE, FF, FG, FH and GH its KF, KG, JG, GE, GF, HF, GG and HG.
Many of them hold their two flip-flops, or drive their two outputs,
the other way round, which the match file renames.  Four are not the
Z3805A's, and the file says how: GB, whose X ORs in HE.X, the kind 1
step; HC, the prescaler's step, the Z3805A's GK in another encoding;
and GD and GE, the Z3805A's HH split in two, GE ORing in IF.Y, kind 3's
restart ("The sync field").

(**simulated**, `z3817a.three`) JB.X's period is 400 or 500 ns; LD.X's
15,600 to 15,700 ns, the Z3805A's 156 or 157 clocks, 625 in 4; HD.Y's
1,000,000 ns, 10,000 clocks; HF.Y's 10,000,000 ns; GE.X's
1,000,000,000 ns.  P78 rises 1,000,000,000 ns apart, the first at
1.000000952 s with RESET risen at 1 us, and stays High 406,500 ns: a
divider of exactly 10,000,000, the Z3805A's to the edge.  The seconds
nibble counts GE.X: 0, 1, 2 at the three stops.

**Model hazards.**  GE.X pulses for a nanosecond at the top decade's
carry 0.1 s before each terminal count (`z3817a.three`: 2 pulses under
5 ns removed, the first at 0.900000953 s), and ID.Y between its
terminals, as the Z3805A's HH.X and JI.X do; every load samples them
on a later GCLK edge (`fpga-comparison.md`, "Reproduce").

## The sync field

(**read**) Register 3's two bits are IG.QY and IG.QX.  IG, the field's
register, JE, its copy a clock behind, IF, the restart, HE, the step
lines, and CD, the 1 PPS's rising edge on GCLK, are the Z3805A's DI,
DJ, EJ, GJ and DH block for block, CD sampling AG.X, the selected
1 PPS, where the Z3805A's DH samples P82 (`check-blocks`,
`tools/fpga/z3817a-z3805a.match`).  So with both bits set, kind 3,
and JF.Y, the pending sync, High, IF.Y takes CD.X, and IF.QX resets
the decades and the six-bit counter, as the Z3805A's EJ.X restarts its
divider; and HE.X and HE.Y, the step lines of kinds 1 and 2, come with
the pending sync in the second's last millisecond, as GJ's do.

Three parts are not the Z3805A's:

- **The step.**  HE.X reaches the prescaler through GB.X, which ORs
  it with the Z3805A's IJ.Y, and HE.Y directly, into HC, which steps
  ID's terminal: the Z3805A's GK, which reads both step lines and IJ.Y,
  in another encoding.
- **The pending sync.**  JF.QY sets on the field's change from 0, as
  EK does, but clears on IE.X, `IF_Y + GE_X·(~IG_Y + ~IG_X)`: kind 3's
  restart, or the internal 1 PPS for kinds 1 and 2 only.  The Z3805A's
  EK clears on the internal 1 PPS whatever the kind, so here a kind 3
  stays pending across internal 1 PPS edges until a 1 PPS restarts the
  divider.
- **The restart's 1 PPS.**  GE.X, the internal 1 PPS, is
  `IF_Y + FG_Y·FG_X·FH_X·HF_Y`: kind 3's restart is itself an internal
  1 PPS, on the 1 PPS's GCLK edge, as in the Z3816A, where the
  Z3805A's restart gives none until a second later.

The 3752 firmware's sync writer is `0x4557a`, called from the arm with
the kind at its configuration's offset `0x10` (**read**); which kinds it
writes is not traced.

## Other outputs

(**read**, periods **simulated** in `z3817a.three`)

- P16 is P82 passed through, unregistered, with fast slew, as in the
  Z3805A.
- P19 is EE.Y, a counter's bit, GCLK / 65: 6500 ns, High 3200 ns.  The
  58503B's P19 is GCLK / 65 too (`fpga-comparison.md`, "Outputs").
- P72 is GJ.Y, a pulse every 10,000 clocks, set on HD.Y and cleared on
  the next prescaler tick: 1,000,000 ns apart, High 400 ns.
- No pin is P2, P76, P77, P79, P81, P83 or P84 as an output: the
  interpolator's start pulse (P81) and its controls are gone.

## Pins against the board

No Z3817A board has been traced, so every pin's role is the logic's.
The FPGA's pins are the register port, P7, P82, P84, P78, P16, P19
and P72.  The two time references' 1 PPS inputs are P82 and P84 if
the board wires them there (**inferred**: they are the design's only
two 1 PPS inputs, P82 the input the other designs give the GPS
1 PPS).  Which reference reaches which pin is **open**; the firmware
selecting P82 alone (above) fits a board that switches the references
ahead of it.

## The 10 ns steps of `:SYNC:TINT?`

The bench Z3817A reports TI in 10 ns steps, where the other receivers
report 0.1 ns.  The netlist does not explain that by a faster count:

- There is no interpolator: no input byte, no start pulse out, no
  reference bit, and the firmware's reading adds no interpolator term
  (**read**).  So a reading resolves one count tick.
- The tick is one GCLK period, and GCLK must be 10 MHz for P78 to be a
  1 PPS: the divide is 10,000,000 (**simulated**).  A 100 MHz clock
  would make P78 10 Hz.  The firmware converts at 1.0e-7 s a tick
  (**read**).
- So each reading is quantized to 100 ns, offset by the half tick the
  firmware adds (**inferred**).

10 ns steps are then the firmware's: an average or filter of 100 ns
readings over ten or more seconds would give them (**inferred**).
Where `:SYNC:TINT?`'s value comes from in the 3752 image is **open**.

## Confidence

**Simulated:** the preset `0x55555555` and the read map
(`z3817a.fast`); the divide by 10,000,000 and P78's width; the tap at
4,000,000 clocks and the stop 3 ns after it; the readings, one tick
over the Z3805A's reference-High figure; register 4's bit 0 moving the
start to P84; the P84 counter; P19 and P72.

**Read:** the register decode, the 32-bit count, the absence of the
interpolator, the read multiplexer, the divider's stages, the firmware's
register writes, reads and conversion; the divider, stop and sync
blocks that are the Z3805A's, and how the rest differ (`check-blocks`).

**Inferred:** the 4,000,004 offset and TI's range; 10 MHz on P7; the 10 ns steps from
firmware averaging; P82 and P84 as the time references' 1 PPS.

**Open:**

- All of it on a board, and the pins' roles.
- The sync field's steps (no run writes it) and which kinds the 3752
  firmware writes.
- What the firmware does with the P84 count and with status bits 2
  and 3, and where `:SYNC:TINT?` comes from.
- Whether the firmware's 0.4000003 s calibration, against the design's
  4,000,004 and the half tick it adds, leaves a constant offset, and
  whether the long at `0x10221a` takes it up.
