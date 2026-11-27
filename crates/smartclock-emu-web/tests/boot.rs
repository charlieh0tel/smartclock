//! The browser's run, natively: the 58503A booted from files in memory,
//! its EEPROM's the bench unit's dump, stepped a frame at a time
//! to a clock the test keeps, its panel page answered, its console
//! typed at and answering `*IDN?`.

use std::path::Path;
use std::time::Duration;

use smartclock::http::Method;
use smartclock::http::Request;
use smartclock::http::Status;
use smartclock_emu::files::InMemory;
use smartclock_emu::mc683xx::Step;
use smartclock_emu_web::emulator::Emulator;

/// The repository's root.
const ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");
/// The files the run reads, by their paths in the repository, which the
/// static build keeps: the board file, the image, and the EEPROM's dump
/// the board file names.
const BOARD: &str = "boards/58503a.toml";
const IMAGE: &str = "third_party/firmware/images/58503a-3704.bin";
const DUMP: &str = "third_party/firmware/images/eeprom/58503a-3710A01056-eeprom.bin";

/// How far the test's clock goes a frame: the most a frame runs.
const FRAME: Duration = Duration::from_millis(50);
/// The most board time the test gives the run for each thing it waits
/// for, frames of `FRAME`: twenty seconds.
const FRAMES: usize = 400;

/// UTC as the run starts.
const START: &str = "2026-10-22T12:00:00Z";

/// The console's client the test reads and types as.
const CLIENT: &str = "test";

/// The files the page gives the run.
fn files() -> InMemory {
    files_of(&[BOARD, IMAGE, DUMP])
}

/// The files at `paths` in the repository, at those paths.
fn files_of(paths: &[&str]) -> InMemory {
    paths.iter().fold(InMemory::default(), |files, path| {
        let bytes = std::fs::read(Path::new(ROOT).join(path)).expect("a file");
        files.with(Path::new(path), bytes)
    })
}

/// The 58503A's run in a browser, started with `arguments` after the
/// image, from `files`.
fn started_with(arguments: &[&str], files: InMemory) -> Emulator {
    let arguments: Vec<String> = [IMAGE]
        .iter()
        .chain(arguments)
        .map(|&argument| argument.to_owned())
        .collect();
    Emulator::start(&arguments, files, START.parse().expect("a time")).expect("started")
}

/// The 58503A's run in a browser, started.
fn started() -> Emulator {
    started_with(&[], files())
}

/// The answer to `method` on `target`, its body `body`, which must be a
/// 200, as text.
fn ask(emulator: &Emulator, method: Method, target: &str, body: &[u8]) -> String {
    let response = emulator.respond(&Request {
        method,
        target: target.to_owned(),
        body: body.to_vec(),
    });
    assert_eq!(response.status, Status::Ok, "{target}: {}", response.body);
    response.body
}

/// The console's output since its start, as JSON.
fn console(emulator: &Emulator) -> String {
    ask(
        emulator,
        Method::Get,
        &format!("/console?since=0&client={CLIENT}"),
        &[],
    )
}

/// Frames run, the clock going `FRAME` a frame, until the console has
/// `text`.
fn frames_until(emulator: &mut Emulator, wall: &mut Duration, text: &str) {
    for _ in 0..FRAMES {
        *wall += FRAME;
        let step = emulator.frame(*wall).expect("a frame");
        assert!(matches!(step, Step::Running { .. }), "{step:?}");
        if console(emulator).contains(text) {
            return;
        }
    }
    panic!("no {text:?} on the console: {}", console(emulator));
}

#[test]
fn the_58503a_boots_in_memory_and_answers_its_identity_on_the_pages_console() {
    let mut emulator = started();
    let mut wall = Duration::ZERO;
    assert!(ask(&emulator, Method::Get, "/", &[]).contains("<title>"));
    assert!(ask(&emulator, Method::Get, "/state", &[]).contains("\"display\""));
    frames_until(&mut emulator, &mut wall, "scpi > ");
    assert_eq!(
        ask(
            &emulator,
            Method::Post,
            &format!("/console?client={CLIENT}"),
            b"*IDN?"
        ),
        "queued\n"
    );
    frames_until(
        &mut emulator,
        &mut wall,
        "HEWLETT-PACKARD,58503A,3710A01056",
    );
    let kept = emulator.eeprom().expect("the EEPROM").expect("in memory");
    let dump = std::fs::read(Path::new(ROOT).join(DUMP)).expect("the dump");
    assert_eq!(kept.cells.len(), dump.len(), "the part's 8 KiB");
    assert!(!kept.protected);
}

/// What a run keeps of its EEPROM starts the next, as the page gives
/// it: the cells as the board file's image, the protection as
/// `--eeprom-protected`.
#[test]
fn the_eeprom_kept_starts_the_next_run() {
    let cells = vec![0x5a; 16];
    let files = files().with(Path::new(DUMP), cells.clone());
    let mut emulator = started_with(&["--eeprom-protected", "true"], files);
    let kept = emulator.eeprom().expect("the EEPROM").expect("in memory");
    assert_eq!(kept.cells[..cells.len()], cells, "the cells kept");
    assert!(kept.protected, "the protection kept");
}

#[test]
fn the_runs_arguments_and_files_are_checked() {
    let start = START.parse().expect("a time");
    let missing = Emulator::start(&[IMAGE].map(str::to_owned), InMemory::default(), start)
        .expect_err("no files");
    assert!(
        missing.to_string().contains("not among the files given"),
        "{missing}"
    );
    let refused = Emulator::start(
        &[IMAGE, "--gps-latitude", "91"].map(str::to_owned),
        files(),
        start,
    )
    .expect_err("refused");
    assert!(refused.chain().contains("latitude"), "{}", refused.chain());
    let no_dump = Emulator::start(
        &[IMAGE].map(str::to_owned),
        files_of(&[BOARD, IMAGE]),
        start,
    )
    .expect_err("no dump");
    assert!(
        no_dump.chain().contains(&format!(
            "reading {}",
            Path::new("boards").join(format!("../{DUMP}")).display()
        )),
        "{}",
        no_dump.chain()
    );
}
