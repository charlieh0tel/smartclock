//! Finding starts of locked operation in made-up logs.

use jiff::Timestamp;

use super::Condition;
use super::FirstLock;
use super::HOLDOVER_LIMIT;
use super::Lock;
use super::STARTUP;
use super::Z3805A_HOLDOVER_LIMIT;
use super::holdover_limit;
use super::spans;
use super::starts;

/// `second` seconds into the record.
fn at(second: i64) -> Timestamp {
    Timestamp::from_second(1_700_000_000 + second).expect("a timestamp")
}

/// The first-lock bit as read on every poll of a row.
fn bit(set: bool) -> FirstLock {
    FirstLock { clear: !set, set }
}

/// One-second rows from `from` to `to`, all alike.
fn rows(
    from: i64,
    to: i64,
    lock: Lock,
    first_lock: FirstLock,
) -> Vec<(Timestamp, Timestamp, Condition)> {
    (from..to)
        .map(|s| (at(s), at(s), Condition { lock, first_lock }))
        .collect()
}

#[test]
fn the_limit_is_the_models_and_the_shorter_when_unknown() {
    assert_eq!(holdover_limit(Some("Z3805A")), Z3805A_HOLDOVER_LIMIT);
    assert_eq!(holdover_limit(Some("58503A")), HOLDOVER_LIMIT);
    assert_eq!(holdover_limit(Some("Z3801A")), HOLDOVER_LIMIT);
    assert_eq!(holdover_limit(None), HOLDOVER_LIMIT);
    assert!(HOLDOVER_LIMIT < Z3805A_HOLDOVER_LIMIT);
}

#[test]
fn a_record_that_opens_locked_has_no_start() {
    let record = rows(0, 100, Lock::Locked, bit(true));
    assert!(starts(record, HOLDOVER_LIMIT).is_empty());
}

#[test]
fn a_power_up_starts_at_the_first_lock() {
    let mut record = rows(0, 100, Lock::PowerUp, bit(false));
    record.extend(rows(100, 200, Lock::Locked, bit(true)));
    assert_eq!(starts(record, HOLDOVER_LIMIT), vec![at(100)]);
}

#[test]
fn a_holdover_starts_only_past_the_limit() {
    let limit = HOLDOVER_LIMIT.as_secs();
    // Out of lock for the limit, from the last locked poll to the
    // first: not past it.
    let mut short = rows(0, 100, Lock::Locked, bit(true));
    short.extend(rows(100, 100 + limit - 1, Lock::Other, bit(true)));
    short.extend(rows(100 + limit - 1, 1000, Lock::Locked, bit(true)));
    assert!(starts(short, HOLDOVER_LIMIT).is_empty());

    let mut long = rows(0, 100, Lock::Locked, bit(true));
    long.extend(rows(100, 100 + limit + 1, Lock::Other, bit(true)));
    long.extend(rows(100 + limit + 1, 1000, Lock::Locked, bit(true)));
    assert_eq!(starts(long, HOLDOVER_LIMIT), vec![at(100 + limit + 1)]);
}

#[test]
fn a_gap_is_a_start_only_with_a_power_up_across_it() {
    // Locked either side of an hour with nothing logged.
    let mut gap = rows(0, 100, Lock::Locked, bit(true));
    gap.extend(rows(3700, 3800, Lock::Locked, bit(true)));
    assert!(starts(gap, HOLDOVER_LIMIT).is_empty());

    // The first-lock bit clear after the gap, though the mode says
    // locked: a power-up the daemon missed all but the end of.  The
    // row that shows the clearing is the power-up; the next locked one
    // starts.
    let mut cleared = rows(0, 100, Lock::Locked, bit(true));
    cleared.extend(rows(3700, 3710, Lock::Locked, bit(false)));
    cleared.extend(rows(3710, 3800, Lock::Locked, bit(true)));
    assert_eq!(starts(cleared, HOLDOVER_LIMIT), vec![at(3701)]);

    // The mode powering up after it.
    let mut powering = rows(0, 100, Lock::Locked, bit(true));
    powering.extend(rows(3700, 3710, Lock::PowerUp, FirstLock::default()));
    powering.extend(rows(3710, 3800, Lock::Locked, FirstLock::default()));
    assert_eq!(starts(powering, HOLDOVER_LIMIT), vec![at(3710)]);
}

#[test]
fn a_bit_never_seen_set_is_not_a_power_up() {
    // A receiver locked with the bit clear from the record's start, as
    // one bench 58503A sat for five days: nothing says it cleared.
    let record = rows(0, 100, Lock::Locked, bit(false));
    assert!(starts(record, HOLDOVER_LIMIT).is_empty());
}

#[test]
fn starts_within_a_start_up_merge() {
    let merged = spans(&[at(0), at(600), at(20_000)]);
    let startup = STARTUP.as_secs();
    assert_eq!(
        merged,
        vec![
            (at(0), at(600 + startup)),
            (at(20_000), at(20_000 + startup))
        ]
    );
}

#[test]
fn a_spell_under_way_at_the_first_row_is_timed_from_it() {
    let limit = HOLDOVER_LIMIT.as_secs();
    let mut short = rows(0, limit, Lock::Other, bit(true));
    short.extend(rows(limit, limit + 100, Lock::Locked, bit(true)));
    assert!(starts(short, HOLDOVER_LIMIT).is_empty());

    let mut long = rows(0, limit + 2, Lock::Other, bit(true));
    long.extend(rows(limit + 2, limit + 100, Lock::Locked, bit(true)));
    assert_eq!(starts(long, HOLDOVER_LIMIT), vec![at(limit + 2)]);
}

#[test]
fn the_bit_setting_within_a_row_is_not_another_power_up() {
    // Locked a moment before the receiver sets the bit, as a 58503A
    // does, with the row it is set in seeing it both ways.
    let mut record = rows(0, 100, Lock::PowerUp, bit(false));
    record.extend(rows(100, 120, Lock::Locked, bit(false)));
    record.extend(rows(
        120,
        121,
        Lock::Locked,
        FirstLock {
            clear: true,
            set: true,
        },
    ));
    record.extend(rows(121, 200, Lock::Locked, bit(true)));
    assert_eq!(starts(record, HOLDOVER_LIMIT), vec![at(100)]);
}
