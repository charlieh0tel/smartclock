use std::time::Duration;

use smartclock::http::Status;

use super::Console;
use super::KEPT;
use super::LEASE;
use super::LINE_LIMIT;
use super::Port;
use crate::board::time::Cycles;
use crate::line::Line;
use crate::panel::record::Typed;
use crate::panel::record::take_typed;

/// A console on a TCP port nobody is connected to, and its line.
fn tcp() -> (Console, std::sync::mpsc::Receiver<Vec<u8>>, Line) {
    let line = Line::new("host", false);
    let (console, typed) = Console::new(Port::Tcp(line.clone()), jiff::Timestamp::UNIX_EPOCH);
    (console, typed, line)
}

/// A field of `body`, a console state.
fn field<'a>(body: &'a str, name: &str) -> &'a str {
    body.split_once(&format!("\"{name}\":"))
        .and_then(|(_, rest)| rest.split([',', '}']).next())
        .unwrap_or_else(|| panic!("{name} in {body}"))
}

#[test]
fn the_output_is_read_by_offset_and_the_oldest_is_dropped_past_its_bound() {
    let (console, _typed, _line) = tcp();
    let now = Duration::ZERO;
    console.sent(b"scpi > ");
    let read = console.read(0, None, now).body;
    assert_eq!(field(&read, "offset"), "7");
    assert!(read.contains("\"text\":\"scpi > \""), "{read}");
    console.sent(b"\r\nHP\x07");
    let read = console.read(7, None, now).body;
    assert!(
        read.contains("\"text\":\"\\u000d\\u000aHP\\u0007\""),
        "{read}"
    );
    console.sent(&vec![b'x'; KEPT]);
    let read = console.read(0, None, now).body;
    assert_eq!(field(&read, "from"), "12", "the oldest kept");
    assert_eq!(field(&read, "offset"), (12 + KEPT).to_string());
}

/// Each console's run is named apart, so that a page sees the emulator
/// start again even where the new run's offsets have passed its own.
#[test]
fn each_run_has_a_name_of_its_own() {
    let now = Duration::ZERO;
    let (first, _typed) = Console::new(Port::Page, jiff::Timestamp::UNIX_EPOCH);
    let (second, _typed) = Console::new(Port::Page, jiff::Timestamp::UNIX_EPOCH);
    let name = |console: &Console| field(&console.read(0, None, now).body, "run").to_owned();
    assert_eq!(name(&first), name(&first), "the same run, the same name");
    assert_ne!(name(&first), name(&second));
}

#[test]
fn a_connected_tcp_client_makes_every_page_read_only_and_ends_the_lease() {
    let (console, typed, line) = tcp();
    let now = Duration::ZERO;
    assert_eq!(
        field(&console.read(0, Some("a"), now).body, "writable"),
        "true"
    );
    let client = line.attach();
    let read = console.read(0, Some("a"), now).body;
    assert_eq!(field(&read, "writable"), "false");
    assert_eq!(field(&read, "typist"), "\"tcp\"");
    let refused = console.write(b"*IDN?", Some("a"), now);
    assert_eq!(refused.status, Status::Conflict);
    assert!(refused.body.contains("TCP client"), "{}", refused.body);
    drop(client);
    assert_eq!(
        field(&console.read(0, Some("b"), now).body, "writable"),
        "true",
        "the lease ended"
    );
    assert!(typed.try_recv().is_err());
}

#[test]
fn the_lease_is_one_pages_at_a_time() {
    let (console, typed, _line) = tcp();
    let now = Duration::ZERO;
    assert_eq!(
        field(&console.read(0, Some("a"), now).body, "writable"),
        "true"
    );
    let other = console.read(0, Some("b"), now).body;
    assert_eq!(field(&other, "writable"), "false");
    assert!(other.contains("another page"), "{other}");
    assert_eq!(console.write(b"x", Some("b"), now).status, Status::Conflict);
    assert_eq!(console.write(b"*IDN?\n", Some("a"), now).status, Status::Ok);
    assert_eq!(typed.try_recv().ok(), Some(b"*IDN?".to_vec()));
}

