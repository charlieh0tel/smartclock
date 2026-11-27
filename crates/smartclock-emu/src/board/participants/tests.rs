//! The parts against their models on the Unicorn board, and the
//! registry's entries.

use std::any::Any;
use std::cell::RefCell;

use mc683xx_board::Environment;
use mc683xx_board::peripheral::Drive;
use mc683xx_board::peripheral::Driven;
use mc683xx_board::peripheral::Peripheral;
use mc683xx_board::peripheral::Sampler;
use mc683xx_board::peripheral::Transfer;
use mc683xx_board::registry::Constructed;
use mc683xx_board::registry::Context;
use mc683xx_board::registry::ParameterError;
use mc683xx_board::registry::Parameters;
use mc683xx_board::registry::PortSpec;
use mc683xx_board::registry::Registry;
use mc683xx_board::registry::RegistryError;
use mc683xx_board::signal::Direction;
use mc683xx_board::signal::Kind;
use mc683xx_board::signal::Serial;
use mc683xx_board::signal::Signal;
use mc683xx_board::time::Clock as NetClock;
use mc683xx_board::time::Phase;
use mc683xx_board::time::Span as BoardSpan;
use mc683xx_board::time::Time;
use mc683xx_cpu32::Frequency;
use mc683xx_modules::qsm::sci::Frame;
use smartclock_gps::oncore::AZ_CABLE_DELAY;
use smartclock_gps::oncore::OVERHEAD;
use smartclock_gps::oncore::encode;

use super::super::adc::Adc;
use super::super::adc::Wired;
use super::super::analog::Warmth;
use super::super::analog::Wiring;
use super::super::snapshot;
use super::ADC_SPAN;
use super::CLOCKED_EFC_MONITOR;
use super::EFC_CODES;
use super::EFC_MONITOR;
use super::EFC_SHIFT;
use super::EFC_VREF_NEGATIVE;
use super::EFC_VREF_POSITIVE;
use super::FRONT_PANEL;
use super::GPS;
use super::OCXO;
use super::SUPPLIES;
use super::Span;
use super::TEMPERATURE;
use super::TIME_REFERENCES;
use super::adc_volts;
use super::gps::GpsPart;
use super::ocxo::OcxoPart;
use super::ocxo::efc_word;
use super::register;
use crate::board::system_clock::test_base;
use crate::board::system_clock::test_drive;
use crate::board::system_clock::test_moment;
use crate::board::system_clock::test_qspi_clock;
use crate::board::system_clock::test_qspi_clock_in;
use crate::board::time::Cycles;

/// The seed of every test's run.
const SEED: u64 = 7;
/// Every test part's name.
const NAME: &str = "part";

/// The stock ADC's type (mc683xx-peripherals).
const ADC: &str = "adc0838";

/// This module's types, and mc683xx-peripherals' stock chips.
fn registry() -> Registry {
    let mut registry = Registry::with_builtins();
    register(&mut registry).expect("the names are new");
    mc683xx_peripherals::register(&mut registry).expect("the stock names are new");
    registry
}

/// The run's choices.
fn environment() -> Environment {
    let mut environment = Environment::default();
    environment.seed = SEED;
    environment
}

/// A part of type `kind` from the parameters `table`, TOML.
fn construct(kind: &str, table: &str) -> Result<Constructed, ParameterError> {
    let parameters = Parameters::parse(table).expect("a table");
    registry()
        .construct(kind, &Context::new(NAME, &parameters, &environment()))
        .map_err(|error| {
            error
                .into_parameter()
                .unwrap_or_else(|error| panic!("{error}"))
        })
}

/// A part of type `kind` from `table`, which it takes.
fn part(kind: &str, table: &str) -> Box<dyn Peripheral> {
    construct(kind, table).expect("constructed").peripheral
}

