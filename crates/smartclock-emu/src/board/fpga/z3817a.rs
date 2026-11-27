//! The Z3817A's design, as docs/firmware/z3817a-trace.md reads its
//! decoded netlist, simulated: not checked on a board, and the pins'
//! roles read from the logic.  A model of events at the Z3805A's model's
//! level (z3805a.rs): the divider, its tap, the seconds, the sync field
//! and the stop are the family's (timebase.rs), which the trace finds
//! re-placed but the Z3805A's to the edge, but for a full sync's
//! restart.  What differs:
//!
//! - **No interpolator** and no mode: register 0 keeps the preset (bit
//!   6) alone, register 1 the read address (bits 3 to 7) alone, and no
//!   start pulse goes out.
//! - **The start's select**, register 4's bit 0 (ADDR2 High): clear, a
//!   1 PPS on P82 starts the count, set, one on P84.  The 3752 firmware
//!   writes it 0 alone (z3817a-3752.md, "P84, register 4 and the status
//!   bits").  The selected 1 PPS's rise sets the start bit, and the gate
//!   opens on the first clock edge after it, as the Z3805A's with its
//!   reference bit Low; the count runs to the stop.  So the count, less
//!   its preset, is `4,000,004 + (t_internal - t_1PPS) / 100 ns` less
//!   the start's fraction of a tick.
//! - **A 32-bit count**, preset `0x55555555`, its eight nibbles at
//!   `0x12` to `0x19`, low first; `0x10` and `0x11` read `0xf`, nothing
//!   above.
//! - **The P84 counter**: four bits of P84's rises, which nothing but
//!   the part's reset clears, in the high nibble of a read at an odd
//!   address where an even one has the seconds.
//! - **The status's bits 2 and 3**, flags of the 1 PPS chain the
//!   firmware never tests (z3817a-trace.md, "The status bits"): bit 2,
//!   FI.X, set by the internal 1 PPS and cleared by the divider's next
//!   millisecond terminal (HD.Y), so High the millisecond after each
//!   pulse; bit 3, GJ.X, set by the 10 ms terminal (HF.Y) and cleared by
//!   the next HD.Y, so High the first millisecond of every ten, counted
//!   from the edge the divider started on.  Neither sets on the reset's
//!   own edge.
//! - **The other outputs**: P72, GJ.Y, High four clocks from each HD.Y,
//!   to the prescaler's next tick; P19, EE.Y, GCLK / 65, High 32 clocks
//!   of each 65 from 32 after the reset's edge; P16, P82 passed through.
//!   No part of the board model takes them: the model gives P72's and
//!   P19's levels for its tests, and P16 is P82's.
//!
//! Each of those is the netlist's to the clock edge, checked against
//! tools/fpga/differential.py's fixture.
//!
//! **The sync field**, as the netlist's runs give it
//! (`check-readings-z3817a.sync1` to `.sync3`, z3817a-trace.md, "The
//! sync field"): kind 1 moves the next internal 1 PPS a clock earlier
//! and kind 2 a clock later, as the Z3805A's; kind 3 restarts the
//! divider on the selected 1 PPS (CD.X), and the restart is itself the
//! internal 1 PPS, counted in the seconds, on the 1 PPS's second clock
//! edge, where the Z3805A's gives none until a second on.  *Inferred*:
//! a kind 3 whose 1 PPS comes after an internal 1 PPS, which no run
//! makes, the Z3805A's hold, and where the flags and P72 fall after a
//! step or a restart, which counts from the divider's new start, no
//! terminal on a restart's edge.

use super::super::clock::Phase;
use super::super::clock::Tick;
use super::timebase::Restart;
use super::timebase::Start;
use super::timebase::Timebase;
use super::timebase::WRITE_STROBE;

/// The registers the port decodes (z3817a-trace.md, "Register port"):
/// ADDR0 to ADDR2, register 2 decoding to nothing.
const REGISTER_PRESET: u32 = 0;
const REGISTER_ADDRESS: u32 = 1;
const REGISTER_SYNC: u32 = 3;
const REGISTER_SELECT: u32 = 4;
/// Register 0's preset (LH).
const PRESET: u8 = 0x40;
/// Register 1's read address, its top five bits.
const ADDRESS_SHIFT: u8 = 3;
/// Register 4's start select (AG.QY): P84 rather than P82.
const SELECT_P84: u8 = 0x01;

