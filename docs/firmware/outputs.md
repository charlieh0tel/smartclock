# The pulse, time-tag and output commands

The 59551A of `097-59551-02` adds to the 58503A a programmable pulse
output, time-tag inputs and an IRIG-B output.  What the firmware does
with the commands for them, read in 58503A 3704 unless another image
is named.

## Present and stubbed in every image

`:SOURce:PULSe`, `:SENSe:DATA` and `:SENSe:TST1` to `TST4`,
`:OUTPut:PIN1` to `PIN8` and `:SOURce:PTIMe:PPS:EDGE` are in all seven
command trees (`../scpi/`), the Z380x's included, so their presence
says nothing about a unit's hardware.

- The programmable pulse stores its settings and drives nothing.  The
  `PERiod` and `CONTinuous:STATe` setters (`0x2da0a`, `0x2da70`) post
  records whose cells are `0x10223e` (period), `0x10223c` (state) and
  `0x10224e` (start); the record's function `0x47faa` gathers them
  and calls `0x47fa8`, a bare `rts`.  The same empty routine is in
  3633 (`0x47e82`), the 58503B (`0x4d126`), the Z3815A (`0x4be5e`), the
  Z3816A (`0x4646a`) and the Z3801A (`0x42b46`).
- `:OUTPut:STATe` likewise, in the Z3805A 3543B: the setting is saved
  in `nv_settings.output_state`, and its descriptor's done hook
  (`0x42b7c`) hands it to `0x42b18`, a bare `rts` beside the pulse's
  (`0x42b1a`).  Nothing else reads the setting but its query.
- `:PULSe:REFerence:EDGE`, `:PTIMe:PPS:EDGE` and `:SENSe:TSTn:EDGE`
  store nothing: their queries share the stub `0x28d70`, and their
  setters call `0x2bcfa(3, value)`, which for some values posts
  message 3 of the table at `0x2bd2a`, `Undefined header`, and
  otherwise returns.
- `:OUTPut:PINn:DELay` and `:FREQuency` are stubs: the setters
  (`0x2bf42`, `0x2bfae`) are `moveq #$e0,d0; rts`, and the queries go
  to the same stub.
- `:SENSe:DATA?` (`0x2eb8a`) reads a buffer through `0x2e84a`; where
  that buffer is filled was not traced.

## What the 58503A drives

- The FPGA at `0x304000` (chip select 5) is the interval counter and
  its interpolator.  Its write routine is `0x40784`, with five byte
  shadows at `0x1022bb` to `0x1022bf`; the field table at `0x413ac`
  has seven fields, none a period or a divider, and every caller is
  the interpolator module (`0x40000` to `0x41000`), the loop's
  interval reads (`0x44fba` to `0x45266`) or a pForth word
  (`0x1b2c4`).
- The latch at `0x300000` (in chip select 5's window with the FPGA,
  `restart.md`, "The chip selects"; shadow `0x100e0e`): a 3-bit
  field the self-test and health code set (`0x186ac`, `0x23b7e`), the
  GPS Lock, Holdover and Alarm lamps, and bits 6 and 7 the front-panel
  code sets (`0x53750`) (`58503a-3704.md`, "The front panel").
- The alarm, besides its lamp: PF5, low while it is on, and bit 2 of
  the FPGA's register 3 (`0x40828`; `58503a-3704.md`, "The alarm
  outputs").  The bitstream does not latch that bit: register 3 keeps
  only the sync field, bits 0 and 1, and the bus's bit 2 reaches no
  logic (`58503a-z3801a-trace.md`, "Register port").  What drives the
  rear-panel Alarm output is not traced
  (`../hardware-investigations.md`, item 25).
- Bits 4 to 2 of PORTGP, which 3633's DUART initialization writes
  (`0x1d5cc`, with 0) and no 3704 code does (`revisions.md`).  What
  those pins drive was not traced.
- Nothing sets the rate of the 1 PPS.

So no 58503A image drives a second pulse channel, a pulse period or a
time-tag input.  None has a two-second pulse mode either: no `PP2S`,
`ESEC` or option word selects one, and no setting reaches the
hardware.  The Z3815A alone names a PP2S, as a signal its health
monitor watches (bit 0x104 of the table at `0x5964a`; `PP2S Fail`
from `0x3429e`).  Its FPGA, like the 58503B's and the Z3816A's,
pulses P14 (and, in the telecom pair, P28) on alternate seconds, even
with every register left at its reset 0, no write needed
(`fpga-comparison.md`, "The five designs"); whether that is the PP2S
the monitor watches is not traced.

