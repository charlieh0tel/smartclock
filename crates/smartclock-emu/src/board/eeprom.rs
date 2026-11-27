//! The EEPROM on chip select 9 at `0x400000`, 8 KiB: the settings
//! records, the NV register at `0x4000c0`, the calibration block at
//! `0x400080` and the logs (restart.md; interval.md; structures.md,
//! "The NV register").  It starts from a dump of a real one, copied to
//! a working file that every write goes through to, so a run's
//! settings carry to the next and the dump itself is never touched.
//!
//! The working file is always whole, `SIZE` bytes, whatever kills the
//! emulator:
//!
//! - It is made, from the dump or erased, or made whole from a short
//!   one, by writing a sibling file, syncing it and renaming it over
//!   the working file, so it is either the old file or the new one.
//! - The bytes of a write cycle are written to the file as the cycle
//!   ends, when the part's cells take them, each by itself, with no
//!   buffer in between: one `write` of one byte, which cannot land in
//!   part.  A kill loses nothing a finished cycle wrote; the kernel has
//!   it.  A kill during a cycle loses the cycle's bytes, and a crash of
//!   the host may lose the last bytes written, as a power loss mid-write
//!   loses them on the real part: the firmware's records are checksummed
//!   and the NV register kept in two slots against just that.  The file
//!   is not synced a byte at a time, which would cost a disk flush for
//!   each of a record's 128 bytes.
//! - It is locked while open, through a sidecar, `<file>.lock`, taken
//!   before the working file is looked for, so a second emulator can
//!   neither make it nor write it too.  The sidecar stays.
//!
//! The part is U4, an Atmel AT28C64B (docs/hardware-investigations.md,
//! item 22), and its writes are the data sheet's
//! (`third_party/microchip/AT28C64B.pdf`, DS20006432B, 6.2 to 6.5,
//! Table 6-4):
//!
//! - A write starts a write cycle and loads a byte; each byte after it
//!   within tBLC, 150 µs, of the one before is loaded too, into the
//!   same 64-byte page, the one A6 to A12 give, by A0 to A5, a byte
//!   loaded twice keeping the second (6.3).  Once tBLC passes with no
//!   byte, the part stops taking them and programs the bytes loaded, and
//!   only those, for tWC; the cells hold them after.  A byte written
//!   then is not taken.  A byte of another page during the load, which
//!   the data sheet forbids and does not say the effect of, is dropped.
//! - From the first byte to the end of tWC every read is a polling read
//!   (6.2): bit 7 the complement of the last byte written's (DATA
//!   polling, 6.4), bit 6 the opposite of what the read before gave
//!   (toggle bit, 6.5), the rest the last byte's own; the data sheet
//!   gives only bits 7 and 6.
//! - Software data protection is off, as the part ships (6.6.2), and
//!   stays off: the firmware never writes its command sequence, no
//!   instruction putting `0x401555` or `0x400aaa` in a register.
//!
//! tWC is a pick, `WRITE_CYCLE`, 5 ms: Table 6-4 gives only its most,
//! 10 ms.  The firmware polls a byte 5000 times for its end
//! (eeprom.c, `eeprom_write_byte`, `eeprom_write_page`), loops of 10
//! and 12 bytes of code, about 6 ms and 7 ms at the core's two cycles a
//! byte, and about as long on the CPU32 with the part's three wait
//! states; at the data sheet's most every write would run out of polls
//! and try again, which a working receiver's log does not show.  So the
//! pick is the most that both of the firmware's polls outlast.
//!
//! It has no net-facing I/O: its bus side is `read` and `write` at an
//! offset, A0 to A12, after `advance` to the access's time, and its
//! time the write cycle's end.

use std::collections::BTreeMap;
use std::fs::File;
use std::fs::OpenOptions;
use std::io::Seek;
use std::io::SeekFrom;
use std::io::Write;
use std::path::Path;
use std::path::PathBuf;

use super::select::Selected;
use super::time::CPU_HZ;
use super::time::Cycles;

use crate::trace;

/// The EEPROM's size.
pub(crate) const SIZE: u32 = 0x2000;

/// An erased cell.
pub(crate) const ERASED: u8 = 0xff;

