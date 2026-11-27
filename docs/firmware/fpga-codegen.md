# Executing the FPGA netlist in the emulator: the LCA model

A design for running a decoded FPGA bitstream directly in the emulator,
the XC3042's Logic Cell Array as the bitstream configures it (the "LCA
model"), so that one mechanism covers every receiver's FPGA from its
bitstream.  Its first slices are done: an evaluator that runs the
six designs' netlists and agrees with each netlist's Icarus
run, counter fast-forward, which moves it on between events as the
evaluator would, and the board running it, fast-forwarded, opt-in
but on the Z3816A's and the 58503B's boards, which have no hand model
("Status").
This records the decisions, for review, and the work left.  The decode
and the gate-level traces it rests on are `docs/firmware/outputs.md`
("Netlists") and the per-image `*-trace.md`; the emulator is
`docs/emulator.md`.

## Why

The emulator's FPGA model (`crates/smartclock-emu/src/board/fpga/z3805a.rs`) is
hand-written and models the Z3805A alone, and the Z3817A's
(`z3817a.rs`) its own design alone.  There are now six distinct
FPGA designs among the images -- the Z3805A, the 58503A/Z3801A (ten
logic blocks configured otherwise, eleven connected otherwise and one
added against it, `tools/fpga/ref/58503a-z3801a.tilediff`), the 58503B
(a larger variant on one global clock), and the two telecom two-clock
designs (Z3815A, Z3816A), and the Z3817A (the Z3805A's divider, tap and
stop with no interpolator, a 32-bit count and a second 1 PPS on P84,
`docs/firmware/z3817a-trace.md`) -- and a 59551A is expected.  A hand model per design is work that drifts; executing the
netlist removes both the per-design labor and the drift, and covers a
new bitstream the day it arrives.

The hand model **acts at the level of events, not clock edges** (the
user's call; an exact per-edge version was dropped), held to the netlist
within a tick by `tools/fpga/differential.py`.  The LCA model keeps that
contract at the board: between events nothing is clocked (it
fast-forwards, below); at an event it runs the netlist itself.

## Status

- `tools/fpga/lcanet.py` writes a decode as the netlist the emulator
  reads; `crates/smartclock-emu/src/board/fpga/<image>.netlist` is each
  of the six bitstreams' (`z3805a`, `58503a-z3801a`, `58503b`,
  `z3815a`, `z3816a`, `z3817a`), and `make -f tools/fpga/Makefile check-lca`,
  part of `check` and so of firmware CI, regenerates each from its
  bitstream and diffs it.  No design needed a block the evaluator
  lacked: none has a 3-state buffer, and the telecom pair's second
  clock buffer, ACLK, and GCLK driven by a logic block (AB.Y) are a
  buffer like any other.  No XACT design of any of them exists, so
  `lcacheck.py`, which holds a decode to the LCA file a design was
  built from, checks the decoder on XACT's test designs alone
  (`tools/fpga/xact/README.md`), not these.