Not traced: what drives the rear-panel Alarm output, and the
time-code worker's output path (`0x13d28`).

## The Z3805A's lamps

097-z3801-01 (1-2, 2-9) gives the Z3801A six front-panel lamps: Power,
Enabled, Active, Alarm, GPS Lock and Holdover.  In the Z3805A 3543B
five are bits of the latch at `0x300000` (in chip select 5's window,
`restart.md`, "The chip selects"), which
`leds_update` (`0x464de`) writes whole at the end of each `pllp_task`
pass, about once a second; Power is not the firmware's.

| Bit | Lamp | Lit while | Query |
| --- | ---- | --------- | ----- |
| 0 | Enabled | `led_enabled` (`0x101e75`) is set | `:LED:ENABled?` |
| 1 | Active | `led_active` (`0x101e74`) is set | `:LED:ACTive?` |
| 2 | Alarm | `led_alarm` (`0x101e72`), set while `status_summary` is not 0 | `:LED:ALARm?`, `:LED:ALARm:MAJor?` |
| 4 | GPS Lock | `pll.led_gps_lock` | `:LED:GPSLock?` |
| 5 | Holdover | `pll.led_holdover` | `:LED:HOLDover?` |

Bits 3, 6 and 7 drive nothing the firmware names.  The self-test's
`lamp_test` (`0x184ee`) writes `0xff`, then 0, then each bit in turn,
`0x133` ticks apiece, and then `leds_update`: the flashing at power-up
the manual describes (1-8).

- *Enabled and Active* are the user's: `:LED:ENABled` and
  `:LED:ACTive` store them through their setting descriptors, and no
  other code writes them.  They live in the block at `0x101e70` that
  `pllp_task` copies to battery-backed RAM (`loop.md`'s table of
  3704's, +4 and +5).
- *GPS Lock* follows the loop's stage (`pll_stage_status`, `0x46524`,
  through the table at `0x4654a`): lit on entering startup PLL (5) or
  normal PLL (6), with log event 6 and status condition `0x1a`;
  cleared, with condition `0x1b`, on entering power-up, power-up
  recovery, holdover, holdover recovery, diagnostic or fatal error.
  Idle leaves it as it is.
- *Holdover* likewise (table `0x465ca`): lit on entering holdover (3)
  or holdover recovery (4), with condition `0x2a` (`0x28` if holdover
  is manual) or `0x2c`; cleared in every other stage but idle.
- *Alarm* is lit while any of `status_summary`'s six bits is, which
  `status_summary_update` (`0x46470`) recomputes each pass from the
  `:STATus:OPERation:HARDware` condition, so it goes out when the
  condition does:

  | Summary bit | From |
  | ----------- | ---- |
  | 1 | hardware bit 0, self-test failure |
  | 2 | hardware bits 1 to 3: the +15 V, −15 V and +5 V supplies |
  | 4 | hardware bit 7, the EFC at full scale |
  | 8 | hardware bits 8 and 9, GPS 1 PPS failure and GPS failure |
  | `0x10` | hardware bit 12, internal reference failure |
  | `0x20` | the holdover duration past `:ROSCillator:HOLDover:DURation:STATus:THReshold` |

  The oven supplies (bits 4 and 5), the EFC near full scale (bit 6)
  and the time-interval and EEPROM events (bits 10 and 11) do not
  light it.  The `es` message carries the summary byte
  (`es_build`).  `:LED:ALARm:USER`, which 097-z3801-01 (4-5) says
  chooses the alarm's conditions, does nothing here: its setter
  and query (`0x2d996`, `0x2d99a`) are `moveq #$e0,d0; rts`, the same stub as
  `:OUTPut:PINn:DELay`'s above, and `:LED:ALARm:MINor?` fails.
- `:LED:TMHValid?` answers `pll.led_tmh_valid`, which no code writes:
  always 0.

