//! `devices`: the serial ports on this host, and the receiver on each
//! where that is known.
//!
//! A port a daemon holds is named by the daemon, from what it holds, so
//! nothing reaches its receiver.  Any other is listed as the system
//! lists it, which opens none of them, unless `--probe` asks it
//! `*IDN?` by the attach a daemon makes: the same settings in the same
//! order, and the same reading off the error queue of what the wrong
//! ones left there.

use std::path::Path;
#[cfg(target_os = "linux")]
use std::path::PathBuf;
use std::thread;

use anyhow::Context as _;
use anyhow::Result;
use smartclock::attach::attach_with;
use smartclock::client::Daemons;
use smartclock::parse::Identity;
use smartclock::session::Config;
use smartclock::transport;
use smartclock::transport::Transport;
use smartclock::transport::serial::Settings;
use smartclock::types::BaudRate;
use smartclock::types::Framing;

use crate::daemons;
use crate::daemons::Asked;
use crate::daemons::State;
use crate::daemons::Survey;
use crate::select::describe;

/// Where the packaged unit's instances find their port: instance
/// `ttyUSB0` opens `/dev/ttyUSB0` (`packaging/systemd/smartclockd@.service`).
const UNIT_DEVICE_DIR: &str = "/dev";

/// What a daemon's row names when its port cannot be told.
const UNKNOWN_PORT: &str = "unknown";

/// Separates a line's fields: the port and what is on it, then the
/// adapter, then each other name for the port.
const SEPARATOR: &str = " | ";

/// Where hosts keep UUCP lock files, `LCK..<name>`, which programs such
/// as minicom take instead of an exclusive open.  `/var/lock` is often
/// a link to `/run/lock`; the first holding a port's file is the one
/// read.
const LOCK_DIRS: [&str; 2] = ["/var/lock", "/run/lock"];

/// Where each running process has an entry named by its pid, to tell
/// a stale lock.  Without it a lock is taken as live.
const PROC: &str = "/proc";

/// The prefix of a UUCP lock file's name, before the port's.
const LOCK_PREFIX: &str = "LCK..";

/// How to ask a port no daemon holds what is on it.
#[derive(Debug)]
pub(crate) struct Probe {
    /// The rate tried first.
    pub(crate) baud: BaudRate,
    /// The framing tried first.
    pub(crate) framing: Framing,
    /// How long to wait for each prompt.
    pub(crate) config: Config,
}

/// One serial port the system lists.
#[derive(Debug, Clone, PartialEq, Eq)]
struct Port {
    /// Its node, such as `/dev/ttyUSB0` or `COM3`.
    path: String,
    /// Other names that lead to it, such as under `/dev/serial/by-id`,
    /// which survive the adapters enumerating in another order.
    aliases: Vec<String>,
    /// The USB adapter it is on, if it is on one.
    usb: Option<Usb>,
}

/// A USB serial adapter, as it describes itself.
#[derive(Debug, Clone, PartialEq, Eq)]
struct Usb {
    vid: u16,
    pid: u16,
    manufacturer: Option<String>,
    product: Option<String>,
}

/// A daemon that holds, or may hold, a port.
#[derive(Debug)]
struct Holder {
    /// Its instance name.
    name: String,
    /// What it is attached to, or why that is not known, as `daemons`
    /// says it.
    status: String,
    /// The port, or `tcp://` address, it holds: as it says, or for one
    /// that does not, as the packaged unit names it.  `None` when
    /// neither tells.
    port: Option<String>,
    /// Whether the daemon said `port` itself, rather than it being
    /// guessed from the instance name.
    reported: bool,
}

/// What is known of what is on a port.
#[derive(Debug)]
enum Seen {
    /// A daemon holds it: the daemon's name and what it says.
    Held { daemon: String, status: String },
    /// No daemon holds it, and it was not asked.
    Free,
    /// Asked, and this answered, at these settings.
    Identified {
        identity: Identity,
        baud: BaudRate,
        framing: Framing,
    },
    /// Asked, and nothing answered: why.
    Unidentified(String),
    /// Not asked, for another program's lock on it.
    Locked(Lock),
}

