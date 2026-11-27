//! The Z3817A 3752's own firmware, booted in the emulator on
//! `boards/z3817a.toml` (docs/emulator.md, "The boards";
//! docs/firmware/z3817a-3752.md, "Emulated"): it answers on the SCI,
//! qualifies a reference from VALID and a pulse alone, hunting between
//! TIMEREF1 and TIMEREF2 until it finds one, sends the reference
//! nothing, and qualifies its GPS record from an `@@Ea` stream no
//! request asked for.  It refuses every request that would transmit,
//! the loop's, the console's and SCPI's alike, at
//! `gps_request_admitted` (`0x52abc`; z3817a-3752.md, "What it sends").  Its debug output, which
//! `:DIAGnostic:SYSTem:DOUTput 1` turns on, comes on the SCI with SCPI,
//! and these read the switches of reference and pulse there, and with
//! the console's `efc_comp_debug` the oscillator's compensation.  The
//! FPGA runs the Z3817A's own design (z3817a-trace.md), so the loop
//! locks: its single readings fall on 50 ns + k x 100 ns, and their mean
//! steps by 10 ns with a 1 PPS and 20 ns with a PP2S.  Each session is a
//! script under `--deterministic`.  The runs to lock on the bench's
//! feed and warm-up and on a PP2S, and the trace of the sync field's
//! writes on the way to the first, are minutes of board time, ignored
//! unless asked for:
//!
//!     cargo test --release -p smartclock-emu --test boot_z3817a -- --ignored

#![cfg(feature = "mc683xx")]

mod common;

use std::path::Path;

use common::CPU_HZ;
use common::Scratch;
use common::Z3817A;
use common::after_prompt;
use common::answer;
use common::repository;
use common::run_receiver_with_engine;
use common::script;

/// The board file, under the repository's root.
const BOARD: &str = "boards/z3817a.toml";
/// A model the emulator has, for `--model`: the image names one it has
/// not (boards/README.md).
const MODEL: &str = "z3805a";
/// The synthetic engine's UTC at power-up.
const START: &str = "2026-11-09T12:00:00Z";
/// The script's first steps: the prompt, then the debug output on
/// (`:DIAGnostic:SYSTem:DOUTput`, handler `0x3c4ec`, sets
/// `debug_enabled`, `0x103e9f`), which `task_dmessage` (`0x2e3f2`)
/// writes to I/O device 0, the SCI, as each task's `dmes_` flag allows;
/// `system_init` sets them all (`0x22d98`).
const DEBUG_ON: &str = "expect scpi >\\x20 within 10
send :DIAGnostic:SYSTem:DOUTput 1
expect scpi >\\x20 within 5
";
/// The PLL task's debug line at each switch of TIMEREF (`0x4d45e`).
const SWITCHED_REFERENCE: &str = "pllp: Switched gps ref";
/// Its line at the switch to PP2S mode (`0x4d68c`).
const SWITCHED_TO_PP2S: &str = "pllp: Switched to PP2S mode";
/// Its line at the switch back to 1PPS mode (`0x4d6b6`).
const SWITCHED_TO_1PPS: &str = "pllp: Switched to 1PPS mode";
/// The instruction `gpsm_task` runs for a request `gps_request_admitted`
/// refuses (`0x52bb0`), before it answers it done (`0x52bb6`) and counts
/// it (`0x52bba`); D1 holds the request's sender there.
const REFUSED: &str = "52bb0";
/// The instruction in `gps_request_admitted` (`0x52abc`) after it has
/// taken the request's sender into D1, the request's ID the low word of
/// the stack's second long.
const ADMISSION: &str = "52ac0";
/// The instruction `gpsm_task` runs for a request it admits, before it
/// queues it (`0x52bca`).
const ADMITTED: &str = "52bc0";
/// The instruction in `0x4d4b0` that fails the unit, setting the latch
/// before it logs "Unit failed" (`0x4d554`; z3817a-3752.md, "The board
/// as the image drives it"): run 300 passes in with the FPGA's design
/// unmodeled.
const UNIT_FAILED: &str = "4d554";
/// The sender `gpsx_send` gives every request it posts (`0x4bb94`).
const SENDER_LOOP: u32 = 1;
/// The sender `pll_scpi_command` gives a request from the SCI
/// (`0x484e0`).
const SENDER_SCI: u32 = 3;
/// The board file's oscillator warm-up, the ready bit from 784 s, which
/// puts the loop's "GPS lock started" where the bench unit's log has it,
/// made none: the bit from the first report, for a run whose subject is
/// not the warm-up and whose length or timing it would only change.
const NO_WARM_UP: (&str, &str) = ("ready_seconds = 784 ", "ready_seconds = 0   ");
/// The bench's feed of 2026-11-09, nothing on RXD: the board file's
/// engine with no saved configuration, deaf and idle, so it sends
/// nothing (docs/emulator.md, "The synthetic GPS engine").
const BENCH_FEED: (&str, &str) = ("saved = \"58503a\"", "#saved = \"58503a\"");

/// `boards/z3817a.toml` with each of `changes`' first text replaced by
/// its second, written for the test `name`, its relative paths made the
/// repository's.
fn variant(name: &str, changes: &[(&str, &str)]) -> Scratch {
    let root = repository("");
    // as a TOML basic string holds it: Windows's separators escaped
    let root = root.to_str().expect("a path").replace('\\', "\\\\");
    let text = std::fs::read_to_string(repository(BOARD))
        .expect("the board file")
        .replace("\"../third_party", &format!("\"{root}/third_party"));
    let text = changes.iter().fold(text, |text, (from, to)| {
        assert!(text.contains(from), "{from}");
        text.replace(from, to)
    });
    let board = Scratch::new(&format!("{name}-board"));
    std::fs::write(&board.0, text).expect("the board file written");
    board
}