/// tBLC, the most from one byte loaded to the next (Table 6-4).
const BYTE_LOAD: Cycles = Cycles(CPU_HZ * 150 / 1_000_000);
/// tWC, a write cycle's programming: a pick within Table 6-4's 10 ms
/// (above).
const WRITE_CYCLE: Cycles = Cycles(CPU_HZ * 5 / 1000);
/// A page, the bytes A6 to A12 share (6.3).
const PAGE_SIZE: u32 = 64;
/// DATA polling's bit and the toggle bit (6.4, 6.5).
const DATA_POLLING: u8 = 0x80;
const TOGGLE: u8 = 0x40;

/// What the sibling a working file is made in adds to its name.
const PARTIAL: &str = ".partial";
/// What the sidecar a working file is locked through adds to its name.
const LOCK: &str = ".lock";

/// The EEPROM's contents, and its working file.
#[derive(Debug)]
pub(crate) struct Eeprom {
    /// The cells.
    cells: Vec<u8>,
    /// The write cycle running, if one is.
    writing: Option<WriteCycle>,
    /// Where writes go through to, if anywhere.
    file: Option<File>,
    /// The working file's sidecar, locked while it is open.  On wasm32
    /// the Unicorn core, and with it the whole model, is not built.
    #[cfg_attr(
        not(target_family = "wasm"),
        expect(dead_code, reason = "held for its lock, released when dropped")
    )]
    lock: Option<File>,
}

/// A write cycle: its bytes loaded, then programmed.
#[derive(Debug)]
struct WriteCycle {
    /// The page, its first byte's offset.
    page: u32,
    /// The bytes loaded, by offset, each once.
    loaded: BTreeMap<u32, u8>,
    /// The last byte written, which DATA polling gives back.
    last: u8,
    /// When it was loaded.
    last_at: Cycles,
    /// What the toggle bit read last.
    toggle: bool,
}

impl WriteCycle {
    /// When it stops taking bytes and starts programming.
    fn programming_from(&self) -> Cycles {
        self.last_at + BYTE_LOAD
    }

    /// When its programming ends.
    fn done(&self) -> Cycles {
        self.programming_from() + WRITE_CYCLE
    }
}

/// Why the working file could not be opened.
#[derive(Debug, thiserror::Error)]
pub(crate) enum OpenError {
    /// A file could not be read, made or opened.
    #[error("{what} {}: {source}", path.display())]
    File {
        /// What was being done.
        what: &'static str,
        /// To which file.
        path: PathBuf,
        /// What failed.
        source: std::io::Error,
    },
    /// The dump or the working file is larger than the part.
    #[error("{} holds {size} bytes, more than the EEPROM's {SIZE}", path.display())]
    TooLarge {
        /// The file.
        path: PathBuf,
        /// Its size.
        size: usize,
    },
    /// Another emulator has the working file open.
    #[error("{} is in use, by another emulator", path.display())]
    InUse {
        /// The working file.
        path: PathBuf,
    },
}

/// A failure of `what` on `path`.
fn failed<'a>(what: &'static str, path: &'a Path) -> impl FnOnce(std::io::Error) -> OpenError + 'a {
    move |source| OpenError::File {
        what,
        path: path.to_owned(),
        source,
    }
}

/// `path`'s contents, which must fit the part.
fn contents(path: &Path) -> Result<Vec<u8>, OpenError> {
    let bytes = std::fs::read(path).map_err(failed("reading", path))?;
    if bytes.len() > SIZE as usize {
        return Err(OpenError::TooLarge {
            path: path.to_owned(),
            size: bytes.len(),
        });
    }
    Ok(bytes)
}

/// The directory `working` is in, if its path names one: a bare file
/// name is in the working directory, which is there.
pub(crate) fn directory(working: &Path) -> Option<&Path> {
    working
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
}

/// Replaces `path` with `cells`, or makes it, all at once: a sibling
/// written and synced, then renamed over it.
fn replace(path: &Path, cells: &[u8]) -> Result<(), OpenError> {
    let mut partial = path.as_os_str().to_owned();
    partial.push(PARTIAL);
    let partial = PathBuf::from(partial);
    let mut file = File::create(&partial).map_err(failed("creating", &partial))?;
    file.write_all(cells)
        .and_then(|()| file.sync_all())
        .map_err(failed("writing", &partial))?;
    std::fs::rename(&partial, path).map_err(failed("renaming over", path))?;
    // the rename is durable once the directory is synced; a directory
    // opens as a file only on Unix
    #[cfg(unix)]
    if let Some(directory) = directory(path) {
        File::open(directory)
            .and_then(|directory| directory.sync_all())
            .map_err(failed("syncing", directory))?;
    }
    Ok(())
}

