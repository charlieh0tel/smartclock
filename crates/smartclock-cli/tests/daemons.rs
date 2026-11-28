//! `daemons` lists the instances under a run directory and says of
//! each what it is attached to, or why that is not known, from fake
//! daemons that answer as smartclockd does.

#![cfg(unix)]

mod common;

use std::os::unix::fs::PermissionsExt as _;
use std::path::Path;

use common::Fake;
use common::RunDir;
use common::cli;
use smartclock::link;
use smartclock::snapshot::Freshness;

/// The `*IDN?` the fake daemons are attached to.
const IDENTITY: &str = "HEWLETT-PACKARD,58503A,0000A00000,3704-C";

/// A daemon at `socket` attached to [`IDENTITY`], whose latest reading
/// is `freshness`.
fn fake(socket: &Path, freshness: Freshness) {
    Fake {
        freshness,
        ..Fake::attached(IDENTITY)
    }
    .serve(socket);
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
    Fake {
        device: Some("/dev/ttyUSB0"),
        ..Fake::attached(IDENTITY)
    }
    .serve(&live);
    let silent = run.instance("silent");
    fake(&silent, Freshness::Disconnected);
    let stale = run.instance("stale");
    drop(std::os::unix::net::UnixListener::bind(&stale).expect("bind"));
    let waiting = run.instance("waiting");
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
            "live {}: running, 58503A serial 0000A00000 firmware 3704-C on /dev/ttyUSB0",
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
        format!("waiting {}: waiting for a receiver", waiting.display()),
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
    let output = cli(&["daemons", "--run-dir", run.path()]);
    assert!(output.status.success());
    assert_eq!(
        String::from_utf8_lossy(&output.stdout),
        format!("no daemons under {}\n", run.0.display())
    );

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
