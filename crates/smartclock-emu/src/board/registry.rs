//! smartclock's bus and sink types for mc683xx-board's registry
//! (docs/emulator.md, "The mc683xx device layer"), beside
//! mc683xx-peripherals' stock chips, which the board's DUART or ACIA,
//! EEPROM, latch, switch byte, ADC and DACs are.  Here is what is the board's
//! own: the EFC's strobe, its bus side the model's (strobe.rs); and the
//! parts the nets end at with nothing modeled behind them, the lamps,
//! the LEDs and the 1 PPS buffers.  The lamps and the LEDs report
//! their nets' changes as `--indicators` gives them (indicators.rs):
//! an indicator is what the board's net shows, whatever drives it
//! (PLAN.md, "The emulator's indicators stay observable from
//! outside").
//!
//! The models count time in the CPU's cycles at the board's fixed
//! 16.777216 MHz (`time`); a part's calls carry board time,
//! picoseconds, converted here.

use mc683xx_board::Board;
use mc683xx_board::adapter::MappedAdapter;
use mc683xx_board::peripheral::Drive;
use mc683xx_board::peripheral::Peripheral;
use mc683xx_board::registry::Context;
use mc683xx_board::registry::ParameterError;
use mc683xx_board::registry::PortSpec;
use mc683xx_board::registry::Registry;
use mc683xx_board::registry::RegistryError;
use mc683xx_board::signal::Kind;
use mc683xx_board::signal::Signal;
use mc683xx_cpu32::state::StateError;
use mc683xx_mc68331::Mc68331;

use super::duart;
use super::fpga_part;
use super::participants;
use super::snapshot;
use super::strobe::Strobe;
use super::system_clock::cycles;
use super::time::Cycles;
use crate::indicators::Change;

/// The EEPROM's type, mc683xx-peripherals' AT28C64B, whose `file` and
/// `image` the run gives (`mc683xx`).
pub(crate) const AT28C64B: &str = "at28c64b";
/// The FPGA's (`fpga_part.rs`).
pub(crate) const XC3042: &str = "xc3042";
/// The EFC's strobe's.
pub(crate) const EFC_STROBE: &str = "efc-strobe";
/// The parts the board's nets end at with nothing modeled behind them:
/// the lamp board, the LEDs, the 1 PPS output buffers.
pub(crate) const LAMP_BOARD: &str = "lamp-board";
pub(crate) const LED_BANK: &str = "led-bank";
pub(crate) const PPS_BUFFERS: &str = "pps-buffers";

/// The lamp board's and the LED bank's: report the changes, as
/// `--indicators` does.
const REPORT: &str = "report";
/// The LED bank's: how many LEDs.
const COUNT: &str = "count";
/// The indicators' banks in reports, the Unicorn board's (latch.rs,
/// duart.rs).
const LAMPS: &str = "lamp";
const LEDS: &str = "led";

/// The lamps a lamp board may have, the Z3805A's and the 58503A's
/// (profile.rs), each a logic input.
const LAMP_NAMES: [&str; 5] = ["enabled", "active", "alarm", "gps_lock", "holdover"];

/// Registers every smartclock device type: the bus devices and sinks
/// here, the QSPI's and the off-bus models (participants/), the FPGA
/// and the interpolator (`fpga_part`); then
/// mc683xx-peripherals' stock chips, over which a type of smartclock's
/// of the same name would take precedence.
pub(crate) fn register_all(registry: &mut Registry) -> Result<(), RegistryError> {
    register_own(registry)?;
    mc683xx_peripherals::register(registry)
}

/// Registers smartclock's own types, `register_all`'s without the stock
/// chips.
fn register_own(registry: &mut Registry) -> Result<(), RegistryError> {
    register(registry)?;
    participants::register(registry)?;
    fpga_part::register(registry)
}

/// The LEDs' names, in port order: the DUART's output port's, OP2 to
/// OP7, as `board_led` (smartclock.h) names them (duart.rs).
fn led_names() -> impl Iterator<Item = &'static str> {
    duart::LEDS.iter().map(|led| led.name)
}