/// `working`'s sidecar, made if it is not there, and locked.
fn lock(working: &Path) -> Result<File, OpenError> {
    let mut path = working.as_os_str().to_owned();
    path.push(LOCK);
    let path = PathBuf::from(path);
    let lock = OpenOptions::new()
        .create(true)
        .truncate(false)
        .write(true)
        .open(&path)
        .map_err(failed("opening", &path))?;
    lock.try_lock().map_err(|error| match error {
        std::fs::TryLockError::WouldBlock => OpenError::InUse {
            path: working.to_owned(),
        },
        std::fs::TryLockError::Error(source) => OpenError::File {
            what: "locking",
            path,
            source,
        },
    })?;
    Ok(lock)
}

/// `cells` padded with erased bytes to the part's size.
fn whole(mut cells: Vec<u8>) -> Vec<u8> {
    cells.resize(SIZE as usize, ERASED);
    cells
}

impl Eeprom {
    /// An EEPROM holding `cells`, padded with erased bytes to its size,
    /// writing through to `file`.
    pub(crate) fn new(cells: Vec<u8>, file: Option<File>) -> Self {
        Self {
            cells: whole(cells),
            writing: None,
            file,
            lock: None,
        }
    }

    /// The EEPROM from its working file, `working`, locked while it is
    /// open: made from `dump` if it does not exist, erased without one,
    /// and made whole, padded erased, if it is short; its directory made
    /// first if it is not there.
    pub(crate) fn open(working: &Path, dump: Option<&Path>) -> Result<Self, OpenError> {
        if let Some(directory) = directory(working) {
            std::fs::create_dir_all(directory)
                .map_err(failed("making the directory", directory))?;
        }
        let lock = lock(working)?;
        let found = match working.try_exists().map_err(failed("finding", working))? {
            true => Some(contents(working)?),
            false => None,
        };
        let cells = match (found, dump) {
            (Some(cells), _) if cells.len() == SIZE as usize => cells,
            (Some(cells), _) => {
                let cells = whole(cells);
                replace(working, &cells)?;
                cells
            }
            (None, dump) => {
                let cells = whole(dump.map(contents).transpose()?.unwrap_or_default());
                replace(working, &cells)?;
                cells
            }
        };
        let file = OpenOptions::new()
            .write(true)
            .open(working)
            .map_err(failed("opening to write", working))?;
        Ok(Self {
            lock: Some(lock),
            ..Self::new(cells, Some(file))
        })
    }

    /// A cell read, or during a write cycle its polling status; after
    /// `advance` to the read's time.
    pub(crate) fn read(&mut self, offset: u32) -> u8 {
        match &mut self.writing {
            Some(cycle) => {
                cycle.toggle = !cycle.toggle;
                let toggle = if cycle.toggle { TOGGLE } else { 0 };
                (!cycle.last & DATA_POLLING) | toggle | (cycle.last & !(DATA_POLLING | TOGGLE))
            }
            None => self.cells[offset as usize],
        }
    }

    /// A byte written at `now`, after `advance` to it: loaded into the
    /// write cycle, or one started.
    pub(crate) fn write(&mut self, offset: u32, value: u8, now: Cycles) {
        let page = offset - offset % PAGE_SIZE;
        match &mut self.writing {
            None => {
                self.writing = Some(WriteCycle {
                    page,
                    loaded: BTreeMap::from([(offset, value)]),
                    last: value,
                    last_at: now,
                    toggle: false,
                });
            }
            Some(cycle) if now < cycle.programming_from() && cycle.page == page => {
                cycle.loaded.insert(offset, value);
                cycle.last = value;
                cycle.last_at = now;
            }
            // programming, or another page's: not taken
            Some(_) => {}
        }
    }

