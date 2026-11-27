//! The FPGA, a Xilinx XC3042 (the board's U32, an XC3042-70 PC84C:
//! docs/images/Z3805-68001/z3805a-cpu-fpga-flash.jpg): its configuration
//! lines on the MC68331's ports, its configuration and readback on the
//! QSPI's chip select 6, its register port at `0x304000` in chip select
//! 5's 64 KiB window, which it shares with the lamp latch (latch.rs), the
//! switch byte and the EFC's strobe, ADDR13 and ADDR14 picking the
//! device (docs/firmware/restart.md, "The chip selects"; outputs.md,
//! "The FPGA bitstreams"; interval.md, "The FPGA"), and the Z3805A's
//! design in it: the time-interval counter, the divider that makes the
//! internal 1 PPS from the 10 MHz clock on its P7, and the sync.
//!
//! The part's behavior is the XC3000 data sheet's
//! (`third_party/xilinx/xc3000.pdf`, Version 3.1, November 9, 1998;
//! pages cited as 7-n): port E's PROGRAM line is the part's RESET and
//! port F's DONE its DONE/PROG.
//!
//! Configuration, as `fpga_program` (fpga.c) drives it: a Low on both
//! DONE/PROG and RESET clears the configuration (Figure 20, 7-19), and
//! so does a configured part's DONE/PROG falling and held Low alone,
//! more than 6 us (7-20, 7-23; TPGW, 7-34), which the firmware never
//! does but in `fpga_program`, with RESET already Low; INIT
//! is low while RESET is, before configuration (7-38: RESET delays the
//! configuration's start; INIT is low while the memory clears); once it
//! is high the bitstream comes in over the QSPI.  The header's 24-bit
//! length count is the configuration clocks less K, 2 to 4, the
//! start-up's clocks after it, the two together a whole number of bytes
//! (Figure 21, 7-20), so DONE rises at the first multiple of eight at
//! least two past the length count: for the Z3805A's bitstream
//! `0x786c`, 30828, and 30832, the XC3042's 40-bit header and 285
//! frames of 108 bits and a postamble of four, 30,824, and a bit for
//! the one device, rounded up.  After configuration RESET resets the
//! design's storage elements and keeps the configuration (7-38), and
//! holds them reset while it is Low (7-8, 7-9): the design does not
//! run, take a write or see the GPS 1 PPS, and reads as its reset
//! leaves it; the counter and the 1 PPS divider start over at its
//! release, as `fpga_program`'s pulse once DONE is high leaves them.
//!
//! Readback, as `fpga_verify_chip` reads it: a rising edge on port F's
//! bit 6, M0/RTRIG, starts it (7-38), and each QSPI transfer shifts out
//! as many bits as the transfer is long, inverted (7-23: "a zero in
//! configuration becomes a one in Readback"), the first bit highest.
//! The part clocks three dummy bits out first, the third the first
//! frame's start bit, and then each frame with one start bit and one
//! stop bit where the configuration has three (7-23): 106 of each
//! frame's 108.  The firmware samples one bit ahead of the clock's
//! rising edges, so it sees the pin before the first edge, two dummy
//! bits, then the start bit: `fpga_verify_first` reads 11 bits and
//! compares the low 8 with the frame's first byte, start bit first,
//! then frames of 106 bits against 13- and 14-byte windows of the
//! bitstream 108 bits apart.  In the bit positions the configuration
//! does not use the part returns the design's state, its flip-flops,
//! latches and pads (7-23), which `fpga_readback_mask` leaves out; the
//! model, with no design running, returns the configuration there.
//!
//! The design the part runs once configured is `Design`'s, the one the
//! board file names, through `Logic` (logic.rs), the interface every
//! model of a design gives: the Z3805A's or the Z3817A's, as each's
//! decoded netlist reads (z3805a.rs, z3817a.rs, the two sharing
//! timebase.rs), hand models, the Z3805A's the default; or `lca`, the
//! bitstream the firmware sends executed, its netlist picked at DONE by
//! the configuration's hash (netlists.rs, executed.rs), exact and slower
//! (docs/firmware/fpga-codegen.md).  The interpolator beside the part
//! (interpolator.rs) measures each start the design makes.

mod executed;
#[cfg(test)]
mod fixture;
pub(super) mod interpolator;
mod lca;
mod logic;
mod netlists;
mod pins;
mod timebase;
mod z3805a;
mod z3817a;

use thiserror::Error;

use self::executed::Wired;
use self::interpolator::Interpolator;
use self::interpolator::Trims;
use self::lca::Pin;
use self::logic::Logic;
use self::logic::Model;
#[cfg(test)]
use self::z3805a::Z3805a;
use super::clock::Clock;
use super::clock::Phase;
use super::clock::Tick;
use super::parameters::fpga;
use super::time::CPU_HZ;
use super::time::Cycles;

/// Chip select 5's block, from `0x300000`.
pub(crate) const SIZE: u32 = 0x10000;

/// The register port, `fpga_port`, the FPGA's 8 KiB in the block, and
/// the address lines that reach it: ADDR0 to ADDR2.
pub(crate) const PORT: u32 = 0x4000;
const PORT_SIZE: u32 = 0x2000;
const PORT_ADDRESS_MASK: u32 = 0x07;

/// The least Low on DONE/PROG alone that re-programs a configured part,
/// TPGW (7-34): more than 6 us, in board cycles.
const MICROSECONDS_PER_SECOND: u64 = 1_000_000;
const REPROGRAM_MICROSECONDS: u64 = 6;
const REPROGRAM_LOW: Cycles =
    Cycles((CPU_HZ * REPROGRAM_MICROSECONDS).div_ceil(MICROSECONDS_PER_SECOND));