/// The run of `steps` on `board` to `limit` board seconds, with the
/// engine's line copied to standard error, and `options`: the transcript
/// and the trace.
fn session(
    name: &str,
    board: &str,
    steps: &str,
    limit: &str,
    options: &[&str],
) -> (String, Vec<String>) {
    let eeprom = Scratch::new(name);
    session_on(&eeprom.0, name, board, steps, limit, options)
}

/// `session` on the EEPROM's working file `eeprom`, which a run before it
/// may have left: a power cycle between the two.
fn session_on(
    eeprom: &Path,
    name: &str,
    board: &str,
    steps: &str,
    limit: &str,
    options: &[&str],
) -> (String, Vec<String>) {
    let script = script(name, steps);
    let script = script.0.to_str().expect("a path");
    let common = [
        "--board",
        board,
        "--model",
        MODEL,
        "--gps-start",
        START,
        "--tap",
        "--script",
        script,
        "--stop-after",
        limit,
    ];
    let (transcript, trace) =
        run_receiver_with_engine(Z3817A, eeprom, &[common.as_slice(), options].concat());
    (String::from_utf8_lossy(&transcript).into_owned(), trace)
}

/// One run of an instruction `--watch-code` traces.
#[derive(Debug)]
struct CodeHit {
    /// The board's cycle at it.
    cycle: u64,
    /// The data registers, D0 first.
    data: Vec<u32>,
    /// The top of the stack, a long at a time.
    stack: Vec<u32>,
}

/// The longs of a trace record's field `name`, hexadecimal and
/// comma-separated.
fn longs(line: &str, name: &str) -> Vec<u32> {
    line.split(&format!(" {name}="))
        .nth(1)
        .and_then(|rest| rest.split(' ').next())
        .map(|field| {
            field
                .split(',')
                .map(|long| u32::from_str_radix(long, 16).expect("a hexadecimal long"))
                .collect()
        })
        .unwrap_or_default()
}

/// Each run in `trace` of the watched instruction at `address`, in order.
fn code_hits(trace: &[String], address: &str) -> Vec<CodeHit> {
    let pc = format!(" pc=0x000{address} ");
    trace
        .iter()
        .filter(|line| line.contains(&pc))
        .map(|line| CodeHit {
            cycle: line
                .split(" cycle=")
                .nth(1)
                .and_then(|rest| rest.split(' ').next())
                .and_then(|cycle| cycle.parse().ok())
                .expect("a cycle"),
            data: longs(line, "d"),
            stack: longs(line, "stack"),
        })
        .collect()
}

/// What the module sent its reference, as the engine's tap gives it.
fn sent_to_reference(trace: &[String]) -> Vec<&String> {
    trace
        .iter()
        .filter(|line| line.starts_with("smartclock-emu: gps < "))
        .collect()
}

/// The bench's feed of 2026-11-09 (`BENCH_FEED`): a 1 PPS and VALID on
/// TIMEREF2, nothing on RXD.  The module comes up on TIMEREF1,
/// finds it dead, switches at its 300th pass, and takes TIMEREF2 from
/// VALID and the pulse, as the bench unit's log has it ("GPS reference
/// valid" 5 min 19 s after power-on); it sends nothing on channel A.
/// The switch is the one debug line "Switched gps ref" between the two
/// status screens.
#[test]
fn the_z3817a_hunts_to_the_reference_with_valid_and_a_pulse() {
    let steps = format!(
        "{DEBUG_ON}send *IDN?
expect scpi >\\x20 within 5
at 290
send :SYSTem:STATus?
expect scpi >\\x20 within 5
at 330
send :SYSTem:STATus?
expect scpi >\\x20 within 5
stop
"
    );
    let board = variant("boot-z3817a-hunt", &[BENCH_FEED]);
    let (transcript, trace) = session(
        "boot-z3817a-hunt",
        board.0.to_str().expect("a path"),
        &steps,
        "340",
        &[],
    );
    assert!(
        transcript.contains("HEWLETT-PACKARD,Z3817A,3832A03064,3752-A"),
        "{transcript}"
    );
    let before = transcript
        .find("[ TIMEREF1 (GPS) 1PPS Invalid ]")
        .expect("TIMEREF1 first, dead");
    let after = transcript
        .find("[ TIMEREF2 1PPS Valid ]")
        .expect("TIMEREF2 after the switch, valid");
    let switched = transcript
        .find(SWITCHED_REFERENCE)
        .expect("the switch's debug line");
    assert!(before < switched && switched < after, "{transcript}");
    assert_eq!(transcript.matches(SWITCHED_REFERENCE).count(), 1);
    assert!(sent_to_reference(&trace).is_empty(), "nothing sent");
}

/// The board file's engine, deaf and saved as a 58503A leaves it, an
/// `@@Ea` stream that answers nothing, with no VALID: the GPS record
/// qualifies all the same, the 1 PPS check never turned off, since the
/// start-up script that would turn it off is never sent.
#[test]
fn the_z3817a_qualifies_its_gps_record_from_an_unasked_stream() {
    let board = variant(
        "boot-z3817a-stream",
        &[("timeref2_valid = true ", "timeref2_valid = false ")],
    );
    let steps = "expect scpi >\\x20 within 10
at 60
send :SOURce:GPSystem:REFerence:VALid?
expect scpi >\\x20 within 5
stop
";
    let (transcript, trace) = session(
        "boot-z3817a-stream",
        board.0.to_str().expect("a path"),
        steps,
        "70",
        &[],
    );
    assert!(
        transcript.contains(":SOURce:GPSystem:REFerence:VALid?\r\n1\r\n"),
        "{transcript}"
    );
    assert!(sent_to_reference(&trace).is_empty(), "nothing sent");
}