    /// Time moves on to `now`: a write cycle whose programming is over
    /// leaves its bytes in the cells, all of them, then in the working
    /// file; an error writing them through is the caller's to report,
    /// the cells holding the whole page whatever the file took.
    pub(crate) fn advance(&mut self, now: Cycles) -> std::io::Result<()> {
        let Some(cycle) = self.writing.take_if(|cycle| cycle.done() <= now) else {
            return Ok(());
        };
        for (&offset, &value) in &cycle.loaded {
            self.cells[offset as usize] = value;
        }
        if let Some(file) = &mut self.file {
            for (offset, value) in cycle.loaded {
                file.seek(SeekFrom::Start(u64::from(offset)))?;
                file.write_all(&[value])?;
            }
        }
        Ok(())
    }
}

impl Eeprom {
    /// Time moves on to `now`, a failure to write the working file
    /// reported: the run goes on, as the part's own cells do.
    fn advance_reporting(&mut self, now: Cycles) {
        if let Err(error) = self.advance(now) {
            trace::note(format_args!("writing the EEPROM's working file: {error}"));
        }
    }
}

/// A0 to A12 on its select; every access first brings it to its time.
impl Selected for Eeprom {
    fn read_byte(&mut self, address: u32, now: Cycles) -> u8 {
        self.advance_reporting(now);
        self.read(address % SIZE)
    }

    fn write_byte(&mut self, address: u32, value: u8, now: Cycles) {
        self.advance_reporting(now);
        self.write(address % SIZE, value, now);
    }

    fn next_change(&self) -> Option<Cycles> {
        self.writing.as_ref().map(WriteCycle::done)
    }

    fn advance_to(&mut self, now: Cycles) {
        self.advance_reporting(now);
    }
}

#[cfg(test)]
mod tests {
    use std::fs::File;
    use std::path::PathBuf;

    use super::super::time::Cycles;
    use super::BYTE_LOAD;
    use super::ERASED;
    use super::Eeprom;
    use super::OpenError;
    use super::PAGE_SIZE;
    use super::SIZE;
    use super::WRITE_CYCLE;

    /// `eeprom` moved on to `now`.
    fn at(eeprom: &mut Eeprom, now: Cycles) {
        eeprom.advance(now).expect("written through");
    }

    /// A byte written at `now`, as the board writes one.
    fn write(eeprom: &mut Eeprom, offset: u32, value: u8, now: Cycles) {
        at(eeprom, now);
        eeprom.write(offset, value, now);
    }

    /// When a write cycle whose last byte was at `last` is done.
    fn done(last: Cycles) -> Cycles {
        last + BYTE_LOAD + WRITE_CYCLE
    }

    /// A directory of its own for a test, removed when dropped.
    struct Scratch(PathBuf);

    impl Scratch {
        fn new(name: &str) -> Self {
            let path = std::env::temp_dir().join(format!(
                "smartclock-emu-eeprom-{name}-{}",
                std::process::id()
            ));
            let _ = std::fs::remove_dir_all(&path);
            std::fs::create_dir_all(&path).expect("a scratch directory");
            Self(path)
        }

        fn file(&self, name: &str) -> PathBuf {
            self.0.join(name)
        }
    }

    impl Drop for Scratch {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }

    /// DATA polling and the toggle bit (6.4, 6.5) until tWC is over,
    /// tBLC after the byte, as `eeprom_write_byte` polls them.
    #[test]
    fn a_byte_polls_until_its_write_cycle_ends() {
        let mut eeprom = Eeprom::new(vec![0; 16], None);
        write(&mut eeprom, 0x10c0, 0x5a, Cycles(0));
        let polls: Vec<u8> = (0..2).map(|_| eeprom.read(0x10c0)).collect();
        assert_eq!(
            polls[0] & 0x80,
            0x80,
            "bit 7 complemented: {:#04x}",
            polls[0]
        );
        assert_eq!(polls[0] & 0x3f, 0x1a, "the rest the byte's");
        assert_ne!(polls[0] & 0x40, polls[1] & 0x40, "bit 6 toggles");
        assert_eq!(eeprom.read(0x0003) & 0x80, 0x80, "any address polls");
        at(&mut eeprom, done(Cycles(0)) - Cycles(1));
        assert_ne!(eeprom.read(0x10c0), 0x5a, "still programming");
        at(&mut eeprom, done(Cycles(0)));
        assert_eq!(eeprom.read(0x10c0), 0x5a);
        assert_eq!(eeprom.read(0x10c0), 0x5a, "settled");
    }

