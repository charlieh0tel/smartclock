//! The board file's run-time endpoints and the serial lines the run
//! binds them to: `host`, `sci` and `gps` (`ports`), a character on
//! each framed as the line's far end frames it; `gps_pps`, the external
//! engine's 1 PPS; `alarm`, a level the board drives, reported.

use smartclock::types::Framing;

use crate::line::Line;

/// The run-time endpoints the board files name beside the ports': the
/// external engine's 1 PPS, which `--gps` wires in, and the 58503A's
/// alarm output.
pub(super) const GPS_PPS: &str = "gps_pps";
pub(super) const ALARM: &str = "alarm";

/// The alarm output's report, as the Unicorn board's (board/mod.rs).
pub(super) const ALARM_BANK: &str = "output";

/// A run-time endpoint bound to a serial line the command line serves.
#[derive(Debug)]
pub(super) struct Bound {
    /// The endpoint.
    pub(super) name: &'static str,
    /// The line, the board's end of it.
    pub(super) line: Line,
    /// How the line's far end frames a character.
    pub(super) framing: Framing,
}

/// `byte` as a frame's bits on a line framed `framing`: its data bits,
/// then its parity bit if it has one (MC68681 3.1), as the stock
/// DUART's `Frame::Data` carries them.
pub(super) fn frame_bits(framing: Framing, byte: u8) -> u16 {
    let bits = framing.data_bits();
    let data = u16::from(byte) & ((1 << bits) - 1);
    match framing.odd_parity() {
        None => data,
        Some(odd) => data | u16::from((data.count_ones() % 2 == 1) != odd) << bits,
    }
}

/// A frame's data bits on a line framed `framing`, its parity bit left
/// off: the character the far end reads.
pub(super) fn frame_byte(framing: Framing, bits: u16) -> u8 {
    (bits & ((1 << framing.data_bits()) - 1)) as u8
}
