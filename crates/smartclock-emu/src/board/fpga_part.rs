//! The FPGA and the interpolator beside it as mc683xx-board parts
//! (docs/emulator.md, "The mc683xx backend"): `fpga.rs`'s event-level
//! model, unchanged, behind the ports the board files give the XC3042.
//!
//! The XC3042's ports are its pins as the board files name them
//! (boards/z3805a.toml): `P82`, the GPS 1 PPS in; `P84`, the Z3817A's
//! design's second 1 PPS in (z3817a.rs); `P7`, the 10 MHz in,
//! the OCXO's clock net, whose history gives the model its clock
//! (clock.rs) on board time; `pps`, the internal 1 PPS out; `INIT` out;
//! `DONE`, open drain on its wired net, read back resolved; `RESET`,
//! port E's PROGRAM; `M0`, port F's readback trigger; and
//! `P3-P6,P8-P11`, the interpolator's byte in.
//!
//! The CPU's RESET does not reach it, whatever the board file's reset
//! scope: the part's RESET is PROGRAM, which a watchdog reset leaves
//! high, undriven, so that it keeps its configuration and its 1 PPS's
//! phase, as the firmware's warm start takes them (docs/emulator.md,
//! "Across a warm restart").
//!
//! The model computes the interpolator's reading itself from the hex
//! DAC's trims (fpga/interpolator.rs, `Trims`), so the interpolator part
//! carries the trims across that bus: the low trim in bits 0 to 7, the
//! high in 8 to 15.  Not the byte the board's lines carry.  The trims
//! come in as the stock MC144110's outputs drive them, volts, each
//! taken back to its DAC's six-bit code (`trim_code`).

use mc683xx_board::peripheral::Drive;
use mc683xx_board::peripheral::Peripheral;
use mc683xx_board::peripheral::PeripheralAccess;
use mc683xx_board::peripheral::Transfer;
use mc683xx_board::registry::Context;
use mc683xx_board::registry::ParameterError;
use mc683xx_board::registry::PortSpec;
use mc683xx_board::registry::Registry;
use mc683xx_board::registry::RegistryError;
use mc683xx_board::signal::Kind;
use mc683xx_board::signal::Signal;
use mc683xx_board::spi::Edge;
use mc683xx_board::spi::Edges;
use mc683xx_board::time::Clock as NetClock;
use mc683xx_board::time::ClockHistory;
use mc683xx_board::time::Moment;
use mc683xx_board::time::PHASE_UNITS;
use mc683xx_board::time::Span as BoardSpan;
use mc683xx_board::time::Time;
use mc683xx_board::time::TimeBase;
use mc683xx_cpu32::Frequency;

use mc683xx_cpu32::BusError;
use mc683xx_cpu32::state::StateError;

use super::clock::Clock;
use super::clock::IDEAL_HZ;
use super::clock::Phase;
use super::clock::Tick;
use super::fpga::Configured;
use super::fpga::Design;
use super::fpga::Fpga;
use super::fpga::Lines;
use super::fpga::PORT;
use super::fpga::interpolator::Trims;
use super::parameters::fpga::BITSTREAM;
use super::parameters::fpga::DESIGN;
use super::participants::Span;
use super::participants::span;
use super::registry::XC3042;
use super::snapshot;
use super::spi;
use super::system_clock::cycles;
use super::system_clock::mc683xx_cycle;
use super::system_clock::time;
use super::time::Cycles;
use crate::trace;

/// The interpolator's.
pub(crate) const INTERPOLATOR: &str = "smartclock-interpolator";

/// The FPGA's ports, by index.
const P82: usize = 0;
const P7_CLOCK: usize = 1;
const PPS: usize = 2;
const INIT: usize = 3;
const DONE: usize = 4;
const RESET: usize = 5;
const M0: usize = 6;
const BYTE: usize = 7;
const P84: usize = 8;

/// The interpolator's ports, by index.
const TRIM_LOW: usize = 0;
const TRIM_HIGH: usize = 1;
const INTERPOLATOR_BYTE: usize = 2;

/// The XC3042's SPI edges, CCLK on SCK, DIN on MOSI and M1/RDATA on
/// MISO: it takes DIN on CCLK's rise (Slave Serial mode, 7-31) and
/// shifts its readback out on each rise (Readback, 7-23), the stream's
/// first bit out from the readback's start (*inferred*: what RDATA
/// holds before the first rise; the model counts it among the three
/// dummy bits, `fpga.rs`).  In mode 2, the mode `qspi_queue_setup`
/// drives it in (structures.md, "The QSPI's devices"), the master
/// captures on the fall between, so every bit is whole.
const EDGES: Edges = Edges::new(Edge::Rising, Edge::Rising, true);

