//! The FPGA pins as the board wires them, `z3805a_tb.v`'s roles read
//! from the Z3805A's netlist (z3805a-trace.md): what the board's LCA
//! model (executed.rs) and the netlist's tests (lca/tests.rs) drive and
//! watch.  The register port, the bus, the GPS 1 PPS, the internal 1 PPS
//! and the interval are on the same pins in every design (stimulus.vh);
//! the clocks and the inputs held are each design's `Wiring`.

use super::super::clock::IDEAL_HZ;
use super::lca::Clock;
use super::lca::Picoseconds;
use super::lca::Pin;

/// Picoseconds a second.
const PS_PER_SECOND: u64 = 1_000_000_000_000;
/// The P7 clock's period, a tick of the board's 10 MHz, picoseconds.
pub(super) const TICK: u64 = PS_PER_SECOND / IDEAL_HZ;

/// How the board wires the pins of a design's that are its own: the
/// clocks the board drives, in the model's time, the first rise at 0
/// (executed.rs), in the order they edge at an instant several do; the
/// inputs it holds High or Low; and the model's picoseconds a tick of
/// the board's 10 MHz, `TICK` where the model's time is the board's;
/// and the sync field whose steps the board infers and makes itself,
/// where the real board's PLL locks a design's clock to the 10 MHz
/// through a divider the field steps (the PLL is not modeled).
#[derive(Debug)]
pub(super) struct Wiring {
    pub(super) clocks: &'static [Clock],
    pub(super) held: &'static [(Pin, bool)],
    pub(super) tick: u64,
    pub(super) steps: Option<Steps>,
}

/// A sync field that steps the P7 divider a PLL locks the design's clock
/// to, by one P7 clock, a tick: written from 0 to `earlier` the divider
/// counts one clock short, to `later` one long, once (z3816a-trace.md,
/// "The sync field"), and the PLL, following it, moves the design's
/// clock, and everything it clocks, a tick earlier or later.
#[derive(Debug, Clone, Copy)]
pub(super) struct Steps {
    /// The register the field is written to, and its bits there.
    pub(super) register: u32,
    pub(super) mask: u8,
    /// The field's kinds that step the divider.
    pub(super) earlier: u8,
    pub(super) later: u8,
}

/// The 10 MHz clock, P7, rising on each tick and falling half way.
const CLOCK: Clock = Clock {
    pin: P7,
    first_rise: Picoseconds(0),
    half_period: Picoseconds(TICK / 2),
};

/// The Z3805A's, which the 58503A's and Z3801A's and the 58503B's
/// designs share (`z3805a_stimulus.v`, `58503a-z3801a_stimulus.v`,
/// `58503b_stimulus.v`): the 10 MHz on P7 alone, and P17 held High
/// (`z3805a_tb.v`).
pub(super) const TEN_MHZ_ALONE: Wiring = Wiring {
    clocks: &[CLOCK],
    held: &[(Pin(17), true)],
    tick: TICK,
    steps: None,
};
/// The telecom designs' clocks (z3816a-trace.md, "Clocks"): the real
/// board's PLL locks the oscillator on P53, ACLK, to the 10 MHz,
/// P53 / 12,288 to P7 / 6250, so ACLK is the 10 MHz times
/// 12,288 / 6250, 19.6608 MHz (inferred: the CDMA VCXO; GCLK, ACLK
/// halved, divided by 9,830,400 a second).  The PLL itself is not
/// modeled: the board drives ACLK ideally at that ratio.
const ACLK_DIVIDE: u64 = 12_288;
const P7_DIVIDE: u64 = 6250;
/// ACLK's period in the model's time: the benches' 50 ns, so that a
/// tick of the 10 MHz is a whole `ACLK_TICK` of the model's picoseconds,
/// the PLL's ratio exact, and the model's period the benches' (lca/tests.rs).
const ACLK_PERIOD: u64 = 50_000;
pub(super) const ACLK_TICK: u64 = ACLK_PERIOD * ACLK_DIVIDE / P7_DIVIDE;
const _: () = assert!((ACLK_PERIOD * ACLK_DIVIDE).is_multiple_of(P7_DIVIDE));
/// ACLK, P53, rising at 0.
pub(super) const ACLK: Clock = Clock {
    pin: Pin(53),
    first_rise: Picoseconds(0),
    half_period: Picoseconds(ACLK_PERIOD / 2),
};
/// The 10 MHz's pin, which in the telecom designs drives only the PLL's
/// P7 divider and phase detector, P38 and P40, which nothing the
/// firmware reads depends on (`lca/board_clocks.rs`).
pub(super) const P7: Pin = Pin(7);