    /// A page's bytes within tBLC of each other program together, a
    /// byte loaded twice keeping the second; one of another page during
    /// the load, and any during the programming, are not taken (6.3).
    #[test]
    fn a_page_loads_within_tblc_and_programs_at_once() {
        let mut eeprom = Eeprom::new(vec![0; SIZE as usize], None);
        let step = Cycles(BYTE_LOAD.0 / 2);
        let mut now = Cycles(100);
        for (offset, value) in [(0x40, 1), (0x41, 2), (0x7f, 3), (0x41, 4), (0x80, 9)] {
            write(&mut eeprom, offset, value, now);
            now = now + step;
        }
        let last = now - step - step;
        // past the load: programming, so not taken
        write(&mut eeprom, 0x42, 5, last + BYTE_LOAD);
        at(&mut eeprom, done(last));
        let cells: Vec<u8> = [0x40, 0x41, 0x42, 0x7f, 0x80]
            .into_iter()
            .map(|offset| eeprom.read(offset))
            .collect();
        assert_eq!(cells, [1, 4, 0, 3, 0]);
    }

    /// Past tBLC a byte starts a write cycle of its own.
    #[test]
    fn a_byte_after_tblc_is_a_cycle_of_its_own() {
        let mut eeprom = Eeprom::new(vec![0; SIZE as usize], None);
        write(&mut eeprom, 0x10, 1, Cycles(0));
        let first = done(Cycles(0));
        write(&mut eeprom, 0x11, 2, first);
        assert_eq!(eeprom.read(0x10) & 0x80, 0x80, "the second polls");
        at(&mut eeprom, done(first));
        assert_eq!([eeprom.read(0x10), eeprom.read(0x11)], [1, 2]);
    }

    #[test]
    fn it_starts_from_its_dump_padded_erased() {
        let mut eeprom = Eeprom::new(vec![1, 2], None);
        assert_eq!(eeprom.read(1), 2);
        assert_eq!(eeprom.read(2), 0xff);
    }

    #[test]
    fn writes_reach_the_working_file_and_the_next_run() {
        let scratch = Scratch::new("persist");
        let (dump, working) = (scratch.file("dump.bin"), scratch.file("working.bin"));
        std::fs::write(&dump, [7u8; SIZE as usize]).expect("a dump");
        let mut eeprom = Eeprom::open(&working, Some(&dump)).expect("opened");
        write(&mut eeprom, 0xc3, 0x77, Cycles(0));
        at(&mut eeprom, done(Cycles(0)));
        drop(eeprom);
        let held = std::fs::read(&working).expect("read");
        assert_eq!(held.len(), SIZE as usize);
        assert_eq!(held[0xc3], 0x77);
        assert_eq!(held[0xc2], 7);
        assert_eq!(std::fs::read(&dump).expect("read"), [7u8; SIZE as usize]);
        let mut again = Eeprom::open(&working, Some(&dump)).expect("reopened");
        assert_eq!(
            again.read(0xc3),
            0x77,
            "kept, not copied from the dump again"
        );
    }

    /// A write cycle's bytes reach the file as it ends, not before: as a
    /// kill would find it, nothing held back to flush after.
    #[test]
    fn a_write_is_in_the_file_as_its_cycle_ends() {
        let scratch = Scratch::new("through");
        let working = scratch.file("working.bin");
        let mut eeprom = Eeprom::open(&working, None).expect("opened");
        write(&mut eeprom, 0x1fff, 0x12, Cycles(0));
        at(&mut eeprom, done(Cycles(0)) - Cycles(1));
        assert_eq!(std::fs::read(&working).expect("read")[0x1fff], 0xff);
        at(&mut eeprom, done(Cycles(0)));
        assert_eq!(std::fs::read(&working).expect("read")[0x1fff], 0x12);
        drop(eeprom);
    }

