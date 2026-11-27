//! The Z3805A's design, the time-interval counter, the divider that
//! makes the internal 1 PPS from the 10 MHz clock on its P7, and the
//! sync, as docs/firmware/counter.md, "What the netlist shows", reads
//! its decoded netlist, simulated: not checked on a board, and the pins'
//! roles read from the logic.  The model is of events, not clock edges:
//! each pulse, tap, start and stop is a value of the phase of the
//! 10 MHz clock signal the board gives the part (clock.rs), in its
//! periods, ticks, whole where the netlist acts on a clock edge; it
//! knows no period of its own.  The divider, the sync and the stop are
//! the family's (timebase.rs).  The differential test below drives it
//! and the netlist (tools/fpga/differential.py) with the same stimulus
//! and compares what each reads.
//!
//! - **The port.**  Registers 0, 1 and 3 keep register 0's mode, as
//!   one flip-flop, whether it is 3, and its preset (bit 6); register
//!   1's start (bit 0), reference (bit 1) and read address (bits 3 to
//!   7); register 3's sync field (bits 0 and 1).  Register 2 and the
//!   other bits are not kept.  Only ADDR0 to ADDR2 reach the part, and
//!   ADDR2 is Low in every write decode.  A read drives the seconds
//!   count in bits 4 to 7 whatever the address; bits 0 and 1 are the
//!   status's, stop and start, at every address below `0x10`; bits 2
//!   and 3, and bits 0 and 1 from `0x10` on, the nibble the address's
//!   low four bits select among `0x10` to `0x17`: the interpolator's
//!   byte complemented, then the count's six nibbles, and nothing for
//!   `0x18` up.  So the status's bits 2 and 3 are the interpolator's
//!   P5 and P6, complemented.
//! - **The count.**  The preset clears it all and holds the count at
//!   `0x555555`.  Without it, in a mode other than 3, the GPS 1 PPS's
//!   rising edge starts the count (status bit 1), and the gate opens on
//!   the first clock edge after it with the reference bit Low, on the
//!   second with it High; the count runs to the stop (timebase.rs).
//!   So the count, less its preset, is `4,000,003 + (t_internal -
//!   t_GPS) / 100 ns` less the start's fraction of a tick, with the
//!   reference High.
//! - **Mode 3.**  The start bit's fall with the preset off starts on its
//!   write's strobe, a clock edge, sets both status bits at once and
//!   counts six edges; AH.Y lasts one clock with the reference bit Low,
//!   two with it High: the interpolator's references.  The design's
//!   reset leaves it in mode 3 and makes one such start.
//! - **The divider and the sync**, timebase.rs's, the GPS 1 PPS the
//!   pulse a full sync restarts the divider on.

use super::super::clock::Phase;
use super::super::clock::Tick;
use super::timebase::Restart;
use super::timebase::STOP_PIPELINE;
use super::timebase::Start;
use super::timebase::Timebase;
use super::timebase::WRITE_STROBE;

/// The registers the port decodes (counter.md, "What the netlist
/// shows"): ADDR0 to ADDR2, register 2 decoding to nothing.
const REGISTER_MODE: u32 = 0;
const REGISTER_CONTROL: u32 = 1;
const REGISTER_SYNC: u32 = 3;
/// Register 0: the mode, kept only as whether it is 3, and the preset.
const MODE_MASK: u8 = 0x03;
const MODE_REFERENCES: u8 = 3;
const PRESET: u8 = 0x40;
/// Register 1: the start, the reference and the read address.
const START: u8 = 0x01;
const REFERENCE_HIGH: u8 = 0x02;
const ADDRESS_SHIFT: u8 = 3;

