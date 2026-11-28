use std::collections::HashMap;
use std::io::Read;
use std::io::Write;
use std::time::Duration;

use clap::Parser;
use smartclock::session::Config;
use smartclock::transport::Transport;

use super::CODE;
use super::Line;
use super::PassthroughArgs;
use super::Plan;
use super::RelayFraming;
use super::Setting;
use super::compare;
use super::definition;
use super::dry_run_text;
use super::prompt_alone;
use super::run;
use crate::select::EEPROM_MODEL;
use crate::select::EEPROM_SERIAL;
use crate::select::Serial;

/// The longest definition line the relay was proven with.
const LINE_LIMIT: usize = 110;

/// The command line `passthrough` takes, alone.
#[derive(Debug, Parser)]
struct Cli {
    #[arg(long)]
    serial: Option<String>,
    #[command(flatten)]
    args: PassthroughArgs,
}

fn parse(arguments: &[&str]) -> Result<PassthroughArgs, clap::Error> {
    Cli::try_parse_from([&["passthrough"][..], arguments].concat()).map(|cli| cli.args)
}

fn plan(arguments: &[&str]) -> Plan {
    Plan::new(&parse(arguments).expect("arguments")).expect("plan")
}

#[test]
fn a_setting_is_the_duarts_mode_and_clock_and_the_scis_framing_and_divisor() {
    // relay.s's header: 9600 8N1 = 0x13bb0037, 19200 8E1 = 0x03cc061b.
    // MR1 0x13 is eight bits and no parity, 0x03 eight bits and even
    // parity; CSR 0xbb and 0xcc are 9600 and 19200 in set 2; SCCR1's
    // 0x06 is PE and M; SCBR 55 and 27 are 16777216 / 32 / the rate.
    let cell = |baud, framing| Setting::new(baud, framing).expect("a rate").cell;
    assert_eq!(cell(9600, RelayFraming::EightNone), 0x13bb_0037);
    assert_eq!(cell(19200, RelayFraming::EightEven), 0x03cc_061b);
}

#[test]
fn a_rate_the_duart_or_the_sci_would_miss_is_refused() {
    for baud in [2400, 4800, 9600, 19200] {
        assert!(
            Setting::new(baud, RelayFraming::EightNone).is_ok(),
            "{baud}"
        );
    }
    // 38400 is not in set 2; 1200's divisor does not fit a byte.
    for baud in [38400, 57600, 300, 1200, 9601] {
        assert!(
            Setting::new(baud, RelayFraming::EightNone).is_err(),
            "{baud}"
        );
    }
}

#[test]
fn the_bootloader_shorthand_is_its_two_settings_and_excludes_the_explicit_four() {
    assert_eq!(
        plan(&["--oncore-emu-bootloader"]).call,
        "hex 13BB0037 3CC061B relay"
    );
    assert_eq!(
        plan(&["--relay-baud", "9600", "--then-baud", "19200"]).call,
        "hex 13BB0037 3CC061B relay"
    );
    assert_eq!(plan(&[]).call, "hex 13BB0037 0 relay");
    for explicit in [
        &["--relay-baud", "4800"][..],
        &["--relay-framing", "8E1"],
        &["--then-baud", "19200"],
        &["--then-framing", "8N1", "--then-baud", "4800"],
    ] {
        assert!(
            parse(&[&["--oncore-emu-bootloader"][..], explicit].concat()).is_err(),
            "{explicit:?}"
        );
    }
    assert!(parse(&["--then-framing", "8N1"]).is_err());
    assert!(parse(&["--start"]).is_err(), "--start needs --serial");
}

#[test]
fn the_definition_compiles_the_code_and_allots_the_rings() {
    let lines = definition(CODE);
    assert_eq!(lines.first().map(String::as_str), Some("hex : relay ["));
    assert_eq!(lines.last().map(String::as_str), Some("40 allot ] ;"));
    assert!(lines.iter().all(|line| line.len() <= LINE_LIMIT));
    let compiled: Vec<u8> = lines[1..lines.len() - 1]
        .iter()
        .flat_map(|line| line.split(" ,"))
        .map(str::trim)
        .filter(|long| !long.is_empty())
        .flat_map(|long| {
            u32::from_str_radix(long, 16)
                .expect("a hex long")
                .to_be_bytes()
        })
        .collect();
    assert_eq!(compiled, CODE);
    assert_eq!(lines[1].split(" , ").next(), Some("2c1e2a1e"));
}