/// The bitstream's header (Figure 21, 7-20): eight ones, the preamble
/// `0010`, the 24-bit length count, then four ones before the first
/// frame.
const PREAMBLE: u32 = 0b0010;
const PREAMBLE_BITS: usize = 4;
const LENGTH_COUNT_BITS: usize = 24;
const HEADER_BITS: usize = 40;
/// The least of the start-up's K clocks after the length count, and
/// the byte the two together fill (Figure 21, 7-20).
const START_UP_LEAST: usize = 2;
const START_UP_ROUNDING: usize = 8;
/// An XC3042's frame in the configuration, a start bit, 104 data bits
/// and three stop bits, and in the readback, with one stop bit
/// (Figure 21, 7-20; 7-23); `fpga_verify_chip`'s 13- and 14-byte
/// frames, 108 bits apart, and `fpga_verify_odd`'s six words and ten
/// bits agree.
const FRAME_BITS: usize = 108;
const READBACK_FRAME_BITS: usize = 106;
/// The readback's bits before the first frame's start bit, as the
/// firmware samples them: the pin before the first clock edge and two
/// of the three dummy bits (7-23), which `fpga_verify_first` reads in
/// its 11 and does not compare.  What the dummy bits hold the data
/// sheet does not say; the model's read as zeros.
const READBACK_LEAD_BITS: usize = 3;

/// The design the part runs once configured, as the board file names it
/// (`Design::NAMES`).  The configuration it takes is the one the
/// firmware sends; only `Lca` runs the bitstream's logic.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub(crate) enum Design {
    /// The Z3805A's, as its decoded netlist reads (z3805a.rs): the
    /// Unicorn board's, whose three boards all run it.
    #[default]
    Z3805a,
    /// The 58503A's, which the Z3801A loads too, run as the Z3805A's.
    /// The two designs set the same I/O blocks on every pin but P14 and
    /// P45 (outputs.md, "The FPGA bitstreams"), and their netlists read
    /// alike at the port, the count, the divider and the sync; besides
    /// those two pins they differ in the internal 1 PPS's width
    /// (docs/firmware/58503a-z3801a-trace.md).  The model drives neither
    /// pin and gives the 1 PPS a width of its own (`fpga_part.rs`,
    /// `PPS_WIDTH`), so it is the Z3805A's.  Not checked against this
    /// design's own netlist.
    Hp58503a,
    /// The Z3817A's, as its decoded netlist reads (z3817a.rs,
    /// docs/firmware/z3817a-trace.md): the Z3805A's divider, tap and
    /// stop, no interpolator, a 32-bit count, and P82 or P84 as the
    /// start.
    Z3817a,
    /// The others, the 58503B's, the Z3815A's and the Z3816A's: not
    /// hand-modeled, `Lca` running their netlists.  Their bitstreams differ from the others (outputs.md,
    /// "The FPGA bitstreams"), and their netlists in the count's tap, the
    /// registers kept and, the Z3815A's and the Z3816A's, the divider and
    /// its clock (docs/firmware/fpga-comparison.md).  The part configures
    /// and reads back as any XC3042, but no design runs in it: the port
    /// reads 0, and there is no internal 1 PPS.
    Unmodeled,
    /// The netlist of the bitstream the firmware sends, executed: picked
    /// at DONE by the configuration's hash (netlists.rs), and refused
    /// when the model has none for it, or it is not the one `bitstream`
    /// names, which the board file may assert.
    Lca {
        /// The bitstream the board file asserts, if any.
        bitstream: Option<&'static str>,
    },
}

impl Design {
    /// Each design by the name board files give it: the modeled ones by
    /// their bitstream's, `third_party/firmware/images/fpga/NAME.bit`.
    pub(crate) const NAMES: [(&str, Self); 5] = [
        ("z3805a", Self::Z3805a),
        ("58503a-z3801a", Self::Hp58503a),
        ("z3817a", Self::Z3817a),
        ("unmodeled", Self::Unmodeled),
        (fpga::LCA, Self::Lca { bitstream: None }),
    ];

    /// The design board files call `name`, if any.
    pub(crate) fn named(name: &str) -> Option<Self> {
        Self::NAMES
            .iter()
            .find(|(known, _)| *known == name)
            .map(|&(_, design)| design)
    }

    /// The bitstreams a board file may assert with `lca`: those the
    /// model has a netlist for.
    pub(crate) fn bitstreams() -> impl Iterator<Item = &'static str> {
        netlists::named()
    }
}

/// What the LCA model made of a configuration, which the board reports.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) enum Configured {
    /// It runs the netlist of the bitstream named.
    Running(&'static str),
    /// It refused it.
    Refused(Refusal),
}

/// Why the LCA model refuses a configuration: DONE stays Low.
#[derive(Debug, Clone, PartialEq, Eq, Error)]
pub(crate) enum Refusal {
    #[error("the configuration, SHA-256 {0}, is none of the bitstreams the LCA model knows")]
    Unknown(String),
    #[error("the configuration is {0}.bit's, which the LCA model has no netlist for")]
    NoNetlist(&'static str),
    #[error(
        "the configuration is {found}.bit's, but the board file asserts bitstream = \"{asserted}\""
    )]
    Asserted {
        found: &'static str,
        asserted: &'static str,
    },
    #[error("the board does not wire {0}.bit's design's clocks and held inputs yet")]
    NoWiring(&'static str),
    #[error("{0}.bit's netlist bonds no I/O block to P{1}, which the board wires")]
    Unwired(&'static str, u8),
}

/// The configuration lines as port E and port F drive them.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub(crate) struct Lines {
    /// PROGRAM, port E's bit 6: the part's RESET, active low.
    pub(crate) program: bool,
    /// DONE/PROG driven low by port F.
    pub(crate) done_held_low: bool,
    /// The first chip's readback line, port F's bit 6: its M0/RTRIG.
    pub(crate) readback: bool,
}