/// The read multiplexer: the address's bit 4, which takes bits 0 and 1
/// from the nibble rather than the status, and the nibbles it selects
/// (smartclock.h's `fpga_register`): the interpolator's byte, then the
/// count's three, each low nibble first, and nothing past them.
const READ_NIBBLES: u8 = 0x10;
const READ_NIBBLE_MASK: u8 = 0x0f;
const READ_INTERPOLATOR: u8 = 0x10;
const READ_INTERPOLATOR_HIGH: u8 = 0x11;
const READ_COUNT_LOW: u8 = 0x12;
const READ_COUNT_HIGH: u8 = 0x17;
const NIBBLE_BITS: u8 = 4;
const NIBBLE_MASK: u32 = 0x0f;
/// Bits 0 and 1 of a read, the status's or the nibble's.
const STATUS_MASK: u8 = 0x03;
/// The status's bits (ti.c, `TI_STATUS_*`; counter.md): bit 0 set at the
/// stop, bit 1 at the start, and in the high nibble the internal 1 PPS's
/// seconds, modulo 16 (`fpga_read_seconds`).
const STATUS_STOPPED: u8 = 0x01;
const STATUS_STARTED: u8 = 0x02;
const SECONDS_SHIFT: u8 = 4;
const SECONDS_MASK: u8 = 0x0f;
/// What the counter presets to (`counter_offset_apply`), and its width.
const COUNT_PRESET: u32 = 0x55_5555;
const COUNT_MASK: u32 = 0xff_ffff;

/// The gate opens this many ticks after the start, as the next clock
/// edge after it: the first with the reference bit Low, the second with
/// it High.
const OPEN_LOW: u64 = 1;
const OPEN_HIGH: u64 = 2;
/// The ticks mode 3's start counts, whichever the reference.
const REFERENCE_COUNT: u64 = 6;

/// The registers as the design keeps them (counter.md, "What the netlist
/// shows"), all clear after its reset; the sync field is the timebase's.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
struct Registers {
    /// Register 0's mode is not 3 (FH.X): Low, mode 3, after a reset.
    counting: bool,
    /// Register 0's preset (FH.Y).
    preset: bool,
    /// Register 1's start and reference bits (CG).
    start: bool,
    reference_high: bool,
    /// Register 1's read address, its top five bits.
    address: u8,
}

/// The design's state.
#[derive(Debug, Clone, Copy, PartialEq)]
pub(super) struct Z3805a {
    registers: Registers,
    /// The divider, its seconds (IK, IL) and the sync field (DI, EK).
    timebase: Timebase,
    /// The count, once started (AH.X).
    pub(super) start: Option<Start>,
}

impl Z3805a {
    /// The design held in reset at the phase `at`, RESET Low: every
    /// register clear, nothing started.
    pub(super) fn held(at: Phase) -> Self {
        Self {
            registers: Registers::default(),
            timebase: Timebase::reset(at.tick, Restart::Silent),
            start: None,
        }
    }

    /// The design as its reset at the phase `at` leaves it: the
    /// registers clear, so in mode 3, which starts at once on the first
    /// edge.
    pub(super) fn reset(at: Phase) -> Self {
        let mut design = Self::held(at);
        design.reference_start(at.edge(1));
        design
    }