/// What a call at `now` drove.
fn driving(now: Time, call: impl FnOnce(&mut Drive<'_>)) -> Vec<Driven> {
    let mut drive = test_drive(now);
    call(&mut drive);
    drive.finish().driven
}

/// A transfer on the QSPI, PCS[3:0] at `chip_selects`.
fn transfer(chip_selects: u8) -> Transfer {
    entry(0, chip_selects)
}

/// Queue entry `entry`'s transfer, PCS[3:0] at `chip_selects`.
fn entry(entry: u8, chip_selects: u8) -> Transfer {
    Transfer::new(0, 16, test_qspi_clock())
        .at_entry(entry)
        .selecting(chip_selects)
}

/// `transfer` in SPI mode 3, CPOL 1 and CPHA 1, as the firmware sets
/// SPCR0 for the ADC (structures.md, "The QSPI's devices", `QSPI_ADC`)
/// and the 58503A's front panel (58503a-3704.md, "The front panel").
fn mode_3(transfer: Transfer) -> Transfer {
    transfer.clocked(test_qspi_clock_in(3))
}

/// The models' cycle at `now`.
fn cycles(now: Time) -> Cycles {
    Cycles(test_base().cycle(now).clocks())
}

/// A frame of `byte` at the VP's 9600 baud, 8N1.
fn frame(byte: u8) -> Signal {
    Signal::Serial(Serial::new(
        Frame::Data(u16::from(byte)),
        Frequency::from_hz(9600),
        10,
    ))
}

/// `seconds` of board time.
fn seconds(seconds: u64) -> Time {
    Time::from_secs(seconds)
}

/// A port's name, kind and direction.
fn port(port: &PortSpec) -> (&str, Kind, Direction) {
    (port.name.as_str(), port.kind, port.direction)
}

#[test]
fn each_type_declares_its_ports() {
    let registry = registry();
    let ports = |kind: &str| {
        registry
            .ports(kind)
            .expect("fixed")
            .iter()
            .map(|spec| (spec.name.clone(), spec.kind, spec.direction))
            .collect::<Vec<_>>()
    };
    let named = |list: &[(&str, Kind, Direction)]| {
        list.iter()
            .map(|(name, kind, direction)| ((*name).to_owned(), *kind, *direction))
            .collect::<Vec<_>>()
    };
    let panel = ports(FRONT_PANEL);
    assert_eq!(
        panel[..2],
        named(&[
            ("run", Kind::Logic, Direction::In),
            ("frame", Kind::Logic, Direction::In)
        ])
    );
    assert_eq!(panel.len(), 10);
    assert_eq!(
        panel[7],
        ("key-status".to_owned(), Kind::Logic, Direction::In)
    );
    assert_eq!(
        ports(GPS),
        named(&[
            ("rx", Kind::Serial, Direction::In),
            ("tx", Kind::Serial, Direction::Out),
            ("pps", Kind::Edge, Direction::Out)
        ])
    );
    assert_eq!(
        ports(OCXO),
        named(&[
            ("efc", Kind::Analog, Direction::In),
            ("out", Kind::Clock, Direction::Out),
            ("outer_oven", Kind::Logic, Direction::In),
            ("oven_current", Kind::Analog, Direction::Out),
            ("oven_monitor", Kind::Analog, Direction::Out),
            ("outer_oven_monitor", Kind::Analog, Direction::Out)
        ])
    );
    assert_eq!(
        ports(TEMPERATURE),
        named(&[("out", Kind::Analog, Direction::Out)])
    );
    assert_eq!(
        ports(SUPPLIES),
        named(&[
            ("plus_15v", Kind::Analog, Direction::Out),
            ("plus_5v", Kind::Analog, Direction::Out),
            ("minus_15v", Kind::Analog, Direction::Out),
            ("plus_12v_b", Kind::Analog, Direction::Out),
            ("minus_12v_b", Kind::Analog, Direction::Out)
        ])
    );
    let constructed = construct(OCXO, "").expect("constructed");
    assert_eq!(
        port(&constructed.ports[0]),
        ("efc", Kind::Analog, Direction::In)
    );
}

#[test]
fn registering_twice_is_refused() {
    let mut registry = registry();
    assert_eq!(
        register(&mut registry),
        Err(RegistryError::Duplicate(FRONT_PANEL.to_owned()))
    );
}

/// The firmware's channel numbers on the Z3805A, the ADC0838's address
/// bits 13 to 11 (`adc_read`), as the stock part takes them: ODD, then
/// SELECT 1 and SELECT 0, the pin 2 x SELECT + ODD (SNAS531B, Table
/// 2), as the board files wire the pins.
fn pin(channel: u16) -> usize {
    usize::from(2 * (channel & 0b11) + (channel >> 2))
}

/// The stock ADC's reading of the firmware's `channel` at `now`, its
/// inputs from `sampler`: a conversion a frame, the select its /CS, the
/// reading in bits 9 to 2, as `adc_read` takes it.
fn converted(adc: &mut dyn Peripheral, channel: u16, now: Time, sampler: Sampler<'_>) -> u16 {
    let mut drive = Drive::with_sampler(test_moment(now), sampler);
    adc.set_selected(0, true, &mut drive);
    let command = 0xc000 | channel << 11;
    let reply = adc.transfer(mode_3(transfer(1)).shifting(command, 16), &mut drive);
    adc.set_selected(0, false, &mut drive);
    reply >> 2 & 0xff
}

/// The stock ADC on the sources' parts, its pins wired as the board
/// files wire them, reads each of the firmware's channels as the
/// Unicorn board's ADC does, but for its noise, a count at most.
#[test]
fn the_stock_adc_on_the_sources_reads_as_the_unicorn_boards() {
    let mut adc = part(ADC, "");
    let sources = RefCell::new([part(TEMPERATURE, ""), part(OCXO, ""), part(SUPPLIES, "")]);
    // each of the firmware's channels' source and its output port
    // (analog.rs, `Wiring`; boards/z3805a.toml)
    let wiring: [Option<(usize, usize)>; 8] = [
        Some((0, 0)),
        Some((1, 5)),
        Some((2, 0)),
        Some((1, 3)),
        Some((2, 1)),
        Some((1, 4)),
        Some((2, 2)),
        None,
    ];
    let sampler = |port: usize, now: Time| {
        let channel = (0..8).find(|&channel| pin(channel) == port)?;
        let (source, output) = wiring[usize::from(channel)]?;
        sources.borrow_mut()[source].analog_output(output, test_moment(now))
    };
    let mut unicorn = Wired {
        adc: Adc::new(environment().seed_for(NAME)),
        inputs: Wiring::default(),
    };
    for second in [0, 1, 299, 600, 3600] {
        for channel in 0..8 {
            let now = seconds(second);
            let stock = converted(adc.as_mut(), channel, now, &sampler);
            let unicorn = unicorn.exchange(0xc000 | channel << 11, cycles(now)) >> 2 & 0xff;
            assert!(
                stock.abs_diff(unicorn) <= 1,
                "channel {channel} at {second} s: {stock:#04x}, {unicorn:#04x}"
            );
        }
    }
}

/// Each count's voltage, on a channel of the stock ADC, converts back
/// to the count: exactly, as the stock part's default has no noise.
#[test]
fn a_count_is_the_voltage_the_stock_adc_converts_to_it() {
    let mut adc = part(ADC, "");
    let channel = 0;
    for count in 0..=u8::MAX {
        let volts = adc_volts(ADC_SPAN, f64::from(count));
        let sampler = |port: usize, _: Time| (port == pin(channel)).then_some(volts);
        let reading = converted(adc.as_mut(), channel, seconds(0), &sampler);
        assert_eq!(reading, u16::from(count));
    }
}

/// The EFC DAC's span unless the parameters give another.
const EFC_SPAN: Span = Span {
    low: EFC_VREF_NEGATIVE.1,
    high: EFC_VREF_POSITIVE.1,
};

#[test]
fn the_efc_dacs_output_is_taken_back_to_its_word() {
    let (negative, positive) = (EFC_VREF_NEGATIVE.1, EFC_VREF_POSITIVE.1);
    for word in 0..=u16::MAX {
        // the AD569's VOUT for the word (Analog Circuit Connections)
        let volts = negative + f64::from(word) * (positive - negative) / EFC_CODES;
        assert_eq!(efc_word(EFC_SPAN, volts), word);
    }
    assert_eq!(efc_word(EFC_SPAN, -12.0), 0, "below the span");
    assert_eq!(efc_word(EFC_SPAN, 12.0), u16::MAX, "above it");
}

/// A source's span is the board file's, not a fixed 5 V: a reference of
/// half gives each level at half the voltage.
#[test]
fn a_source_gives_its_levels_over_the_board_files_reference() {
    let mut stock = part(SUPPLIES, "");
    let mut halved = part(SUPPLIES, "adc_vref_volts = 2.5");
    let now = seconds(0);
    for port in 0..3 {
        let (full, half) = (
            stock.analog_output(port, test_moment(now)),
            halved.analog_output(port, test_moment(now)),
        );
        assert_eq!(full.zip(half).map(|(full, half)| full / half), Some(2.0));
    }
    let error = construct(SUPPLIES, "adc_vref_volts = 0.0").expect_err("refused");
    assert!(matches!(error, ParameterError::Invalid { .. }), "{error}");
}

/// A front-panel command through the part's ports, framed as `0x53736`
/// and `0x53796` frame it: the reply to its first byte.
fn panel_command(panel: &mut dyn Peripheral, bytes: &[u8], now: Time) -> u16 {
    panel_command_clocked(panel, bytes, now, mode_3)
}

/// `panel_command` with each transfer clocked by `clocked`.
fn panel_command_clocked(
    panel: &mut dyn Peripheral,
    bytes: &[u8],
    now: Time,
    clocked: fn(Transfer) -> Transfer,
) -> u16 {
    let mut drive = test_drive(now);
    panel.input(1, Signal::Level(false), &mut drive);
    let replies: Vec<_> = bytes
        .iter()
        .map(|byte| {
            panel.set_selected(0, true, &mut drive);
            let reply = panel.transfer(
                clocked(transfer(0)).shifting(u16::from(*byte), 8),
                &mut drive,
            );
            panel.set_selected(0, false, &mut drive);
            reply
        })
        .collect();
    panel.input(1, Signal::Level(true), &mut drive);
    replies[0]
}

#[test]
fn the_front_panel_passes_its_self_test_and_gives_keys() {
    let mut panel = part(FRONT_PANEL, "");
    let now = Time::ZERO;
    let mut drive = test_drive(now);
    panel.input(0, Signal::Level(false), &mut drive);
    panel.input(0, Signal::Level(true), &mut drive);
    for byte in [0x00, 0x08] {
        panel_command(&mut *panel, &[byte], now);
    }
    assert_eq!(panel_command(&mut *panel, &[0x10], now), 0x55);
    assert_eq!(panel_command(&mut *panel, &[0x00], now), 0xaa);
    // the status key, sixth, its input the eighth port
    panel.input(7, Signal::Level(false), &mut drive);
    panel.input(7, Signal::Level(true), &mut drive);
    panel_command(&mut *panel, &[0x00], now);
    assert_eq!(panel_command(&mut *panel, &[0x00], now), 6);
    assert_eq!(panel_command(&mut *panel, &[0x00], now), 0, "once");
}

/// In SPI mode 1, SPCR0's reset, the panel samples each bit as the
/// master changes it, the bit before, and its reply comes a bit late
/// behind an undriven 1: the self-test fails.
#[test]
fn the_front_panel_is_clocked_in_mode_3() {
    let mut panel = part(FRONT_PANEL, "");
    let now = Time::ZERO;
    let in_reset_mode = |transfer| transfer;
    let replies: Vec<u16> = [0x00, 0x08, 0x10, 0x00]
        .into_iter()
        .map(|byte| panel_command_clocked(&mut *panel, &[byte], now, in_reset_mode))
        .collect();
    // 0x08 and 0x10 taken as 0x04 and 0x08
    assert_eq!(replies[2..], [0x80, 0x80 | 0x55 >> 1]);
}

/// A frame closed while the panel is selected, its byte not yet come,
/// ends when the byte comes.
#[test]
fn the_front_panel_takes_a_byte_in_flight_at_its_frames_close() {
    let mut panel = part(FRONT_PANEL, "");
    let now = Time::ZERO;
    let mut drive = test_drive(now);
    panel.input(1, Signal::Level(false), &mut drive);
    for byte in [0x04, b'H'] {
        panel.transfer(mode_3(transfer(0)).shifting(u16::from(byte), 8), &mut drive);
    }
    panel.set_selected(0, true, &mut drive);
    panel.input(1, Signal::Level(true), &mut drive);
    panel.transfer(mode_3(transfer(0)).shifting(u16::from(b'P'), 8), &mut drive);
    panel.set_selected(0, false, &mut drive);
    assert_eq!(
        panel.transfer(mode_3(transfer(0)).shifting(0x08, 8), &mut drive),
        0,
        "unframed after it"
    );
}

/// The front panel restores what it saved.
#[test]
fn the_front_panel_restores_what_it_saved() {
    let now = Time::ZERO;
    let mut panel = part(FRONT_PANEL, "");
    for byte in [0x00, 0x08] {
        panel_command(&mut *panel, &[byte], now);
    }
    let mut copy = part(FRONT_PANEL, "");
    copy.restore(&panel.save().expect("saved"))
        .expect("restored");
    assert_eq!(
        panel_command(&mut *copy, &[0x10], now),
        0x55,
        "its self-test"
    );
}

#[test]
fn the_front_panel_reads_report() {
    assert!(construct(FRONT_PANEL, "report = true").is_ok());
    assert!(matches!(
        construct(FRONT_PANEL, "report = 1"),
        Err(ParameterError::Type { .. })
    ));
}

/// The engine's parameters, as a board file gives them.
const ONCORE: &str = "start = \"2026-10-23T00:00:00Z\"\nbaud = 9600\nframe_bits = 10";

#[test]
fn the_engine_needs_its_start_and_its_lines_rate() {
    assert_eq!(
        construct(GPS, "baud = 9600\nframe_bits = 10").map(|_| ()),
        Err(ParameterError::Missing("start".to_owned()))
    );
    assert!(
        construct(GPS, "start = \"2026-10-23T00:00:00Z\"").is_ok(),
        "the VP's line by default"
    );
    assert!(matches!(
        construct(
            GPS,
            &format!("{ONCORE}\nprotocol = \"nmea\"\nengine = \"b4121\"")
        ),
        Err(ParameterError::Invalid { .. })
    ));
    assert!(construct(GPS, &format!("{ONCORE}\nengine = \"b4121\"")).is_ok());
    assert!(matches!(
        construct(GPS, &format!("{ONCORE}\nengine = \"m12\"")),
        Err(ParameterError::Invalid { key, .. }) if key == "engine"
    ));
    assert!(matches!(
        construct(GPS, "start = \"yesterday\"\nbaud = 9600\nframe_bits = 10"),
        Err(ParameterError::Invalid { .. })
    ));
    assert!(construct(GPS, &format!("{ONCORE}\nlatitude = 37.5\nheight = 55")).is_ok());
}

#[test]
fn the_engine_refuses_a_place_or_a_start_the_oncore_cannot_give() {
    let invalid = |extra: &str| match construct(GPS, &format!("{ONCORE}\n{extra}")) {
        Err(ParameterError::Invalid { key, .. }) => key,
        other => panic!("{extra}: {:?}", other.map(|_| ())),
    };
    assert_eq!(invalid("latitude = 90.5"), "latitude");
    assert_eq!(invalid("longitude = -181.0"), "longitude");
    assert_eq!(invalid("height = 18001.0"), "height");
    let start = "baud = 9600\nframe_bits = 10\nstart = \"9999-01-01T00:00:00Z\"";
    assert!(matches!(
        construct(GPS, start),
        Err(ParameterError::Invalid { key, .. }) if key == "start"
    ));
}

#[test]
fn the_engine_drives_its_1_pps_at_each_second() {
    let mut engine = part(GPS, ONCORE);
    assert_eq!(engine.next_event(test_moment(Time::ZERO)), Some(seconds(1)));
    let driven = driving(seconds(1), |drive| engine.advance(drive));
    let pps: Vec<_> = driven
        .iter()
        .filter(|driven| driven.port == 2)
        .map(|driven| (driven.at, driven.signal))
        .collect();
    assert_eq!(
        pps,
        [
            (seconds(1), Signal::Level(true)),
            (
                seconds(1) + BoardSpan::from_millis(200),
                Signal::Level(false)
            )
        ]
    );
    assert_eq!(engine.next_event(test_moment(seconds(1))), Some(seconds(2)));
}

/// The 1 PPS is the emulator's to make, and so moves as the engine's
/// cable delay says: `@@Az` of 250,000 ns puts the next pulse that many
/// nanoseconds before its second, to the picosecond (VP p. 68).
#[test]
fn the_engines_cable_delay_puts_its_1_pps_earlier() {
    const CABLE_DELAY_NANOSECONDS: u32 = 250_000;
    let mut engine = part(GPS, ONCORE);
    let data = CABLE_DELAY_NANOSECONDS.to_be_bytes();
    let mut command = vec![0; OVERHEAD + data.len()];
    encode(AZ_CABLE_DELAY, &data, &mut command).expect("room for the command");
    let now = seconds(1) + BoardSpan::from_picos(5);
    driving(now, |drive| {
        for byte in &command {
            engine.input(0, frame(*byte), drive);
        }
    });
    let line = BoardSpan::from_secs(10) / 9600 * command.len() as u64;
    driving(now + line, |drive| engine.advance(drive));
    let pulse = seconds(2) - BoardSpan::from_nanos(u64::from(CABLE_DELAY_NANOSECONDS));
    assert_eq!(engine.next_event(test_moment(now + line)), Some(pulse));
    let driven = driving(pulse, |drive| engine.advance(drive));
    assert!(
        driven.iter().any(|driven| driven.port == 2
            && driven.at == pulse
            && driven.signal == Signal::Level(true)),
        "{driven:?}"
    );
}

/// `@@Cj`, the receiver ID's request, in frames on `rx`, each read at
/// its stop bit's end: the answer comes back on `tx` from there, back to
/// back at the line's rate, each frame at its start bit.
#[test]
fn the_engine_answers_frames_with_frames() {
    let mut engine = part(GPS, ONCORE);
    let now = seconds(1) + BoardSpan::from_picos(5);
    let driven = driving(now, |drive| {
        for byte in b"@@Cj)\r\n" {
            engine.input(0, frame(*byte), drive);
        }
    });
    assert!(
        driven.iter().all(|driven| driven.port != 1),
        "nothing before the request's end"
    );
    let line = BoardSpan::from_secs(10) / 9600;
    let ended = engine
        .next_event(test_moment(now))
        .expect("the request's end");
    assert_eq!(ended, now + line);
    let driven = driving(ended, |drive| engine.advance(drive));
    let frames: Vec<_> = driven.iter().filter(|driven| driven.port == 1).collect();
    // the answer, 294 bytes (gps/tests.rs)
    assert_eq!(frames.len(), 294);
    assert_eq!(frames[0].at, ended);
    assert_eq!(frames[0].signal, frame(b'@'));
    assert_eq!(frames[293].at, ended + 293 * line);
    assert_eq!(frames[293].signal, frame(b'\n'));
}

/// What a script's write-gps step gives the engine goes out at the time
/// it is given, the engine run then, not at its next second.
#[test]
fn the_engine_sends_what_it_is_given_at_once() {
    let mut engine = part(GPS, ONCORE);
    let now = Time::ZERO + BoardSpan::from_millis(500);
    (engine.as_mut() as &mut dyn Any)
        .downcast_mut::<GpsPart>()
        .expect("the engine")
        .inject(b"@@x", now);
    assert_eq!(engine.next_event(test_moment(now)), Some(now));
    let driven = driving(now, |drive| engine.advance(drive));
    let line = BoardSpan::from_secs(10) / 9600;
    let frames: Vec<_> = driven
        .iter()
        .filter(|driven| driven.port == 1)
        .map(|driven| (driven.at, driven.signal))
        .collect();
    assert_eq!(
        frames,
        [
            (now, frame(b'@')),
            (now + line, frame(b'@')),
            (now + 2 * line, frame(b'x'))
        ]
    );
    assert_eq!(
        engine.next_event(test_moment(now + 2 * line)),
        Some(seconds(1)),
        "its next second"
    );
}

/// The OCXO part's oscillator.
fn ocxo(part: &dyn Peripheral) -> &OcxoPart {
    (part as &dyn Any).downcast_ref().expect("an OCXO")
}

/// The clock a call drove on the OCXO's `out`, port 1.
fn clock(driven: &[Driven]) -> Option<NetClock> {
    driven
        .iter()
        .find_map(|driven| match (driven.port, driven.signal) {
            (1, Signal::Clock(clock)) => Some(clock),
            _ => None,
        })
}

#[test]
fn the_ocxo_drives_its_clock_as_its_efc_moves_it() {
    let mut part = part(OCXO, "offset = 1e-8");
    let frequency = |part: &dyn Peripheral, now| ocxo(part).ocxo.frequency(cycles(now));
    let on = driving(Time::ZERO, |drive| part.power_on(drive));
    let first = clock(&on).expect("its clock at power-on");
    // 10 MHz and 1e-8 of it, in nanohertz
    assert_eq!(
        first.frequency,
        Frequency::from_nanohertz(10_000_000_100_000_000)
    );
    assert_eq!((first.at, first.phase), (Time::ZERO, Phase::from_units(0)));
    let now = seconds(30);
    assert!((frequency(&*part, now) - 1e-8).abs() < 1e-20);
    // the AD569's VOUT for the word 0x8000 - 1000, 1000 steps below the
    // middle, 0 V
    let volts = -1000.0 * (EFC_VREF_POSITIVE.1 - EFC_VREF_NEGATIVE.1) / EFC_CODES;
    let driven = driving(now, |drive| {
        part.input(0, Signal::Analog(volts), drive);
    });
    let retuned = clock(&driven).expect("retuned at the strobe");
    assert_eq!(retuned, first.retuned(Frequency::from_hz(10_000_000), now));
    assert!(frequency(&*part, now).abs() < 1e-20);
    assert_eq!(
        part.next_event(test_moment(now)),
        None,
        "no noise and no drift, no events"
    );
}

#[test]
fn a_drifting_ocxo_retunes_each_second() {
    let mut part = part(OCXO, "aging = 1e-9");
    driving(Time::ZERO, |drive| part.power_on(drive));
    assert_eq!(part.next_event(test_moment(Time::ZERO)), Some(seconds(1)));
    let driven = driving(seconds(1), |drive| part.advance(drive));
    assert!(clock(&driven).is_some(), "a second's aging");
    assert_eq!(part.next_event(test_moment(seconds(1))), Some(seconds(2)));
}

#[test]
fn the_ocxos_noise_is_its_own_and_timed() {
    let table = "white_fm = 1e-11\nrandom_walk_fm = 1e-12";
    let mut part = part(OCXO, table);
    assert_eq!(
        part.next_event(test_moment(Time::ZERO)),
        Some(seconds(1)),
        "drawn at power-up"
    );
    driving(seconds(1), |drive| part.advance(drive));
    assert_eq!(part.next_event(test_moment(seconds(1))), Some(seconds(2)));
    assert!(matches!(
        construct(OCXO, "offset = \"fast\""),
        Err(ParameterError::Type { .. })
    ));
}

/// The temperature's snapshot holds its warm-up's place, which the
/// skip warm-up control moves after construction, and refuses one no
/// run comes to.
#[test]
fn the_temperature_restores_its_warm_up() {
    let mut settled = part(TEMPERATURE, "warm_up_settled = true");
    let mut cold = part(TEMPERATURE, "");
    let minute = seconds(60);
    let settled_level = settled.analog_output(0, test_moment(minute));
    assert_ne!(cold.analog_output(0, test_moment(minute)), settled_level);
    cold.restore(&settled.save().expect("saved"))
        .expect("restored");
    assert_eq!(cold.analog_output(0, test_moment(minute)), settled_level);
    let impossible = snapshot::save(&Warmth::Warming { head_start: -1.0 });
    assert!(cold.restore(&impossible).is_err());
}

/// A term TOML gives as NaN or an infinity is refused, naming it.
#[test]
fn the_ocxo_refuses_a_term_not_a_finite_number() {
    for (table, term) in [("offset = nan", "offset"), ("aging = -inf", "aging")] {
        assert!(
            matches!(
                construct(OCXO, table),
                Err(ParameterError::Invalid { ref key, .. }) if key == term
            ),
            "{table}"
        );
    }
}

#[test]
fn the_sources_are_sampled_as_they_warm() {
    // counts as volts, the ADC's
    let volts = |count: f64| Some(adc_volts(ADC_SPAN, count));
    let mut oven = part(OCXO, "");
    assert_eq!(oven.analog_output(4, test_moment(Time::ZERO)), volts(199.0));
    assert!(oven.analog_output(4, test_moment(seconds(900))) < volts(100.0));
    assert_eq!(
        oven.analog_output(5, test_moment(seconds(900))),
        volts(199.0)
    );
    assert_eq!(
        oven.analog_output(0, test_moment(Time::ZERO)),
        None,
        "the EFC is an input"
    );
    let mut supplies = part(SUPPLIES, "");
    assert_eq!(
        supplies.analog_output(1, test_moment(Time::ZERO)),
        volts(207.0)
    );
    assert_eq!(
        supplies.analog_output(3, test_moment(Time::ZERO)),
        volts(157.0),
        "the Z3817A's 12B"
    );
    assert_eq!(
        supplies.analog_output(4, test_moment(Time::ZERO)),
        volts(123.0),
        "its -12B"
    );
    let mut temperature = part(TEMPERATURE, "");
    assert_eq!(
        temperature.analog_output(0, test_moment(Time::ZERO)),
        volts(92.0)
    );
}

/// What `engine` sends on `tx` by `now`, run there.
fn sent(engine: &mut dyn Peripheral, now: Time) -> Vec<u8> {
    driving(now, |drive| engine.advance(drive))
        .iter()
        .filter(|driven| driven.port == 1)
        .filter_map(|driven| match driven.signal {
            Signal::Serial(serial) => match serial.frame {
                Frame::Data(data) => Some(data as u8),
                _ => None,
            },
            _ => None,
        })
        .collect()
}

/// An engine that does not answer still sends what it sends unasked:
/// `@@Ea` each second from an eight-channel engine saved as a 58503A
/// leaves it, fixing from power-up.
#[test]
fn a_deaf_engine_streams_its_status_unasked() {
    let table = format!("{ONCORE}\nengine = \"b4121\"\nanswers = false\nsaved = \"58503a\"");
    let mut engine = part(GPS, &table);
    let now = seconds(1) + BoardSpan::from_picos(5);
    driving(now, |drive| {
        for byte in b"@@Cj)\r\n" {
            engine.input(0, frame(*byte), drive);
        }
    });
    let line = BoardSpan::from_secs(10) / 9600;
    let bytes = sent(engine.as_mut(), now + line);
    assert!(bytes.is_empty(), "no answer: {bytes:?}");
    let bytes = sent(engine.as_mut(), seconds(2));
    assert_eq!(&bytes[..4], b"@@Ea");
    assert_eq!(&bytes[74..76], b"\r\n", "the eight-channel status message");
}

/// A saved configuration is one the shared engine has built in, and
/// suits the engine's channels: the 58503A's needs eight.
#[test]
fn a_saved_configuration_is_a_known_one_for_the_engines_channels() {
    for table in [
        format!("{ONCORE}\nengine = \"b4121\"\nsaved = \"z3801a\""),
        format!("{ONCORE}\nengine = \"b1121\"\nsaved = \"58503a\""),
    ] {
        assert!(
            matches!(
                construct(GPS, &table),
                Err(ParameterError::Invalid { key, .. }) if key == "saved"
            ),
            "{table}"
        );
    }
}

/// Without Time RAIM the eight-channel engine's `@@Cj` options lack
/// `I`; no six-channel engine comes without it.
#[test]
fn an_engine_without_time_raim_lists_no_i() {
    let table = format!("{ONCORE}\nengine = \"b4121-no-time-raim\"");
    let mut engine = part(GPS, &table);
    let now = seconds(1) + BoardSpan::from_picos(5);
    driving(now, |drive| {
        for byte in b"@@Cj)\r\n" {
            engine.input(0, frame(*byte), drive);
        }
    });
    let line = BoardSpan::from_secs(10) / 9600;
    let bytes = sent(engine.as_mut(), now + line);
    assert_eq!(bytes.len(), 294);
    let text = String::from_utf8_lossy(&bytes);
    assert!(text.contains("OPTIONS LIST    B         "), "{text}");
    assert!(matches!(
        construct(GPS, &format!("{ONCORE}\nengine = \"b1121-no-time-raim\"")),
        Err(ParameterError::Invalid { key, .. }) if key == "engine"
    ));
}

/// The pulses the selector passes by second `n`, each rise's second.
fn rises(selector: &mut dyn Peripheral, until: u64) -> Vec<u64> {
    let mut rises = Vec::new();
    for n in 1..=until {
        let now = seconds(n);
        for driven in driving(now, |drive| selector.advance(drive)) {
            if driven.port == 1 && driven.signal == Signal::Level(true) {
                rises.push(driven.at / BoardSpan::from_secs(1));
            }
        }
    }
    rises
}

/// The selector passes the selected reference's pulse, each second for
/// 1 PPS and each even second for PP2S, and its valid line, the
/// selector's input high picking `select_high`'s.
#[test]
fn the_selector_passes_the_selected_reference() {
    let table = "timeref1 = \"pp2s\"\ntimeref1_valid = false\n\
                 timeref2 = \"1pps\"\ntimeref2_valid = true";
    let mut selector = part(TIME_REFERENCES, table);
    let powered = driving(Time::ZERO, |drive| selector.power_on(drive));
    assert_eq!(powered[0].signal, Signal::Level(false), "TIMEREF1's valid");
    assert_eq!(rises(selector.as_mut(), 4), [2, 4], "TIMEREF1's PP2S");
    let now = seconds(4) + BoardSpan::from_picos(1);
    let switched = driving(now, |drive| {
        selector.input(0, Signal::Level(false), drive);
    });
    assert_eq!(switched[0].port, 2);
    assert_eq!(switched[0].signal, Signal::Level(true), "TIMEREF2's valid");
    let mut later = Vec::new();
    for n in 5..=7 {
        let now = seconds(n);
        for driven in driving(now, |drive| selector.advance(drive)) {
            if driven.port == 1 && driven.signal == Signal::Level(true) {
                later.push(driven.at / BoardSpan::from_secs(1));
            }
        }
    }
    assert_eq!(later, [5, 6, 7], "TIMEREF2's 1 PPS");
    let two = part(TIME_REFERENCES, &format!("{table}\nselect_high = 2"));
    let mut two = two;
    let powered = driving(Time::ZERO, |drive| two.power_on(drive));
    assert_eq!(powered[0].signal, Signal::Level(true), "TIMEREF2's valid");
    for bad in ["timeref1 = \"2pps\"", "select_high = 3"] {
        assert!(
            matches!(
                construct(TIME_REFERENCES, bad),
                Err(ParameterError::Invalid { .. })
            ),
            "{bad}"
        );
    }
}

/// A reference's pulse changed during the run: a PP2S that becomes a
/// 1 PPS at second 5 pulses at the even seconds before it and at every
/// second from it.  A change wants both its pulse and its second.
#[test]
fn the_selector_changes_a_pulse_at_its_second() {
    let mut selector = part(
        TIME_REFERENCES,
        "timeref1 = \"pp2s\"\ntimeref1_change = \"1pps\"\ntimeref1_change_second = 5",
    );
    driving(Time::ZERO, |drive| selector.power_on(drive));
    assert_eq!(rises(selector.as_mut(), 8), [2, 4, 5, 6, 7, 8]);
    for bad in [
        "timeref1_change = \"1pps\"",
        "timeref1_change_second = 5",
        "timeref1_change = \"2pps\"\ntimeref1_change_second = 5",
    ] {
        assert!(
            matches!(
                construct(TIME_REFERENCES, bad),
                Err(ParameterError::Invalid { .. })
            ),
            "{bad}"
        );
    }
}

/// The oscillator's report: 0x55, its reading of the EFC in 24 bits, a
/// status byte, its ready bit set from `ready_seconds` on, and the sum,
/// at 19200 baud each second.
#[test]
fn the_oscillator_reports_the_efc_it_reads() {
    assert_eq!(
        construct(EFC_MONITOR, "").map(|_| ()),
        Err(ParameterError::Missing("counts_per_efc".to_owned()))
    );
    let mut monitor = part(EFC_MONITOR, "counts_per_efc = 16.0");
    driving(Time::ZERO, |drive| monitor.power_on(drive));
    driving(Time::ZERO, |drive| {
        monitor.input(0, Signal::Analog(0.0), drive);
    });
    assert_eq!(
        monitor.next_event(test_moment(Time::ZERO)),
        Some(seconds(1))
    );
    let driven = driving(seconds(1), |drive| monitor.advance(drive));
    let frames: Vec<_> = driven
        .iter()
        .map(|driven| match driven.signal {
            Signal::Serial(serial) => (driven.at, serial.frame),
            other => panic!("{other:?}"),
        })
        .collect();
    // 0 V is the DAC's mid-scale word, 0x8000: 0x80000 of the firmware's
    // EFC, 16 counts each; ready from the first report
    let report: [u8; 6] = [0x55, 0x80, 0x00, 0x00, 0x01, 0xd6];
    assert_eq!(
        frames.iter().map(|(_, frame)| *frame).collect::<Vec<_>>(),
        report.map(|byte| Frame::Data(u16::from(byte)))
    );
    let line = BoardSpan::from_secs(10) / 19_200;
    assert_eq!(frames[5].0, seconds(1) + 5 * line);
    assert_eq!(
        monitor.next_event(test_moment(seconds(1))),
        Some(seconds(2))
    );
    // with `ready_seconds`, not ready before it
    let mut warming = part(EFC_MONITOR, "counts_per_efc = 16.0\nready_seconds = 2");
    driving(Time::ZERO, |drive| warming.power_on(drive));
    let status = |warming: &mut Box<dyn Peripheral>, at: Time| {
        let driven = driving(at, |drive| warming.advance(drive));
        match driven[4].signal {
            Signal::Serial(serial) => serial.frame,
            other => panic!("{other:?}"),
        }
    };
    assert_eq!(status(&mut warming, seconds(1)), Frame::Data(0x00));
    assert_eq!(status(&mut warming, seconds(2)), Frame::Data(0x01));
    // a warm-up settled, or one with a head start, ready before it
    for warm_up in ["warm_up_settled = true", "warm_up_seconds = 1.0"] {
        let table = format!("counts_per_efc = 16.0\nready_seconds = 2\n{warm_up}");
        let mut warm = part(EFC_MONITOR, &table);
        driving(Time::ZERO, |drive| warm.power_on(drive));
        assert_eq!(
            status(&mut warm, seconds(1)),
            Frame::Data(0x01),
            "{warm_up}"
        );
    }
}

/// The Z3815A's puck's report, the same bytes clocked out: `framed`
/// raised at each period with the first bit on `data`, the next at each
/// rise of `clock`, `framed` lowered after the last until the next
/// period, a report clocked past its period ending where it ends.
#[test]
fn the_puck_clocks_its_report_out_a_bit_a_rise() {
    const CLOCK: usize = 1;
    const FRAMED: usize = 2;
    const DATA: usize = 3;
    let mut puck = part(CLOCKED_EFC_MONITOR, "counts_per_efc = 16.0");
    driving(Time::ZERO, |drive| puck.power_on(drive));
    driving(Time::ZERO, |drive| {
        puck.input(0, Signal::Analog(0.0), drive);
    });
    let level = |driven: &[Driven], port: usize| {
        driven
            .iter()
            .rev()
            .find(|driven| driven.port == port)
            .map(|driven| driven.signal)
    };
    for (period, start) in [
        (1, seconds(1)),
        (2, seconds(2) + BoardSpan::from_millis(500)),
    ] {
        assert_eq!(
            puck.next_event(test_moment(seconds(period - 1))),
            Some(seconds(period)),
            "{period}"
        );
        let driven = driving(start, |drive| puck.advance(drive));
        assert_eq!(level(&driven, FRAMED), Some(Signal::Level(true)));
        assert_eq!(
            puck.next_event(test_moment(start)),
            Some(seconds(period + 1)),
            "the next"
        );
        // a period begun while a report goes out passes
        if period == 2 {
            let late = seconds(3);
            let driven = driving(late, |drive| puck.advance(drive));
            assert!(driven.is_empty(), "{driven:?}");
            assert_eq!(puck.next_event(test_moment(late)), Some(seconds(4)));
        }
        // the report of 0 V, as the serial link's, a bit at a time
        let report: [u8; 6] = [0x55, 0x80, 0x00, 0x00, 0x01, 0xd6];
        let mut bits = vec![level(&driven, DATA) == Some(Signal::Level(true))];
        let mut framed = true;
        while framed {
            let driven = driving(start, |drive| {
                puck.input(CLOCK, Signal::Level(false), drive);
                puck.input(CLOCK, Signal::Level(true), drive);
            });
            match level(&driven, FRAMED) {
                Some(Signal::Level(false)) => framed = false,
                _ => bits.push(level(&driven, DATA) == Some(Signal::Level(true))),
            }
        }
        let bytes: Vec<u8> = bits
            .chunks(8)
            .map(|byte| byte.iter().fold(0, |sum, bit| sum << 1 | u8::from(*bit)))
            .collect();
        assert_eq!(bytes, report);
    }
    assert_eq!(
        puck.next_event(test_moment(seconds(2) + BoardSpan::from_millis(500))),
        Some(seconds(4))
    );
}

/// The Z3817A's EFC chain shifts on SCK's fall: a word written in mode
/// 2 reaches `Q` whole, and read back in mode 3 it comes a bit early,
/// which the firmware's self-test shifts back (0x288c4).
#[test]
fn the_efc_shift_register_reads_back_a_bit_early() {
    let mut chain = part(EFC_SHIFT, "initial = 0x8000");
    let powered = driving(Time::ZERO, |drive| chain.power_on(drive));
    assert_eq!(powered[0].signal, Signal::Bus(0x8000));
    let write = Transfer::new(0x5555, 16, test_qspi_clock_in(2)).selecting(5);
    let driven = driving(Time::ZERO, |drive| {
        chain.transfer(write, drive);
    });
    assert_eq!(driven[0].signal, Signal::Bus(0x5555));
    let read = Transfer::new(0, 16, test_qspi_clock_in(3)).selecting(5);
    let mut back = 0;
    driving(Time::ZERO, |drive| back = chain.transfer(read, drive));
    assert_eq!(back >> 1, 0x5555);
}
