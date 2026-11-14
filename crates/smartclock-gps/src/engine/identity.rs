//! Which Oncore a model is, and the text it answers `@@Cj` with.

use crate::Direction;
use crate::oncore::CJ_RECEIVER_ID;
use crate::oncore::data_length;

/// The lines of an Oncore's receiver ID (`@@Cj`,
/// `third_party/motorola/VPCommands.pdf` p. 122 as printed).
pub const IDENTITY_LINES: usize = 10;

/// What ends each line but the last, and stands before the first.
pub(super) const LINE_END: &[u8] = b"\r\n";

/// The kind of Oncore, which fixes the commands it answers and the
/// layout of what it sends.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Model {
    /// A six-channel VP, as the B1121P1114.
    VpSix,
    /// An eight-channel VP, as the B4121P1115.
    VpEight,
    /// A UT Plus, as the R5122U1152: eight channels, fewer commands,
    /// and `@@Bo` (`docs/firmware/gps.md`, "The UT Plus").
    UtPlus,
}

impl Model {
    /// The receiver channels it has.
    #[must_use]
    pub const fn channels(self) -> usize {
        match self {
            Self::VpSix => 6,
            Self::VpEight | Self::UtPlus => 8,
        }
    }
}

/// One Oncore: its kind and its receiver ID.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Identity {
    /// The kind.
    model: Model,
    /// Whether it answers `@@Gj`, as a UT Plus does from firmware 3.0
    /// on.
    leap_details: bool,
    /// The receiver ID's lines.
    lines: [&'static str; IDENTITY_LINES],
}

impl Identity {
    /// An Oncore of `model` whose receiver ID is `lines`.
    /// `leap_details` is whether it answers `@@Gj`.  `None` unless the
    /// lines fill `@@Cj`'s fixed length exactly, which lines of 26
    /// characters after a first of 33 do.
    #[must_use]
    pub const fn new(
        model: Model,
        leap_details: bool,
        lines: [&'static str; IDENTITY_LINES],
    ) -> Option<Self> {
        let Some(room) = data_length(CJ_RECEIVER_ID, Direction::FromEngine) else {
            return None;
        };
        let mut used = IDENTITY_LINES * LINE_END.len();
        let mut index = 0;
        while index < IDENTITY_LINES {
            used += lines[index].len();
            index += 1;
        }
        if used == room {
            Some(Self {
                model,
                leap_details,
                lines,
            })
        } else {
            None
        }
    }

    /// The kind.
    #[must_use]
    pub const fn model(&self) -> Model {
        self.model
    }

    /// Whether it answers `@@Gj`.
    #[must_use]
    pub const fn leap_details(&self) -> bool {
        self.leap_details
    }

    /// The receiver ID's lines.
    #[must_use]
    pub const fn lines(&self) -> &[&'static str; IDENTITY_LINES] {
        &self.lines
    }
}

const fn preset(
    model: Model,
    leap_details: bool,
    lines: [&'static str; IDENTITY_LINES],
) -> Identity {
    match Identity::new(model, leap_details, lines) {
        Some(identity) => identity,
        None => panic!("a preset's lines must fill the receiver ID"),
    }
}

/// A six-channel VP on firmware 8.4.  The serial number is made up.
pub const VP_SIX: Identity = preset(
    Model::VpSix,
    false,
    [
        "COPYRIGHT 1991-1995 MOTOROLA INC.",
        "SFTW P/N # 98-P39972M     ",
        "SOFTWARE VER # 8          ",
        "SOFTWARE REV # 4          ",
        "SOFTWARE DATE  13 JUL 1995",
        "MODEL #    B1121P1114     ",
        "HDWR P/N # _              ",
        "SERIAL #   SYNTHETIC      ",
        "MANUFACTUR DATE 6G09      ",
        "OPTIONS LIST    IB        ",
    ],
);

/// An eight-channel VP on firmware 8.8.  The serial number is made up.
pub const VP_EIGHT: Identity = preset(
    Model::VpEight,
    false,
    [
        "COPYRIGHT 1991-1996 MOTOROLA INC.",
        "SFTW P/N # 98-P36830P     ",
        "SOFTWARE VER # 8          ",
        "SOFTWARE REV # 8          ",
        "SOFTWARE DATE  06 AUG 1996",
        "MODEL #    B4121P1115     ",
        "HDWR P/N # _              ",
        "SERIAL #   SYNTHETIC      ",
        "MANUFACTUR DATE 7D01      ",
        "OPTIONS LIST    IB        ",
    ],
);

/// [`VP_EIGHT`] whose options list does not name Time RAIM.
pub const VP_EIGHT_WITHOUT_TIME_RAIM: Identity = preset(
    Model::VpEight,
    false,
    [
        "COPYRIGHT 1991-1996 MOTOROLA INC.",
        "SFTW P/N # 98-P36830P     ",
        "SOFTWARE VER # 8          ",
        "SOFTWARE REV # 8          ",
        "SOFTWARE DATE  06 AUG 1996",
        "MODEL #    B4121P1115     ",
        "HDWR P/N # _              ",
        "SERIAL #   SYNTHETIC      ",
        "MANUFACTUR DATE 7D01      ",
        "OPTIONS LIST    B         ",
    ],
);

/// A UT Plus on firmware 2.2: the receiver ID one printed in an `ntpd`
/// clock log, published as comment 6 of Launchpad's ntp bug 805661
/// (<https://bugs.launchpad.net/ubuntu/+source/ntp/+bug/805661/comments/6>),
/// serial number and date of manufacture included.  Another unit's,
/// the same but for those two and padded as here, was posted to
/// time-nuts
/// (<https://febo.com/pipermail/time-nuts_lists.febo.com/2014-November/070174.html>).
/// The log prints the last line as blank; the message's fixed length
/// makes it 26 spaces.
pub const UT_PLUS_2_2: Identity = preset(
    Model::UtPlus,
    false,
    [
        "COPYRIGHT 1991-1997 MOTOROLA INC.",
        "SFTW P/N #     98-P36848P ",
        "SOFTWARE VER # 2          ",
        "SOFTWARE REV # 2          ",
        "SOFTWARE DATE  APR 24 1998",
        "MODEL #    R5122U1152     ",
        "HWDR P/N # 5              ",
        "SERIAL #   R05UD5         ",
        "MANUFACTUR DATE 9E14      ",
        "                          ",
    ],
);

/// A UT Plus on firmware 3.1, which answers `@@Gj`.  No receiver ID of
/// one is at hand: all of this but the model, the version and the
/// revision is made up, and says so.
pub const UT_PLUS_3_1: Identity = preset(
    Model::UtPlus,
    true,
    [
        "COPYRIGHT 1991-1997 MOTOROLA INC.",
        "SFTW P/N #     SYNTHETIC  ",
        "SOFTWARE VER # 3          ",
        "SOFTWARE REV # 1          ",
        "SOFTWARE DATE  SYNTHETIC  ",
        "MODEL #    R5122U1152     ",
        "HWDR P/N # SYNTHETIC      ",
        "SERIAL #   SYNTHETIC      ",
        "MANUFACTUR DATE SYNTHETIC ",
        "                          ",
    ],
);
