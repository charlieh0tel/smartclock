//! An oscillator's report of the EFC it reads: the Z3817A's, on DUART
//! channel B (docs/firmware/z3817a-3752.md, "The board as the image
//! drives it"), and the Z3815A's puck's, the same report clocked a bit
//! at a time on port E (docs/firmware/z3815a-4010.md, "The oscillator
//! and references").

use mc683xx_board::Board;
use mc683xx_board::peripheral::Drive;
use mc683xx_board::peripheral::Peripheral;
use mc683xx_board::registry::Context;
use mc683xx_board::registry::ParameterError;
use mc683xx_board::registry::PortSpec;
use mc683xx_board::signal::Kind;
use mc683xx_board::signal::Serial;
use mc683xx_board::signal::Signal;
use mc683xx_board::time::Moment;
use mc683xx_board::time::Span as BoardSpan;
use mc683xx_board::time::Time;
use mc683xx_cpu32::Frequency;
use mc683xx_mc68331::Mc68331;
use mc683xx_modules::qsm::sci::Frame;

use super::EFC_VREF_NEGATIVE;
use super::EFC_VREF_POSITIVE;
use super::Span;
use super::invalid;
use super::ocxo::efc_word;
use super::span;
use super::warmth;
use crate::board::analog::Warmth;

/// A report's first byte (0x3374c frames from it).
const SYNC: u8 = 0x55;
/// A report's bytes: the first, the reading's three, the status and
/// the sum.
const REPORT_BYTES: usize = 6;
/// The firmware's EFC, 20 bits (`efc_clamp`, 0 to 1048560), a DAC
/// word's worth of it: the word is its top 16 bits.
const EFC_PER_WORD: f64 = 16.0;
/// The report's reading, 24 bits (0x3355e takes bytes 1 to 3).
const READING_MAX: f64 = 16_777_215.0;
/// The status byte's ready bit (`0x33542`): the oscillator warm.
const STATUS_READY: u8 = 0x01;
/// Its keys and defaults: the reading's counts a unit of the firmware's
/// EFC, the line's rate and a frame's bits, the reports' period, and
/// the seconds from power-up before the reports say ready.
const COUNTS_PER_EFC: &str = "counts_per_efc";
const BAUD: (&str, u64) = ("baud", 19_200);
const FRAME_BITS: (&str, u32) = ("frame_bits", 10);
const PERIOD: (&str, u64) = ("period_seconds", 1);
const READY: (&str, u64) = ("ready_seconds", 0);

/// The oscillator's report of the EFC it reads, whichever link carries
/// it: 0x55, the reading in 24 bits, most significant byte first, a
/// status byte, and the sum of the five bytes before it (`0x3374c`,
/// `0x3355e`).  The status byte's bit 0 is the oscillator's ready bit
/// (`0x33542`), which power-up's warm-up waits for (`0x31e16`): set in
/// every report once `ready_seconds` (0) of warm-up are behind it, its
/// warm-up's place at power-up from `warm_up_seconds` and
/// `warm_up_settled` (`warmth`), its other bits clear.  The reading is the firmware's 20-bit EFC the volts give
/// back, the DAC word (`efc_word`) times 16, times `counts_per_efc`,
/// which the firmware keeps as its a/d calibration (EEPROM `0x40008c`).
/// Its other parameters: `efc_vref_negative` and `efc_vref_positive`,
/// the EFC DAC's span (-5 V, +5 V).  What the status byte's other bits
/// carry is not read: the firmware tests bit 0 alone.
#[derive(Debug)]
struct Reporter {
    /// The EFC DAC's span.
    efc: Span,
    /// The reading's counts a unit of the firmware's EFC.
    counts_per_efc: f64,
    /// The seconds of warm-up before a report says ready.
    ready: u64,
    /// The warm-up at power-up.
    warmth: Warmth,
    /// The EFC's volts as last driven.
    volts: f64,
}

impl Reporter {
    /// From its parameters (`Reporter`).
    fn from_parameters(context: &Context<'_>) -> Result<Self, ParameterError> {
        let parameters = context.parameters;
        let counts_per_efc = parameters
            .float(COUNTS_PER_EFC)?
            .ok_or_else(|| ParameterError::Missing(COUNTS_PER_EFC.to_owned()))?;
        if !(counts_per_efc.is_finite() && counts_per_efc > 0.0) {
            return Err(invalid(COUNTS_PER_EFC, "not a positive number"));
        }
        Ok(Self {
            efc: span(context, EFC_VREF_NEGATIVE, EFC_VREF_POSITIVE)?,
            counts_per_efc,
            ready: parameters.integer(READY.0)?.unwrap_or(READY.1),
            warmth: warmth(context)?,
            volts: 0.0,
        })
    }