/// Where the interpolator's bus carries each trim.
const TRIM_HIGH_SHIFT: u32 = 8;
const TRIM_MASK: u32 = 0xff;

/// The hex DAC's span, volts, as the interpolator takes it: its
/// `trim_vss` and `trim_vdd`, which the board files give as the hex
/// DAC's own `vss_volts` and `vdd_volts`, the stock part's defaults unless given.
/// The MC144110's outputs run from VSS at code 0 by (VDD - VSS) / 64 a
/// code (MC144110/D, Figures 3 and 4); its VDD rail is open
/// (hardware-investigations.md, item 23).
const TRIM_VSS: (&str, f64) = ("trim_vss", 0.0);
const TRIM_VDD: (&str, f64) = ("trim_vdd", 5.0);
/// The MC144110's codes over its span.
const TRIM_CODES: f64 = 64.0;

/// The internal 1 PPS's width.  The model gives only the rising edge
/// input capture 1 takes; the width is our choice, not the board's.
const PPS_WIDTH: BoardSpan = BoardSpan::from_micros(10);

/// Registers the FPGA's and the interpolator's types.
pub(crate) fn register(registry: &mut Registry) -> Result<(), RegistryError> {
    registry.register(XC3042, fpga_ports(), |context| {
        Ok(Box::new(FpgaPart::new(context)?) as Box<dyn Peripheral>)
    })?;
    registry.register(
        INTERPOLATOR,
        vec![
            PortSpec::input("trim_low", Kind::Analog),
            PortSpec::input("trim_high", Kind::Analog),
            PortSpec::output("byte", Kind::Bus),
        ],
        |context| {
            Ok(Box::new(InterpolatorPart {
                trims: [0; 2],
                driven: None,
                span: span(context, TRIM_VSS, TRIM_VDD)?,
            }) as Box<dyn Peripheral>)
        },
    )
}

/// The FPGA's ports, in their index order.
fn fpga_ports() -> Vec<PortSpec> {
    vec![
        PortSpec::input("P82", Kind::Edge),
        PortSpec::input("P7", Kind::Clock),
        PortSpec::output("pps", Kind::Edge),
        PortSpec::output("INIT", Kind::Logic),
        PortSpec::in_out("DONE", Kind::Logic),
        PortSpec::input("RESET", Kind::Logic),
        PortSpec::input("M0", Kind::Logic),
        PortSpec::input("P3-P6,P8-P11", Kind::Bus),
        PortSpec::input("P84", Kind::Edge),
    ]
}

/// The clock net on the FPGA's P7 as the model reads it (clock.rs): the
/// net's history of the OCXO's frequency on board time, through the
/// system clock between board time and the model's cycles.
struct P7<'a> {
    /// What the net has carried.
    history: &'a ClockHistory,
    /// The system clock on board time.
    base: TimeBase,
}

impl<'a> P7<'a> {
    /// The net's history `history` through the call's system clock.
    fn new(history: &'a ClockHistory, drive: &Drive<'_>) -> Self {
        Self {
            history,
            base: drive.system_clock(),
        }
    }
}

impl Clock for P7<'_> {
    fn phase(&self, at: Cycles) -> Phase {
        let at = self.base.time(mc683xx_cycle(at));
        let phase = self.history.phase(at).unwrap_or_default();
        Phase {
            // a run's ticks fit u64
            tick: Tick(phase.cycles() as u64),
            fraction: phase.fraction() as f64 / PHASE_UNITS as f64,
        }
    }

    /// The first cycle at or after the edge's picosecond, the first at
    /// or after the edge itself: exact, so that the phase there has
    /// reached it.
    fn reaching(&self, tick: Tick) -> Cycles {
        let at = self.history.edge(u128::from(tick.0));
        Cycles(self.base.cycle(at.unwrap_or(Time::MAX)).clocks())
    }
}

/// `xc3042`: the FPGA, from `part` and `design`, the design the model
/// runs (fpga.rs, `Design::NAMES`), and with `lca` an optional
/// `bitstream`, the one its configuration must be: the model configures
/// from the bitstream the firmware sends; the trace says which netlist
/// the LCA model runs, and a configuration it refuses stops the run,
/// saying why.  Its P7 is the OCXO's clock net (`ten-megahertz`); until the
/// net carries a clock it runs at its nominal 10 MHz from power-on.
#[derive(Debug)]
struct FpgaPart {
    /// The model.
    fpga: Fpga,
    /// Its configuration lines as the nets drive them: PROGRAM and the
    /// readback trigger high, DONE released, until driven.
    lines: Lines,
    /// The interpolator's trims as its bus has them.
    trims: Trims,
    /// The clock on P7, as its net has carried it.
    p7: ClockHistory,
    /// The GPS 1 PPS's level on P82, and P84's, for their rising edges.
    pps_in: bool,
    p84_in: bool,
    /// INIT and DONE as last driven, none before the first drive.
    init: Option<bool>,
    done: Option<bool>,
}