The last summary bit's flag (`0x101e88`) is set by `holdover` and
`holdover_recovery` once `pll.holdover_duration` passes the threshold,
with condition `0x2e` (`:STATus:OPERation:HOLDover` bit 3, "Exceeding
User-Threshold"), and cleared on regaining lock.  With it,
`pwma_force_level` forces the PWMA pin low, and releases it on lock,
as `loop.md` records for 3704; what the pin drives was not traced.
`:ROSCillator:HOLDover:DURation:STATus:THReshold:EXCeeded?` returns
the flag.

The hardware register's bits are set and cleared by conditions `0x34`
to `0x4b` of `status_conditions`, a pair for each bit (bits 10 and 11
by one event each), in the order 097-59551-02 (5-38) gives the
58503A's.  The health monitor's messages confirm three: `6`/`7` with
"15V is out of tolerance" and "now within tolerance" on bit 1, `8`/`9`
with "-15V" on bit 2, and `>` with "Secondary oven voltage is out of
tolerance" on bit 5, which the 58503A's list leaves out.

A Z3805A has a rear-panel TTL alarm output.  097-58503-12 (2-11) says the 58503B's, a TTL open
collector, "goes on and off with the Alarm indicator"; if the
Z3805A's does too, it hangs off latch bit 2 in hardware, since no
code drives a pin from `status_summary` but the latch.  The other
pins the firmware drives: PWMA, which follows the holdover threshold
alone; the DUART's OP2, the board's heartbeat LED (below); and OP7, set on a self-test failure by six
`_with_lamp` routines that `selftest_print_failure` runs at power-up
(`duart_output_bits`).  Which one
the connector is needs the board or a unit.

### The heartbeat

One LED of the bank on a Z3805A's board is a heartbeat.  Every image times it the same way: `hmon_task` counts
its passes, one every ten 1024 Hz ticks (`restart.md`), and every
`0x70`, about 1.09 s, flips one output bit, so the LED changes state
about every 1.1 s.  All but the 58503B flip a DUART output bit through
the output-port routine (`duart_output_bits`, `0x1e126`, in the
Z3805A).  The 58503B has no DUART: its CS8, at `0x200000`, is an 8-bit select
synchronous on ECLK (`chip-selects.md`, "58503B 1.01.04"), a 6800-bus
part's timing, and its serial interrupt's message at `0x2d64c` is
"ACIA isr signaling SIG_SIQ" (**inferred**: an ACIA).  It flips bit 3 of the latch at
`0x300000` instead: `0x3353e` passes mask 8 to `0x2379e`, which keeps
bits 3 and 4 of it (`0x18`) and calls `0x236fc`, which clears the
mask's bits in the latch's copy at `0x10119e`, sets the given ones and
writes the copy to `0x300000` (or, while the byte at `0x10255c` is
set, the byte at `0x10255d`).  The bit differs by generation:

| Images | Bit | Toggle |
| ------ | --- | ------ |
| 58503A 3633, 3704; Z3801A 3543; Z3805A 3543B | OP2 (mask 4) | `0x239c6`, `0x23c22`, `0x2372e`, `0x2486a` |
| 58503B 1.01.04 | latch `0x300000` bit 3 (mask 8) | `0x3353e` |
| Z3815A 4010, Z3816A 4001 | OP6 (mask `0x40`) | `0x36cfc`, `0x33e5e` |

A stopped or steady heartbeat points at `hmon` having stopped, or at the LED's own circuit.

### The rest of the bank

The bank on a Z3805A's board is six LEDs, and the DUART's output
port has six general-purpose bits, OP2 to OP7: `duart_init` clears
the OPCR, so none carries a counter or a status, and
`duart_output_bits` masks with `0xfc`.  The power-on code uses them
as a self-test display.  The output port register (OPR) holds "the
complement of the logic levels output at the output port pins"
(MC68681 data sheet, 4.2.12), so setting a bit (`SOPBC`, `0x20001d`)
drives the pin low and resetting it (`ROPBC`, `0x20001f`) drives it
high.  The code reads as if an LED lights on a high pin.  `smartclock.h`'s
`board_led` names the bits, which `duart_output_bits` takes:

| Bit | Lit when | Written at (Z3805A) |
| --- | -------- | ------------------- |
| OP2 | the heartbeat, flipping | `hmon_task` (`0x2487a`) |
| OP3 | the boot code entered the installer, the flash checksums or vector 43 having sent it there | `0x754`, right after the reset code's `SOPBC = 0xff` at `0x746` |
| OP4 | the reset code's CPU test failed (`0x100c24`) | `0x147b8`; the boot code's at `0x8f4` |
| OP5 | its RAM test failed (`0x100c26`) | `0x148ee`; `0xa2a` |
| OP6 | its DUART test failed, `duart_early_init`'s IVR and loopback checks (`0x100c2e`); at power-up, `duart_test` through `selftest_uart_with_lamp` | `0x1490c`; `0xa48`; `0x18d5e` |
| OP7 | a QSPI, FPGA, interpolator, internal-reference, GPS or supplies test failed at power-up, through the `selftest_*_with_lamp` routines in `selftest_print_failure`'s table (`structures.md`, "The self-tests' results") | `0x18cb6`, `0x18cda`, `0x18d06`, `0x18d30`, `0x18d84`, `0x18daa` |

Reset sets every bit, `SOPBC = 0xff`, and each failed test resets its
own and records it in a mask (`0x100c30`; the boot code's
`0x10ab2e`), which `duart_init` writes back whole (`ROPBC` the mask,
`SOPBC` its complement), so a failure stays lit after start-up.  A
hardware reset clears the OPR, which drives every pin high until the
reset code runs: all six would light for a moment at power-on.  The
installer writes the port nowhere, so OP3 stays lit while it runs.
OP0 and OP1, which the two routines `duart_op0_set` and
`duart_op1_set` drive, have no callers.

The 58503A (3633, 3704) and Z3801A write the same values at matching
places.  The 58503B, Z3815A and Z3816A do not: the last two write
OP7 at three places of their own (`0x24e24`, `0x2495c` and on), and
their bank was not traced.  The polarity is the inference above, not
an observation: a unit or the board's LED circuit settles it, as
it does the bank's order.

## The FPGA bitstreams

Every image carries two 3860-byte blocks, each starting `ff 20 07 86`,
the header and length count of a Xilinx XC3000-family bitstream.  In
the Z3805A, the loader `0x22da4` takes an index and reads that entry
of the table at `0x21e2c`, `fpga_configurations`, after a byte that
is the highest index it takes, 1 (`0x22dbc`; the check at `0x23984`
and `0x1891c` is the same).  Each entry is a start and an end address
(`0x22e94` to `0x22ea0`).  The table holds one entry, `0x20f18` to
`0x21e2c`, which is the first bitstream; an index of 1 would read its
entry from the second block's first bytes.  The boot code at `0x22d70`
loads index 0, `FPGA_CONFIGURATION_COUNTER` (`smartclock.h`), and
keeps it at `0x102533`, `pll.fpga_configuration`; the only other
caller, `0x18938`, reloads that stored index.  The loader drives PROGRAM with
PORTE bit 6, reads INIT on `0xfff90c` bit 3, PACTL's PCLKS, and reads
DONE on PORTF bit 3 (`0x22e6e`, `0x22f40`), which it drives low while
it waits for INIT (`0x22dea` to `0x22e44`).  The names file labels
the first block `fpga_bitstream`, `uint8_t[3860]`.  The second
block starts right after the table, at `0x21e36`.  No table entry
points to it, but `fpga_verify_chip` reads it from 5 bytes in,
`fpga_readback_mask` (`0x21e3b`), past its 40-bit header
(`fpga_readback_mask_header`), as the mask its readback is compared
under (`structures.md`, "ROM tables").  That it is the same in every
image fits a mask, which belongs to the part rather than the design.

Copies of the five designs are in `third_party/firmware/images/fpga/`.
`tools/fpga/iobdump.js` prints a bitstream's I/O blocks, and
`tools/fpga/netdump.js` the whole design as JSON, using the decoder of
the XC3000 viewer ("Netlists", below).

| Image | First bitstream | SHA-256 | Second block |
| ----- | ------------ | ---- | ------------- |
| 58503A 3633 | `0x1ff84` | `b9fcb082` | `0x20ea2`, `d64b97e1` |
| 58503A 3704 | `0x1fd16` | `b9fcb082` | `0x20c34`, `d64b97e1` |
| Z3801A 3543 | `0x1fddc` | `b9fcb082` | `0x20cfa`, `d64b97e1` |
| Z3805A 3543B | `0x20f18` | `aa4750d3` | `0x21e36`, `d64b97e1` |
| 58503B 1.01.04 | `0x2f5a8` | `f095d6b5` | `0x304c6`, `d64b97e1` |
| Z3815A 4010 | `0x316a6` | `3402456a` | `0x325c4`, `d64b97e1` |
| Z3816A 4001 | `0x2ffb8` | `0bb50333` | `0x30ed6`, `d64b97e1` |

The second block is the same in all seven.  The first comes in five
designs:
- The 58503A and the Z3801A share one, byte for byte.
- The Z3805A's differs from it in 1868 of 3860 bytes, the 58503B's in
  2347, the Z3816A's in 3315 and the Z3815A's in 3392.
- The Z3815A's and the Z3816A's differ from each other in 1686.

`cargo test -p smartclock-cli --test fpga_bitstreams` checks the
offsets, the hashes (their first four bytes) and the byte differences
against the images and the bitstream files.

Which pins or registers a difference touches cannot be read from the
bytes.

The CPU code around the FPGA is the same in the Z3805A and the Z3801A:
- the writes to the FPGA at `0x304000`;
- the latch at `0x300000` (`0x184f8` to `0x1851e`, `0x4651c`);
- the GPT setup (`0x12050` to `0x120b2`), apart from CFORC.

The Z3805A alone has a second output-compare timer, OC4 (`0x1406a` to
`0x1412c`).  It drives no pin: TCTL1 is cleared and PGP6 is an input.
No image has a command, option or model check that enables a pulse
output.

Decoded with that viewer (at the pinned commit, "Netlists") and its PLCC84
pinout, the 58503A's and the Z3805A's designs set the same I/O blocks
on every pin but six (`tools/fpga/ref/58503a-z3801a.tilediff`'s `iob`
lines): P14 and P45, below, and the bus's P71, P70, P67 and P65, whose
output registers take AL.X and AL.Y the other way round, as AL's two
outputs are exchanged in the 58503A's logic.  "Registered" means the output passes through
the I/O block's flip-flop, "unregistered" that it does not; either way
the pin is the FPGA's, not a jack's, and a jack is reached through the
board's drivers.

| Pin | 58503A and Z3801A | Z3805A |
| --- | ----------------- | ------ |
| P14 | output, unregistered, from P17 | output, registered, fast slew, from EI.Y |
| P45 | unused: no I/O block configured | output, registered, fast slew, from EI.Y |

So P14 differs in what it carries, not only in its register and slew:
the 58503A's passes P17's input straight through, the Z3805A's is the
divider's 1 PPS (`z3805a-trace.md`, "The 1 PPS divider").

The viewer also reports a bad clock-line multiplexer setting
("111111") on the Z3805A design's top-left, bottom-left and left-upper
clock lines, and the 58503A's top-left.  The setting is not bad: it is
these lines' clock taken straight from the global clock buffer
("Netlists", below), and the two lines only the Z3805A uses clock its
P14 and P45.  None of this has been checked on a board.

Which of these pins reach the rear-panel 1 PPS outputs is a board
question: `../hardware-investigations.md`, item 19.

The designs are traced one file each: `z3805a-trace.md` (the Z3805A,
the reference), `58503a-z3801a-trace.md`, `58503b-trace.md`,
`z3815a-trace.md` and `z3816a-trace.md`.

### Netlists

`tools/fpga/Makefile` runs the whole chain for an image, from the
repository root:

    make -f tools/fpga/Makefile z3805a    # .bit -> .json -> .v -> _tb -> .vcd, the fast run
    make -f tools/fpga/Makefile figures   # render every waves/*/*.json
    make -f tools/fpga/Makefile check     # rebuild every image, diff against tools/fpga/ref

It needs git and network access to github.com (for the viewer's
clone), node or headless Chrome, Python 3, Icarus Verilog 12, Yosys
0.52 (`check`'s structure diff, below, which fails if either is
missing or fails) and `pdftotext`, from poppler-utils (`check`'s pad
tables, "The pins").  The Makefile's images are its
testbenches: a receiver is added with its bitstream at
`third_party/firmware/images/fpga/<image>.bit`, a testbench
`tools/fpga/<image>_tb.v` (the nearest one copied; it carries the
`+vcd=FILE` block), one or more figure specs under
`tools/fpga/waves/<image>/`, drawn from the fast run's VCD, or
`tools/fpga/waves/<image>.<run>/`, from the VCD of a `READ_` run's
flags, and the references `check` diffs against,
`tools/fpga/ref/<image>.stats`, `.readout` and `.structure`, each
copied from `target/fpga/` once reviewed: `check` fails without them.
A long run is a `READ_` line in `tools/fpga/Makefile` and its
`tools/fpga/ref/<image>.<run>.readings`.

The decoder is the XC3000 viewer, which the Makefile clones into
`target/fpga/xc3000` at the pinned commit, `DECODER_COMMIT` in
`tools/fpga/decoder.mk`, a full hash (`fpga-comparison.md`,
"Provenance"), and records it in a stamp every decode depends on, so
that a new pin checks out and decodes again; the JSON, and the
Verilog's header, record the clone's own origin and commit, marked
`-dirty` if the clone has local changes.  That the clone is at the pin
with no local change is checked on every decode and every `check`, not
only when the stamp is made, and `check` fails on a JSON whose
recorded commit is not the pin exactly, so a later checkout or an edit
in the clone cannot pass.  Every decode, netlist and
simulation also depends on the tools that make it and on its run's
flags, so `check` against an existing `target/` is a fresh run of
whatever changed.  The viewer has no license, so none of its code,
formulas or tables is copied into the project: each tool reads it from
that local clone (`third_party/NOTICE`).  By hand, the chain's first
two steps are:

    tools/fpga/netdump.sh target/fpga/xc3000/html third_party/firmware/images/fpga/z3805a.bit PLCC84 > target/fpga/z3805a.json
    python3 tools/fpga/netlist.py target/fpga/z3805a.json --module z3805a_fpga > target/fpga/z3805a.v

| Tool | Reads | Writes | Checked by |
| ---- | ----- | ------ | ---------- |
| `netdump.js` (`netdump.sh` runs it with node, or in headless Chrome through `netdump.html`) | a bitstream, through the viewer's decoder | the design as JSON | `make check-fpga-decoder`, `make check-fpga-xapp`; `check-decoder-upstream`, the same netlists at upstream pull request #3 |
| `netlist.py` | that JSON | structural Verilog over `xc3000.v` | `make -f tools/fpga/Makefile check` |
| `xc3000.v` | | models of the logic block, I/O block, clock buffer and 3-state buffer | the testbenches |
| `<image>_tb.v`, run under `vvp` | the netlist | the readings, and with `+vcd=FILE` a VCD of the bench, its pins and internal nets | `check` (the read port, `tools/fpga/ref/<image>.readout`), `check-readings` (the long runs) |
| Icarus Verilog `-Wall`, Yosys `check` | the netlist and its testbench | warnings, and the combinational loops by the nets they pass through | `check` (`tools/fpga/ref/<image>.structure`) |
| `vcd_wave.py` | a VCD and a JSON spec | an SVG waveform figure | `figures` |
| `explainer.py`, with `page.py`, what the FPGA pages share | `tools/fpga/ref`'s references, `decoder.mk` and `vcd_wave.py`'s figures | the illustrated explainer, published at https://charlieh0tel.github.io/smartclock/fpga.html (`make fpga`) | `check-explainer`, part of `check`, that it renders |
| `detail.py`, with `detail_designs.py`, each design's elements as its trace gives them, `detail_bind.py`, the netlist model they are bound to, `detail_draw.py`, the chip and gate-level drawings, and `sheet.py`, the shapes they are drawn as; each drawing opens full screen through `tools/zoom.py` | the decodes' JSON and `tools/fpga/ref`'s references | the detailed diagrams, published at https://charlieh0tel.github.io/smartclock/fpga-detail.html (`make fpga-detail`) | `check-detail`, part of `check`, that it renders: every element's blocks in use, every pin in use with a role, every output an element's, every section cited a heading of its document, every chip-drawing wire a net and every element in a block, and no collision in either drawing (a wire through a box, wires along one line, text on text, a box or a wire, text out of its box), and its own test: each kind of error refused |
| `netstats.py` | the JSON | the logic blocks, I/O blocks and nets a decode uses, their modes, flip-flops and clock loads | `check` (`tools/fpga/ref/<image>.stats`) |
| `tilediff.py` | two designs' JSON | the logic blocks and I/O blocks configured or connected otherwise between them | `check` (`tools/fpga/ref/58503a-z3801a.tilediff`, and its own test: each of a list of concrete changes to a block, written out apart from what the comparison reads, output selection among them, is reported) |
| `blockmatch.py` | two designs' JSON and a match file pairing their blocks | each pair's outputs and flip-flops compared as functions of the nets they read | `check-blocks`, part of `check` (`tools/fpga/58503b-z3805a.match`, `58503b-58503a-z3801a.match`, and its own test: each of a list of concrete changes to a block is reported) |
| `probe.v` | a net under a testbench | its edges, periods and widths, with `+probe`, its pulses under 5 ns removed | `check-readings` |
| `pincheck.py` | the viewer's XC3042 pad tables | agreement with the data sheet ("The pins", below) | `check` (`tools/fpga/ref/pins.txt`) |

`netdump.js` writes what the decoder found: each used or configured
logic block's two function tables, their inputs, its flip-flops' data,
clock inversion, enable and reset, its X and Y; each used or
configured I/O block's input, output, 3-state control, pull-up and
die-edge clock line; the clock buffers and lines; 3-state buffers and
pull-ups; every pad's package pin; and every net, by the pins it
connects.  A block is configured when it decodes otherwise than a
blank one, so one configured but connected to nothing is reported
too, with no pins.  The part is the viewer's family, named as the
bitstream shows it: an XC3000A or XC3100A design is
`XC3042A/XC3142A`, since the two cannot be told apart, and one that
uses nothing only they have is `XC3042`.  Selections use XACT's names:
in mode F both function generators are F, in mode FGM both are M
(DSREF3, Table 4-13), where the viewer says G for the second.

`netlist.py`'s models are written from the data sheet (Figures 4 to 6,
7-7 to 7-10, and 7-16 to 7-17) and model function, not timing.  Names
are the decoder's: a logic block is its tile (`AB`), an I/O block its
pad (`PAD7`), a net the pin driving it (`AB_X`), a port its package
pin (`P7`).  `vcd_wave.py` draws every edge at the time the simulator
recorded it rather than placed by hand, in the browser views' style;
its SVG uses the page's theme tokens with literal fallbacks, so it
renders both embedded and standalone.  `netstats.py` counts a block as
used when a routed net touches it, the configured tie-down blocks
(unloaded outputs) included.

