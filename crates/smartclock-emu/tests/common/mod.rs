//! What the emulator's integration tests share: the emulator run on
//! the Z3805A 3543B image, the 58503A 3704's or the Z3801A 3543's, as a
//! child process, a
//! session on its host port or a script there and the answers in its
//! transcript, and a working EEPROM file of the test's own.

// [TODO] @ch: fix allow lint; `expect` fails in the binaries that use it all
#![allow(
    dead_code,
    reason = "each test binary compiles this module and uses only part of it"
)]

use std::ffi::OsStr;
use std::io::BufRead;
use std::io::BufReader;
use std::path::Path;
use std::path::PathBuf;
use std::process::Child;
use std::process::Command;
use std::process::Stdio;
use std::sync::mpsc;
use std::thread;
use std::time::Duration;
use std::time::Instant;

use smartclock::session::Config;
use smartclock::session::Prompt;
use smartclock::session::Session;
use smartclock::transport::Transport;
use smartclock::transport::open;
use smartclock::transport::serial::Settings;

/// The CPU's clock, cycles a second, as the reset code programs it.
pub(crate) const CPU_HZ: u64 = 16_777_216;

/// How long the boot may take, on the wall clock, run flat out.
pub(crate) const BOOT_LIMIT: Duration = Duration::from_secs(120);

/// How long an answer may take: `:PTIMe:TCODe?` answers at the clock's
/// next second (timecode.c, `timecode_update`), which the FPGA's
/// internal 1 PPS ticks from about 2 s of the board's time after
/// power-up (pps.c).
pub(crate) const ANSWER_LIMIT: Duration = Duration::from_secs(60);

/// The repository's root.
fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

/// A receiver the emulator boots: its image and its EEPROM's dump, a
/// unit's or, where there is none, the synthetic image
/// (docs/emulator.md, "The EEPROM"), under the repository's root.
#[derive(Debug, Clone, Copy)]
pub(crate) struct Receiver {
    /// The flash image.
    image: &'static str,
    /// The EEPROM dump.
    dump: &'static str,
    /// Whether its host port echoes what it is sent: not the Z3801A's,
    /// as its bench unit's NV record sets it
    /// (docs/firmware/z3801a-3543.md, "How it differs from the Z3805A"),
    /// nor the 58503B's, as its dump's parameter record does.
    echo: bool,
    /// Whether its board file has an SCI, the `sci` endpoint: on
    /// mc683xx `--sci` is refused without one (boards/README.md).
    sci: bool,
}

/// The cores this build has, as `--core` names them: mc683xx, the
/// default, and Unicorn with the `unicorn` feature.
#[cfg(feature = "unicorn")]
pub(crate) const CORES: &[&str] = &[MC683XX, UNICORN];
/// The cores this build has, as `--core` names them: mc683xx, the
/// default, and Unicorn with the `unicorn` feature.
#[cfg(not(feature = "unicorn"))]
pub(crate) const CORES: &[&str] = &[MC683XX];
/// The cores, as `--core` names them.
pub(crate) const MC683XX: &str = "mc683xx";
pub(crate) const UNICORN: &str = "unicorn";

/// The Z3805A 3543B, with the bench unit's EEPROM.
pub(crate) const Z3805A: Receiver = Receiver {
    image: "third_party/firmware/images/z3805a-3543b.bin",
    dump: "third_party/firmware/images/eeprom/z3805a-3625A01487-eeprom.bin",
    echo: true,
    sci: true,
};

/// The 58503A 3704, with the bench unit's EEPROM.
pub(crate) const HP58503A: Receiver = Receiver {
    image: "third_party/firmware/images/58503a-3704.bin",
    dump: "third_party/firmware/images/eeprom/58503a-3710A01056-eeprom.bin",
    echo: true,
    sci: false,
};

/// The Z3816A 4001, with its synthetic EEPROM: there is no dump of one.
pub(crate) const Z3816A: Receiver = Receiver {
    image: "third_party/firmware/images/z3816a-4001.bin",
    dump: "third_party/firmware/images/eeprom/z3816a-synthetic-eeprom.bin",
    echo: true,
    sci: false,
};

