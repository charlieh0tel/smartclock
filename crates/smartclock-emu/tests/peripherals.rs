//! The peripheral checks (docs/emulator.md, "The peripheral checks"):
//! each exercises one of the board's peripherals through the receiver's
//! own firmware, from a script at fixed board times under
//! `--deterministic` with the synthetic engine from a fixed start, on
//! both cores, Unicorn, the oracle, and mc683xx, and wants the same
//! values of both.  Times may differ: mc683xx charges the CPU32's
//! instruction timings.  `tools/peripheral_check.py` runs the same
//! checks and more and prints them as a table.  Needs both cores, so
//! only with `unicorn` and `mc683xx`.

#![cfg(all(feature = "unicorn", feature = "mc683xx"))]

mod common;

use std::ffi::OsString;
use std::path::Path;
use std::process::Command;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;

use common::HP58503A;
use common::Receiver;
use common::Scratch;
use common::Z3801A;
use common::Z3805A;
use common::reset_causes;

/// The cores, the oracle first.
const CORES: [&str; 2] = ["unicorn", "mc683xx"];

/// The synthetic engine's power-up UTC, fixed so that runs repeat.
const GPS_START: &str = "2026-10-22T12:00:00Z";

/// A prompt: `scpi > `, `E-113> `, `p4th D > `.
const PROMPT: &str = ">\\x20";

/// How long an answer may take, board seconds: an NV save takes about
/// 12.
const ANSWER_WITHIN: u32 = 30;

/// A run's output: the host port's transcript, standard error's lines
/// and the SCI's bytes.
struct Run {
    /// What the host port sent.
    transcript: String,
    /// Standard error, a line at a time.
    errors: Vec<String>,
    /// What the SCI sent, raw (`--sci-capture`).
    sci: Vec<u8>,
    /// Whether the host port echoed each line sent.
    echo: bool,
    /// The lines the script sent, in order.
    sent: Vec<String>,
}

/// The host port's prompts: `scpi > `, the console's, and an error's,
/// `E-` and its code, then `> `.
const PROMPTS: [&str; 2] = ["scpi > ", "p4th D > "];

/// The texts between `transcript`'s prompts, each after one: the first
/// after the first prompt.
fn between_prompts(transcript: &str) -> Vec<&str> {
    let mut texts = Vec::new();
    let mut start = None;
    let mut at = 0;
    while at < transcript.len() {
        let rest = &transcript[at..];
        let error = rest
            .strip_prefix("E-")
            .and_then(|code| {
                code.find("> ")
                    .filter(|end| *end > 0)
                    .map(|end| (code, end))
            })
            .filter(|(code, end)| code[..*end].bytes().all(|byte| byte.is_ascii_digit()))
            .map(|(_, end)| 2 + end + 2);
        let length = PROMPTS
            .iter()
            .find(|prompt| rest.starts_with(**prompt))
            .map(|prompt| prompt.len())
            .or(error);
        match length {
            Some(length) => {
                if let Some(start) = start {
                    texts.push(&transcript[start..at]);
                }
                at += length;
                start = Some(at);
            }
            None => at += rest.chars().next().map_or(1, char::len_utf8),
        }
    }
    if let Some(start) = start {
        texts.push(&transcript[start..]);
    }
    texts
}

impl Run {
    /// Every answer to `command`: the text between its echo and the
    /// next prompt, trimmed.  A port that does not echo answers each
    /// line sent, after the last's prompt, with its text and the next
    /// prompt: the sends and the prompts after the power-up's paired in
    /// order.
    fn answers(&self, command: &str) -> Vec<String> {
        if !self.echo {
            return self
                .sent
                .iter()
                .zip(between_prompts(&self.transcript))
                .filter(|(sent, _)| *sent == command)
                .map(|(_, answer)| answer.trim().to_owned())
                .collect();
        }
        let mut found = Vec::new();
        let mut rest = self.transcript.as_str();
        while let Some(at) = rest.find(&format!("{command}\r\n")) {
            rest = &rest[at + command.len() + 2..];
            let end = ["scpi > ", "p4th D > ", "> "]
                .iter()
                .filter_map(|prompt| rest.find(prompt))
                .min()
                .unwrap_or(rest.len());
            let answer = &rest[..end];
            // an error's prompt, `E-230> `, ends with the `> ` found
            let answer = answer
                .rsplit_once("E-")
                .filter(|(text, code)| {
                    (text.is_empty() || text.ends_with('\n'))
                        && code.bytes().all(|byte| byte.is_ascii_digit())
                })
                .map_or(answer, |(text, _)| text);
            found.push(answer.trim().to_owned());
        }
        found
    }

