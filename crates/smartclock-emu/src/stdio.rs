//! The host port on standard input and output, the default
//! (docs/emulator.md, "Running it"): what the port sends goes to
//! standard output, and nothing else does; standard input goes to the
//! port.
//!
//! - From a terminal, a byte as it is typed, the terminal raw and not
//!   echoing (the firmware echoes), Enter sent as CR LF, the line end
//!   `smartclock::session` sends: the firmware's line reader ends a
//!   line at CR or LF and skips the other half of a CR LF or LF CR
//!   (duart.h, `duart_gets`), so CR LF ends one line, as CR or LF alone
//!   would.  The terminal keeps its signals, so that ^C stops the
//!   emulator, and is put back on every way out: the end of the run,
//!   an error, SIGINT, SIGTERM or a panic.  ^Z puts it back while the
//!   emulator is stopped, and it is made raw again when it goes on.
//! - From a pipe or a file, its bytes as they are, one put on the line
//!   each time the line has taken the last, so they go back to back at
//!   the line's rate, from the port's first output on: a byte before
//!   the firmware's receiver is enabled is lost, as on the board, and
//!   the firmware's power-up prompt says it is.  The read waits for
//!   the next byte, so the bytes reach the board at the same board
//!   times however the pipe delivers them, and a deterministic run
//!   repeats.  Once input ends, the run stops when the port has been
//!   idle for `DRAIN` of board time since its last output or the last
//!   byte given, with the trace's stopped record.

use std::io::BufReader;
use std::io::Read;
use std::io::Stdin;
use std::io::Write;
use std::sync::mpsc;
use std::sync::mpsc::Receiver;

use smartclock_emu::board::time::CPU_HZ;
use smartclock_emu::board::time::Cycles;
use smartclock_emu::line::FarEnd;
use smartclock_emu::run::Alongside;
use smartclock_emu::run::Flow;

/// The board time the port must be idle, once input has ended, before
/// the run stops: two seconds, past the longest the firmware takes to
/// begin an answer, `:PTIMe:TCODe?`'s wait for the clock's next second
/// (timecode.c, `timecode_update`), and the next prompt with it.
const DRAIN: Cycles = Cycles(2 * CPU_HZ);

/// What Enter sends from a terminal.
const LINE_END: &[u8] = b"\r\n";
const CR: u8 = b'\r';
const LF: u8 = b'\n';

/// What standard input is.
enum Input {
    /// A terminal, raw: the bytes typed, from a thread that reads them.
    Terminal(Receiver<u8>),
    /// A pipe or a file, read a byte at a time as the line wants one.
    Stream(BufReader<Stdin>),
}

/// The host port on standard input and output.
pub(crate) struct Stdio {
    /// Its end of the host port.
    far: FarEnd,
    /// Standard input.
    input: Input,
    /// Whether the port has sent anything: the firmware's receiver is on.
    heard: bool,
    /// When the port last sent, or a byte was last given it.
    last: Cycles,
    /// Whether input has ended.
    ended: bool,
}

impl std::fmt::Debug for Stdio {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter
            .debug_struct("Stdio")
            .field("heard", &self.heard)
            .field("ended", &self.ended)
            .finish_non_exhaustive()
    }
}

/// What a byte typed at a terminal sends: Enter, CR or LF, as CR LF.
fn typed(byte: u8) -> Vec<u8> {
    match byte {
        CR | LF => LINE_END.to_vec(),
        _ => vec![byte],
    }
}

impl Stdio {
    /// The host port on `far`, standard input a terminal when
    /// `terminal`, which the caller has made raw.
    pub(crate) fn new(far: FarEnd, terminal: bool) -> Self {
        let input = if terminal {
            let (sender, receiver) = mpsc::channel();
            std::thread::spawn(move || {
                for byte in std::io::stdin().lock().bytes().map_while(Result::ok) {
                    if sender.send(byte).is_err() {
                        break;
                    }
                }
            });
            Input::Terminal(receiver)
        } else {
            Input::Stream(BufReader::new(std::io::stdin()))
        };
        Self {
            far,
            input,
            heard: false,
            last: Cycles(0),
            ended: false,
        }
    }