#[test]
fn a_definition_line_is_answered_by_the_prompt_alone() {
    assert!(prompt_alone("40 allot ] ;", "").is_ok());
    assert!(prompt_alone("40 allot ] ;", "40 allot ] ;\r\n").is_ok());
    assert!(prompt_alone("40 allot ] ;", "40 allot ] ; ?\r\n").is_err());
    assert!(prompt_alone("hex : relay [", "relay ?\r\n").is_err());
}

#[test]
fn a_read_back_must_be_the_code_to_its_last_byte() {
    assert!(compare(0x10_baf4, CODE).is_ok());
    let mut flipped = CODE.to_vec();
    flipped[0xd8] ^= 0x10;
    let why = compare(0x10_baf4, &flipped)
        .expect_err("differs")
        .to_string();
    assert!(why.contains("0x10bbcc"), "{why}");
    assert!(compare(0x10_baf4, &CODE[..CODE.len() - 4]).is_err());
}

#[test]
fn the_dry_run_names_every_line_it_would_send() {
    let text = dry_run_text(&plan(&["--oncore-emu-bootloader"]));
    for line in definition(CODE) {
        assert!(text.contains(&line), "{line}");
    }
    for part in [
        "hex 13BB0037 3CC061B relay",
        "1 disable_gps_cmds",
        "9600 8N1",
        "19200 8E1",
        "0x400012",
        "0x400024",
    ] {
        assert!(text.contains(part), "{part}: {text}");
    }
}

/// The dictionary's first RAM entry, and the pointer and latest entry
/// before the relay is defined, as a Z3801A's console had them.
const FIRST_ENTRY: u32 = 0x10_a72e;
const HERE: u32 = 0x10_b22a;
const LATEST: u32 = 0x10_b210;
/// Where the fake compiles the relay's code.
const CODE_AT: u32 = 0x10_b23c;

/// A unit's console, as far as a run uses it, then its relay.
struct Fake {
    /// The EEPROM's identity record, by address.
    eeprom: HashMap<u32, u8>,
    /// The dictionary's links.
    links: HashMap<u32, u32>,
    /// Whether the port is at SCPI rather than the console.
    at_scpi: bool,
    /// The code compiled so far.
    compiled: Vec<u8>,
    /// How many more definitions compile a wrong first long.
    corrupt: usize,
    /// Whether the relay answers its call with `!`.
    answers: bool,
    /// Whether moving the port to the relay's setting fails.
    line_fails: bool,
    pending: Vec<u8>,
    reply: Vec<u8>,
    sent: Vec<String>,
    lines: Vec<String>,
    settings: Vec<Setting>,
    breaks: usize,
}

impl Fake {
    fn new(model: &str, serial: &str) -> Self {
        let mut eeprom = HashMap::new();
        for ((at, _), text) in [(EEPROM_MODEL, model), (EEPROM_SERIAL, serial)] {
            for (n, byte) in text.bytes().enumerate() {
                eeprom.insert(at + n as u32, byte);
            }
        }
        Self {
            eeprom,
            links: [(LATEST, LATEST - 0x20), (LATEST - 0x20, FIRST_ENTRY)]
                .into_iter()
                .chain([(FIRST_ENTRY, 0x1_ae72)])
                .collect(),
            at_scpi: false,
            compiled: Vec::new(),
            corrupt: 0,
            answers: true,
            line_fails: false,
            pending: Vec::new(),
            reply: Vec::new(),
            sent: Vec::new(),
            lines: Vec::new(),
            settings: Vec::new(),
            breaks: 0,
        }
    }

    fn scpi(&mut self, line: &str) {
        let answer = match line {
            "*IDN?" => "HEWLETT-PACKARD,Z3801A,3542A01548,3543\r\n",
            ":SYSTem:LANGuage?" => "\"PRIMARY\"\r\n",
            ":SYSTem:LANGuage \"PFORTH\"" => {
                self.at_scpi = false;
                self.compiled.clear();
                self.reply.extend_from_slice(b"\r\np4th D > ");
                return;
            }
            _ => "",
        };
        self.reply
            .extend_from_slice(format!("{line}\r\n{answer}scpi > ").as_bytes());
    }

