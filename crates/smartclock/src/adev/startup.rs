//! The loop's start-up, found in the log so the curve can leave it out.
//!
//! From each start of locked operation the loop runs at a time constant
//! of 150 s and lengthens it to its own, so for a while its bandwidth is
//! wider than the one the rest of the record was measured at, and a
//! curve pooling the two is of neither.  No query reports the loop's
//! stage, so the start-up is found from what the log does record -- the
//! mode, the holdover flag and the powerup register -- and timed by the
//! firmware (`docs/firmware/loop.md`, "Starting up").

use jiff::SignedDuration;
use jiff::Timestamp;

/// How long the loop takes to reach its own time constant from a start
/// of locked operation.
///
/// An estimate from the firmware's timing, not an observation
/// (`docs/firmware/loop.md`, "Starting up"): fifteen ten-second means
/// at 150 s, at least 150 s; after a power-up, a wait for the aging
/// fit's first three samples, 3 x 2700 s; then the ramp to the time
/// constant, 1100 s on the 58503A and 1700 s on the Z3805A.  About
/// 2.8 h and 3 h, so the longer is taken for every receiver.
pub const STARTUP: SignedDuration = SignedDuration::from_hours(3);

/// The longest holdover the 58503A 3704 recovers from without starting
/// up again; after a longer one, holdover recovery sets the time
/// constant back to 150 s (`docs/firmware/loop.md`, "Starting up").
/// The shorter of the two known, so also what an unknown model gets:
/// more left out is the safe side.
const HOLDOVER_LIMIT: SignedDuration = SignedDuration::from_secs(350);

/// The same limit on the Z3805A.
const Z3805A_HOLDOVER_LIMIT: SignedDuration = SignedDuration::from_secs(500);

/// The longest holdover after which a receiver of `model`, as its
/// `*IDN?` names it, keeps its time constant.
pub fn holdover_limit(model: Option<&str>) -> SignedDuration {
    match model.map(str::trim) {
        Some(model) if model.eq_ignore_ascii_case("Z3805A") => Z3805A_HOLDOVER_LIMIT,
        _ => HOLDOVER_LIMIT,
    }
}

/// The loop's state on a logged row, as far as a start-up is concerned.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Lock {
    /// Locked, and not in holdover.
    Locked,
    /// Powering up, before the first lock.
    PowerUp,
    /// Anything else: holdover, recovery, waiting, or a mode not read.
    Other,
}

/// Bit 2 of the powerup register, "date and time valid", set at the
/// first lock after a power-up and cleared by the power-up
/// (097-59551-02, Powerup Status Register), over one logged row's
/// polls.  Both false where the register was not read.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct FirstLock {
    /// Seen clear on one of the row's polls.
    pub clear: bool,
    /// Seen set on one of the row's polls.
    pub set: bool,
}

/// What one logged row says about the loop.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Condition {
    /// The loop's state.
    pub lock: Lock,
    /// The first-lock bit.
    pub first_lock: FirstLock,
}

/// Where locked operation started: the first locked row after either a
/// power-up or a spell out of lock longer than `holdover_limit`.
///
/// `rows` are `(first poll, last poll, condition)`, in time order.
///
/// A power-up shows as the mode `PowerUp`, or as the first-lock bit
/// clearing: seen clear after it was last seen set, which also catches
/// a power-up the daemon saw nothing of but the receiver recovering.
/// A row that saw it both clear and set changed it within its polls,
/// which is a clearing only if it was set before.
/// A spell out of lock runs from the last locked poll before it to the
/// first locked one after, so a gap inside it counts against it.  A gap
/// with neither around it is not a start: the receiver was locked on
/// both sides of it as far as anything shows.  A spell already under
/// way at the first row is timed from that row, as the least it lasted.
pub fn starts(
    rows: impl IntoIterator<Item = (Timestamp, Timestamp, Condition)>,
    holdover_limit: SignedDuration,
) -> Vec<Timestamp> {
    let mut found = Vec::new();
    // The last poll of the latest locked row, or the first row.
    let mut last_locked: Option<Timestamp> = None;
    let mut previous: Option<Lock> = None;
    // A power-up seen since the latest locked row.
    let mut powered = false;
    // The first-lock bit as last seen.
    let mut first_lock: Option<bool> = None;
    for (at, last, condition) in rows {
        let since = *last_locked.get_or_insert(at);
        let FirstLock { clear, set } = condition.first_lock;
        let cleared = clear && first_lock == Some(true);
        if clear || set {
            first_lock = Some(set);
        }
        let lock = if cleared {
            Lock::PowerUp
        } else {
            condition.lock
        };
        match lock {
            Lock::PowerUp => powered = true,
            Lock::Other => {}
            Lock::Locked => {
                if previous.is_some_and(|was| was != Lock::Locked)
                    && (powered || at.duration_since(since) > holdover_limit)
                {
                    found.push(at);
                }
                powered = false;
                last_locked = Some(last);
            }
        }
        previous = Some(lock);
    }
    found
}

/// The spans `starts` begins, `STARTUP` long, merged where they overlap.
pub(super) fn spans(starts: &[Timestamp]) -> Vec<(Timestamp, Timestamp)> {
    let mut merged: Vec<(Timestamp, Timestamp)> = Vec::new();
    for &start in starts {
        let end = start + STARTUP;
        match merged.last_mut() {
            Some((_, until)) if start <= *until => *until = (*until).max(end),
            _ => merged.push((start, end)),
        }
    }
    merged
}

#[cfg(test)]
mod tests;