    /// The first answer to `command`.
    fn answer(&self, command: &str) -> String {
        self.answers(command)
            .into_iter()
            .next()
            .unwrap_or_else(|| panic!("no answer to {command}: {}", self.transcript))
    }

    /// The lines of standard error that start with `prefix`, the rest
    /// of each.
    fn records<'a>(&'a self, prefix: &'a str) -> impl Iterator<Item = &'a str> {
        self.errors
            .iter()
            .filter_map(move |line| line.strip_prefix(prefix))
    }
}

/// A script: the boot's prompt awaited, then `steps`, each `send`
/// followed by its prompt, then the stop.
fn script(steps: &[&str]) -> String {
    let mut text = format!("expect scpi {PROMPT} within 10\n");
    for step in steps {
        text.push_str(step);
        text.push('\n');
        if step.starts_with("send ") {
            text.push_str(&format!("expect {PROMPT} within {ANSWER_WITHIN}\n"));
        }
    }
    text + "stop\n"
}

/// `--sci` on a port the system picks and `--sci-capture` into
/// `capture`, where `receiver`'s SCI is served on `core`.
fn sci_options(receiver: Receiver, core: &str, capture: &Path) -> Vec<OsString> {
    if receiver.serves_sci(core) {
        ["--sci", "127.0.0.1:0", "--sci-capture"]
            .map(OsString::from)
            .into_iter()
            .chain([capture.as_os_str().to_owned()])
            .collect()
    } else {
        Vec::new()
    }
}

/// `script` run on `receiver` and `core` with the synthetic engine, its
/// EEPROM's working file `eeprom`, kept if it is there, for at most
/// `limit` board seconds, with `options`.
fn run(
    receiver: Receiver,
    core: &str,
    eeprom: &Path,
    script: &str,
    limit: u32,
    options: &[&str],
) -> Run {
    let path = Scratch::new(&format!("peripherals-script-{core}-{}", unique()));
    std::fs::write(&path.0, script).expect("the script written");
    let capture = Scratch::new(&format!("peripherals-sci-{core}-{}", unique()));
    let output = Command::new(env!("CARGO_BIN_EXE_smartclock-emu"))
        .arg(receiver.image())
        .args(["--core", core, "--eeprom"])
        .arg(eeprom)
        .arg("--eeprom-dump")
        .arg(receiver.dump())
        .args(["--deterministic", "--gps-start", GPS_START])
        .args(sci_options(receiver, core, &capture.0))
        .arg("--script")
        .arg(&path.0)
        .args(["--stop-after", &limit.to_string()])
        .args(options)
        .output()
        .expect("the emulator runs");
    let errors: Vec<String> = String::from_utf8_lossy(&output.stderr)
        .lines()
        .map(str::to_owned)
        .collect();
    assert!(
        output.status.success(),
        "{core}: {}: {:?}",
        output.status,
        errors.last()
    );
    Run {
        transcript: String::from_utf8_lossy(&output.stdout).into_owned(),
        errors,
        // none where the SCI is not served, a board file without an
        // `sci` endpoint, the 58503A's and the Z3801A's
        sci: std::fs::read(&capture.0).unwrap_or_default(),
        echo: receiver.echoes(),
        sent: script
            .lines()
            .filter_map(|line| line.strip_prefix("send "))
            .map(str::to_owned)
            .collect(),
    }
}