The viewer's faults that `netdump.js` corrects, and which SmartClock
designs each touches, are tabled in `fpga-comparison.md`, "Decoder
faults"; the detail is in `tools/fpga/xact/README.md`, "The viewer's
faults".

All five bitstreams were built with MakeBits's tie-down (**inferred**;
`makebits -t`, DSREF2 7-16: "all unused interconnect is tied to a
known level"): every logic block the design does not use is
configured as F = 0 and G = 0, the tie-down's sources, and many of each design's programmed
switch-matrix connections reach no pin the design uses, where an XACT
design built without `-t` has none (`tools/fpga/xact/designs`), and
MakeBits `-t` on XAPP003's `CFSD20H` gives the same picture.  So a net's
routing is often far larger than its loads need, and an output that
drives only tie-down interconnect, such as DL.Y, comes out unloaded.

The "bad mux 111111" is the viewer reading a die-edge clock line's
multiplexer: one bit inverts the line, one takes its direct input
rather than its branch, and four choose the branch's input.  It accepts
only the branch, so a line on its direct input is reported bad, yet it
still connects the direct input, which is the global clock buffer's
net: the data sheet's "This net may also be programmed to drive the die
edge clock lines for IOB use" (7-17).  The traces agree: GCLK reaches
the output flip-flops of P83 and P78 on the top-left line, P45 on the
bottom-left and P14 on the left-upper.  The Z3815A's and Z3816A's
"111110" on the left-lower line is the same with the inversion bit
clear, but that line's direct input is the alternate clock buffer's net,
ACLK.  Which polarity that bit gives the output and input flip-flops
is the viewer's reading; the data sheet says only that each line's
polarity is programmable (Figure 4, 7-7).