    /// The next pulse, unless held.
    #[cfg(test)]
    pub(super) fn next_pulse(&self) -> Option<Tick> {
        self.timebase.next_pulse()
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

    /// The clock edge after the phase `at` that AH.Y ends on, by the
    /// reference bit.
    fn open(&self, at: Phase) -> Tick {
        at.edge(if self.registers.reference_high {
            OPEN_HIGH
        } else {
            OPEN_LOW
        })
    }

    /// The count started as `start`.  Returns the interval the
    /// interpolator measures, seconds.
    fn started(&mut self, start: Start) -> f64 {
        self.start = Some(start);
        start.interval()
    }

    /// Mode 3's start on the clock edge `strobe`: stopped at once, the
    /// gate counting `REFERENCE_COUNT` ticks.  Returns its interval.
    fn reference_start(&mut self, strobe: Tick) -> f64 {
        let at = Phase::from(strobe);
        self.started(Start {
            at,
            open: self.open(at),
            gate: strobe,
            stop: Some(strobe + REFERENCE_COUNT - STOP_PIPELINE),
        })
    }

    /// The GPS 1 PPS rising at the phase `at`: a full sync pending
    /// restarts the divider on it, and without the preset, outside mode
    /// 3, it starts the count, the gate opening as AH.Y ends.  Returns
    /// the interval the interpolator measures, if it started one.
    pub(super) fn gps_pps(&mut self, at: Phase) -> Option<f64> {
        self.timebase.reference_pulse(at);
        let registers = self.registers;
        (registers.counting && !registers.preset && self.start.is_none()).then(|| {
            let open = self.open(at);
            self.started(Start {
                at,
                open,
                gate: open,
                stop: None,
            })
        })
    }

    /// The port's read at the phase `now` with the interpolator's byte
    /// `pins` on P3 to P11 (counter.md, "What the netlist shows"), its count
    /// off by `error` ticks: the differential test's tolerance of the
    /// netlist's edge timing, else 0.
    pub(super) fn read(&self, now: Phase, pins: u8, error: i32) -> u8 {
        let address = self.registers.address;
        let counted = self
            .start
            .map_or(0, |start| start.counted(now, u64::from(COUNT_MASK)));
        // within the 24-bit mask
        let count = COUNT_PRESET
            .wrapping_add(counted as u32)
            .wrapping_add_signed(error)
            & COUNT_MASK;
        let selected = address & READ_NIBBLE_MASK | READ_NIBBLES;
        let nibble = |value: u32, index: u8| (value >> (NIBBLE_BITS * index) & NIBBLE_MASK) as u8;
        let nibble = match selected {
            // the byte complemented, which interpolator_read undoes
            READ_INTERPOLATOR => nibble(u32::from(!pins), 0),
            READ_INTERPOLATOR_HIGH => nibble(u32::from(!pins), 1),
            READ_COUNT_LOW..=READ_COUNT_HIGH => nibble(count, selected - READ_COUNT_LOW),
            _ => 0,
        };
        let low = if address & READ_NIBBLES == 0 {
            self.start.map_or(0, |start| {
                STATUS_STARTED
                    | if start.stopped(now) {
                        STATUS_STOPPED
                    } else {
                        0
                    }
            })
        } else {
            nibble & STATUS_MASK
        };
        (self.timebase.seconds & SECONDS_MASK) << SECONDS_SHIFT | nibble & !STATUS_MASK | low
    }

    /// `value` written to the register `register`, ADDR0 to ADDR2, at
    /// the phase `now`.  Returns the interval the interpolator measures,
    /// if it started one.
    pub(super) fn write(&mut self, register: u32, value: u8, now: Phase) -> Option<f64> {
        let before = self.registers;
        let registers = &mut self.registers;
        match register {
            REGISTER_MODE => {
                registers.counting = value & MODE_MASK != MODE_REFERENCES;
                registers.preset = value & PRESET != 0;
            }
            REGISTER_CONTROL => {
                registers.start = value & START != 0;
                registers.reference_high = value & REFERENCE_HIGH != 0;
                registers.address = value >> ADDRESS_SHIFT;
            }
            REGISTER_SYNC => self.timebase.write_sync(value),
            _ => {}
        }
        let registers = self.registers;
        let strobe = now.edge(WRITE_STROBE);
        self.timebase.write_strobe(strobe);
        if registers.preset {
            self.start = None;
            None
        } else if !registers.counting && before.start && !registers.start && self.start.is_none() {
            // mode 3's start, on the start bit's fall
            Some(self.reference_start(strobe))
        } else {
            None
        }
    }
}

#[cfg(test)]
mod tests {
    use super::super::super::clock::Clock;
    use super::super::super::clock::Tick;
    use super::super::super::time::CPU_HZ;
    use super::super::super::time::Cycles;
    use super::super::Fpga;
    use super::super::Trims;
    use super::super::fixture::Case;
    use super::super::fixture::NETLIST;
    use super::super::fixture::cases;
    use super::super::fixture::hex;
    use super::super::logic::Logic;
    use super::super::tests::CLOCK;
    use super::super::tests::HELD;
    use super::super::tests::RUNNING;
    use super::super::tests::after;
    use super::super::tests::at_tick;
    use super::super::tests::configured;
    use super::super::tests::read_register;
    use super::super::tests::write;
    use super::super::timebase::DIVIDE;
    use super::super::timebase::Start;
    use super::super::timebase::TAP;