/// A number no other run of this process has had, for scratch files.
fn unique() -> usize {
    static NEXT: AtomicUsize = AtomicUsize::new(0);
    NEXT.fetch_add(1, Ordering::Relaxed)
}

/// `script` on both cores, each with a fresh EEPROM from the dump.
fn both(receiver: Receiver, script: &str, limit: u32, options: &[&str]) -> [Run; 2] {
    CORES.map(|core| {
        let eeprom = Scratch::new(&format!("peripherals-eeprom-{core}-{}", unique()));
        run(receiver, core, &eeprom.0, script, limit, options)
    })
}

/// The interpolator's calibration record and the hex DAC's shadow, in
/// RAM (each image's names.tsv: `interpolator_calibration_state`,
/// `hdac_shadow`).
fn calibration_addresses(receiver: Receiver) -> (u32, u32) {
    let image = receiver.image();
    if image.ends_with("z3805a-3543b.bin") {
        (0x101fce, 0x10314a)
    } else if image.ends_with("z3801a-3543.bin") {
        (0x101fc6, 0x10311c)
    } else {
        (0x1022e0, 0x103470)
    }
}

/// The host port and the pForth console, by its documented route in
/// and `halt` out (console.md), and through the console the hex DAC's
/// trims, the EFC and the ADC's averages.
fn host_port_and_console(receiver: Receiver) {
    let (calibration, hdac) = calibration_addresses(receiver);
    let rb = format!("send {calibration} 9 rb");
    let shadow = format!("send {hdac} 6 rb");
    let text = script(&[
        "at 30",
        "send *IDN?",
        "send :SYSTem:ERRor?",
        "send :DIAGnostic:TEST:RESult?",
        "send :DIAGnostic:ROSCillator:EFControl:ABSolute?",
        "send :SYSTem:LANGuage \"PFORTH\"",
        "send 1 2 + .",
        "send : rb over + swap do i c@ u. loop ;",
        &rb,
        &shadow,
        "send pr_efc",
        "send halt",
        "send :SYSTem:LANGuage?",
    ]);
    let [unicorn, mc683xx] = both(receiver, &text, 90, &[]);
    for command in [
        "*IDN?",
        ":SYSTem:ERRor?",
        ":DIAGnostic:TEST:RESult?",
        ":DIAGnostic:ROSCillator:EFControl:ABSolute?",
        "1 2 + .",
        &rb[5..],
        &shadow[5..],
        "pr_efc",
        ":SYSTem:LANGuage?",
    ] {
        assert_eq!(
            unicorn.answer(command),
            mc683xx.answer(command),
            "{command}"
        );
    }
    assert!(unicorn.answer("*IDN?").starts_with("HEWLETT-PACKARD,"));
    assert_eq!(unicorn.answer(":SYSTem:ERRor?"), "+0,\"No error\"");
    // every power-up test passes with the engine (docs/emulator.md,
    // "What the boot shows"): the FPGA's counter and interpolator too
    assert!(unicorn.answer(":DIAGnostic:TEST:RESult?").starts_with("+0"));
    assert_eq!(unicorn.answer("1 2 + ."), "3");
    assert_eq!(unicorn.answer(":SYSTem:LANGuage?"), "\"PRIMARY\"");
}

#[test]
fn the_z3805a_host_port_and_console_agree() {
    host_port_and_console(Z3805A);
}

#[test]
fn the_58503a_host_port_and_console_agree() {
    host_port_and_console(HP58503A);
}

#[test]
fn the_z3801a_host_port_and_console_agree() {
    host_port_and_console(Z3801A);
}