/// The read multiplexer: the address's bit 4 takes bits 0 to 3 from the
/// count's nibbles rather than the status; its bit 0 takes bits 4 to 7
/// from the P84 counter rather than the seconds.
const READ_NIBBLES: u8 = 0x10;
const READ_NIBBLE_MASK: u8 = 0x0f;
const READ_P84: u8 = 0x01;
/// What the nibbles below the count read, the Z3805A's interpolator
/// byte's place (`z3817a.fast`).
const READ_BELOW_COUNT: u8 = 0x0f;
const READ_COUNT_LOW: u8 = 0x12;
const READ_COUNT_HIGH: u8 = 0x19;
const NIBBLE_BITS: u8 = 4;
const NIBBLE_MASK: u64 = 0x0f;
/// The status's bits: bit 0 set at the stop (CI.X), bit 1 at the start
/// (BI.X), bit 2 the millisecond after the internal 1 PPS (FI.X), bit 3
/// the first millisecond of each ten (GJ.X), and the high nibble's place.
const STATUS_STOPPED: u8 = 0x01;
const STATUS_STARTED: u8 = 0x02;
const STATUS_PULSED: u8 = 0x04;
const STATUS_DECADE: u8 = 0x08;
const HIGH_SHIFT: u8 = 4;
const HIGH_MASK: u8 = 0x0f;
/// What the counter presets to (the firmware's constant at `0x45a92`),
/// and its width.
const COUNT_PRESET: u64 = 0x5555_5555;
const COUNT_MASK: u64 = 0xffff_ffff;
/// The gate opens on the first clock edge after the start (AI).
const OPEN: u64 = 1;
/// The divider's millisecond terminal's period (HD.Y) and the 10 ms
/// one's (HF.Y), ticks, from the edge the divider started on.
const MILLISECOND: u64 = 10_000;
const TEN_MILLISECONDS: u64 = 100_000;
/// P72's width, ticks: GJ.Y set on HD.Y, cleared on the prescaler's next
/// tick (JB.X), which HD.Y's restart of it puts four clocks on.
#[cfg(test)]
const P72_WIDTH: u64 = 4;
/// P19, EE.Y, a counter of the clock: its period, ticks, and its place,
/// from the reset's edge, High from `P19_RISE` to `P19_FALL` of each.
#[cfg(test)]
const P19_PERIOD: u64 = 65;
#[cfg(test)]
const P19_RISE: u64 = 32;
#[cfg(test)]
const P19_FALL: u64 = 64;

/// The registers as the design keeps them, all clear after its reset;
/// the sync field is the timebase's.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
struct Registers {
    /// Register 0's preset (LH).
    preset: bool,
    /// Register 1's read address, its top five bits (FJ, DJ, EJ).
    address: u8,
    /// Register 4's start select: P84 (AG.QY).
    select_p84: bool,
}

/// The design's state.
#[derive(Debug, Clone, Copy, PartialEq)]
pub(super) struct Z3817a {
    registers: Registers,
    /// The divider, its seconds (JH, JI) and the sync field (IG).
    timebase: Timebase,
    /// The count, once started (BI, AI).
    start: Option<Start>,
    /// P84's rises, four bits (HG, HH).
    p84_rises: u8,
}

impl Z3817a {
    /// The design as its reset at the phase `at` leaves it, or holds it
    /// while RESET is Low: every register clear, nothing started.
    pub(super) fn reset(at: Phase) -> Self {
        Self {
            registers: Registers::default(),
            timebase: Timebase::reset(at.tick, Restart::Pulse),
            start: None,
            p84_rises: 0,
        }
    }

    /// The next tick the design acts on by itself: the internal 1 PPS's
    /// next pulse, or the tap a count waits for.
    pub(super) fn next_event(&self) -> Option<Tick> {
        self.timebase.next_event(self.start.as_ref())
    }

    /// The design moved on to the rising edge `now`: the taps and pulses
    /// up to it.  Returns the last pulse.
    pub(super) fn run_to(&mut self, now: Tick) -> Option<Tick> {
        self.timebase.run_to(now, &mut self.start)
    }

    /// The selected 1 PPS rising at the phase `at`: a full sync pending
    /// restarts the divider on it, and without the preset it starts the
    /// count.
    fn selected_pulse(&mut self, at: Phase) {
        self.timebase.reference_pulse(at);
        if !self.registers.preset && self.start.is_none() {
            let gate = at.edge(OPEN);
            self.start = Some(Start {
                at,
                open: gate,
                gate,
                stop: None,
            });
        }
    }