/// A UUCP lock file on a port.
#[derive(Debug, PartialEq, Eq)]
struct Lock {
    /// The file.
    file: String,
    /// The pid it names, if it could be read.
    pid: Option<u32>,
    /// Whether that pid is known not to be running.  A stale lock is
    /// skipped all the same, and left where it is.
    stale: bool,
}

/// One line of the listing.
#[derive(Debug)]
struct Row {
    /// The port, the address a daemon was given, or "unknown".
    port: String,
    aliases: Vec<String>,
    usb: Option<Usb>,
    seen: Seen,
}

/// List the ports on this host and the daemons under `run_dir`, asking
/// the free ports what they are with `probe`.
pub(crate) fn run(run_dir: Option<&Path>, probe: Option<&Probe>) -> Result<()> {
    let Survey { daemons, found } = daemons::survey(run_dir)?;
    let holders: Vec<Holder> = found
        .into_iter()
        .filter_map(|asked| holder(&daemons, asked))
        .collect();
    let rows = rows(ports().context("listing the serial ports")?, &holders);
    let Some(probe) = probe else {
        print(&rows);
        return Ok(());
    };
    for note in unplaced(&holders) {
        eprintln!("{note}");
    }
    print(&probe_free(rows, probe));
    Ok(())
}

/// What a probe skips for each daemon that did not say which port it
/// holds: its unit's port, if there is one, and nothing else.
fn unplaced(holders: &[Holder]) -> Vec<String> {
    holders
        .iter()
        .filter(|holder| !holder.reported)
        .map(|holder| match &holder.port {
            Some(port) => format!(
                "daemon {} did not say which port it holds; not probing {port}, its unit's port",
                holder.name
            ),
            None => format!(
                "daemon {} did not say which port it holds, and no port is named after it; \
                 nothing was skipped for it",
                holder.name
            ),
        })
        .collect()
}

fn print(rows: &[Row]) {
    if rows.is_empty() {
        println!("no serial ports or daemons found");
    }
    for row in rows {
        println!("{}", line(row));
    }
}

/// The daemon behind `asked`, unless there is none: a socket nobody
/// listens on is a daemon gone.  One waiting for its first receiver is
/// kept, since until one answers it is opening its port again and
/// again.
fn holder(daemons: &Daemons, asked: Asked) -> Option<Holder> {
    if matches!(asked.state, State::Stopped(_)) {
        return None;
    }
    let reported = asked.device.is_some();
    let port = asked.device.or_else(|| match daemons {
        Daemons::Dir(_) => {
            unit_device(&asked.found.instance.name).filter(|path| Path::new(path).exists())
        }
        Daemons::Listed(_) => None,
    });
    Some(Holder {
        name: asked.found.instance.name,
        status: daemons::status(&asked.state),
        port,
        reported,
    })
}

/// The port the packaged unit's instance `name` opens: its name
/// unescaped as systemd's `%I` is, `-` for `/` and `\xNN` for byte NN,
/// under [`UNIT_DEVICE_DIR`].
fn unit_device(name: &str) -> Option<String> {
    let bytes = name.as_bytes();
    let mut unescaped = Vec::with_capacity(bytes.len());
    let mut at = 0;
    while at < bytes.len() {
        let escaped = bytes
            .get(at..at + 4)
            .filter(|four| four.starts_with(b"\\x"))
            .and_then(|four| std::str::from_utf8(&four[2..]).ok())
            .and_then(|hex| u8::from_str_radix(hex, 16).ok());
        match (escaped, bytes[at]) {
            (Some(byte), _) => {
                unescaped.push(byte);
                at += 4;
            }
            (None, b'-') => {
                unescaped.push(b'/');
                at += 1;
            }
            (None, byte) => {
                unescaped.push(byte);
                at += 1;
            }
        }
    }
    let path = String::from_utf8(unescaped).ok()?;
    Some(format!("{UNIT_DEVICE_DIR}/{path}"))
}

/// A port as compared with another: through any link, such as a by-id
/// name, and with one spelling for each port a system spells two ways.
fn key(path: &str) -> String {
    let resolved = std::fs::canonicalize(path)
        .ok()
        .and_then(|resolved| resolved.to_str().map(str::to_owned))
        .unwrap_or_else(|| path.to_owned());
    spelling(&resolved)
}