/// The GPS link: the engine's answers, the satellites tracked and the
/// position, the same on both cores.
fn gps_link(receiver: Receiver) {
    let text = script(&[
        "at 72",
        "send :GPS:SATellite:TRACking?",
        "send :GPS:SATellite:TRACking:COUNt?",
        "send :GPS:POSition?",
    ]);
    let [unicorn, mc683xx] = both(receiver, &text, 120, &["--tap"]);
    for command in [
        ":GPS:SATellite:TRACking?",
        ":GPS:SATellite:TRACking:COUNt?",
        ":GPS:POSition?",
    ] {
        assert_eq!(
            unicorn.answer(command),
            mc683xx.answer(command),
            "{command}"
        );
    }
    // the sky's six above the mask (docs/emulator.md, "The synthetic
    // GPS engine")
    assert_eq!(unicorn.answer(":GPS:SATellite:TRACking:COUNt?"), "+6");
    // each way, `<` the firmware's commands and `>` the engine's
    // answers, each message's `@@Xx` in order of first appearance
    for direction in ["<", ">"] {
        let prefix = format!("smartclock-emu: gps {direction} ");
        let messages = |run: &Run| {
            let mut seen: Vec<String> = Vec::new();
            for message in run.records(&prefix) {
                let name: String = message.chars().take(4).collect();
                if name.starts_with("@@") && !seen.contains(&name) {
                    seen.push(name);
                }
            }
            seen
        };
        let seen = messages(&unicorn);
        assert!(
            seen.iter().any(|name| name == "@@Ba"),
            "{direction} {seen:?}"
        );
        assert_eq!(seen, messages(&mc683xx), "{direction}");
    }
}

#[test]
fn the_z3805a_gps_link_agrees() {
    gps_link(Z3805A);
}

#[test]
fn the_58503a_gps_link_agrees() {
    gps_link(HP58503A);
}

#[test]
fn the_z3801a_gps_link_agrees() {
    gps_link(Z3801A);
}

/// The ES message's length as digits, one a byte, its CR included
/// (rewritten/z3805a-3543b/timecode.c, `es_build`, `ES_DIGITS_LENGTH`).
const ES_LENGTH: usize = 16;

/// The ES message's last byte.
const ES_END: u8 = b'\r';

/// The Z3805A's SCI, its ES messages from the raw capture, decoded:
/// (seconds of the day, offset, status, summary).
fn es_messages(run: &Run) -> Vec<(u32, u8, u8, u8)> {
    let (messages, rest) = run.sci.as_chunks::<ES_LENGTH>();
    assert!(rest.len() < ES_LENGTH);
    messages
        .iter()
        .map(|d| {
            assert!(
                d[..13].iter().all(|&digit| digit <= 9) && d[15] == ES_END,
                "an ES message: {d:02x?}"
            );
            let pair = |at: usize| u32::from(d[at]) * 10 + u32::from(d[at + 1]);
            (
                pair(5) * 3600 + pair(7) * 60 + pair(9),
                d[11] * 10 + d[12],
                d[13],
                d[14],
            )
        })
        .collect()
}

#[test]
fn the_z3805a_sends_its_es_message_every_other_second_on_both_cores() {
    let [unicorn, mc683xx] = both(Z3805A, &script(&["at 60"]), 90, &[]);
    let [unicorn, mc683xx] = [&unicorn, &mc683xx].map(es_messages);
    for messages in [&unicorn, &mc683xx] {
        assert!(messages.len() > 20, "{messages:?}");
        // on even seconds (timecode.c, timecode_update)
        assert!(
            messages.windows(2).all(|pair| pair[1].0 == pair[0].0 + 2),
            "{messages:?}"
        );
    }
    let fields = |messages: &[(u32, u8, u8, u8)]| {
        let mut fields: Vec<_> = messages.iter().map(|m| (m.1, m.2, m.3)).collect();
        fields.dedup();
        fields
    };
    assert_eq!(fields(&unicorn), fields(&mc683xx));
}

/// A safe, documented setting the NV register keeps: the antenna delay
/// (097-59551-02 5-17).
const ANTENNA_DELAY: &str = "150E-9";

