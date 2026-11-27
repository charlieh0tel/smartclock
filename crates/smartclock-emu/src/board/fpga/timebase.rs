//! What the family's designs share, as their netlists read (counter.md,
//! "What the netlist shows"; z3817a-trace.md): the divider that makes
//! the internal 1 PPS from the 10 MHz clock on P7, its tap that stops a
//! count, the seconds count of its pulses, the sync field that steps or
//! restarts it, and a count's start and stop, each a value of the phase
//! of the 10 MHz clock signal (clock.rs), in its periods, ticks.  Each
//! design's model (z3805a.rs, z3817a.rs) keeps its own registers and
//! port, and these.
//!
//! - **The divider.**  The internal 1 PPS every 10,000,000 edges, the
//!   first 10,000,000 after the reset, as if a pulse had come at it.
//! - **The sync.**  The field's change from 0 to a kind marks a sync
//!   pending.  The internal 1 PPS clears it; kind 1 makes that pulse an
//!   edge early, kind 2 an edge late.  With kind 3 the 1 PPS that starts
//!   counts restarts the divider on its second clock edge, the next
//!   internal 1 PPS 10,000,000 edges on, the Z3817A's restart itself an
//!   internal 1 PPS (`Restart`); if the internal 1 PPS comes first, the
//!   divider holds there until that 1 PPS.  A field left at its kind
//!   does nothing more.
//! - **The stop.**  A count runs to the fourth edge after the divider's
//!   tap 4,000,000 edges after the edge the internal 1 PPS rises on, the
//!   first tap after the start.

use super::super::clock::IDEAL_HZ;
use super::super::clock::Phase;
use super::super::clock::Tick;

/// The sync field's bits and its kinds (counter.md, "The sync field").
pub(super) const SYNC_MASK: u8 = 0x03;
const SYNC_EARLIER: u8 = 1;
const SYNC_LATER: u8 = 2;
const SYNC_FULL: u8 = 3;

/// The divider and count, in periods of the 10 MHz clock, ticks: a
/// pulse every `DIVIDE` ticks of the oscillator's phase, the first
/// `DIVIDE` after its reset; the tap that stops a count `TAP` ticks
/// after each pulse, and the count's last tick `STOP_PIPELINE` after the
/// tap.  A full sync restarts the divider `RESTART` ticks after the
/// 1 PPS, its next pulse `DIVIDE` on.  From the netlists' simulation
/// (counter.md, "What the netlist shows"; the differential test in
/// z3805a.rs; z3817a-trace.md, "The 1 PPS divider").
pub(super) const DIVIDE: u64 = 10_000_000;
pub(super) const TAP: u64 = 4_000_000;
pub(super) const STOP_PIPELINE: u64 = 4;
const RESTART: u64 = 2;
/// A write reaches the design on this clock edge after the CPU's cycle
/// starts: the select sampled, then the one-clock strobe (FK, FL).
pub(super) const WRITE_STROBE: u64 = 4;
/// The ticks, after the field leaves a full sync, to the pulse a hold
/// let go gives: what the netlist's divider had left when it held (the
/// case `sync_3_late`), its release itself not simulated.
const HOLD_RELEASE: u64 = 156;
/// A tick's seconds.
const TICK_SECONDS: f64 = 1.0 / IDEAL_HZ as f64;

/// What a design's full sync makes of its restart (z3817a-trace.md, "The
/// sync field").
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) enum Restart {
    /// No pulse: the first internal 1 PPS a second on, the Z3805A's.
    Silent,
    /// The restart is itself the internal 1 PPS, counted in the seconds,
    /// the Z3817A's (`check-readings-z3817a.sync3`).
    Pulse,
}

/// The divider: its next pulse and the stop's tap, the oscillator's
/// rising edges.
#[derive(Debug, Clone, Copy, PartialEq)]
struct Divider {
    /// The edge it last started on: the reset's, a pulse's or a full
    /// sync's restart.  Its stages count from it.
    from: Tick,
    /// Whether its stages below the second were reset on `from`, by the
    /// design's reset or a restart, so that none of their terminals came
    /// on it.
    quiet: bool,
    /// The next pulse, before a step; none while a full sync holds the
    /// divider at the end of its second.
    pulse: Option<Tick>,
    /// Whether that pulse is a restart's (`Restart::Pulse`).
    restarting: bool,
    /// The tap still to come before that pulse.
    tap: Option<Tick>,
}

impl Divider {
    /// The divider started on the tick `from`, as a pulse there leaves
    /// it, or the reset or a restart, `quiet`: its next pulse `DIVIDE`
    /// on, and that second's tap `TAP` on, `DIVIDE - TAP` before the
    /// pulse.
    fn started(from: Tick, quiet: bool) -> Self {
        Self {
            from,
            quiet,
            pulse: Some(from + DIVIDE),
            restarting: false,
            tap: Some(from + TAP),
        }
    }
}

/// A count started (status bit 1), on the oscillator's phase.
#[derive(Debug, Clone, Copy, PartialEq)]
pub(super) struct Start {
    /// When it started: the 1 PPS, or the Z3805A's mode 3 strobe.
    pub(super) at: Phase,
    /// The clock edge the Z3805A's AH.Y ends on: from `at` to it is the
    /// interval its interpolator measures.
    pub(super) open: Tick,
    /// The gate's opening: the count is of the ticks after it.
    pub(super) gate: Tick,
    /// The tap that stopped it (status bit 0), once it has come.
    pub(super) stop: Option<Tick>,
}

impl Start {
    /// The interval the interpolator measures, seconds.
    pub(super) fn interval(&self) -> f64 {
        self.at.until(self.open) * TICK_SECONDS
    }

