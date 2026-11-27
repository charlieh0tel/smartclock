//! The synthetic GPS engine on the board's nets.

use std::cell::RefCell;
use std::collections::VecDeque;
use std::rc::Rc;

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
use mc683xx_board::time::Span;
use mc683xx_board::time::Time;
use mc683xx_cpu32::Frequency;
use mc683xx_mc68331::Mc68331;
use mc683xx_modules::qsm::sci::Frame;

use smartclock_gps::engine::Options;
use smartclock_gps::engine::saved::CONFIGURATION_58503A;
use smartclock_gps::engine::saved::SavedConfiguration;
use smartclock_gps::units::Seconds;

use super::invalid;
use crate::board::parameters::oncore::ANTENNA;
use crate::board::parameters::oncore::ENGINE;
use crate::board::parameters::oncore::HEIGHT;
use crate::board::parameters::oncore::LATITUDE;
use crate::board::parameters::oncore::LEAP_SECONDS;
use crate::board::parameters::oncore::LONGITUDE;
use crate::board::parameters::oncore::PROTOCOL;
use crate::board::parameters::oncore::START;
use crate::board::parameters::oncore::TAP;
use crate::board::system_clock::board_cycles;
use crate::board::system_clock::board_time;
use crate::gps::Config as GpsConfig;
use crate::gps::ENGINES;
use crate::gps::Engine;
use crate::gps::GPS_LESS_UTC;
use crate::gps::Port;
use crate::gps::Pulse;
use crate::gps::engine::EngineError;
use crate::gps::engine::Synthetic;
use crate::gps::range;
use crate::gps::range::RangeError;
use crate::gps::sky;
use crate::gps::sky::FIRMWARE_DEFAULT;
use crate::line::Line;
use crate::ports::GPS;
use crate::script::Injected;

/// The VP Oncore's 1 PPS: high for 200 ms from each second's start
/// (VP Oncore Engineering Notes, 6, "1 PPS Signal Definition").
pub(crate) const PPS_HIGH: Span = Span::from_millis(200);
/// The VP Oncore's line, 9600 baud in its binary format (VP Oncore
/// Engineering Notes: "At 9600 baud, the user can insert up to about
/// 1K of data per second"), ten bits a frame as the DUART's model
/// times a character (duart.rs, `FRAME_BITS`).  A GT-74's is the same,
/// 8N1 at 9600 baud, as the images set channel A up for it
/// (docs/firmware/z3805a-1.01.03.md, "The GPS engine").
const BAUD: u64 = 9600;
const FRAME_BITS: u32 = 10;
/// Its own parameters' keys, which the run does not give: the line's
/// rate and its frames' bits.
const BAUD_KEY: &str = "baud";
const FRAME_BITS_KEY: &str = "frame_bits";
/// Its keys for an engine other than the firmware's own: whether it
/// answers, and the configuration it powers up in.
const ANSWERS: &str = "answers";
const SAVED: &str = "saved";
/// The configurations it can power up in, by `saved`'s names: as a
/// 58503A leaves its engine.
const SAVED_CONFIGURATIONS: [(&str, SavedConfiguration); 1] = [("58503a", CONFIGURATION_58503A)];

/// The engine's port as its part's serial ports carry it: bytes
/// received from `rx`'s frames, bytes to send as `tx`'s.
#[derive(Debug, Default)]
struct Frames {
    /// Received, not yet read.
    received: VecDeque<u8>,
    /// Sent, not yet framed.
    sent: Vec<u8>,
}

impl Port for Frames {
    fn receive(&mut self, buffer: &mut [u8]) -> usize {
        let count = buffer.len().min(self.received.len());
        for (slot, byte) in buffer.iter_mut().zip(self.received.drain(..count)) {
            *slot = byte;
        }
        count
    }

    fn send(&mut self, bytes: &[u8]) {
        self.sent.extend_from_slice(bytes);
    }
}

