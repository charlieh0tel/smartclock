//! `daemons` lists the instances under a run directory and says of
//! each what it is attached to, or why that is not known, from fake
//! daemons that answer as smartclockd does.

#![cfg(unix)]

use std::io::BufRead as _;
use std::io::BufReader;
use std::io::Write as _;
use std::os::unix::fs::PermissionsExt as _;
use std::path::Path;
use std::path::PathBuf;
use std::process::Command;
use std::process::Output;

use jiff::Timestamp;
use smartclock::link;
use smartclock::protocol::Message;
use smartclock::protocol::Op;
use smartclock::protocol::Request;
use smartclock::snapshot::Freshness;
use smartclock::snapshot::Snapshot;
use smartclock::snapshot::Tier;
use smartclock::wire::Reading;

/// The `*IDN?` the fake daemons are attached to.
const IDENTITY: &str = "HEWLETT-PACKARD,58503A,0000A00000,3704-C";

/// A run directory of the test's own, removed on drop.
struct RunDir(PathBuf);

impl RunDir {
    fn new(name: &str) -> Self {
        let dir =
            std::env::temp_dir().join(format!("smartclock-cli-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).expect("a run directory");
        Self(dir)
    }

    /// An instance directory, made, and where its socket goes.
    fn instance(&self, name: &str) -> PathBuf {
        let dir = self.0.join(name);
        std::fs::create_dir_all(&dir).expect("an instance directory");
        dir.join("socket")
    }
}

impl Drop for RunDir {
    fn drop(&mut self) {
        // A shut instance directory has to be opened to be removed.
        if let Ok(entries) = std::fs::read_dir(&self.0) {
            for entry in entries.flatten() {
                let _ =
                    std::fs::set_permissions(entry.path(), std::fs::Permissions::from_mode(0o700));
            }
        }
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

/// A daemon at `socket` attached to [`IDENTITY`], whose latest reading
/// is `freshness`, answering `info` and `latest` as smartclockd does
/// and refusing anything else.
fn fake(socket: &Path, freshness: Freshness) {
    let listener = link::listen(socket).expect("listen");
    std::thread::spawn(move || {
        while let Ok(stream) = listener.accept() {
            let Ok(mut writer) = stream.try_clone() else {
                continue;
            };
            for line in BufReader::new(stream).lines() {
                let Ok(line) = line else { break };
                let Ok(request) = serde_json::from_str::<Request>(&line) else {
                    break;
                };
                let reply = match request.op {
                    Op::Info => {
                        Message::ok(request.id, serde_json::json!({ "identity": IDENTITY }))
                    }
                    Op::Latest => {
                        let mut snapshot = Snapshot::new(Timestamp::now());
                        snapshot.freshness = freshness;
                        if freshness != Freshness::Live {
                            snapshot.polled.failed(Tier::Fast, "no prompt within 5s");
                        }
                        Message::ok(
                            request.id,
                            serde_json::to_value(Reading::from(&snapshot)).expect("reading"),
                        )
                    }
                    _ => Message::err(request.id, "not asked of a listing"),
                };
                let text = serde_json::to_string(&reply).expect("reply");
                if writeln!(writer, "{text}").is_err() {
                    break;
                }
            }
        }
    });
}

/// Something at `socket` that accepts and never answers.
fn wedged(socket: &Path) {
    let listener = link::listen(socket).expect("listen");
    std::thread::spawn(move || {
        let mut held = Vec::new();
        while let Ok(stream) = listener.accept() {
            held.push(stream);
        }
    });
}

/// Run `smartclock-cli` with `args`.
fn cli(args: &[&str]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_smartclock-cli"))
        .args(args)
        .output()
        .expect("run the tool")
}

/// `daemons` over `dir`: its exit status and its standard output.
fn daemons(dir: &Path) -> (bool, String) {
    let output = cli(&["daemons", "--run-dir", dir.to_str().expect("a UTF-8 path")]);
    (
        output.status.success(),
        String::from_utf8_lossy(&output.stdout).into_owned(),
    )
}

#[test]
fn every_instance_is_listed_with_what_it_is_attached_to_or_why_not() {
    let run = RunDir::new("daemons");
    let live = run.instance("live");
    fake(&live, Freshness::Live);
    let silent = run.instance("silent");
    fake(&silent, Freshness::Disconnected);
    let stale = run.instance("stale");
    drop(std::os::unix::net::UnixListener::bind(&stale).expect("bind"));
    let stopped = run.instance("stopped");
    let hung = run.instance("wedged");
    wedged(&hung);
    let shut = run.instance("shut");
    std::fs::write(&shut, b"").expect("socket stand-in");
    std::fs::set_permissions(
        shut.parent().expect("its directory"),
        std::fs::Permissions::from_mode(0o000),
    )
    .expect("chmod");
    // Root looks into anything, so there that case cannot be made.
    let root = std::fs::metadata(&shut).is_ok();

    let (ok, out) = daemons(&run.0);
    println!("{out}");
    assert!(ok, "{out}");
    let lines: Vec<&str> = out.lines().collect();
    let expected = [
        format!(
            "live {}: running, 58503A serial 0000A00000 firmware 3704-C",
            live.display()
        ),
        if root {
            format!("shut {}: not running (no socket)", shut.display())
        } else {
            format!(
                "shut {}: cannot tell (permission denied; are you in its group?)",
                shut.display()
            )
        },
        format!(
            "silent {}: running, receiver not answering (last 58503A serial 0000A00000 \
             firmware 3704-C; link down: no prompt within 5s)",
            silent.display()
        ),
        format!(
            "stale {}: not running (stale socket: connection refused)",
            stale.display()
        ),
        format!("stopped {}: not running (no socket)", stopped.display()),
        format!(
            "wedged {}: not answering (it did not answer within 3.0s)",
            hung.display()
        ),
    ];
    // Root reads the shut directory's stand-in, which is not a socket.
    for (got, want) in lines.iter().zip(&expected) {
        if root && got.starts_with("shut ") {
            continue;
        }
        assert_eq!(got, want);
    }
    assert_eq!(lines.len(), expected.len(), "{out}");
}

#[test]
fn no_daemons_is_said_and_is_not_an_error() {
    let run = RunDir::new("no-daemons");
    let (ok, out) = daemons(&run.0);
    assert!(ok);
    assert_eq!(out, format!("no daemons under {}\n", run.0.display()));

    let missing = run.0.join("missing");
    let (ok, out) = daemons(&missing);
    assert!(ok);
    assert_eq!(out, format!("no daemons under {}\n", missing.display()));
}

#[test]
fn a_note_without_a_daemon_says_how_to_find_one() {
    let output = cli(&["note", "added", "an", "amplifier"]);
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(!output.status.success());
    assert!(stderr.contains("smartclock-cli daemons"), "{stderr}");
}
