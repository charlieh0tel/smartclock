//! The panel page's console on the host port (docs/emulator.md, "The
//! console"): what the receiver sends on the port, mirrored into a
//! bounded buffer the page reads by offset, and lines typed on the page,
//! queued whole for the run loop, which puts them on the port at the
//! end of a slice and traces each as a `console` record that a script's
//! `at` and `send` steps replay.
//!
//! One typist at a time: a TCP client on `--host` while one is
//! connected, every page then read-only; with none, or with no `--host`
//! at all, the page holding the writer lease, which its polls renew and
//! which lapses `LEASE` after its last.  With a script on the port, it
//! is the typist, and every page is read-only.

use std::collections::VecDeque;
use std::sync::Mutex;
use std::sync::MutexGuard;
use std::sync::PoisonError;
use std::sync::atomic::AtomicU64;
use std::sync::atomic::Ordering;
use std::sync::mpsc::Receiver;
use std::sync::mpsc::SyncSender;
use std::sync::mpsc::TrySendError;
use std::time::Duration;

use serde::Serialize;
use smartclock::http::Response;
use smartclock::http::Status;

use super::json;
use super::json::Bytes;
use super::routes::JSON;
use crate::line::Line;

/// How much of the port's output is kept for the page, bytes: a few
/// screens of SCPI answers, or a pForth listing's worth.
pub(super) const KEPT: usize = 64 * 1024;
/// How long a page's writer lease lasts after its last poll: a few of
/// its polls, so a page that is closed or asleep frees the port soon.
pub(super) const LEASE: Duration = Duration::from_secs(5);
/// The longest line a page may type, bytes: past the longest SCPI
/// command the firmware's line reader takes.
pub(super) const LINE_LIMIT: usize = 256;
/// How many typed lines may wait for the run loop.
pub(super) const LINES_QUEUED: usize = 16;

/// The query parameters: the offset read from, and the page's own name
/// for itself, which its lease is held by.
pub(super) const SINCE: &str = "since";
pub(super) const CLIENT: &str = "client";

/// A quick line: a button above the console's input that sends its line
/// as though typed.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub(super) struct QuickLine {
    /// The button's label.
    pub(super) label: &'static str,
    /// The line it sends.
    pub(super) line: &'static str,
}

/// The console's quick lines: the identity, the status screen, the error
/// queue, and into the pForth console and out again with its own word,
/// as smartclock's console.rs enters and leaves it.  None that discards
/// the receiver's learned state, strands its serial link or erases its
/// flash (docs/emulator.md, "The console").
pub(super) const QUICK_LINES: [QuickLine; 5] = [
    QuickLine {
        label: "*IDN?",
        line: "*IDN?",
    },
    QuickLine {
        label: ":SYSTem:STATus?",
        line: ":SYSTem:STATus?",
    },
    QuickLine {
        label: ":SYSTem:ERRor?",
        line: ":SYSTem:ERRor?",
    },
    QuickLine {
        label: "pForth",
        line: ":SYSTem:LANGuage \"PFORTH\"",
    },
    QuickLine {
        label: "halt",
        line: "halt",
    },
];

/// Each typist's name in the console's state, with how the page says it
/// has the port, in `Typist`'s order.
pub(super) const TYPISTS: [(&str, &str); 4] = [
    ("tcp", "a TCP client has it"),
    ("script", "a script has it"),
    ("page", "another page has it"),
    ("nobody", "nobody has it"),
];

/// Who is on the far end of the host port, which decides who may type.
#[derive(Debug, Clone)]
pub(crate) enum Port {
    /// Served over TCP with `--host`: the line, which says whether a
    /// client is connected.
    Tcp(Line),
    /// A script's, `--script`.
    Script,
    /// The page's alone, with neither.
    Page,
}

/// Who types at the port now.
#[derive(Debug, Clone, PartialEq, Eq)]
enum Typist {
    /// A TCP client.
    Tcp,
    /// The script.
    Script,
    /// The page holding the lease, by its name.
    Page(String),
    /// Nobody.
    Nobody,
}

impl Typist {
    /// Its name in the console's state (`TYPISTS`).
    fn name(&self) -> &'static str {
        let index = match self {
            Self::Tcp => 0,
            Self::Script => 1,
            Self::Page(_) => 2,
            Self::Nobody => 3,
        };
        TYPISTS[index].0
    }

    /// Why `client` may not type while it types, if it may not.
    fn refusal(&self, client: Option<&str>) -> Option<&'static str> {
        match self {
            Self::Tcp => Some("a TCP client holds the host port"),
            Self::Script => Some("a script holds the host port"),
            Self::Page(holder) if Some(holder.as_str()) == client => None,
            Self::Page(_) => Some("another page holds the host port"),
            Self::Nobody => Some("this page has not taken the host port"),
        }
    }
}

/// The writer lease: the page holding it, and when it last polled.
#[derive(Debug)]
struct Lease {
    /// The page's name for itself.
    holder: String,
    /// Its last poll, on the host's clock.
    renewed: Duration,
}

/// The port's output kept: its last `KEPT` bytes, and the offset after
/// the last, counted from the run's start.
#[derive(Debug, Default)]
struct Output {
    /// The bytes kept.
    bytes: VecDeque<u8>,
    /// The offset after the last byte sent.
    end: u64,
}

impl Output {
    /// The offset of the first byte kept.
    fn start(&self) -> u64 {
        self.end - self.bytes.len() as u64
    }
}

/// The console as the server's threads share it with the run loop.
#[derive(Debug)]
pub(super) struct Console {
    /// Who is on the port's far end.
    port: Port,
    /// The output kept.
    output: Mutex<Output>,
    /// The writer lease, while a page holds it.
    lease: Mutex<Option<Lease>>,
    /// The lines typed, to the run loop.
    lines: SyncSender<Vec<u8>>,
    /// This run's name, which no other run's shares, so that a page
    /// reading by offset knows when the emulator has started again and
    /// its offsets with it.
    run: String,
}