/// `oncore` and `furuno-gt74`: the synthetic GPS engine, its serial
/// ports `rx` (from the DUART's channel A transmitter) and `tx` (to its
/// receiver), and its 1 PPS, `pps`, an edge output high 200 ms from each
/// second's start.  Its parameters: `start`, UTC at power-up (RFC 3339,
/// to the second), which the run gives; `engine`, the engine by its name
/// (`gps::ENGINES`): an Oncore, `b1121`, `b4121`, `b4121-no-time-raim`,
/// the B4121 with no Time RAIM, `I`, in its `@@Cj` options, or `r5122`,
/// the UT Plus, `b1121` unless given on an `oncore`; or a Furuno,
/// `gt-74`, `gt-74-without-time-raim` or `gt-74-pulse-early`, `gt-74`
/// unless given on a `furuno-gt74`; or `--gps-engine`'s; `protocol`, the
/// one the engine speaks, `motorola-binary` for an Oncore and `nmea`
/// for a Furuno, which the run gives with `--gps-engine`; `baud`
/// and `frame_bits`, the line's rate and a frame's bits, start and stop included, the VP's 9600 and 8N1 unless
/// given; `latitude`, `longitude` (degrees) and `height` (meters), the
/// firmware's default position unless given, within the Oncore's ranges
/// (`gps::range`); `leap_seconds` (18);
/// `tap` (false), as `--tap`; `antenna` (true), whether its antenna is
/// connected, which a power cycle carries; `answers` (true), whether it answers what the
/// firmware sends, which it hears either way; `saved` (none), the
/// configuration it powers up in, fixing and sending unasked, as a
/// receiver left it (`smartclock_gps::engine::saved`): `58503a`, as a
/// 58503A leaves its engine, which needs an eight-channel one.  What it sends goes out back to back at
/// the line's rate, each frame driven at its end.
pub(super) struct GpsPart {
    /// The engine.
    engine: Synthetic<Frames>,
    /// The pulses put out, as the engine's hook gives them.
    pulses: Rc<RefCell<Vec<Pulse>>>,
    /// What the firmware sent, as the engine's hook gives it, not yet
    /// taken (`take_heard`).
    heard: Rc<RefCell<Vec<u8>>>,
    /// The line's rate, baud.
    baud: u64,
    /// A frame's bits, start and stop included.
    frame_bits: u32,
    /// When the line is next free to start a frame.
    free: Time,
    /// Frames on `rx` not yet ended, the end and the byte: the engine
    /// reads each at its stop bit's end.
    arriving: VecDeque<(Time, u8)>,
    /// With `tap`, the firmware's end of the engine's line, its tap
    /// copying what `rx` carries, `gps < `, as Unicorn's line does.
    tapped: Option<Line>,
    /// What is to go to the firmware as though the engine had sent it,
    /// which the engine sends after what is due (`Synthetic::on_injected`).
    injected: Injected,
    /// When it was given, while it waits: the engine runs then.
    injected_at: Option<Time>,
}

impl std::fmt::Debug for GpsPart {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter
            .debug_struct("GpsPart")
            .field("engine", &self.engine)
            .field("baud", &self.baud)
            .field("free", &self.free)
            .finish_non_exhaustive()
    }
}

impl GpsPart {
    /// Its ports.
    const RX: usize = 0;
    const TX: usize = 1;
    const PPS: usize = 2;

    /// Its ports.
    pub(super) fn ports() -> Vec<PortSpec> {
        vec![
            PortSpec::input("rx", Kind::Serial),
            PortSpec::output("tx", Kind::Serial),
            PortSpec::output("pps", Kind::Edge),
        ]
    }

