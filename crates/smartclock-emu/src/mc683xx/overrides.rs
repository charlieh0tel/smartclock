//! The run's choices for the board file's parts: parameters of the
//! parts they belong to, merged over the file's by the loader
//! (`Options::parameters`), and the parts the runner reaches by name.

use std::collections::BTreeMap;
use std::path::Path;

use mc683xx_board::registry::ParameterTable;
use mc683xx_board::registry::ParameterValue;
use toml::Table;
use toml::Value;

use super::Error;
use super::carried::Carried;
use super::eeprom::MemoryEeprom;
use super::eeprom::image;
use super::load::PARAMETERS;
use super::load::PERIPHERAL;
use super::load::absolute;
use super::load::kind;
use super::load::name;
use crate::board::analog::Warmth;
use crate::board::eeprom::directory;
use crate::board::ocxo::on_frequency_at;
use crate::board::parameters::REPORT;
use crate::board::parameters::eeprom;
use crate::board::parameters::ocxo;
use crate::board::parameters::oncore;
use crate::board::parameters::warm_up;
use crate::board::participants;
use crate::board::registry;
use crate::config::Eeprom;
use crate::config::RunConfig;
use crate::files::Files;
use crate::gps::Engine;

/// The parts of a board the runner reaches by name, if it has them.
#[derive(Debug, Default, PartialEq, Eq)]
pub(super) struct Parts {
    /// The front panel, which a script's keys press.
    pub(super) front_panel: Option<String>,
    /// The lamp boards and the LED banks, which the panel page shows.
    pub(super) indicators: Vec<String>,
    /// The synthetic GPS engine, whose input a script's expect-gps
    /// steps look in and whose antenna the controls pull.
    pub(super) gps: Option<String>,
    /// The EEPROM, when it is in memory, whose cells and protection a
    /// power cycle carries.
    pub(super) eeprom: Option<MemoryEeprom>,
    /// The oscillator, the temperature inside and the oscillator's
    /// report, whose warm-up the skip warm-up control settles.
    pub(super) ocxo: Option<String>,
    pub(super) temperature: Option<String>,
    pub(super) efc_monitor: Option<String>,
}

/// The run-time parameters the command line gives the board's parts,
/// by peripheral name, each a table the loader merges over the file's own
/// (`Options::parameters`).
pub(super) type Overrides = BTreeMap<String, ParameterTable>;

/// The value of a path as a parameter.
fn path_value(files: &dyn Files, path: &Path) -> Result<ParameterValue, Error> {
    Ok(ParameterValue::String(
        absolute(files, path)?.display().to_string(),
    ))
}

/// Whether the board file's `device` gives its parameter `key`.
fn gives(device: &Table, key: &str) -> bool {
    device
        .get(PARAMETERS)
        .and_then(Value::as_table)
        .is_some_and(|parameters| parameters.contains_key(key))
}

/// The board file's `device`'s parameter `key`, if it gives it as a
/// float.
fn given_float(device: &Table, key: &str) -> Option<f64> {
    device
        .get(PARAMETERS)
        .and_then(|parameters| parameters.get(key))
        .and_then(Value::as_float)
}

