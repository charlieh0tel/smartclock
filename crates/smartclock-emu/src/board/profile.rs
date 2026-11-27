//! Which receiver's board the emulator is: what differs between the
//! models it boots, chosen by the image or by `--model`
//! (docs/emulator.md, "The boards").  The memory map, the MC68331's
//! modules, the DUART, the EEPROM, the ADC, the DACs and the FPGA's
//! model are the same on the Z3805A, the 58503A and the Z3801A; what
//! differs is the latch's lamps, the 58503A's front panel on QSPI device
//! 0 and its alarm output on PF5 (docs/firmware/58503a-3704.md,
//! "Hardware"), the GPS engine, and the host port's framing, the
//! Z3801A's 7O1.  The 58503B's and the Z3816A's boards are their board
//! files' alone, run on mc683xx: their maps and their FPGAs' designs are
//! none of theirs, and the 58503B's ACIA neither.

#[cfg(feature = "mc683xx")]
use smartclock::types::Framing;

use clap::ValueEnum;

#[cfg(not(target_family = "wasm"))]
use crate::board::ocxo::Config as OcxoConfig;
use crate::board::ocxo::EFC_GAIN;
use crate::gps::B1121;
use crate::gps::B4121;
use crate::gps::Engine;
use crate::gps::UT_PLUS;
use crate::indicators::Indicator;

/// A receiver whose board the emulator models, each with its
/// `Profile`.  A board is added as a variant, its profile and its arm
/// in `Model::profile` (docs/emulator.md, "Adding a board").
#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum)]
pub enum Model {
    /// The Z3805A, as its 3543B image drives it.
    #[value(name = "z3805a")]
    Z3805A,
    /// The 58503A, as its 3704 image drives it, with Option 001's front
    /// panel.
    #[value(name = "58503a")]
    Hp58503A,
    /// The Z3801A, as its 3543 image drives it: the Z3805A's board
    /// without its SCI endpoint (docs/firmware/z3801a-3543.md).
    #[value(name = "z3801a")]
    Z3801A,
    /// The Z3816A, as its 4001 image drives it: the second family's
    /// map, the host port on the SCI, the LCA model's FPGA
    /// (boards/z3816a.toml), on mc683xx alone.
    #[value(name = "z3816a")]
    Z3816A,
    /// The 58503B, as its 1.01.04 image drives it: its own map, an
    /// ACIA for the GPS engine and the SCI for the host
    /// (boards/58503b.toml), on mc683xx alone.
    #[value(name = "58503b")]
    Hp58503B,
}

/// What one model's board is, as far as it differs from the others'.
#[derive(Debug)]
pub(crate) struct Profile {
    /// Its name as `--model` takes it, and its board file's,
    /// `boards/NAME.toml`.
    #[cfg(feature = "mc683xx")]
    name: &'static str,
    /// The model name its image gives `*IDN?`, a string of its own
    /// ending in a NUL, by which the image picks the model.
    identity: &'static [u8],
    /// The lamps on the latch.
    lamps: &'static [Indicator],
    /// Its GPS engine on the Unicorn board, as its board file's
    /// `oncore` gives it by its `engine` on mc683xx.
    gps_engine: &'static Engine,
    /// Whether the front panel is on QSPI device 0.
    front_panel: bool,
    /// Whether PF5 is the alarm output.
    alarm_output: bool,
    /// How the host port's far end frames a character.  The Unicorn
    /// board's DUART models no parity, so only mc683xx's stock DUART
    /// needs it.
    #[cfg(feature = "mc683xx")]
    host_framing: Framing,
    /// Whether the Unicorn core's board, wired in code, is its board.
    unicorn: bool,
    /// The firmware's EFC gain G for the oscillator its board file's
    /// EEPROM image, a dump or the synthetic image, leaves it, fractional
    /// frequency an EFC count: the
    /// oscillator's by default (`--ocxo-efc-gain`).
    efc_gain: f64,
    /// Whether its image starts the continuous time code at every
    /// power-up, from its initialized data, which drops the host port's
    /// SCPI output until `:PTIMe:TCODe:CONTinuous OFF`
    /// (docs/firmware/console.md, "Which port"): what `--time-code-off`
    /// is for.
    time_code_at_power_up: bool,
    /// The board files of its own for the images of a revision that
    /// differs in its board from the model's other images.
    variants: &'static [Variant],
}

