//! The EEPROM in memory (`Eeprom::Memory`): the stock AT28C64B with its
//! `image` but no `file`, and no work directory, so that it opens no
//! file of its own.  Its image is read through the host's files at the
//! run's start, and at each power cycle after it the image is the cells
//! the part had (`At28c64b::cells`), its protection the part's
//! (`At28c64b::protected`) as its `protected` parameter: carried
//! (`carried::Carried`) and handed back as `read`'s answer for the
//! image's path (`load::Loaded::build`).

use std::path::Path;
use std::path::PathBuf;

use mc683xx_board::Board;
use mc683xx_mc68331::Mc68331;
use mc683xx_peripherals::At28c64b;
use toml::Table;

use super::Error;
use super::KeptEeprom;
use super::load::PARAMETERS;
use super::load::PERIPHERAL;
use super::load::kind;
use crate::board::parameters::eeprom::FILE;
use crate::board::parameters::eeprom::IMAGE;
use crate::board::registry::AT28C64B;
use crate::config::Eeprom;
use crate::files::Files;

/// What a carried image's path is named for an EEPROM whose board file
/// gives it none, after the part's name, as the part names its working
/// file in a work directory.
const CARRIED_SUFFIX: &str = ".eeprom";

/// The EEPROM in memory, which the run reaches by name.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(super) struct MemoryEeprom {
    /// Its name in the board file.
    pub(super) name: String,
    /// Its image, as its parameter gives it, relative to the board
    /// file's directory: the board file's, or for one with none a path
    /// of its name's, which only a carried image is read at.
    pub(super) image: PathBuf,
}

impl MemoryEeprom {
    /// The EEPROM `name` of the board file, its image `image` if the file
    /// gives one.
    pub(super) fn new(name: &str, image: Option<&str>) -> Self {
        Self {
            name: name.to_owned(),
            image: image.map_or_else(
                || PathBuf::from(format!("{name}{CARRIED_SUFFIX}")),
                PathBuf::from,
            ),
        }
    }
}

/// The board file `file`'s EEPROMs' parameters with no working file: in
/// memory.
pub(super) fn in_memory(file: &mut Table) {
    let parameters = file
        .get_mut(PERIPHERAL)
        .and_then(toml::Value::as_array_mut)
        .into_iter()
        .flatten()
        .filter_map(toml::Value::as_table_mut)
        .filter(|device| kind(device) == AT28C64B)
        .filter_map(|device| device.get_mut(PARAMETERS))
        .filter_map(toml::Value::as_table_mut);
    for parameters in parameters {
        parameters.remove(FILE);
    }
}

/// `board`'s EEPROM `name` as it stands: its cells and its protection.
pub(super) fn kept(board: &Board<Mc68331>, name: &str) -> Result<KeptEeprom, Error> {
    let part = board
        .peripheral::<At28c64b>(name)
        .ok()
        .ok_or_else(|| Error::NoEeprom(name.to_owned()))?;
    Ok(KeptEeprom {
        cells: part.cells().to_vec(),
        protected: part.protected(),
    })
}

/// The image the board file's EEPROM entry `device` names, relative to
/// the file's directory.
pub(super) fn image(device: &Table) -> Option<&str> {
    device
        .get(PARAMETERS)
        .and_then(|parameters| parameters.get(IMAGE))
        .and_then(toml::Value::as_str)
}

/// The cells the board file `file`'s EEPROM starts the run with, as the
/// part takes them (mc683xx's docs/peripherals.md, AT28C64B), `eeprom`
/// where they are kept: the working file's, where it is there already,
/// else the dump the run names, else the board file's image, its path
/// from the file's `directory`; in memory, the image's, which a host
/// gives the cells a run before it kept (`config::Eeprom::Memory`).
/// Each is read through `files`; none for a part erased, or a board
/// with no EEPROM.
pub(super) fn starting_cells(
    file: &Table,
    eeprom: &Eeprom,
    files: &dyn Files,
    directory: &Path,
) -> Option<Vec<u8>> {
    let device = file
        .get(PERIPHERAL)
        .and_then(toml::Value::as_array)
        .into_iter()
        .flatten()
        .filter_map(toml::Value::as_table)
        .find(|device| kind(device) == AT28C64B)?;
    let image = image(device).map(|image| directory.join(image));
    let (working, dump) = match eeprom {
        Eeprom::File { path, dump } => (Some(path), dump.as_ref()),
        Eeprom::Memory { .. } => (None, None),
    };
    [working, dump, image.as_ref()]
        .into_iter()
        .flatten()
        .find_map(|path| files.read(path).ok())
}