    /// A working file that refuses the write leaves the whole page in
    /// the cells, as the part's own: the run goes on with it.
    #[test]
    fn a_page_the_file_refuses_is_whole_in_the_cells() {
        let scratch = Scratch::new("refused");
        let working = scratch.file("working.bin");
        std::fs::write(&working, [0u8; SIZE as usize]).expect("a working file");
        let read_only = File::open(&working).expect("opened to read");
        let mut eeprom = Eeprom::new(Vec::new(), Some(read_only));
        let page: Vec<u8> = (1..=PAGE_SIZE as u8).collect();
        for (offset, &value) in (0..PAGE_SIZE).zip(&page) {
            eeprom.write(offset, value, Cycles(0));
        }
        eeprom
            .advance(done(Cycles(0)))
            .expect_err("the file refuses it");
        let cells: Vec<u8> = (0..PAGE_SIZE).map(|offset| eeprom.read(offset)).collect();
        assert_eq!(cells, page);
        assert_eq!(std::fs::read(&working).expect("read"), [0u8; SIZE as usize]);
    }

    #[test]
    fn without_a_dump_it_starts_erased_and_a_short_file_is_made_whole() {
        let scratch = Scratch::new("whole");
        let working = scratch.file("working.bin");
        drop(Eeprom::open(&working, None).expect("opened"));
        assert_eq!(
            std::fs::read(&working).expect("read"),
            [0xff; SIZE as usize]
        );
        std::fs::write(&working, [1, 2, 3]).expect("a short file");
        let mut eeprom = Eeprom::open(&working, None).expect("opened");
        assert_eq!(eeprom.read(2), 3);
        assert_eq!(eeprom.read(3), 0xff, "padded erased, not zero");
        drop(eeprom);
        let held = std::fs::read(&working).expect("read");
        assert_eq!(held.len(), SIZE as usize);
        assert!(!scratch.file("working.bin.partial").exists());
    }

    /// A working file in a directory that is not there, as in a fresh
    /// checkout's `target/smartclock-emu/`, is made, the directory too.
    #[test]
    fn a_missing_directory_is_made() {
        let scratch = Scratch::new("directory");
        let working = scratch.file("made/below/working.bin");
        drop(Eeprom::open(&working, None).expect("opened"));
        assert_eq!(
            std::fs::read(&working).expect("read"),
            [ERASED; SIZE as usize]
        );
    }

    /// A directory that cannot be made, a file in its place, is refused
    /// by its path.
    #[test]
    fn a_directory_that_cannot_be_made_is_refused_by_its_path() {
        let scratch = Scratch::new("no-directory");
        let blocker = scratch.file("blocker");
        std::fs::write(&blocker, []).expect("a file");
        let directory = blocker.join("below");
        let error = Eeprom::open(&directory.join("working.bin"), None).expect_err("refused");
        assert!(
            matches!(&error, OpenError::File { path, .. } if *path == directory),
            "{error}"
        );
    }

    #[test]
    fn a_dump_larger_than_the_part_is_refused() {
        let scratch = Scratch::new("large");
        let dump = scratch.file("dump.bin");
        std::fs::write(&dump, vec![0; SIZE as usize + 1]).expect("a dump");
        let error = Eeprom::open(&scratch.file("working.bin"), Some(&dump)).expect_err("refused");
        assert!(matches!(error, OpenError::TooLarge { .. }), "{error}");
    }

    /// The lock is taken before the working file is looked for, so a
    /// second emulator starting beside one making it waits for nothing
    /// and makes nothing.
    #[test]
    fn a_locked_sidecar_keeps_a_missing_working_file_unmade() {
        let scratch = Scratch::new("sidecar");
        let working = scratch.file("working.bin");
        let sidecar = File::create(scratch.file("working.bin.lock")).expect("a sidecar");
        sidecar.try_lock().expect("locked");
        let error = Eeprom::open(&working, None).expect_err("refused");
        assert!(matches!(error, OpenError::InUse { .. }), "{error}");
        assert!(!working.exists(), "not made");
        drop(sidecar);
        Eeprom::open(&working, None).expect("free again");
    }

    #[test]
    fn a_second_emulator_cannot_open_the_working_file() {
        let scratch = Scratch::new("lock");
        let working = scratch.file("working.bin");
        let first = Eeprom::open(&working, None).expect("opened");
        let error = Eeprom::open(&working, None).expect_err("refused");
        assert!(matches!(error, OpenError::InUse { .. }), "{error}");
        drop(first);
        Eeprom::open(&working, None).expect("free again");
    }
}