/// Registers smartclock's bus device types and the sinks.
pub(crate) fn register(registry: &mut Registry) -> Result<(), RegistryError> {
    registry.register(
        EFC_STROBE,
        vec![PortSpec::output("out", Kind::Logic)],
        |_| Ok(Box::new(MappedAdapter::new(Strobe::default()).driving()) as Box<dyn Peripheral>),
    )?;
    registry.register(
        LAMP_BOARD,
        LAMP_NAMES
            .iter()
            .map(|name| PortSpec::input(*name, Kind::Logic))
            .collect(),
        |context| {
            let lamps = Indicators::new(context, LAMPS, LAMP_NAMES.to_vec())?;
            Ok(Box::new(lamps) as Box<dyn Peripheral>)
        },
    )?;
    registry.register(
        LED_BANK,
        led_names()
            .map(|name| PortSpec::input(name, Kind::Logic))
            .collect(),
        |context| {
            let leds = led_names().count();
            let count: usize = context.parameters.integer(COUNT)?.unwrap_or(leds);
            if count > leds {
                return Err(ParameterError::Range {
                    key: COUNT.to_owned(),
                    value: i64::try_from(count).unwrap_or(i64::MAX),
                });
            }
            // the first `count` LEDs fitted, the rest's nets ending at nothing
            let names = led_names().take(count).collect();
            Ok(Box::new(Indicators::new(context, LEDS, names)?) as Box<dyn Peripheral>)
        },
    )?;
    registry.register(PPS_BUFFERS, vec![PortSpec::input("in", Kind::Edge)], |_| {
        Ok(Box::new(Sink) as Box<dyn Peripheral>)
    })
}

/// A part whose nets end at it, with nothing behind them modeled.
#[derive(Debug)]
struct Sink;

impl Peripheral for Sink {
    fn save(&self) -> Result<Vec<u8>, StateError> {
        Ok(snapshot::save(&()))
    }

    fn restore(&mut self, snapshot: &[u8]) -> Result<(), StateError> {
        snapshot::load::<()>(snapshot)
    }
}

/// A bank of indicators, each the end of its own logic net, lit while
/// the net is high: the lamp board's lamps on the latch's outputs, the
/// LEDs on the DUART's output pins.  With `report`, what is lit at
/// power-on and each change after are reported, as the Unicorn board's
/// banks report theirs.
#[derive(Debug)]
struct Indicators {
    /// The bank's name in reports.
    bank: &'static str,
    /// Each port's indicator.
    names: Vec<&'static str>,
    /// Each port's level as its net last drove it, none before.
    levels: Vec<Option<bool>>,
    /// Whether its changes are reported: the run's, not saved.
    report: bool,
}

impl Indicators {
    /// The bank `bank` of the indicators `names`, one a port, reported
    /// with `report` (false).
    fn new(
        context: &Context<'_>,
        bank: &'static str,
        names: Vec<&'static str>,
    ) -> Result<Self, ParameterError> {
        Ok(Self {
            bank,
            levels: vec![None; names.len()],
            names,
            report: context.parameters.boolean(REPORT)?.unwrap_or(false),
        })
    }
}

impl Indicators {
    /// Each indicator whose net has been driven, at `at`, lit while
    /// the net is high: a port the board leaves unwired is not shown.
    fn shown(&self, at: Cycles) -> Vec<Change> {
        self.names
            .iter()
            .zip(&self.levels)
            .filter_map(|(&name, level)| {
                level.map(|lit| Change {
                    at,
                    bank: self.bank,
                    name,
                    lit,
                })
            })
            .collect()
    }
}

/// Each driven indicator of `board`'s bank `name`, a lamp board or an
/// LED bank, at `at`: none if the board has no such bank.
pub(crate) fn indicators(board: &Board<Mc68331>, name: &str, at: Cycles) -> Option<Vec<Change>> {
    board
        .peripheral::<Indicators>(name)
        .ok()
        .map(|bank| bank.shown(at))
}

impl Peripheral for Indicators {
    fn save(&self) -> Result<Vec<u8>, StateError> {
        Ok(snapshot::save(&self.levels))
    }

    fn restore(&mut self, snapshot: &[u8]) -> Result<(), StateError> {
        let levels: Vec<Option<bool>> = snapshot::load(snapshot)?;
        if levels.len() != self.names.len() {
            return Err(StateError::mismatch("an indicator bank's size"));
        }
        self.levels = levels;
        Ok(())
    }