/// The Z3816A's: ACLK driven ideally at the OCXO x 12,288 / 6250, so
/// the EFC steers it; the PLL not modeled at all, P7 held Low, its
/// domain dropped, so the model runs one clock and the P7 divider and
/// the phase detector, P38 and P40, are constant; the P7 divider's
/// steps, which the real PLL follows, inferred by the board from
/// register 3's sync field as written (`Steps`; inferred too: the PLL
/// settles within the two seconds the firmware leaves between steps).
pub(super) const ACLK_ALONE: Wiring = Wiring {
    clocks: &[ACLK],
    held: &[(P7, false)],
    tick: ACLK_TICK,
    steps: Some(TELECOM_STEPS),
};

/// The Z3815A's: the Z3816A's, ACLK alone and P7 held Low, and the
/// inputs only its design has held Low as its bench holds them
/// (`z3815a_tb.v`; z3815a-trace.md, "Clocks"): P78 and P84, the start
/// sources register 2 selects but the GPS 1 PPS, which the firmware
/// selects only in its standby card's stage (z3815a-trace.md, "What the
/// firmware takes from P35 and P84"); P35, the P35 counter's clock,
/// which drives pins alone; P23, P27 and P50, which reach only pins
/// (z3815a-trace.md, "Every output pin").  Inferred: their board
/// sources are open.
pub(super) const ACLK_Z3815A: Wiring = Wiring {
    clocks: &[ACLK],
    held: &[
        (P7, false),
        (Pin(23), false),
        (Pin(27), false),
        (Pin(35), false),
        (Pin(50), false),
        (Pin(78), false),
        (Pin(84), false),
    ],
    tick: ACLK_TICK,
    steps: Some(TELECOM_STEPS),
};

/// The telecom designs' sync field, register 3's bits 0 and 1 in GG:
/// kind 1 steps the P7 divider a P7 clock earlier, kind 2 later
/// (z3816a-trace.md and z3815a-trace.md, "The sync field").
const TELECOM_STEPS: Steps = Steps {
    register: 3,
    mask: 0b11,
    earlier: 1,
    later: 2,
};

/// The GPS 1 PPS in, and the internal 1 PPS out.
pub(super) const GPS: Pin = Pin(82);
pub(super) const PPS: Pin = Pin(83);
/// The start's pulse to the interpolator, Low: ~AH.Y in the Z3805A's
/// design and the 58503A's and Z3801A's, ~AH.X in the 58503B's, ~BH.X
/// in the Z3815A's and the Z3816A's (fpga-comparison.md, "Outputs").
pub(super) const INTERVAL: Pin = Pin(81);
/// The register port's select and R/W.
pub(super) const CHIP_SELECT: Pin = Pin(61);
pub(super) const READ_WRITE: Pin = Pin(75);
/// ADDR0 to ADDR2, and DATA bits 0 to 7.
pub(super) const ADDRESS: [Pin; 3] = [Pin(69), Pin(68), Pin(66)];
pub(super) const DATA: [Pin; 8] = [
    Pin(71),
    Pin(70),
    Pin(67),
    Pin(65),
    Pin(62),
    Pin(60),
    Pin(59),
    Pin(58),
];
/// The interpolator's byte, P3 to P11, bit 0 first.
pub(super) const BYTE: [Pin; 8] = [
    Pin(3),
    Pin(4),
    Pin(5),
    Pin(6),
    Pin(8),
    Pin(9),
    Pin(10),
    Pin(11),
];