    #[test]
    fn the_counter_presets_and_the_interpolator_follows_its_trims() {
        let mut fpga = configured();
        let mut now = Cycles(0);
        // the preset, then the count's six nibbles
        write(&mut fpga, 0, 0x40, now);
        let count = (0x12..=0x17).rev().fold(0u32, |count, register| {
            count << 4 | u32::from(read_register(&mut fpga, register, now))
        });
        assert_eq!(count, 0x55_5555);
        // interpolator_measure at the low reference, its trim at the
        // middle and higher
        let mut measure = |trims: Trims| {
            for (register, value) in [(0, 0x43), (1, 0x01), (0, 0x03), (1, 0x00)] {
                fpga.write(0x4000 + register, value, now, &CLOCK, trims);
            }
            now = after(now);
            assert_eq!(
                fpga.read(0x4000, now, &CLOCK, None) & 0x03,
                0x03,
                "both bits"
            );
            let count =
                read_register(&mut fpga, 0x12, now) | read_register(&mut fpga, 0x13, now) << 4;
            assert_eq!(count, 0x5b, "six edges");
            !(read_register(&mut fpga, 0x10, now) | read_register(&mut fpga, 0x11, now) << 4)
        };
        let middle = measure(Trims::new(31, 0));
        assert!(
            measure(Trims::new(41, 0)) < middle,
            "a higher trim reads lower"
        );
    }

    /// The first internal 1 PPS 10,000,000 ticks after the reset, and
    /// every 10,000,000 on.
    #[test]
    fn the_1_pps_pulses_each_second_once_configured() {
        let mut fpga = Fpga::default();
        assert_eq!(fpga.advance(Cycles(10 * CPU_HZ), &CLOCK), None);
        let mut fpga = configured();
        assert_eq!(fpga.advance(Cycles(0), &CLOCK), None);
        let first = at_tick(DIVIDE);
        assert_eq!(fpga.next_event(&CLOCK), Some(first));
        assert_eq!(fpga.advance(first, &CLOCK), Some(first));
        assert_eq!(fpga.next_event(&CLOCK), Some(at_tick(2 * DIVIDE)));
    }

    /// A pulse a GPS 1 PPS or a read runs the design past, at or before
    /// the pulse's own event, is still reported by the next `advance`,
    /// and only once.
    #[test]
    fn a_pulse_run_past_outside_advance_is_reported() {
        let trims = Trims::default();
        let mut fpga = configured();
        assert_eq!(fpga.advance(Cycles(0), &CLOCK), None);
        let first = at_tick(DIVIDE);
        fpga.gps_pps(first, &CLOCK, trims);
        assert_eq!(fpga.next_event(&CLOCK), Some(first));
        assert_eq!(fpga.advance(first, &CLOCK), Some(first));
        assert_eq!(fpga.advance(first, &CLOCK), None);
        let second = at_tick(2 * DIVIDE);
        read_register(&mut fpga, 0, second);
        assert_eq!(fpga.advance(second, &CLOCK), Some(second));
    }

    /// `counter_arm`'s writes at `now`: the preset, mode 0, then the
    /// preset off, with a sync pair between when `sync` gives one.
    fn arm(fpga: &mut Fpga, sync: Option<u8>, now: Cycles) {
        write(fpga, 0, 0x40, now);
        if let Some(kind) = sync {
            write(fpga, 3, 0, now);
            write(fpga, 3, kind, now);
        }
        write(fpga, 0, 0x00, now);
    }