    fn input(&mut self, port: usize, signal: Signal, drive: &mut Drive<'_>) {
        let now = drive.now();
        let (Signal::Level(lit), Some(level), Some(&name)) =
            (signal, self.levels.get_mut(port), self.names.get(port))
        else {
            return;
        };
        let was = level.replace(lit);
        // the first level is reported only lit, as a bank's at power-up
        if self.report && was.map_or(lit, |was| was != lit) {
            crate::trace::emit(&Change {
                at: cycles(drive, now),
                bank: self.bank,
                name,
                lit,
            });
        }
    }
}

#[cfg(test)]
mod tests {
    use std::path::Path;
    use std::path::PathBuf;

    use mc683xx_board::Description;
    use mc683xx_board::Environment;
    use mc683xx_board::Options;
    use mc683xx_board::peripheral::Driven;
    use mc683xx_board::peripheral::Lane;
    use mc683xx_board::peripheral::Peripheral;
    use mc683xx_board::peripheral::PeripheralAccess;
    use mc683xx_board::peripheral::Role;
    use mc683xx_board::registry::Constructed;
    use mc683xx_board::registry::Context;
    use mc683xx_board::registry::ParameterError;
    use mc683xx_board::registry::Parameters;
    use mc683xx_board::registry::Registry;
    use mc683xx_board::signal::Direction;
    use mc683xx_board::signal::Kind;
    use mc683xx_board::signal::Signal;
    use mc683xx_board::time::Time;
    use mc683xx_cpu32::Cycle;
    use mc683xx_modules::chip_select::ChipSelect;
    use mc683xx_modules::chip_select::Number;
    use mc683xx_target::FunctionCode;
    use mc683xx_target::Size;

    use super::AT28C64B;
    use super::EFC_STROBE;
    use super::LAMP_BOARD;
    use super::LED_BANK;
    use super::PPS_BUFFERS;
    use super::register;
    use super::register_all;
    use super::register_own;
    use crate::board::system_clock::test_base;
    use crate::board::system_clock::test_drive;

    /// The strobe's select, chip select 5's.
    const CS5: u8 = 5;

    /// The registry with smartclock's bus types.
    fn registry() -> Registry {
        let mut registry = Registry::new();
        register(&mut registry).expect("registered");
        registry
    }

    /// A part of `kind` built from the parameter table `table`.
    fn construct(kind: &str, table: &str) -> Result<Constructed, ParameterError> {
        let parameters = Parameters::parse(table).expect("a table");
        registry()
            .construct(
                kind,
                &Context::new(kind, &parameters, &Environment::default()),
            )
            .map_err(|error| {
                error
                    .into_parameter()
                    .unwrap_or_else(|error| panic!("{error}"))
            })
    }

    /// A part of `kind` from `table`.
    fn part(kind: &str, table: &str) -> Box<dyn Peripheral> {
        construct(kind, table).expect("constructed").peripheral
    }

    /// The ports `kind` declares, as name, kind and direction.
    fn ports(kind: &str) -> Vec<(String, Kind, Direction)> {
        registry()
            .ports(kind)
            .expect("fixed ports")
            .iter()
            .map(|port| (port.name.clone(), port.kind, port.direction))
            .collect()
    }

    /// What a byte write at `address` at `now` drove, supervisor data.
    fn write(part: &mut dyn Peripheral, address: u32, now: Time) -> Vec<Driven> {
        let select = Number::try_from(CS5).expect("a select");
        let access = PeripheralAccess::new(
            ChipSelect::Numbered(select),
            address,
            Size::Byte,
            FunctionCode::SupervisorData,
        )
        .through(0, Role::Write, Lane::Both);
        let mut drive = test_drive(now);
        part.write(access, 0, &mut drive).expect("written");
        drive.finish().driven
    }

    #[test]
    fn the_types_are_registered_by_name_and_the_stock_chips_after() {
        let names: Vec<String> = registry().names().map(str::to_owned).collect();
        assert_eq!(names, [EFC_STROBE, LAMP_BOARD, LED_BANK, PPS_BUFFERS]);
        let mut all = Registry::new();
        register_all(&mut all).expect("registered");
        for stock in ["74x244", "74x374", AT28C64B, "ad569", "adc0838", "scc68692"] {
            assert!(all.contains(stock), "{stock}");
        }
    }

    /// The repository's board files.
    const BOARDS: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../../boards");
    /// How many: one a model, the Z3817A's among them
    /// (boards/README.md).
    const BOARD_FILES: usize = 8;