    /// From its parameters (`GpsPart`), its engine `default` unless
    /// they name one.
    pub(super) fn from_parameters(
        context: &Context<'_>,
        default: &'static Engine,
    ) -> Result<Self, ParameterError> {
        let parameters = context.parameters;
        let start = parameters
            .string(START)?
            .ok_or_else(|| ParameterError::Missing(START.to_owned()))?
            .parse::<jiff::Timestamp>()
            .map_err(RangeError::from)
            .and_then(range::start)
            .map_err(|error| invalid(START, error.to_string()))?;
        let name = parameters.string(ENGINE)?.unwrap_or(default.name);
        let engine = Engine::named(name).ok_or_else(|| {
            let names: Vec<&str> = ENGINES.iter().map(|engine| engine.name).collect();
            invalid(ENGINE, format!("the engine is one of {}", names.join(", ")))
        })?;
        let speaks = engine.family.protocol();
        if let Some(protocol) = parameters.string(PROTOCOL)?
            && protocol != speaks
        {
            return Err(invalid(
                PROTOCOL,
                format!("the engine {name} speaks {speaks}"),
            ));
        }
        let baud: u64 = parameters.integer(BAUD_KEY)?.unwrap_or(BAUD);
        let frame_bits: u32 = parameters.integer(FRAME_BITS_KEY)?.unwrap_or(FRAME_BITS);
        if baud == 0 {
            return Err(invalid(BAUD_KEY, "it must be at least 1".to_owned()));
        }
        let (latitude, longitude, height) = sky::degrees(FIRMWARE_DEFAULT);
        let position = sky::place(
            parameters.float(LATITUDE)?.unwrap_or(latitude),
            parameters.float(LONGITUDE)?.unwrap_or(longitude),
            parameters.float(HEIGHT)?.unwrap_or(height),
        )
        .map_err(|error| invalid(error.quantity(), error.to_string()))?;
        let mut options = Options::DEFAULT;
        options.deaf = !parameters.boolean(ANSWERS)?.unwrap_or(true);
        options.saved = parameters
            .string(SAVED)?
            .map(|name| {
                SAVED_CONFIGURATIONS
                    .into_iter()
                    .find_map(|(known, saved)| (known == name).then_some(saved))
                    .ok_or_else(|| {
                        let names: Vec<&str> =
                            SAVED_CONFIGURATIONS.iter().map(|(name, _)| *name).collect();
                        invalid(
                            SAVED,
                            format!("the configuration is one of {}", names.join(", ")),
                        )
                    })
            })
            .transpose()?;
        let config = GpsConfig {
            position,
            start,
            leap_seconds: Seconds(parameters.integer(LEAP_SECONDS)?.unwrap_or(GPS_LESS_UTC)),
            engine,
            options,
        };
        let tap = parameters.boolean(TAP)?.unwrap_or(false);
        let mut engine = Synthetic::new(Frames::default(), config, tap).map_err(|error| {
            let key = if matches!(error, EngineError::Saved(_)) {
                SAVED
            } else {
                START
            };
            invalid(key, error.to_string())
        })?;
        engine.set_antenna(parameters.boolean(ANTENNA)?.unwrap_or(true));
        let pulses = Rc::new(RefCell::new(Vec::new()));
        let begun = Rc::clone(&pulses);
        engine.on_pulse(Box::new(move |pulse| begun.borrow_mut().push(*pulse)));
        let heard = Rc::new(RefCell::new(Vec::new()));
        let read = Rc::clone(&heard);
        engine.on_heard(Box::new(move |bytes| {
            read.borrow_mut().extend_from_slice(bytes);
        }));
        let injected = Injected::default();
        engine.on_injected(Rc::clone(&injected));
        Ok(Self {
            engine,
            pulses,
            heard,
            baud,
            frame_bits,
            free: Time::ZERO,
            arriving: VecDeque::new(),
            tapped: tap.then(|| Line::new(GPS, true)),
            injected,
            injected_at: None,
        })
    }

    /// `bytes` to the firmware as though the engine had sent them, given
    /// at `now`: the engine runs then, and sends them after what is due.
    pub(super) fn inject(&mut self, bytes: &[u8], now: Time) {
        self.injected.borrow_mut().extend_from_slice(bytes);
        self.injected_at = Some(self.injected_at.map_or(now, |at| at.min(now)));
    }