/// The FPGA's state.
#[derive(Debug, Default)]
pub(crate) struct Fpga {
    /// The design it runs once configured.
    design: Design,
    /// The netlist the configuration picked, for `Design::Lca`, as
    /// the board wires it.
    netlist: Option<Wired>,
    /// What the LCA model made of the configuration, until the board
    /// takes it; one refused is not taken again until the part is
    /// cleared.
    configured_as: Option<Configured>,
    refused: bool,
    /// The lines as last driven.
    lines: Lines,
    /// The configuration's bits as they came, header first.
    bits: Vec<bool>,
    /// The header's length count, once it has come.
    length_count: Option<usize>,
    /// Configured: DONE high.
    configured: bool,
    /// When DONE/PROG fell, driven Low, on the configured part: held
    /// Low `REPROGRAM_LOW` from then, it clears the configuration.
    done_fell: Option<Cycles>,
    /// The next bit of the readback, once started.
    readback: Option<usize>,
    /// The design's logic, once configured and timed: its first use
    /// after the configuration, or its reset, starts it.
    logic: Option<Logic>,
    /// The last internal 1 PPS the design ran past outside `advance`, a
    /// GPS 1 PPS or an access at or after it, until `advance` reports
    /// it: else that pulse would never be driven.
    unreported: Option<Tick>,
    /// The interpolator beside it.
    interpolator: Interpolator,
    /// The hex DAC's trims as the last GPS 1 PPS or write found them:
    /// an interval a design gives only as time moves on, after the
    /// start, is measured under them.
    trims: Trims,
}

/// Whether `offset` in chip select 5's block is the register port's.
fn in_port(offset: u32) -> bool {
    (PORT..PORT + PORT_SIZE).contains(&offset)
}

/// The register `offset` in the port addresses, on ADDR0 to ADDR2.
fn register(offset: u32) -> u32 {
    (offset - PORT) & PORT_ADDRESS_MASK
}

impl Fpga {
    /// The part as power-up leaves it, `design` the one it runs once
    /// configured.
    pub(crate) fn new(design: Design) -> Self {
        Self {
            design,
            ..Self::default()
        }
    }

    /// Whether the design it runs once configured is a netlist's, the
    /// LCA model, whose reads sample their bus cycle.
    pub(crate) fn runs_netlist(&self) -> bool {
        matches!(self.design, Design::Lca { .. })
    }

    /// The configuration lines driven to `lines` at `now`, the FPGA's P7
    /// clocked by `clock`.
    pub(crate) fn set_lines(&mut self, lines: Lines, now: Cycles, clock: &dyn Clock) {
        self.reprogram_due(now);
        let before = std::mem::replace(&mut self.lines, lines);
        // a Low on both clears it (Figure 20); RESET before or during a
        // configuration starts it over (7-38)
        if lines.done_held_low && !lines.program || !lines.program && !self.configured {
            self.clear();
        } else if self.configured {
            if lines.done_held_low && !before.done_held_low {
                self.done_fell = Some(now);
            } else if !lines.done_held_low {
                self.done_fell = None;
            }
            if let Some(model) = self.model()
                && before.program != lines.program
            {
                // held reset while RESET is Low, the divider counting
                // from its release
                let phase = clock.phase(now);
                self.logic = Some(if lines.program {
                    Logic::reset(model, phase)
                } else {
                    Logic::held(model, phase)
                });
            }
        }
        if self.configured && lines.readback && !before.readback {
            self.readback = Some(0);
        }
    }

    /// When DONE/PROG, Low since it fell, re-programs the part.
    fn reprogram_at(&self) -> Option<Cycles> {
        self.done_fell
            .filter(|_| self.lines.done_held_low)
            .map(|fell| fell + REPROGRAM_LOW)
    }

    /// The configuration cleared at `now` if DONE/PROG alone has been
    /// Low long enough (7-20, 7-23).
    fn reprogram_due(&mut self, now: Cycles) {
        if self.reprogram_at().is_some_and(|at| at <= now) {
            self.clear();
        }
    }

    /// The configuration cleared, as a power-up leaves it.
    fn clear(&mut self) {
        *self = Self {
            design: self.design,
            lines: self.lines,
            ..Self::default()
        };
    }

    /// INIT: low while RESET is, high once the FPGA is ready for its
    /// configuration.  The clearing's own time, about 290 cycles of the
    /// part's clock for an XC3042 (Figure 20), is not modeled.
    pub(crate) fn init(&self) -> bool {
        self.lines.program
    }

    /// DONE: high once configured.
    pub(crate) fn done(&self) -> bool {
        self.configured
    }

    /// The model that runs the design, if any: for `Design::Lca`, once
    /// the configuration has picked its netlist.
    fn model(&self) -> Option<Model> {
        match self.design {
            Design::Z3805a | Design::Hp58503a => Some(Model::Z3805a),
            Design::Z3817a => Some(Model::Z3817a),
            Design::Unmodeled => None,
            Design::Lca { .. } => self.netlist.map(Model::Lca),
        }
    }

    /// Whether the design runs: configured, RESET High, and modeled.
    fn running(&self) -> bool {
        self.configured && self.lines.program && self.model().is_some()
    }

    /// What the LCA model made of the configuration, once: the board
    /// reports it.
    pub(crate) fn take_configured(&mut self) -> Option<Configured> {
        self.configured_as.take()
    }

    /// A word of `bits` bits on the QSPI: configuration while
    /// unconfigured, readback once configured.  It shifts back `reply`.
    pub(crate) fn exchange(&mut self, word: u16, bits: u8) -> u16 {
        let back = self.reply(bits);
        let width = usize::from(bits);
        if !self.configured {
            if self.lines.program {
                self.configure(word, width);
            }
        } else if let Some(start) = self.readback {
            self.readback = Some(start + width);
        }
        back
    }

    /// What the next word of `bits` bits shifts back: the readback's
    /// next bits once it is configured and reading back, else 0.
    pub(crate) fn reply(&self, bits: u8) -> u16 {
        let width = usize::from(bits);
        let Some(start) = self.readback.filter(|_| self.configured) else {
            return 0;
        };
        let read = (start..start + width).fold(0u16, |read, at| {
            read << 1 | u16::from(self.readback_bit(at))
        });
        // the part shifts its readback out inverted
        !read & (u16::MAX >> (u16::BITS as usize - width))
    }