    /// The device types the board files name that neither registry has
    /// yet: none since the Z3815A's GPS engine, `furuno-gt74`, was
    /// written (boards/README.md).
    const UNWRITTEN_TYPES: [&str; 0] = [];

    /// The repository's board files, in order.
    fn board_files() -> Vec<PathBuf> {
        let mut files: Vec<_> = std::fs::read_dir(BOARDS)
            .expect("the boards")
            .map(|entry| entry.expect("an entry").path())
            .filter(|path| {
                path.extension()
                    .is_some_and(|extension| extension == "toml")
            })
            .collect();
        files.sort();
        assert_eq!(files.len(), BOARD_FILES, "{files:?}");
        files
    }

    /// The board file at `path`, as a table.
    fn board_file(path: &Path) -> toml::Table {
        let text = std::fs::read_to_string(path).expect("the board file");
        toml::from_str(&text).expect("TOML")
    }

    /// Each of `file`'s devices: its name, type and parameters.
    fn devices(file: &toml::Table) -> Vec<(&str, &str, toml::Table)> {
        file.get("peripheral")
            .and_then(toml::Value::as_array)
            .into_iter()
            .flatten()
            .filter_map(|device| {
                let name = device.get("name")?.as_str()?;
                let kind = device.get("type")?.as_str()?;
                let parameters = device
                    .get("parameters")
                    .and_then(toml::Value::as_table)
                    .cloned()
                    .unwrap_or_default();
                Some((name, kind, parameters))
            })
            .collect()
    }

    /// The board files' environment: their directory.
    fn environment() -> Environment {
        let mut environment = Environment::default();
        environment.set_board_dir(BOARDS);
        environment
    }

    /// Device `name` of `kind` built from `table`, as the board's
    /// loader builds it, and the parameters it was built from.
    fn build(
        path: &Path,
        registry: &Registry,
        (name, kind, table): &(&str, &str, toml::Table),
    ) -> (Parameters, Constructed) {
        let parameters =
            Parameters::parse(&toml::to_string(table).expect("TOML")).expect("a table");
        let constructed = registry
            .construct(kind, &Context::new(name, &parameters, &environment()))
            .unwrap_or_else(|error| panic!("{}: {name}: {error}", path.display()));
        (parameters, constructed)
    }

    /// Every board file loads as a description, and its stock chips
    /// and mc683xx-board's built-ins are built from their parameters,
    /// each key read: what the board's loader asks of them, for the
    /// files not yet run (`--core mc683xx` runs the Z3805A's, the
    /// 58503A's and the Z3801A's).  A stock type smartclock registers
    /// too is smartclock's, which the loader builds instead.
    #[test]
    fn every_board_files_stock_chips_take_their_parameters() {
        let mut stock = Registry::with_builtins();
        mc683xx_peripherals::register(&mut stock).expect("registered");
        let mut own = Registry::new();
        register_own(&mut own).expect("registered");
        for path in board_files() {
            Description::read(&path, &Options::default())
                .unwrap_or_else(|error| panic!("{}: {error}", path.display()));
            let file = board_file(&path);
            for device in devices(&file) {
                if stock.contains(device.1) && !own.contains(device.1) {
                    let (parameters, _) = build(&path, &stock, &device);
                    assert_eq!(
                        parameters.unread(),
                        None,
                        "{}: {}",
                        path.display(),
                        device.0
                    );
                }
            }
        }
    }

    /// Every net in every board file ends at a port its device has, of
    /// the net's type and facing its way, smartclock's types, the stock
    /// chips and mc683xx-board's built-ins alike, every option fitted.
    #[test]
    fn every_board_files_nets_end_at_ports_their_devices_have() {
        let mut all = Registry::with_builtins();
        register_all(&mut all).expect("registered");
        for kind in UNWRITTEN_TYPES {
            assert!(!all.contains(kind), "{kind} is written: check its ports");
        }
        let mut options = Options::default();
        options.all_enabled = true;
        for path in board_files() {
            Description::read(&path, &options)
                .unwrap_or_else(|error| panic!("{}: {error}", path.display()))
                .check_ports(&all, &environment())
                .unwrap_or_else(|error| panic!("{}: {error}", path.display()));
        }
    }

