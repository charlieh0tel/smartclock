//! The 58503A's Option 001 front panel, its twelve-character display
//! and eight keys, each with its lamp: QSPI device 0, with all four
//! PCS lines low, a byte a transfer, and framed by bits 6 and 7 of the
//! latch at `0x300000` (docs/firmware/58503a-3704.md, "The front
//! panel").  Bit 6 low frames a command, from its first byte to its
//! last; bit 7 low holds the panel in reset.  The first byte of a
//! command is the command, the rest its data:
//!
//! | Command | Data | Routine |
//! | ------- | ---- | ------- |
//! | `0x00` | none, a key poll | `0x5395c` |
//! | `0x01` | the key lamps, then the text | `0x4812e` |
//! | `0x02` | the key lamps | `0x538d0` |
//! | `0x04` | the text | `0x5390a` |
//! | `0x08`, `0x10` | none, the self-test | `0x537ea` |
//!
//! The byte the panel shifts back with a command is a key, 0 for none,
//! and the self-test (`0x537ea`) passes when the one with `0x10` is
//! `0x55` and the one with the next command `0xaa`.  Which command a
//! reply answers the code does not settle; the model answers each
//! command with the byte the one before it loaded, `0x55` after `0x08`,
//! `0xaa` after `0x10`, and otherwise the key pressed since, so the
//! self-test's four commands pass.  *Inferred*: the panel's controller
//! is not traced, and this is the least that passes, not its protocol.

use std::fmt;

use crate::board::time::Cycles;
use crate::names::Named;
use crate::trace;

/// The latch's bit that frames a command while low.
pub(crate) const LATCH_FRAME: u8 = 0x40;
/// The latch's bit that holds the panel in reset while low.
pub(crate) const LATCH_RUN: u8 = 0x80;

/// The commands, by their first byte.
const COMMAND_LAMPS_AND_TEXT: u8 = 0x01;
const COMMAND_LAMPS: u8 = 0x02;
const COMMAND_TEXT: u8 = 0x04;
const COMMAND_SELF_TEST_FIRST: u8 = 0x08;
const COMMAND_SELF_TEST_SECOND: u8 = 0x10;
/// What the self-test wants back after its two commands.
const SELF_TEST_FIRST_REPLY: u8 = 0x55;
const SELF_TEST_SECOND_REPLY: u8 = 0xaa;
/// The reply with no key pressed.
const NO_KEY: u8 = 0;

/// A key, by its code in the reply byte: 1 to 8 in the panel's order,
/// key n's lamp bit n - 1 of the lamp byte (097-58503-08, 1-2, 2-3 to
/// 2-5; `0x53372`'s table).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[cfg_attr(feature = "mc683xx", derive(serde::Serialize, serde::Deserialize))]
pub(crate) enum Key {
    Time = 1,
    Long,
    Lat,
    Alt,
    Sat,
    Status,
    SerialPort,
    Shift,
}

impl Key {
    /// How many there are.
    pub(crate) const COUNT: usize = <Self as Named>::NAMES.len();
}

impl Named for Key {
    /// The keys in the panel's order, each with the name a script gives
    /// it.
    const NAMES: &'static [(Self, &'static str)] = &[
        (Self::Time, "time"),
        (Self::Long, "long"),
        (Self::Lat, "lat"),
        (Self::Alt, "alt"),
        (Self::Sat, "sat"),
        (Self::Status, "status"),
        (Self::SerialPort, "serial"),
        (Self::Shift, "shift"),
    ];
}

/// What the display shows: the key lamps and the text, as last written.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
#[cfg_attr(feature = "mc683xx", derive(serde::Serialize, serde::Deserialize))]
pub(crate) struct Display {
    /// The key lamps, key n's on bit n - 1.
    pub(crate) lamps: u8,
    /// The text, as sent: ASCII, twelve characters a screen, but kept
    /// whole.
    pub(crate) text: Vec<u8>,
}

/// The display changed, as its record gives it:
///
///     smartclock-emu: display cycle=67108864 lamps=0x20 text=OCXO\x20WARM\x20UP
///
/// the text with a space, a backslash and anything not printable ASCII
/// as `\xHH`, so that a record is one field a word.
#[derive(Debug, Clone, PartialEq, Eq)]
struct Shown<'a> {
    /// When.
    at: Cycles,
    /// What.
    display: &'a Display,
}

impl fmt::Display for Shown<'_> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "display cycle={} lamps={:#04x} text=",
            self.at.0, self.display.lamps
        )?;
        for byte in &self.display.text {
            if byte.is_ascii_graphic() && *byte != b'\\' {
                write!(formatter, "{}", char::from(*byte))?;
            } else {
                write!(formatter, "\\x{byte:02x}")?;
            }
        }
        Ok(())
    }
}