- `crates/smartclock-emu/src/board/fpga/lca.rs` evaluates them.  Its
  tests, one `Bench` an image (`lca/tests.rs`), drive each netlist's
  pins as the image's `tools/fpga/<image>_stimulus.v` drives the
  Verilog (each one's body `tools/fpga/stimulus.vh` but the Z3817A's,
  a harness of its own), from `differential.py`'s cases, and hold every
  read, internal 1 PPS, interval and watched pin's change, and for the
  Z3817A every change of the status's bits 2 and 3 (FI.X, GJ.X) and of
  P16, P72 and P19, to the image's fixture
  (`crates/smartclock-emu/src/board/fpga/<image>.fixture`) exactly.  A
  bench names its design's internal 1 PPS (P83, the Z3817A's P78), its
  interval's net and its interpolator's byte, which the Z3817A's design
  has neither of, and its second 1 PPS input, the Z3817A's P84, whose
  pulses' widths the fixture records (the `registers` case's are
  2 us); the Z3817A's fixture also records a case's harness options
  (`options +edges` for `outputs`, `+p19` for `p19`), and the bench
  watches only what each option prints, as the harness does: P16 and
  P72 for `+edges`, P19 for `+p19`.  Each image's short cases run on
  every `cargo test`: `registers` and `references` (the port, the read
  multiplexer, the preset, mode 3's six-edge count and the
  references), the Z3817A's `registers`, `outputs` and `p19`; and every
  case, seconds of the clock each, in an ignored test an image that
  prints the evaluator's speed, a thread a case:

      cargo test --release -p smartclock-emu --lib lca::tests -- --ignored --nocapture

  Every image but the Z3817A runs the Z3805A's fifteen cases; the
  Z3817A runs its own (`differential.py`'s `z3817a_cases`: the
  read map at either select, the P84 counter, P82 and P84 starts and
  the outputs); the others add their own
  (`differential.py`'s `image_cases`): every register written and read
  back, three seconds with the pins that pulse on the seconds watched
  (the 58503B's and the telecom pair's P14, the telecom pair's P28 and
  P20) and register 2's bit 0 written among them, half a millisecond of
  the faster outputs (the 58503B's P19, the telecom pair's P51 and P57),
  and for the telecom pair twenty milliseconds of the phase-frequency
  detector, P38 and P40.  The telecom benches clock P53 at 20 MHz and
  P7 as a 10 MHz of its own, its edges on P53's falls, so that the P7
  divider runs apart from GCLK and no edge of one domain meets the
  other's in a race that would leave Icarus's result to its event order.
  The model drives both as its own clocks, P53's edge first at an
  instant both edge, and fast-forward's period is the two's joint one,
  P7's 100 ns ("Counter fast-forward", "Two clocks").

- `crates/smartclock-emu/src/board/fpga/lca/fast_forward.rs` moves the
  model on to a time as `Lca::run_clocked`, which drives the model's
  own clocks edge by edge, does, its other pins holding, without
  clocking most edges ("Counter fast-forward").  Its tests hold it to
  the clocked model in every node, at every action of every image's
  short cases, the Z3817A's among them, and at random times between, and property tests from
  states random stimuli reach check F(s, 0) = s, F(s, n + 1) = T(F(s, n))
  and F(s, n) = T^n(s), on one clock (the Z3805A's), on two (the
  Z3815A's), and F(s, n) = T^n(s) on the Z3816A's netlist on its
  board's clocks, ACLK alone with P7 held, on every `cargo test`, which
  also holds the Z3816A's short cases in lock step on those clocks,
  and the Z3815A's and the Z3816A's on their boards' clocks across
  the first second's terminal count.
  The short cases span microseconds, few of their landings jump, and
  the property tests' F(s, n + 1) = T(F(s, n)) compares fast-forward
  with itself, so every `cargo test` also holds two longer spans of
  each image's `near_tap` in lock step, and the Z3816A's on its board's
  clocks
  (`fast_forward_agrees_with_the_clock_across_the_terminal_count`): the
  first 30 ms from RESET, where its transients settle, clocked from the
  start; and 10 ms either side of the first internal 1 PPS, the
  one-second divider's terminal count, the case fast-forwarded alone
  to the span's start, through the long jumps a short case never
  makes, then clocked beside it.  Every case against the fixture,
  and every case in lock step, seconds of the clocks clocked beside it,
  are ignored tests, one of each for each image, and the lock step on
the Z3816A's board's clocks too (`z3816a_board`):

      cargo test --release -p smartclock-emu --lib fast_forward_reads_as_the_netlist_does_in_every -- --ignored
      cargo test --release -p smartclock-emu --lib fast_forward_agrees_with_the_clock_in_every -- --ignored --nocapture

- `crates/smartclock-emu/src/board/fpga/logic.rs` is the one interface
  `Fpga` drives a design through, `Logic`, whose variants are how a
  design is modeled: the hand-written models, the Z3805A's (the
  default) and the Z3817A's, and `Lca`,
  the netlist executed (`board/fpga/executed.rs`), which a board file
  asks for with `design = "lca"`.  The configuration the firmware sends
  picks the netlist at DONE by its hash (`board/fpga/netlists.rs`);
  bus accesses are bus cycles, reads sampled by the model itself where
  mc683xx's read-time horizon says nothing can reach the part first,
  else answered from a copy; the internal
  1 PPS and the interpolator's interval are P83's rise and P81's Low
  pulse (`board/fpga/pins.rs`), which the Z3817A's design has not, its
  1 PPS on P78 beside a second 1 PPS in on P84, so `netlists.rs` gives
  its netlist no board wiring and its board runs its hand model; the model's time is P7's phase, so the steered oscillator
  steers it.  `--fpga-design lca` asks for it in a run; the Z3816A's
  and the 58503B's board files say it (`tests/boot_z3816a.rs` and
  `tests/boot_58503b.rs` boot them).  The Z3805A
  3543B's configuration picks the Z3805A's netlist and the 58503A's
  the 58503A/Z3801A's (`tests/boot_lca.rs`), and the Z3805A boots on it
  and answers as on the hand model, an ignored test.  Between the bus's
  changes the board's model is fast-forwarded (`Executed`'s
  `Run::advance`), and the copy run ahead for the next internal 1 PPS
  is fast-forwarded to it, stopping at the first change of P83 or P81
  or at its horizon, which starts short at each access and grows each
  time the model takes the copy over, to a second (`AHEAD_TICKS`):
  `make bench-lca` prints the speed, the netlist alone and the board's
  run on it beside the hand model's, natively and in the browser's
  module ("Performance").

## Decisions

### The netlist: decoded at build time, checked in as data

