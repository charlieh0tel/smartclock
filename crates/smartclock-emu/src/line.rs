//! A serial line between one of the board's ports and whoever is on
//! the far end: a TCP client, or nobody.
//!
//! The device side is the emulator's thread; the far end is a
//! connection's.  Bytes the device sends while nobody is attached are
//! lost, as on a port with nothing plugged in.

use std::collections::VecDeque;
use std::io::Read;
use std::io::Write;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::MutexGuard;

use crate::trace;

/// The bytes in flight each way, and whether a far end is attached.
#[derive(Debug, Default)]
struct Queues {
    /// Sent by the far end, not yet taken by the device.
    to_device: VecDeque<u8>,
    /// Sent by the device, not yet read by the far end.
    to_far: VecDeque<u8>,
    /// Whether a far end is attached.
    attached: bool,
}

/// One serial line, shared by the device and its far end.
#[derive(Clone)]
pub(crate) struct Line {
    /// Its name, for messages and the tap: `host`, `sci` or `gps`
    /// (`ports`).
    name: &'static str,
    /// Copy what the device sends to standard error, a line at a time.
    tap: bool,
    /// The bytes each way.
    queues: Arc<Mutex<Queues>>,
    /// What the tap has of a line not yet ended.
    tapped: Arc<Mutex<Vec<u8>>>,
    /// Where every byte the device sends is written, raw, if anywhere:
    /// natively a file (`host::Ports::capture`).
    capture: Option<Arc<Mutex<Box<dyn Write + Send>>>>,
}

impl std::fmt::Debug for Line {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter
            .debug_struct("Line")
            .field("name", &self.name)
            .field("tap", &self.tap)
            .field("queues", &self.queues)
            .field("captured", &self.capture.is_some())
            .finish_non_exhaustive()
    }
}

impl Line {
    /// A line named `name`, its output copied to standard error with
    /// `tap`.
    pub(crate) fn new(name: &'static str, tap: bool) -> Self {
        Self {
            name,
            tap,
            queues: Arc::default(),
            tapped: Arc::default(),
            capture: None,
        }
    }

    /// The same line with every byte the device sends written to
    /// `capture`, raw, as it is sent.
    pub(crate) fn capturing(mut self, capture: Box<dyn Write + Send>) -> Self {
        self.capture = Some(Arc::new(Mutex::new(capture)));
        self
    }

    /// Its name.
    pub(crate) fn name(&self) -> &'static str {
        self.name
    }

    fn queues(&self) -> MutexGuard<'_, Queues> {
        // A thread that panicked holding the lock left nothing half
        // done: each use is one push or pop.
        self.queues
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
    }

    /// The next byte the far end sent, for the device.
    pub(crate) fn receive(&self) -> Option<u8> {
        self.queues().to_device.pop_front()
    }

    /// Whether a byte waits for the device.
    pub(crate) fn waiting(&self) -> bool {
        !self.queues().to_device.is_empty()
    }

    /// A byte the device sends.
    pub(crate) fn transmit(&self, byte: u8) {
        {
            let mut queues = self.queues();
            if queues.attached {
                queues.to_far.push_back(byte);
            }
        }
        if self.tap {
            self.copy(byte);
        }
        if let Some(capture) = &self.capture {
            let written = capture
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner)
                .write_all(&[byte]);
            if let Err(error) = written {
                trace::note(format_args!("{} capture: {error}", self.name));
            }
        }
    }

    /// The tap: a line of what the device sent, escaped, at its end.
    fn copy(&self, byte: u8) {
        let mut tapped = self
            .tapped
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        if byte != b'\n' && byte != b'\r' {
            tapped.push(byte);
        }
        if byte == b'\n' || tapped.len() >= TAP_WIDTH {
            trace::note(format_args!("{} < {}", self.name, tapped.escape_ascii()));
            tapped.clear();
        }
    }

    /// Whether a far end is attached: on a TCP port, a client
    /// connected.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn connected(&self) -> bool {
        self.queues().attached
    }

    /// `bytes` for the device, if no far end is attached: a line typed
    /// on the panel page's console, where nobody is attached to type
    /// instead.  Whether they went on: checked and put on under one
    /// lock, so a TCP client that attaches meanwhile either gets the
    /// line's answer or never sees the line, not half of a session that
    /// is not its own.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn inject_unattached(&self, bytes: &[u8]) -> bool {
        let mut queues = self.queues();
        if queues.attached {
            return false;
        }
        queues.to_device.extend(bytes);
        true
    }

    /// `bytes` for the device, whoever is attached: a command the
    /// emulator gives as a client would (`--time-code-off`), after what
    /// the far end has sent.  A far end that attaches before the device
    /// takes them drops them, as it drops what an earlier one left.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn inject(&self, bytes: &[u8]) {
        self.queues().to_device.extend(bytes);
    }

    /// Attaches a far end, from which a new connection starts afresh:
    /// what an earlier one left unread either way is dropped.
    pub(crate) fn attach(&self) -> FarEnd {
        let mut queues = self.queues();
        queues.to_device.clear();
        queues.to_far.clear();
        queues.attached = true;
        FarEnd { line: self.clone() }
    }

    /// Bytes the device sent that the far end has not read, for a test.
    #[cfg(test)]
    pub(crate) fn sent(&self) -> Vec<u8> {
        self.queues().to_far.drain(..).collect()
    }
}

