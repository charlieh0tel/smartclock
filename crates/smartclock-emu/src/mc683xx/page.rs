//! The panel page as the runner serves it (`--panel`; docs/emulator.md,
//! "The panel page"): published every `PUBLISH_EVERY` of board time,
//! its keys, controls and console lines taken at a slice's end, or a
//! paced frame's quantum's.

use mc683xx_board::time::Time;

use super::Error;
use super::powered::Live;
use crate::config::HostPort;
use crate::host::Ports;
use crate::line::Line;
use crate::panel;
use crate::panel::PUBLISH_EVERY;
use crate::panel::Panel;
use crate::panel::console::Port;
use crate::panel::state::Drawing;

/// The page, served.
#[derive(Debug)]
pub(super) struct Page {
    /// Where the runner publishes and takes the clicked keys.
    pub(super) panel: Panel,
    /// The board's drawing.
    drawing: Drawing,
    /// When it is next published, on the power-up's board time.
    due: Time,
}

impl Page {
    /// The page served at `address`, `--panel`'s, by `ports`, the board
    /// `powered` drawn as its model's, its console on `host`, the host
    /// port's line as `port` puts its far end, named for `started`, UTC
    /// as the run started.
    pub(super) fn serve(
        address: &str,
        ports: &mut dyn Ports,
        powered: &Live<'_>,
        drawing: Drawing,
        port: &HostPort,
        host: Option<&Line>,
        started: jiff::Timestamp,
    ) -> Result<Self, Error> {
        let address = panel::address(address);
        let port = match port {
            HostPort::Script(_) => Port::Script,
            HostPort::Tcp(_) => host.map_or(Port::Script, |line| Port::Tcp(line.clone())),
            HostPort::Panel | HostPort::Stdio => Port::Page,
        };
        let (panel, page) = Panel::new(powered.snapshot(drawing), port, started);
        ports
            .serve_page(&address, page)
            .map_err(|source| Error::Panel {
                address: address.clone(),
                source,
            })?;
        Ok(Self {
            panel,
            drawing,
            due: powered.now(),
        })
    }

    /// `powered`'s snapshot published, when it is due or when `forced`.
    pub(super) fn publish(&mut self, powered: &Live<'_>, forced: bool) {
        let at = powered.now();
        if !forced && at < self.due {
            return;
        }
        self.due = at + PUBLISH_EVERY;
        self.panel.publish(powered.snapshot(self.drawing));
    }

    /// Published at the next chance: the board was built again, its
    /// clock from zero.
    pub(super) fn due_now(&mut self) {
        self.due = Time::ZERO;
    }
}