    /// Configuration bits, the first highest; DONE rises once the
    /// start-up's clocks after the length count have come.
    fn configure(&mut self, word: u16, width: usize) {
        self.bits
            .extend((0..width).rev().map(|bit| word >> bit & 1 != 0));
        if self.length_count.is_none() {
            self.length_count = length_count(&self.bits);
        }
        if let Some(length) = self.length_count
            && !self.refused
        {
            let done = (length + START_UP_LEAST).next_multiple_of(START_UP_ROUNDING);
            if self.bits.len() >= done {
                match self.pick(done) {
                    Ok(picked) => {
                        if let Some((name, wired)) = picked {
                            self.netlist = Some(wired);
                            self.configured_as = Some(Configured::Running(name));
                        }
                        self.configured = true;
                    }
                    Err(refusal) => {
                        self.configured_as = Some(Configured::Refused(refusal));
                        self.refused = true;
                    }
                }
            }
        }
    }

    /// For `Design::Lca`, the bitstream and its netlist the
    /// configuration's bits to `done` pick, as the board wires it; for
    /// the others, none.
    fn pick(&self, done: usize) -> Result<Option<(&'static str, Wired)>, Refusal> {
        let Design::Lca { bitstream } = self.design else {
            return Ok(None);
        };
        let known = netlists::configured(&self.bits[..done]).map_err(Refusal::Unknown)?;
        if let Some(asserted) = bitstream
            && asserted != known.name
        {
            return Err(Refusal::Asserted {
                found: known.name,
                asserted,
            });
        }
        let netlist = known.netlist.ok_or(Refusal::NoNetlist(known.name))?;
        let wiring = known.wiring.ok_or(Refusal::NoWiring(known.name))?;
        let wired = Wired { netlist, wiring };
        match wired.unwired() {
            Some(Pin(pin)) => Err(Refusal::Unwired(known.name, pin)),
            None => Ok(Some((known.name, wired))),
        }
    }

    /// The readback stream's bit `at`.
    fn readback_bit(&self, at: usize) -> bool {
        let Some(offset) = at.checked_sub(READBACK_LEAD_BITS) else {
            return true;
        };
        let frame = offset / READBACK_FRAME_BITS;
        let bit = HEADER_BITS + frame * FRAME_BITS + offset % READBACK_FRAME_BITS;
        self.bits.get(bit).copied().unwrap_or(true)
    }

    /// The design moved on to `now` on `clock`'s phase, once
    /// configured, the phase there, and the last internal 1 PPS on the
    /// way; started, if this is its first use since the configuration.
    fn logic_at(
        &mut self,
        now: Cycles,
        clock: &dyn Clock,
    ) -> Option<(&mut Logic, Phase, Option<Tick>)> {
        self.reprogram_due(now);
        let model = self.model().filter(|_| self.running())?;
        let phase = clock.phase(now);
        let logic = self.logic.get_or_insert_with(|| Logic::reset(model, phase));
        let ran = logic.run_to(phase);
        if ran.pulse.is_some() {
            self.unreported = ran.pulse;
        }
        if let Some(interval) = ran.interval {
            self.interpolator.measure(interval, self.trims);
        }
        Some((logic, phase, ran.pulse))
    }

    /// Time moves on to `now`, the FPGA's P7 clocked by `clock`.  Returns
    /// the last internal 1 PPS not yet reported, if any.
    pub(crate) fn advance(&mut self, now: Cycles, clock: &dyn Clock) -> Option<Cycles> {
        self.logic_at(now, clock);
        self.unreported.take().map(|pulse| clock.reaching(pulse))
    }

    /// When the internal 1 PPS next pulses, or the tap a count waits for
    /// comes, or the unreported pulse, now past.
    pub(crate) fn next_event(&self, clock: &dyn Clock) -> Option<Cycles> {
        // a held design waits for RESET's release
        let logic_event = self
            .logic
            .as_ref()
            .filter(|_| self.running())
            .and_then(Logic::next_event)
            .map(|tick| clock.reaching(tick));
        let unreported = self.unreported.map(|pulse| clock.reaching(pulse));
        [logic_event, unreported, self.reprogram_at()]
            .into_iter()
            .flatten()
            .min()
    }

    /// The GPS engine's 1 PPS rising at `at`, on P82, with the hex DAC's
    /// trims as they stand.
    pub(crate) fn gps_pps(&mut self, at: Cycles, clock: &dyn Clock, trims: Trims) {
        self.trims = trims;
        let Some((logic, at, _)) = self.logic_at(at, clock) else {
            return;
        };
        if let Some(interval) = logic.gps_pps(at) {
            self.interpolator.measure(interval, trims);
        }
    }

    /// A 1 PPS rising at `at` on P84, which only the Z3817A's design
    /// takes.
    pub(crate) fn p84(&mut self, at: Cycles, clock: &dyn Clock) {
        if let Some((logic, at, _)) = self.logic_at(at, clock) {
            logic.p84(at);
        }
    }

    /// The GPS engine's 1 PPS falling at `at`.
    pub(crate) fn gps_pps_fall(&mut self, at: Cycles, clock: &dyn Clock) {
        if let Some((logic, at, _)) = self.logic_at(at, clock) {
            logic.gps_pps_fall(at);
        }
    }

    /// A byte of chip select 5's block at `now`, nothing reaching the
    /// part before the cycle `horizon` if the board knows one (`Logic::read`).
    pub(crate) fn read(
        &mut self,
        offset: u32,
        now: Cycles,
        clock: &dyn Clock,
        horizon: Option<Cycles>,
    ) -> u8 {
        if in_port(offset) {
            self.port(register(offset), now, clock, None, horizon)
        } else {
            0
        }
    }