    /// What the port sent, to standard output.
    fn listen(&mut self, now: Cycles) {
        let mut buffer = [0; 256];
        while let Ok(count @ 1..) = self.far.read(&mut buffer) {
            let mut stdout = std::io::stdout().lock();
            // output nobody reads is no reason to stop
            let _ = stdout.write_all(&buffer[..count]);
            let _ = stdout.flush();
            self.heard = true;
            self.last = now;
        }
    }

    /// Standard input to the port.
    fn feed(&mut self, now: Cycles) {
        match &mut self.input {
            // what has been typed; a terminal closed leaves the run going
            Input::Terminal(typed_bytes) => {
                while let Ok(byte) = typed_bytes.try_recv() {
                    // the line takes everything; a write cannot fail
                    let _ = self.far.write_all(&typed(byte));
                }
            }
            Input::Stream(stream) => {
                if !self.heard || self.ended || self.far.unsent() {
                    return;
                }
                let mut byte = [0];
                match stream.read(&mut byte) {
                    Ok(1..) => {
                        let _ = self.far.write_all(&byte);
                    }
                    // the end, or an input that cannot be read
                    Ok(0) | Err(_) => self.ended = true,
                }
                self.last = self.last.max(now);
            }
        }
    }
}

impl Alongside for Stdio {
    fn advance(&mut self, now: Cycles) -> Flow {
        self.listen(now);
        self.feed(now);
        if self.ended && !self.far.unsent() && now >= self.last + DRAIN {
            Flow::Stop
        } else {
            Flow::Continue
        }
    }

    /// The drain's end, once input has ended.
    fn next_event(&self) -> Option<Cycles> {
        self.ended.then_some(self.last + DRAIN)
    }
}

/// Standard input as a raw terminal, when it is one, put back on every
/// way out, and while the emulator is stopped.
#[cfg(unix)]
pub(crate) mod terminal {
    use std::io::IsTerminal;
    use std::os::fd::AsFd;
    use std::sync::Mutex;

    use nix::sys::termios::InputFlags;
    use nix::sys::termios::LocalFlags;
    use nix::sys::termios::SetArg;
    use nix::sys::termios::SpecialCharacterIndices;
    use nix::sys::termios::Termios;
    use nix::sys::termios::tcgetattr;
    use nix::sys::termios::tcsetattr;
    use signal_hook::consts::SIGCONT;
    use signal_hook::consts::SIGINT;
    use signal_hook::consts::SIGTERM;
    use signal_hook::consts::SIGTSTP;
    use signal_hook::iterator::Signals;
    use signal_hook::low_level::emulate_default_handler;

    /// A terminal's settings as they were, and as the emulator has them.
    #[derive(Debug, Clone)]
    struct Settings {
        /// Before it was made raw.
        saved: Termios,
        /// Raw.
        raw: Termios,
    }

    impl Settings {
        /// The settings `saved` and the raw ones made from them: no
        /// line editing, no echo, no CR to LF, no XON/XOFF, a byte a
        /// read, the signals kept.
        fn of(saved: Termios) -> Self {
            let mut raw = saved.clone();
            raw.local_flags &=
                !(LocalFlags::ICANON | LocalFlags::ECHO | LocalFlags::ECHONL | LocalFlags::IEXTEN);
            raw.input_flags &=
                !(InputFlags::ICRNL | InputFlags::INLCR | InputFlags::IGNCR | InputFlags::IXON);
            raw.control_chars[SpecialCharacterIndices::VMIN as usize] = 1;
            raw.control_chars[SpecialCharacterIndices::VTIME as usize] = 0;
            Self { saved, raw }
        }

        /// `terminal` as it was, while the emulator is stopped or once it
        /// is done.
        fn suspend(&self, terminal: impl AsFd) {
            // a terminal gone is nothing to put back
            let _ = tcsetattr(terminal, SetArg::TCSANOW, &self.saved);
        }

        /// `terminal` raw again, the emulator going on.
        fn resume(&self, terminal: impl AsFd) {
            // in the background a terminal stops the emulator until it is
            // brought back, which goes on again here
            let _ = tcsetattr(terminal, SetArg::TCSANOW, &self.raw);
        }
    }

    /// Standard input's settings, while it is raw.
    static SETTINGS: Mutex<Option<Settings>> = Mutex::new(None);

