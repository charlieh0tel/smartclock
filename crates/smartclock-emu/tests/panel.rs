//! The panel page (docs/emulator.md, "The panel page"): a key clicked
//! there, posted to the page's key path while the 58503A's banner
//! scrolls, is pressed and traced as a `key` record; the record's `at`
//! and name, replayed as a script's `at` and `key` steps, press the
//! same key at the same cycle, so the display and the lamps go the same
//! way in both runs.  A simulation control posted to its event path
//! goes the same way, as an `event` record and an `event` step
//! ("The controls").  A line typed on the page's console reaches the
//! port, beside `--host` or with the page alone on it ("The console").
//! On the default core, so only with `mc683xx`.

#![cfg(feature = "mc683xx")]

mod common;

use std::io::BufRead;
use std::io::BufReader;
use std::io::Read;
use std::io::Write;
use std::net::TcpStream;
use std::process::Child;
use std::process::Command;
use std::process::Stdio;
use std::sync::mpsc;
use std::sync::mpsc::Receiver;
use std::thread;
use std::time::Duration;
use std::time::Instant;

use common::BOOT_LIMIT;
use common::HP58503A;
use common::Scratch;
use common::run_receiver_with_engine;
use common::script;

/// Board seconds each run lasts: past the display test the key starts
/// (58503a-3704.md, "The front panel", "When").
const SECONDS: &str = "9";
/// UTC at power-up, the same in both runs.
const START: &str = "2026-10-22T12:00:00Z";
/// The key clicked: Sat in the banner's first frames starts the display
/// test.
const KEY: &str = "sat";
/// What the display shows once the test has started.
const TEST: &str = "text=VFD\\x20DSP\\x20TEST";
/// Board seconds the controlled run lasts: past the banner the board
/// powered up again starts, three seconds after its power-up.
const CONTROLLED_SECONDS: &str = "12";
/// The controls clicked: the antenna pulled, and the power cycled.
const CONTROLS: [&str; 2] = ["antenna-off", "power-cycle"];
/// Board seconds a console run may last, past what its test waits for.
const CONSOLE_SECONDS: &str = "60";
/// A quick line on the page's console, and its answer: the error queue,
/// empty.
const QUICK: &str = ":SYSTem:ERRor?";
const QUICK_ANSWER: &str = "No error";
/// How often the test asks for the state.
const POLL: Duration = Duration::from_millis(20);

/// The answer's body to `method` `target` on the page at `address`.
fn request(address: &str, method: &str, target: &str) -> String {
    let (status, body) = exchange(address, method, target, "");
    assert_eq!(status, 200, "{body}");
    body
}

/// The answer's status and body to `method` `target` with `body` on
/// the page at `address`.
fn exchange(address: &str, method: &str, target: &str, body: &str) -> (u16, String) {
    let mut stream = TcpStream::connect(address).expect("the page takes a connection");
    // closed after the answer, which is read to its end
    let request = format!(
        "{method} {target} HTTP/1.1\r\nHost: {address}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}",
        body.len()
    );
    stream.write_all(request.as_bytes()).expect("sent");
    let mut answer = String::new();
    stream.read_to_string(&mut answer).expect("an answer");
    let (head, body) = answer.split_once("\r\n\r\n").expect("a head and a body");
    let status = head
        .split(' ')
        .nth(1)
        .and_then(|code| code.parse().ok())
        .unwrap_or_else(|| panic!("a status: {head}"));
    (status, body.to_owned())
}

/// The records of what the panel shows: the display's and the
/// indicators'.
fn shown(trace: &[String]) -> Vec<&str> {
    trace
        .iter()
        .map(String::as_str)
        .filter(|line| {
            line.starts_with("smartclock-emu: display ")
                || line.starts_with("smartclock-emu: indicator ")
        })
        .collect()
}

/// The host port served over TCP, beside the page's console.
const HOST: [&str; 2] = ["--host", "127.0.0.1:0"];

/// A paced 58503A run serving the page, `seconds` of board time: the
/// child, its standard error's lines as they come, the lines so far,
/// and the page's address.  Its host port is served over TCP.
fn serve(name: &str, seconds: &str) -> (Child, Receiver<String>, Vec<String>, String, Scratch) {
    serve_with(name, seconds, &HOST)
}