/// A board file of a model's own for the images of one revision: the
/// model's board but for what the revision changes, its GPS engine, say.
#[derive(Debug)]
struct Variant {
    /// The revision string its images hold, ending in a NUL, which
    /// `*IDN?` gives with a suffix, by which an image picks it.
    revision: &'static [u8],
    /// Its board file's name, `boards/NAME.toml`, and its `[board]` name.
    #[cfg(feature = "mc683xx")]
    name: &'static str,
}

/// The Z3805A 1.01.03's: the Z3805A's board with a Furuno GT-74 in
/// place of the Oncore, its revision string at `0x13048`
/// (docs/firmware/z3805a-1.01.03.md, "Provenance", "The GPS engine").
const Z3805A_10103: Variant = Variant {
    revision: b"1.01.03\0",
    #[cfg(feature = "mc683xx")]
    name: "z3805a-10103",
};

/// The Z3805A's.
const Z3805A: Profile = Profile {
    #[cfg(feature = "mc683xx")]
    name: "z3805a",
    identity: b"Z3805A\0",
    lamps: &Z3805A_LAMPS,
    gps_engine: &B1121,
    front_panel: false,
    alarm_output: false,
    #[cfg(feature = "mc683xx")]
    host_framing: Framing::EightNone,
    unicorn: true,
    efc_gain: EFC_GAIN,
    time_code_at_power_up: false,
    variants: &[Z3805A_10103],
};

/// The 58503A's: its eight-channel B4121 (docs/firmware/gps.md, "The
/// engines on the bench"), Option 001's front panel and the alarm on
/// PF5 (58503a-3704.md, "The alarm outputs"), its port 8N1 as the bench
/// unit's is set.
const HP58503A: Profile = Profile {
    #[cfg(feature = "mc683xx")]
    name: "58503a",
    identity: b"58503A\0",
    lamps: &HP58503A_LAMPS,
    gps_engine: &B4121,
    front_panel: true,
    alarm_output: true,
    #[cfg(feature = "mc683xx")]
    host_framing: Framing::EightNone,
    unicorn: true,
    efc_gain: EFC_GAIN,
    time_code_at_power_up: false,
    variants: &[],
};

/// The Z3801A's: the Z3805A's lamps, its `leds_update` the Z3805A's
/// relocated (z3801a-3543.md), and six-channel engine; its port fixed
/// at 7O1 (097-z3801-01 1-8, 2-10).
const Z3801A: Profile = Profile {
    #[cfg(feature = "mc683xx")]
    name: "z3801a",
    identity: b"Z3801A\0",
    lamps: &Z3805A_LAMPS,
    gps_engine: &B1121,
    front_panel: false,
    alarm_output: false,
    #[cfg(feature = "mc683xx")]
    host_framing: Framing::SevenOdd,
    unicorn: true,
    efc_gain: EFC_GAIN,
    time_code_at_power_up: false,
    variants: &[],
};

/// The Z3816A's: an eight-channel engine, the image taking `@@Ea` and
/// `@@En` (gps.md, "Six or eight channels"); no lamps named, its latch's
/// eight bits lit in turn by the lamp test at 0x2871e and their meaning
/// not traced; no front panel or alarm output known; the SCI 8N1, its
/// defaults, which the synthetic EEPROM keeps; the time code on at
/// power-up, the initialized data's byte at `0x40172`.
const Z3816A: Profile = Profile {
    #[cfg(feature = "mc683xx")]
    name: "z3816a",
    identity: b"Z3816A\0",
    lamps: &[],
    gps_engine: &B4121,
    front_panel: false,
    alarm_output: false,
    #[cfg(feature = "mc683xx")]
    host_framing: Framing::EightNone,
    unicorn: false,
    efc_gain: Z3816A_EFC_GAIN,
    time_code_at_power_up: true,
    variants: &[],
};

/// The Z3816A's G with bit 8 of the switch word at `0x302000` set, as
/// its board file's `switch_high` leaves it: -1.25e-12, and -2.125e-12
/// were it clear (docs/firmware/loop.md, "τ and G"; `0x4b14c` to
/// `0x4b166`).
const Z3816A_EFC_GAIN: f64 = -1.25e-12;