/// A name for a run's console that no other's shares: natively the
/// process's id, when the run started, `started`, and how many consoles
/// the process made before.
fn run_name(started: jiff::Timestamp) -> String {
    static MADE: AtomicU64 = AtomicU64::new(0);
    let nanos = started.as_nanosecond().max(0);
    let made = MADE.fetch_add(1, Ordering::Relaxed);
    #[cfg(not(target_family = "wasm"))]
    {
        format!("{}-{nanos}-{made}", std::process::id())
    }
    // a browser's page has no process to name
    #[cfg(target_family = "wasm")]
    {
        format!("{nanos}-{made}")
    }
}

/// `mutex`'s lock: each holder leaves its value whole.
fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex.lock().unwrap_or_else(PoisonError::into_inner)
}

/// A refusal of `status`, saying `why`.
fn refused(status: Status, why: &str) -> Response {
    Response::text(status, format!("{why}\n"))
}

/// `GET /console`'s object.
#[derive(Debug, Serialize)]
struct ReadJson<'a> {
    /// The offset `text` starts at.
    from: u64,
    /// The offset after it.
    offset: u64,
    /// The port's bytes.
    text: Bytes<'a>,
    /// Who types (`TYPISTS`).
    typist: &'static str,
    /// Whether the page asking may type.
    writable: bool,
    /// Why not, if not.
    reason: Option<&'static str>,
    /// The run's name (`Console::run`).
    run: &'a str,
}

impl Console {
    /// The console of `port`, named for `started`, UTC as the run
    /// started, and the run loop's end of its lines.
    pub(super) fn new(port: Port, started: jiff::Timestamp) -> (Self, Receiver<Vec<u8>>) {
        let (lines, typed) = std::sync::mpsc::sync_channel(LINES_QUEUED);
        (
            Self {
                port,
                output: Mutex::default(),
                lease: Mutex::default(),
                lines,
                run: run_name(started),
            },
            typed,
        )
    }

    /// The port's `bytes`, sent now, kept.
    pub(super) fn sent(&self, bytes: &[u8]) {
        let mut output = lock(&self.output);
        output.bytes.extend(bytes);
        output.end += bytes.len() as u64;
        let over = output.bytes.len().saturating_sub(KEPT);
        output.bytes.drain(..over);
    }

    /// Who types at `now`, `client` taking or renewing the lease if it
    /// may: a TCP client connected ends any page's.
    fn typist(&self, client: Option<&str>, now: Duration) -> Typist {
        let mut lease = lock(&self.lease);
        match &self.port {
            Port::Script => return Typist::Script,
            Port::Tcp(line) if line.connected() => {
                *lease = None;
                return Typist::Tcp;
            }
            Port::Tcp(_) | Port::Page => {}
        }
        if lease
            .as_ref()
            .is_some_and(|held| now.saturating_sub(held.renewed) >= LEASE)
        {
            *lease = None;
        }
        match (lease.as_mut(), client) {
            (Some(held), Some(client)) if held.holder == client => held.renewed = now,
            (None, Some(client)) => {
                *lease = Some(Lease {
                    holder: client.to_owned(),
                    renewed: now,
                });
            }
            _ => {}
        }
        lease
            .as_ref()
            .map_or(Typist::Nobody, |held| Typist::Page(held.holder.clone()))
    }

    /// `GET /console?since=N&client=ID`: the output after offset N, from
    /// the first kept if N is older, its offset after, and who types, a
    /// poll renewing `client`'s lease.
    pub(super) fn read(&self, since: u64, client: Option<&str>, now: Duration) -> Response {
        let typist = self.typist(client, now);
        let (from, end, text) = {
            let output = lock(&self.output);
            let from = since.clamp(output.start(), output.end);
            // within the kept bytes: from is at least the start
            let skip = (from - output.start()) as usize;
            let text: Vec<u8> = output.bytes.iter().skip(skip).copied().collect();
            (from, output.end, text)
        };
        let refusal = typist.refusal(client);
        Response::ok(
            JSON,
            json::to_string(&ReadJson {
                from,
                offset: end,
                text: Bytes(&text),
                typist: typist.name(),
                writable: refusal.is_none(),
                reason: refusal,
                run: &self.run,
            }),
        )
    }

    /// `POST /console?client=ID` with a line: queued whole, with the
    /// port's terminator, if `client` may type; 409 with the reason if
    /// not, 400 for what is not one line, 503 for a full queue or a run
    /// that has ended.
    pub(super) fn write(&self, body: &[u8], client: Option<&str>, now: Duration) -> Response {
        if let Some(why) = self.typist(client, now).refusal(client) {
            return refused(Status::Conflict, why);
        }
        let line = body
            .strip_suffix(b"\r\n")
            .or_else(|| body.strip_suffix(b"\n"))
            .unwrap_or(body);
        if line.contains(&b'\r') || line.contains(&b'\n') {
            return refused(Status::BadRequest, "one line at a time");
        }
        if line.len() > LINE_LIMIT {
            return refused(
                Status::BadRequest,
                &format!("a line is at most {LINE_LIMIT} bytes"),
            );
        }
        match self.lines.try_send(line.to_vec()) {
            Ok(()) => Response::text(Status::Ok, "queued\n"),
            Err(TrySendError::Full(_)) => refused(Status::Unavailable, "lines waiting"),
            Err(TrySendError::Disconnected(_)) => refused(Status::Unavailable, "the run has ended"),
        }
    }
}

#[cfg(test)]
mod tests;
