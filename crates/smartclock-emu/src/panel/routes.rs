//! What the page's server answers, by path: the page's files, the
//! constants the page shares with the run, the state, and the keys,
//! controls and console lines the page posts (docs/emulator.md, "The
//! panel page").

use std::collections::BTreeMap;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::PoisonError;
use std::sync::mpsc::SyncSender;
use std::sync::mpsc::TrySendError;
use std::time::Duration;

use serde::Serialize;
use smartclock::http::Method;
use smartclock::http::Request;
use smartclock::http::Response;
use smartclock::http::Status;

use crate::board::front_panel::Key;
use crate::control::Control;
use crate::names::Named;

use super::console::CLIENT;
use super::console::Console;
use super::console::KEPT;
use super::console::LINE_LIMIT;
use super::console::QUICK_LINES;
use super::console::QuickLine;
use super::console::SINCE;
use super::console::TYPISTS;
use super::json;
use super::record::Queued;
use super::state::Snapshot;

/// The media types served.
const HTML: &str = "text/html; charset=utf-8";
const CSS: &str = "text/css; charset=utf-8";
const JAVASCRIPT: &str = "text/javascript; charset=utf-8";
pub(super) const JSON: &str = "application/json";

/// How often the page asks for the state and the console's output.
pub(super) const POLL: Duration = Duration::from_millis(150);

/// The query parameter naming what is queued.
const NAME: &str = "name";

/// What a path serves.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Route {
    /// One of the page's files, built into the binary: its media type
    /// and its text.
    File(&'static str, &'static str),
    /// The constants the page shares with the run (`Constants`).
    Constants,
    /// The last snapshot published.
    State,
    /// A key clicked.
    Key,
    /// A simulation control clicked.
    Event,
    /// The console: its output read, or a line typed.
    Console,
}

impl Route {
    /// Whether it takes `method`: the page's files and the state a GET,
    /// a key or a control a POST, the console either.
    fn takes(self, method: Method) -> bool {
        match self {
            Self::File(..) | Self::Constants | Self::State => method == Method::Get,
            Self::Key | Self::Event => method == Method::Post,
            Self::Console => method != Method::Other,
        }
    }

    /// The methods it takes, as `Allow` lists them.
    fn allow(self) -> &'static str {
        match self {
            Self::File(..) | Self::Constants | Self::State => "GET, HEAD",
            Self::Key | Self::Event => "POST",
            Self::Console => "GET, HEAD, POST",
        }
    }
}

/// Every path served, with what it serves: the page, its style, its
/// script and its drawings', the display's font, and the endpoints the
/// script asks.
const ROUTES: [(&str, Route); 10] = [
    ("/", Route::File(HTML, include_str!("page/panel.html"))),
    (
        "/panel.css",
        Route::File(CSS, include_str!("page/panel.css")),
    ),
    (
        "/panel.js",
        Route::File(JAVASCRIPT, include_str!("page/panel.js")),
    ),
    (
        "/drawings.js",
        Route::File(JAVASCRIPT, include_str!("page/drawings.js")),
    ),
    (
        "/font.json",
        Route::File(JSON, include_str!("page/font.json")),
    ),
    ("/constants.json", Route::Constants),
    ("/state", Route::State),
    ("/key", Route::Key),
    ("/event", Route::Event),
    ("/console", Route::Console),
];

/// The control strip's controls, each by the name the page posts.
#[derive(Debug, Serialize)]
struct ControlNames {
    /// The antenna pulled.
    antenna_off: &'static str,
    /// The antenna put back.
    antenna_on: &'static str,
    /// The warm-up skipped.
    skip_warm_up: &'static str,
    /// The power cycled.
    power_cycle: &'static str,
    /// The CPU reset.
    reset: &'static str,
}