/// A PP2S and VALID on TIMEREF2, PE7 high selecting it, the pulse a
/// 1 PPS from 120 s (run e of z3817a-3752.md, "Emulated"): with the
/// 1 PPS dead and the PP2S alive the loop switches to PP2S mode
/// (`0x4d670`), says so on the debug output, and takes the reference;
/// with the PP2S dead and the 1 PPS alive it switches back, each with no
/// switch of TIMEREF.  The mode byte (`pll` + `0x3ea`) and the count
/// beside it (`+0x3ec`) are set once each way: 1 and 5 at `0x4d686`
/// before the change, 0 and 10 at `0x4d6b0` after it.
#[test]
fn the_z3817a_switches_to_pp2s_mode_and_back() {
    /// The second from power-up from which the pulse is a 1 PPS.
    const CHANGE_SECOND: u64 = 120;
    /// Where the loop sets the mode byte and its count for PP2S.
    const TO_PP2S: &str = "4d686";
    /// Where it sets them for 1 PPS.
    const TO_1PPS: &str = "4d6b0";
    let change = format!(
        "timeref2_change = \"1pps\"\ntimeref2_change_second = {CHANGE_SECOND}\nselect_high = 2 "
    );
    let board = variant(
        "boot-z3817a-pp2s",
        &[
            BENCH_FEED,
            ("timeref2 = \"1pps\" ", "timeref2 = \"pp2s\" "),
            ("select_high = 1 ", &change),
            NO_WARM_UP,
        ],
    );
    let steps = format!(
        "{DEBUG_ON}at 110
send :SYSTem:STATus?
expect scpi >\\x20 within 5
at 200
send :SYSTem:STATus?
expect scpi >\\x20 within 5
stop
"
    );
    let (transcript, trace) = session(
        "boot-z3817a-pp2s",
        board.0.to_str().expect("a path"),
        &steps,
        "210",
        &["--watch-code", TO_PP2S, "--watch-code", TO_1PPS],
    );
    let in_order = [
        SWITCHED_TO_PP2S,
        "[ TIMEREF1 PP2S Valid ]",
        "hmon: Ref pp2s signal is dead",
        "hmon: Ref 1pps signal is ok",
        SWITCHED_TO_1PPS,
        "[ TIMEREF1 1PPS Valid ]",
    ];
    let at: Vec<usize> = in_order
        .iter()
        .map(|line| transcript.find(line).expect(line))
        .collect();
    assert!(at.is_sorted(), "{transcript}");
    assert!(!transcript.contains(SWITCHED_REFERENCE), "{transcript}");
    let to_pp2s = code_hits(&trace, TO_PP2S);
    let to_1pps = code_hits(&trace, TO_1PPS);
    assert_eq!((to_pp2s.len(), to_1pps.len()), (1, 1));
    let change = CHANGE_SECOND * CPU_HZ;
    assert!(to_pp2s[0].cycle < change && change < to_1pps[0].cycle);
    // the board seconds z3817a-3752.md quotes, run e
    let tenths = |hit: &CodeHit| (10 * hit.cycle).div_ceil(CPU_HZ);
    assert_eq!((tenths(&to_pp2s[0]), tenths(&to_1pps[0])), (709, 1372));
    assert!(sent_to_reference(&trace).is_empty(), "nothing sent");
}

/// SCPI's settings that the 58503A sends its engine, `@@Ag`, the
/// survey's script `0x5f`, `@@Ac`, `@@Az` and `@@Am`: each is posted
/// from the SCI with sender 3, refused at `gps_request_admitted` and
/// answered done, so SCPI reports no error and the module sends nothing.
#[test]
fn the_z3817a_refuses_scpi_requests_for_its_engine() {
    let board = variant(
        "boot-z3817a-scpi",
        &[BENCH_FEED, ("select_high = 1 ", "select_high = 2 ")],
    );
    let commands = [
        ":SOURce:GPSystem:SATellite:TRACking:EMANgle 15",
        ":SOURce:GPSystem:POSition:SURVey:STATe ONCE",
        ":SOURce:GPSystem:INITial:DATE 2026,11,9",
        ":SOURce:GPSystem:REFerence:ADELay 100ns",
        ":SOURce:GPSystem:SATellite:TRACking:IGNore 5",
    ];
    let sends: String = commands
        .iter()
        .map(|command| {
            format!(
                "send {command}
expect scpi >\\x20 within 30
send :SYSTem:ERRor?
expect scpi >\\x20 within 5
"
            )
        })
        .collect();
    let steps = format!(
        "expect scpi >\\x20 within 10
at 30
{sends}at 60
stop
"
    );
    let (transcript, trace) = session(
        "boot-z3817a-scpi",
        board.0.to_str().expect("a path"),
        &steps,
        "70",
        &["--watch-code", REFUSED],
    );
    assert_eq!(answer(&transcript, ":SYSTem:ERRor?"), "+0,\"No error\"");
    assert_eq!(
        transcript.matches("+0,\"No error\"").count(),
        commands.len(),
        "{transcript}"
    );
    let refused_from_sci = code_hits(&trace, REFUSED)
        .iter()
        .filter(|hit| hit.data[1] & 0xff == SENDER_SCI)
        .count();
    assert_eq!(refused_from_sci, commands.len(), "refused, sender 3");
    assert!(sent_to_reference(&trace).is_empty(), "nothing sent");
}

