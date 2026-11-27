//! The Z3817A's EFC shift register on the QSPI, clocked on SCK's fall
//! (docs/firmware/z3817a-3752.md, "The board as the image drives it").

use mc683xx_board::peripheral::Drive;
use mc683xx_board::peripheral::Peripheral;
use mc683xx_board::peripheral::Transfer;
use mc683xx_board::registry::Context;
use mc683xx_board::registry::ParameterError;
use mc683xx_board::registry::PortSpec;
use mc683xx_board::signal::Kind;
use mc683xx_board::signal::Signal;
use mc683xx_board::spi::Edge;
use mc683xx_board::spi::Edges;
use mc683xx_cpu32::state::StateError;

use crate::board::snapshot;
use crate::board::spi;

/// Its parameter: the word its stages hold at power-up.
const INITIAL: &str = "initial";

/// `efc-shift-register`: sixteen stages, shifting MOSI in on SCK's fall
/// and giving the last stage on MISO, the stages a bus output `Q` to
/// the EFC DAC; `initial` (0) the word at power-up.  Clocked on the
/// fall, a write in SPI mode 2, the Z3817A's (`qspi_queue_setup`
/// device 5, SPCR0 `0x8204`), takes each bit as sent, and a read in
/// mode 3 (`0x8304`), whose master captures on the rise, gets the word
/// a bit early, as its EFC self-test expects, shifting the reading
/// right once before comparing (0x2887e, 0x288c4).  *Inferred*: the
/// Z3805A's chain, two 74HC164s, shifts on the rise and its test does
/// not shift; the part is not seen.
#[derive(Debug)]
pub(super) struct EfcShiftPart {
    /// The stages, the last the most significant bit.
    stages: u16,
}

impl EfcShiftPart {
    /// Its edges: it samples and shifts on the fall, its last stage on
    /// MISO from its select.
    const EDGES: Edges = Edges::new(Edge::Falling, Edge::Falling, true);

    /// Its port.
    const Q: usize = 0;

    /// Its ports.
    pub(super) fn ports() -> Vec<PortSpec> {
        vec![PortSpec::output("Q", Kind::Bus)]
    }

    /// From its parameters (`EfcShiftPart`).
    pub(super) fn from_parameters(context: &Context<'_>) -> Result<Self, ParameterError> {
        Ok(Self {
            stages: context.parameters.integer(INITIAL)?.unwrap_or(0),
        })
    }
}

impl Peripheral for EfcShiftPart {
    fn power_on(&mut self, drive: &mut Drive<'_>) {
        drive.drive(Self::Q, Signal::Bus(u32::from(self.stages)));
    }

    fn save(&self) -> Result<Vec<u8>, StateError> {
        Ok(snapshot::save(&self.stages))
    }

    fn restore(&mut self, snapshot: &[u8]) -> Result<(), StateError> {
        self.stages = snapshot::load(snapshot)?;
        Ok(())
    }

    fn transfer(&mut self, transfer: Transfer, drive: &mut Drive<'_>) -> u16 {
        let (back, sampled) = spi::shift(transfer, Self::EDGES, self.stages);
        let bits = u32::from(transfer.bits.min(u16::BITS as u8));
        self.stages = if bits >= u16::BITS {
            sampled
        } else {
            self.stages << bits | sampled
        };
        drive.drive(Self::Q, Signal::Bus(u32::from(self.stages)));
        back
    }

    /// No: it drives `Q` from its transfers.
    fn qspi_deferrable(&self) -> bool {
        false
    }
}