    /// The port's read of `register` at `now` (counter.md, "What the
    /// netlist shows"), with `pins` on P3 to P11, or else the
    /// interpolator's byte as the design run to `now` leaves it, nothing
    /// reaching the part before the cycle `horizon` if given.
    fn port(
        &mut self,
        register: u32,
        now: Cycles,
        clock: &dyn Clock,
        pins: Option<u8>,
        horizon: Option<Cycles>,
    ) -> u8 {
        // the design run first: the interpolator measures as it runs
        let running = self.logic_at(now, clock).map(|(_, phase, _)| phase);
        let pins = pins.unwrap_or(self.interpolator.byte);
        if let (Some(phase), Some(logic)) = (running, self.logic.as_mut()) {
            let horizon = horizon.map(|horizon| clock.phase(horizon));
            return logic.read(register, phase, pins, horizon);
        }
        match self.model().filter(|_| self.configured) {
            // held in reset, as its reset leaves it
            Some(model) => {
                let phase = clock.phase(now);
                Logic::held(model, phase).read(register, phase, pins, None)
            }
            None => 0,
        }
    }

    /// The port's read as the hand-written model gives it, its count off
    /// by `error` ticks: the differential test's tolerance of the
    /// netlist's edge timing (z3805a.rs).
    #[cfg(test)]
    fn port_counting(&mut self, now: Cycles, clock: &dyn Clock, pins: u8, error: i32) -> u8 {
        let running = self
            .logic_at(now, clock)
            .and_then(|(logic, now, _)| Some((*logic.z3805a()?, now)));
        let (design, now) = match running {
            Some(running) => running,
            // held in reset, as its reset leaves it
            None if self.configured && self.model().is_some() => {
                let phase = clock.phase(now);
                (Z3805a::held(phase), phase)
            }
            None => return 0,
        };
        design.read(now, pins, error)
    }

    /// A byte written to chip select 5's block at `now`, with the hex
    /// DAC's trims as they stand.
    pub(crate) fn write(
        &mut self,
        offset: u32,
        value: u8,
        now: Cycles,
        clock: &dyn Clock,
        trims: Trims,
    ) {
        if !in_port(offset) {
            return;
        }
        self.trims = trims;
        let Some((logic, now, _)) = self.logic_at(now, clock) else {
            return;
        };
        if let Some(interval) = logic.write(register(offset), value, now) {
            self.interpolator.measure(interval, trims);
        }
    }
}

/// The length count of a bitstream's header, once its bits have come:
/// after the leading ones, the preamble, then 24 bits.
fn length_count(bits: &[bool]) -> Option<usize> {
    let ones = bits.iter().take_while(|bit| **bit).count();
    let preamble_end = ones + PREAMBLE_BITS;
    let field = bits.get(ones..preamble_end + LENGTH_COUNT_BITS)?;
    let value = |bits: &[bool]| {
        bits.iter()
            .fold(0usize, |value, bit| value << 1 | usize::from(*bit))
    };
    (value(&field[..PREAMBLE_BITS]) == PREAMBLE as usize).then(|| value(&field[PREAMBLE_BITS..]))
}

#[cfg(test)]
mod tests {
    use super::super::clock::Clock;
    use super::super::clock::Ideal;
    use super::super::clock::Tick;
    use super::super::time::CPU_HZ;
    use super::super::time::Cycles;
    use super::Configured;
    use super::Design;
    use super::Fpga;
    use super::Interpolator;
    use super::Lines;
    use super::Refusal;
    use super::Trims;
    use super::timebase::DIVIDE;

    /// The Z3805A 3543B image's first bitstream and readback mask
    /// (outputs.md, "The FPGA bitstreams").
    const IMAGE: &[u8] = include_bytes!("../../../../third_party/firmware/images/z3805a-3543b.bin");
    const BITSTREAM: std::ops::Range<usize> = 0x20f18..0x21e2c;
    const MASK: usize = 0x21e3b;

    /// The lines as the firmware leaves them running: PROGRAM high,
    /// DONE let go, the readback line high.
    pub(super) const RUNNING: Lines = Lines {
        program: true,
        done_held_low: false,
        readback: true,
    };
    /// RESET, PROGRAM, Low after the configuration.
    pub(super) const HELD: Lines = Lines {
        program: false,
        ..RUNNING
    };

    /// `fpga_program`'s sequence: DONE held low and PROGRAM low, then
    /// PROGRAM high, DONE let go, the bitstream, then PROGRAM pulsed at
    /// `now` on `clock`.
    pub(super) fn program(fpga: &mut Fpga, bitstream: &[u8], now: Cycles, clock: &dyn Clock) {
        send(fpga, bitstream, now, clock);
        pulse(fpga, now, clock);
    }

    /// `fpga_program`'s sequence to the bitstream's end, at `now` on
    /// `clock`.
    fn send(fpga: &mut Fpga, bitstream: &[u8], now: Cycles, clock: &dyn Clock) {
        fpga.set_lines(
            Lines {
                done_held_low: true,
                ..HELD
            },
            now,
            clock,
        );
        assert!(!fpga.init());
        fpga.set_lines(
            Lines {
                done_held_low: true,
                ..RUNNING
            },
            now,
            clock,
        );
        assert!(fpga.init());
        fpga.set_lines(RUNNING, now, clock);
        assert!(!fpga.done(), "not early");
        for pair in bitstream.chunks(2) {
            let word = u16::from_be_bytes([pair[0], *pair.get(1).unwrap_or(&0)]);
            fpga.exchange(word, 16);
        }
    }

    /// PROGRAM pulsed at `now` on `clock`, as `fpga_program` ends.
    fn pulse(fpga: &mut Fpga, now: Cycles, clock: &dyn Clock) {
        fpga.set_lines(HELD, now, clock);
        fpga.set_lines(RUNNING, now, clock);
    }

    /// The part configured with the Z3805A's bitstream, its design
    /// reset.
    pub(super) fn configured() -> Fpga {
        let mut fpga = Fpga::default();
        program(&mut fpga, &IMAGE[BITSTREAM], Cycles(0), &CLOCK);
        fpga
    }