/// Windows' `\\.\COM3` is `COM3`, in any case; macOS's `/dev/tty.X` is
/// `/dev/cu.X`, the same port under a node of its own, and so under a
/// lock of its own: probing one while a daemon held the other would
/// reach the daemon's receiver.
fn spelling(path: &str) -> String {
    let path = path.strip_prefix(r"\\.\").unwrap_or(path);
    if path
        .get(..3)
        .is_some_and(|prefix| prefix.eq_ignore_ascii_case("COM"))
    {
        return path.to_ascii_uppercase();
    }
    match path.strip_prefix("/dev/tty.") {
        Some(rest) => format!("/dev/cu.{rest}"),
        None => path.to_owned(),
    }
}

/// A row for each port, one for each daemon on it if any is; then one
/// for each daemon on no port listed, such as one on `tcp://`.
fn rows(ports: Vec<Port>, holders: &[Holder]) -> Vec<Row> {
    let keys: Vec<Option<String>> = holders
        .iter()
        .map(|holder| holder.port.as_deref().map(key))
        .collect();
    let mut ports: Vec<(String, Port)> = ports
        .into_iter()
        .map(|port| (key(&port.path), port))
        .collect();
    // The spelling macOS's other node shares sorts after `/dev/cu.`.
    ports.sort_by(|(a, x), (b, y)| a.cmp(b).then_with(|| x.path.cmp(&y.path)));
    ports.dedup_by(|(a, _), (b, _)| a == b);
    let listed = ports.iter().flat_map(|(port_key, port)| {
        let on: Vec<Seen> = holders
            .iter()
            .zip(&keys)
            .filter(|(_, key)| key.as_deref() == Some(port_key.as_str()))
            .map(|(holder, _)| holder.seen())
            .collect();
        let seen = if on.is_empty() { vec![Seen::Free] } else { on };
        seen.into_iter().map(|seen| Row {
            port: port.path.clone(),
            aliases: port.aliases.clone(),
            usb: port.usb.clone(),
            seen,
        })
    });
    let elsewhere = holders
        .iter()
        .zip(&keys)
        .filter(|(_, key)| {
            !key.as_deref()
                .is_some_and(|key| ports.iter().any(|(port_key, _)| port_key == key))
        })
        .map(|(holder, _)| Row {
            port: holder
                .port
                .clone()
                .unwrap_or_else(|| UNKNOWN_PORT.to_owned()),
            aliases: Vec::new(),
            usb: None,
            seen: holder.seen(),
        });
    listed.chain(elsewhere).collect()
}

impl Holder {
    fn seen(&self) -> Seen {
        Seen::Held {
            daemon: self.name.clone(),
            status: self.status.clone(),
        }
    }
}

/// Ask every free port in `rows` what it is, together.
fn probe_free(rows: Vec<Row>, probe: &Probe) -> Vec<Row> {
    let answers: Vec<Option<Seen>> = thread::scope(|scope| {
        let asking: Vec<_> = rows
            .iter()
            .map(|row| {
                scope.spawn(|| {
                    matches!(row.seen, Seen::Free).then(|| {
                        let dirs = LOCK_DIRS.map(Path::new);
                        match lock(&row.port, &dirs, Path::new(PROC)) {
                            Some(lock) => Seen::Locked(lock),
                            None => identify(&row.port, probe, transport::open),
                        }
                    })
                })
            })
            .collect();
        asking
            .into_iter()
            .map(|asking| {
                asking
                    .join()
                    .unwrap_or_else(|_| Some(Seen::Unidentified("probing it failed".to_owned())))
            })
            .collect()
    });
    rows.into_iter()
        .zip(answers)
        .map(|(row, answer)| match answer {
            Some(seen) => Row { seen, ..row },
            None => row,
        })
        .collect()
}

/// The UUCP lock file on `port` in the first of `dirs` holding one, and
/// whether the process it names is gone, by `proc`.
fn lock(port: &str, dirs: &[&Path], proc: &Path) -> Option<Lock> {
    let name = Path::new(port).file_name()?.to_str()?;
    let file = dirs
        .iter()
        .map(|dir| dir.join(format!("{LOCK_PREFIX}{name}")))
        .find(|file| file.is_file())?;
    let pid = std::fs::read(&file).ok().and_then(|bytes| lock_pid(&bytes));
    let stale = pid.is_some_and(|pid| proc.is_dir() && !proc.join(pid.to_string()).exists());
    Some(Lock {
        file: file.display().to_string(),
        pid,
        stale,
    })
}

/// The pid in a lock file: ASCII digits, as the HDB format writes it,
/// or a four-byte binary one, as the older format does.
fn lock_pid(bytes: &[u8]) -> Option<u32> {
    std::str::from_utf8(bytes)
        .ok()
        .and_then(|text| text.trim().parse().ok())
        .or_else(|| <[u8; 4]>::try_from(bytes).ok().map(u32::from_ne_bytes))
        .filter(|pid| *pid > 0)
}

/// What answers `*IDN?` on `path`, opening it with `open`, by the
/// attach a daemon makes.
fn identify<T, F>(path: &str, probe: &Probe, open: F) -> Seen
where
    T: Transport,
    F: FnMut(&Settings) -> smartclock::error::Result<T>,
{
    let settings = Settings {
        path: path.to_owned(),
        baud: probe.baud,
        framing: probe.framing,
        ..Settings::default()
    };
    match attach_with(&settings, &probe.config, open) {
        Ok(attached) => {
            if let Some(report) = attached.probe_report(&settings) {
                eprintln!("{report}");
            }
            Seen::Identified {
                identity: attached.device.identity().clone(),
                baud: attached.baud,
                framing: attached.framing,
            }
        }
        Err(e) => Seen::Unidentified(e.to_string()),
    }
}

/// One row's line: the port and what is on it, then the adapter and the
/// port's other names, each after [`SEPARATOR`].
fn line(row: &Row) -> String {
    let seen = match &row.seen {
        Seen::Held { daemon, status } => format!("daemon {daemon}, {status}"),
        Seen::Free => "free".to_owned(),
        Seen::Identified {
            identity,
            baud,
            framing,
        } => format!("free, {} at {baud} {framing}", describe(identity)),
        Seen::Unidentified(why) => format!("free, not identified ({why})"),
        Seen::Locked(Lock { file, pid, stale }) => {
            let by = match (pid, stale) {
                (Some(pid), false) => format!("locked by pid {pid}"),
                (Some(pid), true) => format!("stale lock by pid {pid}, no such process"),
                (None, _) => "locked, pid unreadable".to_owned(),
            };
            format!("free, {by} ({file}), not probed")
        }
    };
    let usb = row.usb.iter().map(|usb| {
        let names = [&usb.manufacturer, &usb.product]
            .into_iter()
            .flatten()
            .fold(String::new(), |text, name| text + " " + name);
        format!("usb {:04x}:{:04x}{names}", usb.vid, usb.pid)
    });
    std::iter::once(format!("{}: {seen}", row.port))
        .chain(usb)
        .chain(row.aliases.iter().cloned())
        .map(|field| plain(&field))
        .collect::<Vec<_>>()
        .join(SEPARATOR)
}

/// `text` as printable ASCII on one line, with no [`SEPARATOR`] in it:
/// an adapter names itself, and an error may quote what a port sent.
fn plain(text: &str) -> String {
    text.replace(SEPARATOR, ", ")
        .chars()
        .map(|c| match c {
            ' '..='~' => c,
            _ => '?',
        })
        .collect()
}

/// The ports on this host, read from sysfs.
///
/// Not the serialport crate's listing, which opens every 8250 port to
/// see whether a UART is there, setting it raw at 9600 and raising its
/// modem lines: as root, that reaches through another program's
/// exclusive hold.  A serial core port with no UART reads type 0
/// instead (`PORT_UNKNOWN`).
#[cfg(target_os = "linux")]
fn ports() -> Result<Vec<Port>> {
    Ok(sysfs_ports(
        Path::new("/sys/class/tty"),
        Path::new("/dev"),
        Path::new("/dev/serial/by-id"),
    )?)
}

/// The ports on this host, as the system lists them, which opens none.
#[cfg(not(target_os = "linux"))]
fn ports() -> Result<Vec<Port>> {
    Ok(serialport::available_ports()?
        .into_iter()
        .map(|info| Port {
            path: info.port_name,
            aliases: Vec::new(),
            usb: match info.port_type {
                serialport::SerialPortType::UsbPort(usb) => Some(Usb {
                    vid: usb.vid,
                    pid: usb.pid,
                    manufacturer: usb.manufacturer,
                    product: usb.product,
                }),
                _ => None,
            },
        })
        .collect())
}

/// The ttys in `class` with hardware behind them, at their nodes in
/// `dev`, each with the names in `by_id` that lead to it.
#[cfg(target_os = "linux")]
fn sysfs_ports(class: &Path, dev: &Path, by_id: &Path) -> std::io::Result<Vec<Port>> {
    let aliases: Vec<(PathBuf, String)> = std::fs::read_dir(by_id)
        .map(|entries| {
            entries
                .filter_map(|entry| {
                    let path = entry.ok()?.path();
                    Some((
                        std::fs::canonicalize(&path).ok()?,
                        path.to_str()?.to_owned(),
                    ))
                })
                .collect()
        })
        .unwrap_or_default();
    let mut ports: Vec<Port> = std::fs::read_dir(class)?
        .filter_map(|entry| {
            let entry = entry.ok()?;
            let tty = entry.path();
            // A console or a pseudo-terminal has no device behind it.
            let device = std::fs::canonicalize(tty.join("device")).ok()?;
            if std::fs::read_to_string(tty.join("type")).is_ok_and(|kind| kind.trim() == "0") {
                return None;
            }
            let node = dev.join(entry.file_name());
            let target = std::fs::canonicalize(&node).ok()?;
            let mut names: Vec<String> = aliases
                .iter()
                .filter(|(to, _)| *to == target)
                .map(|(_, name)| name.clone())
                .collect();
            names.sort();
            Some(Port {
                path: node.to_str()?.to_owned(),
                aliases: names,
                usb: usb(&device),
            })
        })
        .collect();
    ports.sort_by(|a, b| a.path.cmp(&b.path));
    Ok(ports)
}

/// The USB device `device` is part of, from the nearest directory above
/// it that names a vendor.
#[cfg(target_os = "linux")]
fn usb(device: &Path) -> Option<Usb> {
    let dir = device
        .ancestors()
        .find(|dir| dir.join("idVendor").is_file())?;
    let text = |name: &str| {
        std::fs::read_to_string(dir.join(name))
            .ok()
            .map(|text| text.trim().to_owned())
            .filter(|text| !text.is_empty())
    };
    let hex = |name: &str| text(name).and_then(|text| u16::from_str_radix(&text, 16).ok());
    Some(Usb {
        vid: hex("idVendor")?,
        pid: hex("idProduct")?,
        manufacturer: text("manufacturer"),
        product: text("product"),
    })
}

#[cfg(test)]
mod tests {
    use std::io::Read;
    use std::io::Write;
    use std::time::Duration;

    use smartclock::parse;
    use smartclock::session::Config;
    use smartclock::transport::Transport;
    use smartclock::transport::serial::Settings;
    use smartclock::types::BaudRate;
    use smartclock::types::Framing;
    use smartclock_sim::receiver::Receiver;
    use smartclock_sim::transport::SimTransport;

    use super::Holder;
    use super::Lock;
    use super::Port;
    use super::Probe;
    use super::Row;
    use super::Seen;
    use super::Usb;
    use super::identify;
    use super::line;
    use super::lock;
    use super::lock_pid;
    use super::rows;
    use super::spelling;
    use super::unit_device;
    use super::unplaced;
    use crate::daemons::Asked;
    use crate::daemons::State;
    use smartclock::client::Daemons;
    use smartclock::client::Found;
    use smartclock::client::Instance;
    use smartclock::client::Presence;

    /// A port with nothing on the other end that understands it.
    struct Deaf;

    impl Read for Deaf {
        fn read(&mut self, _: &mut [u8]) -> std::io::Result<usize> {
            Ok(0)
        }
    }

    impl Write for Deaf {
        fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
            Ok(buf.len())
        }

        fn flush(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }

    impl Transport for Deaf {
        fn describe(&self) -> String {
            "a port hearing nothing".to_owned()
        }
    }

    /// Quick to give up, since every wrong setting costs one timeout.
    fn quick() -> Probe {
        Probe {
            baud: BaudRate::B19200,
            framing: Framing::EightNone,
            config: Config {
                timeout: Duration::from_millis(300),
                idle: Duration::from_millis(20),
                ..Config::default()
            },
        }
    }

    fn port(path: &str) -> Port {
        Port {
            path: path.to_owned(),
            aliases: Vec::new(),
            usb: None,
        }
    }

    fn holder(name: &str, port: Option<&str>) -> Holder {
        Holder {
            name: name.to_owned(),
            status: "running, Z3801A serial 0000A00000 firmware 3543-A".to_owned(),
            port: port.map(str::to_owned),
            reported: port.is_some(),
        }
    }

    #[test]
    fn a_probe_finds_a_receiver_at_the_settings_attach_tries() {
        let at = (BaudRate::B19200, Framing::SevenOdd);
        let mut receiver = Some(Receiver::z3801a());
        let seen = identify("/dev/ttyUSB-test", &quick(), |tried: &Settings| {
            Ok(if (tried.baud, tried.framing) == at {
                Box::new(SimTransport::new(receiver.take().expect("opened once")))
                    as Box<dyn Transport + Send>
            } else {
                Box::new(Deaf)
            })
        });
        match seen {
            Seen::Identified {
                identity,
                baud,
                framing,
            } => {
                assert_eq!(identity.model, "Z3801A");
                assert_eq!((baud, framing), at);
            }
            other => panic!("{other:?}"),
        }
    }

    #[test]
    fn a_port_nothing_answers_on_is_not_identified() {
        let seen = identify("/dev/ttyUSB-test", &quick(), |_: &Settings| Ok(Deaf));
        assert!(matches!(seen, Seen::Unidentified(_)), "{seen:?}");
    }

    #[test]
    fn a_held_port_is_the_daemons_and_a_daemon_elsewhere_has_a_row_of_its_own() {
        let ports = vec![port("/dev/ttyS0"), port("/dev/ttyUSB0")];
        let holders = [
            holder("ttyUSB0", Some("/dev/ttyUSB0")),
            holder("sim", Some("tcp://127.0.0.1:5025")),
            holder("wedged", None),
        ];
        let lines: Vec<String> = rows(ports, &holders).iter().map(line).collect();
        assert_eq!(
            lines,
            [
                "/dev/ttyS0: free",
                "/dev/ttyUSB0: daemon ttyUSB0, running, Z3801A serial 0000A00000 firmware 3543-A",
                "tcp://127.0.0.1:5025: daemon sim, running, Z3801A serial 0000A00000 firmware 3543-A",
                "unknown: daemon wedged, running, Z3801A serial 0000A00000 firmware 3543-A",
            ]
        );
    }

    #[cfg(unix)]
    #[test]
    fn a_daemon_given_a_link_to_a_port_holds_that_port() {
        let dir = std::env::temp_dir().join(format!("smartclock-link-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).expect("directory");
        let (node, link) = (dir.join("ttyUSB0"), dir.join("usb-1a86-port0"));
        std::fs::write(&node, b"").expect("node");
        std::os::unix::fs::symlink(&node, &link).expect("link");
        let found = rows(
            vec![port(node.to_str().expect("UTF-8"))],
            &[holder("bench", link.to_str())],
        );
        let _ = std::fs::remove_dir_all(&dir);
        assert_eq!(found.len(), 1, "{found:?}");
        assert!(
            matches!(&found[0].seen, Seen::Held { daemon, .. } if daemon == "bench"),
            "{found:?}"
        );
    }

    #[test]
    fn a_line_carries_the_adapter_and_the_other_names_as_plain_fields() {
        let row = Row {
            port: "/dev/ttyUSB0".to_owned(),
            aliases: vec!["/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0".to_owned()],
            usb: Some(Usb {
                vid: 0x1a86,
                pid: 0x7523,
                manufacturer: None,
                product: Some("USB | Serial\n\u{e9}".to_owned()),
            }),
            seen: Seen::Identified {
                identity: parse::identity("HEWLETT-PACKARD,58503A,0000A00000,3704-C")
                    .expect("identity"),
                baud: BaudRate::B9600,
                framing: Framing::EightNone,
            },
        };
        assert_eq!(
            line(&row),
            "/dev/ttyUSB0: free, 58503A serial 0000A00000 firmware 3704-C at 9600 8N1 \
             | usb 1a86:7523 USB, Serial?? \
             | /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"
        );
    }

    fn asked(name: &str, socket: Presence, state: State, device: Option<&str>) -> Asked {
        Asked {
            found: Found {
                instance: Instance {
                    name: name.to_owned(),
                    endpoint: format!("/run/smartclockd/{name}/socket").into(),
                },
                socket,
            },
            state,
            device: device.map(str::to_owned),
        }
    }

    #[test]
    fn a_daemon_holds_the_port_it_names_and_a_gone_one_holds_none() {
        let run = Daemons::Dir("/run/smartclockd".into());
        let named = super::holder(
            &run,
            asked(
                "bench",
                Presence::Present,
                State::Silent(None, "link down".to_owned()),
                Some("/dev/ttyUSB3"),
            ),
        )
        .expect("a daemon");
        assert_eq!(named.port.as_deref(), Some("/dev/ttyUSB3"));
        let stale = asked(
            "ttyUSB1",
            Presence::Present,
            State::Stopped("stale socket: connection refused".to_owned()),
            None,
        );
        assert!(super::holder(&run, stale).is_none());
        let waiting = super::holder(
            &run,
            asked("no-such-port", Presence::Absent, State::Waiting, None),
        )
        .expect("a daemon still waiting for its receiver");
        assert_eq!(waiting.port, None);
        let notes = unplaced(&[named, waiting]);
        assert_eq!(notes.len(), 1, "{notes:?}");
        assert!(
            notes[0].starts_with("daemon no-such-port ")
                && notes[0].ends_with("nothing was skipped for it"),
            "{notes:?}"
        );
        let guessed = Holder {
            reported: false,
            ..holder("ttyUSB1", Some("/dev/ttyUSB1"))
        };
        assert_eq!(
            unplaced(&[guessed]),
            [
                "daemon ttyUSB1 did not say which port it holds; not probing /dev/ttyUSB1, \
              its unit's port"
            ]
        );
    }

    #[test]
    fn a_lock_file_names_its_pid_and_whether_it_is_running() {
        let root = std::env::temp_dir().join(format!("smartclock-locks-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        let (var, run, proc) = (root.join("var"), root.join("run"), root.join("proc"));
        for dir in [&var, &run, &proc.join("1234")] {
            std::fs::create_dir_all(dir).expect("directory");
        }
        std::fs::write(run.join("LCK..ttyS0"), "      1234\n").expect("live lock");
        std::fs::write(run.join("LCK..ttyS1"), "      4321\n").expect("stale lock");
        std::fs::write(var.join("LCK..ttyS2"), "garbage").expect("unreadable lock");
        let dirs = [var.as_path(), run.as_path()];
        let found: Vec<Option<Lock>> = ["/dev/ttyS0", "/dev/ttyS1", "/dev/ttyS2", "/dev/ttyS3"]
            .into_iter()
            .map(|port| lock(port, &dirs, &proc))
            .collect();
        let _ = std::fs::remove_dir_all(&root);
        let file = |dir: &std::path::Path, name: &str| dir.join(name).display().to_string();
        assert_eq!(
            found,
            [
                Some(Lock {
                    file: file(&run, "LCK..ttyS0"),
                    pid: Some(1234),
                    stale: false
                }),
                Some(Lock {
                    file: file(&run, "LCK..ttyS1"),
                    pid: Some(4321),
                    stale: true
                }),
                Some(Lock {
                    file: file(&var, "LCK..ttyS2"),
                    pid: None,
                    stale: false
                }),
                None,
            ]
        );
        assert_eq!(lock_pid(&77u32.to_ne_bytes()), Some(77));
    }

    #[test]
    fn a_locked_port_says_by_whom_and_that_it_was_not_probed() {
        let row = |lock| Row {
            port: "/dev/ttyS0".to_owned(),
            aliases: Vec::new(),
            usb: None,
            seen: Seen::Locked(lock),
        };
        let lock = |pid, stale| Lock {
            file: "/run/lock/LCK..ttyS0".to_owned(),
            pid,
            stale,
        };
        assert_eq!(
            line(&row(lock(Some(1234), false))),
            "/dev/ttyS0: free, locked by pid 1234 (/run/lock/LCK..ttyS0), not probed"
        );
        assert_eq!(
            line(&row(lock(Some(1234), true))),
            "/dev/ttyS0: free, stale lock by pid 1234, no such process \
             (/run/lock/LCK..ttyS0), not probed"
        );
        assert_eq!(
            line(&row(lock(None, false))),
            "/dev/ttyS0: free, locked, pid unreadable (/run/lock/LCK..ttyS0), not probed"
        );
    }

    #[test]
    fn one_port_spelled_two_ways_is_one() {
        assert_eq!(spelling(r"\\.\com3"), "COM3");
        assert_eq!(spelling("COM12"), "COM12");
        assert_eq!(spelling("/dev/tty.usbserial-1"), "/dev/cu.usbserial-1");
        assert_eq!(spelling("/dev/ttyUSB0"), "/dev/ttyUSB0");
        assert_eq!(spelling("tcp://127.0.0.1:5025"), "tcp://127.0.0.1:5025");
    }

    #[test]
    fn a_unit_instance_names_its_port_as_systemd_escapes_it() {
        assert_eq!(unit_device("ttyUSB0").as_deref(), Some("/dev/ttyUSB0"));
        assert_eq!(
            unit_device(r"serial-by\x2did-usb\x2d1a86\x2dport0").as_deref(),
            Some("/dev/serial/by-id/usb-1a86-port0")
        );
    }

    #[cfg(target_os = "linux")]
    #[test]
    fn sysfs_gives_the_ports_with_a_uart_behind_them_and_their_names() {
        use std::os::unix::fs::symlink;

        use super::sysfs_ports;

        let root = std::env::temp_dir().join(format!("smartclock-devices-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        let (class, dev, by_id, devices) = (
            root.join("class"),
            root.join("dev"),
            root.join("by-id"),
            root.join("devices"),
        );
        let usb = devices.join("usb1/1-2");
        let interface = usb.join("1-2:1.0/ttyUSB0");
        for dir in [&class, &dev, &by_id, &interface, &devices.join("serial0")] {
            std::fs::create_dir_all(dir).expect("directory");
        }
        std::fs::write(usb.join("idVendor"), "1a86\n").expect("vendor");
        std::fs::write(usb.join("idProduct"), "7523\n").expect("product");
        std::fs::write(usb.join("product"), "USB Serial\n").expect("product name");
        for (tty, device, kind) in [
            ("ttyUSB0", Some(interface.clone()), None),
            ("ttyS0", Some(devices.join("serial0")), Some("4")),
            ("ttyS1", Some(devices.join("serial0")), Some("0")),
            ("tty1", None, None),
        ] {
            let dir = class.join(tty);
            std::fs::create_dir_all(&dir).expect("tty");
            std::fs::write(dev.join(tty), b"").expect("node");
            if let Some(device) = device {
                symlink(device, dir.join("device")).expect("device link");
            }
            if let Some(kind) = kind {
                std::fs::write(dir.join("type"), format!("{kind}\n")).expect("type");
            }
        }
        symlink(
            dev.join("ttyUSB0"),
            by_id.join("usb-1a86_USB_Serial-if00-port0"),
        )
        .expect("by-id link");
        let found = sysfs_ports(&class, &dev, &by_id);
        let _ = std::fs::remove_dir_all(&root);
        let found = found.expect("ports");
        let paths: Vec<&str> = found.iter().map(|port| port.path.as_str()).collect();
        assert_eq!(
            paths,
            [
                dev.join("ttyS0").to_str().expect("UTF-8"),
                dev.join("ttyUSB0").to_str().expect("UTF-8")
            ]
        );
        assert_eq!(found[0].usb, None);
        assert_eq!(
            found[1].usb,
            Some(Usb {
                vid: 0x1a86,
                pid: 0x7523,
                manufacturer: None,
                product: Some("USB Serial".to_owned()),
            })
        );
        assert_eq!(
            found[1].aliases,
            [by_id
                .join("usb-1a86_USB_Serial-if00-port0")
                .to_str()
                .expect("UTF-8")]
        );
    }
}