/// The same with `host`, the host port's options, its standard output
/// piped.
fn serve_with(
    name: &str,
    seconds: &str,
    host: &[&str],
) -> (Child, Receiver<String>, Vec<String>, String, Scratch) {
    let eeprom = Scratch::new(name);
    let mut child = Command::new(env!("CARGO_BIN_EXE_smartclock-emu"))
        .arg(HP58503A.image())
        .arg("--eeprom")
        .arg(&eeprom.0)
        .arg("--eeprom-dump")
        .arg(HP58503A.dump())
        .args(host)
        .args(["--panel", "127.0.0.1:0", "--indicators"])
        .args(["--gps-start", START, "--stop-after", seconds])
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("the emulator starts");
    let stderr = child.stderr.take().expect("its standard error");
    let (sender, lines) = mpsc::channel();
    thread::spawn(move || {
        for line in BufReader::new(stderr).lines().map_while(Result::ok) {
            if sender.send(line).is_err() {
                break;
            }
        }
    });
    let started = Instant::now();
    let mut trace = Vec::new();
    let address = loop {
        let line = lines
            .recv_timeout(BOOT_LIMIT.saturating_sub(started.elapsed()))
            .expect("the emulator says where the page is");
        let address = line
            .strip_prefix("smartclock-emu: panel on http://")
            .map(|rest| rest.trim_end_matches('/').to_owned());
        trace.push(line);
        if let Some(address) = address {
            break address;
        }
    };
    (child, lines, trace, address, eeprom)
}

/// Waits for the banner scrolling in on the page at `address`, its
/// first frames, whose keys pick the display test; the state then.
fn banner(address: &str) -> String {
    let started = Instant::now();
    loop {
        let state = request(address, "GET", "/state");
        let text = state
            .split_once("\"text\":\"")
            .and_then(|(_, rest)| rest.split_once('"'))
            .map_or("", |(text, _)| text);
        if text.starts_with(' ') && text.contains('H') {
            break state;
        }
        assert!(started.elapsed() < BOOT_LIMIT, "no banner: {state}");
        thread::sleep(POLL);
    }
}

/// `record`'s field `name`.
fn field<'a>(record: &'a str, name: &str) -> &'a str {
    record
        .split(' ')
        .find_map(|field| field.strip_prefix(name)?.strip_prefix('='))
        .unwrap_or_else(|| panic!("{name} in {record}"))
}

/// The records of `kind` in `trace`, each replayed as a script's `at`
/// and `step` steps.
fn replayed(trace: &[String], kind: &str, step: &str) -> String {
    let prefix = format!("smartclock-emu: {kind} ");
    trace
        .iter()
        .filter_map(|line| line.strip_prefix(&prefix))
        .map(|record| {
            format!(
                "at {}\n{step} {}\n",
                field(record, "at"),
                field(record, "name")
            )
        })
        .collect()
}

/// What a deterministic run of `steps` shows, `seconds` of board time.
fn replay(name: &str, steps: &str, seconds: &str) -> Vec<String> {
    let replay = script(&format!("{name}-replay"), steps);
    let eeprom = Scratch::new(&format!("{name}-replayed"));
    let (_, replayed) = run_receiver_with_engine(
        HP58503A,
        &eeprom.0,
        &[
            "--script",
            replay.0.to_str().expect("a path"),
            "--indicators",
            "--gps-start",
            START,
            "--stop-after",
            seconds,
        ],
    );
    shown(&replayed).into_iter().map(str::to_owned).collect()
}

#[test]
fn a_clicked_key_is_pressed_and_replays_from_its_record() {
    let (mut child, lines, mut trace, address, _eeprom) = serve("panel-clicked", SECONDS);
    assert!(request(&address, "GET", "/").contains("\"panel.js\""));
    assert!(request(&address, "GET", "/panel.js").contains("(\"/state\""));
    banner(&address);
    request(&address, "POST", &format!("/key?name={KEY}"));
    let status = child.wait().expect("it exits");
    assert!(status.success(), "{status}");
    trace.extend(lines.iter());
    let clicked = shown(&trace);
    assert!(
        clicked.iter().any(|line| line.ends_with(TEST)),
        "the key started the display test: {clicked:?}"
    );
    let steps = replayed(&trace, "key", "key");
    assert_eq!(steps.lines().count(), 2, "one click: {steps}");
    assert!(steps.ends_with(&format!("key {KEY}\n")), "{steps}");
    assert_eq!(replay("panel", &steps, SECONDS), clicked);
}

#[test]
fn a_clicked_control_is_applied_and_replays_from_its_record() {
    let (mut child, lines, mut trace, address, _eeprom) =
        serve("panel-controlled", CONTROLLED_SECONDS);
    let state = banner(&address);
    assert!(
        state.contains("\"controls\":{\"names\":[\"antenna-off\",\"antenna-on\",\"skip-warm-up\",\"power-cycle\",\"reset\"],\"antenna\":true,\"warming\":true}"),
        "{state}"
    );
    for control in CONTROLS {
        request(&address, "POST", &format!("/event?name={control}"));
    }
    let status = child.wait().expect("it exits");
    assert!(status.success(), "{status}");
    trace.extend(lines.iter());
    let controlled = shown(&trace);
    let power_ons = trace
        .iter()
        .filter(|line| line.as_str() == "smartclock-emu: reset cycle=0 cause=PowerOn")
        .count();
    assert_eq!(power_ons, 2, "the power cycle's power-up");
    let steps = replayed(&trace, "event", "event");
    assert_eq!(
        steps
            .lines()
            .filter(|line| line.starts_with("event "))
            .collect::<Vec<_>>(),
        CONTROLS.map(|control| format!("event {control}")),
        "{steps}"
    );
    assert_eq!(
        replay("panel-controls", &steps, CONTROLLED_SECONDS),
        controlled
    );
}