/// The 58503B 1.01.04, with a 58503B's EEPROM, its serial number
/// replaced by L00000 and its position by HP Santa Clara Division's
/// (`tools/firmware/scrubbed_eeprom.py`).  Its port does not echo, as
/// the dump's parameter record has it, echo off.
pub(crate) const HP58503B: Receiver = Receiver {
    image: "third_party/firmware/images/58503b-1.01.04.bin",
    dump: "third_party/firmware/images/eeprom/58503b-L00000-eeprom.bin",
    echo: false,
    sci: false,
};

/// The 58503B 1.01.04, with its synthetic EEPROM, whose port echoes,
/// as the defaults have it.
pub(crate) const HP58503B_SYNTHETIC: Receiver = Receiver {
    dump: "third_party/firmware/images/eeprom/58503b-synthetic-eeprom.bin",
    echo: true,
    ..HP58503B
};

/// The Z3801A 3543, with the bench unit's EEPROM.
pub(crate) const Z3801A: Receiver = Receiver {
    image: "third_party/firmware/images/z3801a-3543.bin",
    dump: "third_party/firmware/images/eeprom/z3801a-3542A01548-eeprom.bin",
    echo: false,
    sci: false,
};

/// The Z3817A 3752, with the bench unit's EEPROM, on `boards/z3817a.toml`
/// with another model's `--model` (boards/README.md).  Its host port is
/// the SCI, which the board file binds to `host`, not `sci`.
pub(crate) const Z3817A: Receiver = Receiver {
    image: "third_party/firmware/images/z3817a-3752.bin",
    dump: "third_party/firmware/images/eeprom/z3817a-3832A03064-eeprom.bin",
    echo: true,
    sci: false,
};

/// The Z3805A 1.01.03, its EEPROM erased: the one unit's dump is not the
/// repository's (docs/emulator.md, "The boards").  Its image picks its
/// board file, `boards/z3805a-10103.toml`, with its Furuno GT-74.
pub(crate) const Z3805A_10103: Receiver = Receiver {
    image: "third_party/firmware/images/z3805a-1.01.03.bin",
    dump: "third_party/firmware/images/eeprom/z3805a-1.01.03-synthetic-eeprom.bin",
    echo: true,
    sci: true,
};

/// The Z3815A 4010, its EEPROM the synthetic image, there being no
/// dump of one, on `boards/z3815a.toml` with the Z3816A's `--model`, the
/// emulator having no Z3815A model (boards/README.md).  Its host port is
/// the SCI, which the board file binds to `host`.
pub(crate) const Z3815A: Receiver = Receiver {
    image: "third_party/firmware/images/z3815a-4010.bin",
    dump: "third_party/firmware/images/eeprom/z3815a-synthetic-eeprom.bin",
    echo: true,
    sci: false,
};

/// The repository's file under its root at `path`.
pub(crate) fn repository(path: &str) -> PathBuf {
    root().join(path)
}

impl Receiver {
    /// Its image.
    pub(crate) fn image(self) -> PathBuf {
        root().join(self.image)
    }

    /// Its EEPROM dump.
    pub(crate) fn dump(self) -> PathBuf {
        root().join(self.dump)
    }

    /// Whether its host port echoes.
    pub(crate) fn echoes(self) -> bool {
        self.echo
    }

    /// Whether the emulator serves its SCI on `core`, as `--core` names
    /// it: on the board files that have one, and on Unicorn's board,
    /// which always does.
    pub(crate) fn serves_sci(self, core: &str) -> bool {
        self.sci || core == UNICORN
    }

    /// `--sci` on a port the system picks, where the emulator serves
    /// its SCI on the core `options` name.
    fn sci_options(self, options: &[&str]) -> &'static [&'static str] {
        let core = options
            .windows(2)
            .find(|pair| pair[0] == "--core")
            .map_or(MC683XX, |pair| pair[1]);
        if self.serves_sci(core) {
            &["--sci", "127.0.0.1:0"]
        } else {
            &[]
        }
    }
}

/// The Z3805A 3543B's image.
pub(crate) fn image() -> PathBuf {
    Z3805A.image()
}