/// The panel.
#[derive(Debug, Clone, Default)]
#[cfg_attr(feature = "mc683xx", derive(serde::Serialize, serde::Deserialize))]
pub(crate) struct FrontPanel {
    /// A command framed: the latch's bit 6 low, out of reset.
    framed: bool,
    /// The frame closed with its last byte still on the QSPI: it ends
    /// when the byte comes (`control`).
    closing: bool,
    /// The framed command's first byte, once it has come.
    command: Option<u8>,
    /// Its data so far.
    data: Vec<u8>,
    /// What the next command's byte shifts back.
    reply: u8,
    /// A key pressed and not yet loaded into a reply.
    key: Option<Key>,
    /// What it shows.
    display: Display,
    /// Whether the display's changes are reported.
    reporting: bool,
}

impl FrontPanel {
    /// Reports each change of the display from now on.
    pub(crate) fn report(&mut self) {
        self.reporting = true;
    }

    /// The latch written `value` at `now`: its bits 6 and 7, with
    /// `in_flight` whether a transfer to the panel is on the QSPI.
    ///
    /// `qspi_write` returns once it has started its transfer, and
    /// `0x53796` raises bit 6 after the last byte's: on the board the
    /// byte, 2 us at the panel's 4.19 MHz, is through before the code
    /// gets there, but the QSPI's model takes a fixed span for every
    /// queue (`qsm`), longer, so a frame closing with its byte in
    /// flight ends when the byte comes.
    pub(crate) fn control(&mut self, value: u8, in_flight: bool, now: Cycles) {
        let running = value & LATCH_RUN != 0;
        let framed = running && value & LATCH_FRAME == 0;
        if !running {
            *self = Self {
                key: self.key,
                display: std::mem::take(&mut self.display),
                reporting: self.reporting,
                ..Self::default()
            };
            return;
        }
        if framed && (!self.framed || self.closing) {
            if self.closing {
                self.end(now);
            }
            self.command = None;
            self.data.clear();
            self.framed = true;
        } else if !framed && self.framed && !self.closing {
            if in_flight {
                self.closing = true;
            } else {
                self.end(now);
            }
        }
    }

    /// The panel takes `word`'s low byte from the QSPI at `now` and
    /// shifts back its reply (`reply`).
    pub(crate) fn exchange(&mut self, word: u16, now: Cycles) -> u16 {
        let reply = self.reply();
        if !self.framed {
            return u16::from(reply);
        }
        let byte = word as u8;
        if self.command.is_some() {
            self.data.push(byte);
        } else {
            self.command = Some(byte);
            self.reply = match byte {
                COMMAND_SELF_TEST_FIRST => SELF_TEST_FIRST_REPLY,
                COMMAND_SELF_TEST_SECOND => SELF_TEST_SECOND_REPLY,
                _ => self.key.take().map_or(NO_KEY, |key| key as u8),
            };
        }
        if self.closing {
            self.end(now);
        }
        u16::from(reply)
    }

    /// What the next byte shifts back: a command's, the byte the one
    /// before it loaded; a data byte's, or one unframed, none.
    pub(crate) fn reply(&self) -> u8 {
        if self.framed && self.command.is_none() {
            self.reply
        } else {
            NO_KEY
        }
    }

    /// The frame ends at `now`.
    fn end(&mut self, now: Cycles) {
        self.framed = false;
        self.closing = false;
        self.finish(now);
    }

    /// What it shows.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn display(&self) -> &Display {
        &self.display
    }

    /// `key` pressed: loaded into the reply after the next command.
    pub(crate) fn press(&mut self, key: Key) {
        self.key = Some(key);
    }

    /// The framed command ends at `now`: what it writes is shown.
    fn finish(&mut self, now: Cycles) {
        let shown = match (self.command, self.data.split_first()) {
            (Some(COMMAND_LAMPS_AND_TEXT), Some((lamps, text))) => Display {
                lamps: *lamps,
                text: text.to_vec(),
            },
            (Some(COMMAND_LAMPS), Some((lamps, _))) => Display {
                lamps: *lamps,
                text: self.display.text.clone(),
            },
            (Some(COMMAND_TEXT), _) => Display {
                lamps: self.display.lamps,
                text: self.data.clone(),
            },
            _ => return,
        };
        if shown != self.display {
            self.display = shown;
            if self.reporting {
                trace::emit(&Shown {
                    at: now,
                    display: &self.display,
                });
            }
        }
    }
}

/// The panel's snapshot (`snapshot`): all of it but whether it reports,
/// which is the run's.
#[cfg(feature = "mc683xx")]
mod save {
    use mc683xx_cpu32::state::Save;
    use mc683xx_cpu32::state::StateError;

