# XACT, for checking the FPGA decoder

Xilinx's XACT 6.0.1 (1996), run headless under DOSBox-X, builds XC3000
bitstreams from LCA designs, so that what `../netdump.js` decodes from
a bitstream can be checked against the design XACT built it from.  The
SmartClock bitstreams' own designs are lost; these are designs written
to test the decoder.  "The viewer's faults", below, has what the checks
found and fixed, and `../../../docs/firmware/fpga-comparison.md`,
"Decoder faults", which SmartClock designs each touches.

XACT is not ours.  It is fetched from archive.org into `target/xact/`,
which git ignores, and runs only inside DOSBox-X: nothing from the
archive runs on the host, and nothing of Xilinx's is committed.  The
designs in `designs/` are ours, and so are the routed LCAs and
bitstreams XACT made of them.  XACT's own application-note designs
(XAPP) are Xilinx's, so `xapp.sh` builds them from the fetched tree and
they are never committed.

## Files

- `fetch.sh`: downloads the archive.org item's two archives, checks them
  against `SHA256SUMS`, and unpacks the 6.0.1 CD's `XACT` and `XAPP`
  trees into `target/xact/iso_960628/` (`XACT_HOME` moves it).  The
  second archive, the patches, is checked but not unpacked: the CD's
  `apr` and `makebits` need no key.
- `run-xact.sh WORKDIR 'COMMAND' ...`: one DOSBox-X session, XACT on
  `C:` read only, WORKDIR on `D:`, the commands in order;
  `dosbox-x.conf` is its configuration.  A tool that waits for input is
  stopped after `XACT_TIMEOUT` seconds (300).
- `build.sh [NAME ...]`: each `designs/NAME.lca` through `apr -l -p`
  (keep the hand placement, route the nets) and `makebits`, into
  `designs/NAME.routed.lca` and `designs/NAME.bit`.  A design with no
  nets, or one that routes its own (`NProgram`), skips APR.
- `roundtrip.py VIEWER`: decodes every `designs/NAME.bit` and checks it
  against `NAME.routed.lca` with `../lcacheck.py`.  No XACT needed.
- `xapp.sh VIEWER [GLOB]`: builds XACT's XC3000-family XAPP designs
  (XAPP003 by default: XC3042A and XC3020A counters) with MakeBits and
  checks each the same way, and fails when it finds none.  Local only.
- `Dockerfile`: Ubuntu 26.04, by digest, with dosbox-x, p7zip, curl,
  git, node and Python, XACT and the viewer fetched at build time, the
  viewer at `tools/fpga/decoder.mk`'s pin, which `build.sh` passes.

## Running

    make check-fpga-decoder       # CI's: the committed designs' round trip
    tools/fpga/xact/fetch.sh      # once, for what follows
    make fpga-designs             # rebuild designs/ with XACT
    make fpga-designs XACT_DOCKER=1   # the same, in Docker
    make check-fpga-xapp          # XAPP003, locally

A rebuilt `.bit` differs from the committed one in its header's date
alone, unless the design changed.

## The designs

| Design | Covers |
| ------ | ------ |
| `ff` | logic-block flip-flops: K, K inverted, EC, RD, DI, both flip-flops fed back |
| `modes` | mode F (five inputs), FGM (M), FG; F with four inputs, which APR rewrites as FG |
| `fmode` | mode F reading E as its functions' fourth input, hand routed |
| `iobin` | input flip-flop and latch, IKNOT, pull-up, on both clock buffers |
| `iobout` | output flip-flop, OKNOT, inverted, fast and slow, 3-state either sense |
| `clocks` | input and output clocks on every die edge, from both clock buffers |
| `tbuf` | 3-state buffers on two Longlines, Longline pull-ups |
| `route` | direct connects, vertical and horizontal Longlines |
| `unused` | blocks configured with no nets |
| `empty` | no blocks: the blank bitstream |
| `t2` | the phase-0 hand design |