/// The Z3805A bench unit's EEPROM dump.
pub(crate) fn dump() -> PathBuf {
    Z3805A.dump()
}

/// What the emulator keeps beside an EEPROM's working file: the
/// sibling Unicorn's makes it in, and mc683xx's AT28C64B's protection
/// (mc683xx's docs/peripherals.md, AT28C64B).
const SIBLINGS: [&str; 2] = [".partial", ".sdp"];

/// A file of a test's own in the temporary directory, removed when
/// dropped, with the siblings the emulator keeps beside it.
pub(crate) struct Scratch(pub PathBuf);

impl Scratch {
    /// The file for the test `name`, not yet there.
    pub(crate) fn new(name: &str) -> Self {
        let path =
            std::env::temp_dir().join(format!("smartclock-emu-{name}-{}.bin", std::process::id()));
        let scratch = Self(path);
        scratch.remove();
        scratch
    }

    fn remove(&self) {
        let _ = std::fs::remove_file(&self.0);
        for suffix in SIBLINGS {
            let mut sibling = self.0.as_os_str().to_owned();
            sibling.push(suffix);
            let _ = std::fs::remove_file(sibling);
        }
    }
}

impl Drop for Scratch {
    fn drop(&mut self) {
        self.remove();
    }
}

/// What `line` holds after the prompt it starts with, `scpi > ` or an
/// error's `E-nnn> ` (docs/protocol.md, "The error prompt"): the echo
/// of the command sent at it.  `None` for a line that is no prompt's.
pub(crate) fn after_prompt(line: &str) -> Option<&str> {
    let rest = line.strip_prefix("scpi").or_else(|| {
        line.strip_prefix("E-")
            .map(|code| code.trim_start_matches(|c: char| c.is_ascii_digit()))
    })?;
    rest.trim_start_matches(' ').strip_prefix("> ")
}

/// The answers in `transcript` from a port that does not echo, in
/// order: what follows each prompt on its line.
pub(crate) fn unechoed_answers(transcript: &str) -> Vec<&str> {
    transcript.lines().filter_map(after_prompt).collect()
}

/// The script for the test `name`, its steps `steps`, for `--script`.
pub(crate) fn script(name: &str, steps: &str) -> Scratch {
    let script = Scratch::new(&format!("{name}-script"));
    std::fs::write(&script.0, steps).expect("the script written");
    script
}

/// The lines `transcript` gives after the first echo of `command`, up
/// to the next prompt: its answer.
pub(crate) fn answer_lines<'a>(transcript: &'a str, command: &str) -> Vec<&'a str> {
    let mut lines = transcript.lines();
    lines
        .find(|line| after_prompt(line) == Some(command))
        .unwrap_or_else(|| panic!("{command} not sent: {transcript}"));
    lines
        .take_while(|line| after_prompt(line).is_none())
        .collect()
}

/// The one line of `command`'s answer in `transcript`.
pub(crate) fn answer<'a>(transcript: &'a str, command: &str) -> &'a str {
    match answer_lines(transcript, command).as_slice() {
        [line] => line.trim_end(),
        lines => panic!("not one line in the answer to {command}: {lines:?}"),
    }
}

/// What a trace record starts with on either core: mc683xx-board's
/// `mc68331: ` and smartclock-emu's own, which Unicorn's records and
/// both cores' indicators are.
pub(crate) const RECORD_STARTS: [&str; 2] = ["mc68331: ", "smartclock-emu: "];

/// The fields of `line` if it is a stop record, on either core.
pub(crate) fn stopped(line: &str) -> Option<&str> {
    RECORD_STARTS
        .iter()
        .find_map(|start| line.strip_prefix(start))
        .and_then(|record| record.strip_prefix("stopped "))
}

/// Each reset's cause in `trace`, in order: `PowerOn`, `Watchdog` and
/// the rest, as smartclock-emu's reset records name them.
pub(crate) fn reset_causes(trace: &[String]) -> Vec<&str> {
    trace
        .iter()
        .filter_map(|line| line.strip_prefix("smartclock-emu: reset "))
        .filter_map(|fields| fields.split_once(" cause="))
        .map(|(_, cause)| cause)
        .collect()
}