The emulator cannot decode a bitstream at run time: the only decoder is
the XC3000 viewer, which has no license, so none of its tables can be
copied into the project (`third_party/NOTICE`; outputs.md, "Netlists"),
and its JavaScript runs under node or headless Chrome, which neither the
emulator nor its browser build has.  So the decode happens where it
already does, in `tools/fpga/`, at the pinned commit, and its result is
checked in, as the differential fixture is: `lcanet.py` writes it, and
`check-lca` regenerates and diffs it in CI.  This reverses "the netlist
stays generated, in `target/`" (PLAN.md) for the emulator's copy only.

It is checked in as **data, not generated Rust**: a line a block, the
options and selections `tools/fpga/xc3000.v` takes, each net by the pin
that drives it, and the SHA-256 of the `.bit` it came from.  One
evaluator, reviewed once, runs every image; each image's netlist is a
diffable text of a line a block rather than a module of generated code to compile, review and keep free
of warnings.  Code generation stays open as an optimization of the same
data, if a profile ever shows the evaluator's dispatch matters, which
fast-forward, running the evaluator only at landings, makes unlikely.

### Evaluation: event-driven, at the Verilog models' delays

The evaluator is event-driven, in picoseconds, with the delays of
`xc3000.v` exactly: every logic and I/O block output and pad driver one
nanosecond (an inertial delay, as Verilog's `assign #1`), routing and the
clock buffers none.  A levelized, cycle-based evaluation would be faster
per clock but cannot agree with the oracle where the designs are not
synchronous: the count's ripple Johnson stages clocked by the stage
before, the GPS 1 PPS clocking AH, the asynchronous resets, the input
latches, the Z3815A's and Z3816A's self-clearing `LD`/`LE` loop, and the
glitches `z3805a-trace.md` lists as model hazards.  Event-driven, the
model and Icarus run the same design under the same rules, and agree to
the picosecond; a change of one nanosecond to the block delay fails the
test.  A second clock, ACLK on the telecom designs, is only another
input pin with its own edges.

### Counter fast-forward

Clocking every edge is exact and far from real time (the speed the
ignored test prints), and the divider and the count cannot be clocked
every 10 MHz edge in the browser.  Between events, with the pins other
than the clock's holding, the model moves the counters on by their
orbits and the rest not at all.  As built (`lca/fast_forward/`):

1. **Nodes, at a landing.**  A landing is a period's start, the clock's
   rise still to come, with nothing on its way.  The state is split into
   nodes, each flip-flop, latch, pad and delayed output one, and each
   node's dependence on the others read from its truth tables
   three-valued, not from the wiring alone: the Z3805A packs a count bit
   and a read-multiplexer output, which reads the fast AL counter, into
   one logic block, and at block granularity every digit of the count
   would read AL (`analysis.rs`).
2. **Constants, followers, stages** (`plan.rs`).  The constants are the
   greatest set of nodes each of which, the set's values known and the
   others unknown, cannot change: sound by induction over time.  The
   followers forget what they held within a few clocks: delayed outputs
   and pads, which follow their inputs, and storage that takes
   something other than itself on every edge of the clock, or of a
   free-running counter of at most four bits on it (AL), checked to
   toggle.  The stages are the rest and everything they read.  The
   stages' blocks, by strongly connected component, are the members, in
   the order they read each other: the Z3805A's prescaler with the
   6-bit counter that clears it and the /64's first bits, then each bit
   of the /64, the decades, the terminal count, the seconds, the stop's
   pipeline, each digit of the count, AL, and P83's pad.  No annotation
   per image: the structure is read from the netlist.
3. **Each member's transitions from the evaluator itself**
   (`learn.rs`).  For a member's state and what its inputs do through a
   period, glitches and all, the evaluator runs that period with the
   member's blocks alone evaluated, its inputs driven as seen, and
   records the state it leaves and what its outputs did: the table is
   the netlist's behavior, not a reading of it.  A table is kept by the
   member's identity, its nodes, the channels it outputs and those it
   reads in order, the reset, the clocks' edges through a period, what
   drives its pads and the values of every constant it reads, numbered
   as met and compared whole, never by a hash alone, so that a collision
   cannot hand a member another's table; groups, the sets of waveforms
   a group's sources take, and what a still group ignores are keyed the
   same way.  So a table
   serves every landing that has that member, and every run: the
   board keeps one fast-forward per netlist a thread runs
   (`executed.rs`), so a design reset or configured again, which the
   Z3805A's boot does several times, or a board booted again, learns
   nothing twice.  ("Performance" says why it is not built in ahead.)  For this to be
   the full run's period, a clock buffer's output follows its input
   before any block sees either, so that the order blocks are evaluated
   in at an instant never matters.
4. **Groups and orbits** (`orbit.rs`).  A member grows into a parent
   group while what it reads of that changes and the two keep an orbit
   that closes within 2^16 periods.  A member reading several grows into
   one only where its others change what it reads at least 16 times
   less often (`WAKES_SAVED`), their outputs then the whole's inputs:
   grown, it wakes at their changes, each a bigger group's, rather than
   at that one's.  The prescaler's group takes the
   rest of the /64, and the count's lowest digit takes the next fifteen
   bits; the telecom pair's P7 divider, one member with the detector it
   feeds, takes its prescaler, GJ.Y's tick from the 1 PPS chain an
   input; free-running groups that a member alone reads together are
   merged with it, as the prescaler and the 6-bit counter are while a
   sync field of 3 holds the divider.  A group whose inputs hold runs
   along its orbit, a tail and a cycle, its state at any period an index;
   a group whose inputs change starts a new orbit where they do, the
   decades at their carries.  An orbit that comes to a state one of the
   group's orbits under the same inputs already has ends there, exiting
   into that one, so that what is kept is the states not seen before,
   and a group at an exit runs on along the orbit it exits into, the
   groups that read it told: the telecom pair's divider, woken at each
   tick of the other domain's, starts a new orbit at a phase of the two
   dividers' not met before, which the canonical cycle it soon rejoins
   would otherwise copy each time.  A member that grows into nothing
   takes a copy of each free-running source it reads, a member that
   reads no other, into a group of its own, if the whole keeps a short
   orbit along which the member does not hold still (one that does,
   alone, ignores the source at no cost, where with the copy its group
   runs the source's cycle and wakes at its other inputs' every
   change, which on the 58503B's bench made the jumps many times
   slower): a source runs the same from the same state wherever it is, so
   the copy is exact, and its reader no longer wakes at its every
   change.  On the Z3816A's board GCLK is such a source, ACLK halved in
   a flip-flop (AB), which the 1 PPS chain's group holds; the count's
   first stage, CG, the gated GCLK toggle (`z3816a-trace.md`), reads it
   and DI, a group after that one, so it grows into neither, and
   without the copy CG and the count's first stages woke every period;
   with it they grow into groups as the Z3805A's digits do.  In a kept
   plan's grouping a member alone with its copies is placed again as a
   member alone is.  A growth refused forgets the orbit
   its trial made, often cut at 2^16 periods.  A group holding still is not told of an
   input that, at every waveform its source's orbit has, leaves it as it
   is: P83's pad, Low, ignores the /64's first bit, which it reads only
   while High.  The jump goes to the first period a watched pin or net
   is not level through, or an orbit's end, or the time asked.
5. **The wake set, frozen.**  A member too large to learn, a counter's
   chain closed into one loop, is broken up by freezing one of its
   delayed outputs that has not changed in a sample of clocks, the one
   furthest downstream of the fastest node: its readers take it as
   constant, and it is watched, so that a jump stops where it would
   change.  The Z3805A with a sync field other than 0 closes the divider
   through EK and GJ; GJ.Y, at the terminal count, is the one frozen.
6. **Plans kept.**  A plan holds at a later landing whose constants have
   the values they had, its pins driven, reset and clock as they were:
   each proof that a constant cannot change goes through as it did.  The
   last few plans are kept and tried first, each with the groups its
   first jump made; a member alone in them is placed again as a fresh
   jump would place it, since what it reads may run now and not then:
   kept blindly, a count stopped at a plan's first landing left its
   digits apart, a group each, at every landing after, where the count
   runs and they changed millions of times a second.
7. **The landing.**  The jump stops short of where it may go by the
   periods the followers take to settle, writes the stages' states
   there, and clocks those periods, so that the followers forget the
   state they were left in.  External events -- a bus access, the GPS
   1 PPS, RESET, the interpolator's byte -- end an `advance`; the caller
   makes them and advances again.

Fast-forward equals the clocked run where five things hold: the
constants are constant (by construction), each member's table is the
evaluator's (by construction), the followers settle within the
periods clocked at the landing (by the followers' definition), every
watched channel is a stage's, so that a jump stops before its change
(a channel on a clock pin's I/O block, or on a net that follows one
through the clock buffers, is none, and its changes would be jumped
over: the plan is refused, `Refusal::WatchedClock`, and the span
clocked), and the clock pins' I/O blocks are input-only and
transparent, so that the clock nodes, which no member holds, follow
the pins at once.  The tests check the result rather than trusting the
argument ("Status").

**Two clocks.**  The telecom pair's benches drive two clocks, P53 at
20 MHz and P7 at 10 MHz, and the model drives both, so that P7's edges
are no longer events to fast-forward.  Its period is their joint one,
the least common multiple of theirs (`Lca::period`), 100 ns, starting
on a rise of the first clock once every clock has begun; a member's
period is learned with every clock's edges in it, in the order the
clocked model makes them, and a member's identity holds those edges.  No
orbit of the whole state is needed, which the two dividers, /6250 on P7
and GCLK / 6144 on P53, would make 19.2 million periods long: each
clock domain's dividers are their own groups, as a single clock's are,
and what reads both domains runs exactly where they meet.  The P7
divider and the detector are one member, the detector's self-clearing
loop closing through the divider's clear; it grows into P7's prescaler
rather than into the 1 PPS chain, whose GJ.Y tick it then takes as an
input, waking it only where that changes.  This needs the clocks'
periods to have a short common multiple: clocks whose joint period has
more than 64 edges (`LONGEST_PERIOD_EDGES`), such as a board's
19.6608 MHz ACLK against its 10 MHz, are clocked edge by edge, exact
and slow ("Work left").

### The board file names the LCA, and the configuration picks the netlist

The board file says `design = "lca"`.  The netlist the part runs is
chosen by the configuration the firmware sends: `Fpga` already keeps its
bits for the length count and the readback, so at DONE it hashes the
bits that came and runs the embedded netlist of the bitstream with the
same hash of its bits to DONE, or refuses the configuration loudly:
DONE stays Low, and the run stops with a line saying why (a bitstream it
does not know, one it has no netlist for, one the board does not wire
yet, or not the one the board file asserts).  What the board wires is
the same on every design but each one's clocks and held inputs, a
`Wiring` beside its netlist (`board/fpga/pins.rs`): the Z3805A's, the
58503A's and Z3801A's and the 58503B's designs the 10 MHz on P7 alone
and P17 held High, as their stimulus benches drive them; the Z3816A's
ACLK alone, below; and the Z3815A's as the Z3816A's, the inputs only its
design has, P23, P27, P35, P50, P78 and P84, held Low as its bench
holds them (`ACLK_Z3815A`; `z3815a-trace.md`, "Clocks").  The Z3815A's
design is the Z3816A's in everything this section says of ACLK, GCLK
and P7 (`z3815a-trace.md`, "What of P7 reaches the firmware"), so it
runs on the board as the Z3816A's does.

**The Z3816A's clocks on the board.**  Everything the firmware observes
of the Z3816A's design runs on ACLK, P53, and GCLK, ACLK halved: the
register port, the count, the 1 PPS chain, P20, P81 and P83.  P7 clocks
only the PLL's P7 divider and its phase detector, P38 and P40
(`z3816a-trace.md`, "Clocks").  The 1 PPS chain divides GCLK by
9,830,400, so ACLK is 19.6608 MHz, the CDMA VCXO the board's PLL locks
to the 10 MHz OCXO, P53 / 12,288 against P7 / 6250 (*inferred*; the
benches' 20 MHz is a test's convenience).  The PLL is not modeled at
all: the board drives ACLK ideally, at the OCXO's rate times
12,288 / 6250, so the EFC steers it, and P7 is held Low, its domain
dropped, so that fast-forward sees one clock, and the P7 divider and
the phase detector at P38 and P40 are constant.  That ratio is not a whole number of
picoseconds against the 10 MHz (50,862.63 ps a period), so the model's
time is scaled instead of rounded: each `Wiring` says how many of the
model's picoseconds a board tick is, and the Z3816A's ACLK keeps the
benches' 50 ns period, a tick then 98,304 of the model's picoseconds
(`ACLK_TICK`), exact.  The 1 PPS keeps the OCXO's time with no error at
all; what the scaling costs is the netlist's delays, taken in the
model's picoseconds, so on the board's time each is 1.7 % longer
(20 MHz / 19.6608 MHz), a nanosecond's delay 17 ps, against the
12 ppm a rounded period would have drifted the 1 PPS by, 12 us a
second.  With P7 held, the sync field's kinds 1 and 2, which step the
P7 divider a P7 clock earlier or later (`z3816a-trace.md`, "The sync
field"), step nothing in the model, yet they are how the firmware
aligns the 1 PPS's phase: after the full sync it writes the field 0
then 1 every two seconds, and without the step its TI never moved and
the loop never left its phase alignment.  So the board makes the step
the real PLL would make of it, *inferred* from the field as written (`Steps` in
`board/fpga/pins.rs`, `Executed::follow`): written from 0 to 1, the
model's time moves a tick ahead of the board's, ACLK and all it clocks
100 ns earlier, and from 0 to 2 a tick behind, the model standing still
until the board's time comes; at once, where the PLL slews over its
loop's time (*inferred*: the PLL settles within the two seconds
between steps; its filter is not known).  The full sync, which also
resets the P7 divider a little after the 1 PPS chain, is not followed:
the offset that leaves between them is the firmware's to step away.
Nor is what the field does while a full sync is pending, from its
write of 3 to the GPS 1 PPS that fires it: both its bits gate the P7
prescaler's head (`z3816a-trace.md`, "The sync field"), whose cycles
are then 4 P7 clocks, not 5, the P7 divider 25 % fast, which the real
PLL would see at its phase detector; with P7 held the model's ACLK
does not follow it.
Exactness against the netlist: `lca/board_clocks.rs` runs
every fixture case of the Z3816A's and the Z3815A's twice on the full
netlist, edge by edge, P7 held
and P7 running at the board's ratio to ACLK, and holds the reads, the
internal 1 PPS, the intervals and P20, P81, P14 and P28 to be the same
(the short cases on every `cargo test`, every case ignored but when
asked, minutes optimized): the one path from P7's domain into the rest,
IH.Y into JH, carries nothing under any sync field the cases set.  The hashes to DONE are kept beside the netlists, a table of
every `.bit` of the repository's (`board/fpga/netlists.rs`), and a test
holds each to its `.bit` and each netlist's header, the whole `.bit`'s
hash, to the same file: the `.bit` runs a few bytes past DONE, which the
model does not keep.  (A `configuration` field `lcanet.py` writes into
the header would move the table's hash there.)  This was "probably more
than it is worth" while the configuration interface was not modeled; it
is now, so the stream is the cheapest and the most faithful key: it
proves what the firmware programmed, and a modified firmware carrying
another design's bitstream runs that design or is refused.  A board file may add
`bitstream = "z3805a"` to assert the netlist it expects, which a board
test checks against its images, as `each_board_file_names_its_images_design`
checks the design names now.  The behavioral names stay until the LCA
model replaces them.  Two board files say `design = "lca"` already,
with `bitstream` naming their netlists, since neither design is a hand
model's: `z3816a.toml` and `58503b.toml`, whose images boot and lock on it (`tests/boot_z3816a.rs`,
`tests/boot_58503b.rs`, each ignored but when asked; docs/emulator.md,
"What the boot shows").

### A bus access is a bus cycle

The CPU's access is an instant on the board; the netlist samples the
select on its clock over the access's cycle, chip select 5's 12 wait
states (`chip-selects.md`).  A write becomes pin changes
scheduled in the model's own time, the select Low for the cycle, which
the model makes in order with any GPS 1 PPS among them, exactly.  A read
must answer at once, from the data bus as its cycle ends, three clocks
and chip select 5's 12 wait states after it starts (`BUS_CYCLE`).  The
part asks mc683xx for each access's horizon (`wants_horizon`, the LCA
model only; mc683xx 0.5.0): the board time before which everything that
could reach the part has, the other peripherals' next events, the nets'
changes waiting, the chip's outputs' next change and the end of the
chip's run among them.  Where the cycle's end, and a tick of the model's
time more, falls before it, the model itself runs through the cycle and
answers from its own sample, exactly for the netlist, keeping what it
saw past the board's time (a pulse, an interval) until the board gets
there.  The interpolator's byte on P3 to P11 is the one as of the
cycle's start, though an interval may end within the cycle and the
interpolator model then holds a new one: no access the firmware makes
can tell the two apart.  Where
it does not, the read runs its bus cycle on a copy of the model and
returns what the copy samples, while the model itself makes the same pin
changes in order, so that a GPS 1 PPS falling inside the read's cycle
still reaches the model at its time: the answer then misses it, a race
the hardware has too.  The two ways give the same answer, and leave the
board the same, unless an input does land within the cycle, which only
the copy can take; so a run's reads do not depend on where the host
cuts its runs, though the horizon does.  The tick's margin covers a sync
step written after the read, which moves the board's time in the model's
back a tick (`Executed::follow`).  No QSPI peripheral on any board is
deferred (`qspi_deferrable`), the one gap in the horizon, and a deferred
one drives no net in its transfers, so it could not reach the FPGA
anyway.  `executed.rs`'s tests run the fixture's short cases with the
run cut every 64 cycles, every slice, never, and with no horizon, and
hold every read, pulse, interval and next event alike; and do what the
model did to the netlist again edge by edge, every sample alike (every
case: `cargo test --release -p smartclock-emu --lib a_reads_answer_is_the_same_wherever_the_run_is_cut_in_every_case -- --ignored`,
and `exact_sampling_is_the_netlist_clocked_in_every_case`).

## Validation

- **Against the netlist.**  The fixture's cases, exactly: every read,
  internal 1 PPS and interval (done for the short cases on every test,
  for all of them in the ignored tests), each image against its own
  Icarus run (`make -j16 -f tools/fpga/Makefile check-differential`,
  `check-differential-<image>` one image's).  Too slow for CI: Icarus
  takes minutes a simulated second, and the telecom pair, with a
  20 MHz clock and a second domain, longer (`time make` gives it).
- **Fast-forward against the full clock.**  Done for the fixture's
  cases: a clocked model beside a fast-forwarded one on the same
  stimulus, every node, event and level compared at every action and at
  random times between (on every test the short cases, and two spans
  of `near_tap`, from RESET and across the first internal 1 PPS; all of
  them in the ignored tests, an image each, "Status"), and property
  tests from random stimuli, on one clock and on two.  The span across
  the 1 PPS starts from a fast-forwarded state, so only the ignored
  tests clock a whole second from RESET.  Left: the long runs'
  stimuli, and a make target.
- **Against the hand-written model.**  On the Z3805A's design, the
  differential test the hand model already passes, run with the LCA
  model as the reference in place of the fixture: they must agree where
  the hand model claims to (its tolerance of a tick and of the bus
  cycle's span).
- **The firmware.**  The boot and self-tests of every image on the LCA
  model, as the emulator's boot tests run them on the hand model, and the
  emulator's acceptance tests (the TI reading, the interpolator's
  calibration, the sync), first beside the hand model, then as the
  board's default.

## Performance

The target is real time in the browser build with room to spare.
Fast-forward's own speed, every case's CPU time against the time it
simulates, a fresh fast-forward each so that what it learns counts, and
the clocked evaluator's beside it, is an ignored test's output:

    cargo test --release -p smartclock-emu --lib fast_forward_speed -- --ignored --nocapture

(the Z3805A's bench; `FF_IMAGE=z3815a`, say, another image's).

The cost is the landings, not the seconds: per emulated second, two or
three of the design's own (the internal 1 PPS, the tap), the GPS 1 PPS,
and the firmware's accesses, which `counter_wait_ready`'s polling makes
the most of; each landing runs a few to a few tens of clocks.  The
evaluator's speed per clock is the ignored test's output; fast-forward's
budget is that speed times the clocks landings run, against the
emulator's CPU interpretation, which is nearly all of a test's time
today.  `make bench-lca` measures it, fast-forwarded: the netlist alone
over a second, and the Z3805A's boot over the same ten seconds of the
board on each design, natively
and in the browser's module in Node, both with the synthetic GPS
engine, which gives the wasm build's ratio to native.  The engine
matters: its 1 PPS drives a pin each second, and the firmware's
measurement around it reads the FPGA, so the board runs several times
as many landings as it does with nothing on the GPS's port, as the
native measure once ran it, which made the wasm build look far slower
than native when the two were not running the same board.  It prints
the figures rather than checking them, so none is quoted here: the
host's load moves them.

### Where the time goes

Profiled natively with `perf` (a build with frame pointers and line
tables, run on one core, the run to 1.1 s of the board subtracted from
the run to 11.1 s so that the measured ten seconds are what is left):

    RUSTFLAGS="-C force-frame-pointers=yes" CARGO_PROFILE_RELEASE_STRIP=none \
      CARGO_PROFILE_RELEASE_DEBUG=line-tables-only cargo build --release -p smartclock-emu
    perf record -e cpu_core/cycles/ --call-graph fp -- target/release/smartclock-emu \
      third_party/firmware/images/z3805a-3543b.bin --eeprom ... --eeprom-dump ... \
      --deterministic --script SCRIPT --fpga-design lca

and in wasm with Node's sampling profiler, on a build that keeps its
names (`CARGO_PROFILE_RELEASE_STRIP=none`, wasm-bindgen with
`--keep-debug` and without `--remove-name-section`):

    node --cpu-prof crates/smartclock-emu-web/tests/lca-speed.mjs

What they showed, in order of what it cost:

- **Not wasm.**  The native and wasm measures ran different boards: the
  browser's has the GPS engine, the native one had nothing on the GPS's
  port.  Natively with the engine, the LCA model cost about what it
  costs in wasm; wasm's remaining ratio to native is about the hand
  model's own.  The release profile's settings (fat LTO, one codegen
  unit, SIMD128) measured no faster in wasm and are left as they are;
  `wasm-opt` is not part of the build (`make emulator-web`).  The
  hasher (a multiply a word, which wasm has natively) and the
  `Mutex` (uncontended, taken once an advance) did not show.
- **A kept grouping.**  Most of the run was a few jumps of a second
  each, anchoring millions of times: the count's digits each a group,
  because the plan's first landing found the count stopped and the
  grouping it kept never grew them ("Plans kept", above).
- **Learning again.**  The boot resets and configures the design several
  times, and each new run learned every table and orbit afresh, most of
  it the orbits a grown group is tried on, up to 2^16 periods each.
- **The orbits' bookkeeping.**  Then a group's period allocated vectors
  per member, the orbit's walk one per state, and what a still group
  ignores hashed its sources' waveforms at every event.

After those, natively, the CPU's interpretation is again a large share,
and fast-forward's is spread over the jump's events (what a still group
ignores, the orbits a landing starts), fresh plans (the three-valued
pass), and the clocks landings run; none of them alone most of it.

On the telecom pair's benches (`FF_IMAGE=z3815a` or `z3816a` to
`fast_forward_speed`, and `describe_counting` for the groups), in the
order they were removed:

- **P7 as an outside pin.**  The test drove P7 between the model's
  moves, every edge an event, so that fast-forward never planned and
  the benches ran clocked edge by edge.
- **The divider woken every period.**  With both clocks the model's,
  the P7 divider and its detector, one member, read P7's prescaler and
  the 1 PPS chain; it grew into neither, and the prescaler woke it
  nearly every period.
- **Its orbit walked again at every wake.**  Grown into the prescaler,
  it woke only at the 1 PPS chain's tick, but each time at a phase of
  the two dividers' not met before, and walked and kept the whole
  6250-period cycle again, as refused growths kept their trial orbits:
  slower fresh, and far more kept than the single-clock designs keep.
  Exits and forgetting removed both.

On the Z3816A's board, ACLK alone with P7 held (`FF_IMAGE=z3816a-board`
to `fast_forward_speed`; on the board, a `--skip-warm-up` boot with
`--trace position` under `perf`, as above, over a few hundred seconds
of the board, since the firmware's traffic grows past its first
minute):