    /// The 58503A's and the Z3801A's bitstream.
    const HP58503A_BITSTREAM: &[u8] =
        include_bytes!("../../../../third_party/firmware/images/fpga/58503a-z3801a.bit");
    /// The Z3816A's and the Z3815A's; and the Z3817A's, a netlist the
    /// board does not wire yet.
    const Z3816A_BITSTREAM: &[u8] =
        include_bytes!("../../../../third_party/firmware/images/fpga/z3816a.bit");
    const Z3815A_BITSTREAM: &[u8] =
        include_bytes!("../../../../third_party/firmware/images/fpga/z3815a.bit");
    const Z3817A_BITSTREAM: &[u8] =
        include_bytes!("../../../../third_party/firmware/images/fpga/z3817a.bit");

    /// The LCA model asserting `bitstream`, configured with `sent`, and
    /// why it refused it, if it did, as the board takes that before
    /// `fpga_program` pulses PROGRAM.
    fn executing(bitstream: Option<&'static str>, sent: &[u8]) -> (Fpga, Option<Refusal>) {
        let mut fpga = Fpga::new(Design::Lca { bitstream });
        send(&mut fpga, sent, Cycles(0), &CLOCK);
        let refusal = match fpga.take_configured() {
            Some(Configured::Refused(refusal)) => Some(refusal),
            Some(Configured::Running(name)) => {
                assert!(bitstream.is_none_or(|asserted| asserted == name), "{name}");
                None
            }
            None => panic!("nothing configured"),
        };
        assert_eq!(fpga.take_configured(), None, "once");
        pulse(&mut fpga, Cycles(0), &CLOCK);
        (fpga, refusal)
    }

    /// The board's 10 MHz in the tests.
    pub(super) const CLOCK: Ideal = Ideal;

    /// The board cycle at which the ideal clock's phase reaches `tick`.
    pub(super) fn at_tick(tick: u64) -> Cycles {
        CLOCK.reaching(Tick(tick))
    }

    /// The register port's `register` written `value` at `now`.
    pub(super) fn write(fpga: &mut Fpga, register: u32, value: u8, now: Cycles) {
        fpga.write(0x4000 + register, value, now, &CLOCK, Trims::default());
    }

    /// A register `register` of the port read at `now`, as `fpga_read`
    /// addresses it.
    pub(super) fn read_register(fpga: &mut Fpga, register: u8, now: Cycles) -> u8 {
        write(fpga, 1, register << 3 | 0x02, now);
        fpga.read(0x4000, now, &CLOCK, None) & 0x0f
    }

    /// A few microseconds after `now`, past a write's strobe.
    pub(super) fn after(now: Cycles) -> Cycles {
        now + Cycles(CPU_HZ / 100_000)
    }

    #[test]
    fn done_rises_at_the_length_count_and_not_for_noise() {
        let fpga = configured();
        assert!(fpga.done());
        assert_eq!(fpga.length_count, Some(0x786c));
        let mut noise = Fpga::default();
        program(&mut noise, &[0; 4096], Cycles(0), &CLOCK);
        assert!(!noise.done());
    }

    #[test]
    fn program_pulsed_after_the_configuration_keeps_it() {
        let mut fpga = configured();
        fpga.set_lines(HELD, Cycles(0), &CLOCK);
        assert!(fpga.done());
        fpga.set_lines(RUNNING, Cycles(0), &CLOCK);
        assert!(fpga.done());
        fpga.set_lines(
            Lines {
                program: false,
                done_held_low: true,
                ..RUNNING
            },
            Cycles(0),
            &CLOCK,
        );
        assert!(!fpga.done(), "a Low on both clears it");
    }

    /// 7-20 and 7-23: a configured part re-programs on DONE/PROG falling
    /// and held Low more than 6 us (TPGW, 7-34), RESET High.
    #[test]
    fn done_prog_held_low_alone_clears_the_configuration() {
        const DONE_LOW: Lines = Lines {
            done_held_low: true,
            ..RUNNING
        };
        let mut fpga = configured();
        let fell = Cycles(1000);
        fpga.set_lines(DONE_LOW, fell, &CLOCK);
        assert!(fpga.done(), "not at once");
        let short = fell + super::REPROGRAM_LOW - Cycles(1);
        fpga.set_lines(RUNNING, short, &CLOCK);
        assert!(fpga.done(), "too short a Low");
        assert_eq!(fpga.next_event(&CLOCK), Some(at_tick(DIVIDE)));
        // released when long enough
        let fell = Cycles(2000);
        fpga.set_lines(DONE_LOW, fell, &CLOCK);
        fpga.set_lines(RUNNING, fell + super::REPROGRAM_LOW, &CLOCK);
        assert!(!fpga.done(), "released after a long enough Low");
        assert!(fpga.init(), "ready for the next configuration");
        // still held: the time it is due is an event, and moving on to
        // it clears the part
        let mut fpga = configured();
        let fell = Cycles(1000);
        fpga.set_lines(DONE_LOW, fell, &CLOCK);
        let due = fell + super::REPROGRAM_LOW;
        assert_eq!(fpga.next_event(&CLOCK), Some(due));
        assert_eq!(fpga.advance(due, &CLOCK), None);
        assert!(!fpga.done(), "held long enough");
        // and the firmware's configuration still takes it
        program(&mut fpga, &IMAGE[BITSTREAM], Cycles(0), &CLOCK);
        assert!(fpga.done());
    }

