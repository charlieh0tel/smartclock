//! `--serial` refuses a receiver that is not the one named before
//! anything is sent to it, directly and through a daemon, and finds the
//! local daemon attached to it.

#![cfg(unix)]

mod common;

use std::io::Read;
use std::io::Write;
use std::net::TcpListener;
use std::process::Output;
use std::sync::Arc;
use std::sync::Mutex;

use common::Asked;
use common::Fake;
use common::RunDir;
use common::cli;
use smartclock::protocol::Op;
use smartclock_sim::net::serve;
use smartclock_sim::receiver::Receiver;
use smartclock_sim::transport::SimTransport;

/// The simulator's serial, which every simulated model shares.
const SIMULATED: &str = "0000A00000";
const Z3817A: &str = "HEWLETT-PACKARD,Z3817A,3800A00002,3752-A";
const A58503: &str = "HEWLETT-PACKARD,58503A,3700A00001,3704-C";

/// A port that keeps everything it is sent.
struct Recording {
    port: SimTransport,
    sent: Arc<Mutex<Vec<u8>>>,
}

impl Read for Recording {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        self.port.read(buf)
    }
}

impl Write for Recording {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        self.sent.lock().expect("sent").extend_from_slice(buf);
        self.port.write(buf)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        self.port.flush()
    }
}

/// The simulator on a local TCP port: its `--device`, and everything
/// every connection sent it.
fn simulator() -> (String, Arc<Mutex<Vec<u8>>>) {
    let listener = TcpListener::bind("127.0.0.1:0").expect("bind");
    let device = format!("tcp://{}", listener.local_addr().expect("address"));
    let receiver = SimTransport::new(Receiver::default());
    let sent = Arc::new(Mutex::new(Vec::new()));
    let log = Arc::clone(&sent);
    std::thread::spawn(move || {
        for stream in listener.incoming().flatten() {
            let port = Recording {
                port: receiver.clone(),
                sent: Arc::clone(&log),
            };
            std::thread::spawn(move || serve(stream, port));
        }
    });
    (device, sent)
}

fn text(sent: &Arc<Mutex<Vec<u8>>>) -> String {
    String::from_utf8_lossy(&sent.lock().expect("sent")).into_owned()
}

fn stderr(output: &Output) -> String {
    String::from_utf8_lossy(&output.stderr).into_owned()
}

/// The queries a fake daemon was sent, with the serial each carried.
fn queries(asked: &Asked) -> Vec<(String, Option<String>)> {
    asked
        .lock()
        .expect("asked")
        .iter()
        .filter_map(|op| match op {
            Op::Query {
                scpi,
                receiver_serial,
            } => Some((scpi.clone(), receiver_serial.clone())),
            _ => None,
        })
        .collect()
}

const QUERY: &str = ":SYNChronization:TFOMerit?";

#[test]
fn directly_the_named_receiver_is_queried() {
    let (device, sent) = simulator();
    let output = cli(&["--device", &device, "--serial", SIMULATED, "query", QUERY]);
    assert!(output.status.success(), "{}", stderr(&output));
    assert!(text(&sent).contains(QUERY));
}

#[test]
fn directly_another_receiver_is_sent_nothing_after_its_identity() {
    let (device, sent) = simulator();
    let output = cli(&[
        "--device",
        &device,
        "--serial",
        "3700A00001",
        "query",
        QUERY,
    ]);
    let why = stderr(&output);
    assert!(!output.status.success());
    for part in ["3700A00001", SIMULATED, device.as_str()] {
        assert!(why.contains(part), "{why}");
    }
    let sent = text(&sent);
    assert!(sent.contains("*IDN?"), "{sent:?}");
    assert!(!sent.contains(QUERY), "{sent:?}");
}

#[test]
fn a_console_read_of_another_receiver_never_enters_the_console() {
    let (device, sent) = simulator();
    let out = std::env::temp_dir().join(format!("smartclock-cli-serial-{}", std::process::id()));
    let output = cli(&[
        "--device",
        &device,
        "--serial",
        "3700A00001",
        "read-eeprom",
        "--out",
        out.to_str().expect("a UTF-8 path"),
    ]);
    assert!(!output.status.success());
    assert!(
        stderr(&output).contains("3700A00001"),
        "{}",
        stderr(&output)
    );
    assert!(!text(&sent).contains("PFORTH"), "{:?}", text(&sent));
    assert!(!out.exists());
}

#[test]
fn through_a_daemon_the_serial_is_checked_and_sent_with_the_command() {
    let run = RunDir::new("serial-match");
    let socket = run.instance("ttyUSB0");
    let asked = Fake::attached(A58503).serve(&socket);
    let daemon = socket.to_str().expect("a UTF-8 path");
    let output = cli(&["--daemon", daemon, "--serial", "3700A00001", "query", QUERY]);
    assert!(output.status.success(), "{}", stderr(&output));
    assert_eq!(
        queries(&asked),
        [(QUERY.to_owned(), Some("3700A00001".to_owned()))]
    );
}