/// Checks the run ended at its script's `stop`, its last record the
/// stop record.
pub(crate) fn assert_stopped(trace: &[String]) {
    let last = trace.last().expect("a trace");
    assert!(stopped(last).is_some(), "the script's stop: {last}");
}

/// The Z3805A 3543B's power-up self-test's FPGA check,
/// `selftest_fpga_with_lamp` (selftest.c, `selftest_print_failure`):
/// the first after the two that keep the host port, DUART channel B,
/// from hearing what it is sent.  The UART's check disables the
/// channel's receiver, resets it and runs it in local loopback
/// (duart.h, `duart_loopback_test`), losing what the line carries
/// meanwhile, as the board's DUART does (docs/emulator.md, "What is
/// modeled"); the QSPI's masks the interrupts about its transfers.  The
/// self-test runs some seconds of board time after the first prompt,
/// on every power-up, the run's and a restart's.
const PAST_HOST_PORT_CHECKS: u32 = 0x18cc6;

/// The emulator, killed when dropped, as a power cut stops a receiver:
/// no chance to finish what it was writing.
pub(crate) struct Emulator {
    /// Its process.
    child: Child,
    /// What it writes to standard error, a line at a time.
    pub(crate) lines: mpsc::Receiver<String>,
    /// The lines it wrote before those the harness waited for, which
    /// are not kept: the records of power-up.
    early: Vec<String>,
    /// Where it serves the host port.
    pub(crate) host: String,
}

impl Emulator {
    /// The emulator on the Z3805A's image, its EEPROM's working file
    /// `eeprom` from the bench unit's dump, flat out, with `options`,
    /// and no GPS engine: channel A served and left unconnected.
    pub(crate) fn start(eeprom: &Path, options: &[&str]) -> Self {
        Self::start_with_engine(eeprom, &[&["--gps", "127.0.0.1:0"], options].concat())
    }

    /// The same with the synthetic GPS engine on channel A.
    pub(crate) fn start_with_engine(eeprom: &Path, options: &[&str]) -> Self {
        let mut child = Command::new(env!("CARGO_BIN_EXE_smartclock-emu"))
            .arg(image())
            .arg("--eeprom")
            .arg(eeprom)
            .arg("--eeprom-dump")
            .arg(dump())
            .args([
                "--host",
                "127.0.0.1:0",
                "--sci",
                "127.0.0.1:0",
                "--deterministic",
            ])
            .args(options)
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
        let mut early = Vec::new();
        let host = await_line(&lines, &mut early, "where its host port is", |line| {
            line.strip_prefix("smartclock-emu: host port on ")
                .map(str::to_owned)
        });
        Self {
            child,
            lines,
            early,
            host,
        }
    }

    /// The same as `start`, returned once the power-up self-test is past
    /// the checks that keep the host port from hearing what it is sent
    /// (`PAST_HOST_PORT_CHECKS`): a session opened at the first prompt
    /// would have its commands land in them, or not, by how fast the
    /// host runs the board.
    pub(crate) fn start_past_self_test(eeprom: &Path, options: &[&str]) -> Self {
        let check = format!("{PAST_HOST_PORT_CHECKS:#010x}");
        let mut emulator = Self::start(eeprom, &[&["--watch-code", &check], options].concat());
        let pc = format!("pc={check}");
        await_line(
            &emulator.lines,
            &mut emulator.early,
            "the self-test is past the host port's checks",
            |line| {
                line.strip_prefix("mc68331: code ")
                    .is_some_and(|fields| fields.split(' ').any(|field| field == pc))
                    .then_some(())
            },
        );
        emulator
    }

    /// Kills it, at once.
    pub(crate) fn kill(mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }

    /// Waits for it to exit by itself: what it wrote to standard error
    /// but the lines the harness waited for.
    pub(crate) fn finish(mut self) -> Vec<String> {
        let status = self.child.wait().expect("it exits");
        assert!(status.success(), "{status}");
        let early = std::mem::take(&mut self.early);
        early.into_iter().chain(self.lines.iter()).collect()
    }

