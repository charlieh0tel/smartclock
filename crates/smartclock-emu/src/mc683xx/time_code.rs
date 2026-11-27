//! `--time-code-off`: `:PTIMe:TCODe:CONTinuous OFF` sent on the host
//! port once each power-up's time code starts, as a client of the
//! Z3816A sends it first (docs/emulator.md, "The boards").  The image
//! starts the time code at every power-up, and while it runs the port's
//! write drops everything else (docs/firmware/console.md, "Which
//! port"); the first frame's end shows the port's task running, and the
//! command sent then is taken.  A reset, the power-on's, a power
//! cycle's, a watchdog's or one asked for, waits for the next frame
//! again.

/// The command, ended as a script's `send` ends a line.
const COMMAND: &[u8] = b":PTIMe:TCODe:CONTinuous OFF\r\n";
/// What ends a time code frame: 17 bytes and a CR (console.md).
const FRAME_END: u8 = b'\r';

/// Where the command stands in the power-up the board is in.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) enum TimeCodeOff {
    /// Not asked for: the port as a real unit's.
    Unasked,
    /// Asked for, waiting for the power-up's first frame.
    Waiting,
    /// Sent in this power-up.
    Sent,
}

impl TimeCodeOff {
    /// Waiting for the run's first frame if `asked`.
    pub(super) fn new(asked: bool) -> Self {
        if asked { Self::Waiting } else { Self::Unasked }
    }

    /// After a slice in which the host port sent `sent`, the board
    /// reset in it if `reset`: the command if it is due now, at the
    /// first frame's end; waiting again after a reset.
    pub(super) fn after(&mut self, sent: &[u8], reset: bool) -> Option<&'static [u8]> {
        let due = *self == Self::Waiting && sent.contains(&FRAME_END);
        if due {
            *self = Self::Sent;
        }
        if reset && *self != Self::Unasked {
            *self = Self::Waiting;
        }
        due.then_some(COMMAND)
    }
}

#[cfg(test)]
mod tests {
    use super::COMMAND;
    use super::TimeCodeOff;

    /// The command goes once a power-up, at its first frame's end, and
    /// again after a reset; never unasked.
    #[test]
    fn the_command_goes_at_each_power_ups_first_frame() {
        let mut off = TimeCodeOff::new(true);
        assert_eq!(off.after(b"\x09\x04", false), None, "mid frame");
        assert_eq!(off.after(b"\x01\r", false), Some(COMMAND));
        assert_eq!(off.after(b"scpi > \r\n", false), None, "sent already");
        assert_eq!(off.after(b"", true), None, "reset");
        assert_eq!(off.after(b"\r", false), Some(COMMAND), "again");
        let mut unasked = TimeCodeOff::new(false);
        assert_eq!(unasked.after(b"", true), None);
        assert_eq!(unasked.after(b"\r", false), None);
    }
}
