//! The front panel in a browser, `--panel ADDR` (docs/emulator.md,
//! "The panel page"): a page, served by the host (natively with
//! `smartclock::http`), that draws the receiver's panel and polls the
//! run for what it shows, and takes its keys back.
//!
//! The run loop owns the board.  It publishes a `Snapshot`, what the
//! panel shows, into a mutex every `PUBLISH_EVERY` of board time; the
//! server's threads read that copy and never touch the board.  A key
//! clicked on the page goes into a bounded queue that the run loop
//! empties at the end of a slice, where a script's keys are pressed, and
//! traces each as an `Applied` record, which a script replays.  The run
//! never waits on the server: a publish takes the lock for a swap, the
//! queue is read without blocking, and with nobody polling neither does
//! more than that.
//!
//! `state` is what the page shows, `routes` what the server answers,
//! `record` what the run traces of what the page gave it, and `console`
//! the console on the host port.

use std::sync::Arc;
use std::sync::Mutex;
use std::sync::PoisonError;
use std::sync::mpsc::Receiver;
use std::sync::mpsc::sync_channel;

use mc683xx_board::time::PICOSECONDS_PER_SECOND;
use mc683xx_board::time::Span;

use crate::board::front_panel::Key;
use crate::board::time::Cycles;
use crate::control::Control;
use crate::line::Line;

use self::console::Console;
use self::console::Port;
use self::routes::Shared;
use self::state::Snapshot;

/// How many clicked keys, or controls, may wait for the run loop; past
/// it a click is answered busy.  The firmware takes a key a poll, about
/// every 50 ms (58503a-3704.md, "The front panel", "When"), and the run
/// a control at the end of a slice, so more than a few waiting is a
/// stuck run, not a fast hand.
const CLICKS_QUEUED: usize = 16;

/// The address `--panel` binds: as given, or a bare port on the
/// loopback interface alone.
const LOOPBACK: &str = "127.0.0.1";

/// The run loop's end of the page: where it publishes, and the keys
/// clicked.
#[derive(Debug)]
pub(crate) struct Panel {
    /// The last snapshot published, which the server reads.
    snapshot: Arc<Mutex<Snapshot>>,
    /// The clicked keys.
    keys: Receiver<Key>,
    /// The clicked controls.
    controls: Receiver<Control>,
    /// The console, whose output the run loop keeps.
    console: Arc<Console>,
    /// The lines typed on the console.
    typed: Receiver<Vec<u8>>,
}

/// How often the run loop publishes, of board time: 64
/// times a board second, past the page's polls (`routes::POLL`) and the
/// display's own changes, 100 ticks a frame at its fastest.
pub(crate) const PUBLISH_EVERY: Span = Span::from_picos(PICOSECONDS_PER_SECOND / 64);

/// The address `--panel`'s `text` names: a bare port on the loopback
/// interface alone, else as given.
pub(crate) fn address(text: &str) -> String {
    match text.parse::<u16>() {
        Ok(port) => format!("{LOOPBACK}:{port}"),
        Err(_) => text.to_owned(),
    }
}

impl Panel {
    /// The run loop's end and the server's, from `first`, the console
    /// on `port`, named for `started`, UTC as the run started: the
    /// server, the host's (`host::Ports::serve_page`), answers each
    /// request with the second.
    pub(crate) fn new(first: Snapshot, port: Port, started: jiff::Timestamp) -> (Self, Shared) {
        let snapshot = Arc::new(Mutex::new(first));
        let (key_sender, keys) = sync_channel(CLICKS_QUEUED);
        let (control_sender, controls) = sync_channel(CLICKS_QUEUED);
        let (console, typed) = Console::new(port, started);
        let console = Arc::new(console);
        let shared = Shared {
            snapshot: Arc::clone(&snapshot),
            keys: key_sender,
            controls: control_sender,
            console: Arc::clone(&console),
        };
        (
            Self {
                snapshot,
                keys,
                controls,
                console,
                typed,
            },
            shared,
        )
    }

    /// What the host port sent, kept for the console.
    pub(crate) fn sent(&self, bytes: &[u8]) {
        if !bytes.is_empty() {
            self.console.sent(bytes);
        }
    }

    /// The lines typed on the console since the last call, each with
    /// the port's terminator, put on `line` while no far end is attached
    /// and traced as put on the port at `cycle`, `at` on the run's
    /// clock; dropped if a TCP client has taken the port since.
    pub(crate) fn type_lines(&self, line: &Line, cycle: Cycles, at: Cycles) {
        record::take_typed(&self.typed, cycle, at, |bytes| {
            line.inject_unattached(bytes)
        });
    }

    /// `snapshot` published for the page.
    pub(crate) fn publish(&self, snapshot: Snapshot) {
        *self.snapshot.lock().unwrap_or_else(PoisonError::into_inner) = snapshot;
    }

    /// The keys clicked since the last call, in order, each traced as
    /// pressed at `cycle`, `at` on the run's clock.
    pub(crate) fn take_keys(&self, cycle: Cycles, at: Cycles) -> Vec<Key> {
        record::take(&self.keys, cycle, at)
    }

    /// The controls clicked since the last call, in order, each traced
    /// as applied at `cycle`, `at` on the run's clock.
    pub(crate) fn take_controls(&self, cycle: Cycles, at: Cycles) -> Vec<Control> {
        record::take(&self.controls, cycle, at)
    }
}

pub(crate) mod console;
mod json;
mod record;
pub mod routes;
pub(crate) mod state;
#[cfg(test)]
mod tests;