/// The console's GPS words, `print_stat` and its kind, leave a code in
/// `gps_command` (`0x102ab2`) that the loop's `gps_command_send`
/// (`0x4c63c`) turns into a request and posts with `gpsx_send`, sender 1,
/// as the loop's own (z3817a-3752.md, "What it sends"): `print_stat`'s
/// step `0x3a` is admitted and prints the engine's status, `pr_gps_id`'s
/// step `0x3e` and `init_gps`'s script `0x54` are refused, the first
/// printing nothing, and nothing is sent.
#[test]
fn the_z3817a_console_posts_as_the_loop() {
    /// When the console is entered, after the loop's start-up sequence.
    const CONSOLE_SECOND: u64 = 30;
    let steps = format!(
        "expect scpi >\\x20 within 10
at {CONSOLE_SECOND}
send :SYSTem:LANGuage \"PFORTH\"
expect >\\x20 within 10
send print_stat
at 40
send pr_gps_id
at 50
send init_gps
at 60
send halt
expect scpi >\\x20 within 10
stop
"
    );
    let board = variant("boot-z3817a-console", &[BENCH_FEED]);
    let (transcript, trace) = session(
        "boot-z3817a-console",
        board.0.to_str().expect("a path"),
        &steps,
        "70",
        &[
            "--watch-code",
            ADMISSION,
            "--watch-code",
            ADMITTED,
            "--watch-code",
            REFUSED,
        ],
    );
    let after_console = |hit: &&CodeHit| hit.cycle >= CONSOLE_SECOND * CPU_HZ;
    let checked: Vec<(u32, u32)> = code_hits(&trace, ADMISSION)
        .iter()
        .filter(after_console)
        .map(|hit| (hit.data[1] & 0xff, hit.stack[1] & 0xffff))
        .collect();
    assert_eq!(
        checked,
        [
            (SENDER_LOOP, 0x3a),
            (SENDER_LOOP, 0x3e),
            (SENDER_LOOP, 0x54)
        ]
    );
    let admitted = code_hits(&trace, ADMITTED);
    let refused = code_hits(&trace, REFUSED);
    assert_eq!(admitted.iter().filter(after_console).count(), 1);
    assert_eq!(refused.iter().filter(after_console).count(), 2);
    assert!(transcript.contains("CHN      SVID"), "{transcript}");
    assert!(sent_to_reference(&trace).is_empty(), "nothing sent");
}

/// The oscillator's compensation, with the console's `efc_comp_debug` on
/// and the debug output (z3817a-3752.md, "Emulated"): a line a report on
/// channel B, "err= %d corr= %f efc= %f adc= %d" (`0x3365a`), each `corr`
/// the last plus `err` times 0.001 (`0x335e2`), each `efc` the EFC
/// `pr_efc` prints plus `corr`; no line after `0 efc_comp_debug`.
#[test]
fn the_z3817a_integrates_its_oscillators_reports() {
    /// What `0x3355e` integrates `err` by.
    const GAIN: f64 = 0.001;
    /// The first lines, which z3817a-3752.md quotes.
    const FIRST_LINES: [&str; 5] = [
        "err= -10 corr= 3.110000 efc= 764562.500000 adc= 12420014",
        "err= 250 corr= 3.360000 efc= 764562.750000 adc= 12419754",
        "err= 250 corr= 3.610000 efc= 764563.000000 adc= 12419754",
        "err= -10 corr= 3.600000 efc= 764563.000000 adc= 12420014",
        "err= -10 corr= 3.590000 efc= 764562.937500 adc= 12420014",
    ];
    /// The resolution of the line's `corr`, printed to six places.
    const PRINTED: f64 = 1e-5;
    /// The resolution of a single-precision float near the EFC, which
    /// the line's `efc` is.
    const EFC_RESOLUTION: f64 = 0.0625;
    let steps = format!(
        "{DEBUG_ON}at 30
send :SYSTem:LANGuage \"PFORTH\"
expect >\\x20 within 10
send pr_efc
at 32
send 1 efc_comp_debug
at 52
send 0 efc_comp_debug
at 56
send halt
expect scpi >\\x20 within 10
stop
"
    );
    let board = variant("boot-z3817a-efc", &[BENCH_FEED]);
    let (transcript, trace) = session(
        "boot-z3817a-efc",
        board.0.to_str().expect("a path"),
        &steps,
        "60",
        &[],
    );
    let efc: f64 = transcript
        .lines()
        .find_map(|line| line.strip_prefix("efc = "))
        .and_then(|value| value.trim().parse().ok())
        .expect("pr_efc's line");
    let field = |line: &str, name: &str| -> f64 {
        line.split(&format!("{name}= "))
            .nth(1)
            .and_then(|rest| rest.split(' ').next())
            .and_then(|value| value.trim().parse().ok())
            .expect(name)
    };
    let off = transcript.find("0 efc_comp_debug").expect("turned off");
    let reports: Vec<(f64, f64, f64)> = transcript[..off]
        .lines()
        .filter(|line| line.contains("err= "))
        .map(|line| (field(line, "err"), field(line, "corr"), field(line, "efc")))
        .collect();
    assert!(reports.len() >= 15, "{transcript}");
    // the first lines, as z3817a-3752.md quotes them
    let first: Vec<&str> = transcript[..off]
        .lines()
        .filter_map(|line| line.find("err= ").map(|at| line[at..].trim_end()))
        .take(FIRST_LINES.len())
        .collect();
    assert_eq!(first, FIRST_LINES);
    for pair in reports.windows(2) {
        let [(_, last, _), (err, corr, _)] = pair else {
            unreachable!("windows of two")
        };
        assert!((corr - last - err * GAIN).abs() < PRINTED, "{pair:?}");
    }
    for (_, corr, line_efc) in &reports {
        assert!(
            (line_efc - efc - corr).abs() <= EFC_RESOLUTION,
            "{line_efc}"
        );
    }
    assert!(!transcript[off..].contains("err= "), "{transcript}");
    assert!(sent_to_reference(&trace).is_empty(), "nothing sent");
}