/// `config`'s run-time choices for the parts of the board file `file`,
/// with what a power cycle `carried`, its paths as `files` has them,
/// the oscillator on frequency at `start_efc` unless the file or the
/// run gives its offset (docs/emulator.md, "The oscillator the EEPROM
/// describes"), and the parts the runner reaches.  Without
/// `--eeprom-dump` the EEPROM's working file starts from the board
/// file's image, the bench unit's dump; in memory it has no working file, and after a power
/// cycle its image is the cells carried, at the board file's image's
/// path or one of its own (`Loaded::build`), its protection the one
/// carried, or at the start the run's.
pub(super) fn overrides(
    file: &Table,
    config: &RunConfig,
    carried: &Carried,
    files: &dyn Files,
    start_efc: f64,
) -> Result<(Overrides, Parts), Error> {
    let mut overrides = Overrides::new();
    let mut parts = Parts::default();
    let gps = &config.synthetic;
    let gps_start = carried.gps_start(gps.start)?;
    let warm_up = |set: &mut dyn FnMut(&str, ParameterValue)| match carried.warmth {
        Warmth::Settled => set(warm_up::SETTLED, ParameterValue::Boolean(true)),
        Warmth::Warming { head_start } => set(warm_up::SECONDS, ParameterValue::Float(head_start)),
    };
    let devices = file
        .get(PERIPHERAL)
        .and_then(Value::as_array)
        .into_iter()
        .flatten()
        .filter_map(Value::as_table);
    for device in devices {
        let Some(name) = name(device) else {
            continue;
        };
        let mut parameters = ParameterTable::new();
        let mut set = |key: &str, value: ParameterValue| {
            parameters.insert(key.to_owned(), value);
        };
        match kind(device) {
            registry::AT28C64B => match &config.eeprom {
                // in memory, no working file: the file's is taken off (`load`)
                Eeprom::Memory { protected } => {
                    let memory = MemoryEeprom::new(name, image(device));
                    if carried.eeprom.is_some() {
                        set(
                            eeprom::IMAGE,
                            ParameterValue::String(memory.image.display().to_string()),
                        );
                    }
                    let protected = carried
                        .eeprom
                        .as_ref()
                        .map(|kept| kept.protected)
                        .or(*protected);
                    if let Some(protected) = protected {
                        set(eeprom::PROTECTED, ParameterValue::Boolean(protected));
                    }
                    parts.eeprom = Some(memory);
                }
                Eeprom::File { path, dump } => {
                    // the part makes its working file, not its directory
                    if let Some(directory) = directory(path) {
                        std::fs::create_dir_all(directory).map_err(|source| Error::Directory {
                            path: directory.to_owned(),
                            source,
                        })?;
                    }
                    set(eeprom::FILE, path_value(files, path)?);
                    if let Some(dump) = dump {
                        set(eeprom::IMAGE, path_value(files, dump)?);
                    }
                }
            },
            registry::LAMP_BOARD | registry::LED_BANK => {
                set(REPORT, ParameterValue::Boolean(config.indicators));
                parts.indicators.push(name.to_owned());
            }
            participants::FRONT_PANEL => {
                set(REPORT, ParameterValue::Boolean(config.indicators));
                parts.front_panel = Some(name.to_owned());
            }
            participants::OCXO => {
                // the model's gain where the board file gives none, and
                // the offset that puts it on frequency at the EEPROM's
                // starting EFC with the gain it runs at; the run's terms
                // over both
                let efc_gain = config.ocxo.efc_gain.unwrap_or_else(|| {
                    given_float(device, ocxo::EFC_GAIN).unwrap_or(config.model.efc_gain())
                });
                for (key, value) in [
                    (ocxo::OFFSET, on_frequency_at(efc_gain, start_efc)),
                    (ocxo::EFC_GAIN, efc_gain),
                ] {
                    if !gives(device, key) {
                        set(key, ParameterValue::Float(value));
                    }
                }
                for (key, value) in ocxo::given(&config.ocxo) {
                    set(key, ParameterValue::Float(value));
                }
                warm_up(&mut set);
                let crystal = carried.crystal;
                set(ocxo::AGING_SECONDS, ParameterValue::Float(crystal.aged));
                set(
                    ocxo::NOISE_DRAWS,
                    ParameterValue::Integer(i64::try_from(crystal.draws)?),
                );
                set(ocxo::RANDOM_WALK, ParameterValue::Float(crystal.walk));
                parts.ocxo = Some(name.to_owned());
            }
            participants::TEMPERATURE => {
                warm_up(&mut set);
                parts.temperature = Some(name.to_owned());
            }
            participants::EFC_MONITOR | participants::CLOCKED_EFC_MONITOR => {
                warm_up(&mut set);
                parts.efc_monitor = Some(name.to_owned());
            }
            kind if participants::GPS_TYPES.contains(&kind) => {
                set(oncore::START, ParameterValue::String(gps_start.to_string()));
                set(oncore::LATITUDE, ParameterValue::Float(gps.latitude));
                set(oncore::LONGITUDE, ParameterValue::Float(gps.longitude));
                set(oncore::HEIGHT, ParameterValue::Float(gps.height));
                set(
                    oncore::LEAP_SECONDS,
                    ParameterValue::Integer(i64::from(gps.leap_seconds)),
                );
                set(oncore::TAP, ParameterValue::Boolean(config.tap));
                set(oncore::ANTENNA, ParameterValue::Boolean(carried.antenna));
                if let Some(engine) = &config.gps_engine {
                    set(oncore::ENGINE, ParameterValue::String(engine.clone()));
                    // its protocol with it, whichever the file's part
                    // spoke; an unknown name the part refuses
                    if let Some(named) = Engine::named(engine) {
                        set(
                            oncore::PROTOCOL,
                            ParameterValue::String(named.family.protocol().to_owned()),
                        );
                    }
                }
                parts.gps = Some(name.to_owned());
            }
            _ => {}
        }
        if !parameters.is_empty() {
            overrides.insert(name.to_owned(), parameters);
        }
    }
    Ok((overrides, parts))
}