/// The 58503B's: the 58503A's lamps on the latch's bits 0 to 2, Option
/// 001's front panel on bits 6 and 7 and QSPI device 0, the alarm on
/// PF5 (boards/58503b.toml), and the UT Plus Oncore a shipped 58503B
/// reports (docs/firmware/gps.md, "The UT Plus"); its host port the SCI,
/// framed 8N1 as the others' are.
const HP58503B: Profile = Profile {
    #[cfg(feature = "mc683xx")]
    name: "58503b",
    identity: b"58503B\0",
    lamps: &HP58503A_LAMPS,
    gps_engine: &UT_PLUS,
    front_panel: true,
    alarm_output: true,
    #[cfg(feature = "mc683xx")]
    host_framing: Framing::EightNone,
    unicorn: false,
    efc_gain: HP58503B_EFC_GAIN,
    time_code_at_power_up: false,
    variants: &[],
};

/// The 58503B's G as its board file's EEPROM, a 58503B's, holds it: the
/// unit's own oscillator calibration, `:DIAGnostic:ROSCillator:EFControl:
/// ASLOPe`, the slope its EFC calibration measured, the calibration
/// block's float at `+0x10` (`0x400090`, read into `0x10271e` at
/// `0x40c10` when the block's oscillator type is the one in force;
/// docs/firmware/loop.md, "In every image"), `0xab3c228c`, about
/// -6.684e-13 rather than type 0's -1.25e-12.
const HP58503B_EFC_GAIN: f64 = f32::from_bits(0xab3c_228c) as f64;

/// The Z3805A's lamps on the latch, by bit (outputs.md, "The Z3805A's
/// lamps").
const Z3805A_LAMPS: [Indicator; 5] = [
    Indicator {
        bit: 0,
        name: "enabled",
    },
    Indicator {
        bit: 1,
        name: "active",
    },
    Indicator {
        bit: 2,
        name: "alarm",
    },
    Indicator {
        bit: 4,
        name: "gps_lock",
    },
    Indicator {
        bit: 5,
        name: "holdover",
    },
];

/// The 58503A's: GPS Lock, Holdover and Alarm on bits 0 to 2, bits 6
/// and 7 the front panel's (58503a-3704.md, "The front panel").
const HP58503A_LAMPS: [Indicator; 3] = [
    Indicator {
        bit: 0,
        name: "gps_lock",
    },
    Indicator {
        bit: 1,
        name: "holdover",
    },
    Indicator {
        bit: 2,
        name: "alarm",
    },
];

impl Model {
    /// Its profile.
    fn profile(self) -> &'static Profile {
        match self {
            Self::Z3805A => &Z3805A,
            Self::Hp58503A => &HP58503A,
            Self::Z3801A => &Z3801A,
            Self::Z3816A => &Z3816A,
            Self::Hp58503B => &HP58503B,
        }
    }

    /// Every model.
    fn all() -> &'static [Self] {
        Self::value_variants()
    }

    /// The model whose image `image` is: the one model whose `*IDN?`
    /// name it holds, else none.
    pub(crate) fn of_image(image: &[u8]) -> Option<Self> {
        let mut found = Self::all().iter().filter(|model| {
            let name = model.profile().identity;
            image.windows(name.len()).any(|window| window == name)
        });
        match (found.next(), found.next()) {
            (Some(model), None) => Some(*model),
            _ => None,
        }
    }

    /// Its board file's name in the repository's `boards`.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn board_file(self) -> String {
        format!("{}.toml", self.name())
    }

    /// The variant whose revision `image` holds, if one does.
    fn variant_of(self, image: &[u8]) -> Option<&'static Variant> {
        self.profile().variants.iter().find(|variant| {
            image
                .windows(variant.revision.len())
                .any(|window| window == variant.revision)
        })
    }

    /// The board file in the repository's `boards` for its image
    /// `image`: a variant's where the image's revision has one, else its
    /// own.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn board_file_for(self, image: &[u8]) -> String {
        self.variant_of(image).map_or_else(
            || self.board_file(),
            |variant| format!("{}.toml", variant.name),
        )
    }

    /// Whether `image` is of a revision with a board of its own, which
    /// the Unicorn core, its board the model's wired in code, does not
    /// run.
    pub(crate) fn has_variant_for(self, image: &[u8]) -> bool {
        self.variant_of(image).is_some()
    }

    /// Its name as `--model` takes it.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn name(self) -> &'static str {
        self.profile().name
    }

    /// The lamps on the latch.
    pub(crate) fn lamps(self) -> &'static [Indicator] {
        self.profile().lamps
    }

    /// Its GPS engine on the Unicorn board.
    pub(crate) fn gps_engine(self) -> &'static Engine {
        self.profile().gps_engine
    }

    /// Whether the front panel is on QSPI device 0.
    pub(crate) fn has_front_panel(self) -> bool {
        self.profile().front_panel
    }

    /// Whether PF5 is the alarm output.
    pub(crate) fn has_alarm_output(self) -> bool {
        self.profile().alarm_output
    }

    /// How the host port's far end frames a character.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn host_framing(self) -> Framing {
        self.profile().host_framing
    }

    /// The firmware's EFC gain for its oscillator.
    pub(crate) fn efc_gain(self) -> f64 {
        self.profile().efc_gain
    }

    /// Its oscillator as the model's profile has it, the firmware's EFC
    /// gain, the other terms the default's: the terms a board file and
    /// the run leave it on the Unicorn core, whose oscillator is on
    /// frequency at the EFC's middle (docs/emulator.md, "The
    /// oscillator").
    #[cfg(not(target_family = "wasm"))]
    pub(crate) fn ocxo(self) -> OcxoConfig {
        OcxoConfig {
            efc_gain: self.efc_gain(),
            ..OcxoConfig::default()
        }
    }

    /// Whether the Unicorn core runs it.
    pub(crate) fn on_unicorn(self) -> bool {
        self.profile().unicorn
    }

    /// Whether its image starts the continuous time code at every
    /// power-up.
    pub(crate) fn starts_time_code(self) -> bool {
        self.profile().time_code_at_power_up
    }
}