    /// A frame of `byte` on the line at its rate.
    fn serial(&self, byte: u8) -> Serial {
        Serial::new(
            Frame::Data(u16::from(byte)),
            Frequency::from_hz(self.baud),
            self.frame_bits,
        )
    }

    /// The engine brought to `now`: the frames ended by then read, its
    /// 1 PPS each time it puts one out, and what it sent framed onto `tx`,
    /// each frame at its start bit, back to back.
    fn run(&mut self, now: Time, drive: &mut Drive<'_>) {
        self.injected_at = None;
        while let Some(&(end, byte)) = self.arriving.front()
            && end <= now
        {
            self.arriving.pop_front();
            self.engine.port().received.push_back(byte);
        }
        self.engine.advance(board_cycles(now));
        for pulse in self.pulses.borrow_mut().drain(..) {
            let at = Time::from_picos(pulse.picoseconds).max(now);
            drive.drive_at(at, Self::PPS, Signal::Level(true));
            drive.drive_at(at + PPS_HIGH, Self::PPS, Signal::Level(false));
        }
        for byte in std::mem::take(&mut self.engine.port().sent) {
            let serial = self.serial(byte);
            let start = self.free.max(now);
            drive.drive_at(start, Self::TX, Signal::Serial(serial));
            self.free = serial.end(start);
        }
    }
}

/// What the firmware sent `board`'s synthetic GPS engine, the part
/// `name`, since the last call: nothing if it has none.
pub(crate) fn take_heard(board: &mut Board<Mc68331>, name: &str) -> Vec<u8> {
    board
        .peripheral_mut::<GpsPart>(name)
        .ok()
        .map(|part| std::mem::take(&mut *part.heard.borrow_mut()))
        .unwrap_or_default()
}

/// `board`'s synthetic GPS engine's antenna, the part `name`,
/// connected or not: whether the board has the engine.
pub(crate) fn set_antenna(board: &mut Board<Mc68331>, name: &str, connected: bool) -> bool {
    board
        .peripheral_mut::<GpsPart>(name)
        .ok()
        .map(|mut part| part.engine.set_antenna(connected))
        .is_some()
}

/// Puts `bytes` on `board`'s synthetic GPS engine's line to the
/// firmware, the part `name`, now: the engine runs at the board's time,
/// and sends them after what is due, once what it has sent is out.
pub(crate) fn inject(board: &mut Board<Mc68331>, name: &str, bytes: &[u8]) {
    let now = board.now();
    if let Ok(mut part) = board.peripheral_mut::<GpsPart>(name) {
        part.inject(bytes, now);
    }
}

impl Peripheral for GpsPart {
    fn input(&mut self, port: usize, signal: Signal, drive: &mut Drive<'_>) {
        let now = drive.now();
        if let (Self::RX, Signal::Serial(serial)) = (port, signal)
            && let Frame::Data(data) = serial.frame
        {
            // eight data bits; a ninth or a parity bit is not the VP's
            let byte = data as u8;
            if let Some(line) = &self.tapped {
                line.transmit(byte);
            }
            self.arriving.push_back((serial.end(now), byte));
            self.run(now, drive);
        }
    }

    fn next_event(&self, _: Moment) -> Option<Time> {
        let due = board_time(self.engine.next_due());
        let arriving = self.arriving.front().map(|&(end, _)| end);
        // a pulse at its own picosecond, not its cycle's
        let pulse = self
            .engine
            .next_pulse()
            .map(|pulse| Time::from_picos(pulse.picoseconds));
        [due]
            .into_iter()
            .chain(pulse)
            .chain(arriving)
            .chain(self.injected_at)
            .min()
    }

    fn advance(&mut self, drive: &mut Drive<'_>) {
        let now = drive.now();
        self.run(now, drive);
    }
}