/// How long P7's history is kept: the oscillator model's own span
/// (ocxo.rs, `RETAIN_SECONDS`), far past what the model asks back for.
const P7_HISTORY: BoardSpan = BoardSpan::from_secs(64);

impl FpgaPart {
    /// The FPGA as power-up leaves it, running the design its board
    /// file names.
    fn new(context: &Context<'_>) -> Result<Self, ParameterError> {
        let name = context
            .parameters
            .string(DESIGN)?
            .ok_or_else(|| ParameterError::Missing(DESIGN.to_owned()))?;
        let design = Design::named(name).ok_or_else(|| {
            let names: Vec<&str> = Design::NAMES.iter().map(|&(known, _)| known).collect();
            ParameterError::invalid(
                DESIGN,
                format!("\"{name}\" is not one of {}", names.join(", ")),
            )
        })?;
        let design = match (design, context.parameters.string(BITSTREAM)?) {
            (design, None) => design,
            (Design::Lca { .. }, Some(asserted)) => Design::Lca {
                bitstream: Some(
                    Design::bitstreams()
                        .find(|known| *known == asserted)
                        .ok_or_else(|| {
                            let known: Vec<&str> = Design::bitstreams().collect();
                            ParameterError::invalid(
                                BITSTREAM,
                                format!(
                                    "\"{asserted}\" is not one of the LCA model's netlists, {}",
                                    known.join(", ")
                                ),
                            )
                        })?,
                ),
            },
            (_, Some(_)) => {
                return Err(ParameterError::invalid(
                    BITSTREAM,
                    format!("only design = \"lca\" runs a bitstream, not \"{name}\""),
                ));
            }
        };
        let mut p7 = ClockHistory::new(P7_HISTORY);
        p7.push(NetClock::new(Frequency::from_hz(IDEAL_HZ), Time::ZERO));
        Ok(Self {
            fpga: Fpga::new(design),
            lines: Lines {
                program: true,
                done_held_low: false,
                readback: true,
            },
            trims: Trims::default(),
            p7,
            pps_in: false,
            p84_in: false,
            init: None,
            done: None,
        })
    }

    /// INIT and DONE onto their nets where they changed; and at DONE's
    /// rise the internal 1 PPS Low, the design's output starting Low
    /// where the part's pull-up held the pin High while it configured
    /// (`third_party/xilinx/xc3000.pdf`, p. 21: "Prior to the completion
    /// of configuration the user I/O pins each have a high impedance
    /// pull-up"): the chip's input capture sees the first pulse's rise
    /// as an edge.
    fn sync(&mut self, drive: &mut Drive<'_>) {
        for (port, level, driven) in [
            (INIT, self.fpga.init(), &mut self.init),
            (DONE, self.fpga.done(), &mut self.done),
        ] {
            if *driven != Some(level) {
                *driven = Some(level);
                drive.drive(port, Signal::Level(level));
                if port == DONE && level {
                    drive.drive(PPS, Signal::Level(false));
                }
            }
        }
    }

    /// The internal 1 PPS at `pulse`, a rising edge and its fall.
    fn pulse(pulse: Cycles, drive: &mut Drive<'_>) {
        let at = time(drive, pulse);
        drive.drive_at(at, PPS, Signal::Level(true));
        drive.drive_at(at + PPS_WIDTH, PPS, Signal::Level(false));
    }

    /// A configuration done by now starts the design, its 1 PPS's
    /// divider from now, as the Unicorn board's next slice does.
    fn configured(&mut self, now: Time, drive: &mut Drive<'_>) {
        if self.fpga.done() {
            self.run(now, drive);
        }
        self.sync(drive);
    }

    /// The design run to `now`, the internal 1 PPS it has not reported
    /// driven: one an access or the GPS 1 PPS ran it past goes out now.
    fn run(&mut self, now: Time, drive: &mut Drive<'_>) {
        let clock = P7::new(&self.p7, drive);
        if let Some(pulse) = self.fpga.advance(cycles(drive, now), &clock) {
            Self::pulse(pulse, drive);
        }
    }
}

impl Peripheral for FpgaPart {
    fn read(&mut self, access: PeripheralAccess, drive: &mut Drive<'_>) -> Result<u32, BusError> {
        let now = cycles(drive, drive.now());
        let clock = P7::new(&self.p7, drive);
        // the last cycle before it: `cycles` gives the first at or after
        let horizon = access
            .horizon
            .map(|horizon| Cycles(cycles(drive, horizon).0.saturating_sub(1)));
        let value = self.fpga.read(PORT + access.address, now, &clock, horizon);
        self.run(drive.now(), drive);
        Ok(u32::from(value))
    }