/// What the page takes from the run rather than keeping a copy of its
/// own, `/constants.json`.
#[derive(Debug, Serialize)]
struct Constants {
    /// How often the page polls, milliseconds (`POLL`).
    poll_ms: u128,
    /// The longest line the console takes, bytes.
    line_limit: usize,
    /// How much of the port's output the run keeps, bytes; the pane
    /// keeps as many characters.
    kept: usize,
    /// The controls' names.
    controls: ControlNames,
    /// Each typist's name, with how the page says it has the port.
    typists: BTreeMap<&'static str, &'static str>,
    /// The console's quick lines.
    quick_lines: &'static [QuickLine],
}

/// `/constants.json`'s object.
fn constants() -> String {
    json::to_string(&Constants {
        poll_ms: POLL.as_millis(),
        line_limit: LINE_LIMIT,
        kept: KEPT,
        controls: ControlNames {
            antenna_off: Control::AntennaOff.name(),
            antenna_on: Control::AntennaOn.name(),
            skip_warm_up: Control::SkipWarmUp.name(),
            power_cycle: Control::PowerCycle.name(),
            reset: Control::Reset.name(),
        },
        typists: TYPISTS.into_iter().collect(),
        quick_lines: &QUICK_LINES,
    })
}

/// `query`'s `name`, queued on `sender`: 400 for no such name, 503 for
/// a full queue or a run that has ended.
fn queue<T: Queued>(sender: &SyncSender<T>, query: &str) -> Response {
    let Some(what) = smartclock::http::value(query, NAME)
        .as_deref()
        .and_then(T::from_name)
    else {
        return Response::text(
            Status::BadRequest,
            format!("a {} is one of {}\n", T::KIND, T::names()),
        );
    };
    match sender.try_send(what) {
        Ok(()) => Response::text(Status::Ok, "queued\n"),
        Err(TrySendError::Full(_)) => {
            Response::text(Status::Unavailable, format!("{}s waiting\n", T::KIND))
        }
        Err(TrySendError::Disconnected(_)) => {
            Response::text(Status::Unavailable, "the run has ended\n")
        }
    }
}

/// What the server's threads share with the run loop: each request
/// answered (`respond`).
#[derive(Debug, Clone)]
pub struct Shared {
    /// The last snapshot published.
    pub(super) snapshot: Arc<Mutex<Snapshot>>,
    /// The clicked keys, to the run loop.
    pub(super) keys: SyncSender<Key>,
    /// The clicked controls, to the run loop.
    pub(super) controls: SyncSender<Control>,
    /// The console on the host port.
    pub(super) console: Arc<Console>,
}

impl Shared {
    /// The answer to `request`, by its path, and 405 to a method its
    /// path does not take, `now` the host's clock's reading, from any
    /// origin it keeps for the run's life, never going back.
    pub fn respond(&self, request: &Request, now: Duration) -> Response {
        let (path, query) = request
            .target
            .split_once('?')
            .unwrap_or((&request.target, ""));
        let Some(&(_, route)) = ROUTES.iter().find(|(served, _)| *served == path) else {
            return Response::not_found();
        };
        if !route.takes(request.method) {
            return Response::method_not_allowed(route.allow());
        }
        match route {
            Route::File(kind, body) => Response::ok(kind, body.to_owned()),
            Route::Constants => Response::ok(JSON, constants()),
            Route::State => {
                let snapshot = self
                    .snapshot
                    .lock()
                    .unwrap_or_else(PoisonError::into_inner)
                    .clone();
                Response::ok(JSON, snapshot.json())
            }
            Route::Key => queue(&self.keys, query),
            Route::Event => queue(&self.controls, query),
            Route::Console => {
                let client = smartclock::http::value(query, CLIENT);
                if request.method == Method::Post {
                    self.console.write(&request.body, client.as_deref(), now)
                } else {
                    let since = smartclock::http::value(query, SINCE)
                        .and_then(|since| since.parse().ok())
                        .unwrap_or(0);
                    self.console.read(since, client.as_deref(), now)
                }
            }
        }
    }
}
