//! The Z3805A 3543B's own firmware, booted in the emulator, reaches its
//! SCPI prompt on the host port and answers there as the library's
//! session expects a receiver to.  Each session is a script, its
//! queries at fixed board times under `--deterministic`, so a loaded
//! host cannot move them.  On the default core, so only with
//! `mc683xx`.

#![cfg(feature = "mc683xx")]

mod common;

use std::iter;
use std::path::Path;

use common::CORES;
use common::Output;
use common::Scratch;
use common::after_prompt;
use common::answer;
use common::answer_lines;
use common::assert_stopped;
use common::run;
use common::run_with_engine;
use common::script;

/// The queries `smartclock-cli diagnose` makes of a Z3805A in power-up,
/// in its order, after `*IDN?`.
const DIAGNOSE: [&str; 20] = [
    ":ROSCillator:STATe?",
    ":ROSCillator:HOLDover:WAITing?",
    ":PTIMe:TFOMerit?",
    ":PTIMe:FFOMerit?",
    ":PTIMe:TINTerval?",
    ":DIAGnostic:ROSCillator:EFControl:RELative?",
    ":DIAGnostic:ROSCillator:CURRent?",
    ":DIAGnostic:ROSCillator:TCOefficient?",
    ":DIAGnostic:ROSCillator:EFControl:ABSolute?",
    ":STATus:OPERation:HARDware:CONDition?",
    ":STATus:OPERation:CONDition?",
    ":STATus:OPERation:POWerup:CONDition?",
    ":ROSCillator:HOLDover:DURation?",
    ":ROSCillator:HOLDover:TUNCertainty:PREDicted?",
    ":ROSCillator:HOLDover:TUNCertainty:PRESent?",
    ":PTIMe:GPSystem:SATellite:TRACking:COUNt?",
    ":PTIMe:GPSystem:SATellite:VISible:PREDicted:COUNt?",
    ":PTIMe:TIME?",
    ":PTIMe:DATE?",
    ":DIAGnostic:LOG:COUNt?",
];

/// How many times the host port is put through `DIAGNOSE`.
const DIAGNOSE_ROUNDS: usize = 10;

/// The longest the boot's prompt, or a query's answer and the prompt
/// after it, may take, board seconds: with no GPS engine the status
/// screen asked for at the first prompt comes late, when the script's
/// record of the expect after it says (docs/emulator.md, "What the
/// boot shows"; "Scripts").
const ANSWER_LIMIT: u32 = 120;

/// The longest a run may take, board seconds, should a step wait for
/// good.
const LIMIT: &str = "1200";

/// When the tracking test asks for the status screen, board seconds:
/// past the synthetic engine's init script and the first seconds of
/// @@Ba, the screen tracking six by then.
const TRACKING_AT: u32 = 60;

/// The synthetic engine's UTC at power-up, fixed, so that the run with
/// it repeats (docs/emulator.md, "Scripts").
const GPS_START: &str = "2026-10-23T00:00:00Z";

/// A script's text for the prompt after a command that raised no
/// error, `scpi > `.
const READY: &str = "scpi >\\x20";

/// A script's text for either prompt, `scpi > ` or an error's
/// `E-nnn> ` (docs/protocol.md, "The error prompt"), as `diagnose`
/// reads each answer to one: some of its queries fail in power-up.  It
/// is in none of their answers, but it is in the status screen
/// (`>> Power-up`).
const ANY_PROMPT: &str = ">\\x20";

/// The script's step awaiting the next `prompt`.
fn await_prompt(prompt: &str) -> String {
    format!("expect {prompt} within {ANSWER_LIMIT}\n")
}

/// The script's steps sending each of `commands` as soon as the last
/// has been answered, in board time: at the `prompt` after its answer.
fn queries<'a>(prompt: &str, commands: impl IntoIterator<Item = &'a str>) -> String {
    commands
        .into_iter()
        .map(|command| format!("send {command}\n{}", await_prompt(prompt)))
        .collect()
}

/// A script that waits for the boot's `prompt`, then sends `commands`
/// as `queries` does, and stops.
fn session<'a>(prompt: &str, commands: impl IntoIterator<Item = &'a str>) -> String {
    format!(
        "{}{}stop\n",
        await_prompt(prompt),
        queries(prompt, commands)
    )
}