#[cfg(test)]
mod tests {
    use clap::ValueEnum;

    use super::HP58503B_EFC_GAIN;
    use super::Model;
    use super::Variant;

    /// The repository's boards.
    const BOARDS: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../../boards");

    /// The board file `name`, parsed.
    fn board(name: &str) -> toml::Table {
        let text = std::fs::read_to_string(format!("{BOARDS}/{name}")).expect("the board file");
        toml::from_str(&text).expect("a table")
    }

    /// The calibration block's G in the 58503B's EEPROM, its offset in
    /// the part.
    const CALIBRATION_GAIN: usize = 0x90;

    /// The float at `offset` in `image`.
    fn float_at(image: &[u8], offset: usize) -> f64 {
        let bytes = image[offset..offset + 4].try_into().expect("four bytes");
        f64::from(f32::from_be_bytes(bytes))
    }

    /// The parameters of the peripheral `name` in the board file `file`.
    fn parameters<'a>(file: &'a toml::Table, name: &str) -> &'a toml::Value {
        file["peripheral"]
            .as_array()
            .expect("the peripherals")
            .iter()
            .find(|part| part["name"].as_str() == Some(name))
            .map(|part| &part["parameters"])
            .unwrap_or_else(|| panic!("the peripheral {name}"))
    }

    /// The EEPROM image the board file `file` names, its bytes.
    fn eeprom_image(file: &toml::Table) -> Vec<u8> {
        let image = parameters(file, "eeprom")["image"]
            .as_str()
            .expect("the EEPROM's image");
        std::fs::read(format!("{BOARDS}/{image}")).expect("the image")
    }

    #[test]
    fn the_58503bs_gain_is_its_eeproms() {
        let image = eeprom_image(&board("58503b.toml"));
        assert_eq!(float_at(&image, CALIBRATION_GAIN), HP58503B_EFC_GAIN);
    }

    /// Each model's `--model` name is its profile's, and its board
    /// file's `[board]` name.
    #[test]
    fn each_models_name_is_its_board_files() {
        for model in Model::all() {
            let value = model.to_possible_value().expect("a value");
            assert_eq!(value.get_name(), model.name());
            assert_eq!(
                board(&model.board_file())["board"]["name"].as_str(),
                Some(model.name())
            );
        }
    }

    /// Each board file's images pick its model, by the board file's
    /// `[board]` name, or none
    /// where the emulator has no such model: a profile's `identity` is
    /// in its images and no other's.
    #[test]
    fn each_board_files_images_pick_its_model() {
        let mut files: Vec<_> = std::fs::read_dir(BOARDS)
            .expect("the board files")
            .map(|entry| entry.expect("an entry").path())
            .filter(|path| {
                path.extension()
                    .is_some_and(|extension| extension == "toml")
            })
            .collect();
        files.sort();
        for path in files {
            let file = board(&path.file_name().expect("a name").to_string_lossy());
            let name = file["board"]["name"].as_str().expect("[board] name");
            let model = Model::all().iter().copied().find(|model| {
                model.name() == name
                    || model
                        .profile()
                        .variants
                        .iter()
                        .any(|variant| variant.name == name)
            });
            for image in file["board"]["images"].as_array().expect("its images") {
                let image = image.as_str().expect("a path");
                let bytes = std::fs::read(format!("{BOARDS}/{image}")).expect("the image");
                assert_eq!(
                    Model::of_image(&bytes),
                    model,
                    "{}: {image}",
                    path.display()
                );
                // and its board file is this one, a variant's or its own
                if let Some(model) = model {
                    assert!(
                        path.ends_with(model.board_file_for(&bytes)),
                        "{}: {image}",
                        path.display()
                    );
                }
            }
        }
    }

    /// The keys a variant's board file may change from its model's: its
    /// name, its title and its images, the EEPROM's image, and the GPS
    /// engine's part, its type, protocol and engine.
    const VARIANT_KEYS: [&str; 7] = [
        "name", "title", "images", "image", "type", "protocol", "engine",
    ];
    /// What marks a line a variant changes: its revision.
    fn revision_mark(variant: &Variant) -> String {
        String::from_utf8_lossy(variant.revision)
            .trim_end_matches('\0')
            .to_owned()
    }

    /// Each variant's board file is its model's but for the lines marked
    /// with its revision, which change only `VARIANT_KEYS`: every other
    /// line, comments aside, is the model's file's, in its order.
    #[test]
    fn each_variant_is_its_models_board_but_for_its_marked_lines() {
        let lines = |file: &str| -> Vec<String> {
            std::fs::read_to_string(format!("{BOARDS}/{file}"))
                .expect("the board file")
                .lines()
                .map(str::trim)
                .filter(|line| !line.is_empty() && !line.starts_with('#'))
                .map(str::to_owned)
                .collect()
        };
        for model in Model::all() {
            let original = lines(&model.board_file());
            for variant in model.profile().variants {
                let mark = revision_mark(variant);
                let file = format!("{}.toml", variant.name);
                let (marked, kept): (Vec<_>, Vec<_>) = lines(&file)
                    .into_iter()
                    .partition(|line| line.contains(&mark));
                assert!(!marked.is_empty(), "{file}: no line marked {mark}");
                let key = |line: &str| line.split('=').next().unwrap_or_default().trim().to_owned();
                for line in &marked {
                    assert!(VARIANT_KEYS.contains(&key(line).as_str()), "{file}: {line}");
                }
                // the kept lines in the original's order; what they skip,
                // the lines the variant changes or drops, of its keys alone
                let mut rest = original.iter();
                for line in &kept {
                    for skipped in rest.by_ref() {
                        if skipped == line {
                            break;
                        }
                        assert!(
                            VARIANT_KEYS.contains(&key(skipped).as_str()),
                            "{file}: {skipped} is the model's, changed unmarked"
                        );
                    }
                }
                for skipped in rest {
                    assert!(
                        VARIANT_KEYS.contains(&key(skipped).as_str()),
                        "{file}: {skipped}"
                    );
                }
            }
        }
    }

    #[test]
    fn each_boards_gps_engine_is_the_models() {
        for model in Model::all() {
            let file = model.board_file();
            let engine = board(&file)["peripheral"]
                .as_array()
                .expect("[[peripheral]]")
                .iter()
                .find(|device| device["type"].as_str() == Some("oncore"))
                .and_then(|device| device["parameters"]["engine"].as_str().map(str::to_owned))
                .expect("the engine's name");
            assert_eq!(engine, model.gps_engine().name, "{file}");
        }
    }

    #[test]
    fn an_image_is_the_model_it_names() {
        assert_eq!(Model::of_image(b"\x4e\x75Z3805A\0S"), Some(Model::Z3805A));
        assert_eq!(
            Model::of_image(b"Nu58503A\0CQ HP 58503A\0"),
            Some(Model::Hp58503A)
        );
        assert_eq!(Model::of_image(b"\x00Z3801A\0Q"), Some(Model::Z3801A));
        assert_eq!(Model::of_image(b"\x4e\x75Z3816A\0"), Some(Model::Z3816A));
        assert_eq!(
            Model::of_image(b"Nu58503B\0HP 58503B\0"),
            Some(Model::Hp58503B)
        );
        assert_eq!(Model::of_image(b"Z3815A\0"), None, "not modeled");
        assert_eq!(Model::of_image(b"Z3805A\x0058503A\0"), None, "both");
    }
}