/// The console's state for the page `client`, read from the start.
fn console(address: &str, client: &str) -> String {
    request(address, "GET", &format!("/console?since=0&client={client}"))
}

/// Waits until the console's state for `client` has `wanted`.
fn console_until(address: &str, client: &str, wanted: &str) -> String {
    let started = Instant::now();
    loop {
        let state = console(address, client);
        if state.contains(wanted) {
            break state;
        }
        assert!(started.elapsed() < BOOT_LIMIT, "no {wanted}: {state}");
        thread::sleep(POLL);
    }
}

/// The host port's address, as the run says it on standard error.
fn host_port(trace: &[String]) -> String {
    trace
        .iter()
        .find_map(|line| line.strip_prefix("smartclock-emu: host port on tcp://"))
        .expect("the host port's address")
        .to_owned()
}

#[test]
fn a_line_typed_on_the_console_reaches_the_firmware_and_its_answer_comes_back() {
    let (mut child, lines, mut trace, address, _eeprom) = serve("console-typed", CONSOLE_SECONDS);
    // the prompt, a line's answer, before the line is typed
    console_until(&address, "a", "\"writable\":true");
    console_until(&address, "a", "scpi >");
    let (status, body) = exchange(&address, "POST", "/console?client=a", "*IDN?");
    assert_eq!(status, 200, "{body}");
    console_until(&address, "a", "HEWLETT-PACKARD,58503A");
    // the lease is the first page's
    let (status, body) = exchange(&address, "POST", "/console?client=b", "*IDN?");
    assert_eq!(status, 409, "{body}");
    assert!(body.contains("another page"), "{body}");
    child.kill().expect("stopped");
    let _ = child.wait();
    trace.extend(lines.try_iter());
    assert!(
        trace
            .iter()
            .any(|line| line.starts_with("smartclock-emu: console cycle=")
                && line.ends_with(" line=*IDN?")),
        "the line's record"
    );
}

#[test]
fn with_the_panel_alone_the_page_has_the_host_port_and_the_terminal_none_of_it() {
    let (mut child, lines, mut trace, address, _eeprom) =
        serve_with("console-alone", CONSOLE_SECONDS, &[]);
    console_until(&address, "a", "\"writable\":true");
    console_until(&address, "a", "scpi >");
    let (status, body) = exchange(&address, "POST", "/console?client=a", "*IDN?");
    assert_eq!(status, 200, "{body}");
    console_until(&address, "a", "HEWLETT-PACKARD,58503A");
    // a quick line, as its button posts it
    assert!(request(&address, "GET", "/constants.json").contains(&format!("\"line\":\"{QUICK}\"")));
    let (status, body) = exchange(&address, "POST", "/console?client=a", QUICK);
    assert_eq!(status, 200, "{body}");
    console_until(&address, "a", QUICK_ANSWER);
    child.kill().expect("stopped");
    let _ = child.wait();
    let mut stdout = Vec::new();
    child
        .stdout
        .take()
        .expect("its standard output")
        .read_to_end(&mut stdout)
        .expect("read");
    assert!(stdout.is_empty(), "the port's bytes on standard output");
    trace.extend(lines.try_iter());
    assert!(
        trace
            .iter()
            .any(|line| line == "smartclock-emu: host port on the panel page's console"),
        "{trace:?}"
    );
    for typed in ["*IDN?", QUICK] {
        assert!(
            trace
                .iter()
                .any(|line| line.starts_with("smartclock-emu: console cycle=")
                    && line.ends_with(&format!(" line={typed}"))),
            "{typed}'s record"
        );
    }
}

#[test]
fn a_tcp_client_on_the_host_port_makes_the_console_read_only() {
    let (mut child, _lines, trace, address, _eeprom) = serve("console-tcp", CONSOLE_SECONDS);
    console_until(&address, "a", "\"writable\":true");
    let client = TcpStream::connect(host_port(&trace)).expect("the host port takes a client");
    let state = console_until(&address, "a", "\"typist\":\"tcp\"");
    assert!(state.contains("\"writable\":false"), "{state}");
    let (status, body) = exchange(&address, "POST", "/console?client=a", "*IDN?");
    assert_eq!(status, 409, "{body}");
    assert!(body.contains("TCP client"), "{body}");
    drop(client);
    console_until(&address, "b", "\"writable\":true");
    child.kill().expect("stopped");
    let _ = child.wait();
}