/// The antenna delay set, then read on a second boot from the same
/// working file.
fn eeprom_persists(receiver: Receiver) {
    let set = format!("send :GPS:REFerence:ADELay {ANTENNA_DELAY}");
    let first = script(&["at 10", &set, "send :GPS:REFerence:ADELay?", "at 40"]);
    let second = script(&["at 10", "send :GPS:REFerence:ADELay?"]);
    let read: Vec<String> = CORES
        .iter()
        .map(|core| {
            let eeprom = Scratch::new(&format!("peripherals-persist-{core}-{}", unique()));
            run(receiver, core, &eeprom.0, &first, 60, &[]);
            run(receiver, core, &eeprom.0, &second, 40, &[]).answer(":GPS:REFerence:ADELay?")
        })
        .collect();
    assert_eq!(read[0], read[1]);
    assert_eq!(read[0], "+1.50000E-007");
}

#[test]
fn a_z3805a_setting_survives_a_reboot_on_both_cores() {
    eeprom_persists(Z3805A);
}

#[test]
fn a_58503a_setting_survives_a_reboot_on_both_cores() {
    eeprom_persists(HP58503A);
}

#[test]
fn a_z3801a_setting_survives_a_reboot_on_both_cores() {
    eeprom_persists(Z3801A);
}

/// `4 crash` in the console ends in the watchdog's reset (restart.md,
/// "The watchdog"); the board boots again and answers.
fn watchdog_reboots(receiver: Receiver) {
    let text = format!(
        "expect scpi {PROMPT} within 10
at 20
send :SYSTem:LANGuage \"PFORTH\"
expect {PROMPT} within 10
send 4 crash
expect scpi {PROMPT} within 40
at 60
send *IDN?
expect {PROMPT} within 10
stop
"
    );
    let [unicorn, mc683xx] = both(receiver, &text, 120, &[]);
    // each reset record's cause, the power-on's first on both cores
    for run in [&unicorn, &mc683xx] {
        assert_eq!(reset_causes(&run.errors), ["PowerOn", "Watchdog"]);
    }
    assert_eq!(unicorn.answer("*IDN?"), mc683xx.answer("*IDN?"));
}

#[test]
fn the_z3805a_watchdog_reboots_it_on_both_cores() {
    watchdog_reboots(Z3805A);
}

#[test]
fn the_58503a_watchdog_reboots_it_on_both_cores() {
    watchdog_reboots(HP58503A);
}

#[test]
fn the_z3801a_watchdog_reboots_it_on_both_cores() {
    watchdog_reboots(Z3801A);
}

/// The 58503A's keys, each at its board time, and the display's first
/// record after each that is not the idle line's Status lamp blinking
/// (58503a-3704.md, "The front panel").
#[test]
fn the_58503a_keys_change_its_display_alike_on_both_cores() {
    const KEYS: [&str; 6] = ["sat", "time", "lat", "long", "shift", "alt"];
    const IDLE: &str = "lamps=0x20 text=OCXO\\x20WARM\\x20UP";
    let mut steps = Vec::new();
    for (index, key) in KEYS.iter().enumerate() {
        steps.push(format!("at {}", 80 + 2 * index));
        steps.push(format!("key {key}"));
    }
    steps.push("at 94".to_owned());
    let steps: Vec<&str> = steps.iter().map(String::as_str).collect();
    let [unicorn, mc683xx] = both(HP58503A, &script(&steps), 120, &["--indicators"]);
    let shown = |run: &Run| {
        let mut shown = Vec::new();
        let mut waiting = false;
        for line in &run.errors {
            if line.contains(" step=key") {
                waiting = true;
            } else if waiting && let Some(display) = line.strip_prefix("smartclock-emu: display ") {
                let display = display.split_once(' ').map_or("", |(_, rest)| rest);
                if display != IDLE {
                    shown.push(display.to_owned());
                    waiting = false;
                }
            }
        }
        shown
    };
    let shown = [shown(&unicorn), shown(&mc683xx)];
    assert_eq!(shown[0], shown[1]);
    assert_eq!(shown[0].len(), KEYS.len(), "{:?}", shown[0]);
    assert_eq!(shown[0][0], "lamps=0x10 text=6\\x20TRACKED");
}

