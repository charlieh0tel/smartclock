//! The simulated receiver behind a [`Transport`].
//!
//! Framing is the part worth simulating.  The receiver echoes what it
//! is sent and ends every exchange with a prompt that reflects its error
//! queue: `scpi > ` while it is empty, `E-nnn> ` while it holds
//! anything.  Those are what the session layer exists to handle, and a
//! simulator that skipped them would test nothing that matters.

use std::collections::VecDeque;
use std::io::Read;
use std::io::Write;
use std::sync::Arc;
use std::sync::Mutex;

use smartclock::transport::Transport;

use crate::receiver::PROMPT;
use crate::receiver::Receiver;

/// Longest command the simulator will accumulate before giving up on
/// it.  Generous next to any real command.
const MAX_PARTIAL: usize = 4096;

/// A simulated receiver on the other end of a transport.
#[derive(Debug, Clone)]
pub struct SimTransport {
    receiver: Arc<Mutex<Receiver>>,
    /// Bytes the caller has yet to read.
    pending: Arc<Mutex<VecDeque<u8>>>,
    /// The command being typed, up to its terminator.
    partial: Arc<Mutex<String>>,
    /// Whether what is sent comes back: the receiver's own habit
    /// (`Receiver::echo`) unless overridden.
    echo: bool,
}

impl Default for SimTransport {
    fn default() -> Self {
        Self::new(Receiver::default())
    }
}

impl SimTransport {
    /// Wrap a receiver, echoing as that receiver does.
    pub fn new(receiver: Receiver) -> Self {
        let echo = receiver.echo;
        let transport = Self {
            receiver: Arc::new(Mutex::new(receiver)),
            pending: Arc::new(Mutex::new(VecDeque::new())),
            partial: Arc::new(Mutex::new(String::new())),
            echo,
        };
        // A real receiver is already sitting at a prompt when something
        // connects to it.
        transport.emit(PROMPT);
        transport
    }

    /// This transport with its echo on or off, whatever the receiver's.
    pub fn with_echo(mut self, echo: bool) -> Self {
        self.echo = echo;
        self
    }

    /// The receiver, for a test that wants to change or inspect it.
    pub fn receiver(&self) -> &Arc<Mutex<Receiver>> {
        &self.receiver
    }

    fn emit(&self, text: &str) {
        self.pending
            .lock()
            .expect("pending mutex")
            .extend(text.as_bytes());
    }

    /// Handle one complete command line.
    fn run(&self, command: &str) {
        let answer = self
            .receiver
            .lock()
            .expect("receiver mutex")
            .respond(command);
        for line in &answer.lines {
            self.emit(line);
            self.emit("\r\n");
        }
        let prompt = self.receiver.lock().expect("receiver mutex").prompt();
        self.emit(&prompt);
    }
}

impl Read for SimTransport {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        let mut pending = self.pending.lock().expect("pending mutex");
        let n = buf.len().min(pending.len());
        for slot in buf.iter_mut().take(n) {
            *slot = pending.pop_front().expect("length checked");
        }
        Ok(n)
    }
}

impl Write for SimTransport {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        // Echo, as the receiver does, a character at a time as they
        // arrive.
        if self.echo {
            self.emit(&String::from_utf8_lossy(buf));
        }

        let mut partial = self.partial.lock().expect("partial mutex");
        // A client that never sends a newline would otherwise grow this
        // without limit, and each write would rescan more of it.  The
        // receiver has a finite input buffer too; overrunning it
        // discards the line rather than the simulator's memory.
        if partial.len() + buf.len() > MAX_PARTIAL {
            partial.clear();
            let prompt = self.receiver.lock().expect("receiver mutex").overrun();
            self.emit("\r\n");
            self.emit(&prompt);
            return Ok(buf.len());
        }
        partial.push_str(&String::from_utf8_lossy(buf));
        // A command ends at a newline; carriage returns are part of the
        // terminator, not of the command.
        while let Some(at) = partial.find('\n') {
            let line: String = partial.drain(..=at).collect();
            let command = line.trim_end_matches(['\r', '\n']).to_owned();
            drop(partial);
            self.run(&command);
            partial = self.partial.lock().expect("partial mutex");
        }
        Ok(buf.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

impl Transport for SimTransport {
    fn describe(&self) -> String {
        "a simulated receiver".to_owned()
    }
}

#[cfg(test)]
mod tests {
    use std::io::Read as _;
    use std::io::Write as _;

    use super::SimTransport;
    use crate::receiver::Receiver;

    /// Everything the transport has to say after `command`.
    fn exchange(mut transport: SimTransport, command: &str) -> String {
        let mut drain = [0u8; 256];
        let _ = transport.read(&mut drain).expect("the first prompt");
        transport.write_all(command.as_bytes()).expect("write");
        let mut out = vec![0u8; 4096];
        let n = transport.read(&mut out).expect("read");
        String::from_utf8_lossy(&out[..n]).into_owned()
    }

    #[test]
    fn a_receiver_echoes_as_its_model_does_unless_told_otherwise() {
        let echoed = |text: &str| text.starts_with("*IDN?");
        assert!(echoed(&exchange(
            SimTransport::new(Receiver::default()),
            "*IDN?\r\n"
        )));
        assert!(!echoed(&exchange(
            SimTransport::new(Receiver::z3801a()),
            "*IDN?\r\n"
        )));
        assert!(echoed(&exchange(
            SimTransport::new(Receiver::z3801a()).with_echo(true),
            "*IDN?\r\n"
        )));
    }
}
