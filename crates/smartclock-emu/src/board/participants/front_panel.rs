//! The 58503A's front panel on the QSPI.

use mc683xx_board::Board;
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
use mc683xx_cpu32::state::Save;
use mc683xx_cpu32::state::StateError;
use mc683xx_mc68331::Mc68331;

use crate::board::front_panel::Display;
use crate::board::front_panel::FrontPanel;
use crate::board::front_panel::Key;
use crate::board::front_panel::LATCH_FRAME;
use crate::board::front_panel::LATCH_RUN;
use crate::board::parameters::REPORT;
use crate::board::snapshot;
use crate::board::spi;
use crate::board::system_clock::cycles;
use crate::names::Named as _;

/// `hp58503a-front-panel`: the 58503A's front panel on the QSPI, its
/// latch bits as logic inputs, `run` (bit 7, low holding it in reset)
/// and `frame` (bit 6, low framing a command), and its keys as logic
/// inputs `key-time` to `key-shift`, each pressed as it goes low.  The
/// display is traced, with `report = true`, as the Unicorn board's
/// `--indicators` reports it: no port.  A byte is in flight while the
/// panel is selected and has not had it.  RESET does not reach it: the
/// panel is a board of its own.
#[derive(Debug)]
pub(super) struct FrontPanelPart {
    /// The panel.
    panel: FrontPanel,
    /// The latch's bits 6 and 7 as their inputs have them, high until
    /// driven.
    latch: u8,
    /// Each key's input, high until driven.
    keys: [bool; Key::COUNT],
    /// Selected on the QSPI, and its byte not yet come.
    awaiting: bool,
}

impl FrontPanelPart {
    /// Its SPI edges: SPI mode 3's own part, which samples on SCK's rise
    /// and changes on its fall, its first bit out at the first fall
    /// (*inferred*: the panel's controller is not traced; mode 3 is the
    /// mode `qspi_queue_setup` drives it in, 58503a-3704.md, "The
    /// front panel").
    const EDGES: Edges = Edges::new(Edge::Rising, Edge::Falling, false);

    /// Its latch inputs.
    const RUN: usize = 0;
    const FRAME: usize = 1;
    /// Its first key's input; the rest follow in `Key::all`'s order.
    const FIRST_KEY: usize = 2;

    /// Its ports.
    pub(super) fn ports() -> Vec<PortSpec> {
        let latch = [
            PortSpec::input("run", Kind::Logic),
            PortSpec::input("frame", Kind::Logic),
        ];
        let keys =
            Key::all().map(|key| PortSpec::input(format!("key-{}", key.name()), Kind::Logic));
        latch.into_iter().chain(keys).collect()
    }

    /// From `report` (false) and `part`.
    pub(super) fn from_parameters(context: &Context<'_>) -> Result<Self, ParameterError> {
        let mut panel = FrontPanel::default();
        if context.parameters.boolean(REPORT)?.unwrap_or(false) {
            panel.report();
        }
        Ok(Self {
            panel,
            latch: LATCH_RUN | LATCH_FRAME,
            keys: [true; Key::COUNT],
            awaiting: false,
        })
    }
}

/// `key` pressed on `board`'s front panel, the part `name`: whether
/// the board has one.
pub(crate) fn press(board: &mut Board<Mc68331>, name: &str, key: Key) -> bool {
    board
        .peripheral_mut::<FrontPanelPart>(name)
        .ok()
        .map(|mut part| part.panel.press(key))
        .is_some()
}

/// What `board`'s front panel, the part `name`, shows: none if the
/// board has none.
pub(crate) fn display(board: &Board<Mc68331>, name: &str) -> Option<Display> {
    board
        .peripheral::<FrontPanelPart>(name)
        .ok()
        .map(|part| part.panel.display().clone())
}

impl Peripheral for FrontPanelPart {
    fn save(&self) -> Result<Vec<u8>, StateError> {
        let panel = Save::save(&self.panel)?;
        Ok(snapshot::save(&(
            panel,
            self.latch,
            self.keys,
            self.awaiting,
        )))
    }

    fn restore(&mut self, snapshot: &[u8]) -> Result<(), StateError> {
        let (panel, latch, keys, awaiting): (Vec<u8>, _, _, _) = snapshot::load(snapshot)?;
        Save::restore(&mut self.panel, &panel)?;
        self.latch = latch;
        self.keys = keys;
        self.awaiting = awaiting;
        Ok(())
    }

    fn transfer(&mut self, transfer: Transfer, drive: &mut Drive<'_>) -> u16 {
        self.awaiting = false;
        let (back, byte) = spi::shift(transfer, Self::EDGES, u16::from(self.panel.reply()));
        self.panel.exchange(byte, cycles(drive, drive.now()));
        back
    }

    fn set_selected(&mut self, _: usize, selected: bool, _: &mut Drive<'_>) {
        self.awaiting = selected;
    }

    fn input(&mut self, port: usize, signal: Signal, drive: &mut Drive<'_>) {
        let now = drive.now();
        let Signal::Level(high) = signal else { return };
        let bit = match port {
            Self::RUN => LATCH_RUN,
            Self::FRAME => LATCH_FRAME,
            _ => {
                let index = port - Self::FIRST_KEY;
                if let (Some(key), Some(level)) = (Key::all().nth(index), self.keys.get_mut(index))
                {
                    if *level && !high {
                        self.panel.press(key);
                    }
                    *level = high;
                }
                return;
            }
        };
        self.latch = if high {
            self.latch | bit
        } else {
            self.latch & !bit
        };
        self.panel
            .control(self.latch, self.awaiting, cycles(drive, now));
    }
}