- **The count woken every period.**  GCLK, ACLK halved in AB, is in the
  1 PPS chain's group; the count's first stage, CG, reads it and DI, a
  later group's, so it grew into neither, and it and each stage of the
  count it clocks started an orbit every period or two, keeping a step
  of the jump's history each time: the board ran slower than real time
  and slowing, and at the run's first minute all but stopped, its
  memory growing.  Copies of free-running sources ("Groups and orbits",
  above) removed it.
- **The copy ahead thrown away.**  Past its first minute the firmware
  writes the port about a hundred times a second, and each access threw
  away the copy run ahead for the next internal 1 PPS and ran a new one
  up to a second ahead: the board's design ran many times the board's
  time, and on this design, whose groups wake thousands of times a
  second, that is not cheap.  The copy's horizon now starts short at an
  access and grows while none comes (`executed.rs`, `AHEAD_TICKS`).
- **A landing for every bus cycle.**  A bus cycle's changes, the select
  at its start and end and the data released a quarter cycle on, are a
  few tens of periods apart, and each gap of more than eight periods
  was a landing: a plan, a jump whose groups started orbits, kept, and
  the periods it settles, for a span cheaper to clock outright.  Gaps
  up to 32 periods are now clocked (`SHORTEST_JUMP`); on every image's
  bench and on the board the run is faster and holds less, the board's
  Z3805A measure unchanged.  How a 300 s board run's wall time and
  peak memory compare, at 8 and at 32, is measured as above, with
  `/usr/bin/time -v`.
  What is left is mostly the evaluator, the periods landings clock.