    fn write(
        &mut self,
        access: PeripheralAccess,
        value: u32,
        drive: &mut Drive<'_>,
    ) -> Result<(), BusError> {
        let now = cycles(drive, drive.now());
        let clock = P7::new(&self.p7, drive);
        // a byte port
        self.fpga
            .write(PORT + access.address, value as u8, now, &clock, self.trims);
        self.run(drive.now(), drive);
        Ok(())
    }

    fn transfer(&mut self, transfer: Transfer, drive: &mut Drive<'_>) -> u16 {
        let (back, word) = spi::shift(transfer, EDGES, self.fpga.reply(transfer.bits));
        self.fpga.exchange(word, transfer.bits);
        match self.fpga.take_configured() {
            Some(Configured::Running(name)) => {
                trace::note(format_args!(
                    "fpga: the LCA model runs {name}.bit's netlist"
                ));
            }
            Some(Configured::Refused(refusal)) => {
                trace::note(format_args!("fpga: {refusal}"));
                drive.stop();
            }
            None => {}
        }
        self.configured(drive.now(), drive);
        back
    }

    fn input(&mut self, port: usize, signal: Signal, drive: &mut Drive<'_>) {
        let now = drive.now();
        match (port, signal) {
            (P7_CLOCK, Signal::Clock(clock)) => self.p7.push(clock),
            (P82, Signal::Level(high)) => {
                let changed = high != self.pps_in;
                self.pps_in = high;
                let clock = P7::new(&self.p7, drive);
                match (changed, high) {
                    (true, true) => self.fpga.gps_pps(cycles(drive, now), &clock, self.trims),
                    (true, false) => self.fpga.gps_pps_fall(cycles(drive, now), &clock),
                    (false, _) => return,
                }
                self.run(now, drive);
            }
            (P84, Signal::Level(high)) => {
                let rising = high && !self.p84_in;
                self.p84_in = high;
                if rising {
                    let clock = P7::new(&self.p7, drive);
                    self.fpga.p84(cycles(drive, now), &clock);
                    self.run(now, drive);
                }
            }
            (BYTE, Signal::Bus(value)) => {
                // each trim six bits, within its byte
                let low = (value & TRIM_MASK) as u8;
                let high = (value >> TRIM_HIGH_SHIFT & TRIM_MASK) as u8;
                self.trims = Trims::new(low, high);
            }
            (RESET | M0 | DONE, Signal::Level(high)) => {
                match port {
                    RESET => self.lines.program = high,
                    M0 => self.lines.readback = high,
                    _ => self.lines.done_held_low = !high,
                }
                let clock = P7::new(&self.p7, drive);
                self.fpga.set_lines(self.lines, cycles(drive, now), &clock);
                self.configured(now, drive);
            }
            _ => {}
        }
    }

    fn power_on(&mut self, drive: &mut Drive<'_>) {
        self.sync(drive);
    }

    /// Only for the LCA model, whose reads sample their bus cycle on the
    /// netlist exactly where nothing can reach the part first; the hand
    /// model answers at the access's start and needs none.
    fn wants_horizon(&self) -> bool {
        self.fpga.runs_netlist()
    }

    /// The internal 1 PPS's next pulse, the tap a count waits for, or a
    /// re-program by DONE/PROG held Low: `advance` there runs the design
    /// past it.
    fn next_event(&self, moment: Moment) -> Option<Time> {
        let system_clock = moment.system_clock();
        let clock = P7 {
            history: &self.p7,
            base: system_clock,
        };
        Some(system_clock.time(mc683xx_cycle(self.fpga.next_event(&clock)?)))
    }

    fn advance(&mut self, drive: &mut Drive<'_>) {
        self.run(drive.now(), drive);
        // a re-program clears DONE
        self.sync(drive);
    }
}

/// The hex DAC's code whose output is `volts` on `trims`, the nearest.
fn trim_code(trims: Span, volts: f64) -> u8 {
    let code = trims.code(volts, TRIM_CODES);
    // six bits, within a byte
    code.round().clamp(0.0, TRIM_CODES - 1.0) as u8
}

/// `smartclock-interpolator`: the hex DAC's trims in, volts over the
/// DAC's span, `trim_vss` and `trim_vdd`, on `trim_low` and
/// `trim_high`, and out on `byte` to the FPGA's model as this module
/// says.
#[derive(Debug)]
struct InterpolatorPart {
    /// The trims as their nets have them.
    trims: [u8; 2],
    /// The value last driven, none before the first drive.
    driven: Option<u32>,
    /// The hex DAC's span.
    span: Span,
}