    /// The ticks counted by the phase `now`: those after the gate's
    /// opening, to the stop's last, modulo `mask` + 1, the counter's
    /// width.
    pub(super) fn counted(&self, now: Phase, mask: u64) -> u64 {
        let end = self
            .stop
            .map_or(now.tick, |stop| now.tick.min(stop + STOP_PIPELINE));
        // the counter wraps; a count is never negative
        end.0.saturating_sub(self.gate.0) & mask
    }

    /// Whether its stop has come by the phase `now`.
    pub(super) fn stopped(&self, now: Phase) -> bool {
        self.stop.is_some_and(|stop| stop <= now.tick)
    }
}

/// The divider, its seconds and the sync field.
#[derive(Debug, Clone, Copy, PartialEq)]
pub(super) struct Timebase {
    divider: Divider,
    /// The edge of the design's reset, from which its other counters,
    /// the Z3817A's P19's among them, count.
    pub(super) reset: Tick,
    /// The internal 1 PPS's last pulse, none since the reset.
    pub(super) last_pulse: Option<Tick>,
    /// The internal 1 PPS's pulses, modulo 16 in a read.
    pub(super) seconds: u8,
    /// The sync field, its two bits.
    sync: u8,
    /// A sync pending, from the field's change from 0.
    sync_pending: bool,
    /// What a full sync's restart is.
    restart: Restart,
}

impl Timebase {
    /// As the design's reset at the tick `at` leaves it: the field
    /// clear, the divider started there; a full sync's restart `restart`.
    pub(super) fn reset(at: Tick, restart: Restart) -> Self {
        Self {
            divider: Divider::started(at, true),
            reset: at,
            last_pulse: None,
            seconds: 0,
            sync: 0,
            sync_pending: false,
            restart,
        }
    }

    /// The ticks from the edge the divider last started on to `now`, if
    /// it has started by then: where its stages stand.
    pub(super) fn since_start(&self, now: Tick) -> Option<u64> {
        now.0.checked_sub(self.divider.from.0)
    }

    /// Whether the divider's stages below the second were reset on the
    /// edge it last started on, the reset's or a restart's.
    pub(super) fn started_quiet(&self) -> bool {
        self.divider.quiet
    }

    /// Whether a full sync is pending, which holds the divider at its
    /// pulse and restarts it on the 1 PPS.
    fn full_sync_pending(&self) -> bool {
        self.sync_pending && self.sync == SYNC_FULL
    }

    /// The next pulse, unless held: a pending step's kind moves it a
    /// tick.
    pub(super) fn next_pulse(&self) -> Option<Tick> {
        let pulse = self.divider.pulse?;
        Some(match (self.sync_pending, self.sync) {
            (true, SYNC_EARLIER) => pulse - 1,
            (true, SYNC_LATER) => pulse + 1,
            _ => pulse,
        })
    }

    /// The next tick it acts on by itself: the internal 1 PPS's next
    /// pulse, or the tap the count `start` waits for.
    pub(super) fn next_event(&self, start: Option<&Start>) -> Option<Tick> {
        let tap = start
            .filter(|start| start.stop.is_none())
            .and(self.divider.tap);
        [tap, self.next_pulse()].into_iter().flatten().min()
    }

    /// Moved on to the rising edge `now`: the taps and pulses up to it,
    /// a tap stopping `start`.  Returns the last pulse.
    pub(super) fn run_to(&mut self, now: Tick, start: &mut Option<Start>) -> Option<Tick> {
        let mut last = None;
        loop {
            let pulse = self.next_pulse().filter(|pulse| *pulse <= now);
            match self.divider.tap.filter(|tap| *tap <= now) {
                Some(tap) if pulse.is_none_or(|pulse| tap < pulse) => {
                    self.divider.tap = None;
                    if let Some(start) = start
                        && start.stop.is_none()
                    {
                        start.stop = Some(tap);
                    }
                }
                _ => match pulse {
                    // a full sync pending holds the divider at its pulse
                    Some(_) if self.full_sync_pending() => {
                        self.divider.pulse = None;
                    }
                    Some(pulse) => {
                        self.seconds = self.seconds.wrapping_add(1);
                        self.sync_pending = false;
                        self.divider = Divider::started(pulse, self.divider.restarting);
                        self.last_pulse = Some(pulse);
                        last = Some(pulse);
                    }
                    None => break,
                },
            }
        }
        last
    }

    /// The sync field written `value`'s two bits.
    pub(super) fn write_sync(&mut self, value: u8) {
        let sync = value & SYNC_MASK;
        if self.sync == 0 && sync != 0 {
            self.sync_pending = true;
        }
        self.sync = sync;
    }

    /// The 1 PPS that starts counts rising at the phase `at`: a full
    /// sync pending restarts the divider on it, the restart a pulse there
    /// if the design's is.
    pub(super) fn reference_pulse(&mut self, at: Phase) {
        if self.full_sync_pending() {
            self.sync_pending = false;
            let restart = at.edge(RESTART);
            self.divider = match self.restart {
                Restart::Silent => Divider::started(restart, true),
                Restart::Pulse => Divider {
                    pulse: Some(restart),
                    restarting: true,
                    tap: None,
                    ..self.divider
                },
            };
        }
    }

    /// A write's strobe on the clock edge `strobe`: a full sync's hold
    /// ends with the field's kind.
    pub(super) fn write_strobe(&mut self, strobe: Tick) {
        if self.divider.pulse.is_none() && self.sync != SYNC_FULL {
            self.divider = Divider {
                pulse: Some(strobe + HOLD_RELEASE),
                tap: None,
                ..self.divider
            };
        }
    }
}