/// `:DIAGnostic:SYSTem:DOUTput` is saved in the NV register and restored
/// at power-up (z3817a-3752.md, "Emulated"): set to 1, it reads 1 after a
/// power cycle, the debug lines coming before any command; set to 0 then,
/// it reads 0 after the next, with no debug line.  The bench unit's dump
/// holds 0.
#[test]
fn the_z3817a_keeps_its_debug_output_across_a_power_cycle() {
    /// The query.
    const QUERY: &str = ":DIAGnostic:SYSTem:DOUTput?";
    /// The PLL task's debug lines' start, which every run with the
    /// output on prints at power-up.
    const DEBUG_LINE: &str = "pllp: ";
    let eeprom = Scratch::new("boot-z3817a-dout");
    let board = variant("boot-z3817a-debug", &[BENCH_FEED]);
    let board = board.0.to_str().expect("a path");
    let run = |name: &str, setting: Option<u8>| {
        let set = setting
            .map(|value| {
                format!("send :DIAGnostic:SYSTem:DOUTput {value}\nexpect scpi >\\x20 within 5\n")
            })
            .unwrap_or_default();
        let steps = format!(
            "expect scpi >\\x20 within 10
send {QUERY}
expect scpi >\\x20 within 5
{set}at 20
stop
"
        );
        session_on(&eeprom.0, name, board, &steps, "30", &[]).0
    };
    let answered =
        |transcript: &str, value: u8| transcript.contains(&format!("{QUERY}\r\n{value}\r\n"));
    let set = run("boot-z3817a-dout-set", Some(1));
    assert!(answered(&set, 0), "{set}");
    let kept = run("boot-z3817a-dout-kept", Some(0));
    assert!(answered(&kept, 1), "{kept}");
    let before_query = kept.find(QUERY).expect("the query");
    assert!(kept[..before_query].contains(DEBUG_LINE), "{kept}");
    let cleared = run("boot-z3817a-dout-cleared", None);
    assert!(answered(&cleared, 0), "{cleared}");
    assert!(!cleared.contains(DEBUG_LINE), "{cleared}");
}

/// The single reading, `:DIAGnostic:PTIMe:TINTerval?` (`0x3bc2a`), and
/// the mean, `:SOURce:SYNChronization:TINTerval?` (`0x40bee`).
const SINGLE: &str = ":DIAGnostic:PTIMe:TINTerval?";
const MEAN: &str = ":SOURce:SYNChronization:TINTerval?";
/// A tick of the count, and the half tick `counter_read` adds (`0x45852`
/// to `0x458b0`), seconds.
const TICK: f64 = 1e-7;
const HALF_TICK: f64 = 5e-8;
/// How far a reading may be from a grid point and still be on it: the
/// answers' three significant figures and a float's rounding.
const ON_GRID: f64 = 1e-12;
/// The board seconds a run may go past its last sample, for the queries
/// after it, the status screen's among them, to be answered before the
/// script's `stop` ends it.
const LIMIT_MARGIN: u64 = 60;

/// Each answer in `transcript` to `command`, the line after each of its
/// echoes, that is a number: the mean is an error while its flag is
/// clear.
fn numbers(transcript: &str, command: &str) -> Vec<f64> {
    let lines: Vec<&str> = transcript.lines().collect();
    lines
        .windows(2)
        .filter(|pair| after_prompt(pair[0]) == Some(command))
        .filter_map(|pair| pair[1].trim().parse().ok())
        .collect()
}

/// Whether `value` is `offset` plus a whole number of `step`s.
fn on_grid(value: f64, offset: f64, step: f64) -> bool {
    let steps = (value - offset) / step;
    ((steps - steps.round()) * step).abs() < ON_GRID
}

/// The script's queries of the single reading and the mean every
/// `step` seconds from `from` to `to`, each to its prompt, an error's
/// too: `>` and a space, which a status screen's `>>` line has too, so
/// that a query of it waits for its last line first.
fn sampled(from: u64, to: u64, step: usize) -> String {
    (from..=to)
        .step_by(step)
        .map(|at| {
            format!(
                "at {at}
send {SINGLE}
expect >\\x20 within 5
send {MEAN}
expect >\\x20 within 5
"
            )
        })
        .collect()
}

/// The emulated log's entries on the bench's feed and the board file's
/// warm-up, after power-on: the reference valid at 5 min 16 s, from the
/// hunt, and the lock started at 14 min 11 s, from the warm-up's 784 s.
/// The bench unit's own, in the dump before them, say 00:05:19 and
/// 00:14:11 (z3817a-3752.md, "The warm-up").
const BENCH_LOG: [&str; 2] = [
    ".00:05:16:  GPS reference valid",
    ".00:14:11:  GPS lock started",
];
/// The log's entry for a power-on, the last of which is this run's.
const POWER_ON: &str = ":  Power on";
/// The sampling's step, seconds.
const SAMPLE_STEP: usize = 10;

/// What a run locked to a 1 PPS must show.
struct Locked<'a> {
    /// The TIMEREF it locks to, as the status screen names it.
    reference: &'a str,
    /// `:DIAGnostic:REFerence:STATus?`'s answer.
    status: &'a str,
    /// The log's entries it must have.
    log: &'a [&'a str],
}

