//! A source that reads a recording.
//!
//! A [`Replay`] pulls bytes from a reader and hands them to an
//! assembler, which builds each second's record out of them; the first
//! record in the recording is tick 0's, the next tick 1's.  What format
//! the bytes are in is the assembler's affair: this crate has none of
//! its own yet, since no recording format is among what a `SmartClock`
//! sends or decodes.
//!
//! Over a slice, or any reader that can be copied, a replay is
//! [`Driven`](Clocking::Driven): asked for an earlier tick it starts
//! again from a copy it kept and reads forward.  Over a reader that
//! cannot go back it is [`Sequential`](Clocking::Sequential).  Either
//! way it holds no record and no buffer of its own.

use crate::record::Record;
use crate::source::Clocking;
use crate::source::Presence;
use crate::source::Source;
use crate::source::Tick;

/// Where a replay's bytes come from.
pub trait Read {
    /// The next byte, `None` at the end.
    fn byte(&mut self) -> Option<u8>;
}

impl Read for &[u8] {
    fn byte(&mut self) -> Option<u8> {
        let (&first, rest) = self.split_first()?;
        *self = rest;
        Some(first)
    }
}

/// What makes records of a recording's bytes.
pub trait Assemble {
    /// Takes the next byte.  `true` when it completes a second, whose
    /// record is then in `record`.  `record` is [`Record::EMPTY`] at
    /// each second's first byte and not touched by others between.
    fn push(&mut self, byte: u8, record: &mut Record) -> bool;

    /// Forgets what it has of a second in progress: the next byte is
    /// the recording's first.
    fn reset(&mut self);
}

/// A recording, replayed a second a tick.
///
/// Reaching a tick means reading every record before it, so a jump far
/// ahead, or any step back, costs the bytes between.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Replay<R, A> {
    /// The reader at the recording's start, kept to go back to; `None`
    /// if it cannot go back.
    origin: Option<R>,
    /// The reader, where it has got to.
    reader: R,
    /// What makes records of the bytes.
    assembler: A,
    /// The tick of the next record the reader will give.
    next: u64,
}

impl<R: Copy, A> Replay<R, A> {
    /// A recording that can be read again from its start.  `assembler`
    /// is to be as [`Assemble::reset`] leaves it.
    #[must_use]
    pub const fn new(reader: R, assembler: A) -> Self {
        Self {
            origin: Some(reader),
            reader,
            assembler,
            next: 0,
        }
    }
}

impl<R, A> Replay<R, A> {
    /// A recording that can only be read forward.  `assembler` is to be
    /// as [`Assemble::reset`] leaves it.
    #[must_use]
    pub const fn forward(reader: R, assembler: A) -> Self {
        Self {
            origin: None,
            reader,
            assembler,
            next: 0,
        }
    }
}

impl<R: Read, A: Assemble> Replay<R, A> {
    /// Reads the next record.  `false` at the recording's end.
    fn advance(&mut self, record: &mut Record) -> bool {
        *record = Record::EMPTY;
        while let Some(byte) = self.reader.byte() {
            if self.assembler.push(byte, record) {
                self.next = self.next.saturating_add(1);
                return true;
            }
        }
        false
    }
}

impl<R: Read + Clone, A: Assemble> Source for Replay<R, A> {
    fn clocking(&self) -> Clocking {
        if self.origin.is_some() {
            Clocking::Driven
        } else {
            Clocking::Sequential
        }
    }

    /// [`Silent`](Presence::Silent) past the recording's end, and for a
    /// tick behind a reader that cannot go back.
    fn record(&mut self, tick: Tick, record: &mut Record) -> Presence {
        if tick.0 < self.next {
            let Some(origin) = &self.origin else {
                return Presence::Silent;
            };
            self.reader = origin.clone();
            self.assembler.reset();
            self.next = 0;
        }
        while self.next <= tick.0 {
            if !self.advance(record) {
                return Presence::Silent;
            }
        }
        Presence::Fresh
    }
}

#[cfg(test)]
mod tests {
    use super::Assemble;
    use super::Read;
    use super::Replay;
    use crate::record::GpsTime;
    use crate::record::Record;
    use crate::source::Clocking;
    use crate::source::Driver;
    use crate::source::Presence;
    use crate::source::Refusal;
    use crate::source::Source;
    use crate::source::Tick;
    use crate::source::admit;
    use crate::time::GpsSeconds;
    use crate::units::Nanoseconds;
    use crate::units::Seconds;
    use std::sync::Mutex;

