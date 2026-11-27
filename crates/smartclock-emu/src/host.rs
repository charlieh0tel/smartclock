//! What the host gives a run (`Ports`): the far ends of the board's
//! serial lines it serves, a TCP server's or standard input and
//! output's, the SCI's capture, and the panel page's server; and the
//! host port's far end as the run puts it (`config::HostPort`): a
//! script, a TCP server, the panel page's console, or standard input
//! and output.

use std::io::Read;
use std::io::Write;
use std::path::Path;

#[cfg(feature = "mc683xx")]
use smartclock::http::ServeError;

use crate::config::HostPort;
use crate::config::RunConfig;
use crate::files::Files;
use crate::line::FarEnd;
use crate::line::Line;
#[cfg(feature = "mc683xx")]
use crate::panel::routes::Shared;
use crate::run::Alongside;
use crate::script::Error as ScriptError;
use crate::script::Script;
use crate::script::ScriptWiring;
use crate::trace;

/// A far end a server puts on a line for each connection: read and
/// written as the line's far end is.
pub trait Duplex: Read + Write {}

impl<T: Read + Write> Duplex for T {}

/// Makes a connection's far end: a line's attached afresh, or one
/// wrapped (the Unicorn core's GPS port, its marks counted).
pub type Attach = Box<dyn Fn() -> Box<dyn Duplex> + Send>;

/// What the host gives a run beyond the board.
pub trait Ports {
    /// Serves the port `name` on `address`, one connection at a time, as
    /// a serial port takes one cable, each connection's far end from
    /// `attach`.
    ///
    /// # Errors
    ///
    /// The address not served.
    fn serve(&mut self, address: &str, name: &'static str, attach: Attach) -> std::io::Result<()>;

    /// The host port on the host's own console, `far` its end of the
    /// line: what runs alongside the board for it.
    fn console(&mut self, far: FarEnd) -> Box<dyn Alongside>;

    /// Where every byte the SCI sends is written, `--sci-capture`'s
    /// `path`, made afresh.
    ///
    /// # Errors
    ///
    /// The file not made.
    fn capture(&mut self, path: &Path) -> std::io::Result<Box<dyn Write + Send>>;

    /// Serves the panel page on `address`, each request answered by
    /// `page`.
    ///
    /// # Errors
    ///
    /// The address not served.
    #[cfg(feature = "mc683xx")]
    fn serve_page(&mut self, address: &str, page: Shared) -> Result<(), ServeError>;
}

/// The far end of each connection to `line`: the line attached afresh.
pub(crate) fn attach(line: Line) -> Attach {
    Box::new(move || Box::new(line.attach()))
}

/// Why a line's far end could not be put on.
#[derive(Debug, thiserror::Error)]
pub enum HostError {
    /// A script that cannot be run.
    #[error(transparent)]
    Script(#[from] ScriptError),
    /// An address not served.
    #[error("listening on {address}")]
    Serve {
        /// The address.
        address: String,
        /// Why.
        #[source]
        source: std::io::Error,
    },
    /// The SCI's capture not made.
    #[error("creating {path}")]
    Capture {
        /// The capture's file.
        path: String,
        /// Why.
        #[source]
        source: std::io::Error,
    },
}

/// `line` served on `address` by `ports`.
pub(crate) fn serve(ports: &mut dyn Ports, address: &str, line: Line) -> Result<(), HostError> {
    let name = line.name();
    ports
        .serve(address, name, attach(line))
        .map_err(|source| HostError::Serve {
            address: address.to_owned(),
            source,
        })
}

/// The far end of `host` as `config` puts it, the host's console
/// `ports`', a script, read from `files`, reaching beyond the port
/// through `wiring`: what
/// runs alongside the board for it, if anything does.
pub(crate) fn host_side(
    config: &RunConfig,
    ports: &mut dyn Ports,
    files: &dyn Files,
    host: &Line,
    wiring: ScriptWiring,
) -> Result<Option<Box<dyn Alongside>>, HostError> {
    match &config.host {
        HostPort::Script(path) => {
            let script = Script::open(path, files, host.attach())?.wired(wiring);
            trace::note(format_args!("host port on script {}", path.display()));
            Ok(Some(Box::new(script)))
        }
        HostPort::Tcp(address) => {
            serve(ports, address, host.clone())?;
            Ok(None)
        }
        // the page's console types, at the end of a slice (`mc683xx`)
        HostPort::Panel => {
            trace::note(format_args!("host port on the panel page's console"));
            Ok(None)
        }
        HostPort::Stdio => Ok(Some(ports.console(host.attach()))),
    }
}

/// The SCI's line, tapped with `--tap` and captured with
/// `--sci-capture` where `ports` says.
pub(crate) fn sci_line(config: &RunConfig, ports: &mut dyn Ports) -> Result<Line, HostError> {
    let sci = Line::new(crate::ports::SCI, config.tap);
    match &config.sci_capture {
        Some(path) => {
            let capture = ports.capture(path).map_err(|source| HostError::Capture {
                path: path.display().to_string(),
                source,
            })?;
            Ok(sci.capturing(capture))
        }
        None => Ok(sci),
    }
}

/// Ports for a test: nothing served, no console, captures dropped.
#[cfg(test)]
#[derive(Debug, Default)]
pub(crate) struct Unserved;

#[cfg(test)]
impl Ports for Unserved {
    fn serve(&mut self, _: &str, _: &'static str, _: Attach) -> std::io::Result<()> {
        Ok(())
    }

    fn console(&mut self, far: FarEnd) -> Box<dyn Alongside> {
        Box::new(move |_| {
            let _ = &far;
            crate::run::Flow::Continue
        })
    }

    fn capture(&mut self, _: &Path) -> std::io::Result<Box<dyn Write + Send>> {
        Ok(Box::new(std::io::sink()))
    }

    #[cfg(feature = "mc683xx")]
    fn serve_page(&mut self, _: &str, _: Shared) -> Result<(), ServeError> {
        Ok(())
    }
}