/// The transcript of a fresh run of the script `steps`, by `launch`
/// (`run` or `run_with_engine`) with `options`, checked to have
/// reached the script's `stop`.
fn scripted(
    name: &str,
    steps: &str,
    launch: fn(&Path, &[&str]) -> Output,
    options: &[&str],
) -> String {
    let eeprom = Scratch::new(name);
    let script = script(name, steps);
    let path = script.0.to_str().expect("a path");
    let (transcript, trace) = launch(
        &eeprom.0,
        &[&["--script", path, "--stop-after", LIMIT], options].concat(),
    );
    assert_stopped(&trace);
    String::from_utf8_lossy(&transcript).into_owned()
}

/// The host port answers query after query, each sent as soon as the
/// last answered, as `diagnose` sends them: a run that once stopped
/// the port for good partway through a command's echo, when the CPU
/// core lost the condition codes across an interrupt
/// (docs/emulator.md, "The condition codes").  The run reaching its
/// `stop` is each prompt come within its limit; each command was
/// echoed whole at one.  On each core this build has, Unicorn's being
/// the one that lost them.
#[test]
fn the_host_port_answers_diagnose_again_and_again() {
    let round = || iter::once("*IDN?").chain(DIAGNOSE);
    let commands: Vec<&str> = iter::repeat_with(round)
        .take(DIAGNOSE_ROUNDS)
        .flatten()
        .collect();
    for core in CORES {
        let transcript = scripted(
            &format!("diagnose-{core}"),
            &session(ANY_PROMPT, commands.iter().copied()),
            run,
            &["--core", core],
        );
        let echoes: Vec<&str> = transcript
            .lines()
            .filter_map(after_prompt)
            .filter(|echo| !echo.is_empty())
            .collect();
        assert!(echoes == commands, "{core}: {transcript}");
    }
}

#[test]
fn the_z3805a_boots_to_its_prompt_and_answers() {
    let transcript = scripted(
        "boot",
        &session(
            READY,
            [
                "*IDN?",
                ":SYSTem:ERRor?",
                ":PTIMe:TCODe?",
                ":SYSTem:STATus?",
            ],
        ),
        run,
        &[],
    );

    assert_eq!(
        answer(&transcript, "*IDN?"),
        "HEWLETT-PACKARD,Z3805A,3625A01487,3543B-A",
        "the image's revision and the EEPROM's serial number"
    );
    assert_eq!(answer(&transcript, ":SYSTem:ERRor?"), "+0,\"No error\"");
    let code = answer(&transcript, ":PTIMe:TCODe?");
    assert!(code.starts_with("T1#H") || code.starts_with("T2"), "{code}");

    let status = answer_lines(&transcript, ":SYSTem:STATus?");
    assert!(
        status.iter().any(|line| line.contains("Receiver Status")),
        "{status:?}"
    );
    assert!(
        status.iter().any(|line| line.contains("Power-up")),
        "the loop in power-up, with no GPS: {status:?}"
    );
}

#[test]
fn the_synthetic_engine_shows_as_tracking_on_the_status_screen() {
    let steps = format!(
        "{}at {TRACKING_AT}\n{}stop\n",
        await_prompt(READY),
        queries(READY, [":SYSTem:STATus?"])
    );
    let transcript = scripted("gps", &steps, run_with_engine, &["--gps-start", GPS_START]);
    let status = answer_lines(&transcript, ":SYSTem:STATus?");
    assert!(
        status.iter().any(|line| line.starts_with("Tracking: 6")),
        "no satellites tracked by {TRACKING_AT} s: {status:?}"
    );
}

/// The synthetic engine's highest satellite, PRN 3 (smartclock-gps's
/// `source::fake_receiver`), which it tracks until it is ignored.
const HIGHEST: u8 = 3;

/// Board seconds from a change to the ignore list to the count asked
/// for: the firmware's @@Am, its echo, and the next @@Ba after it.
const IGNORE_SETTLES: u32 = 10;