There is no set: an XC3000 logic block's flip-flops have a direct reset
alone (DSREF3, 4-68: "Storage elements for XC3000 CLBs are always
flip-flops").

## XACT's LCA format

No specification ships; the XDE commands (DSREF3 ch. 4) name the
records, and the CD's examples show them.  What these designs needed:
- `Design 3042PC84`, `Speed -70`; `Editblk TILE` with `Base`, `Config`
  and `Equate`; `Addnet NAME PIN ...` and `Addpin NAME PIN ...` (a line
  longer than 1023 characters is cut, the rest of its pins lost);
  `NProgram NAME PIP ...`.  XACT reads CR LF; `build.sh` adds the CRs.
- A PIP is `LINE:BLOCK.PIN`, `LINE:LINE` or a switch-matrix pair
  `TILE.20.1.N TILE.20.1.M` (pins 0 to 19, the viewer's 1 to 20).  A
  `-l` or `-s` suffix on a two-way PIP between lines gives its direction:
  from the first line to the second, or the second to the first.
- I/O blocks are named by package pin (`P11`) or pad (`PAD7`); 3-state
  buffers `TBUF.CD.1`, pull-ups `PU.CA.1`, clock buffers `GCLK` and
  `ACLK`, with `TCLKIN.I` and `BCLKIN.I` for the dedicated pads.
- APR swaps a block's pins, so a routed design's inputs are not the
  hand-written ones, and rewrites a mode-F block of four inputs as
  mode FG.  `apr -o NAME.APR` writes nothing; `build.sh` names its
  messages `NAMEA.OUT`.
- MakeBits `-t` ties unused interconnect down (DSREF2, 7-16) and with
  `-n` saves the tied design, which lists every pin the tie-down added:
  the check that found the left edge's clock faults.

## The viewer's faults

The viewer drops one clock.  It decides which of a logic block's
inputs are in use as it decodes the block, and clears the routing of
those that are not; a block whose flip-flop feeds only its output, an
output it learns is used only when a later block is decoded, has by
then lost its K, EC, RD and DI routing and its EC and RD settings.  In
the Z3805A that is CI, whose clock was the untraced CI.K.  `netdump.js`
keeps every input's configured routing and each block's configured EC
and RD for the decode, which is safe because a trace still connects
only the pins enabled when it runs: CI then registers AH.X (also P72)
on the rising edge of GH.X, reset by FH.Y like the count (`../../../docs/firmware/z3805a-trace.md`).  The
same block, CI, is affected in all five designs, and HJ too in the
58503B's.

The viewer also connects two drivers to a horizontal Longline where an
XC3000A design has one.  Beside each 3-state buffer, a vertical local
line reaches the Longline through the buffer used as a plain PIP, and on
the XC3000A and XC3100A the neighboring logic block's X or Y can too;
one bit, the buffer's input select, chooses which.  The viewer enabled
the local line's PIP on the buffer's other bits alone, so both drove the
Longline, and every load on it read both: in XACT's XAPP003 designs,
load pins came out on the X and the Y of one block (`CFSD20H`'s `FA.D`
on `FC.X` and `FC.Y`; its LCA has `FC.Y`).
`netdump.js` enables the local line's PIP only with the input select
at 1, after which those designs decode to their LCAs exactly.  An
XC3042 has no input select (every XC3042 bitstream reads 1 there, and
the viewer tells the families apart by it), so the five SmartClock
designs, all XC3042, decode the same as before.

More faults came out of checking the viewer against XACT's own bits,
MakeBits run on designs made to test them (`tools/fpga/xact/designs`),
and `netdump.js` corrects each:
- In modes F and FGM, as in FG, each function generator's fourth input
  is D or E, as a bit of the block's configuration selects.  The viewer
  took D in those modes, so a block whose functions read E came out
  reading D, and with it whatever net reached D's routing.  In the
  SmartClock designs the bit is E in a few used blocks in each, which
  changes their nets; the Z3805A's is EF, which reads DB.X.  Taken
  for D, EF read DL.Y, which the tie-down (`../../../docs/firmware/outputs.md`, "Netlists") routes there, and the
  count under the preset read `0x551555`; read as E, it reads
  `0x555555`, as `counter_selftest` requires, and Yosys proved the
  decode the same as the hand-repaired netlist that stood in before
  (`equiv_simple`, `equiv_induct`), a one-off proof: `netlist.py` no
  longer has the `--reconnect` option that made the repaired netlist,
  so it cannot be rerun.  The 58503A's design decodes the same way.
- The viewer stops a trace 300 steps deep, because it keeps no record
  of the switch-matrix pins a net has passed, and a loop of local lines
  through switch matrices would otherwise never end; a large net loses
  what lies beyond.  `netdump.js` gives each net a record of the
  switch-matrix pins it has passed and lifts the limit.
- A left-edge I/O block in the upper half of a tile below the top row
  takes its output clock from the line bit 1 of the tile's first row
  selects; the viewer read bit 0, one of the block's output multiplexer
  bits.
- The left edge's lower clock line, col.A.local.13, numbers its PIPs the
  other way about from the other seven: PIP 0 is the alternate clock
  buffer's and PIP 1 its branch's.  The viewer put the line on its
  branch, which then had no input, whenever its bits said the buffer.
  So P20's output register, in the Z3815A and the Z3816A, came out with
  no clock; it is on the alternate clock buffer, ACLK, which also clocks
  17 logic blocks.
- A clock buffer that clocks only I/O blocks, through the die-edge
  lines, drives no Longline, and the viewer dropped it with its nets.
- With the output used, the bit the viewer reads as the pull-up is one
  of the 3-state control's multiplexer bits; the data sheet allows the
  pull-up on inputs alone (7-7).
- A die-edge line's one polarity bit (Figure 4, 7-7) reads the other
  way about for input flip-flops and latches: XACT's flip-flops without
  IKNOT came out inverted.  No SmartClock design registers an input.
  `netdump.js` reads the bit as the output's, which upstream pull
  request #3 leaves alone where it turns the input's around.  The test
  designs with registered and latched inputs (`iobin`, `clocks`)
  agree with XACT at the pin and at the pull request
  (`make check-fpga-decoder`; at the pull request,
  `make -f tools/fpga/Makefile check-decoder-upstream`); the five
  SmartClock designs register no input, so they do not exercise it.

## Not covered

- Two PIPs at the bottom-right corner, `col.M.local.2:row.M.local.8`
  and `col.M.long.1:row.M.long.1`, which MakeBits's tie-down programs
  and the viewer does not decode.  Turned on by hand they connect every
  pin a tied test design's tie-down reaches, but their bits are not
  pinned down, so `netdump.js` does not decode them.  No pin any
  SmartClock design uses depends on them.
- The 3-state controls of I/O blocks whose outputs are not used: the
  tie-down lists them on its nets, but with no output their multiplexer
  bits are the pull-up's, and the viewer reads them as a 3-state choice.
- XC3000A-only resources beyond the 3-state buffer inputs, and parts
  other than the XC3042 PC84 and, through XAPP003, the XC3042A PC84 and
  XC3020A PC68.