/// The longest tap line before it is broken.
const TAP_WIDTH: usize = 160;

/// The far end of a line, attached until dropped.
#[derive(Debug)]
pub struct FarEnd {
    /// The line.
    line: Line,
}

impl FarEnd {
    /// Whether bytes it wrote wait for the device, not yet on the line.
    pub fn unsent(&self) -> bool {
        self.line.waiting()
    }
}

impl Read for FarEnd {
    /// What the device has sent, without waiting: zero bytes when it
    /// has sent nothing.
    fn read(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
        let mut queues = self.line.queues();
        let count = buffer.len().min(queues.to_far.len());
        for (slot, byte) in buffer.iter_mut().zip(queues.to_far.drain(..count)) {
            *slot = byte;
        }
        Ok(count)
    }
}

impl Write for FarEnd {
    fn write(&mut self, buffer: &[u8]) -> std::io::Result<usize> {
        self.line.queues().to_device.extend(buffer);
        Ok(buffer.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

impl Drop for FarEnd {
    fn drop(&mut self) {
        self.line.queues().attached = false;
    }
}

#[cfg(test)]
mod tests {
    use std::io::Read;
    use std::io::Write;

    use super::Line;

    #[test]
    fn bytes_cross_each_way_while_attached() {
        let line = Line::new("host", false);
        let mut far = line.attach();
        far.write_all(b"*I").expect("written");
        assert_eq!(line.receive(), Some(b'*'));
        assert_eq!(line.receive(), Some(b'I'));
        assert_eq!(line.receive(), None);
        line.transmit(b'>');
        let mut buffer = [0; 4];
        assert_eq!(far.read(&mut buffer).expect("read"), 1);
        assert_eq!(buffer[0], b'>');
    }

    #[test]
    fn a_capture_has_every_byte_sent_raw_attached_or_not() {
        let path = std::env::temp_dir().join(format!(
            "smartclock-emu-line-capture-{}.bin",
            std::process::id()
        ));
        let file = std::fs::File::create(&path).expect("the capture made");
        let line = Line::new("sci", false).capturing(Box::new(file));
        for byte in [0x07, b'\r', b'\n', 0xff] {
            line.transmit(byte);
        }
        assert_eq!(
            std::fs::read(&path).expect("read"),
            [0x07, b'\r', b'\n', 0xff]
        );
        let _ = std::fs::remove_file(&path);
    }

    /// A line for the device goes on while nobody is attached, and not
    /// while a far end is.
    #[cfg(feature = "mc683xx")]
    #[test]
    fn an_injected_line_goes_on_only_with_nobody_attached() {
        let line = Line::new("host", false);
        assert!(line.inject_unattached(b"a"));
        let far = line.attach();
        assert!(!line.inject_unattached(b"b"));
        assert_eq!(line.receive(), None, "the attach starts afresh");
        drop(far);
        assert!(line.inject_unattached(b"c"));
        assert_eq!(line.receive(), Some(b'c'));
    }

    #[test]
    fn what_the_device_sends_with_nobody_attached_is_lost() {
        let line = Line::new("host", false);
        line.transmit(b'x');
        drop(line.attach());
        line.transmit(b'y');
        let mut far = line.attach();
        line.transmit(b'z');
        let mut buffer = [0; 4];
        assert_eq!(far.read(&mut buffer).expect("read"), 1);
        assert_eq!(buffer[0], b'z');
    }
}