    /// 7-8 and 7-9: RESET Low holds the design's storage reset: no
    /// 1 PPS, write or GPS start until its release, from which the
    /// divider counts.
    #[test]
    fn reset_held_low_holds_the_design() {
        let mut fpga = configured();
        let held = at_tick(1000);
        fpga.set_lines(HELD, held, &CLOCK);
        assert!(fpga.done(), "the configuration kept");
        assert_eq!(fpga.next_event(&CLOCK), None, "nothing while held");
        let late = at_tick(5 * DIVIDE / 2);
        assert_eq!(fpga.advance(late, &CLOCK), None, "no 1 PPS while held");
        // a write and a GPS 1 PPS while held change nothing
        write(&mut fpga, 0, 0x00, late);
        fpga.gps_pps(late, &CLOCK, Trims::default());
        // bits 2 and 3 the interpolator's byte, 0, complemented
        assert_eq!(
            fpga.read(0x4000, late, &CLOCK, None),
            0x0c,
            "no seconds, no start"
        );
        let released = at_tick(3 * DIVIDE);
        fpga.set_lines(RUNNING, released, &CLOCK);
        let first = at_tick(4 * DIVIDE);
        assert_eq!(
            fpga.next_event(&CLOCK),
            Some(first),
            "counting from the release"
        );
        assert_eq!(fpga.advance(first, &CLOCK), Some(first));
    }

    /// Figure 21's XC3042: the start-up's clocks after the length count
    /// end on a byte, 30832 bits for the Z3805A's 30828.
    #[test]
    fn done_rises_after_the_start_ups_clocks() {
        let mut fpga = Fpga::default();
        let bitstream = &IMAGE[BITSTREAM];
        // short of the length count, 30816 bits, then the 30832
        program(&mut fpga, &bitstream[..30816 / 8], Cycles(0), &CLOCK);
        assert!(!fpga.done());
        let mut fpga = Fpga::default();
        program(&mut fpga, &bitstream[..30832 / 8], Cycles(0), &CLOCK);
        assert!(fpga.done());
    }