#### The pins

`pincheck.py` reads the data sheet's 132-pin PGA table (7-70), which
lists all 118 of the XC3042's pads in order around the die, 96 of them
I/O blocks, and checks the decoder's PAD1 to PAD96 against it: all 96
agree.  The 84-pin PLCC table (7-67) names its pins' functions but not
their pads; all 74 PC84 pins the decoder bonds are the table's I/O pins,
in the same order, and the 35 with a function of their own (A0 to A15,
D0 to D7, TCLKIN, BCLKIN, M2, HDC, LDC, INIT, DOUT, CS0, CS1, RDY/BUSY,
XTL2) sit on the pads the 132-pin table gives those functions.  The
dedicated clock pads agree with the text too: TCLKIN "the second pad
from the top of the left die edge" (7-17), PAD96, and BCLKIN "the third
pad from the bottom of the right die edge", PAD47.  No mismatch.  Which
pad a generic I/O pin between two named ones is, the data sheet does
not say.

The bus is not on D0 to D7: it has six of them (D1 to D6, P70 to P58)
and P59 and P71 (RDY/BUSY) in place of D0 (P72) and D7 (P56).  That is
consistent.  The firmware's loader, `fpga_program`
(`0x22da4` in the Z3805A), shifts the bitstream out through the QSPI
(devices 6 and 9, `structures.md`, "The QSPI's devices") with PROGRAM,
INIT and DONE on the CPU's pins ("The FPGA bitstreams", above): slave
serial mode's interface (**inferred**; the mode pins are the board's),
in which only DIN, D0, carries configuration data (7-40); D1 to D7 and RDY/BUSY are ordinary I/O after
configuration, and a bus may use any pins.  D0 itself, P72, is an
output after configuration (AH.X), on the line that carried DIN.

