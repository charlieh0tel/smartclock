//! The mc683xx run's tests: its boards built and run, a power cycle's
//! carry, the lines' framing, the external 1 PPS, and the EEPROM in
//! memory from files in memory.

use std::cell::Ref;
use std::cell::RefCell;
use std::io::Write;
use std::path::Path;
use std::path::PathBuf;
use std::rc::Rc;

use mc683xx_board::Board;
use mc683xx_board::signal::Direction;
use mc683xx_board::signal::Kind;
use mc683xx_board::time::PICOSECONDS_PER_SECOND;
use mc683xx_board::time::Span;
use mc683xx_board::time::Time;
use mc683xx_mc68331::Mc68331;
use mc683xx_peripherals::At28c64b;
use smartclock::http::ServeError;
use smartclock::types::Framing;
use smartclock_gps::engine::Options;
use smartclock_gps::units::Seconds;
use toml::Table;

use super::Error;
use super::KeptEeprom;
use super::Step;
use super::carried::Carried;
use super::carried::Epoch;
use super::eeprom;
use super::endpoints::ALARM;
use super::endpoints::GPS_PPS;
use super::endpoints::frame_bits;
use super::endpoints::frame_byte;
use super::load::Loaded;
use super::load::entries;
use super::load::fpga_design;
use super::overrides::Parts;
use super::powered::Powered;
use super::pps::PpsTrain;
use super::runner::Runner;
use crate::board::analog::Oven;
use crate::board::analog::OvenOutput;
use crate::board::analog::Warmth;
use crate::board::eeprom::ERASED;
use crate::board::lifetime::start_efc;
use crate::board::ocxo::EFC_GAIN;
use crate::board::ocxo::MIDDLE_COUNTS;
use crate::board::ocxo::on_frequency_at;
use crate::board::participants::gps::PPS_HIGH;
use crate::board::participants::ocxo::frequency_now;
use crate::board::participants::ocxo::oven_level_now;
use crate::board::profile::Model;
use crate::board::time::CPU_HZ;
use crate::board::time::Cycles;
use crate::config::Core;
use crate::config::Eeprom;
use crate::config::GpsSource;
use crate::config::HostPort;
use crate::config::RunConfig;
use crate::config::StopAfter;
use crate::files::Disk;
use crate::files::Files;
use crate::files::InMemory;
use crate::gps::B1121;
use crate::gps::B4121;
use crate::gps::Config as GpsConfig;
use crate::gps::GPS_LESS_UTC;
use crate::gps::Port;
use crate::gps::engine::Synthetic;
use crate::gps::external::PulseClock;
use crate::gps::sky::FIRMWARE_DEFAULT;
use crate::host::Attach;
use crate::host::Duplex;
use crate::host::Ports;
use crate::host::Unserved;
use crate::line::FarEnd;
use crate::panel::routes::Shared;
use crate::panel::state::Drawing;
use crate::ports::GPS;
use crate::ports::HOST;
use crate::ports::SCI;
use crate::run::Alongside;

/// UTC at the run's start, seconds since 1970.
const START_SECONDS: i64 = 1_792_670_400;
/// The repository's root.
pub(super) const ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");

/// A run of `image` on the mc683xx core, as the command line's defaults
/// make it, its EEPROM working file in the temporary directory under
/// `name`, the engine's start fixed.
pub(super) fn config(image: &str, name: &str) -> RunConfig {
    let eeprom = std::env::temp_dir().join(format!(
        "smartclock-emu-board-file-{name}-{}.bin",
        std::process::id()
    ));
    remove_eeprom(&eeprom);
    let path = PathBuf::from(format!("{ROOT}/{image}"));
    let bytes = std::fs::read(&path).expect("the image");
    let start = "2026-10-22T12:00:00Z".parse().expect("a time");
    let eeprom = Eeprom::File {
        path: eeprom,
        dump: None,
    };
    let mut config = RunConfig::new(path, bytes, None, eeprom, start).expect("a run");
    config.core = Core::Mc683xx;
    config
}

/// The 58503A's image, whose board file has no SCI.
const HP58503A: &str = "third_party/firmware/images/58503a-3704.bin";

/// The run `config` asks for, its board powered up and its endpoints
/// bound, the host port served on a port the system picks.
fn runner(config: RunConfig) -> Result<Runner, Error> {
    let loaded = Loaded::new(&config, Rc::new(Disk))?;
    let powered = Powered::up(&config, &loaded, Carried::default())?;
    Runner::new(config, loaded, powered, &mut Unserved)
}

/// The board `config` asks for, its file read, powered up with what
/// `carried` carries.
fn build(config: &RunConfig, carried: &Carried) -> Result<(Board<Mc68331>, Parts), Error> {
    Loaded::new(config, Rc::new(Disk))?.build(config, carried)
}

/// A run's EEPROM working file `path` removed, with the AT28C64B's
/// protection beside it (mc683xx's docs/peripherals.md).
pub(super) fn remove_eeprom(path: &Path) {
    let _ = std::fs::remove_file(path);
    let mut protection = path.as_os_str().to_owned();
    protection.push(".sdp");
    let _ = std::fs::remove_file(protection);
}

/// `config`'s EEPROM's working file.
pub(super) fn working_file(config: &RunConfig) -> &Path {
    config.eeprom.working_file().expect("a working file")
}

/// What the run's first power-up carries.
fn carried() -> Carried {
    Carried::default()
}

/// The board's run-time endpoints' names.
fn names(board: &Board<Mc68331>) -> Vec<String> {
    board
        .externals()
        .into_iter()
        .map(|external| external.name)
        .collect()
}