/// The board file `board` run for the test `name`, a 1 PPS on its
/// selected TIMEREF, sampled every `SAMPLE_STEP` from `from` to `to`
/// seconds once locked: the module locks to `locked.reference`, the
/// status screen's ">> Locked to", `:SOURce:SYNChronization:STATe?`
/// `LOCK`, FFOM 1, `locked.status`, the self-tests passed (the FPGA's
/// counters among them, `0x45a0c`), no unit failure, no alarm and
/// `locked.log` in the log.  Each single reading is 50 ns + k x 100 ns,
/// the count's 100 ns tick and the half tick `counter_read` adds, and
/// each mean, of ten, a multiple of 10 ns; some mean between two grid
/// points shows the readings of a block differ (z3817a-3752.md, "The
/// mean").
fn locks_to_a_1pps(name: &str, board: &str, from: u64, to: u64, locked: &Locked<'_>) {
    /// The mean's step, ten readings of 100 ns.
    const MEAN_STEP: f64 = 1e-8;
    let steps = format!(
        "expect scpi >\\x20 within 10
{}at {}
send :SYSTem:STATus?
expect GPS\\x20Rcv: within 5
expect >\\x20 within 5
send :SOURce:SYNChronization:STATe?
expect >\\x20 within 5
send :SOURce:SYNChronization:FFOMerit?
expect >\\x20 within 5
send :DIAGnostic:REFerence:STATus?
expect >\\x20 within 5
send :DIAGnostic:TEST:RESult?
expect >\\x20 within 5
send :LED:ALARm?
expect >\\x20 within 5
send :DIAGnostic:LOG:READ:ALL?
expect >\\x20 within 10
stop
",
        sampled(from, to, SAMPLE_STEP),
        to + 2
    );
    let (transcript, trace) = session(
        name,
        board,
        &steps,
        &(to + LIMIT_MARGIN).to_string(),
        &["--watch-code", UNIT_FAILED],
    );
    let reference = locked.reference;
    assert!(
        transcript.contains(&format!(">> Locked to {reference}")),
        "{transcript}"
    );
    assert!(transcript.contains(&format!("[ {reference} 1PPS Valid ]")));
    assert_eq!(
        answer(&transcript, ":SOURce:SYNChronization:STATe?"),
        "LOCK"
    );
    assert_eq!(
        answer(&transcript, ":SOURce:SYNChronization:FFOMerit?"),
        "+1"
    );
    assert_eq!(
        answer(&transcript, ":DIAGnostic:REFerence:STATus?"),
        locked.status
    );
    assert_eq!(answer(&transcript, ":DIAGnostic:TEST:RESult?"), "+0,ALL");
    assert_eq!(answer(&transcript, ":LED:ALARm?"), "0");
    assert!(code_hits(&trace, UNIT_FAILED).is_empty(), "the unit failed");
    // this power-on's entries, after the dump's own
    let this_power_on = transcript
        .rfind(POWER_ON)
        .map_or("", |at| &transcript[at..]);
    for entry in locked.log {
        assert!(this_power_on.contains(entry), "{entry}: {transcript}");
    }
    let singles = numbers(&transcript, SINGLE);
    let means = numbers(&transcript, MEAN);
    let samples = usize::try_from((to - from) / SAMPLE_STEP as u64 + 1).expect("a count");
    assert_eq!((singles.len(), means.len()), (samples, samples));
    for single in &singles {
        assert!(on_grid(*single, HALF_TICK, TICK), "{single}");
    }
    for mean in &means {
        assert!(on_grid(*mean, 0.0, MEAN_STEP), "{mean}");
    }
    assert!(
        means.iter().any(|mean| !on_grid(*mean, HALF_TICK, TICK)),
        "{means:?}"
    );
}

/// A 1 PPS and VALID on TIMEREF2, PE7 high selecting it, the ready bit
/// from the first report: the bench's feed with no hunt and no warm-up,
/// so the loop locks within about five minutes (z3817a-3752.md, "The
/// lock and the interval").  `:DIAGnostic:REFerence:STATus?` answers
/// `+1,+1,1,0,1`, TIMEREF1 to the status screen as PE7 high is
/// (z3817a-3752.md, "TIMEREF1 or TIMEREF2: PE7").
#[test]
fn the_z3817a_locks_to_valid_and_a_1pps_and_reads_on_its_grid() {
    let board = variant(
        "boot-z3817a-lock",
        &[
            BENCH_FEED,
            ("select_high = 1 ", "select_high = 2 "),
            NO_WARM_UP,
        ],
    );
    locks_to_a_1pps(
        "boot-z3817a-lock",
        board.0.to_str().expect("a path"),
        340,
        430,
        &Locked {
            reference: "TIMEREF1",
            status: "+1,+1,1,0,1",
            log: &[],
        },
    );
}

/// The bench's feed (`BENCH_FEED`) and the board file's warm-up (2026-11-11 on the
/// bench: the unit locked to TIMEREF2, `:DIAGnostic:REFerence:STATus?`
/// `+2,+1,1,0,1`, single readings -50 ns, means -30, -40 and -50 ns): the
/// module hunts to TIMEREF2 and locks, the log's entries `BENCH_LOG`.
#[test]
#[ignore = "the bench's hunt and warm-up, 1200 s of board time; the short run is \
            the_z3817a_locks_to_valid_and_a_1pps_and_reads_on_its_grid"]
fn the_z3817a_locks_on_the_bench_feed_as_its_log_has_it() {
    let board = variant("boot-z3817a-bench-lock", &[BENCH_FEED]);
    locks_to_a_1pps(
        "boot-z3817a-bench-lock",
        board.0.to_str().expect("a path"),
        880,
        1200,
        &Locked {
            reference: "TIMEREF2",
            status: "+2,+1,1,0,1",
            log: &BENCH_LOG,
        },
    );
}

/// The board file's warm-up made short: the ready bit from 20 s.
const SHORT_WARM_UP: (&str, &str) = ("ready_seconds = 784 ", "ready_seconds = 20  ");
/// The status screen's line while power-up waits for the ready bit.
const WARMING: &str = ">> Power-up: OCXO warm-up";

/// The status screen's stage line at each of `seconds`, a run of
/// `board` for the test `name` with `options`, the `skip-warm-up`
/// control taken at `skipped`, if given.
fn stages(
    name: &str,
    board: &str,
    seconds: &[u64],
    skipped: Option<u64>,
    options: &[&str],
) -> Vec<String> {
    let mut steps: Vec<(u64, String)> = seconds
        .iter()
        .map(|&at| {
            (
                at,
                format!("at {at}\nsend :SYSTem:STATus?\nexpect GPS\\x20Rcv: within 10\nexpect scpi >\\x20 within 5\n"),
            )
        })
        .chain(skipped.map(|at| (at, format!("at {at}\nevent skip-warm-up\n"))))
        .collect();
    steps.sort_by_key(|(at, _)| *at);
    let steps: String = steps.into_iter().map(|(_, step)| step).collect();
    let steps = format!("expect scpi >\\x20 within 10\n{steps}stop\n");
    let last = seconds.last().copied().unwrap_or_default();
    let (transcript, _) = session(name, board, &steps, &(last + 20).to_string(), options);
    transcript
        .lines()
        .filter(|line| line.starts_with(">> "))
        .map(str::to_owned)
        .collect()
}