    /// Takes the EFC's volts.
    fn set_volts(&mut self, volts: f64) {
        self.volts = volts;
    }

    /// The report of the EFC now, `seconds` after power-up.
    fn report(&self, seconds: u64) -> [u8; REPORT_BYTES] {
        let efc = f64::from(efc_word(self.efc, self.volts)) * EFC_PER_WORD;
        // a whole count within the reading's 24 bits
        let reading = (efc * self.counts_per_efc).round().clamp(0.0, READING_MAX) as u32;
        let [_, high, middle, low] = reading.to_be_bytes();
        let ready = match self.warmth {
            Warmth::Settled => true,
            Warmth::Warming { head_start } => head_start + seconds as f64 >= self.ready as f64,
        };
        let status = if ready { STATUS_READY } else { 0 };
        let mut report = [SYNC, high, middle, low, status, 0];
        report[REPORT_BYTES - 1] = report[..REPORT_BYTES - 1]
            .iter()
            .fold(0, |sum: u8, byte| sum.wrapping_add(*byte));
        report
    }
}

/// `board`'s oscillator's report, the part `name`, either kind, ready
/// from now on, its warm-up settled: whether the board has the part.
pub(crate) fn settle(board: &mut Board<Mc68331>, name: &str) -> bool {
    if let Ok(mut part) = board.peripheral_mut::<EfcMonitorPart>(name) {
        part.reporter.warmth = Warmth::Settled;
        return true;
    }
    board
        .peripheral_mut::<ClockedEfcMonitorPart>(name)
        .map(|mut part| part.reporter.warmth = Warmth::Settled)
        .is_ok()
}

/// `efc-monitor`: the oscillator's report (`Reporter`) of `efc`, an
/// analog input in volts, sent on `tx`, a serial output, every
/// `period_seconds` (1) from power-up, at `baud` (19200) in frames of
/// `frame_bits` (10).
#[derive(Debug)]
pub(super) struct EfcMonitorPart {
    /// The report.
    reporter: Reporter,
    /// The line's rate, baud.
    baud: u64,
    /// A frame's bits, start and stop included.
    frame_bits: u32,
    /// The reports' period.
    period: u64,
    /// Power-up, from which the reports count.
    origin: Time,
    /// The next report, counted in periods from power-up.
    next: u64,
}

impl EfcMonitorPart {
    /// Its ports.
    const EFC: usize = 0;
    const TX: usize = 1;

    /// Its ports.
    pub(super) fn ports() -> Vec<PortSpec> {
        vec![
            PortSpec::input("efc", Kind::Analog),
            PortSpec::output("tx", Kind::Serial),
        ]
    }

    /// From its parameters (`EfcMonitorPart`).
    pub(super) fn from_parameters(context: &Context<'_>) -> Result<Self, ParameterError> {
        let reporter = Reporter::from_parameters(context)?;
        let parameters = context.parameters;
        let baud = parameters.integer(BAUD.0)?.unwrap_or(BAUD.1);
        let period = period(context)?;
        if baud == 0 {
            return Err(invalid(BAUD.0, "it must be at least 1"));
        }
        Ok(Self {
            reporter,
            baud,
            frame_bits: parameters.integer(FRAME_BITS.0)?.unwrap_or(FRAME_BITS.1),
            period,
            origin: Time::ZERO,
            next: 1,
        })
    }

    /// When report `n` goes out.
    fn report_at(&self, n: u64) -> Time {
        self.origin + BoardSpan::from_secs(n * self.period)
    }
}

/// The reports' period `context`'s parameters give, seconds, at least 1.
fn period(context: &Context<'_>) -> Result<u64, ParameterError> {
    let period = context.parameters.integer(PERIOD.0)?.unwrap_or(PERIOD.1);
    if period == 0 {
        return Err(invalid(PERIOD.0, "it must be at least 1"));
    }
    Ok(period)
}

impl Peripheral for EfcMonitorPart {
    fn power_on(&mut self, drive: &mut Drive<'_>) {
        self.origin = drive.now();
    }

    fn input(&mut self, port: usize, signal: Signal, _: &mut Drive<'_>) {
        if let (Self::EFC, Signal::Analog(volts)) = (port, signal) {
            self.reporter.set_volts(volts);
        }
    }

    fn next_event(&self, _: Moment) -> Option<Time> {
        Some(self.report_at(self.next))
    }

    fn advance(&mut self, drive: &mut Drive<'_>) {
        let now = drive.now();
        while self.report_at(self.next) <= now {
            let mut at = self.report_at(self.next);
            for byte in self.reporter.report(self.next * self.period) {
                let serial = Serial::new(
                    Frame::Data(u16::from(byte)),
                    Frequency::from_hz(self.baud),
                    self.frame_bits,
                );
                drive.drive_at(at, Self::TX, Signal::Serial(serial));
                at = serial.end(at);
            }
            self.next += 1;
        }
    }
}

