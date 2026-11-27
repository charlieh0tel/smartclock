//! What a call refuses, before it becomes a JavaScript exception: an
//! `Error` with the message and its causes.

use std::error::Error as _;
use std::path::PathBuf;

use smartclock_emu::config::ConfigError;
use wasm_bindgen::JsError;
use wasm_bindgen::JsValue;

/// A call the emulator refuses.
#[derive(Debug, thiserror::Error)]
pub enum Error {
    /// Arguments the command line refuses, or its help asked for.
    #[error("{0}")]
    Arguments(#[from] clap::Error),
    /// A file the run names that was not given.
    #[error("{}: not among the files given", path.display())]
    File {
        /// Its path.
        path: PathBuf,
        /// Why.
        #[source]
        source: std::io::Error,
    },
    /// A run that cannot be.
    #[error(transparent)]
    Config(#[from] ConfigError),
    /// The run not started, or stopped short.
    #[error(transparent)]
    Run(#[from] smartclock_emu::mc683xx::Error),
    /// A run that served no panel page.
    #[error("the run served no panel page")]
    NoPage,
    /// Files that are neither a `Map` nor an object.
    #[error("files are neither a Map nor an object")]
    Files,
    /// A file's path, a `Map`'s key, that is not a string: what it is
    /// (`a number`).
    #[error("a file's path is not a string but {0}")]
    FilePath(String),
    /// A file's bytes that are not a `Uint8Array`.
    #[error("file {0:?} is not a Uint8Array")]
    FileBytes(String),
    /// A body that is not a `Uint8Array`: what it is.
    #[error("a body is not a Uint8Array but {0}")]
    Body(String),
    /// A clock reading that is no time.
    #[error("{0} ms is no clock reading: give a finite time, 0 or more")]
    Wall(f64),
    /// A UTC reading that is no time jiff takes.
    #[error("{0} ms since 1970 is no UTC reading")]
    Utc(f64),
}

impl Error {
    /// The message, each cause after it.
    #[must_use]
    pub fn chain(&self) -> String {
        std::iter::successors(self.source(), |&cause| cause.source())
            .fold(self.to_string(), |message, cause| {
                format!("{message}: {cause}")
            })
    }
}

impl From<Error> for JsValue {
    fn from(error: Error) -> Self {
        JsError::new(&error.chain()).into()
    }
}