impl Peripheral for InterpolatorPart {
    fn save(&self) -> Result<Vec<u8>, StateError> {
        Ok(snapshot::save(&(self.trims, self.driven)))
    }

    fn restore(&mut self, snapshot: &[u8]) -> Result<(), StateError> {
        (self.trims, self.driven) = snapshot::load(snapshot)?;
        Ok(())
    }

    fn input(&mut self, port: usize, signal: Signal, drive: &mut Drive<'_>) {
        let (TRIM_LOW | TRIM_HIGH, Signal::Analog(value)) = (port, signal) else {
            return;
        };
        self.trims[port] = trim_code(self.span, value);
        let value =
            u32::from(self.trims[TRIM_LOW]) | u32::from(self.trims[TRIM_HIGH]) << TRIM_HIGH_SHIFT;
        if self.driven != Some(value) {
            self.driven = Some(value);
            drive.drive(INTERPOLATOR_BYTE, Signal::Bus(value));
        }
    }
}

#[cfg(test)]
mod tests {

    use std::path::PathBuf;

    use mc683xx_board::Environment;
    use mc683xx_board::peripheral::Drive;
    use mc683xx_board::peripheral::Peripheral;
    use mc683xx_board::peripheral::PeripheralAccess;
    use mc683xx_board::peripheral::Transfer;
    use mc683xx_board::registry::Context;
    use mc683xx_board::registry::ParameterError;
    use mc683xx_board::registry::Parameters;
    use mc683xx_board::registry::Registry;
    use mc683xx_board::signal::Signal;
    use mc683xx_board::time::Time;

    use super::DONE;
    use super::INIT;
    use super::INTERPOLATOR;
    use super::INTERPOLATOR_BYTE;
    use super::PPS;
    use super::PPS_WIDTH;
    use super::RESET;
    use super::TRIM_HIGH;
    use super::TRIM_LOW;
    use super::XC3042;
    use super::register;
    use mc683xx_board::time::Clock as NetClock;
    use mc683xx_board::time::ClockHistory;
    use mc683xx_cpu32::Frequency;
    use mc683xx_target::FunctionCode;
    use mc683xx_target::Size;

    use super::super::fpga::Design;
    use super::P7;
    use super::P7_CLOCK;
    use super::P7_HISTORY;
    use crate::board::clock::Clock;
    use crate::board::clock::IDEAL_HZ;
    use crate::board::clock::Tick;
    use crate::board::system_clock::test_base;
    use crate::board::system_clock::test_drive;
    use crate::board::system_clock::test_moment;
    use crate::board::system_clock::test_qspi_clock_in;
    use crate::board::time::Cycles;

