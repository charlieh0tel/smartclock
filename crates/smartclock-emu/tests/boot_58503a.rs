//! The 58503A 3704's own firmware, booted in the emulator on the
//! 58503A's board profile (docs/emulator.md, "The boards"), reaches its
//! SCPI prompt on the host port and answers there, and its front panel
//! passes the self-test that starts the `fpnl` task, which puts the
//! model's name on the display.  The session is a script, its queries
//! at fixed board times under `--deterministic`, so a loaded host
//! cannot move them before the boot's work is done.  On the default
//! core, so only with `mc683xx`.

#![cfg(feature = "mc683xx")]

mod common;

use common::HP58503A;
use common::Scratch;
use common::answer;
use common::assert_stopped;
use common::run_receiver;
use common::run_receiver_with_engine;
use common::script;

/// The session: the power-up prompt, then, once the self-test and
/// `fpnl`'s banner are done (by about 5.3 s of board time), each query
/// at its prompt.
const SESSION: &str = "# the 58503A's boot
expect scpi >\\x20 within 5
at 10
send *IDN?
expect scpi >\\x20 within 5
send :SYSTem:ERRor?
expect scpi >\\x20 within 5
send :DIAGnostic:TEST:RESult?
expect scpi >\\x20 within 5
stop
";

/// The longest the run may take, board seconds, should a step wait for
/// good.
const LIMIT: &str = "60";

/// The display record `fpnl`'s banner ends on, its scroll done
/// (58503a-3704.md, "The front panel", "When").
const BANNER: &str = "text=HP\\x2058503A";

/// The power-up self-test's summary bit for the GPS engine
/// (rewritten/58503a-3704/types/smartclock.h, `selftest_summary_bit`), which fails
/// with none connected; the rest are the board's.
const GPS: u32 = 0x200;

#[test]
fn the_58503a_boots_to_its_prompt_and_answers() {
    let eeprom = Scratch::new("boot-58503a");
    let script = script("boot-58503a", SESSION);
    let path = script.0.to_str().expect("a path");
    let (transcript, trace) = run_receiver(
        HP58503A,
        &eeprom.0,
        &["--script", path, "--stop-after", LIMIT, "--indicators"],
    );
    let transcript = String::from_utf8_lossy(&transcript);

    assert_eq!(
        answer(&transcript, "*IDN?"),
        "HEWLETT-PACKARD,58503A,3710A01056,3704-C",
        "the image's revision and the EEPROM's serial number"
    );
    assert_eq!(answer(&transcript, ":SYSTem:ERRor?"), "+0,\"No error\"");
    let result = answer(&transcript, ":DIAGnostic:TEST:RESult?");
    let summary: u32 = result
        .split(',')
        .next()
        .and_then(|code| code.trim_start_matches('+').parse().ok())
        .unwrap_or_else(|| panic!("a code: {result}"));
    assert_eq!(summary & !GPS, 0, "{result}");

    assert!(
        trace
            .iter()
            .any(|line| line.starts_with("smartclock-emu: display ") && line.ends_with(BANNER)),
        "the front panel shows the banner"
    );
    assert_stopped(&trace);
}

/// The synthetic engine's UTC at power-up, fixed, so that the run with
/// it repeats (docs/emulator.md, "Scripts").
const GPS_START: &str = "2026-10-22T12:00:00Z";

/// The display run: the keys in warm-up, past the banner, then the
/// warm-up skipped, so that the loop goes through its stages to
/// `OUTPUT VALID` within `DISPLAY_LIMIT`.  Shift then Alt within a
/// second of it is the alarm's line, not the altitude's.
const DISPLAY_SESSION: &str = "# the 58503A's display
at 80
key sat
at 82
key time
at 84
key shift
at 84.5
key alt
at 90
event skip-warm-up
";

/// The display run's length, board seconds.
const DISPLAY_LIMIT: &str = "240";

/// What the display shows, as `display` records give it, in the order
/// docs/emulator.md's "What the boot shows" quotes it: the banner's
/// first two characters scrolled in, the key lamps all lit as they
/// sweep, the banner, the Status key's lamp blinking over it, the
/// warm-up's line, Sat's, Time's before the time is known, Shift then
/// Alt's, and the loop's last two stages.
const DISPLAYED: [&str; 10] = [
    "text=\\x20\\x20\\x20\\x20\\x20\\x20\\x20\\x20\\x20\\x20HP",
    "lamps=0xff ",
    BANNER,
    "lamps=0x20 text=HP\\x2058503A",
    "text=OCXO\\x20WARM\\x20UP",
    "text=6\\x20TRACKED",
    "text=TIME:\\x20**:**:**",
    "text=NO\\x20ALARM",
    "text=LEAPSEC\\x20CALC",
    "text=OUTPUT\\x20VALID",
];

/// The display texts docs/emulator.md quotes, each in its turn, from
/// one deterministic run with the synthetic engine.
#[test]
fn the_58503a_display_shows_what_the_docs_quote() {
    let eeprom = Scratch::new("display-58503a");
    let script = script("display-58503a", DISPLAY_SESSION);
    let path = script.0.to_str().expect("a path");
    let (_, trace) = run_receiver_with_engine(
        HP58503A,
        &eeprom.0,
        &[
            "--script",
            path,
            "--stop-after",
            DISPLAY_LIMIT,
            "--gps-start",
            GPS_START,
            "--indicators",
        ],
    );
    let mut displays = trace
        .iter()
        .filter_map(|line| line.strip_prefix("smartclock-emu: display "));
    for wanted in DISPLAYED {
        assert!(
            displays.any(|display| display.contains(wanted)),
            "the display shows {wanted} in its turn"
        );
    }
}
