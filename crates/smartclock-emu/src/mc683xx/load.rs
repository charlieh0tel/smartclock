//! The board built from the receiver's board file through mc683xx-board
//! with smartclock's device types, the run's choices merged over the
//! file's parameters (`overrides`), and `--gps`'s endpoint swapped in
//! for the synthetic engine.
//!
//! The file, its images and the FPGA's bitstream are read once, at the
//! run's start (`Loaded`), and the cells the EEPROM starts with, for the
//! oscillator its lifetime record describes, from the host's files
//! (`files::Files`), which
//! the board's parts read their file parameters through too
//! (`Board::load_with`, `Context::contents`), and each power-up's board
//! is built from what was read then: a power cycle reads nothing again
//! but the EEPROM's files, the dump its part, the stock AT28C64B, reads
//! through the host's files at each power-up, and the working file it
//! opens itself.  In memory (`eeprom`) it has no working file, and a
//! power cycle reads not even the dump: the part's image is the cells
//! carried.

use std::path::Path;
use std::path::PathBuf;
use std::rc::Rc;

use mc683xx_board::Board;
use mc683xx_board::Description;
use mc683xx_board::Environment;
use mc683xx_board::Options;
use mc683xx_board::registry::Registry;
use mc683xx_mc68331::Mc68331;
use toml::Table;
use toml::Value;

use super::Error;
use super::carried::Carried;
use super::eeprom;
use super::endpoints::GPS_PPS;
use super::overrides::Parts;
use super::overrides::overrides;
use crate::board::lifetime::start_efc;
use crate::board::parameters::fpga;
use crate::board::participants;
use crate::board::registry;
use crate::config::Eeprom;
use crate::config::GpsSource;
use crate::config::RunConfig;
use crate::files::Files;
use crate::files::read_text;
use crate::ports::GPS;
use crate::trace;

/// The board file's array of peripherals.
pub(super) const PERIPHERAL: &str = "peripheral";
/// A peripheral entry's table of parameters.
pub(super) const PARAMETERS: &str = "parameters";
/// Its array of nets.
const NET: &str = "net";

/// The board file as the run read it at its start, and the types its
/// parts are built from.
#[derive(Debug)]
pub(super) struct Loaded {
    /// The file, `--gps`'s endpoint swapped in, which the run's
    /// parameters are worked out from.
    file: Table,
    /// The file checked, its images read, before the run's parameters.
    description: Description,
    /// smartclock's types and the stock ones.
    registry: Registry,
    /// Where the files are read from.
    files: Rc<dyn Files>,
    /// The board file's directory, which its parts' paths are from.
    directory: PathBuf,
    /// The EFC the firmware starts its loop from on the EEPROM the run
    /// starts with, counts (`lifetime::start_efc`): where the
    /// oscillator is on frequency unless the board file or the run
    /// gives its offset (`overrides`).  Worked out once, at the run's
    /// start, as the oscillator is the same one through each power
    /// cycle.
    start_efc: f64,
}

impl Loaded {
    /// The board file `config` names, read from `files`, its images and
    /// `config`'s image with it.
    pub(super) fn new(config: &RunConfig, files: Rc<dyn Files>) -> Result<Self, Error> {
        let path = &config.board;
        let text = read_text(&*files, path).map_err(|source| Error::Read {
            path: path.clone(),
            source,
        })?;
        let mut file: Table = toml::from_str(&text).map_err(|source| Error::Parse {
            path: path.clone(),
            source,
        })?;
        if let GpsSource::External(_) = config.gps {
            external_gps(&mut file);
        }
        if let Eeprom::Memory { .. } = config.eeprom {
            eeprom::in_memory(&mut file);
        }
        if let Some(design) = &config.fpga_design {
            fpga_design(&mut file, design);
        }
        let mut options = Options::default();
        // every option fitted, as the Unicorn board has (the 58503A's
        // front panel)
        options.all_enabled = true;
        options.firmware = Some(absolute(&*files, &config.image_path)?);
        let directory = path.parent().map(Path::to_owned).unwrap_or_default();
        let start_efc = start_efc(
            &eeprom::starting_cells(&file, &config.eeprom, &*files, &directory).unwrap_or_default(),
        );
        let description =
            Description::parse_with(&toml::to_string(&file)?, &directory, &options, |image| {
                files.read(image)
            })
            .map_err(|source| Error::Load {
                path: path.clone(),
                source,
            })?;
        let mut registry = Registry::with_builtins();
        registry::register_all(&mut registry)?;
        Ok(Self {
            file,
            description,
            registry,
            files,
            directory,
            start_efc,
        })
    }

    /// Where the run's files are read from.
    pub(super) fn files(&self) -> &dyn Files {
        &*self.files
    }