    /// The repository's FPGA bitstreams (outputs.md, "The FPGA
    /// bitstreams"), and its board files.
    const BITSTREAMS: &str = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../third_party/firmware/images/fpga"
    );
    const BOARDS: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../../boards");

    /// A part of `kind` with the parameters the TOML table `parameters`
    /// gives.
    fn constructed(kind: &str, parameters: &str) -> Result<Box<dyn Peripheral>, ParameterError> {
        let mut registry = Registry::new();
        register(&mut registry).expect("registered");
        let parameters = Parameters::parse(parameters).expect("a table");
        match registry.construct(
            kind,
            &Context::new(kind, &parameters, &Environment::default()),
        ) {
            Ok(constructed) => Ok(constructed.peripheral),
            Err(error) => Err(error
                .into_parameter()
                .unwrap_or_else(|error| panic!("{error}"))),
        }
    }

    /// The FPGA running the design `name`.
    fn fpga(name: &str) -> Box<dyn Peripheral> {
        constructed(XC3042, &format!("design = \"{name}\"")).expect("constructed")
    }

    /// A part of `kind`: the FPGA with the Z3805A's design, else with no
    /// parameters.
    fn part(kind: &str) -> Box<dyn Peripheral> {
        if kind == XC3042 {
            fpga("z3805a")
        } else {
            constructed(kind, "").expect("constructed")
        }
    }

    /// What `call` drove at `now`: port, time and value.
    fn driving(now: Time, call: impl FnOnce(&mut Drive<'_>)) -> Vec<(usize, Time, Signal)> {
        let mut drive = test_drive(now);
        call(&mut drive);
        drive
            .finish()
            .driven
            .into_iter()
            .map(|driven| (driven.port, driven.at, driven.signal))
            .collect()
    }

    #[test]
    fn programmed_it_raises_done_and_its_1_pps_follows_a_second_on() {
        let mut fpga = part(XC3042);
        let now = Time::from_secs(1);
        let driven = driving(now, |drive| {
            fpga.input(RESET, Signal::Level(false), drive);
            fpga.input(RESET, Signal::Level(true), drive);
        });
        assert_eq!(
            driven,
            [
                (INIT, now, Signal::Level(false)),
                (DONE, now, Signal::Level(false)),
                (INIT, now, Signal::Level(true)),
            ]
        );
        // a bitstream's header with a length count of 62 bits (fpga.rs),
        // in SPI mode 2, as fpga_program sends it (structures.md)
        let transfer = Transfer::new(0, 16, test_qspi_clock_in(2))
            .through(1)
            .selecting(6);
        let driven = driving(now, |drive| {
            for word in [0xff20, 0x0003, 0xefff, 0xffff] {
                fpga.transfer(transfer.shifting(word, 16), drive);
            }
        });
        assert_eq!(
            driven,
            [
                (DONE, now, Signal::Level(true)),
                (PPS, now, Signal::Level(false)),
            ]
        );
        let second = Time::from_secs(2);
        assert_eq!(fpga.next_event(test_moment(now)), Some(second));
        let driven = driving(second, |drive| fpga.advance(drive));
        assert_eq!(
            driven,
            [
                (PPS, second, Signal::Level(true)),
                (PPS, second + PPS_WIDTH, Signal::Level(false)),
            ]
        );
    }

    #[test]
    fn the_interpolator_carries_its_trims_to_the_fpga() {
        let mut interpolator = part(INTERPOLATOR);
        let driven = driving(Time::ZERO, |drive| {
            // the MC144110's outputs at codes 31 and 17, VDD 5 V
            interpolator.input(TRIM_LOW, Signal::Analog(2.421875), drive);
            interpolator.input(TRIM_HIGH, Signal::Analog(1.328125), drive);
            interpolator.input(TRIM_HIGH, Signal::Analog(1.328125), drive);
        });
        assert_eq!(
            driven,
            [
                (INTERPOLATOR_BYTE, Time::ZERO, Signal::Bus(31)),
                (INTERPOLATOR_BYTE, Time::ZERO, Signal::Bus(31 | 17 << 8)),
            ]
        );
    }

    /// 10 MHz in nanohertz, and the fraction of it a steered OCXO's
    /// frequency stays within: 1e-7, the 10811's EFC range and more.
    const NOMINAL_NANOHERTZ: u64 = IDEAL_HZ * 1_000_000_000;
    const STEERED_NANOHERTZ: u64 = NOMINAL_NANOHERTZ / 10_000_000;
    /// A day of the 10 MHz's ticks.
    const DAY_TICKS: u64 = IDEAL_HZ * 86_400;

    /// The net's history of a clock at `nanohertz` from power-on.
    fn history(nanohertz: u64) -> ClockHistory {
        let mut history = ClockHistory::new(P7_HISTORY);
        history.push(NetClock::new(
            Frequency::from_nanohertz(nanohertz),
            Time::ZERO,
        ));
        history
    }

    /// The edge `tick` of `p7` comes at the cycle `reaching` names, not
    /// a cycle before or after.
    fn reaches_exactly(p7: &P7<'_>, tick: Tick) {
        let at = p7.reaching(tick);
        assert!(p7.phase(at).tick >= tick, "{tick:?} not yet at {at:?}");
        assert!(
            p7.phase(Cycles(at.0 - 1)).tick < tick,
            "{tick:?} already at the cycle before {at:?}"
        );
    }

    /// The review's case: P7 593 parts in 1e10 slow, the internal 1 PPS
    /// due at tick 8641010124685, 10 days in.  The phase in f64 seconds
    /// put `reaching` there a cycle early, where the phase was
    /// 8641010124684.999: the pulse never came, and the board stalled.
    #[test]
    fn the_edge_a_slow_p7_names_has_come() {
        let history = history(9_999_999_593_859_586);
        let p7 = P7 {
            history: &history,
            base: test_base(),
        };
        reaches_exactly(&p7, Tick(8_641_010_124_685));
    }

    /// Over frequencies across 1e-7 either side of 10 MHz and edges over
    /// a month, every edge comes at the cycle `reaching` names.
    #[test]
    fn every_edge_comes_at_the_cycle_it_names() {
        const FREQUENCIES: u64 = 2_000;
        const EDGES: u64 = 16;
        // primes, so that the edges fall at every place in a cycle
        const FREQUENCY_STEP: u64 = 1_000_003;
        const EDGE_STEP: u64 = 162_007_919_311;
        for index in 0..FREQUENCIES {
            let nanohertz = NOMINAL_NANOHERTZ - STEERED_NANOHERTZ + index * FREQUENCY_STEP;
            let history = history(nanohertz);
            let p7 = P7 {
                history: &history,
                base: test_base(),
            };
            for edge in 0..EDGES {
                let tick = (index * EDGES + edge) * EDGE_STEP % (30 * DAY_TICKS);
                reaches_exactly(&p7, Tick(tick + 1));
            }
        }
    }

    /// The part programmed at `now`, its P7 at `nanohertz` from
    /// power-on: its design starts there.
    fn running_at(nanohertz: u64, now: Time) -> Box<dyn Peripheral> {
        let mut fpga = part(XC3042);
        let clock = NetClock::new(Frequency::from_nanohertz(nanohertz), Time::ZERO);
        let transfer = Transfer::new(0, 16, test_qspi_clock_in(2))
            .through(1)
            .selecting(6);
        driving(now, |drive| {
            fpga.input(P7_CLOCK, Signal::Clock(clock), drive);
            fpga.input(RESET, Signal::Level(false), drive);
            fpga.input(RESET, Signal::Level(true), drive);
            // the 62-bit header of the test above
            for word in [0xff20, 0x0003, 0xefff, 0xffff] {
                fpga.transfer(transfer.shifting(word, 16), drive);
            }
        });
        fpga
    }

    /// Steered and days in, the part's internal 1 PPS comes at the time
    /// its event names, and its next event is the next second's: the
    /// board's run never comes back to the same time.
    #[test]
    fn a_steered_part_pulses_at_its_events() {
        const FREQUENCIES: u64 = 40;
        const PULSES: usize = 4;
        const FREQUENCY_STEP: u64 = 50_000_017;
        const START_STEP: u64 = 86_399;
        for index in 0..FREQUENCIES {
            let nanohertz = NOMINAL_NANOHERTZ - STEERED_NANOHERTZ + index * FREQUENCY_STEP;
            let mut fpga = running_at(nanohertz, Time::from_secs(index * START_STEP));
            let mut last = None;
            for _ in 0..PULSES {
                let due = fpga
                    .next_event(test_moment(
                        last.unwrap_or(Time::from_secs(index * START_STEP)),
                    ))
                    .expect("a pulse due");
                assert!(last.is_none_or(|last| due > last), "{nanohertz} nHz");
                let driven = driving(due, |drive| fpga.advance(drive));
                assert_eq!(
                    driven.first(),
                    Some(&(PPS, due, Signal::Level(true))),
                    "{nanohertz} nHz"
                );
                last = Some(due);
            }
        }
    }

    /// Each board file names the design of the bitstream its images
    /// carry: a hand-modeled design's, `NAME.bit` of the repository's,
    /// or the bitstream an `lca` design asserts, is in every image the
    /// board takes, and `unmodeled` a board whose images carry none of
    /// those.  The hand models read no bitstream; this keeps the names
    /// honest, as the LCA model's configuration keeps its own.
    #[test]
    fn each_board_file_names_its_images_design() {
        let hand_modeled: Vec<&str> = Design::NAMES
            .iter()
            .filter(|&&(_, design)| {
                matches!(design, Design::Z3805a | Design::Hp58503a | Design::Z3817a)
            })
            .map(|&(name, _)| name)
            .collect();
        let mut names: Vec<&str> = hand_modeled
            .iter()
            .copied()
            .chain(Design::bitstreams())
            .collect();
        names.sort_unstable();
        names.dedup();
        let modeled: Vec<(&str, Vec<u8>)> = names
            .into_iter()
            .map(|name| {
                let bitstream =
                    std::fs::read(format!("{BITSTREAMS}/{name}.bit")).expect("the bitstream");
                (name, bitstream)
            })
            .collect();
        let carries = |image: &[u8], bitstream: &[u8]| {
            image
                .windows(bitstream.len())
                .any(|window| window == bitstream)
        };
        let mut boards: Vec<PathBuf> = std::fs::read_dir(BOARDS)
            .expect("the board files")
            .map(|entry| entry.expect("an entry").path())
            .filter(|path| {
                path.extension()
                    .is_some_and(|extension| extension == "toml")
            })
            .collect();
        boards.sort();
        assert!(!boards.is_empty(), "no board files");
        for path in boards {
            let board = path.display();
            let file: toml::Table = std::fs::read_to_string(&path)
                .expect("a board file")
                .parse()
                .expect("TOML");
            let parameters = &file["peripheral"]
                .as_array()
                .expect("peripherals")
                .iter()
                .find(|peripheral| peripheral["type"].as_str() == Some(XC3042))
                .unwrap_or_else(|| panic!("{board}: an FPGA"))["parameters"];
            let named = parameters["design"]
                .as_str()
                .unwrap_or_else(|| panic!("{board}: an FPGA with a design"));
            let design = Design::named(named).unwrap_or_else(|| panic!("{board}: {named}"));
            let asserted = parameters.get("bitstream").and_then(toml::Value::as_str);
            let images = file["board"]["images"].as_array().expect("its images");
            for image in images {
                let image = image.as_str().expect("a path");
                let bytes = std::fs::read(format!("{BOARDS}/{image}")).expect("the image");
                // an unmodeled board's images may carry a bitstream the
                // LCA model has a netlist for, but neither hand model's
                let carried: Vec<&str> = modeled
                    .iter()
                    .filter(|(name, _)| {
                        !matches!(design, Design::Unmodeled) || hand_modeled.contains(name)
                    })
                    .filter(|(_, bitstream)| carries(&bytes, bitstream))
                    .map(|&(name, _)| name)
                    .collect();
                let expected: Vec<&str> = match design {
                    Design::Unmodeled => Vec::new(),
                    Design::Z3805a | Design::Hp58503a | Design::Z3817a => vec![named],
                    // the configuration picks the netlist
                    Design::Lca { .. } => asserted.into_iter().collect(),
                };
                if matches!(design, Design::Lca { .. }) && asserted.is_none() {
                    continue;
                }
                assert_eq!(carried, expected, "{board}: {image}");
            }
        }
    }

    #[test]
    fn the_fpga_needs_its_design() {
        assert!(matches!(
            constructed(XC3042, ""),
            Err(ParameterError::Missing(key)) if key == "design"
        ));
        assert!(matches!(
            constructed(XC3042, "design = \"z3805a.bit\""),
            Err(ParameterError::Invalid { key, .. }) if key == "design"
        ));
        for parameters in [
            "design = \"z3805a\"\nbitstream = \"z3805a\"",
            "design = \"lca\"\nbitstream = \"59551a\"",
        ] {
            assert!(matches!(
                constructed(XC3042, parameters),
                Err(ParameterError::Invalid { key, .. }) if key == "bitstream"
            ));
        }
        assert!(constructed(XC3042, "design = \"lca\"\nbitstream = \"z3805a\"").is_ok());
    }

    /// The LCA model's configuration with a bitstream it does not know,
    /// the Z3805A's with a bit changed, stops the run, DONE Low; with the
    /// Z3805A's or the 58503A's it raises DONE.
    #[test]
    fn a_configuration_the_lca_model_refuses_stops_the_run() {
        /// The byte of the Z3805A's `.bit` changed, in its configuration.
        const ALTERED: usize = 100;
        let now = Time::from_secs(1);
        let transfer = Transfer::new(0, 16, test_qspi_clock_in(2))
            .through(1)
            .selecting(6);
        for (bitstream, altered, refused) in [
            ("z3805a", true, true),
            ("58503a-z3801a", false, false),
            ("z3805a", false, false),
        ] {
            let mut fpga = fpga("lca");
            let mut bytes =
                std::fs::read(format!("{BITSTREAMS}/{bitstream}.bit")).expect("the bitstream");
            if altered {
                bytes[ALTERED] ^= 0x01;
            }
            let mut drive = test_drive(now);
            fpga.input(RESET, Signal::Level(false), &mut drive);
            fpga.input(RESET, Signal::Level(true), &mut drive);
            for pair in bytes.chunks(2) {
                let word = u16::from_be_bytes([pair[0], pair[1]]);
                fpga.transfer(transfer.shifting(word, 16), &mut drive);
            }
            let driving = drive.finish();
            assert_eq!(driving.stop, refused, "{bitstream}");
            let done = driving
                .driven
                .iter()
                .any(|driven| driven.port == DONE && driven.signal == Signal::Level(true));
            assert_eq!(done, !refused, "{bitstream}");
        }
    }

    /// An unmodeled design configures and reads back, but does not run:
    /// no internal 1 PPS, and the port reads 0.
    #[test]
    fn an_unmodeled_design_configures_but_does_not_run() {
        let mut fpga = fpga("unmodeled");
        let now = Time::from_secs(1);
        let transfer = Transfer::new(0, 16, test_qspi_clock_in(2))
            .through(1)
            .selecting(6);
        let driven = driving(now, |drive| {
            fpga.input(RESET, Signal::Level(false), drive);
            fpga.input(RESET, Signal::Level(true), drive);
            for word in [0xff20, 0x0003, 0xefff, 0xffff] {
                fpga.transfer(transfer.shifting(word, 16), drive);
            }
        });
        assert!(driven.contains(&(DONE, now, Signal::Level(true))));
        assert_eq!(fpga.next_event(test_moment(now)), None);
        let later = Time::from_secs(3);
        assert_eq!(driving(later, |drive| fpga.advance(drive)), []);
        let access = PeripheralAccess::unselected(0, Size::Byte, FunctionCode::SupervisorData);
        let read = fpga.read(access, &mut test_drive(later));
        assert_eq!(read.expect("a byte"), 0);
    }
}
