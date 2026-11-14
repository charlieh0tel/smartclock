//! What the deframers share: how one hands a chunk of bytes to its
//! byte-at-a-time state machine.  Public for a deframer of a format
//! kept in a crate of its own.

/// What one byte did to a deframer: nothing yet, ended a frame of some
/// length, or an error of the deframer's kind.  A frame borrows the
/// deframer's buffer, so the step is taken first and the frame made
/// from it after.
pub type Step<E> = Option<Result<usize, E>>;

/// Gives `step` bytes from the front of `chunk` up to the first that
/// ends a frame or is an error, and returns how many it took and what
/// that one ended.
pub fn feed<E>(chunk: &[u8], mut step: impl FnMut(u8) -> Step<E>) -> (usize, Step<E>) {
    chunk
        .iter()
        .enumerate()
        .find_map(|(index, &byte)| step(byte).map(|ended| (index + 1, ended)))
        .map_or((chunk.len(), None), |(taken, ended)| (taken, Some(ended)))
}