    /// The spans smartclock's parts take a converter's voltages over, each
    /// part's key, and the stock converter's type, key and default.
    const SPANS: [(&str, &str, &str, &str, f64); 7] = [
        (
            "ocxo",
            "efc_vref_negative",
            "ad569",
            "vref_negative_volts",
            -5.0,
        ),
        (
            "ocxo",
            "efc_vref_positive",
            "ad569",
            "vref_positive_volts",
            5.0,
        ),
        ("ocxo", "adc_vref_volts", "adc0838", "vref_volts", 5.0),
        (
            "temperature-sensor",
            "adc_vref_volts",
            "adc0838",
            "vref_volts",
            5.0,
        ),
        (
            "supply-monitor",
            "adc_vref_volts",
            "adc0838",
            "vref_volts",
            5.0,
        ),
        (
            "smartclock-interpolator",
            "trim_vss",
            "mc144110",
            "vss_volts",
            0.0,
        ),
        (
            "smartclock-interpolator",
            "trim_vdd",
            "mc144110",
            "vdd_volts",
            5.0,
        ),
    ];

    /// Every board file gives its oscillator, sources and interpolator
    /// the span its stock converter has, so a reference changed on one
    /// is changed on the other.
    #[test]
    fn every_board_files_parts_take_their_converters_spans() {
        let value = |table: &toml::Table, key: &str| {
            table.get(key).and_then(|value| {
                value
                    .as_float()
                    .or_else(|| value.as_integer().map(|integer| integer as f64))
            })
        };
        for path in board_files() {
            let file = board_file(&path);
            let devices = devices(&file);
            let of_type = |kind: &str| devices.iter().find(|(_, other, _)| *other == kind);
            for (part, key, stock, stock_key, default) in SPANS {
                let Some((name, _, parameters)) = of_type(part) else {
                    continue;
                };
                let converter = of_type(stock).map_or(default, |(_, _, table)| {
                    value(table, stock_key).unwrap_or(default)
                });
                assert_eq!(
                    value(parameters, key),
                    Some(converter),
                    "{}: {name}'s {key}",
                    path.display()
                );
            }
        }
    }

    #[test]
    fn the_strobe_and_the_indicators_declare_their_levels() {
        assert_eq!(
            ports(EFC_STROBE),
            [("out".to_owned(), Kind::Logic, Direction::Out)]
        );
        let leds: Vec<String> = ports(LED_BANK).into_iter().map(|port| port.0).collect();
        assert_eq!(
            leds,
            ["heartbeat", "installer", "cpu", "ram", "duart", "selftest"]
        );
        assert!(
            ports(LAMP_BOARD)
                .iter()
                .all(|(_, kind, direction)| (*kind, *direction) == (Kind::Logic, Direction::In))
        );
    }

    /// A bank of fewer LEDs keeps only theirs: its state is not a full
    /// bank's.
    #[test]
    fn the_led_bank_has_count_leds() {
        let full = part(LED_BANK, "").save().expect("saved");
        let mut two = part(LED_BANK, "count = 2");
        assert!(two.restore(&full).is_err(), "six levels into two");
        let saved = two.save().expect("saved");
        two.restore(&saved).expect("its own");
    }

    #[test]
    fn the_led_bank_refuses_more_leds_than_it_names() {
        let error = construct(LED_BANK, "count = 7").expect_err("refused");
        assert!(matches!(error, ParameterError::Range { .. }), "{error}");
    }

    #[test]
    fn the_efc_strobe_pulses_low_on_a_write() {
        let mut strobe = part(EFC_STROBE, "");
        let now = test_base().time(Cycle::from_clocks(1000));
        let driven = write(strobe.as_mut(), 0, now);
        assert_eq!(
            driven
                .iter()
                .map(|driven| (driven.at, driven.signal))
                .collect::<Vec<_>>(),
            [
                (now, Signal::Level(false)),
                (
                    test_base().time(Cycle::from_clocks(1003)),
                    Signal::Level(true)
                )
            ]
        );
    }

    #[test]
    fn an_indicator_bank_keeps_its_levels_across_a_snapshot() {
        let mut leds = part(LED_BANK, "report = false");
        leds.input(0, Signal::Level(true), &mut test_drive(Time::ZERO));
        let mut copy = part(LED_BANK, "");
        copy.restore(&leds.save().expect("saved"))
            .expect("restored");
        assert_eq!(copy.save().expect("saved"), leds.save().expect("saved"));
        let lamps = part(LAMP_BOARD, "");
        assert!(
            copy.restore(&lamps.save().expect("saved")).is_err(),
            "another bank's size"
        );
    }
}