    /// The board `config` asks for, powered up with what `carried`
    /// carries, and the parts the runner reaches.  Its parts' files are
    /// read from the run's files, but for the EEPROM's image when it is
    /// in memory and a power cycle has carried its cells: those are the
    /// image.
    pub(super) fn build(
        &self,
        config: &RunConfig,
        carried: &Carried,
    ) -> Result<(Board<Mc68331>, Parts), Error> {
        let path = &config.board;
        let (parameters, parts) =
            overrides(&self.file, config, carried, &*self.files, self.start_efc)?;
        let mut description = self.description.clone();
        for (peripheral, table) in parameters {
            for (key, value) in table {
                description
                    .set_parameter(&peripheral, key, value)
                    .map_err(|source| Error::Edit {
                        path: path.clone(),
                        source,
                    })?;
            }
        }
        let mut environment = Environment::default();
        environment.seed = config.seed;
        let cells = carried.eeprom.as_ref().zip(parts.eeprom.as_ref());
        let cells_at = cells.map(|(kept, memory)| (self.directory.join(&memory.image), kept));
        let read = |file: &Path| match &cells_at {
            Some((image, kept)) if file == image => Ok(kept.cells.clone()),
            _ => self.files.read(file),
        };
        let board = Board::load_with(&description, &self.registry, &environment, read).map_err(
            |source| Error::Build {
                path: path.clone(),
                source,
            },
        )?;
        trace::note(format_args!("mc683xx core on {}", path.display()));
        Ok((board, parts))
    }
}

/// The FPGA in `file` running `design` (`--fpga-design`), its
/// `bitstream` dropped unless the design is the LCA model's, the only
/// one that runs a bitstream.
pub(super) fn fpga_design(file: &mut Table, design: &str) {
    for device in entries(file, PERIPHERAL) {
        if kind(device) != registry::XC3042 {
            continue;
        }
        let parameters = device
            .entry(PARAMETERS)
            .or_insert_with(|| Value::Table(Table::new()));
        if let Some(parameters) = parameters.as_table_mut() {
            parameters.insert(fpga::DESIGN.to_owned(), Value::String(design.to_owned()));
            if design != fpga::LCA {
                parameters.remove(fpga::BITSTREAM);
            }
        }
    }
}

/// `path` from the working directory, as `files` has it.
pub(super) fn absolute(files: &dyn Files, path: &Path) -> Result<PathBuf, Error> {
    files.absolute(path).map_err(|source| Error::Resolve {
        path: path.to_owned(),
        source,
    })
}

/// The board file's `[[peripheral]]`, `[[net]]` and `[[memory]]`
/// arrays.
pub(super) fn entries<'a>(file: &'a mut Table, key: &str) -> Vec<&'a mut Table> {
    file.get_mut(key)
        .and_then(Value::as_array_mut)
        .into_iter()
        .flatten()
        .filter_map(Value::as_table_mut)
        .collect()
}

/// A peripheral entry's type.
pub(super) fn kind(device: &Table) -> &str {
    device.get("type").and_then(Value::as_str).unwrap_or("")
}

/// A peripheral entry's name.
pub(super) fn name(device: &Table) -> Option<&str> {
    device.get("name").and_then(Value::as_str)
}

/// The synthetic engine taken off the board for `--gps`, an Oncore or
/// a Furuno: its serial ports' nets end at the run-time endpoint `gps`
/// instead, the external engine to speak the protocol the part named,
/// and its 1 PPS comes from `gps_pps`, which the runner pulses on each
/// whole second (`gps::external`).
fn external_gps(file: &mut Table) {
    let is_engine = |device: &Table| participants::GPS_TYPES.contains(&kind(device));
    let engines: Vec<String> = entries(file, PERIPHERAL)
        .into_iter()
        .filter(|device| is_engine(device))
        .filter_map(|device| name(device).map(str::to_owned))
        .collect();
    if let Some(devices) = file.get_mut(PERIPHERAL).and_then(Value::as_array_mut) {
        devices.retain(|device| device.as_table().is_none_or(|device| !is_engine(device)));
    }
    let external = Value::String(format!("external.{GPS}"));
    let external_pps = Value::String(format!("external.{GPS_PPS}"));
    let of_engine = |endpoint: &Value| {
        endpoint.as_str().is_some_and(|endpoint| {
            endpoint
                .split_once('.')
                .is_some_and(|(device, _)| engines.iter().any(|engine| engine == device))
        })
    };
    let pps = |endpoint: &Value| {
        of_engine(endpoint)
            && endpoint
                .as_str()
                .is_some_and(|endpoint| endpoint.ends_with(".pps"))
    };
    for net in entries(file, NET) {
        if net.get("from").is_some_and(pps) {
            net.insert("from".to_owned(), external_pps.clone());
        } else if net.get("from").is_some_and(of_engine) {
            net.insert("from".to_owned(), external.clone());
        }
        if let Some(to) = net.get_mut("to").and_then(Value::as_array_mut) {
            for endpoint in to.iter_mut().filter(|endpoint| of_engine(endpoint)) {
                *endpoint = external.clone();
            }
        }
    }
}
