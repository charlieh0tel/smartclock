//! `daemons`: the receiver daemons on this host, and what each is
//! attached to, so a command that needs `--daemon` can be given one.
//!
//! Asks each daemon only what it already holds -- `info` and `latest`
//! are answered from its own state -- so nothing is sent to any
//! receiver.

use std::path::Path;
use std::path::PathBuf;
use std::thread;
use std::time::Duration;

use anyhow::Context as _;
use anyhow::Result;
use smartclock::client;
use smartclock::client::Daemon;
use smartclock::client::Daemons;
use smartclock::client::Found;
use smartclock::client::Instance;
use smartclock::client::Presence;
use smartclock::error::Error;
use smartclock::parse::Identity;
use smartclock::snapshot::Freshness;
use smartclock::wire::Reading;

use crate::select::Serial;
use crate::select::describe;

/// How long each daemon has to say what it is attached to.
///
/// Both answers come from the daemon's memory, so a healthy one takes
/// milliseconds; this only bounds one that accepts and never answers.
/// The daemons are asked together, so the whole listing takes no
/// longer than this however many there are.
const BUDGET: Duration = Duration::from_secs(3);

/// What became of asking one instance.
#[derive(Debug)]
pub(crate) enum State {
    /// The daemon answered and its receiver is answering it.
    Answering(Identity),
    /// The daemon answered, but its receiver is not answering it: the
    /// receiver it last had, if any, and why.
    Silent(Option<Identity>, String),
    /// No daemon is there: no socket, or one nobody listens on.
    Stopped(String),
    /// Something listens but did not answer in time, or answered
    /// something else.
    Unanswered(String),
    /// The instance directory could not be looked into, or its socket
    /// opened.
    Unreadable(std::io::ErrorKind),
}

/// The daemons a listing or a lookup found: where they were looked
/// for, and each instance with what became of asking it.
pub(crate) struct Survey {
    pub(crate) daemons: Daemons,
    pub(crate) found: Vec<Asked>,
}

/// One instance and what became of asking it.
#[derive(Debug)]
pub(crate) struct Asked {
    pub(crate) found: Found,
    pub(crate) state: State,
    /// The port, or `tcp://` address, its daemon says it holds: `None`
    /// from one that did not answer or is too old to say.
    pub(crate) device: Option<String>,
}

/// Ask every daemon under `run_dir`, or where there are no Unix sockets
/// the one a client asks by default, what it is attached to.
pub(crate) fn survey(run_dir: Option<&Path>) -> Result<Survey> {
    let daemons = Daemons::from_names(&[], run_dir);
    let found: Vec<Found> = match &daemons {
        Daemons::Listed(listed) => listed
            .iter()
            .map(|instance| Found {
                instance: instance.clone(),
                socket: Presence::Present,
            })
            .collect(),
        Daemons::Dir(dir) => match client::scan(dir) {
            Ok(found) => found,
            Err(Error::Io(e)) if e.kind() == std::io::ErrorKind::NotFound => Vec::new(),
            Err(e) => Err(e).with_context(|| format!("reading {}", dir.display()))?,
        },
    };
    let states: Vec<(State, Option<String>)> = thread::scope(|scope| {
        let asked: Vec<_> = found.iter().map(|f| scope.spawn(|| state(f))).collect();
        asked
            .into_iter()
            .map(|asked| {
                asked
                    .join()
                    .unwrap_or_else(|_| (State::Unanswered("asking it failed".to_owned()), None))
            })
            .collect()
    });
    Ok(Survey {
        daemons,
        found: found
            .into_iter()
            .zip(states)
            .map(|(found, (state, device))| Asked {
                found,
                state,
                device,
            })
            .collect(),
    })
}

/// List the daemons under `run_dir`, or where there are no Unix
/// sockets the one a client asks by default.
pub(crate) fn run(run_dir: Option<&Path>) -> Result<()> {
    let Survey { daemons, found } = survey(run_dir)?;
    if found.is_empty() {
        match &daemons {
            Daemons::Dir(dir) => println!("no daemons under {}", dir.display()),
            Daemons::Listed(_) => println!("no daemons"),
        }
        return Ok(());
    }
    for asked in &found {
        println!("{}", line(&asked.found.instance, &asked.state));
    }
    Ok(())
}

/// The socket of the one daemon under `run_dir` attached to `serial`'s
/// receiver, whether or not the receiver is answering it.  None or
/// several is an error listing every daemon found.
pub(crate) fn serving(run_dir: Option<&Path>, serial: &Serial) -> Result<PathBuf> {
    let Survey { daemons, found } = survey(run_dir)?;
    let attached = |state: &State| match state {
        State::Answering(id) | State::Silent(Some(id), _) => serial.matches(id),
        _ => false,
    };
    let matching: Vec<&Found> = found
        .iter()
        .filter(|asked| attached(&asked.state))
        .map(|asked| &asked.found)
        .collect();
    if let [one] = matching.as_slice() {
        return Ok(one.instance.endpoint.clone());
    }
    let place = match &daemons {
        Daemons::Dir(dir) => format!("under {}", dir.display()),
        Daemons::Listed(_) => "here".to_owned(),
    };
    let listing: String = found
        .iter()
        .map(|asked| format!("\n  {}", line(&asked.found.instance, &asked.state)))
        .collect();
    match matching.len() {
        0 if found.is_empty() => {
            anyhow::bail!("no daemon {place} is attached to serial {serial}: none were found")
        }
        0 => anyhow::bail!(
            "no daemon {place} is attached to serial {serial}; found:{listing}\n\
             Give --daemon for one elsewhere, or --device with its daemon stopped"
        ),
        _ => anyhow::bail!(
            "more than one daemon {place} is attached to serial {serial}; found:{listing}\n\
             Give --daemon to say which"
        ),
    }
}