#[test]
fn each_board_file_builds_with_smartclocks_types_and_runs() {
    for (model, image, externals, panel, gps) in [
        (
            Model::Z3805A,
            "third_party/firmware/images/z3805a-3543b.bin",
            vec![HOST, SCI],
            None,
            Some("gps"),
        ),
        (
            Model::Hp58503A,
            "third_party/firmware/images/58503a-3704.bin",
            vec![ALARM, HOST],
            Some("front_panel"),
            Some("gps"),
        ),
        (
            Model::Z3801A,
            "third_party/firmware/images/z3801a-3543.bin",
            vec![HOST],
            None,
            Some("gps"),
        ),
        (
            Model::Z3816A,
            "third_party/firmware/images/z3816a-4001.bin",
            vec![HOST],
            None,
            Some("gps"),
        ),
        (
            Model::Hp58503B,
            "third_party/firmware/images/58503b-1.01.04.bin",
            vec![ALARM, HOST],
            Some("front_panel"),
            Some("gps"),
        ),
    ] {
        let config = config(image, &format!("{model:?}"));
        assert_eq!(config.model, model);
        let (mut board, parts) = build(&config, &carried()).expect("built");
        assert_eq!(names(&board), externals, "{model:?}");
        assert_eq!(parts.front_panel.as_deref(), panel);
        assert_eq!(parts.gps.as_deref(), gps);
        board
            .run_until(Time::ZERO + Span::from_millis(100))
            .expect("no part stalls");
        assert_eq!(board.chip().cpu().halted(), None, "{model:?}");
        drop(board);
        remove_eeprom(working_file(&config));
    }
}