/// The 58503A's alarm, *STB? AND *SRE (58503a-3704.md, "The alarm
/// outputs"; 097-59551-02 5-42): a command error's ESR bit enabled into
/// ESB and ESB into the alarm, an error raises it and PF5 goes low;
/// *CLS clears both.
#[test]
fn the_58503a_alarm_drives_pf5_alike_on_both_cores() {
    let text = script(&[
        "at 10",
        "send *ESE 32",
        "send *SRE 32",
        "send :NOSuch?",
        "at 40",
        "send :LED:ALARm?",
        "send *CLS",
        "at 45",
        "send :LED:ALARm?",
    ]);
    let [unicorn, mc683xx] = both(HP58503A, &text, 90, &["--indicators"]);
    for run in [&unicorn, &mc683xx] {
        assert_eq!(run.answers(":LED:ALARm?"), ["1", "0"]);
        let pf5: Vec<&str> = run
            .records("smartclock-emu: indicator ")
            .filter(|record| record.contains(" bank=output name=alarm "))
            .map(|record| record.rsplit_once(' ').map_or("", |(_, lit)| lit))
            .collect();
        assert_eq!(pf5, ["lit=1", "lit=0"]);
    }
}

/// The loop pulls in an oscillator 1e-8 fast on both cores (the same
/// as `tests/ocxo.rs` on mc683xx alone): `LOCK` within a query, the
/// TI pulled in to within `TI_APART` of the other core's and the EFC
/// to within `EFC_APART`, both near the EFC on frequency
/// (docs/emulator.md, "The peripheral checks").
#[test]
#[ignore = "2000 s of board time on both cores: about 3 minutes of host time, release"]
fn the_loop_locks_alike_on_both_cores() {
    const EVERY: u32 = 60;
    const UNTIL: u32 = 2000;
    const TI_APART: f64 = 5e-9;
    const EFC_APART: f64 = 32.0;
    // 1e-8 over the firmware's G, 6.25e-13 a count, below the middle
    const ON_FREQUENCY: f64 = 524_288.0 - 16_000.0;
    const NEAR: f64 = 100.0;
    let mut steps = Vec::new();
    for seconds in (EVERY..=UNTIL).step_by(EVERY as usize) {
        steps.push(format!("at {seconds}"));
        steps.push("send :SYNChronization:STATe?".to_owned());
        steps.push("send :SYNChronization:TINTerval?".to_owned());
        steps.push("send :DIAGnostic:ROSCillator:EFControl:ABSolute?".to_owned());
    }
    let steps: Vec<&str> = steps.iter().map(String::as_str).collect();
    let [unicorn, mc683xx] = both(
        Z3805A,
        &script(&steps),
        UNTIL + 100,
        &["--ocxo-offset", "1e-8"],
    );
    let states = [&unicorn, &mc683xx].map(|run| run.answers(":SYNChronization:STATe?"));
    let first_lock = states
        .clone()
        .map(|states| states.iter().position(|state| state == "LOCK"));
    assert!(first_lock[0].is_some(), "{:?}", states[0]);
    // the power-up stages end on whole seconds the CPU's speed moves
    assert!(
        first_lock[0]
            .zip(first_lock[1])
            .is_some_and(|(a, b)| a.abs_diff(b) <= 1),
        "{states:?}"
    );
    let last = |run: &Run, query: &str| -> f64 {
        let answers = run.answers(query);
        let answer = answers.last().expect("an answer");
        answer
            .parse()
            .unwrap_or_else(|_| panic!("{query} {answer}"))
    };
    let ti = [&unicorn, &mc683xx].map(|run| last(run, ":SYNChronization:TINTerval?"));
    assert!((ti[0] - ti[1]).abs() <= TI_APART, "{ti:?}");
    let efc =
        [&unicorn, &mc683xx].map(|run| last(run, ":DIAGnostic:ROSCillator:EFControl:ABSolute?"));
    assert!((efc[0] - efc[1]).abs() <= EFC_APART, "{efc:?}");
    assert!((efc[0] - ON_FREQUENCY).abs() < NEAR, "{efc:?}");
}