/// Power-up waits in its OCXO warm-up for the oscillator's ready bit,
/// `ready_seconds` into the board's warm-up; `--skip-warm-up` and the
/// `skip-warm-up` control set it at once, as they settle the other
/// boards' warm-up.
#[test]
fn the_z3817a_leaves_its_warm_up_at_the_ready_bit_or_when_skipped() {
    let board = variant("boot-z3817a-warm-up", &[SHORT_WARM_UP]);
    let board = board.0.to_str().expect("a path");
    let warming = |stages: &[String]| -> Vec<bool> {
        stages
            .iter()
            .map(|stage| stage.starts_with(WARMING))
            .collect()
    };
    let waited = stages("boot-z3817a-warm-up-waits", board, &[15, 30], None, &[]);
    assert_eq!(warming(&waited), [true, false], "{waited:?}");
    let flagged = stages(
        "boot-z3817a-warm-up-flag",
        board,
        &[12],
        None,
        &["--skip-warm-up"],
    );
    assert_eq!(warming(&flagged), [false], "{flagged:?}");
    let controlled = stages("boot-z3817a-warm-up-control", board, &[8, 14], Some(9), &[]);
    assert_eq!(warming(&controlled), [true, false], "{controlled:?}");
}

/// `counter_arm`'s call of `counter_set_sync` (`0x45760`, `0x4557a`),
/// the one write of the FPGA's sync field: the kind the byte on top of
/// the stack.
const SET_SYNC: &str = "457ea";
/// `sync_perform` (`0x4a292`), its reading the float above its return.
const SYNC_PERFORM: &str = "4a292";
/// `sync_request` (`0x4555a`), its caller's return on top of the stack.
const SYNC_REQUEST: &str = "4555a";
/// `sync_perform`'s test of the reading, which runs only with the byte
/// at `0x102a82`, a sync asked for whatever the reading, clear.
const READING_TESTED: &str = "4a29c";
/// `sync_perform`'s call of `float_compare` (`0x657f4`), the reading made
/// positive in D0 and `limit_threshold` (`0x1023ee`) in D1: it syncs
/// when D0 is the greater.
const THRESHOLD_COMPARED: &str = "4a2ae";
/// `limit_threshold`'s value, seconds: `nv_settings_defaults`' (`0x4148a`)
/// and the bench unit's NV register's, `:SOURce:ROSCillator:LIMit:THReshold`
/// (z3817a-3752.md, "The sync on the way to lock").
const LIMIT_THRESHOLD: f32 = 1e-5;
/// The returns into `interval_rearm` (`0x4a42e`) from `counter_arm`,
/// into `fine` (`0x4a6f2`) from `sync_perform` at `0x4a7a2` and from
/// `interval_rearm` at `0x4a7d8`, and into `sync_perform` and
/// `fine_sync_adjust` (`0x4a1b8`, which calls through A2: kind 1 at
/// `0x4a1f2` and `0x4a21c`, kind 2 at `0x4a24e` and `0x4a276`) from
/// `sync_request`.
const FROM_REARM: u32 = 0x4a48c;
const FROM_SYNC_PERFORM: u32 = 0x4a7a6;
const FROM_FINE_REARM: u32 = 0x4a7dc;
const FROM_PERFORM_REQUEST: u32 = 0x4a2c6;
const FROM_ADJUST_REQUESTS: [u32; 4] = [0x4a1f4, 0x4a21e, 0x4a250, 0x4a278];
/// The sync field's kinds: the steps of a clock `fine_sync_adjust` asks
/// for, for a positive reading and for a negative one, and the full sync.
const KIND_STEPS: [u8; 2] = [1, 2];
const KIND_FULL: u8 = 3;
/// When the bench-feed run's kind 3 comes, board seconds: `fine`'s
/// first reading, after the ready bit at 784 s; its steps after it all
/// come before "GPS lock started", 851 s; and the run's end.
const FULL_SYNC_FROM: f64 = 785.0;
const FULL_SYNC_TO: f64 = 792.0;
const LOCK_STARTED: f64 = 851.0;
const TRACED_TO: u64 = 860;
/// The least size of the reading `sync_perform` fully syncs on, seconds,
/// here: the free-running internal 1 PPS's place on the reference.
const FAR_OFF: f32 = 0.1;

/// `hit`'s stack as bytes, from its top.
fn stack_bytes(hit: &CodeHit) -> Vec<u8> {
    hit.stack
        .iter()
        .flat_map(|long| long.to_be_bytes())
        .collect()
}

/// The long at `offset` bytes into `bytes`.
fn long_at(bytes: &[u8], offset: usize) -> u32 {
    u32::from_be_bytes(bytes[offset..offset + 4].try_into().expect("four bytes"))
}

/// `hit`'s board second.
fn second(hit: &CodeHit) -> f64 {
    hit.cycle as f64 / CPU_HZ as f64
}