    /// `fpga_verify_chip`'s compare (fpga.c): the readback against the
    /// bitstream from past its header, under `fpga_readback_mask`.
    struct Verify<'a> {
        fpga: &'a mut Fpga,
        configuration: &'a [u8],
        mask: &'a [u8],
        /// The next byte of each.
        at: usize,
        /// The last transfer's bits, as received, inverted.
        receive: u16,
    }

    impl Verify<'_> {
        fn read(&mut self, bits: u8) {
            self.receive = self.fpga.exchange(0xffff, bits);
        }

        fn high(&self) -> u8 {
            (!self.receive >> 8) as u8
        }

        fn low(&self) -> u8 {
            !self.receive as u8
        }

        fn compare(&self, read: u8, bits: u8) {
            assert_eq!(
                self.mask[self.at] & (self.configuration[self.at] ^ read) & bits,
                0,
                "byte {}",
                self.at
            );
        }

        fn compare_next(&mut self, read: u8, bits: u8) {
            self.compare(read, bits);
            self.at += 1;
        }

        fn words(&mut self, count: usize) {
            for _ in 0..count {
                self.read(16);
                self.compare_next(self.high(), 0xff);
                self.compare_next(self.low(), 0xff);
            }
        }

        /// `fpga_readback_last`: 9 to 15 bits, left-justified, past the
        /// first byte only.
        fn last(&mut self, bits: u8) {
            self.read(bits);
            self.receive <<= 16 - bits;
            self.compare_next(self.high(), 0xff);
            self.compare(self.low(), 0xff << (16 - bits));
        }
    }

    /// The readback started again, as `fpga_verify_chip` starts it: M0
    /// Low, then rising.
    fn restart_readback(fpga: &mut Fpga) {
        fpga.set_lines(
            Lines {
                readback: false,
                ..RUNNING
            },
            Cycles(0),
            &CLOCK,
        );
        fpga.set_lines(RUNNING, Cycles(0), &CLOCK);
    }

    #[test]
    fn the_readback_passes_fpga_verify_chips_compare() {
        let mut fpga = configured();
        restart_readback(&mut fpga);
        let mut verify = Verify {
            fpga: &mut fpga,
            configuration: &IMAGE[BITSTREAM.start + 5..],
            mask: &IMAGE[MASK..],
            at: 0,
            receive: 0,
        };
        // fpga_verify_first: 11 bits, five words, 8 bits and 10
        verify.read(11);
        verify.compare_next(verify.low(), 0xff);
        verify.words(5);
        verify.read(8);
        verify.compare_next(verify.low(), 0xff);
        verify.last(10);
        let mut frame_start = 13;
        for frame in 2..=285 {
            verify.at = frame_start;
            if frame % 2 == 0 {
                // fpga_verify_even: 12 bits, five words and 14 bits
                verify.read(12);
                verify.compare_next(verify.high(), 0x0f);
                verify.compare_next(verify.low(), 0xff);
                verify.words(5);
                verify.last(14);
                frame_start += 14;
            } else {
                // fpga_verify_odd: six words and 10 bits
                verify.words(6);
                verify.last(10);
                frame_start += 13;
            }
        }
    }

    #[test]
    fn a_wrong_configuration_fails_the_compare() {
        let mut bitstream = IMAGE[BITSTREAM].to_vec();
        // a data bit of the first frame the mask compares
        bitstream[5 + 1] ^= 0x10;
        let mut fpga = Fpga::default();
        program(&mut fpga, &bitstream, Cycles(0), &CLOCK);
        restart_readback(&mut fpga);
        let configuration = &IMAGE[BITSTREAM.start + 5..];
        let mask = IMAGE[MASK + 1];
        // the first frame's first two bytes: 11 bits, then a word
        fpga.exchange(0xffff, 11);
        let [high, _] = (!fpga.exchange(0xffff, 16)).to_be_bytes();
        assert_ne!(mask & (configuration[1] ^ high), 0);
    }

    /// The LCA model's configuration picks its own netlist, the
    /// Z3805A's, the 58503A's, the Z3816A's or the Z3815A's, or is refused, DONE Low, once, saying
    /// why: one it does not know, one the board file does not assert,
    /// or one the board does not wire.
    #[test]
    fn the_configuration_picks_the_netlist_or_is_refused() {
        let (fpga, refusal) = executing(Some("z3805a"), &IMAGE[BITSTREAM]);
        assert!(fpga.done());
        assert_eq!(refusal, None);
        assert!(fpga.next_event(&CLOCK).is_some(), "the netlist runs");
        let (fpga, refusal) = executing(Some("58503a-z3801a"), HP58503A_BITSTREAM);
        assert!(fpga.done());
        assert_eq!(refusal, None);
        assert!(fpga.next_event(&CLOCK).is_some(), "the netlist runs");
        let (_, refusal) = executing(Some("z3805a"), HP58503A_BITSTREAM);
        assert_eq!(
            refusal,
            Some(Refusal::Asserted {
                found: "58503a-z3801a",
                asserted: "z3805a"
            })
        );
        let (fpga, refusal) = executing(Some("z3816a"), Z3816A_BITSTREAM);
        assert!(fpga.done());
        assert_eq!(refusal, None);
        assert!(fpga.next_event(&CLOCK).is_some(), "the netlist runs");
        let (fpga, refusal) = executing(Some("z3815a"), Z3815A_BITSTREAM);
        assert!(fpga.done());
        assert_eq!(refusal, None);
        assert!(fpga.next_event(&CLOCK).is_some(), "the netlist runs");
        let (fpga, refusal) = executing(None, Z3817A_BITSTREAM);
        assert_eq!(refusal, Some(Refusal::NoWiring("z3817a")));
        assert!(!fpga.done());
        let mut altered = IMAGE[BITSTREAM].to_vec();
        altered[100] ^= 0x01;
        let (fpga, refusal) = executing(None, &altered);
        assert!(matches!(refusal, Some(Refusal::Unknown(_))));
        assert!(!fpga.done());
    }

    /// The netlist and the hand model read alike where both are exact:
    /// the preset's count and the status, and `interpolator_measure`'s
    /// mode 3 start at its low reference, its count of six edges, a few
    /// microseconds on; the interpolator's byte, from the netlist's
    /// interval to the picosecond rather than the hand model's to the
    /// tick, within what the netlist's nominal block delays make of it.
    #[test]
    fn the_netlist_reads_as_the_hand_model_does() {
        const COUNT: [u8; 6] = [0x12, 0x13, 0x14, 0x15, 0x16, 0x17];
        const INTERPOLATOR: [u8; 2] = [0x10, 0x11];
        let mut hand = configured();
        let (mut lca, _) = executing(None, &IMAGE[BITSTREAM]);
        let trims = Trims::new(31, 17);
        let mut now = at_tick(1000);
        let mut both = [&mut hand, &mut lca];
        for writes in [
            &[(0, 0x40)][..],
            &[(0, 0x43), (1, 0x01), (0, 0x03), (1, 0x00)],
        ] {
            for fpga in &mut both {
                for (register, value) in writes {
                    fpga.write(0x4000 + register, *value, now, &CLOCK, trims);
                }
            }
            now = after(now);
            for register in COUNT.into_iter().chain([0x00]) {
                let reads = both
                    .each_mut()
                    .map(|fpga| read_register(fpga, register, now));
                // the status's bits, not the interpolator's
                let mask = if register == 0x00 { 0x03 } else { 0x0f };
                assert_eq!(
                    reads[0] & mask,
                    reads[1] & mask,
                    "{register:#04x} after {writes:x?}"
                );
            }
        }
        let [hand, lca] = both.map(|fpga| {
            INTERPOLATOR.iter().rev().fold(0, |byte, register| {
                byte << 4 | read_register(fpga, *register, now)
            })
        });
        // the low reference, a tick, and three of the netlist's delays,
        // a nanosecond each (z3805a.rs's differential test)
        const TICK_SECONDS: f64 = 1e-7;
        const DELAYS_SECONDS: f64 = 3e-9;
        let reading = |seconds: f64| {
            let mut interpolator = Interpolator::default();
            interpolator.measure(seconds, trims);
            interpolator.byte
        };
        let tolerance = reading(TICK_SECONDS).abs_diff(reading(TICK_SECONDS + DELAYS_SECONDS));
        assert!(
            hand.abs_diff(lca) <= tolerance,
            "the interpolator: {hand:#04x}, {lca:#04x}"
        );
    }

    /// The netlist's first internal 1 PPS where the hand model's comes,
    /// a second of the 10 MHz after the reset, fast-forwarded, so only
    /// when asked, optimized.  It prints the netlist's time on its own,
    /// with no CPU, against real time.
    /// The reset is between two edges, as the fixture's is: on one, the
    /// netlist counts that edge, a race the hand model does not have.
    #[test]
    #[ignore = "a second of the board on each model, a measure: optimized"]
    fn the_netlists_1_pps_comes_as_the_hand_models_does() {
        let reset = at_tick(DIVIDE / 2) + Cycles(1);
        let [mut hand, mut lca] = [configured(), executing(None, &IMAGE[BITSTREAM]).0];
        for fpga in [&mut hand, &mut lca] {
            fpga.set_lines(HELD, reset, &CLOCK);
            fpga.set_lines(RUNNING, reset, &CLOCK);
        }
        let started = std::time::Instant::now();
        let mut pulses = Vec::new();
        while pulses.is_empty() {
            let next = lca.next_event(&CLOCK).expect("an event");
            pulses.extend(lca.advance(next, &CLOCK));
        }
        let elapsed = started.elapsed().as_secs_f64();
        let simulated = (pulses[0].0 - reset.0) as f64 / CPU_HZ as f64;
        println!(
            "the netlist alone: {simulated:.3} s simulated in {elapsed:.4} s, {:.2}% of real time",
            100.0 * elapsed / simulated
        );
        assert_eq!(pulses, [hand.next_event(&CLOCK).expect("a pulse")]);
    }

    #[test]
    fn the_port_reads_zero_unconfigured() {
        let mut fpga = Fpga::default();
        assert_eq!(fpga.read(0x4000, Cycles(0), &CLOCK, None), 0);
    }
}