    /// A process's exit status for a signal: 128 and its number.
    const SIGNALED: i32 = 128;

    /// Standard input's settings, while it is raw.
    fn current() -> Option<Settings> {
        SETTINGS
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .clone()
    }

    /// Puts the terminal back, once.
    fn restore() {
        let settings = SETTINGS
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .take();
        if let Some(settings) = settings {
            settings.suspend(std::io::stdin());
        }
    }

    /// Puts the terminal back when dropped.
    #[derive(Debug)]
    pub(crate) struct Raw;

    impl Drop for Raw {
        fn drop(&mut self) {
            restore();
        }
    }

    /// Makes standard input raw if it is a terminal (`Settings::of`);
    /// the terminal put back when the result drops, on SIGINT and
    /// SIGTERM, and on a panic, and while SIGTSTP stops the emulator,
    /// made raw again at SIGCONT.
    pub(crate) fn raw() -> std::io::Result<Option<Raw>> {
        let stdin = std::io::stdin();
        if !stdin.is_terminal() {
            return Ok(None);
        }
        let settings = Settings::of(tcgetattr(&stdin)?);
        let raw = settings.raw.clone();
        *SETTINGS
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner) = Some(settings);
        let previous = std::panic::take_hook();
        std::panic::set_hook(Box::new(move |info| {
            restore();
            previous(info);
        }));
        let mut signals = Signals::new([SIGINT, SIGTERM, SIGTSTP, SIGCONT])?;
        std::thread::spawn(move || {
            for signal in signals.forever() {
                match signal {
                    SIGTSTP => {
                        if let Some(settings) = current() {
                            settings.suspend(std::io::stdin());
                        }
                        // stopped here, as SIGTSTP's own action stops it
                        let _ = emulate_default_handler(SIGTSTP);
                    }
                    SIGCONT => {
                        if let Some(settings) = current() {
                            settings.resume(std::io::stdin());
                        }
                    }
                    _ => {
                        restore();
                        std::process::exit(SIGNALED + signal);
                    }
                }
            }
        });
        tcsetattr(&stdin, SetArg::TCSANOW, &raw)?;
        Ok(Some(Raw))
    }

    #[cfg(test)]
    mod tests {
        use nix::pty::openpty;
        use nix::sys::termios::LocalFlags;
        use nix::sys::termios::tcgetattr;

        use super::Settings;

        /// A stop puts a terminal back as it was, and going on makes it
        /// raw again: on a pseudoterminal, as ^Z and `fg` would.
        #[test]
        fn a_stop_puts_the_terminal_back_and_going_on_makes_it_raw_again() {
            let pty = openpty(None, None).expect("a pseudoterminal");
            let terminal = pty.slave;
            let settings = Settings::of(tcgetattr(&terminal).expect("its settings"));
            let canonical = |terminal| {
                tcgetattr(terminal)
                    .expect("its settings")
                    .local_flags
                    .contains(LocalFlags::ICANON | LocalFlags::ECHO)
            };
            assert!(canonical(&terminal), "a new terminal is cooked");
            settings.resume(&terminal);
            assert!(!canonical(&terminal), "raw");
            settings.suspend(&terminal);
            assert!(canonical(&terminal), "stopped: cooked");
            settings.resume(&terminal);
            assert!(!canonical(&terminal), "going on: raw again");
        }
    }
}

/// Elsewhere standard input is taken as it comes.
#[cfg(not(unix))]
pub(crate) mod terminal {
    /// Nothing to put back.
    #[derive(Debug)]
    pub(crate) struct Raw;

    /// Never raw.
    pub(crate) fn raw() -> std::io::Result<Option<Raw>> {
        Ok(None)
    }
}

#[cfg(test)]
mod tests {
    use super::typed;

    #[test]
    fn enter_is_cr_lf_and_the_rest_as_typed() {
        assert_eq!(typed(b'\r'), b"\r\n");
        assert_eq!(typed(b'\n'), b"\r\n");
        assert_eq!(typed(b'*'), b"*");
        assert_eq!(typed(0x08), [0x08], "backspace, the firmware's to act on");
        assert_eq!(typed(0xff), [0xff]);
    }
}