/// A working file in a directory that is not there, as in a fresh
/// checkout's `target/smartclock-emu/`, is made, the directory too.
#[test]
fn the_eeprom_working_files_missing_directory_is_made() {
    let mut config = config(HP58503A, "directory");
    let directory =
        std::env::temp_dir().join(format!("smartclock-emu-directory-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&directory);
    let working = directory.join("below/eeprom.bin");
    config.eeprom = Eeprom::File {
        path: working.clone(),
        dump: None,
    };
    let (board, _) = build(&config, &carried()).expect("built");
    drop(board);
    assert!(working.parent().expect("a directory").is_dir());
    std::fs::remove_dir_all(&directory).expect("removed");
}

/// `--gps-engine` gives the board file's engine its model: one the
/// engine knows builds, of either family, another is refused by the
/// part.
#[test]
fn the_gps_engine_is_the_runs_in_place_of_the_board_files() {
    let mut config = config(
        "third_party/firmware/images/58503b-1.01.04.bin",
        "gps-engine",
    );
    config.gps_engine = Some("b4121".to_owned());
    assert!(build(&config, &carried()).is_ok());
    // a Furuno on the Oncore's part: the run gives its protocol too
    config.gps_engine = Some("gt-74".to_owned());
    assert!(build(&config, &carried()).is_ok());
    config.gps_engine = Some("m12".to_owned());
    let refused = build(&config, &carried())
        .map(|_| ())
        .expect_err("no such engine");
    let refused = format!("{refused:?}");
    assert!(refused.contains("the engine is one of"), "{refused}");
    remove_eeprom(working_file(&config));
}

/// A deterministic power cycle 2 s in, the oscillator aging fast, with
/// no offset and its EFC at no gain: its frequency on the board built
/// again, at power-up, is the old board's just before, the aging
/// carried.
#[test]
fn a_power_cycle_keeps_the_oscillators_aging() {
    const AGING: f64 = 1e-3;
    let mut config = config("third_party/firmware/images/z3805a-3543b.bin", "aging");
    config.ocxo.aging = Some(AGING);
    config.ocxo.offset = Some(0.0);
    config.ocxo.efc_gain = Some(0.0);
    let (mut board, parts) = build(&config, &carried()).expect("built");
    let ocxo = parts.ocxo.clone().expect("an oscillator");
    board.run_until(Time::from_secs(2)).expect("no part stalls");
    let before = frequency_now(&board, &ocxo).expect("its frequency");
    let carried = carried()
        .through_power_cycle(&board, &parts)
        .expect("carried");
    drop(board);
    let (board, _) = build(&config, &carried).expect("built again");
    let after = frequency_now(&board, &ocxo).expect("its frequency");
    assert!(before > AGING * 2.0 / 86_400.0 * 0.99, "{before}");
    assert!((after - before).abs() < 1e-20, "{before} then {after}");
    drop(board);
    remove_eeprom(working_file(&config));
}

/// The 58503A's board file with its oscillator's `offset` at
/// `BOARD_OFFSET`, read from memory: the oscillator runs at the board
/// file's offset unless `--ocxo-offset` gives one, which takes its
/// place.
#[test]
fn the_board_files_oscillator_terms_hold_unless_the_run_gives_its_own() {
    const BOARD_OFFSET: f64 = 1e-6;
    const RUN_OFFSET: f64 = -2e-7;
    const GAIN: &str = "efc_gain = 6.25e-13";
    let config = in_memory("board-terms", None);
    let text = std::fs::read_to_string(&config.board).expect("the board file");
    assert!(text.contains(GAIN), "the oscillator's gain to place it by");
    let text = text.replacen(GAIN, &format!("offset = {BOARD_OFFSET}\n{GAIN}"), 1);
    let files = [
        config.image_path.clone(),
        Path::new(ROOT).join(HP58503A_DUMP),
    ]
    .iter()
    .fold(InMemory::default(), |files, path| from_disk(files, path))
    .with(&config.board, text.into_bytes());
    let frequency = |config: &RunConfig| {
        let loaded = Loaded::new(config, Rc::new(files.clone())).expect("loaded");
        let (board, parts) = loaded.build(config, &carried()).expect("built");
        frequency_now(&board, &parts.ocxo.expect("an oscillator")).expect("its frequency")
    };
    assert_eq!(frequency(&config), BOARD_OFFSET, "the board file's");
    let mut config = config;
    config.ocxo.offset = Some(RUN_OFFSET);
    assert_eq!(frequency(&config), RUN_OFFSET, "the run's");
}

/// A power cycle 2 s in carries 2 s of warm-up, the board's own
/// clock's, as the oscillator and the temperature count it, not the
/// chip's, which ran slower until the reset code set SYNCR.
#[test]
fn a_power_cycle_carries_the_warm_up_on_the_boards_clock() {
    let config = config("third_party/firmware/images/z3805a-3543b.bin", "warm-up");
    let (mut board, parts) = build(&config, &carried()).expect("built");
    let now = Time::from_secs(2);
    board.run_until(now).expect("no part stalls");
    assert!(
        board.system_clock().cycle(now).clocks() < 2 * CPU_HZ,
        "the chip's clock behind the board's"
    );
    let carried = carried()
        .through_power_cycle(&board, &parts)
        .expect("carried");
    assert_eq!(carried.warmth, Warmth::Warming { head_start: 2.0 });
    drop(board);
    remove_eeprom(working_file(&config));
}

/// With `--skip-warm-up` the run's power-up and a power cycle's each
/// start at the curve's settled end: `warming` false and the oven's
/// monitors at their warm levels from the first instant; without it
/// the run starts warming.
#[test]
fn skip_warm_up_settles_every_power_up() {
    let mut config = config(
        "third_party/firmware/images/z3805a-3543b.bin",
        "skip-warm-up",
    );
    let loaded = Loaded::new(&config, Rc::new(Disk)).expect("loaded");
    let mut cold = Powered::up(&config, &loaded, Carried::default()).expect("built");
    let warming = cold.live().snapshot(Drawing::Z3805A).controls.warming;
    assert_eq!(warming, Some(true), "warming without the option");
    drop(cold);
    config.skip_warm_up = true;
    let settled = |powered: &mut Powered| {
        let live = powered.live();
        assert_eq!(live.snapshot(Drawing::Z3805A).controls.warming, Some(false));
        let board = live.board();
        for output in [OvenOutput::Current, OvenOutput::Oven] {
            let level = oven_level_now(board, "ocxo", output).expect("an oscillator");
            let warm = Oven {
                warmth: Warmth::Settled,
            }
            .level(output, Cycles(0));
            assert!((level - warm).abs() < f64::EPSILON, "{output:?} at {level}");
        }
    };
    let mut powered = Powered::up(&config, &loaded, Carried::default()).expect("built");
    settled(&mut powered);
    powered
        .live()
        .run_until(Time::ZERO + Span::from_millis(100))
        .expect("no part stalls");
    let mut powered = powered.power_cycled(&config, &loaded).expect("built again");
    assert_eq!(powered.live().now(), Time::ZERO, "powered up again");
    settled(&mut powered);
    drop(powered);
    remove_eeprom(working_file(&config));
}

/// `--fpga-design` sets the FPGA's design, and drops a `bitstream` the
/// board file asserts unless the design is the LCA model, which alone
/// takes one.
#[test]
fn the_fpga_design_drops_a_bitstream_but_for_the_lca() {
    let text = std::fs::read_to_string(format!("{ROOT}/boards/z3805a.toml")).expect("the board");
    let fpga = |file: &mut Table| -> Table {
        let device = entries(file, "peripheral")
            .into_iter()
            .find(|device| device["type"].as_str() == Some("xc3042"))
            .expect("the FPGA");
        device["parameters"]
            .as_table()
            .expect("its parameters")
            .clone()
    };
    for (design, kept) in [("z3805a", false), ("unmodeled", false), ("lca", true)] {
        let asserted = text.replace(
            "design = \"z3805a\"",
            "design = \"lca\"\nbitstream = \"z3805a\"",
        );
        let mut file: Table = toml::from_str(&asserted).expect("parsed");
        assert!(
            fpga(&mut file).contains_key("bitstream"),
            "the file asserts it"
        );
        fpga_design(&mut file, design);
        let parameters = fpga(&mut file);
        assert_eq!(parameters["design"].as_str(), Some(design));
        assert_eq!(parameters.contains_key("bitstream"), kept, "{design}");
    }
}

#[test]
fn a_seven_odd_line_carries_odd_parity_above_seven_data_bits() {
    // 's' 0x73 has five ones, odd already; 'c' 0x63 four, so its
    // parity bit is set, bit 7 (MC68681 3.1)
    assert_eq!(frame_bits(Framing::SevenOdd, b's'), 0x73);
    assert_eq!(frame_bits(Framing::SevenOdd, b'c'), 0xe3);
    assert_eq!(frame_byte(Framing::SevenOdd, 0xe3), b'c');
    assert_eq!(frame_bits(Framing::EightNone, 0xe3), 0xe3);
    assert_eq!(frame_byte(Framing::EightNone, 0xe3), 0xe3);
}

/// On the emulator's own clock the external pulse rises on the run's
/// whole seconds, each a second after the last, high for its width,
/// whatever the engine sends: no message starts or moves it.
#[test]
fn the_external_pulse_rises_on_the_runs_seconds() {
    let now = Time::from_picos(PICOSECONDS_PER_SECOND / 3);
    // the run 1/3 s past a whole second of its own
    let run = Time::from_picos(4 * PICOSECONDS_PER_SECOND + PICOSECONDS_PER_SECOND / 3);
    let mut train = PpsTrain::new(PulseClock::Own, now, run);
    let first = Time::from_secs(1);
    assert_eq!(train.next(), Some(first));
    assert_eq!(train.edges(first - Span::from_picos(1)), Vec::<bool>::new());
    assert_eq!(train.edges(first), [true]);
    assert_eq!(train.next(), Some(first + PPS_HIGH));
    assert_eq!(train.edges(first + PPS_HIGH), [false]);
    let second = first + Span::from_secs(1);
    assert_eq!(train.next(), Some(second));
    assert_eq!(train.edges(second), [true]);
}

/// In real time the external pulse rises on the host's whole seconds:
/// its first rise is within a second of the start, and the rest a
/// second apart on board time, a run behind taking them late, unmoved.
#[test]
fn the_external_pulse_in_real_time_rises_on_the_hosts_seconds() {
    let now = Time::from_secs(2);
    let mut train = PpsTrain::new(PulseClock::Host, now, now);
    let first = train.next().expect("a rise");
    assert!(
        first >= now && first <= now + Span::from_secs(1),
        "{first:?}"
    );
    let late = first + Span::from_secs(3);
    let mut rises = Vec::new();
    while let Some(at) = train.next().filter(|at| *at <= late) {
        if train.edges(at).contains(&true) {
            rises.push(at);
        }
    }
    let steps: Vec<u64> = rises
        .windows(2)
        .map(|pair| pair[1].picos() - pair[0].picos())
        .collect();
    assert_eq!(rises.first(), Some(&first));
    assert_eq!(steps, [PICOSECONDS_PER_SECOND; 3]);
}

#[test]
fn an_external_engine_takes_the_synthetic_ones_place() {
    let mut config = config("third_party/firmware/images/z3805a-3543b.bin", "gps");
    config.gps = GpsSource::External("127.0.0.1:0".to_owned());
    let (board, _) = build(&config, &carried()).expect("built");
    assert_eq!(names(&board), [GPS, GPS_PPS, HOST, SCI]);
    let externals = board.externals();
    let (gps, pps) = (&externals[0], &externals[1]);
    assert_eq!((gps.kind, gps.direction), (Kind::Serial, Direction::InOut));
    assert_eq!((pps.kind, pps.direction), (Kind::Edge, Direction::Out));
    drop(board);
    remove_eeprom(working_file(&config));
}

/// The rises of `train` due by `until`, falls taken as they come.
fn rises_until(train: &mut PpsTrain, until: Time) -> Vec<Time> {
    let mut rises = Vec::new();
    while let Some(at) = train.next().filter(|at| *at <= until) {
        if train.edges(at).contains(&true) {
            rises.push(at);
        }
    }
    rises
}

/// The `@@Az` frame that sets a cable delay of `nanoseconds` (VP p. 68).
fn cable_delay_command(nanoseconds: u32) -> Vec<u8> {
    let mut frame = [0; 16];
    let length = smartclock_gps::oncore::encode(
        smartclock_gps::oncore::MessageId::new(*b"Az"),
        &nanoseconds.to_be_bytes(),
        &mut frame,
    )
    .expect("a frame");
    frame[..length].to_vec()
}

/// A cable delay the firmware sends the external engine moves its pulse
/// by the delay, earlier, at the next rise, and by no more: each rise
/// after is a second after the last, on either clock, the run's or, in
/// real time, the host's.
#[test]
fn a_cable_delay_steps_the_external_pulse_by_the_delay_and_no_more() {
    const DELAY_NANOSECONDS: u32 = 500;
    let delay = u64::from(DELAY_NANOSECONDS) * 1000;
    for seconds in [PulseClock::Own, PulseClock::Host] {
        let now = Time::from_secs(10);
        let mut train = PpsTrain::new(seconds, now, now);
        let before = rises_until(&mut train, now + Span::from_secs(5));
        let last = *before.last().expect("rises");
        train.heard(&cable_delay_command(DELAY_NANOSECONDS));
        let after = rises_until(&mut train, last + Span::from_secs(100));
        assert_eq!(
            after[0].picos(),
            last.picos() + PICOSECONDS_PER_SECOND - delay,
            "{seconds:?}"
        );
        let steps: Vec<u64> = after
            .windows(2)
            .map(|pair| pair[1].picos() - pair[0].picos())
            .collect();
        assert!(
            steps.iter().all(|step| *step == PICOSECONDS_PER_SECOND),
            "{seconds:?}"
        );
        // sent again, it moves nothing
        train.heard(&cable_delay_command(DELAY_NANOSECONDS));
        let again = rises_until(&mut train, last + Span::from_secs(102));
        assert_eq!(
            again[0].picos() - after.last().expect("rises").picos(),
            PICOSECONDS_PER_SECOND
        );
    }
}

/// The same on a running board: the Z3805A 3543B, its engine external,
/// takes `:GPS:REFerence:ADELay 500E-9`, sends `@@Az`, and the pulse,
/// on the run's whole seconds before, is 500 ns before them after, a
/// second apart throughout.
#[test]
fn an_antenna_delay_set_steps_the_external_pulse_on_a_running_board() {
    const STEP: u64 = PICOSECONDS_PER_SECOND / 20;
    const UNTIL: u64 = 45;
    const DELAY: u64 = 500_000;
    let mut config = config("third_party/firmware/images/z3805a-3543b.bin", "gps-adel");
    let script = std::env::temp_dir().join(format!(
        "smartclock-emu-gps-adel-{}.script",
        std::process::id()
    ));
    std::fs::write(&script, "at 25\nsend :GPS:REFerence:ADELay 500E-9\n").expect("the script");
    config.host = HostPort::Script(script.clone());
    config.gps = GpsSource::External("127.0.0.1:0".to_owned());
    config.pacing = crate::run::Pacing::Free;
    let start = config.synthetic.start;
    let loaded = Loaded::new(&config, Rc::new(Disk)).expect("loaded");
    let powered = Powered::up(&config, &loaded, Carried::default()).expect("powered");
    let mut ports = GpsClient::default();
    let eeprom = working_file(&config).to_owned();
    let mut runner = Runner::new(config, loaded, powered, &mut ports).expect("a runner");
    let mut engine = Synthetic::new(
        Client(ports.far.expect("the GPS port served")),
        GpsConfig {
            position: FIRMWARE_DEFAULT,
            start,
            leap_seconds: Seconds(GPS_LESS_UTC),
            engine: &B1121,
            options: Options::DEFAULT,
        },
        false,
    )
    .expect("an engine");
    let heard = Rc::new(RefCell::new(Vec::new()));
    let kept = Rc::clone(&heard);
    engine.on_heard(Box::new(move |bytes| {
        kept.borrow_mut().extend_from_slice(bytes)
    }));
    let mut rises: Vec<u64> = Vec::new();
    for step in 1..=UNTIL * PICOSECONDS_PER_SECOND / STEP {
        let (next, outcome) = runner
            .step_until(Some(Time::from_picos(step * STEP)))
            .expect("ran");
        assert!(matches!(outcome, Step::Running { .. }), "{outcome:?}");
        runner = next;
        engine.advance(runner.live().clocks().cycle);
        let pending = runner.live().next_pps().expect("a pulse due").picos();
        let whole = pending.div_ceil(PICOSECONDS_PER_SECOND) * PICOSECONDS_PER_SECOND;
        // the rise pending for a second, as it stands when that second's
        // rise is taken; a fall, 200 ms in, is none
        if pending != whole && whole - pending != DELAY {
            continue;
        }
        match rises.last_mut() {
            Some(last)
                if last.div_ceil(PICOSECONDS_PER_SECOND)
                    == pending.div_ceil(PICOSECONDS_PER_SECOND) =>
            {
                *last = pending;
            }
            _ => rises.push(pending),
        }
    }
    let early: Vec<bool> = rises
        .iter()
        .map(|at| !at.is_multiple_of(PICOSECONDS_PER_SECOND))
        .collect();
    let first_early = early
        .iter()
        .position(|early| *early)
        .unwrap_or_else(|| panic!("{rises:?} {}", heard.borrow().escape_ascii()));
    assert!(first_early > 20, "{rises:?}");
    assert!(early[first_early..].iter().all(|early| *early), "{rises:?}");
    let steps: Vec<u64> = rises.windows(2).map(|pair| pair[1] - pair[0]).collect();
    for (index, step) in steps.iter().enumerate() {
        let expected = if index + 1 == first_early {
            PICOSECONDS_PER_SECOND - DELAY
        } else {
            PICOSECONDS_PER_SECOND
        };
        assert_eq!(*step, expected, "{rises:?}");
    }
    drop(runner);
    remove_eeprom(&eeprom);
    let _ = std::fs::remove_file(script);
}

/// `--gps` on a board whose engine is a Furuno, the Z3805A 1.01.03's:
/// the `furuno-gt74` part comes off as an `oncore` does, its nets ending
/// at the run-time endpoints.
#[test]
fn an_external_engine_takes_a_furunos_place() {
    let mut config = config(
        "third_party/firmware/images/z3805a-1.01.03.bin",
        "gps-10103",
    );
    config.gps = GpsSource::External("127.0.0.1:0".to_owned());
    let (board, _) = build(&config, &carried()).expect("built");
    assert_eq!(names(&board), [GPS, GPS_PPS, HOST, SCI]);
    drop(board);
    remove_eeprom(working_file(&config));
}

/// An external GT-74 on `--gps` that sends nothing gets its 1 PPS all the
/// same, on each whole second of the run's clock in a deterministic run:
/// the pulse is timed, never taken from the engine's messages, so an
/// engine with no Time RAIM, or one on NMEA, gets it too.
#[test]
fn an_external_engine_gets_a_pulse_on_each_second_without_a_message() {
    let mut config = config(
        "third_party/firmware/images/z3805a-1.01.03.bin",
        "gps-pulse-10103",
    );
    config.gps = GpsSource::External("127.0.0.1:0".to_owned());
    config.pacing = crate::run::Pacing::Free;
    let loaded = Loaded::new(&config, Rc::new(Disk)).expect("loaded");
    let powered = Powered::up(&config, &loaded, Carried::default()).expect("powered");
    let mut ports = GpsClient::default();
    let eeprom = working_file(&config).to_owned();
    let mut runner = Runner::new(config, loaded, powered, &mut ports).expect("a runner");
    let mut rises = Vec::new();
    let step = PICOSECONDS_PER_SECOND / 10;
    for n in 1..=35 {
        let (next, outcome) = runner
            .step_until(Some(Time::from_picos(n * step)))
            .expect("ran");
        assert!(matches!(outcome, Step::Running { .. }), "{outcome:?}");
        runner = next;
        let pending = runner.live().next_pps().expect("a pulse due");
        if rises.last() != Some(&pending) && pending.picos().is_multiple_of(PICOSECONDS_PER_SECOND)
        {
            rises.push(pending);
        }
    }
    let seconds: Vec<u64> = rises
        .iter()
        .map(|at| at.picos() / PICOSECONDS_PER_SECOND)
        .collect();
    assert_eq!(seconds, [1, 2, 3, 4]);
    drop(runner);
    remove_eeprom(&eeprom);
}

/// Ports for a test that keeps the GPS port's far end, as a client
/// connecting to `--gps` would have it, and serves nothing else.
#[derive(Default)]
struct GpsClient {
    /// The far end, once the run serves the port.
    far: Option<Box<dyn Duplex>>,
}

impl Ports for GpsClient {
    fn serve(&mut self, address: &str, name: &'static str, attach: Attach) -> std::io::Result<()> {
        if name == GPS {
            self.far = Some(attach());
        }
        Unserved.serve(address, name, attach)
    }

    fn console(&mut self, far: FarEnd) -> Box<dyn Alongside> {
        Unserved.console(far)
    }

    fn capture(&mut self, path: &Path) -> std::io::Result<Box<dyn Write + Send>> {
        Unserved.capture(path)
    }

    fn serve_page(&mut self, address: &str, shared: Shared) -> Result<(), ServeError> {
        Unserved.serve_page(address, shared)
    }
}

/// A synthetic engine's port on the far end of `--gps`.
struct Client(Box<dyn Duplex>);

impl Port for Client {
    fn receive(&mut self, buffer: &mut [u8]) -> usize {
        self.0.read(buffer).unwrap_or(0)
    }

    fn send(&mut self, bytes: &[u8]) {
        let _ = self.0.write_all(bytes);
    }
}

/// The 58503B's ACIA takes an external engine's answers, each one a
/// burst of bytes onto the line at once, as they come over TCP, at the
/// line's rate: the firmware reads `@@Ea` and `@@Cj` the first time
/// and goes on to the self-test and the 58503B's `@@Ta`, where with
/// every byte but the first lost to an overrun it sent each query again
/// after its timeout (oncore-emu's report).
#[test]
fn the_58503bs_acia_takes_an_external_engines_answers() {
    /// Board time a step: the engine answers at each step's end.
    const STEP: u64 = PICOSECONDS_PER_SECOND / 100;
    /// Board time by which the firmware sends `@@Ta` (12 to 13 s with
    /// the synthetic engine on the board, whose self-test answers six
    /// seconds after its command), short of its 11 s timeout's resend.
    const BY: u64 = 14 * PICOSECONDS_PER_SECOND;
    let mut config = config(
        "third_party/firmware/images/58503b-1.01.04.bin",
        "gps-58503b",
    );
    config.gps = GpsSource::External("127.0.0.1:0".to_owned());
    let start = config.synthetic.start;
    let loaded = Loaded::new(&config, Rc::new(Disk)).expect("loaded");
    let powered = Powered::up(&config, &loaded, Carried::default()).expect("powered");
    let mut ports = GpsClient::default();
    let eeprom = working_file(&config).to_owned();
    let mut runner = Runner::new(config, loaded, powered, &mut ports).expect("a runner");
    let mut engine = Synthetic::new(
        Client(ports.far.expect("the GPS port served")),
        GpsConfig {
            position: FIRMWARE_DEFAULT,
            start,
            leap_seconds: Seconds(GPS_LESS_UTC),
            engine: &B4121,
            options: Options::DEFAULT,
        },
        false,
    )
    .expect("an engine");
    let heard = Rc::new(RefCell::new(Vec::new()));
    let kept = Rc::clone(&heard);
    engine.on_heard(Box::new(move |bytes| {
        kept.borrow_mut().extend_from_slice(bytes)
    }));
    for step in 1..=BY / STEP {
        let (next, outcome) = runner
            .step_until(Some(Time::from_picos(step * STEP)))
            .expect("ran");
        assert!(matches!(outcome, Step::Running { .. }), "{outcome:?}");
        runner = next;
        engine.advance(runner.live().clocks().cycle);
    }
    let heard = heard.borrow();
    let count = |command: &[u8]| {
        heard
            .windows(command.len())
            .filter(|w| *w == command)
            .count()
    };
    assert_eq!(count(b"@@Ea\x00"), 1, "{}", heard.escape_ascii());
    assert_eq!(count(b"@@Cj"), 2, "{}", heard.escape_ascii());
    assert_eq!(count(b"@@Ta"), 1, "{}", heard.escape_ascii());
    remove_eeprom(&eeprom);
}

#[test]
fn the_sci_options_are_refused_on_a_board_without_an_sci() {
    let capture = std::env::temp_dir().join(format!(
        "smartclock-emu-board-file-capture-{}.bin",
        std::process::id()
    ));
    for option in ["--sci", "--sci-capture"] {
        let mut config = config(HP58503A, "no-sci");
        config.host = HostPort::Tcp("127.0.0.1:0".to_owned());
        if option == "--sci" {
            config.sci = Some("127.0.0.1:0".to_owned());
        } else {
            config.sci_capture = Some(capture.clone());
        }
        let eeprom = working_file(&config).to_owned();
        let error = runner(config).expect_err(option).to_string();
        assert!(error.starts_with(option), "{error}");
        assert!(error.contains("no `sci` endpoint"), "{error}");
        remove_eeprom(&eeprom);
    }
    let mut config = config(HP58503A, "no-sci");
    config.host = HostPort::Tcp("127.0.0.1:0".to_owned());
    let eeprom = working_file(&config).to_owned();
    drop(runner(config).expect("no SCI asked for"));
    remove_eeprom(&eeprom);
}

/// A power cycle builds the board again from the board file and its
/// images as the run read them at its start: a copy of the Z3805A's,
/// removed from the disk once the board is up.  The EEPROM's
/// files, its working file and the dump, stay: its part opens them at
/// each power-up.
#[test]
fn a_power_cycle_builds_the_board_from_what_was_read_at_the_start() {
    let copy = std::env::temp_dir().join(format!(
        "smartclock-emu-board-file-copied-{}",
        std::process::id()
    ));
    let image = "third_party/firmware/images/z3805a-3543b.bin";
    for file in ["boards/z3805a.toml", image] {
        let to = copy.join(file);
        std::fs::create_dir_all(to.parent().expect("a directory")).expect("made");
        std::fs::copy(Path::new(ROOT).join(file), &to).expect("copied");
    }
    let board = copy.join("boards/z3805a.toml");
    let dump = format!("{ROOT}/third_party/firmware/images/eeprom/z3805a-3625A01487-eeprom.bin");
    let mut config = config(image, "copied");
    config.eeprom = Eeprom::File {
        path: working_file(&config).to_owned(),
        dump: Some(dump.into()),
    };
    config.image_path = copy.join(image);
    config.board = board;
    let loaded = Loaded::new(&config, Rc::new(Disk)).expect("loaded");
    let mut powered = Powered::up(&config, &loaded, Carried::default()).expect("built");
    powered
        .live()
        .run_until(Time::ZERO + Span::from_millis(100))
        .expect("no part stalls");
    std::fs::remove_dir_all(&copy).expect("removed");
    let mut powered = powered
        .power_cycled(&config, &loaded)
        .unwrap_or_else(|error| panic!("built again from what was read: {error:#}"));
    assert_eq!(powered.live().now(), Time::ZERO, "powered up again");
    drop(powered);
    remove_eeprom(working_file(&config));
}

/// The synthetic engine's UTC at a power-up is the run's start and the
/// whole seconds of board time since, not of the chip's clocks, which
/// fell behind at each power-up.
#[test]
fn the_engines_start_after_a_power_cycle_counts_board_time() {
    let start = jiff::Timestamp::from_second(START_SECONDS).expect("a time");
    let carried = Carried {
        epoch: Epoch {
            cycles: Cycles(CPU_HZ),
            time: Time::ZERO + Span::from_millis(2500),
        },
        ..Carried::default()
    };
    let expected = jiff::Timestamp::from_second(START_SECONDS + 2).expect("a time");
    assert_eq!(carried.gps_start(start).expect("a time"), expected);
}

/// `files` with the file at `path` too, read from the disk now.
fn from_disk(files: InMemory, path: &Path) -> InMemory {
    files.with(path, std::fs::read(path).expect("the file"))
}

/// The 58503A's EEPROM's name in its board file.
const EEPROM: &str = "eeprom";
/// The bench 58503A's EEPROM dump, its board file's image.
const HP58503A_DUMP: &str = "third_party/firmware/images/eeprom/58503a-3710A01056-eeprom.bin";

/// The session on the EEPROM in memory: the prompt, a power cycle, the
/// prompt again, then the identity, its serial number the dump's,
/// carried through the power cycle in the EEPROM's cells.
const MEMORY_SESSION: &str = "expect scpi >\\x20 within 5
event power-cycle
expect scpi >\\x20 within 5
at 12
send *IDN?
expect 3710A01056 within 5
stop
";

/// The longest the session may take, board seconds.
const MEMORY_LIMIT: f64 = 40.0;

/// The 58503A's run with its EEPROM in memory, named `name`.
fn in_memory(name: &str, protected: Option<bool>) -> RunConfig {
    let mut config = config(HP58503A, name);
    config.eeprom = Eeprom::Memory { protected };
    config.check().expect("a run");
    config
}

/// `board`'s EEPROM.
fn eeprom_of(board: &Board<Mc68331>) -> Ref<'_, At28c64b> {
    board.peripheral::<At28c64b>(EEPROM).expect("the EEPROM")
}

/// The 58503A boots with its EEPROM in memory alone, from the bench
/// unit's dump read through the host's files, and answers with that
/// dump's serial number after a power cycle, which carried its cells;
/// every file the run reads comes from memory, and none is written.
#[test]
fn an_eeprom_in_memory_boots_the_58503a_from_its_dump() {
    let dump = std::fs::read(Path::new(ROOT).join(HP58503A_DUMP)).expect("the dump");
    let mut config = in_memory("memory", None);
    let script = PathBuf::from("/memory/session.script");
    let files = [
        config.board.clone(),
        config.image_path.clone(),
        Path::new(ROOT).join(HP58503A_DUMP),
    ]
    .iter()
    .fold(InMemory::default(), |files, path| from_disk(files, path))
    .with(&script, MEMORY_SESSION.as_bytes().to_vec());
    config.host = HostPort::Script(script);
    config.pacing = crate::run::Pacing::Free;
    config.until = Some(StopAfter::new(MEMORY_LIMIT).expect("a span"));
    let loaded = Loaded::new(&config, Rc::new(files)).expect("loaded from memory");
    let mut powered = Powered::up(&config, &loaded, Carried::default()).expect("built from memory");
    assert_eq!(
        eeprom_of(powered.live().board()).cells(),
        dump,
        "the dump's cells at power-up"
    );
    let runner = Runner::new(config, loaded, powered, &mut Unserved).expect("bound");
    let (mut runner, step) = runner
        .step_until(None)
        .unwrap_or_else(|error| panic!("the session: {error:#}"));
    assert_eq!(step, Step::Ended);
    let ended = runner.live().run_time();
    assert!(
        ended.picos() < (MEMORY_LIMIT * PICOSECONDS_PER_SECOND as f64) as u64,
        "the session stopped it, its expects met, at {ended:?}"
    );
    let kept = runner.eeprom().expect("the EEPROM").expect("in memory");
    assert_eq!(kept.cells.len(), dump.len(), "the part's cells");
    assert!(!kept.protected);
}

/// A power cycle gives the EEPROM in memory the cells and the
/// protection it carried, over the board file's image and the run's
/// protection; and the protection the run starts with survives it.
#[test]
fn a_power_cycle_carries_the_eeproms_cells_and_protection() {
    let config = in_memory("memory-protected", Some(true));
    let loaded = Loaded::new(&config, Rc::new(Disk)).expect("loaded");
    let powered = Powered::up(&config, &loaded, Carried::default()).expect("built");
    let mut powered = powered.power_cycled(&config, &loaded).expect("built again");
    let live = powered.live();
    let part = eeprom_of(live.board());
    assert!(part.protected(), "the protection survives a power cycle");
    let dump = std::fs::read(Path::new(ROOT).join(HP58503A_DUMP)).expect("the dump");
    assert_eq!(part.cells(), dump, "the cells survive it");
    drop(part);
    drop(powered);

    let cells = vec![0x5a; 16];
    let carried = Carried {
        eeprom: Some(KeptEeprom {
            cells: cells.clone(),
            protected: false,
        }),
        ..Carried::default()
    };
    let (board, _) = loaded.build(&config, &carried).expect("built");
    let part = eeprom_of(&board);
    assert_eq!(part.cells()[..cells.len()], cells, "the cells carried");
    assert!(
        part.cells()[cells.len()..]
            .iter()
            .all(|&cell| cell == ERASED)
    );
    assert!(!part.protected(), "the protection carried, not the run's");
}

/// In memory the 58503A's EEPROM has no working file among its
/// parameters, so that the part opens no file, and keeps its dump.
#[test]
fn an_eeprom_in_memory_names_no_file_and_keeps_its_dump() {
    let text = std::fs::read_to_string(Path::new(ROOT).join("boards/58503a.toml")).expect("read");
    let mut file: toml::Table = toml::from_str(&text).expect("TOML");
    let parameters = |file: &toml::Table| {
        file["peripheral"]
            .as_array()
            .expect("peripherals")
            .iter()
            .find(|device| device["name"].as_str() == Some(EEPROM))
            .expect("the EEPROM")["parameters"]
            .as_table()
            .expect("its parameters")
            .clone()
    };
    let working = toml::Value::String("working.bin".to_owned());
    file["peripheral"]
        .as_array_mut()
        .expect("peripherals")
        .iter_mut()
        .find(|device| device["name"].as_str() == Some(EEPROM))
        .expect("the EEPROM")["parameters"]
        .as_table_mut()
        .expect("its parameters")
        .insert("file".to_owned(), working);
    assert!(parameters(&file).contains_key("file"));
    eeprom::in_memory(&mut file);
    let memory = parameters(&file);
    assert!(memory.contains_key("image") && !memory.contains_key("file"));
}

/// The bench Z3805A's and Z3801A's dumps, and the 58503B's.
const Z3805A_DUMP: &str = "third_party/firmware/images/eeprom/z3805a-3625A01487-eeprom.bin";
const Z3801A_DUMP: &str = "third_party/firmware/images/eeprom/z3801a-3542A01548-eeprom.bin";

/// The model a board file's image runs as: its own, or for an image of
/// a board the emulator has no model for, the one its tests give it
/// (boards/README.md).
fn model_for(image: &[u8], board: &str) -> Model {
    Model::of_image(image).unwrap_or(if board.starts_with("z3815a") {
        Model::Z3816A
    } else {
        Model::Z3805A
    })
}

/// The oscillator's frequency at power-up on the board `config` asks
/// for, built from `files` with what `carried` carries: before the EFC's
/// first strobe, at its middle, so its offset.
fn offset_at_power_up(config: &RunConfig, files: Rc<dyn Files>, carried: &Carried) -> f64 {
    let loaded = Loaded::new(config, files).expect("loaded");
    let (board, parts) = loaded.build(config, carried).expect("built");
    frequency_now(&board, &parts.ocxo.expect("an oscillator")).expect("its frequency")
}

/// Every board file's oscillator, at the gain it runs at, is on
/// frequency at the EFC its EEPROM image's lifetime record gives the
/// firmware (docs/emulator.md, "The oscillator the EEPROM describes"),
/// and so off it by nothing on a synthetic image, whose record is
/// erased, unless the board file gives its offset, as none does; on the
/// dumps it is the offset the board files and the 58503B's
/// profile gave by hand before.
#[test]
fn each_boards_oscillator_is_on_frequency_at_its_eeproms_lifetime_efc() {
    let by_hand = [
        ("z3805a", -1.405_697_265_625e-7),
        ("z3801a", -2.032_432_812_5e-7),
        ("58503b", 7.518_685_433_769_562e-8),
    ];
    let mut files: Vec<_> = std::fs::read_dir(Path::new(ROOT).join("boards"))
        .expect("the boards")
        .map(|entry| entry.expect("an entry").path())
        .filter(|path| {
            path.extension()
                .is_some_and(|extension| extension == "toml")
        })
        .collect();
    files.sort();
    for path in files {
        let text = std::fs::read_to_string(&path).expect("the board file");
        let file: Table = toml::from_str(&text).expect("TOML");
        let name = file["board"]["name"].as_str().expect("its name");
        let directory = path.parent().expect("its directory");
        let image = directory.join(file["board"]["images"][0].as_str().expect("an image"));
        let bytes = std::fs::read(&image).expect("the image");
        let model = model_for(&bytes, name);
        let eeprom = Eeprom::File {
            path: std::env::temp_dir().join(format!(
                "smartclock-emu-lifetime-{name}-{}.bin",
                std::process::id()
            )),
            dump: None,
        };
        remove_eeprom(eeprom.working_file().expect("a working file"));
        let start = "2026-10-22T12:00:00Z".parse().expect("a time");
        let mut config =
            RunConfig::new(image.clone(), bytes, Some(model), eeprom, start).expect("a run");
        config.core = Core::Mc683xx;
        config.board = path.clone();
        let device = |kind: &str| {
            file["peripheral"]
                .as_array()
                .expect("its parts")
                .iter()
                .find(|device| device["type"].as_str() == Some(kind))
                .map(|device| &device["parameters"])
        };
        let eeprom = device("at28c64b").expect("an EEPROM")["image"]
            .as_str()
            .expect("its image");
        let cells = std::fs::read(directory.join(eeprom)).expect("the EEPROM's image");
        let oscillator = device("ocxo").expect("an oscillator");
        let term = |key: &str| oscillator.get(key).and_then(toml::Value::as_float);
        let gain = term("efc_gain").unwrap_or(model.efc_gain());
        let offset = offset_at_power_up(&config, Rc::new(Disk), &carried());
        if let Some(given) = term("offset") {
            assert_eq!(offset, given, "{name}: the board file's");
        } else {
            assert_eq!(offset, on_frequency_at(gain, start_efc(&cells)), "{name}");
            let valid = start_efc(&cells) != MIDDLE_COUNTS;
            assert_eq!(offset != 0.0, valid, "{name}: {offset:e}");
        }
        if let Some((_, by_hand)) = by_hand.iter().find(|(board, _)| *board == name) {
            assert!(
                (offset - by_hand).abs() <= f64::EPSILON * by_hand.abs(),
                "{name}: {offset:e}"
            );
        }
        remove_eeprom(working_file(&config));
    }
}

/// The offset is the EEPROM's the run starts with, whatever gives it:
/// another unit's dump, an erased part, the working file a run before
/// left, which the dump does not replace; the gain the run gives; and
/// the offset the run gives in its place.
#[test]
fn the_oscillators_offset_is_the_working_eeproms() {
    let z3801a = std::fs::read(Path::new(ROOT).join(Z3801A_DUMP)).expect("the dump");
    let z3801a_offset = on_frequency_at(EFC_GAIN, start_efc(&z3801a));
    let mut config = config("third_party/firmware/images/z3805a-3543b.bin", "working");
    let path = working_file(&config).to_owned();
    let dump = |file: &str| Some(Path::new(ROOT).join(file));
    let offset = |config: &RunConfig| offset_at_power_up(config, Rc::new(Disk), &carried());
    config.eeprom = Eeprom::File {
        path: path.clone(),
        dump: dump(Z3801A_DUMP),
    };
    assert_eq!(offset(&config), z3801a_offset, "another unit's dump");
    // the working file the run left, from the Z3801A's dump, over the
    // Z3805A's
    config.eeprom = Eeprom::File {
        path: path.clone(),
        dump: dump(Z3805A_DUMP),
    };
    assert_eq!(offset(&config), z3801a_offset, "the working file");
    remove_eeprom(&path);
    let erased = std::env::temp_dir().join(format!(
        "smartclock-emu-erased-dump-{}.bin",
        std::process::id()
    ));
    std::fs::write(&erased, []).expect("an empty dump");
    config.eeprom = Eeprom::File {
        path: path.clone(),
        dump: Some(erased.clone()),
    };
    assert_eq!(offset(&config), 0.0, "an erased part");
    remove_eeprom(&path);
    std::fs::remove_file(&erased).expect("removed");
    config.eeprom = Eeprom::File {
        path: path.clone(),
        dump: dump(Z3801A_DUMP),
    };
    const GAIN: f64 = 4e-13;
    config.ocxo.efc_gain = Some(GAIN);
    assert_eq!(
        offset(&config),
        on_frequency_at(GAIN, start_efc(&z3801a)),
        "the run's gain"
    );
    remove_eeprom(&path);
    const OFFSET: f64 = 3e-8;
    config.ocxo.offset = Some(OFFSET);
    assert_eq!(offset(&config), OFFSET, "the run's offset");
    remove_eeprom(&path);
}

/// In memory the offset is the image's the host gives, and a power cycle
/// keeps it, the oscillator being the same, whatever the cells it
/// carries hold: here erased.
#[test]
fn a_power_cycle_keeps_the_offset_the_run_started_with() {
    let dump = std::fs::read(Path::new(ROOT).join(HP58503A_DUMP)).expect("the dump");
    let config = in_memory("lifetime-memory", None);
    let files: Rc<dyn Files> = Rc::new(
        [config.board.clone(), config.image_path.clone()]
            .iter()
            .fold(InMemory::default(), |files, path| from_disk(files, path))
            .with(&Path::new(ROOT).join(HP58503A_DUMP), dump.clone()),
    );
    let wanted = on_frequency_at(EFC_GAIN, start_efc(&dump));
    assert_ne!(wanted, 0.0, "the dump's record");
    let loaded = Loaded::new(&config, files).expect("loaded");
    let erased = Carried {
        eeprom: Some(KeptEeprom {
            cells: vec![ERASED; dump.len()],
            protected: false,
        }),
        ..carried()
    };
    for carried in [carried(), erased] {
        let (board, parts) = loaded.build(&config, &carried).expect("built");
        let offset = frequency_now(&board, &parts.ocxo.expect("an oscillator"));
        assert_eq!(offset, Some(wanted));
    }
}
