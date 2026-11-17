//! smartclock-gps-feed: runs the `smartclock-gps` engine on a host,
//! paced to real time, and speaks Oncore binary, or a Furuno GT-74's
//! NMEA, to a `SmartClock`'s engine port over TCP or a serial port.
//! Each second it takes a record from its provider -- gpsd, a UBX
//! recording or a made-up sky -- and steps the engine; between seconds
//! it answers what the instrument sends.  The 1 PPS is not carried: the instrument's comes
//! from elsewhere.  `PLAN.md` has the design.

mod cli;
mod engine;
mod feed;
mod link;
mod provider;

use std::fmt::Display;
use std::fs::File;
use std::io::BufWriter;
use std::sync::Arc;
use std::sync::Mutex;

use anyhow::Context as _;
use anyhow::Result;
use clap::Parser as _;

use crate::cli::Cli;
use crate::cli::ProviderSpec;
use crate::engine::Engine;
use crate::feed::Feed;
use crate::feed::SystemClock;
use crate::feed::WireLog;
use crate::feed::first_second;
use crate::feed::phase;
use crate::feed::run;
use crate::feed::utc_second;
use crate::link::Backoff;
use crate::provider::Built;
use crate::provider::GPSD_SILENCE;
use crate::provider::Shared;
use crate::provider::build;
use crate::provider::kind;
use crate::provider::spawn_gpsd;

/// Reports what happened, on standard error.
pub(crate) fn event(message: impl Display) {
    eprintln!("{message}");
}

fn main() -> Result<()> {
    let config = Cli::parse().configure()?;
    let engine = Engine::new(&config.spec).context("making the engine")?;
    let clock = SystemClock::new();
    let first = first_second(&clock, phase(kind(&config.provider), &engine));
    let Built { nest, kind } = build(&config.provider, config.fake_start, utc_second(first))?;
    let shared = Arc::new(Mutex::new(Shared::new(nest)));
    if let ProviderSpec::Gpsd(gpsd) = &config.provider {
        spawn_gpsd(
            gpsd.clone(),
            Arc::clone(&shared),
            Backoff::default(),
            GPSD_SILENCE,
        );
    }
    let mut log = config
        .log
        .as_ref()
        .map(|path| {
            File::create(path)
                .with_context(|| format!("creating {}", path.display()))
                .map(|file| WireLog::new(BufWriter::new(file)))
        })
        .transpose()?;
    let mut transport = link::open(&config.transport)?;
    event(format_args!(
        "{} engine, from {}",
        config.engine.name(),
        config.provider
    ));
    let mut feed = Feed::new(engine, shared, kind);
    run(&mut feed, transport.as_mut(), &clock, &mut log, first, None);
    Ok(())
}
