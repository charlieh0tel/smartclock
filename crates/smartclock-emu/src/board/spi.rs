//! A word-level model on the QSPI clocked through mc683xx-board's SPI
//! edges (`mc683xx_board::spi`), as the stock chips are: the model's
//! reply shifted out and its word sampled at the edges its part acts
//! on, in the master's mode (SPCR0's CPOL and CPHA, QSMRM 4.3.1).  In
//! the mode the firmware drives the part in, the model sees the word
//! sent and the master the reply whole; in another, both come skewed,
//! as they would from the part.

use mc683xx_board::peripheral::Transfer;
use mc683xx_board::spi::Edges;
use mc683xx_board::spi::Slave;
use mc683xx_board::spi::exchange;

/// A transfer's shift register on the part's side: the model's reply
/// shifted out most significant bit first, and what the part sampled.
#[derive(Debug)]
struct Shifter {
    /// The reply, right-aligned in the transfer's `bits`.
    reply: u16,
    /// The transfer's bits.
    bits: u8,
    /// The reply's bits shifted past so far: MISO is the next.
    shifted: u8,
    /// The bits sampled, the last lowest.
    sampled: u16,
}

impl Slave for Shifter {
    fn sample(&mut self, mosi: bool) {
        self.sampled = self.sampled << 1 | u16::from(mosi);
    }

    fn change(&mut self) {
        self.shifted = self.shifted.saturating_add(1);
    }

    /// The reply's next bit; past its last, undriven.
    fn miso(&self) -> Option<bool> {
        let below = self.bits.checked_sub(self.shifted)?.checked_sub(1)?;
        Some(self.reply >> below & 1 != 0)
    }
}

/// `transfer` to a part with `edges` whose model replies `reply`: what
/// the master captures, and the word the part sampled, both
/// right-aligned in the transfer's bits.
pub(crate) fn shift(transfer: Transfer, edges: Edges, reply: u16) -> (u16, u16) {
    let mut shifter = Shifter {
        reply,
        bits: transfer.bits.min(u16::BITS as u8),
        shifted: 0,
        sampled: 0,
    };
    let captured = exchange(transfer, edges, &mut shifter);
    (captured, shifter.sampled)
}

#[cfg(test)]
mod tests {
    use mc683xx_board::peripheral::Transfer;
    use mc683xx_board::spi::Edge;
    use mc683xx_board::spi::Edges;

    use super::shift;
    use crate::board::system_clock::test_qspi_clock_in;

    /// A frame's first transfer of `bits` of `out` in SPI mode `number`.
    fn transfer(number: u8, out: u16, bits: u8) -> Transfer {
        Transfer::new(out, bits, test_qspi_clock_in(number))
    }

    #[test]
    fn in_its_own_mode_a_part_takes_the_word_and_gives_the_reply() {
        // each mode's own part: it samples on the edge the master
        // captures on and changes on the other, its first bit out at its
        // select with CPHA 0, at its first change with CPHA 1
        for (number, edges) in [
            (0, Edges::new(Edge::Rising, Edge::Falling, true)),
            (1, Edges::new(Edge::Falling, Edge::Rising, false)),
            (2, Edges::new(Edge::Falling, Edge::Rising, true)),
            (3, Edges::new(Edge::Rising, Edge::Falling, false)),
        ] {
            assert_eq!(
                shift(transfer(number, 0x3c, 8), edges, 0xa5),
                (0xa5, 0x3c),
                "mode {number}"
            );
            assert_eq!(
                shift(transfer(number, 0x1234, 16), edges, 0xfedc),
                (0xfedc, 0x1234),
                "mode {number}, 16 bits"
            );
        }
    }

    #[test]
    fn in_another_mode_both_come_skewed() {
        // mode 3's part in mode 0: its first change edge, the first
        // fall, comes after the first capture, so the reply is a bit
        // late behind an undriven 1; it samples on the rise, as mode 0
        // sends, so the word is whole
        let edges = Edges::new(Edge::Rising, Edge::Falling, false);
        assert_eq!(
            shift(transfer(0, 0x3c, 8), edges, 0xa5),
            (0x80 | 0xa5 >> 1, 0x3c)
        );
        // in mode 1 it samples on the rise as the master changes MOSI,
        // a bit early, and the master captures on the fall before it
        // changes, the bit before
        let (captured, sampled) = shift(transfer(1, 0x3c, 8), edges, 0xa5);
        assert_ne!((captured, sampled), (0xa5, 0x3c));
    }
}