    /// A recording format for these tests: a second is two bytes, its
    /// GPS time, high byte first.
    #[derive(Debug, Clone, Copy)]
    struct Pairs {
        /// The first byte of a second in progress.
        high: Option<u8>,
    }

    impl Pairs {
        const NEW: Self = Self { high: None };
    }

    impl Assemble for Pairs {
        fn push(&mut self, byte: u8, record: &mut Record) -> bool {
            let Some(high) = self.high.take() else {
                self.high = Some(byte);
                return false;
            };
            record.time = Some(GpsTime {
                seconds: GpsSeconds::from_epoch(Seconds(i64::from(u16::from_be_bytes([
                    high, byte,
                ])))),
                subsecond: Nanoseconds(0),
            });
            true
        }

        fn reset(&mut self) {
            self.high = None;
        }
    }

    /// Seconds 100, 101, 102 and 103, and half of another.
    static RECORDING: [u8; 9] = [0, 100, 0, 101, 0, 102, 0, 103, 0];

    /// A replay of a slice is built in a `static` too.
    static REPLAY: Mutex<Replay<&[u8], Pairs>> =
        Mutex::new(Replay::new(RECORDING.as_slice(), Pairs::NEW));

    fn seconds(source: &mut impl Source, tick: u64) -> Option<i64> {
        let mut record = Record::EMPTY;
        match source.record(Tick(tick), &mut record) {
            Presence::Fresh => Some(record.time.unwrap().seconds.since_epoch().0),
            Presence::Silent => None,
        }
    }

    #[test]
    fn a_slice_is_read_a_byte_at_a_time() {
        let mut bytes: &[u8] = &[1, 2];
        assert_eq!(bytes.byte(), Some(1));
        assert_eq!(bytes.byte(), Some(2));
        assert_eq!(bytes.byte(), None);
        assert_eq!(bytes.byte(), None);
    }

    /// Over a slice any tick can be asked for at any time; past the
    /// end, and in a second cut short, there is silence.
    #[test]
    fn a_replay_of_a_slice_seeks() {
        let mut replay = REPLAY.lock().unwrap();
        assert_eq!(replay.clocking(), Clocking::Driven);
        assert_eq!(
            admit("recording", replay.clocking(), Driver::Restoring),
            Ok(())
        );

        assert_eq!(seconds(&mut *replay, 0), Some(100));
        assert_eq!(seconds(&mut *replay, 1), Some(101));
        // Ahead, skipping one.
        assert_eq!(seconds(&mut *replay, 3), Some(103));
        // Back, and the same tick again.
        assert_eq!(seconds(&mut *replay, 1), Some(101));
        assert_eq!(seconds(&mut *replay, 1), Some(101));
        // The fifth second has one byte of its two.
        assert_eq!(seconds(&mut *replay, 4), None);
        assert_eq!(seconds(&mut *replay, 400), None);
        // Back from the end, with half a second forgotten.
        assert_eq!(seconds(&mut *replay, 2), Some(102));
    }

    /// A reader that cannot go back gives each tick at most once, and
    /// is refused where it would have to rewind.
    #[test]
    fn a_forward_replay_does_not_go_back() {
        let mut replay = Replay::forward(RECORDING.as_slice(), Pairs::NEW);
        assert_eq!(replay.clocking(), Clocking::Sequential);
        assert_eq!(
            admit("stream", replay.clocking(), Driver::Restoring),
            Err(Refusal::NoRewind { name: "stream" })
        );
        assert_eq!(admit("stream", replay.clocking(), Driver::Free), Ok(()));

        assert_eq!(seconds(&mut replay, 0), Some(100));
        assert_eq!(seconds(&mut replay, 2), Some(102));
        assert_eq!(seconds(&mut replay, 2), None);
        assert_eq!(seconds(&mut replay, 1), None);
        assert_eq!(seconds(&mut replay, 3), Some(103));
        assert_eq!(seconds(&mut replay, 4), None);
    }

    /// Each second starts from an empty record, whatever the last left.
    #[test]
    fn a_replayed_record_is_wholly_its_seconds_own() {
        let mut replay = Replay::new(RECORDING.as_slice(), Pairs::NEW);
        let mut record = Record::EMPTY;
        record.utc_offset = Some(Seconds(18));
        assert_eq!(replay.record(Tick(0), &mut record), Presence::Fresh);
        assert_eq!(record.utc_offset, None);
    }
}