/// On the bench's feed and warm-up, the firmware writes the FPGA's sync
/// field with kind 3 once, from `fine`'s first reading: `fine` calls
/// `sync_perform` with a reading a large part of a second off, more than
/// `limit_threshold`, which it compares at 1e-5 s on each call and which
/// every later reading is under; it asks for kind 3 (`sync_request`), and `fine`'s `interval_rearm` then
/// arms the counter, whose `counter_arm` writes it.  Every later write
/// is kind 1 or 2, which `fine_sync_adjust` asks for, before the lock starts
/// (z3817a-3752.md, "The sync on the way to lock").
#[test]
#[ignore = "the bench's hunt and warm-up, 860 s of board time"]
fn the_z3817a_syncs_by_kind_3_once_on_the_way_to_lock() {
    let board = variant("boot-z3817a-sync-3", &[BENCH_FEED]);
    let steps = format!("expect scpi >\\x20 within 10\nat {TRACED_TO}\nstop\n");
    let (_, trace) = session(
        "boot-z3817a-sync-3",
        board.0.to_str().expect("a path"),
        &steps,
        &(TRACED_TO + LIMIT_MARGIN).to_string(),
        &[
            "--watch-code",
            SET_SYNC,
            "--watch-code",
            SYNC_PERFORM,
            "--watch-code",
            SYNC_REQUEST,
            "--watch-code",
            READING_TESTED,
            "--watch-code",
            THRESHOLD_COMPARED,
        ],
    );
    let writes = code_hits(&trace, SET_SYNC);
    let (full, stepped): (Vec<_>, Vec<_>) = writes
        .iter()
        .partition(|hit| stack_bytes(hit)[0] == KIND_FULL);
    let [full] = full.as_slice() else {
        panic!("{} writes of kind 3", full.len());
    };
    let bytes = stack_bytes(full);
    // the kind's word, counter_arm's D2 and A2, then its return, its two
    // arguments and interval_rearm's return
    assert_eq!(
        (long_at(&bytes, 10), long_at(&bytes, 22)),
        (FROM_REARM, FROM_FINE_REARM)
    );
    assert!(
        (FULL_SYNC_FROM..FULL_SYNC_TO).contains(&second(full)),
        "{}",
        second(full)
    );
    let (performed, later): (Vec<_>, Vec<_>) = code_hits(&trace, SYNC_PERFORM)
        .into_iter()
        .partition(|hit| hit.cycle < full.cycle);
    let [performed] = performed.as_slice() else {
        panic!("{} calls of sync_perform before it", performed.len());
    };
    assert_eq!(performed.stack[0], FROM_SYNC_PERFORM);
    let reading = f32::from_bits(performed.stack[1]);
    assert!(reading.abs() > FAR_OFF, "{reading}");
    let compared = code_hits(&trace, THRESHOLD_COMPARED);
    for hit in &compared {
        assert_eq!(f32::from_bits(hit.data[1]), LIMIT_THRESHOLD);
    }
    let (first, compared_later): (Vec<_>, Vec<_>) =
        compared.iter().partition(|hit| hit.cycle < full.cycle);
    let [first] = first.as_slice() else {
        panic!("{} tests of the reading before it", first.len());
    };
    assert_eq!(f32::from_bits(first.data[0]), reading.abs());
    assert_eq!(compared_later.len(), later.len());
    for (call, hit) in later.iter().zip(&compared_later) {
        let reading = f32::from_bits(call.stack[1]).abs();
        assert_eq!(f32::from_bits(hit.data[0]), reading);
        assert!(reading < LIMIT_THRESHOLD, "{reading}");
    }
    let requests = code_hits(&trace, SYNC_REQUEST);
    let full_requests: Vec<_> = requests
        .iter()
        .filter(|hit| stack_bytes(hit)[4] == KIND_FULL)
        .collect();
    let [request] = full_requests.as_slice() else {
        panic!("{} requests of kind 3", full_requests.len());
    };
    assert_eq!(request.stack[0], FROM_PERFORM_REQUEST);
    assert!(performed.cycle < request.cycle && request.cycle < full.cycle);
    assert!(
        code_hits(&trace, READING_TESTED)
            .iter()
            .any(|hit| performed.cycle < hit.cycle && hit.cycle < request.cycle),
        "the sync asked for whatever the reading"
    );
    assert!(!stepped.is_empty());
    for step in &stepped {
        assert!(KIND_STEPS.contains(&stack_bytes(step)[0]));
        assert!(
            (FULL_SYNC_TO..LOCK_STARTED).contains(&second(step)),
            "{}",
            second(step)
        );
    }
    for request in requests.iter().filter(|hit| hit.cycle > full.cycle) {
        assert!(
            FROM_ADJUST_REQUESTS.contains(&request.stack[0]),
            "{:x}",
            request.stack[0]
        );
    }
}

/// A PP2S and VALID on TIMEREF2, PE7 high selecting it, the ready bit
/// from the first report: the module takes the reference in PP2S mode and
/// locks, and each mean, of five readings of 2 s, is an odd multiple of
/// 10 ns, a step of 20 ns (z3817a-3752.md, "The mean").  The lock comes
/// near 530 s of board time, and the first mean between the readings'
/// grid points near 730 s, so no shorter run shows it: the PP2S mode
/// itself is `the_z3817a_switches_to_pp2s_mode_and_back`'s.
#[test]
#[ignore = "PP2S mode locks only after some 530 s of board time"]
fn the_z3817a_locks_to_a_pp2s_and_its_mean_steps_by_20_ns() {
    /// The mean's step, five readings of 100 ns, and its offset, the
    /// half tick.
    const MEAN_STEP: f64 = 2e-8;
    const MEAN_OFFSET: f64 = 1e-8;
    /// The sampling, once locked, and the run's end.
    const FROM: u64 = 560;
    const TO: u64 = 740;
    let board = variant(
        "boot-z3817a-pp2s-lock",
        &[
            BENCH_FEED,
            ("timeref2 = \"1pps\" ", "timeref2 = \"pp2s\" "),
            ("select_high = 1 ", "select_high = 2 "),
            NO_WARM_UP,
        ],
    );
    let steps = format!(
        "expect scpi >\\x20 within 10
{}at {}
send :SOURce:SYNChronization:STATe?
expect >\\x20 within 5
send :DIAGnostic:REFerence:STATus?
expect >\\x20 within 5
stop
",
        sampled(FROM, TO, SAMPLE_STEP),
        TO + 2
    );
    let (transcript, _) = session(
        "boot-z3817a-pp2s-lock",
        board.0.to_str().expect("a path"),
        &steps,
        &(TO + LIMIT_MARGIN).to_string(),
        &[],
    );
    assert_eq!(
        answer(&transcript, ":SOURce:SYNChronization:STATe?"),
        "LOCK"
    );
    assert_eq!(
        answer(&transcript, ":DIAGnostic:REFerence:STATus?"),
        "+1,+2,1,0,1"
    );
    let singles = numbers(&transcript, SINGLE);
    let means = numbers(&transcript, MEAN);
    assert!(!means.is_empty(), "{transcript}");
    for single in &singles {
        assert!(on_grid(*single, HALF_TICK, TICK), "{single}");
    }
    for mean in &means {
        assert!(on_grid(*mean, MEAN_OFFSET, MEAN_STEP), "{mean}");
    }
    assert!(
        means.iter().any(|mean| !on_grid(*mean, HALF_TICK, TICK)),
        "{means:?}"
    );
}