#[test]
fn the_lease_lapses_after_its_last_poll() {
    let (console, _typed, _line) = tcp();
    let now = Duration::ZERO;
    console.read(0, Some("a"), now);
    // a poll renews it
    let renewed = now + LEASE / 2;
    console.read(0, Some("a"), renewed);
    assert_eq!(
        field(&console.read(0, Some("b"), now + LEASE).body, "writable"),
        "false",
        "renewed"
    );
    assert_eq!(
        field(
            &console.read(0, Some("b"), renewed + LEASE).body,
            "writable"
        ),
        "true",
        "lapsed, and taken"
    );
}

#[test]
fn a_script_holds_the_port() {
    let (console, _typed) = Console::new(Port::Script, jiff::Timestamp::UNIX_EPOCH);
    let read = console.read(0, Some("a"), Duration::ZERO).body;
    assert_eq!(field(&read, "typist"), "\"script\"");
    assert_eq!(field(&read, "writable"), "false");
    assert_eq!(
        console.write(b"x", Some("a"), Duration::ZERO).status,
        Status::Conflict
    );
}

#[test]
fn with_no_far_end_the_page_holding_the_lease_types() {
    let (console, typed) = Console::new(Port::Page, jiff::Timestamp::UNIX_EPOCH);
    let now = Duration::ZERO;
    let read = console.read(0, Some("a"), now).body;
    assert_eq!(field(&read, "typist"), "\"page\"");
    assert_eq!(field(&read, "writable"), "true");
    assert_eq!(console.write(b"x", Some("b"), now).status, Status::Conflict);
    assert_eq!(console.write(b"*IDN?", Some("a"), now).status, Status::Ok);
    assert_eq!(typed.try_recv().ok(), Some(b"*IDN?".to_vec()));
}

/// A line typed while the port was the page's, and taken by the run
/// after a TCP client connected, is not put on the port in the middle of
/// the client's session.
#[test]
fn a_line_typed_before_a_tcp_client_connects_is_not_put_on_its_session() {
    let (console, typed, line) = tcp();
    let now = Duration::ZERO;
    console.read(0, Some("a"), now);
    assert_eq!(console.write(b"*IDN?", Some("a"), now).status, Status::Ok);
    let client = line.attach();
    assert_eq!(
        take_typed(&typed, Cycles(5), Cycles(5), |bytes| {
            line.inject_unattached(bytes)
        }),
        0
    );
    assert_eq!(line.receive(), None, "the client's session has none of it");
    drop(client);
}

#[test]
fn what_is_not_one_line_is_refused() {
    let (console, _typed, _line) = tcp();
    let now = Duration::ZERO;
    console.read(0, Some("a"), now);
    assert_eq!(
        console.write(b"a\nb", Some("a"), now).status,
        Status::BadRequest
    );
    assert_eq!(
        console
            .write(&vec![b'x'; LINE_LIMIT + 1], Some("a"), now)
            .status,
        Status::BadRequest
    );
    assert_eq!(console.write(b"", Some("a"), now).status, Status::Ok);
}

#[test]
fn a_typed_line_goes_on_with_the_terminator_and_its_record_replays_as_a_send() {
    let (console, typed, line) = tcp();
    let now = Duration::ZERO;
    console.read(0, Some("a"), now);
    console.write(b":SYST:ERR? \\x", Some("a"), now);
    assert_eq!(
        take_typed(&typed, Cycles(5), Cycles(16_777_216), |bytes| {
            line.inject_unattached(bytes)
        }),
        1
    );
    let put: Vec<u8> = std::iter::from_fn(|| line.receive()).collect();
    assert_eq!(put, b":SYST:ERR? \\x\r\n");
    let record = Typed {
        cycle: Cycles(5),
        at: Cycles(16_777_216),
        line: b":SYST:ERR? \\x\x07".to_vec(),
    };
    assert_eq!(
        record.to_string(),
        "console cycle=5 at=1 line=:SYST:ERR?\\x20\\\\x\\x07"
    );
}