    use super::FrontPanel;
    use crate::board::snapshot;

    impl Save for FrontPanel {
        fn save(&self) -> Result<Vec<u8>, StateError> {
            Ok(snapshot::save(self))
        }

        fn restore(&mut self, snapshot: &[u8]) -> Result<(), StateError> {
            let reporting = self.reporting;
            *self = snapshot::load(snapshot)?;
            self.reporting = reporting;
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Display;
    use super::FrontPanel;
    use super::Key;
    use super::LATCH_FRAME;
    use super::LATCH_RUN;
    use super::Shown;
    use crate::board::time::Cycles;
    use crate::names::Named as _;

    /// One command and its data, framed as `0x53736` and `0x53796`
    /// frame it: the reply to its first byte.
    fn command(panel: &mut FrontPanel, bytes: &[u8]) -> u8 {
        panel.control(LATCH_RUN, false, Cycles(0));
        let reply = bytes
            .iter()
            .map(|byte| panel.exchange(u16::from(*byte), Cycles(0)) as u8)
            .next()
            .unwrap_or(0);
        for byte in bytes.iter().skip(1) {
            panel.exchange(u16::from(*byte), Cycles(0));
        }
        panel.control(LATCH_RUN | LATCH_FRAME, false, Cycles(0));
        reply
    }

    #[test]
    fn the_self_test_passes() {
        let mut panel = FrontPanel::default();
        panel.control(0, false, Cycles(0));
        panel.control(LATCH_RUN | LATCH_FRAME, false, Cycles(0));
        command(&mut panel, &[0x00]);
        command(&mut panel, &[0x08]);
        assert_eq!(command(&mut panel, &[0x10]), 0x55);
        assert_eq!(command(&mut panel, &[0x00]), 0xaa);
    }

    #[test]
    fn nothing_answers_in_reset_or_unframed() {
        let mut panel = FrontPanel::default();
        panel.control(0, false, Cycles(0));
        assert_eq!(panel.exchange(0x08, Cycles(0)), 0);
        assert_eq!(panel.exchange(0x10, Cycles(0)), 0);
        panel.control(LATCH_RUN | LATCH_FRAME, false, Cycles(0));
        assert_eq!(panel.exchange(0x08, Cycles(0)), 0);
        assert_eq!(command(&mut panel, &[0x00]), 0, "no key");
    }

    #[test]
    fn a_key_is_given_once_with_the_command_after_the_next() {
        let mut panel = FrontPanel::default();
        panel.press(Key::Sat);
        assert_eq!(command(&mut panel, &[0x00]), 0);
        assert_eq!(command(&mut panel, &[0x00]), 5);
        assert_eq!(command(&mut panel, &[0x00]), 0);
    }

    #[test]
    fn the_display_takes_its_lamps_and_text() {
        let mut panel = FrontPanel::default();
        command(&mut panel, &[0x01, 0x20, b'O', b'K']);
        assert_eq!(
            panel.display,
            Display {
                lamps: 0x20,
                text: b"OK".to_vec()
            }
        );
        command(&mut panel, &[0x02, 0x01]);
        command(&mut panel, &[0x04, b'H', b'P']);
        assert_eq!(
            panel.display,
            Display {
                lamps: 0x01,
                text: b"HP".to_vec()
            }
        );
    }

    #[test]
    fn a_frame_closed_with_its_last_byte_in_flight_takes_it() {
        let mut panel = FrontPanel::default();
        panel.control(LATCH_RUN, false, Cycles(0));
        for byte in [0x04, b'H'] {
            panel.exchange(u16::from(byte), Cycles(0));
        }
        panel.control(LATCH_RUN | LATCH_FRAME, true, Cycles(1));
        assert!(panel.display.text.is_empty(), "not yet");
        panel.exchange(u16::from(b'P'), Cycles(2));
        assert_eq!(panel.display.text, b"HP");
        assert_eq!(panel.exchange(0x08, Cycles(3)), 0, "unframed");
    }

    #[test]
    fn a_display_record_is_one_field_a_word() {
        let display = Display {
            lamps: 0x20,
            text: b"OCXO WARM\\UP".to_vec(),
        };
        let shown = Shown {
            at: Cycles(7),
            display: &display,
        };
        assert_eq!(
            shown.to_string(),
            "display cycle=7 lamps=0x20 text=OCXO\\x20WARM\\x5cUP"
        );
    }

    #[test]
    fn keys_go_by_their_names() {
        assert_eq!(Key::from_name("serial"), Some(Key::SerialPort));
        assert_eq!(Key::Shift.name(), "shift");
        assert_eq!(Key::from_name("enter"), None);
    }
}