    fn console(&mut self, line: &str) {
        let number = |text: &str| u32::from_str_radix(text, 16).expect("hex");
        let value = match line.split(' ').collect::<Vec<_>>().as_slice() {
            ["halt"] => {
                self.at_scpi = true;
                self.reply.extend_from_slice(b"\r\nscpi > ");
                return;
            }
            ["hex", "here", "@", "u."] => Some(HERE),
            ["here", "4", "-", "@", "u."] | ["hex", "here", "4", "-", "@", "u."] => {
                Some(if self.compiled.is_empty() {
                    LATEST
                } else {
                    HERE
                })
            }
            [entry, "4", "+", "@", "u."] if number(entry) == HERE => Some(CODE_AT),
            [address, "c@", "u."] => Some(u32::from(
                self.eeprom.get(&number(address)).copied().unwrap_or(0),
            )),
            [address, "@", "u."] => Some(self.links.get(&number(address)).copied().unwrap_or(0)),
            [address, length, "rd"] => {
                let from = (number(address) - CODE_AT) as usize;
                let to = from + number(length) as usize;
                for long in self.compiled[from..to].chunks(4) {
                    let word = u32::from_be_bytes([long[0], long[1], long[2], long[3]]);
                    self.reply
                        .extend_from_slice(format!(" {word:X}").as_bytes());
                }
                None
            }
            ["hex", _, _, "relay"] => {
                self.reply.extend_from_slice(b"garbled at the old setting");
                return;
            }
            _ if line.ends_with(" ,") => {
                for long in line.split(" ,").map(str::trim).filter(|l| !l.is_empty()) {
                    let word = number(long);
                    let word = if self.compiled.is_empty() && self.corrupt > 0 {
                        self.corrupt -= 1;
                        word ^ 0x1000_0000
                    } else {
                        word
                    };
                    self.compiled.extend_from_slice(&word.to_be_bytes());
                }
                None
            }
            _ => None,
        };
        if let Some(value) = value {
            self.reply
                .extend_from_slice(format!(" {value:X}").as_bytes());
        }
        self.reply.extend_from_slice(b"p4th X > ");
    }
}

