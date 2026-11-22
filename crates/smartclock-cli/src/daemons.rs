//! `daemons`: the receiver daemons on this host, and what each is
//! attached to, so a command that needs `--daemon` can be given one.
//!
//! Asks each daemon only what it already holds -- `info` and `latest`
//! are answered from its own state -- so nothing is sent to any
//! receiver.

use std::path::Path;
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

/// How long each daemon has to say what it is attached to.
///
/// Both answers come from the daemon's memory, so a healthy one takes
/// milliseconds; this only bounds one that accepts and never answers.
/// The daemons are asked together, so the whole listing takes no
/// longer than this however many there are.
const BUDGET: Duration = Duration::from_secs(3);

/// What became of asking one instance.
#[derive(Debug)]
enum State {
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

/// List the daemons under `run_dir`, or where there are no Unix
/// sockets the one a client asks by default.
pub(crate) fn run(run_dir: Option<&Path>) -> Result<()> {
    let daemons = Daemons::from_names(&[], run_dir);
    let found = match &daemons {
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
    if found.is_empty() {
        match &daemons {
            Daemons::Dir(dir) => println!("no daemons under {}", dir.display()),
            Daemons::Listed(_) => println!("no daemons"),
        }
        return Ok(());
    }
    let states: Vec<State> = thread::scope(|scope| {
        let asked: Vec<_> = found.iter().map(|f| scope.spawn(|| state(f))).collect();
        asked
            .into_iter()
            .map(|asked| {
                asked
                    .join()
                    .unwrap_or_else(|_| State::Unanswered("asking it failed".to_owned()))
            })
            .collect()
    });
    for (found, state) in found.iter().zip(&states) {
        println!("{}", line(&found.instance, state));
    }
    Ok(())
}

/// What is known of one instance: from its directory, and if a socket
/// is there, from the daemon behind it.
fn state(found: &Found) -> State {
    match found.socket {
        Presence::Absent => State::Stopped("no socket".to_owned()),
        Presence::Unreadable(kind) => State::Unreadable(kind),
        Presence::Present => ask(&found.instance.endpoint),
    }
}

/// Ask the daemon at `endpoint` what it is attached to, within
/// [`BUDGET`].
fn ask(endpoint: &Path) -> State {
    let mut daemon = match Daemon::connect_within(endpoint, BUDGET) {
        Ok(daemon) => daemon,
        // A socket left behind by a daemon that is gone refuses.
        Err(Error::Io(e)) if e.kind() == std::io::ErrorKind::ConnectionRefused => {
            return State::Stopped(format!("stale socket: {}", std::io::Error::from(e.kind())));
        }
        // The directory let us in but the socket does not.
        Err(Error::Io(e)) if e.kind() == std::io::ErrorKind::PermissionDenied => {
            return State::Unreadable(e.kind());
        }
        Err(e) => return State::Unanswered(reason(e)),
    };
    let info = match daemon.info() {
        Ok(info) => info,
        Err(e) => return State::Unanswered(reason(e)),
    };
    let Some(identity) = client::identity(&info) else {
        return State::Silent(None, "no receiver has answered it".to_owned());
    };
    match daemon.latest() {
        Ok(reading) => match reading.freshness {
            Freshness::Live => State::Answering(identity),
            Freshness::Stale => {
                State::Silent(Some(identity), why(&reading, "its last poll failed"))
            }
            Freshness::Disconnected => State::Silent(Some(identity), why(&reading, "link down")),
        },
        Err(e) => State::Silent(Some(identity), reason(e)),
    }
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

/// A receiver as the listing names it.
fn receiver(id: &Identity) -> String {
    format!("{} serial {} firmware {}", id.model, id.serial, id.firmware)
}

/// One instance's line: its name, the endpoint to give `--daemon`, and
/// what it is attached to or why that is not known.
fn line(instance: &Instance, state: &State) -> String {
    let status = match state {
        State::Answering(id) => format!("running, {}", receiver(id)),
        State::Silent(Some(id), why) => {
            format!(
                "running, receiver not answering (last {}; {why})",
                receiver(id)
            )
        }
        State::Silent(None, why) => format!("running, receiver not answering ({why})"),
        State::Stopped(why) => format!("not running ({why})"),
        State::Unanswered(why) => format!("not answering ({why})"),
        State::Unreadable(kind) => format!(
            "cannot tell ({}; are you in its group?)",
            std::io::Error::from(*kind)
        ),
    };
    format!(
        "{} {}: {status}",
        instance.name,
        instance.endpoint.display()
    )
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