    /// `counter_read`'s reading at `now`, seconds: the count less its
    /// preset and the interpolator's fraction, the calibration taken
    /// off, with the interpolator's references under `trims`.
    fn reading(fpga: &mut Fpga, now: Cycles, trims: Trims) -> f64 {
        let count = [0x16, 0x14, 0x12]
            .into_iter()
            .fold(0u32, |count, register| {
                let byte = read_register(fpga, register, now)
                    | read_register(fpga, register + 1, now) << 4;
                count << 8 | u32::from(byte)
            });
        let interpolated = !(read_register(fpga, 0x10, now) | read_register(fpga, 0x11, now) << 4);
        let low = trims.reading(false).round();
        let high = trims.reading(true).round();
        let fraction = (f64::from(interpolated) - low) / (high - low);
        (f64::from(count) - f64::from(0x55_5555) + fraction) * 1e-7 - 0.400_000_3
    }

    /// The GPS 1 PPS starts the count (status bit 1), the tap 4,000,000
    /// ticks after the internal 1 PPS stops it (bit 0), and the reading,
    /// with the reference High, is TI, `t_internal - t_GPS`, to the
    /// interpolator's half nanosecond.
    #[test]
    fn the_count_runs_from_the_gps_1_pps_to_the_dividers_tap() {
        let mut fpga = configured();
        let trims = Trims::default();
        write(&mut fpga, 1, 0x02, Cycles(0));
        arm(&mut fpga, None, Cycles(0));
        // no count waits for the first second's tap
        let pulse = fpga.next_event(&CLOCK).expect("running");
        assert_eq!(pulse, at_tick(DIVIDE));
        assert_eq!(fpga.advance(pulse, &CLOCK), Some(pulse));
        // the GPS's 0.25 s and 1234 cycles later, so TI is negative
        let late = CPU_HZ / 4 + 1234;
        let gps = pulse + Cycles(late);
        fpga.gps_pps(gps, &CLOCK, trims);
        assert_eq!(read_register(&mut fpga, 0x00, gps) & 0x03, 0x02, "started");
        let tap = fpga.next_event(&CLOCK).expect("the tap");
        assert_eq!(tap, at_tick(DIVIDE + TAP));
        let before = Cycles(tap.0 - 1);
        assert_eq!(read_register(&mut fpga, 0x00, before) & 0x03, 0x02);
        // past the tap's four ticks of pipeline
        let done = after(tap);
        assert_eq!(read_register(&mut fpga, 0x00, done) & 0x03, 0x03, "stopped");
        let ti = 1.0 - CLOCK.phase(gps).seconds();
        let read = reading(&mut fpga, done, trims);
        assert!((read - ti).abs() < 0.5e-9, "{read} for {ti}");
        // the preset clears both and holds the count
        write(&mut fpga, 0, 0x40, done);
        assert_eq!(read_register(&mut fpga, 0x00, done) & 0x03, 0);
        assert!((reading(&mut fpga, done, trims) + 0.400_000_3).abs() < 1e-7);
    }

    /// The next internal 1 PPS.
    fn next_pulse(fpga: &Fpga) -> Option<Tick> {
        fpga.logic
            .as_ref()
            .and_then(Logic::z3805a)
            .expect("running")
            .next_pulse()
    }