impl Write for Fake {
    fn write(&mut self, bytes: &[u8]) -> std::io::Result<usize> {
        self.pending.extend_from_slice(bytes);
        while let Some(end) = self.pending.iter().position(|&b| b == b'\r') {
            let line = String::from_utf8_lossy(&self.pending[..end])
                .trim_start_matches('\n')
                .to_owned();
            self.pending.drain(..=end);
            self.sent.push(line.clone());
            if self.at_scpi {
                self.scpi(&line);
            } else {
                self.lines.push(line.clone());
                self.console(&line);
            }
        }
        Ok(bytes.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

impl Read for Fake {
    fn read(&mut self, out: &mut [u8]) -> std::io::Result<usize> {
        let n = out.len().min(self.reply.len());
        out[..n].copy_from_slice(&self.reply[..n]);
        self.reply.drain(..n);
        Ok(n)
    }
}

impl Transport for Fake {
    fn describe(&self) -> String {
        "fake unit".to_owned()
    }
}

impl Line for Fake {
    fn set_line(&mut self, setting: Setting) -> smartclock::error::Result<()> {
        if self.line_fails {
            return Err(std::io::Error::other("the adapter went away").into());
        }
        self.settings.push(setting);
        self.reply.clear();
        if self.answers {
            self.reply.extend_from_slice(b"!");
        }
        Ok(())
    }

    fn send_break(&mut self, _: Duration) -> smartclock::error::Result<()> {
        self.breaks += 1;
        Ok(())
    }
}

fn quick() -> Config {
    Config {
        timeout: Duration::from_millis(200),
        idle: Duration::from_millis(1),
        ..Config::default()
    }
}

fn serial(text: &str) -> Serial {
    Serial::parse(text).expect("serial")
}

#[test]
fn a_run_defines_reads_back_calls_and_latches() {
    let mut unit = Fake::new("Z3801A", "3542A01548");
    let args = parse(&["--oncore-emu-bootloader"]).expect("arguments");
    run(&mut unit, &args, &serial("3542A01548"), quick()).expect("started");
    assert_eq!(unit.compiled, CODE);
    assert_eq!(
        unit.sent.last().map(String::as_str),
        Some("hex 13BB0037 3CC061B relay")
    );
    assert_eq!(
        unit.pending,
        [0],
        "the latch, after the call's carriage return"
    );
    assert_eq!(unit.settings.len(), 1);
    assert_eq!(unit.settings[0].to_string(), "9600 8N1");
    assert!(unit.sent.iter().any(|line| line == "1 disable_gps_cmds"));
    assert!(!unit.sent.iter().any(|line| line == "halt"));
    assert_eq!(unit.breaks, 0);
}

#[test]
fn another_serial_or_an_unproven_model_is_returned_to_scpi_with_nothing_defined() {
    for (model, found, asked) in [
        ("Z3801A", "3542A01548", "3542A01549"),
        ("58503B", "L00000", "L00000"),
        ("Z3815A", "0000A00000", "0000A00000"),
        ("Z3817A", "3832A03064", "3832A03064"),
        ("", "0000A00000", "0000A00000"),
    ] {
        let mut unit = Fake::new(model, found);
        let args = parse(&[]).expect("arguments");
        assert!(run(&mut unit, &args, &serial(asked), quick()).is_err());
        assert!(unit.compiled.is_empty(), "{model}");
        assert!(unit.sent.iter().any(|line| line == "halt"), "{model}");
        assert!(!unit.sent.iter().any(|line| line.ends_with("relay")));
    }
}

#[test]
fn each_proven_model_is_started() {
    for model in super::PROVEN {
        let mut unit = Fake::new(model, "3542A01548");
        let args = parse(&[]).expect("arguments");
        run(&mut unit, &args, &serial("3542A01548"), quick()).expect(model);
        assert_eq!(unit.compiled, CODE, "{model}");
    }
}

#[test]
fn a_read_back_that_differs_is_defined_once_more_after_halt() {
    let mut unit = Fake::new("Z3801A", "3542A01548");
    unit.corrupt = 1;
    let args = parse(&[]).expect("arguments");
    run(&mut unit, &args, &serial("3542A01548"), quick()).expect("started");
    assert_eq!(unit.compiled, CODE);
    let halt = unit
        .sent
        .iter()
        .position(|line| line == "halt")
        .expect("halt");
    assert!(unit.sent[..halt].contains(&"0 disable_gps_cmds".to_owned()));
    assert_eq!(
        unit.sent.last().map(String::as_str),
        Some("hex 13BB0037 0 relay")
    );
}

#[test]
fn a_second_difference_stops_the_run_back_at_scpi() {
    let mut unit = Fake::new("Z3801A", "3542A01548");
    unit.corrupt = 2;
    let args = parse(&[]).expect("arguments");
    assert!(run(&mut unit, &args, &serial("3542A01548"), quick()).is_err());
    assert!(unit.at_scpi);
    assert!(!unit.sent.iter().any(|line| line.ends_with("relay")));
}

#[test]
fn a_relay_that_does_not_answer_is_ended_by_a_break_for_each_setting() {
    let mut unit = Fake::new("Z3801A", "3542A01548");
    unit.answers = false;
    let args = parse(&["--oncore-emu-bootloader"]).expect("arguments");
    assert!(run(&mut unit, &args, &serial("3542A01548"), quick()).is_err());
    assert_eq!(unit.breaks, 2);
    assert!(unit.pending.is_empty(), "no latch");
}

#[test]
fn a_failure_after_the_call_still_ends_the_relay_with_its_breaks() {
    let mut unit = Fake::new("Z3801A", "3542A01548");
    unit.line_fails = true;
    let args = parse(&[]).expect("arguments");
    let why = run(&mut unit, &args, &serial("3542A01548"), quick())
        .expect_err("failed")
        .to_string();
    assert!(why.contains("the adapter went away"), "{why}");
    assert_eq!(unit.breaks, 1);
    assert!(unit.pending.is_empty(), "no latch");
}
