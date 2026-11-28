//! `devices` lists the daemons under a run directory against the ports
//! they hold, from fake daemons that answer as smartclockd does.
//!
//! Never with `--probe`: that would open this host's own ports.

#![cfg(unix)]

mod common;

use common::Fake;
use common::RunDir;
use common::cli;

/// The `*IDN?` the fake daemons are attached to.
const IDENTITY: &str = "HEWLETT-PACKARD,Z3801A,0000A00000,3543-A";

#[test]
fn each_daemon_is_listed_on_the_port_it_holds_and_a_gone_one_is_not() {
    let run = RunDir::new("devices");
    Fake {
        device: Some("tcp://127.0.0.1:5025"),
        ..Fake::attached(IDENTITY)
    }
    .serve(&run.instance("sim"));
    Fake::attached(IDENTITY).serve(&run.instance("old"));
    let stale = run.instance("stale");
    drop(std::os::unix::net::UnixListener::bind(&stale).expect("bind"));
    run.instance("waiting");

    let output = cli(&["devices", "--run-dir", run.path()]);
    let out = String::from_utf8_lossy(&output.stdout);
    println!("{out}");
    assert!(output.status.success(), "{out}");
    let daemons: Vec<&str> = out
        .lines()
        .filter(|line| line.contains(": daemon "))
        .collect();
    assert_eq!(
        daemons,
        [
            "unknown: daemon old, running, Z3801A serial 0000A00000 firmware 3543-A",
            "tcp://127.0.0.1:5025: daemon sim, running, Z3801A serial 0000A00000 firmware 3543-A",
            "unknown: daemon waiting, not running (no socket)",
        ]
    );
    assert!(
        out.lines()
            .all(|line| line.contains(": daemon ") || line.contains(": free")),
        "{out}"
    );
}