    /// A 1 PPS rising on P82 at the phase `at`.
    pub(super) fn p82(&mut self, at: Phase) {
        if !self.registers.select_p84 {
            self.selected_pulse(at);
        }
    }

    /// A 1 PPS rising on P84 at the phase `at`: counted, and the start's
    /// if selected.
    pub(super) fn p84(&mut self, at: Phase) {
        self.p84_rises = self.p84_rises.wrapping_add(1) & HIGH_MASK;
        if self.registers.select_p84 {
            self.selected_pulse(at);
        }
    }

    /// The port's read at the phase `now` (z3817a-trace.md, "Register
    /// port").
    pub(super) fn read(&self, now: Phase) -> u8 {
        let address = self.registers.address;
        let high = if address & READ_P84 == 0 {
            self.timebase.seconds
        } else {
            self.p84_rises
        } & HIGH_MASK;
        let low = if address & READ_NIBBLES == 0 {
            let counting = self.start.map_or(0, |start| {
                STATUS_STARTED
                    | if start.stopped(now) {
                        STATUS_STOPPED
                    } else {
                        0
                    }
            });
            counting | self.flags(now.tick)
        } else {
            let selected = address & READ_NIBBLE_MASK | READ_NIBBLES;
            let counted = self.start.map_or(0, |start| start.counted(now, COUNT_MASK));
            let count = COUNT_PRESET.wrapping_add(counted) & COUNT_MASK;
            match selected {
                READ_COUNT_LOW..=READ_COUNT_HIGH => {
                    // a nibble, within a byte
                    (count >> (NIBBLE_BITS * (selected - READ_COUNT_LOW)) & NIBBLE_MASK) as u8
                }
                _ if selected < READ_COUNT_LOW => READ_BELOW_COUNT,
                _ => 0,
            }
        };
        high << HIGH_SHIFT | low
    }

    /// Whether the clock edge `now` falls in the first `width` ticks of a
    /// period of `period` of the divider's, counted from the edge it
    /// started on, but for the reset's own edge's.
    fn on_divider(&self, now: Tick, period: u64, width: u64) -> bool {
        self.timebase.since_start(now).is_some_and(|since| {
            since % period < width && !(self.timebase.started_quiet() && since < period)
        })
    }

    /// The status's bits 2 and 3 on the clock edge `now`: FI.X the
    /// millisecond from the internal 1 PPS, GJ.X the first of each ten.
    fn flags(&self, now: Tick) -> u8 {
        let pulsed = self.timebase.last_pulse.is_some_and(|pulse| {
            now.0
                .checked_sub(pulse.0)
                .is_some_and(|since| since < MILLISECOND)
        });
        let decade = self.on_divider(now, TEN_MILLISECONDS, MILLISECOND);
        [(pulsed, STATUS_PULSED), (decade, STATUS_DECADE)]
            .into_iter()
            .filter(|(set, _)| *set)
            .fold(0, |flags, (_, bit)| flags | bit)
    }

    /// P72's level on the clock edge `now`: High four clocks from each
    /// of the divider's milliseconds.
    #[cfg(test)]
    fn p72(&self, now: Tick) -> bool {
        self.on_divider(now, MILLISECOND, P72_WIDTH)
    }

    /// P19's level on the clock edge `now`: GCLK / 65 from the reset.
    #[cfg(test)]
    fn p19(&self, now: Tick) -> bool {
        now.0
            .checked_sub(self.timebase.reset.0)
            .is_some_and(|since| (P19_RISE..P19_FALL).contains(&(since % P19_PERIOD)))
    }