#### Simulation

`make -f tools/fpga/Makefile check` compiles each netlist with Icarus
Verilog 12 (`-Wall`) and runs Yosys 0.52's `check` after `proc;
flatten`, and diffs what they report with
`tools/fpga/ref/<image>.structure`: no warning, and no undriven net or
conflicting driver, in any of the five.  The Z3815A's and Z3816A's list
a combinational loop through LD.QY and one through LE.QX, which are the
design's: LD.X resets LD and LE and reads LE's QX and LD's own QY, a
flip-flop pair that clears itself.
An input-only I/O block has no pad driver (a constant 3-state driver
would be a second driver of the pad), and each block's outputs take a
nominal 1 ns (`xc3000.v`; routing none): a flip-flop clocked by a net
would otherwise race the logic that net also drives, as the bus's
output flip-flops, clocked by AL.X and AL.Y, which also select the
read multiplexer, would latch the next value.  The delays are picks,
not the part's timing, which the data sheet gives from 7-41 and the
models leave out, so a simulated pulse width is the model's, not the
hardware's.

The one delay a block also makes hazards: a combinational output whose
inputs change on one clock edge by paths of different depth can pulse
for a nanosecond or two before it settles, as a terminal-count AND does
when a carry ripples into it.  Such a pulse is the model's, not the
design's, and the loads that matter sample those nets on a clock edge
after they settle.  `tools/fpga/probe.v` removes every pulse, High or
Low, shorter than 5 ns, five block delays, before it measures a net,
and ends its line with how many it removed and when the first began;
the traces name the nets that have them, and `fpga-comparison.md`,
"Reproduce", lists where they appear.  The filter removes the design's
own short pulses too: the Z3815A's and Z3816A's LE.X or LD.Y,
whichever lags, at any phase: the side that sets second clears both
within two block delays, so its pulse is under 5 ns.  The testbenches' own pin
edges are the pins' as simulated.

"No undriven net" is weak evidence of a complete decode: `netlist.py`
ties Low each enabled input the decoder traced no source to.  There
are none (`unrouted 0` in each `tools/fpga/ref/<image>.stats`).  An
earlier decode left two, both the decoder's faults (above): the
Z3816A's JH's fourth input, which is E, DG.X, once the mode-F fourth
input is read as XACT sets it, and, in the Z3815A and the Z3816A,
PAD86.OK, the clock of P20's output register, which is ACLK.

The eight bus pins have no pull-ups: their pull-up bit is a 3-state
multiplexer bit when the output is used.  An undriven input is
Verilog's x, which the models' tables pass on, so the testbenches hold
the bus High between cycles (`tri1`), and read the same with it held
Low, as a design that reads the bus only on a write would.  What the
board's bus does between cycles is not established.

`make -f tools/fpga/Makefile <image>` runs the testbench's fast path,
`+seconds=0 +registers +interpolator`: the preset, every address's
read and the interpolator's references.  Each trace names its long
runs as `check-readings-<image>.<run>` targets, whose flags are the
Makefile's `READ_` lines (`time make` for how long one takes).

`tools/fpga/z3805a_stimulus.v` runs the Z3805A's netlist from a
stimulus file instead, register writes and reads, GPS 1 PPS edges and
the interpolator's byte at given times, printing each read, each rise
of P83 and each pulse of AH.Y.  `check-readings-z3805a.sync_3_late`
runs it on `tools/fpga/z3805a.sync_3_late.stim`.

The netlist is kept generated, in `target/`, not committed: it is the
vendor's design re-expressed, and making it needs the viewer, which
`check` and firmware CI clone at the pin.  When it is committed, it
belongs beside the bitstreams in `third_party/firmware/images/fpga/`,
noted in `third_party/NOTICE` as derived from them, as the decompiled
C is.