#[test]
fn through_a_daemon_another_receiver_is_sent_nothing() {
    let run = RunDir::new("serial-mismatch");
    let socket = run.instance("ttyUSB0");
    let asked = Fake::attached(Z3817A).serve(&socket);
    let daemon = socket.to_str().expect("a UTF-8 path");
    for command in [&["query", QUERY][..], &["note", "moved", "the", "antenna"]] {
        let output = cli(&[&["--daemon", daemon, "--serial", "3700A00001"][..], command].concat());
        let why = stderr(&output);
        assert!(!output.status.success());
        for part in ["3700A00001", "3800A00002", daemon] {
            assert!(why.contains(part), "{why}");
        }
    }
    let asked = asked.lock().expect("asked");
    assert!(asked.iter().all(|op| matches!(op, Op::Info)), "{asked:?}");
}

#[test]
fn a_daemon_too_old_to_check_the_serial_is_refused() {
    let run = RunDir::new("serial-old");
    let socket = run.instance("ttyUSB0");
    let asked = Fake {
        checks: false,
        ..Fake::attached(A58503)
    }
    .serve(&socket);
    let daemon = socket.to_str().expect("a UTF-8 path");
    let output = cli(&["--daemon", daemon, "--serial", "3700A00001", "query", QUERY]);
    assert!(!output.status.success());
    assert!(stderr(&output).contains("too old"), "{}", stderr(&output));
    assert!(queries(&asked).is_empty());
}

#[test]
fn a_daemon_that_has_identified_nothing_is_refused() {
    let run = RunDir::new("serial-unidentified");
    let socket = run.instance("ttyUSB0");
    let asked = Fake::attached("").serve(&socket);
    let daemon = socket.to_str().expect("a UTF-8 path");
    let output = cli(&["--daemon", daemon, "--serial", "3700A00001", "query", QUERY]);
    assert!(!output.status.success());
    assert!(queries(&asked).is_empty());
}

#[test]
fn the_serial_alone_finds_the_one_daemon_attached_to_it() {
    let run = RunDir::new("serial-one");
    let other = Fake::attached(Z3817A).serve(&run.instance("ttyUSB0"));
    let wanted = Fake::attached(A58503).serve(&run.instance("ttyUSB1"));
    let output = cli(&[
        "--run-dir",
        run.path(),
        "--serial",
        "3700A00001",
        "query",
        QUERY,
    ]);
    assert!(output.status.success(), "{}", stderr(&output));
    assert_eq!(
        queries(&wanted),
        [(QUERY.to_owned(), Some("3700A00001".to_owned()))]
    );
    assert!(queries(&other).is_empty());
}

#[test]
fn no_daemon_attached_to_the_serial_is_an_error_listing_those_found() {
    let empty = RunDir::new("serial-none");
    let output = cli(&[
        "--run-dir",
        empty.path(),
        "--serial",
        "3700A00001",
        "diagnose",
    ]);
    assert!(!output.status.success());
    assert!(
        stderr(&output).contains("none were found"),
        "{}",
        stderr(&output)
    );

    let run = RunDir::new("serial-zero");
    let asked = Fake::attached(Z3817A).serve(&run.instance("ttyUSB0"));
    let output = cli(&[
        "--run-dir",
        run.path(),
        "--serial",
        "3700A00001",
        "query",
        QUERY,
    ]);
    let why = stderr(&output);
    assert!(!output.status.success());
    assert!(
        why.contains("ttyUSB0") && why.contains("Z3817A serial 3800A00002 firmware 3752-A"),
        "{why}"
    );
    assert!(queries(&asked).is_empty());
}

#[test]
fn two_daemons_attached_to_the_serial_is_an_error_listing_both() {
    let run = RunDir::new("serial-two");
    let first = Fake::attached(A58503).serve(&run.instance("ttyUSB0"));
    let second = Fake::attached(A58503).serve(&run.instance("ttyUSB1"));
    let output = cli(&[
        "--run-dir",
        run.path(),
        "--serial",
        "3700A00001",
        "query",
        QUERY,
    ]);
    let why = stderr(&output);
    assert!(!output.status.success());
    assert!(
        why.contains("more than one") && why.contains("ttyUSB0") && why.contains("ttyUSB1"),
        "{why}"
    );
    assert!(queries(&first).is_empty() && queries(&second).is_empty());
}

#[test]
fn a_command_needing_the_port_is_told_which_daemon_to_stop() {
    let run = RunDir::new("serial-port");
    let socket = run.instance("ttyUSB1");
    let asked = Fake::attached(A58503).serve(&socket);
    let output = cli(&["--run-dir", run.path(), "--serial", "3700A00001", "probe"]);
    let why = stderr(&output);
    assert!(!output.status.success());
    assert!(
        why.contains(socket.to_str().expect("a UTF-8 path")),
        "{why}"
    );
    assert!(queries(&asked).is_empty());
}