    /// `value` written to the register `register`, ADDR0 to ADDR2, at
    /// the phase `now`.
    pub(super) fn write(&mut self, register: u32, value: u8, now: Phase) {
        match register {
            REGISTER_PRESET => self.registers.preset = value & PRESET != 0,
            REGISTER_ADDRESS => self.registers.address = value >> ADDRESS_SHIFT,
            REGISTER_SYNC => self.timebase.write_sync(value),
            REGISTER_SELECT => self.registers.select_p84 = value & SELECT_P84 != 0,
            _ => {}
        }
        self.timebase.write_strobe(now.edge(WRITE_STROBE));
        if self.registers.preset {
            self.start = None;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::super::super::clock::Clock;
    use super::super::super::clock::Phase;
    use super::super::super::clock::Tick;
    use super::super::super::time::Cycles;
    use super::super::Design;
    use super::super::Fpga;
    use super::super::Trims;
    use super::super::fixture::Case;
    use super::super::fixture::NETLIST_Z3817A;
    use super::super::fixture::cases;
    use super::super::fixture::hex;
    use super::super::logic::Logic;
    use super::super::tests::CLOCK;
    use super::super::tests::HELD;
    use super::super::tests::RUNNING;
    use super::super::tests::at_tick;
    use super::super::tests::program;
    use super::ADDRESS_SHIFT;
    use super::COUNT_PRESET;
    use super::REGISTER_ADDRESS;
    use super::STATUS_DECADE;
    use super::STATUS_PULSED;

    /// The Z3817A 3752's bitstream (outputs.md, "The FPGA bitstreams").
    const BITSTREAM: &[u8] =
        include_bytes!("../../../../../third_party/firmware/images/fpga/z3817a.bit");

    /// The part configured with the Z3817A's design and bitstream on
    /// `clock`, RESET pulsed at `reset`.
    fn configured_on(clock: &dyn Clock, reset: Cycles) -> Fpga {
        let mut fpga = Fpga::new(Design::Z3817a);
        program(&mut fpga, BITSTREAM, reset, clock);
        fpga
    }

    /// The clock edges a read's bus cycle spans in `z3817a_stimulus.v`,
    /// `z3805a_stimulus.v`'s: the select Low 1 us and the data out through
    /// flip-flops on AL's clock, a quarter of the 10 MHz (z3805a.rs).
    const BUS_CYCLE_TICKS: u64 = 16;
    /// differential.py's 1 PPS width, 100 us, ticks: P16 falls this long
    /// after P82's rise.
    const PPS_WIDTH_TICKS: u64 = 1_000;

    /// The design moved on to `now`, the internal 1 PPS's pulse on the
    /// way, if any, added to `pulses`.
    fn run_to(fpga: &mut Fpga, now: Cycles, pulses: &mut Vec<u128>) {
        if let Some(pulse) = fpga.logic_at(now, &CLOCK).and_then(|(_, _, pulse)| pulse) {
            pulses.push(u128::from(pulse.0));
        }
    }

    /// The level the model gives the net `name`, a status bit or P72 or
    /// P19, on the clock edge `tick`, the design run to it.
    fn level(fpga: &mut Fpga, name: &str, tick: u64, pulses: &mut Vec<u128>) -> bool {
        run_to(fpga, at_tick(tick), pulses);
        let design = fpga
            .logic
            .as_ref()
            .and_then(Logic::z3817a)
            .expect("running");
        let now = Tick(tick);
        match name {
            "FI.X" => design.flags(now) & STATUS_PULSED != 0,
            "GJ.X" => design.flags(now) & STATUS_DECADE != 0,
            "P72" => design.p72(now),
            "P19" => design.p19(now),
            _ => panic!("{name}"),
        }
    }

    /// `case` through the model: what differs from the netlist.  A read
    /// is the netlist's if the model reads the same somewhere in the
    /// read's bus cycle; the internal 1 PPS's pulses, the status's bits 2
    /// and 3 and P72's and P19's edges are the netlist's to the clock
    /// edge, and P16's are P82's.
    fn replay(case: &Case) -> Vec<String> {
        let mut fpga = configured_on(&CLOCK, Cycles(0));
        let mut pulses = Vec::new();
        let mut differences = Vec::new();
        // P82's rises, ticks, for P16's edges
        let mut p82 = Vec::new();
        let (p16, mut flags): (Vec<_>, Vec<_>) =
            case.flags.iter().partition(|(_, name, _)| name == "P16");
        flags.sort_by_key(|(tick, _, _)| *tick);
        let mut flags = flags.into_iter().peekable();
        for (at, operation) in &case.operations {
            let now = Cycles(*at);
            // each edge before the operation: the level before it and on it
            while let Some((tick, name, high)) = flags.next_if(|(tick, _, _)| at_tick(*tick) <= now)
            {
                let before = level(&mut fpga, name, tick - 1, &mut pulses);
                let after = level(&mut fpga, name, *tick, &mut pulses);
                if (before, after) != (!high, *high) {
                    differences.push(format!(
                        "{}: {name} {before} then {after} at {tick}, the netlist {high}",
                        case.name
                    ));
                }
            }
            run_to(&mut fpga, now, &mut pulses);
            let words: Vec<&str> = operation.iter().map(String::as_str).collect();
            match words.as_slice() {
                ["reset"] => {
                    fpga.set_lines(HELD, now, &CLOCK);
                    fpga.set_lines(RUNNING, now, &CLOCK);
                }
                ["write", register, value] => {
                    let register: u32 = register.parse().expect("a register");
                    fpga.write(0x4000 + register, hex(value), now, &CLOCK, Trims::default());
                }
                ["read", value] => {
                    let netlist = hex(value);
                    let phase = CLOCK.phase(now);
                    let span = std::iter::once(now)
                        .chain((1..=BUS_CYCLE_TICKS).map(|tick| at_tick(phase.edge(tick).0)));
                    let reads: Vec<u8> = span
                        .map(|later| {
                            run_to(&mut fpga, later, &mut pulses);
                            fpga.read(0x4000, later, &CLOCK, None)
                        })
                        .collect();
                    if !reads.contains(&netlist) {
                        differences.push(format!(
                            "{}: {at} read {:02x}, the netlist {value}",
                            case.name, reads[0]
                        ));
                    }
                }
                ["gps"] => {
                    p82.push(CLOCK.phase(now).tick.0);
                    fpga.gps_pps(now, &CLOCK, Trims::default());
                }
                // the model counts P84's rises, whatever their width
                ["p84", _] => fpga.p84(now, &CLOCK),
                ["end"] => {}
                _ => panic!("{}: {operation:?}", case.name),
            }
        }
        if let Some(left) = flags.next() {
            differences.push(format!("{}: {left:?} after the end", case.name));
        }
        for (tick, _, high) in p16 {
            let edge = |rise: &u64| *tick == if *high { *rise } else { rise + PPS_WIDTH_TICKS };
            if !p82.iter().any(edge) {
                differences.push(format!("{}: P16 {high} at {tick}, not P82's", case.name));
            }
        }
        if pulses != case.pulses {
            differences.push(format!(
                "{}: pulses {pulses:?}, the netlist {:?}",
                case.name, case.pulses
            ));
        }
        differences
    }

    /// The model reads what the netlist read, pulses when it pulsed and
    /// changes the status's bits 2 and 3 and its outputs when the
    /// netlist did, for every case of the Z3817A's fixture: the read map
    /// at either select, the P84 counter through its wrap, counts started
    /// by P82 before and after the internal 1 PPS, in the first second
    /// and a few clocks before a tap, and by P84 selected, and P16's,
    /// P19's and P72's edges.
    #[test]
    fn the_model_reads_as_the_netlist_does() {
        let cases = cases(NETLIST_Z3817A);
        assert!(!cases.is_empty());
        let differences: Vec<String> = cases.iter().flat_map(replay).collect();
        assert!(differences.is_empty(), "{}", differences.join("\n"));
    }

    /// The same against the netlist run now, where Icarus Verilog and the
    /// generated netlist are (docs/firmware/outputs.md, "Netlists"):
    /// minutes a simulated second, so only when asked.  `make -f
    /// tools/fpga/Makefile check-differential-z3817a` does the same in
    /// parallel.
    #[test]
    #[ignore = "runs the netlist in Icarus Verilog, minutes a simulated second"]
    fn the_model_reads_as_the_netlist_does_run_now() {
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
        let netlist = root.join("target/fpga/z3817a.v");
        assert!(
            netlist.exists(),
            "no {}: `make -f tools/fpga/Makefile target/fpga/z3817a.v` makes it",
            netlist.display()
        );
        // A simulation of its own, so that a concurrent
        // `make check-differential` does not build over it.
        let binary = root.join(format!(
            "target/fpga/z3817a_stimulus.{}",
            std::process::id()
        ));
        let output = std::process::Command::new("python3")
            .arg("-I")
            .arg(root.join("tools/fpga/differential.py"))
            .args(["--design", "z3817a", "--netlist"])
            .arg(&netlist)
            .arg("--binary")
            .arg(&binary)
            .output()
            .expect("differential.py");
        // Whatever the outcome, the binary is this test's alone.
        let _ = std::fs::remove_file(&binary);
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        let text = String::from_utf8(output.stdout).expect("text");
        let differences: Vec<String> = cases(&text).iter().flat_map(replay).collect();
        assert!(differences.is_empty(), "{}", differences.join("\n"));
        assert_eq!(
            text, NETLIST_Z3817A,
            "the fixture is stale: differential.py writes it"
        );
    }

    /// The clock of a board whose cycles are nanoseconds: 10 MHz, its
    /// rising edges 50 ns into each 100 ns, tick 0 the first, as
    /// `tools/fpga/z3817a_tb.v`'s.
    struct Nanoseconds;

    /// Its period and its first rising edge, nanoseconds.
    const PERIOD_NS: u64 = 100;
    const FIRST_EDGE_NS: u64 = 50;
    const NS_PER_SECOND: u64 = 1_000_000_000;

    impl Clock for Nanoseconds {
        fn phase(&self, at: Cycles) -> Phase {
            let since = at.0.saturating_sub(FIRST_EDGE_NS);
            Phase {
                tick: Tick(since / PERIOD_NS),
                // a part of a period, exact in f64
                fraction: (since % PERIOD_NS) as f64 / PERIOD_NS as f64,
            }
        }

        fn reaching(&self, tick: Tick) -> Cycles {
            Cycles(FIRST_EDGE_NS + tick.0 * PERIOD_NS)
        }
    }

    /// RESET's rise, and the internal 1 PPS that follows it, on
    /// `Nanoseconds`.
    const RESET_NS: u64 = 1_000;
    const PULSE_NS: i64 = 1_000_000_950;

    /// `fpga_write` at `at` on `Nanoseconds`.
    fn write(fpga: &mut Fpga, register: u32, value: u8, at: u64) {
        fpga.write(
            0x4000 + register,
            value,
            Cycles(at),
            &Nanoseconds,
            Trims::default(),
        );
    }

    /// `fpga_read` (`0x451dc`) at `at`: the address into register 1,
    /// then the port.
    fn read(fpga: &mut Fpga, register: u8, at: u64) -> u8 {
        write(fpga, REGISTER_ADDRESS, register << ADDRESS_SHIFT, at);
        fpga.read(0x4000, Cycles(at), &Nanoseconds, None)
    }

    /// `counter_read_raw` (`0x456fc`): the bytes at `0x18`, `0x16`,
    /// `0x14` and `0x12`, each `reg + 1`'s low nibble high and `reg`'s low.
    fn count(fpga: &mut Fpga, at: u64) -> u32 {
        [0x18, 0x16, 0x14, 0x12]
            .into_iter()
            .fold(0, |count, register| {
                let byte =
                    (read(fpga, register + 1, at) & 0x0f) << 4 | read(fpga, register, at) & 0x0f;
                count << 8 | u32::from(byte)
            })
    }

    /// The firmware's reading (`counter_read`, `0x45810`): the count less
    /// the preset times 100 ns, plus 50 ns, less 0.4000003 s.  With the
    /// reference `offset_ns` after the internal 1 PPS's edge, it is on
    /// the grid of 50 ns + k x 100 ns, one tick above the interval
    /// rounded to the half tick (z3817a-3752.md, "The time offset").
    #[test]
    fn a_reading_falls_on_the_half_tick_grid() {
        const TICK_SECONDS: f64 = 1e-7;
        const HALF_TICK: f64 = 5e-8;
        const CALIBRATION: f64 = 0.400_000_3;
        for offset_ns in [-260_i64, -150, -149, -51, -50, -1, 0, 1, 37, 99, 100, 250] {
            let mut fpga = configured_on(&Nanoseconds, Cycles(RESET_NS));
            // the reference in the second before the pulse, TI positive
            // for a reference before it
            let reference = (PULSE_NS - NS_PER_SECOND as i64 / 2 - offset_ns) as u64;
            // the arm (`0x457c0`): the preset on, the address 0, the
            // preset off
            let arm_at = NS_PER_SECOND / 2 - 1_000_000;
            for (register, value) in [(0, 0x40), (1, 0x00), (0, 0x00)] {
                write(&mut fpga, register, value, arm_at);
            }
            fpga.gps_pps(Cycles(reference), &Nanoseconds, Trims::default());
            let read_at = 1_401_000_000;
            while let Some(due) = fpga.next_event(&Nanoseconds).filter(|due| due.0 <= read_at) {
                fpga.advance(due, &Nanoseconds);
            }
            let counted = count(&mut fpga, read_at).wrapping_sub(COUNT_PRESET as u32);
            let reading = f64::from(counted) * TICK_SECONDS + HALF_TICK - CALIBRATION;
            let ticks = (reading - HALF_TICK) / TICK_SECONDS;
            assert!(
                (ticks - ticks.round()).abs() < 1e-6,
                "{offset_ns}: {reading}"
            );
            // half a second from the reference to the pulse, and the
            // offset, rounded to the half tick, plus the tick
            let interval = (NS_PER_SECOND as f64 / 2.0 + offset_ns as f64) * 1e-9;
            let error = reading - (interval + TICK_SECONDS);
            assert!(error.abs() <= HALF_TICK + 1e-12, "{offset_ns}: {reading}");
        }
    }

    /// `check-readings-z3817a.sync1`, `.sync2` and `.sync3`
    /// (z3817a-trace.md, "The sync field"), as `z3817a_tb.v` runs them:
    /// the field written 0 then the kind on the first arm, with the
    /// preset on, the P82 1 PPS at 0.250000037 s each second, a reading
    /// each second.  Kind 1 moves the internal 1 PPS a clock earlier, the
    /// count after it 1,500,012, kind 2 a clock later, 1,500,014.  Kind 3
    /// restarts the divider on the 1 PPS and the restart is itself the
    /// internal 1 PPS, on the clock edge 113 ns after the 1 PPS (P78
    /// rising 2 ns later): both counts 4,000,005, the seconds 1 and 2.
    #[test]
    fn the_sync_kinds_are_the_netlists() {
        /// The first arm and the P82 1 PPS's place in each second.
        const ARM_NS: u64 = 70_000;
        const P82_NS: u64 = 250_000_037;
        /// A run: the kind, when the testbench reads its two counts, the
        /// internal 1 PPS's edges, and the readings, seconds and count.
        struct Run {
            kind: u8,
            reads: [u64; 2],
            pulses: &'static [u64],
            readings: [(u8, u32); 2],
        }
        let runs = [
            Run {
                kind: 1,
                reads: [401_700_000, 1_401_700_000],
                pulses: &[1_000_000_850],
                readings: [(0, 1_500_013), (1, 1_500_012)],
            },
            Run {
                kind: 2,
                reads: [401_700_000, 1_401_700_000],
                pulses: &[1_000_001_050],
                readings: [(0, 1_500_013), (1, 1_500_014)],
            },
            Run {
                kind: 3,
                reads: [650_500_000, 1_651_200_000],
                pulses: &[250_000_150, 1_250_000_150],
                readings: [(1, 4_000_005), (2, 4_000_005)],
            },
        ];
        for run in runs {
            let mut fpga = configured_on(&Nanoseconds, Cycles(RESET_NS));
            let mut pulses = Vec::new();
            let mut readings = Vec::new();
            let mut arm_at = ARM_NS;
            for (second, read_at) in run.reads.into_iter().enumerate() {
                let sync: &[(u32, u8)] = if second == 0 {
                    &[(3, 0x00), (3, run.kind)]
                } else {
                    &[]
                };
                write(&mut fpga, 0, 0x40, arm_at);
                write(&mut fpga, 1, 0x00, arm_at);
                for (register, value) in sync {
                    write(&mut fpga, *register, *value, arm_at);
                }
                write(&mut fpga, 0, 0x00, arm_at);
                let p82 = second as u64 * NS_PER_SECOND + P82_NS;
                for (until, then_p82) in [(p82, true), (read_at, false)] {
                    while let Some(due) = fpga.next_event(&Nanoseconds).filter(|due| due.0 <= until)
                    {
                        if let Some(pulse) = fpga.advance(due, &Nanoseconds) {
                            pulses.push(pulse.0);
                        }
                    }
                    if then_p82 {
                        fpga.gps_pps(Cycles(p82), &Nanoseconds, Trims::default());
                    }
                }
                let seconds = read(&mut fpga, 0x00, read_at) >> 4;
                let counted = count(&mut fpga, read_at).wrapping_sub(COUNT_PRESET as u32);
                readings.push((seconds, counted));
                arm_at = read_at;
            }
            assert_eq!(pulses, run.pulses, "kind {}", run.kind);
            assert_eq!(readings, run.readings, "kind {}", run.kind);
        }
    }
}