/// A report's bits, each byte most significant bit first, as
/// `puck_link_clock` shifts them in (`0x3658e`).
const REPORT_BITS: usize = REPORT_BYTES * u8::BITS as usize;

/// `clocked-efc-monitor`: the oscillator's report (`Reporter`) of `efc`,
/// an analog input in volts, clocked out a bit at a time, as the
/// Z3815A's puck link takes it (`puck_link_clock`, `0x3658e`): every
/// `period_seconds` (1) from power-up it raises `framed`, a logic
/// output, and puts the report's first bit on `data`, another; each
/// rise of `clock`, a logic input, puts the next bit there, and the one
/// after the last lowers `framed` until the next period.  The firmware
/// samples `data` while `framed` is high, then pulses the clock Low and
/// High, a bit each pass of its health monitor; while `framed` is low it
/// gives no clock and starts its frame again.  A report not clocked out
/// within its period goes on into the next, and the next report waits
/// for the period after it ends.
#[derive(Debug)]
pub(super) struct ClockedEfcMonitorPart {
    /// The report.
    reporter: Reporter,
    /// The reports' period.
    period: u64,
    /// Power-up, from which the reports count.
    origin: Time,
    /// The next report, counted in periods from power-up.
    next: u64,
    /// The report going out and the bit on `data`, while `framed` is
    /// high.
    sending: Option<([u8; REPORT_BYTES], usize)>,
    /// The clock's level as last driven.
    clock: bool,
}

impl ClockedEfcMonitorPart {
    /// Its ports.
    const EFC: usize = 0;
    const CLOCK: usize = 1;
    const FRAMED: usize = 2;
    const DATA: usize = 3;

    /// Its ports.
    pub(super) fn ports() -> Vec<PortSpec> {
        vec![
            PortSpec::input("efc", Kind::Analog),
            PortSpec::input("clock", Kind::Logic),
            PortSpec::output("framed", Kind::Logic),
            PortSpec::output("data", Kind::Logic),
        ]
    }

    /// From its parameters (`ClockedEfcMonitorPart`).
    pub(super) fn from_parameters(context: &Context<'_>) -> Result<Self, ParameterError> {
        Ok(Self {
            reporter: Reporter::from_parameters(context)?,
            period: period(context)?,
            origin: Time::ZERO,
            next: 1,
            sending: None,
            clock: false,
        })
    }

    /// When report `n` goes out.
    fn report_at(&self, n: u64) -> Time {
        self.origin + BoardSpan::from_secs(n * self.period)
    }

    /// Bit `bit` of `report`, the first byte's most significant first.
    fn bit(report: &[u8; REPORT_BYTES], bit: usize) -> bool {
        let bits = u8::BITS as usize;
        report[bit / bits] >> (bits - 1 - bit % bits) & 1 != 0
    }
}

impl Peripheral for ClockedEfcMonitorPart {
    fn power_on(&mut self, drive: &mut Drive<'_>) {
        self.origin = drive.now();
        self.next = 1;
        self.sending = None;
        drive.drive(Self::FRAMED, Signal::Level(false));
        drive.drive(Self::DATA, Signal::Level(false));
    }

    fn input(&mut self, port: usize, signal: Signal, drive: &mut Drive<'_>) {
        match (port, signal) {
            (Self::EFC, Signal::Analog(volts)) => self.reporter.set_volts(volts),
            (Self::CLOCK, Signal::Level(high)) => {
                let rose = high && !self.clock;
                self.clock = high;
                if let (true, Some((report, bit))) = (rose, self.sending) {
                    let bit = bit + 1;
                    if bit < REPORT_BITS {
                        self.sending = Some((report, bit));
                        drive.drive(Self::DATA, Signal::Level(Self::bit(&report, bit)));
                    } else {
                        self.sending = None;
                        drive.drive(Self::FRAMED, Signal::Level(false));
                    }
                }
            }
            _ => {}
        }
    }

    /// The next period's start, also while a report goes out: the clock
    /// that ends it comes in a CPU's run, which the board does not stop
    /// for a part's next event changed by an input.
    fn next_event(&self, _: Moment) -> Option<Time> {
        Some(self.report_at(self.next))
    }

    fn advance(&mut self, drive: &mut Drive<'_>) {
        let now = drive.now();
        if self.report_at(self.next) > now {
            return;
        }
        if self.sending.is_none() {
            let report = self.reporter.report(self.next * self.period);
            self.sending = Some((report, 0));
            drive.drive(Self::DATA, Signal::Level(Self::bit(&report, 0)));
            drive.drive(Self::FRAMED, Signal::Level(true));
        }
        // the next report at the first period not yet begun
        while self.report_at(self.next) <= now {
            self.next += 1;
        }
    }
}