What fast-forward learns, the members' tables and the groups' orbits,
is learned as the board runs and kept for the thread's life, one
fast-forward per netlist, not built ahead and checked in as the
netlist is.  Built ahead, the tables would have to be keyed by numbers
stable across runs (a member's key is its identity's number in the order the run met it), would hold the
orbits of whatever runs made them, megabytes of states, and would need
a run of the firmware to make and a target to check; what they save is
the first run's learning on each thread, which the measure names
(`fast_forward_speed` prints a fresh fast-forward's speed beside a
learned one's).  Kept per thread, the learning a boot used to repeat,
once for each reset or configuration of the design, is done once.

## Work left

| Step | What | Size |
| ---- | ---- | ---- |
| Fast-forward | done, the telecom pair's two clocks too, and their boards on ACLK alone; left: clocks with no short common period (a board's ACLK against its P7, which a telecom board would need if the firmware read P7), orbits of orbits for a counter longer than 2^16 periods in one group, the cross-check as a make target; a cheaper plan where a bus write changes a register only if the profile asks for it (a fresh plan is a fraction of the board's time, "Where the time goes") | small |
| Board wiring | done, fast-forwarded: `Logic::Lca`, the configuration's hash at DONE, `design = "lca"`, bus cycles, reads sampled exactly within mc683xx's horizon, the interpolator from P81's pulse, the model's time P7's phase, the copy ahead fast-forwarded to the next change of P83 or P81 | done |
| Browser | measured, `make bench-lca`, faster than real time there; the default once it runs with room to spare, which is the owner's call; code generation only if the measure asks for it, and the profile does not: the evaluator's own share is small | small, unless it does |
| Retire the hand model | once every image boots and its acceptance tests pass on the LCA model | a deletion, and the docs that describe it |

## Prerequisites and open questions

- **The decode readout is settled.**  The pipeline pins the viewer to
  the commit `make check-fpga-decoder` validates against XACT; at
  upstream pull request #3 (`02c1a56`), `netdump.js` decodes the five
  designs to the same netlists as at the pin, and the test designs
  agree with XACT at both (`check-decoder-upstream`;
  `fpga-comparison.md`, "Provenance").
  A model that executes the decode is only as right as the decode.
- **Unknown values.**  Icarus starts every net unknown and reads an
  undriven pad as unknown; the evaluator starts Low and reads an
  undriven pad Low, or High with its pull-up.  Every storage element is
  reset while RESET is Low, so the two meet before the design runs; a
  design read during that reset, or a pad left floating, could differ.
- **The hand model's tolerances** (a tick, the bus cycle's span) are
  what the LCA model removes; where it and the hand model disagree, the
  netlist decides.
