//! The host's side of the run on this command line (`Ports`): the
//! board's serial ports served over TCP, the host port on standard
//! input and output, the SCI's capture to a file, and the panel page
//! on `smartclock::http`.

use std::fs::File;
use std::io::Write;
use std::net::TcpListener;
use std::path::Path;
use std::thread;
use std::time::Instant;

#[cfg(feature = "mc683xx")]
use smartclock::http::ServeError;
use smartclock_emu::host::Attach;
use smartclock_emu::host::Ports;
use smartclock_emu::line::FarEnd;
#[cfg(feature = "mc683xx")]
use smartclock_emu::panel::routes::Shared;
use smartclock_emu::run::Alongside;
use smartclock_emu::trace;
use smartclock_sim::net::serve;

use crate::stdio::Stdio;

/// The run's ports as this command line serves them.
#[derive(Debug)]
pub(crate) struct Native {
    /// Whether standard input is a terminal, made raw.
    terminal: bool,
}

impl Native {
    /// The ports, standard input a terminal when `terminal`, which the
    /// caller has made raw.
    pub(crate) fn new(terminal: bool) -> Self {
        Self { terminal }
    }
}

impl Ports for Native {
    /// Says where on standard error.
    fn serve(&mut self, address: &str, name: &'static str, attach: Attach) -> std::io::Result<()> {
        let listener = TcpListener::bind(address)?;
        let bound = listener.local_addr()?;
        trace::note(format_args!("{name} port on tcp://{bound}"));
        thread::spawn(move || {
            for stream in listener.incoming() {
                match stream {
                    Ok(stream) => {
                        if let Err(error) = serve(stream, attach()) {
                            trace::note(format_args!("{name} client ended: {error}"));
                        }
                    }
                    Err(error) => trace::note(format_args!("{name} refused a client: {error}")),
                }
            }
        });
        Ok(())
    }

    fn console(&mut self, far: FarEnd) -> Box<dyn Alongside> {
        trace::note(format_args!(
            "host port on standard input and output{}",
            if self.terminal { ", a terminal" } else { "" }
        ));
        Box::new(Stdio::new(far, self.terminal))
    }

    /// Raw and unbuffered.
    fn capture(&mut self, path: &Path) -> std::io::Result<Box<dyn Write + Send>> {
        Ok(Box::new(File::create(path)?))
    }

    /// Says where on standard error.
    #[cfg(feature = "mc683xx")]
    fn serve_page(&mut self, address: &str, page: Shared) -> Result<(), ServeError> {
        let listener = smartclock::http::bind(address)?;
        match listener.local_addr() {
            Ok(bound) => trace::note(format_args!("panel on http://{bound}/")),
            Err(error) => trace::note(format_args!("panel on an address unknown: {error}")),
        }
        let origin = Instant::now();
        thread::spawn(move || {
            if let Err(error) = smartclock::http::serve_requests_on(listener, move |request| {
                page.respond(request, origin.elapsed())
            }) {
                trace::note(format_args!("panel stopped: {error}"));
            }
        });
        Ok(())
    }
}
