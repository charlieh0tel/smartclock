//! Where a run reads its files from (`Files`), the host's to give:
//! natively the file system (`Disk`); a host with none, a browser, hands
//! over the bytes it has, by path (`InMemory`).  A run reads the board file, its
//! images, its FPGA's bitstream and a script through it, all at its
//! start.

use std::collections::BTreeMap;
use std::io;
use std::path::Component;
use std::path::Path;
use std::path::PathBuf;

/// The files a run reads.
pub trait Files: std::fmt::Debug {
    /// The bytes of the file at `path`.
    ///
    /// # Errors
    ///
    /// The file not read.
    fn read(&self, path: &Path) -> io::Result<Vec<u8>>;

    /// `path` from the working directory, as a board's parts are given
    /// their files: a host with none gives it as it is.
    ///
    /// # Errors
    ///
    /// The working directory unknown.
    fn absolute(&self, path: &Path) -> io::Result<PathBuf>;
}

/// The text of the file at `path` in `files`, UTF-8, as
/// `std::fs::read_to_string` reads it.
pub(crate) fn read_text(files: &dyn Files, path: &Path) -> io::Result<String> {
    String::from_utf8(files.read(path)?).map_err(|_| {
        io::Error::new(
            io::ErrorKind::InvalidData,
            "stream did not contain valid UTF-8",
        )
    })
}

/// The file system.
#[cfg(not(target_family = "wasm"))]
#[derive(Debug, Clone, Copy, Default)]
pub struct Disk;

#[cfg(not(target_family = "wasm"))]
impl Files for Disk {
    fn read(&self, path: &Path) -> io::Result<Vec<u8>> {
        std::fs::read(path)
    }

    fn absolute(&self, path: &Path) -> io::Result<PathBuf> {
        std::path::absolute(path)
    }
}

/// Files in memory, by path, as a host with no file system, a browser,
/// hands them over: a path is looked up with its `.` and `..` taken out,
/// as a file system would resolve it, so that the path a board file
/// names its images by, `../third_party/...` from its directory, finds
/// the file given as `third_party/...`; a path it does not have is not
/// found, and a path is absolute as it is.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct InMemory(BTreeMap<PathBuf, Vec<u8>>);

impl InMemory {
    /// The files with `bytes` at `path` too, in place of any there.
    #[must_use]
    pub fn with(mut self, path: &Path, bytes: Vec<u8>) -> Self {
        self.0.insert(normal(path), bytes);
        self
    }
}

impl Files for InMemory {
    fn read(&self, path: &Path) -> io::Result<Vec<u8>> {
        self.0
            .get(&normal(path))
            .cloned()
            .ok_or_else(|| io::Error::from(io::ErrorKind::NotFound))
    }

    fn absolute(&self, path: &Path) -> io::Result<PathBuf> {
        Ok(path.to_owned())
    }
}

/// `path` with its `.` and `..` taken out, lexically.
fn normal(path: &Path) -> PathBuf {
    path.components()
        .fold(PathBuf::new(), |mut normal, component| {
            match component {
                Component::ParentDir => {
                    normal.pop();
                }
                Component::CurDir => {}
                other => normal.push(other),
            }
            normal
        })
}

#[cfg(test)]
mod tests {
    use std::path::Path;

    use super::Files;
    use super::InMemory;

    #[test]
    fn a_file_in_memory_is_found_by_its_path_resolved_and_no_other() {
        let files = InMemory::default().with(Path::new("third_party/image.bin"), vec![1, 2]);
        let read = files
            .read(Path::new("boards/../third_party/./image.bin"))
            .expect("found");
        assert_eq!(read, [1, 2]);
        assert!(files.read(Path::new("image.bin")).is_err());
    }
}