    /// Kind 1 moves the next internal 1 PPS a tick earlier, 2 a tick
    /// later, on the field's change from 0; a field left at its kind
    /// does nothing more; 3 restarts the divider on the next GPS 1 PPS,
    /// its pulse a second on, or, the pulse coming first, holds it there
    /// until the GPS 1 PPS.
    #[test]
    fn the_sync_field_steps_and_restarts_the_internal_1_pps() {
        let mut fpga = configured();
        let mut now = Cycles(CPU_HZ / 2);
        fpga.advance(now, &CLOCK);
        let start = next_pulse(&fpga).expect("running");
        arm(&mut fpga, Some(1), now);
        assert_eq!(next_pulse(&fpga), Some(start - 1), "earlier");
        now = fpga.next_event(&CLOCK).expect("the pulse");
        fpga.advance(now, &CLOCK);
        assert_eq!(next_pulse(&fpga), Some(start - 1 + DIVIDE), "once");
        arm(&mut fpga, None, now);
        write(&mut fpga, 3, 1, now);
        assert_eq!(next_pulse(&fpga), Some(start - 1 + DIVIDE), "left at 1");
        arm(&mut fpga, Some(2), now);
        assert_eq!(next_pulse(&fpga), Some(start + DIVIDE), "later");
        // a full sync, the GPS 1 PPS first
        arm(&mut fpga, Some(3), now);
        let gps = now + Cycles(CPU_HZ / 3);
        fpga.gps_pps(gps, &CLOCK, Trims::default());
        let restart = CLOCK.phase(gps).edge(2);
        assert_eq!(next_pulse(&fpga), Some(restart + DIVIDE));
        fpga.gps_pps(gps + Cycles(CPU_HZ), &CLOCK, Trims::default());
        assert_eq!(next_pulse(&fpga), Some(restart + DIVIDE), "once");
        // a full sync, the pulse first: held until the GPS 1 PPS
        now = at_tick((restart + DIVIDE).0);
        assert!(fpga.advance(now, &CLOCK).is_some());
        arm(&mut fpga, Some(3), now);
        let held = at_tick((restart + 2 * DIVIDE).0);
        assert_eq!(fpga.advance(after(held), &CLOCK), None, "held");
        assert_eq!(next_pulse(&fpga), None);
        let gps = after(held) + Cycles(CPU_HZ / 5);
        fpga.gps_pps(gps, &CLOCK, Trims::default());
        let restart = CLOCK.phase(gps).edge(2);
        assert_eq!(next_pulse(&fpga), Some(restart + DIVIDE));
    }

    /// The clock edges a read's bus cycle spans in `z3805a_stimulus.v`,
    /// 1 us with the select Low, and some for the read's own pipeline,
    /// the select sampled and the data out through flip-flops on AL's
    /// clock, a quarter of the 10 MHz: a count still running reads as it
    /// was somewhere in this span.
    const BUS_CYCLE_TICKS: u8 = 16;
    /// Picoseconds a second, differential.py's unit for AH.Y.
    const PS_PER_SECOND: f64 = 1e12;
    /// How far the netlist's AH.Y may be from the model's interval: its
    /// models' nominal delays, a nanosecond a block, on the way in and
    /// out (tools/fpga/xc3000.v).
    const INTERVAL_TOLERANCE_PS: u128 = 3_000;
    /// The one-clock pulse of AH.Y a preset gives after a GPS start, AI
    /// and BJ reset a clock before AH (counter.md, "What the netlist
    /// shows"), picoseconds.
    const PRESET_PULSE_PS: u128 = 100_000;