/// What is known of one instance, and the port its daemon says it
/// holds: from its directory, and if a socket is there, from the daemon
/// behind it.
fn state(found: &Found) -> (State, Option<String>) {
    match found.socket {
        Presence::Absent => (State::Stopped("no socket".to_owned()), None),
        Presence::Unreadable(kind) => (State::Unreadable(kind), None),
        Presence::Present => ask(&found.instance.endpoint),
    }
}

/// Ask the daemon at `endpoint` what it is attached to, and on which
/// port, within [`BUDGET`].
fn ask(endpoint: &Path) -> (State, Option<String>) {
    let mut daemon = match Daemon::connect_within(endpoint, BUDGET) {
        Ok(daemon) => daemon,
        // A socket left behind by a daemon that is gone refuses.
        Err(Error::Io(e)) if e.kind() == std::io::ErrorKind::ConnectionRefused => {
            return (
                State::Stopped(format!("stale socket: {}", std::io::Error::from(e.kind()))),
                None,
            );
        }
        // The directory let us in but the socket does not.
        Err(Error::Io(e)) if e.kind() == std::io::ErrorKind::PermissionDenied => {
            return (State::Unreadable(e.kind()), None);
        }
        Err(e) => return (State::Unanswered(reason(e)), None),
    };
    let info = match daemon.info() {
        Ok(info) => info,
        Err(e) => return (State::Unanswered(reason(e)), None),
    };
    let device = client::device(&info);
    let Some(identity) = client::identity(&info) else {
        return (
            State::Silent(None, "no receiver has answered it".to_owned()),
            device,
        );
    };
    let state = match daemon.latest() {
        Ok(reading) => match reading.freshness {
            Freshness::Live => State::Answering(identity),
            Freshness::Stale => {
                State::Silent(Some(identity), why(&reading, "its last poll failed"))
            }
            Freshness::Disconnected => State::Silent(Some(identity), why(&reading, "link down")),
        },
        Err(e) => State::Silent(Some(identity), reason(e)),
    };
    (state, device)
}

/// What went wrong asking a daemon.  Its own refusals and the client's
/// deadline come as [`Error::Daemon`], whose display would credit the
/// daemon with saying what the client concluded.
fn reason(e: Error) -> String {
    match e {
        Error::Daemon(why) => why,
        other => other.to_string(),
    }
}

/// `what` happened, and the error its most recent tier recorded, if
/// one did.
fn why(reading: &Reading, what: &str) -> String {
    let polled = &reading.polled;
    match [&polled.fast, &polled.medium, &polled.slow]
        .into_iter()
        .find_map(|tier| tier.error.as_deref())
    {
        Some(error) => format!("{what}: {error}"),
        None => what.to_owned(),
    }
}

/// One instance's line: its name, the endpoint to give `--daemon`, and
/// what it is attached to or why that is not known.
fn line(instance: &Instance, state: &State) -> String {
    format!(
        "{} {}: {}",
        instance.name,
        instance.endpoint.display(),
        status(state)
    )
}

/// What an instance is attached to, or why that is not known.
pub(crate) fn status(state: &State) -> String {
    match state {
        State::Answering(id) => format!("running, {}", describe(id)),
        State::Silent(Some(id), why) => {
            format!(
                "running, receiver not answering (last {}; {why})",
                describe(id)
            )
        }
        State::Silent(None, why) => format!("running, receiver not answering ({why})"),
        State::Stopped(why) => format!("not running ({why})"),
        State::Unanswered(why) => format!("not answering ({why})"),
        State::Unreadable(kind) => format!(
            "cannot tell ({}; are you in its group?)",
            std::io::Error::from(*kind)
        ),
    }
}

#[cfg(test)]
mod tests {
    use super::State;
    use super::line;
    use smartclock::client::Instance;
    use smartclock::parse;

    #[test]
    fn a_line_names_the_instance_its_endpoint_and_what_it_is_attached_to() {
        let instance = Instance {
            name: "ttyUSB0".to_owned(),
            endpoint: "/run/smartclockd/ttyUSB0/socket".into(),
        };
        let id = parse::identity("HEWLETT-PACKARD,58503A,0000A00000,3704-C").expect("identity");
        assert_eq!(
            line(&instance, &State::Answering(id)),
            "ttyUSB0 /run/smartclockd/ttyUSB0/socket: running, 58503A serial 0000A00000 firmware 3704-C"
        );
        assert_eq!(
            line(
                &instance,
                &State::Unreadable(std::io::ErrorKind::PermissionDenied)
            ),
            "ttyUSB0 /run/smartclockd/ttyUSB0/socket: cannot tell (permission denied; are you in its group?)"
        );
    }
}
