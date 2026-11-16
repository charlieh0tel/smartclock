//! smartclock-gps-feed: runs the `smartclock-gps` engine on a host,
//! paced to real time, and speaks Oncore binary to a `SmartClock`'s
//! engine port over TCP or a serial port.  Each second it takes a
//! record from its provider -- gpsd, a UBX recording or a made-up sky
//! -- and steps the engine; between seconds it answers what the
//! instrument sends.  The 1 PPS is not carried: the instrument's comes
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
use std::time::Duration;

use anyhow::Context as _;
use anyhow::Result;
use clap::Parser as _;
use smartclock_gps::source::Tick;
use smartclock_gps::time::UnixSeconds;
use smartclock_gps::units::Seconds;

use crate::cli::Cli;
use crate::cli::ProviderSpec;
use crate::engine::Engine;
use crate::feed::Clock as _;
use crate::feed::Feed;
use crate::feed::LIVE_PHASE;
use crate::feed::SystemClock;
use crate::feed::WireLog;
use crate::feed::run;
use crate::link::Backoff;
use crate::provider::Built;
use crate::provider::Kind;
use crate::provider::Shared;
use crate::provider::build;
use crate::provider::spawn_gpsd;

/// Reports what happened, on standard error.
pub(crate) fn event(message: impl Display) {
    eprintln!("{message}");
}

fn main() -> Result<()> {
    let config = Cli::parse().configure()?;
    let engine = Engine::new(&config.spec).context("making the engine")?;
    let now = SystemClock.now();
    let now = UnixSeconds::from_epoch(Seconds(i64::try_from(now.as_secs())?));
    let Built { nest, kind } = build(&config.provider, config.fake_start, now)?;
    let shared = Arc::new(Mutex::new(Shared {
        nest,
        tick: Tick(0),
    }));
    if let ProviderSpec::Gpsd { host, port } = &config.provider {
        spawn_gpsd(host.clone(), *port, Arc::clone(&shared), Backoff::default());
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
    let phase = if kind == Kind::Live {
        LIVE_PHASE
    } else {
        Duration::ZERO
    };
    let mut feed = Feed::new(engine, shared, kind);
    run(
        &mut feed,
        transport.as_mut(),
        &SystemClock,
        &mut log,
        phase,
        None,
    );
    Ok(())
}
