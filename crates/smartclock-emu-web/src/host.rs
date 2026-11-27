//! What a browser gives a run (`host::Ports`): no TCP, so a line the
//! board file would serve is not; no console of its own, the host port
//! being the panel page's; no file for the SCI's capture; and the panel
//! page answered in the page, its server's half kept for the host to
//! answer the page's requests with.

use std::io;
use std::io::Write;
use std::path::Path;

use smartclock::http::ServeError;
use smartclock_emu::host::Attach;
use smartclock_emu::host::Ports;
use smartclock_emu::line::FarEnd;
use smartclock_emu::panel::routes::Shared;
use smartclock_emu::run::Alongside;
use smartclock_emu::run::Flow;
use smartclock_emu::trace;

/// The browser's ports.
#[derive(Debug, Default)]
pub(crate) struct Browser {
    /// The panel page's answers, once the run has served it.
    pub(crate) page: Option<Shared>,
}

impl Ports for Browser {
    /// Serves nothing, and says so: a browser has no sockets, so nobody
    /// could connect.
    fn serve(&mut self, address: &str, name: &'static str, _: Attach) -> io::Result<()> {
        trace::note(format_args!(
            "{name} port not served on {address}: a browser has no TCP"
        ));
        Ok(())
    }

    /// Nothing alongside: the host port is the page's console, never
    /// standard input and output, which a browser has none of.
    fn console(&mut self, far: FarEnd) -> Box<dyn Alongside> {
        Box::new(move |_| {
            let _ = &far;
            Flow::Continue
        })
    }

    /// Refused: a browser has no files to write.
    fn capture(&mut self, path: &Path) -> io::Result<Box<dyn Write + Send>> {
        Err(io::Error::new(
            io::ErrorKind::Unsupported,
            format!("{}: a browser has no files", path.display()),
        ))
    }

    /// Kept, for the host to answer the page's requests with.
    fn serve_page(&mut self, _: &str, page: Shared) -> Result<(), ServeError> {
        self.page = Some(page);
        Ok(())
    }
}