    /// A session on the host port once the firmware gives its prompt.
    pub(crate) fn session(&self) -> Session<Box<dyn Transport + Send>> {
        let started = Instant::now();
        loop {
            let settings = Settings {
                path: self.host.clone(),
                ..Settings::default()
            };
            let transport = open(&settings).expect("the host port takes a connection");
            let config = Config {
                timeout: ANSWER_LIMIT,
                ..Config::default()
            };
            let mut session = Session::new(transport, config);
            if matches!(session.sync(), Ok(Prompt::Ready)) {
                return session;
            }
            assert!(
                started.elapsed() < BOOT_LIMIT,
                "no SCPI prompt within {BOOT_LIMIT:?}"
            );
        }
    }
}

/// What `pick` takes from the first line of `lines` it takes anything
/// from, within `BOOT_LIMIT`, the lines before it kept in `early`;
/// `what` says what the emulator failed to give.
fn await_line<T>(
    lines: &mpsc::Receiver<String>,
    early: &mut Vec<String>,
    what: &str,
    mut pick: impl FnMut(&str) -> Option<T>,
) -> T {
    let started = Instant::now();
    loop {
        let line = lines
            .recv_timeout(BOOT_LIMIT.saturating_sub(started.elapsed()))
            .unwrap_or_else(|error| panic!("the emulator says {what}: {error}"));
        if let Some(picked) = pick(&line) {
            return picked;
        }
        early.push(line);
    }
}

/// What a run wrote: to standard output, and to standard error a line
/// at a time.
pub(crate) type Output = (Vec<u8>, Vec<String>);

/// The emulator run to its end on the Z3805A's image, its EEPROM's
/// working file `eeprom` from the bench unit's dump, flat out, with
/// `options` and no GPS engine, serving no host port: what it wrote to
/// standard output, and to standard error a line at a time.
pub(crate) fn run(eeprom: &Path, options: &[&str]) -> Output {
    run_receiver(Z3805A, eeprom, options)
}

/// The same on `receiver`'s image and dump.
pub(crate) fn run_receiver(receiver: Receiver, eeprom: &Path, options: &[&str]) -> Output {
    launch(
        receiver,
        eeprom,
        &receiver.dump(),
        &[&["--gps", "127.0.0.1:0"], options].concat(),
    )
}

/// The same as `run` with the synthetic GPS engine on channel A.
pub(crate) fn run_with_engine(eeprom: &Path, options: &[&str]) -> Output {
    run_receiver_with_engine(Z3805A, eeprom, options)
}

/// The same on `receiver`'s image and dump.
pub(crate) fn run_receiver_with_engine(
    receiver: Receiver,
    eeprom: &Path,
    options: &[&str],
) -> Output {
    launch(receiver, eeprom, &receiver.dump(), options)
}

/// The same with its EEPROM erased: the working file `eeprom` from an
/// empty dump, which the part pads erased (docs/emulator.md, "The
/// EEPROM").
pub(crate) fn run_receiver_erased(receiver: Receiver, eeprom: &Path, options: &[&str]) -> Output {
    let erased = Scratch::new(&format!(
        "erased-{}",
        eeprom
            .file_stem()
            .and_then(OsStr::to_str)
            .unwrap_or_default()
    ));
    std::fs::write(&erased.0, []).expect("an empty dump");
    launch(receiver, eeprom, &erased.0, options)
}

/// The emulator run to its end on `receiver`, its EEPROM's working file
/// `eeprom` from `dump`, with `options`.
fn launch(receiver: Receiver, eeprom: &Path, dump: &Path, options: &[&str]) -> Output {
    let output = Command::new(env!("CARGO_BIN_EXE_smartclock-emu"))
        .arg(receiver.image())
        .arg("--eeprom")
        .arg(eeprom)
        .arg("--eeprom-dump")
        .arg(dump)
        .args(receiver.sci_options(options))
        .arg("--deterministic")
        .args(options)
        .output()
        .expect("the emulator runs");
    let errors = String::from_utf8_lossy(&output.stderr);
    assert!(
        output.status.success(),
        "{}: {}",
        output.status,
        errors.lines().last().unwrap_or_default()
    );
    let errors = errors.lines().map(str::to_owned).collect();
    (output.stdout, errors)
}

impl Drop for Emulator {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}