/// `:GPS:SAT:TRAC:IGN` reaches the synthetic engine as @@Am, whose
/// list it tracks without (docs/emulator.md, "The synthetic GPS
/// engine"): the tracked count drops by the satellite ignored, and
/// `:INCL` brings it back.
#[test]
fn an_ignored_satellite_leaves_the_tracked_count_until_it_is_included() {
    const COUNT: &str = ":PTIMe:GPSystem:SATellite:TRACking:COUNt?";
    let ignored_at = TRACKING_AT + IGNORE_SETTLES;
    let included_at = ignored_at + IGNORE_SETTLES;
    let steps = format!(
        "{}at {TRACKING_AT}\n{}at {ignored_at}\n{}at {included_at}\n{}stop\n",
        await_prompt(READY),
        queries(READY, [COUNT, &format!(":GPS:SAT:TRAC:IGN {HIGHEST}")]),
        queries(READY, [COUNT, &format!(":GPS:SAT:TRAC:INCL {HIGHEST}")]),
        queries(READY, [COUNT]),
    );
    let transcript = scripted(
        "ignore",
        &steps,
        run_with_engine,
        &["--gps-start", GPS_START],
    );
    let lines: Vec<&str> = transcript.lines().collect();
    let counts: Vec<&str> = lines
        .windows(2)
        .filter(|pair| after_prompt(pair[0]) == Some(COUNT))
        .map(|pair| pair[1].trim_end())
        .collect();
    assert_eq!(counts, ["+6", "+5", "+6"], "{transcript}");
}

/// The power-up self-test's summary bit for the GPS engine
/// (smartclock.h's `selftest_summary_bit`), the one test that fails
/// with no engine connected.
const GPS: u32 = 0x200;

/// When the self-test's result is asked for, board seconds: past the
/// GPS engine's test's time-out, before which the summary has no GPS
/// bit yet.
const SELFTEST_DONE_AT: u32 = 60;

/// The power-up self-test passes on the board's models (selftest.c):
/// once the GPS engine's test has timed out, with none connected, its
/// bit is the summary's only one.
#[test]
fn the_power_up_self_test_passes_on_the_boards_devices() {
    let steps = format!(
        "{}at {SELFTEST_DONE_AT}\n{}stop\n",
        await_prompt(READY),
        queries(READY, [":DIAGnostic:TEST:RESult?"])
    );
    let transcript = scripted("selftest", &steps, run, &[]);
    let result = answer(&transcript, ":DIAGnostic:TEST:RESult?");
    let summary: u32 = result
        .split(',')
        .next()
        .and_then(|code| code.trim_start_matches('+').parse().ok())
        .unwrap_or_else(|| panic!("a code: {result}"));
    assert_eq!(summary & !GPS, 0, "only the GPS engine fails: {result}");
    assert_ne!(
        summary & GPS,
        0,
        "the GPS engine's test timed out: {result}"
    );
}

/// A query long enough, at the line's rate, to span the power-up
/// self-test's UART check.
const LONG_QUERY: &str = ":SOURce:ROSCillator:HOLDover:DURation:STATus:THReshold?";

/// When `LONG_QUERY` is sent into the UART check, board seconds: its
/// head before the check disables the host port's receiver, the rest
/// in it (`--watch-code` on `duart_loopback_test_b` gives when).
const IN_UART_CHECK_AT: &str = "4.1";

/// When the host port hears again, board seconds: past the UART check
/// and the QSPI's.
const PAST_HOST_PORT_CHECKS_AT: u32 = 6;

/// What the host port is sent in the power-up self-test's UART check
/// is lost, from where the check disables channel B's receiver, its end
/// of line too, as on the board: the echo stops short, and the line
/// ends only with the next one sent.  After the check the port hears
/// the same query whole.  So a session must wait out the self-test's
/// checks of the host port, not just the first prompt
/// (`tests/common`, `Emulator::start_past_self_test`).
#[test]
fn the_power_up_self_tests_uart_check_loses_what_the_host_port_is_sent() {
    let steps = format!(
        "{}at {IN_UART_CHECK_AT}\nsend {LONG_QUERY}\nat {PAST_HOST_PORT_CHECKS_AT}\nsend\n{}{}stop\n",
        await_prompt(READY),
        await_prompt(ANY_PROMPT),
        queries(ANY_PROMPT, [LONG_QUERY])
    );
    let transcript = scripted("uart-check", &steps, run, &[]);
    let cut = transcript
        .lines()
        .find_map(after_prompt)
        .unwrap_or_else(|| panic!("an echo: {transcript}"))
        .trim_end();
    assert!(
        cut.len() < LONG_QUERY.len() && LONG_QUERY.starts_with(cut),
        "the echo stops short: {cut:?}"
    );
    let answered = answer(&transcript, LONG_QUERY);
    assert!(
        answered.trim_start_matches('+').parse::<f64>().is_ok(),
        "answered whole after the check: {answered:?}"
    );
}
