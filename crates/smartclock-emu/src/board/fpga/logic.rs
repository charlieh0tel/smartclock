//! The design a configured part runs, behind the one interface `Fpga`
//! drives it through: time moved on to a phase, the next tick it acts
//! on by itself, the 1 PPS inputs, and the register port's reads and
//! writes, each on the phase of its 10 MHz clock.  How the design is
//! modeled is the `Logic`'s variant, so that `Fpga` (fpga.rs) and the
//! board's part (`fpga_part.rs`) are the same whatever runs inside:
//! the hand-written models of the Z3805A's design (z3805a.rs) and the
//! Z3817A's (z3817a.rs), or the bitstream's own logic executed, its
//! netlist (executed.rs; docs/firmware/fpga-codegen.md).

use super::super::clock::Phase;
use super::super::clock::Tick;
use super::executed::Executed;
use super::executed::Wired;
use super::z3805a::Z3805a;
use super::z3817a::Z3817a;

/// How a design's logic is modeled, which `Logic` it starts.
#[derive(Debug, Clone, Copy)]
pub(super) enum Model {
    /// The hand-written model of the Z3805A's design (z3805a.rs).
    Z3805a,
    /// The hand-written model of the Z3817A's design (z3817a.rs).
    Z3817a,
    /// The netlist of the bitstream the part was configured with, as
    /// the board wires it.
    Lca(Wired),
}

/// What a design did as time moved on.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub(super) struct Ran {
    /// The last internal 1 PPS on the way, its rising edge.
    pub(super) pulse: Option<Tick>,
    /// The last interval the interpolator measured on the way, seconds,
    /// where the design gives it only once its start's pulse has ended.
    pub(super) interval: Option<f64>,
}

/// A design's logic as a model of it runs it.
#[derive(Debug, Clone)]
pub(super) enum Logic {
    /// The hand-written model of the Z3805A's design (z3805a.rs).
    Z3805a(Z3805a),
    /// The hand-written model of the Z3817A's design (z3817a.rs).
    Z3817a(Z3817a),
    /// A netlist executed (executed.rs).
    Lca(Box<Executed>),
}

impl Logic {
    /// `model`'s design held in reset at the phase `at`, RESET Low.
    pub(super) fn held(model: Model, at: Phase) -> Self {
        match model {
            Model::Z3805a => Self::Z3805a(Z3805a::held(at)),
            Model::Z3817a => Self::Z3817a(Z3817a::reset(at)),
            Model::Lca(wired) => Self::Lca(Box::new(Executed::held(wired, at))),
        }
    }

    /// `model`'s design as its reset at the phase `at` leaves it.
    pub(super) fn reset(model: Model, at: Phase) -> Self {
        match model {
            Model::Z3805a => Self::Z3805a(Z3805a::reset(at)),
            Model::Z3817a => Self::Z3817a(Z3817a::reset(at)),
            Model::Lca(wired) => Self::Lca(Box::new(Executed::reset(wired, at))),
        }
    }

    /// The next tick the design acts on by itself.
    pub(super) fn next_event(&self) -> Option<Tick> {
        match self {
            Self::Z3805a(design) => design.next_event(),
            Self::Z3817a(design) => design.next_event(),
            Self::Lca(design) => design.next_event(),
        }
    }

    /// The design moved on to the phase `now`.
    pub(super) fn run_to(&mut self, now: Phase) -> Ran {
        match self {
            Self::Z3805a(design) => Ran {
                pulse: design.run_to(now.tick),
                interval: None,
            },
            Self::Z3817a(design) => Ran {
                pulse: design.run_to(now.tick),
                interval: None,
            },
            Self::Lca(design) => design.run_to(now),
        }
    }

    /// The GPS 1 PPS rising on P82 at the phase `at`.  Returns the
    /// interval the interpolator measures, seconds, if it started one
    /// and gives it at once.
    pub(super) fn gps_pps(&mut self, at: Phase) -> Option<f64> {
        match self {
            Self::Z3805a(design) => design.gps_pps(at),
            Self::Z3817a(design) => {
                design.p82(at);
                None
            }
            Self::Lca(design) => design.gps_pps(at),
        }
    }

    /// The GPS 1 PPS falling at the phase `at`, which the hand models do
    /// not look at.
    pub(super) fn gps_pps_fall(&mut self, at: Phase) {
        match self {
            Self::Z3805a(_) | Self::Z3817a(_) => {}
            Self::Lca(design) => design.gps_pps_fall(at),
        }
    }

    /// A 1 PPS rising on P84 at the phase `at`: an input of the Z3817A's
    /// design alone, which the board's LCA wirings do not drive (pins.rs).
    pub(super) fn p84(&mut self, at: Phase) {
        match self {
            Self::Z3805a(_) | Self::Lca(_) => {}
            Self::Z3817a(design) => design.p84(at),
        }
    }

    /// The port's read of the register `register`, ADDR0 to ADDR2, at
    /// the phase `now` with the interpolator's byte `pins` on P3 to P11,
    /// nothing reaching the part before the phase `horizon` if the board
    /// knows one: the netlist then samples its own bus cycle where it
    /// falls before it (executed.rs).  The hand models answer at `now`.
    pub(super) fn read(
        &mut self,
        register: u32,
        now: Phase,
        pins: u8,
        horizon: Option<Phase>,
    ) -> u8 {
        match self {
            // the hand models' read multiplexers ignore the address
            // lines, and the Z3817A's design has no interpolator byte
            Self::Z3805a(design) => design.read(now, pins, 0),
            Self::Z3817a(design) => design.read(now),
            Self::Lca(design) => design.read(register, now, pins, horizon),
        }
    }

    /// `value` written to the register `register`, ADDR0 to ADDR2, at
    /// the phase `now`.  Returns the interval the interpolator
    /// measures, seconds, if it started one and gives it at once.
    pub(super) fn write(&mut self, register: u32, value: u8, now: Phase) -> Option<f64> {
        match self {
            Self::Z3805a(design) => design.write(register, value, now),
            Self::Z3817a(design) => {
                design.write(register, value, now);
                None
            }
            Self::Lca(design) => design.write(register, value, now),
        }
    }

    /// The Z3805A's model inside, if that is what runs.
    #[cfg(test)]
    pub(super) fn z3805a(&self) -> Option<&Z3805a> {
        match self {
            Self::Z3805a(design) => Some(design),
            Self::Z3817a(_) | Self::Lca(_) => None,
        }
    }

    /// The Z3817A's model inside, if that is what runs.
    #[cfg(test)]
    pub(super) fn z3817a(&self) -> Option<&Z3817a> {
        match self {
            Self::Z3817a(design) => Some(design),
            Self::Z3805a(_) | Self::Lca(_) => None,
        }
    }
}