    /// `case` through the model: what differs from the netlist.
    fn replay(case: &Case) -> Vec<String> {
        let mut fpga = configured();
        let trims = Trims::default();
        let mut pins = 0;
        let mut pulses = Vec::new();
        let mut intervals: Vec<Start> = Vec::new();
        let mut differences = Vec::new();
        for (at, operation) in &case.operations {
            let now = Cycles(*at);
            if let Some(pulse) = fpga.logic_at(now, &CLOCK).and_then(|(_, _, pulse)| pulse) {
                pulses.push(u128::from(pulse.0));
            }
            let words: Vec<&str> = operation.iter().map(String::as_str).collect();
            match words.as_slice() {
                ["reset"] => {
                    fpga.set_lines(HELD, now, &CLOCK);
                    fpga.set_lines(RUNNING, now, &CLOCK);
                }
                ["write", register, value] => {
                    let register: u32 = register.parse().expect("a register");
                    fpga.write(0x4000 + register, hex(value), now, &CLOCK, trims);
                }
                ["read", value] => {
                    let read = fpga.port(0, now, &CLOCK, Some(pins), None);
                    // a count within a tick of the netlist's, and one
                    // still running as the bus cycle samples it, up to
                    // its end
                    let phase = CLOCK.phase(now);
                    let mut span = std::iter::once(now).chain(
                        (1..=BUS_CYCLE_TICKS).map(|tick| at_tick(phase.edge(u64::from(tick)).0)),
                    );
                    let near = span.any(|later| {
                        (-1..=1).any(|error| {
                            fpga.port_counting(later, &CLOCK, pins, error) == hex(value)
                        })
                    });
                    if read != hex(value) && !near {
                        differences.push(format!(
                            "{}: {at} read {read:02x}, the netlist {value}",
                            case.name
                        ));
                    }
                }
                ["gps"] => fpga.gps_pps(now, &CLOCK, trims),
                ["pins", value] => pins = hex(value),
                ["end"] => {}
                _ => panic!("{}: {operation:?}", case.name),
            }
            // a start the operation made, and its interval
            let start = fpga
                .logic
                .as_ref()
                .and_then(Logic::z3805a)
                .and_then(|design| design.start);
            if let Some(start) = start
                && intervals
                    .last()
                    .is_none_or(|last| (last.at, last.open) != (start.at, start.open))
            {
                intervals.push(start);
            }
        }
        let widths: Vec<u128> = intervals
            .iter()
            // picoseconds, whole
            .map(|start| (start.interval() * PS_PER_SECOND).round() as u128)
            .collect();
        // the netlist's in order, but for the one-clock pulse a preset
        // gives AH.Y after a GPS start, which the model leaves out
        let near = |model: u128, netlist: u128| model.abs_diff(netlist) <= INTERVAL_TOLERANCE_PS;
        let mut model = widths.iter().peekable();
        let matched = case.intervals.iter().all(|netlist| {
            if model.peek().is_some_and(|model| near(**model, *netlist)) {
                model.next();
                true
            } else {
                near(PRESET_PULSE_PS, *netlist)
            }
        });
        if !matched || model.next().is_some() {
            differences.push(format!(
                "{}: intervals {widths:?} ps, the netlist {:?}",
                case.name, case.intervals
            ));
        }
        if pulses != case.pulses {
            differences.push(format!(
                "{}: pulses {pulses:?}, the netlist {:?}",
                case.name, case.pulses
            ));
        }
        differences
    }

    fn compare(text: &str) {
        let cases = cases(text);
        assert!(!cases.is_empty());
        let differences: Vec<String> = cases.iter().flat_map(replay).collect();
        assert!(differences.is_empty(), "{}", differences.join("\n"));
    }

    /// The model reads what the netlist read, for every case of the
    /// fixture.
    #[test]
    fn the_model_reads_as_the_netlist_does() {
        compare(NETLIST);
    }

    /// The same against the netlist run now, where Icarus Verilog and the
    /// generated netlist are (docs/firmware/outputs.md, "Netlists"):
    /// minutes a simulated second, so only when asked.  `make -f
    /// tools/fpga/Makefile check-differential` does the same in parallel.
    #[test]
    #[ignore = "runs the netlist in Icarus Verilog, minutes a simulated second"]
    fn the_model_reads_as_the_netlist_does_run_now() {
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
        let netlist = root.join("target/fpga/z3805a.v");
        assert!(
            netlist.exists(),
            "no {}: `make -f tools/fpga/Makefile target/fpga/z3805a.v` makes it",
            netlist.display()
        );
        // A simulation of its own, so that a concurrent
        // `make check-differential` does not build over it.
        let binary = std::env::temp_dir().join(format!("z3805a_stimulus.{}", std::process::id()));
        let output = std::process::Command::new("python3")
            .arg("-I")
            .arg(root.join("tools/fpga/differential.py"))
            .arg("--netlist")
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
        compare(&text);
        assert_eq!(
            text, NETLIST,
            "the fixture is stale: differential.py writes it"
        );
    }
}
