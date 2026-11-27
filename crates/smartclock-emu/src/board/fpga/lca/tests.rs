//! The LCA model against the netlist's Icarus run: each image's netlist
//! driven as its `tools/fpga/<image>_stimulus.v` drives the Verilog, from
//! tools/fpga/differential.py's cases, each read, internal 1 PPS,
//! interval, watched pin and flag held to what the image's fixture
//! recorded, exactly.

use std::sync::LazyLock;
use std::time::Instant;

use sha2::Digest;
use sha2::Sha256;

use super::super::super::time::CPU_HZ;
use super::super::fixture::Case;
use super::super::fixture::Edge;
use super::super::fixture::NETLIST;
use super::super::fixture::NETLIST_Z3817A;
use super::super::fixture::cases;
use super::super::fixture::hex;
use super::super::pins::ACLK_ALONE;
use super::super::pins::ACLK_Z3815A;
use super::super::pins::ADDRESS;
use super::super::pins::BYTE;
use super::super::pins::CHIP_SELECT;
use super::super::pins::DATA;
use super::super::pins::GPS;
use super::super::pins::PPS;
use super::super::pins::READ_WRITE;
use super::super::pins::TEN_MHZ_ALONE;
use super::Clock;
use super::Drive;
use super::Lca;
use super::Net;
use super::Netlist;
use super::NetlistError;
use super::Picoseconds;
use super::Pin;
use super::PinError;
use super::Probe;
use super::Transition;

/// A stimulus bench, an image's `tools/fpga/<image>_stimulus.v`: its
/// design's netlist and fixture, and how the bench drives and watches
/// the pins it does not share with the others' (`tools/fpga/stimulus.vh`;
/// the Z3817A's harness is its own).
pub(super) struct Bench {
    /// The image, differential.py's name for it.
    pub(super) image: &'static str,
    /// The netlist, as tools/fpga/lcanet.py writes it from the bitstream's
    /// decode (`make -f tools/fpga/Makefile check-lca`), and the bitstream.
    pub(super) netlist: &'static LazyLock<Netlist>,
    pub(super) bitstream: &'static [u8],
    /// differential.py's cases on the netlist in Icarus.
    pub(super) fixture: &'static str,
    /// The clocks the model drives itself, in the order they edge at an
    /// instant several do: the bench's own first.
    pub(super) clocks: &'static [Clock],
    /// The inputs held High or Low.
    pub(super) held: &'static [(Pin, bool)],
    /// The internal 1 PPS's pin: P83 but in the Z3817A's design, P78.
    pub(super) pps: Pin,
    /// The net the interpolator measures, the start's interval, if the
    /// design has one.
    pub(super) interval: Option<&'static str>,
    /// The interpolator's byte, bit 0 first, which a case's `pins`
    /// drives: none on a design without one.
    pub(super) byte: &'static [Pin],
    /// The second 1 PPS input a case's `p84` pulses, each as wide as the
    /// fixture records, if any.
    pub(super) p84: Option<Pin>,
    /// The nets whose changes after the reset the fixture records as
    /// flags in every case, and the pins each harness option (`+edges`,
    /// `+p19`) adds to them, in the cases the fixture records it for.
    pub(super) flag_nets: &'static [&'static str],
    pub(super) flag_pins: &'static [(&'static str, &'static [(Pin, &'static str)])],
    /// The cases short enough to run unoptimized on every test.
    pub(super) short: &'static [&'static str],
}

/// differential.py's board time 0, the first rise of the 10 MHz, and its
/// clock edge, by which it counts the internal 1 PPS's ticks.
const CLOCK_PHASE: u64 = 50_000;
const TICK: u64 = 100_000;
/// The clock edge `tick`, by differential.py's count, the fixture's
/// record of an internal 1 PPS: the pulse rises a nanosecond or two
/// after it.
pub(super) fn tick_edge(tick: u128) -> Picoseconds {
    Picoseconds(u64::try_from(tick).expect("a tick within u64") * TICK + CLOCK_PHASE)
}

/// The 10 MHz on P7 of the single-clock designs' benches, rising at
/// board time 0.
const TEN_MHZ: Clock = Clock {
    pin: Pin(7),
    first_rise: Picoseconds(CLOCK_PHASE),
    half_period: Picoseconds(TICK / 2),
};

/// The Z3805A's.
pub(super) static Z3805A: Bench = Bench {
    image: "z3805a",
    netlist: &NETLIST_Z3805A,
    bitstream: include_bytes!("../../../../../../third_party/firmware/images/fpga/z3805a.bit"),
    fixture: NETLIST,
    clocks: &[TEN_MHZ],
    held: TEN_MHZ_ALONE.held,
    pps: PPS,
    interval: Some("AH.Y"),
    byte: &BYTE,
    p84: None,
    flag_nets: &[],
    flag_pins: &[],
    short: &SHORT_CASES,
};

pub(super) static NETLIST_Z3805A: LazyLock<Netlist> = LazyLock::new(|| {
    Netlist::parse(include_str!("../z3805a.netlist")).expect("the Z3805A's netlist")
});

/// The 58503A's and Z3801A's: the Z3805A's pins and clock but P45, which
/// it does not bond.
pub(super) static FPGA_58503A_Z3801A: Bench = Bench {
    image: "58503a-z3801a",
    netlist: &NETLIST_58503A_Z3801A,
    bitstream: include_bytes!(
        "../../../../../../third_party/firmware/images/fpga/58503a-z3801a.bit"
    ),
    fixture: include_str!("../58503a-z3801a.fixture"),
    clocks: &[TEN_MHZ],
    held: TEN_MHZ_ALONE.held,
    pps: PPS,
    interval: Some("AH.Y"),
    byte: &BYTE,
    p84: None,
    flag_nets: &[],
    flag_pins: &[],
    short: &SHORT_CASES,
};

static NETLIST_58503A_Z3801A: LazyLock<Netlist> = LazyLock::new(|| {
    Netlist::parse(include_str!("../58503a-z3801a.netlist"))
        .expect("the 58503A's and Z3801A's netlist")
});

/// The 58503B's: the Z3805A's clock, its interval AH.X.
pub(super) static FPGA_58503B: Bench = Bench {
    image: "58503b",
    netlist: &NETLIST_58503B,
    bitstream: include_bytes!("../../../../../../third_party/firmware/images/fpga/58503b.bit"),
    fixture: include_str!("../58503b.fixture"),
    clocks: &[TEN_MHZ],
    held: TEN_MHZ_ALONE.held,
    pps: PPS,
    interval: Some("AH.X"),
    byte: &BYTE,
    p84: None,
    flag_nets: &[],
    flag_pins: &[],
    short: &SHORT_CASES,
};

static NETLIST_58503B: LazyLock<Netlist> = LazyLock::new(|| {
    Netlist::parse(include_str!("../58503b.netlist")).expect("the 58503B's netlist")
});

/// The telecom designs' two clocks (`z3815a_stimulus.v`): P53 at 20 MHz,
/// rising at 25 ns, which AB halves into GCLK; and P7 a 10 MHz of its
/// own, Low from 0 and rising at 50 ns, on P53's falls, which it edges
/// after.
const TELECOM_CLOCKS: [Clock; 2] = [
    Clock {
        pin: Pin(53),
        first_rise: Picoseconds(TICK / 4),
        half_period: Picoseconds(TICK / 4),
    },
    Clock {
        pin: Pin(7),
        first_rise: Picoseconds(TICK / 2),
        half_period: Picoseconds(TICK / 2),
    },
];

/// The Z3815A's: its other inputs held Low.
pub(super) static Z3815A: Bench = Bench {
    image: "z3815a",
    netlist: &NETLIST_Z3815A,
    bitstream: include_bytes!("../../../../../../third_party/firmware/images/fpga/z3815a.bit"),
    fixture: include_str!("../z3815a.fixture"),
    clocks: &TELECOM_CLOCKS,
    held: &[
        (Pin(23), false),
        (Pin(27), false),
        (Pin(35), false),
        (Pin(50), false),
        (Pin(78), false),
        (Pin(84), false),
    ],
    pps: PPS,
    interval: Some("BH.X"),
    byte: &BYTE,
    p84: None,
    flag_nets: &[],
    flag_pins: &[],
    short: &SHORT_CASES,
};

/// The Z3815A's on its board's clocks (pins.rs, `ACLK_Z3815A`): ACLK
/// alone, P7 held Low beside its bench's inputs.
pub(super) static Z3815A_BOARD: Bench = Bench {
    image: "z3815a-board",
    clocks: ACLK_Z3815A.clocks,
    held: ACLK_Z3815A.held,
    ..Z3815A
};

static NETLIST_Z3815A: LazyLock<Netlist> = LazyLock::new(|| {
    Netlist::parse(include_str!("../z3815a.netlist")).expect("the Z3815A's netlist")
});

/// The Z3816A's: the Z3815A's clocks, and no other inputs.
pub(super) static Z3816A: Bench = Bench {
    image: "z3816a",
    netlist: &NETLIST_Z3816A,
    bitstream: include_bytes!("../../../../../../third_party/firmware/images/fpga/z3816a.bit"),
    fixture: include_str!("../z3816a.fixture"),
    clocks: &TELECOM_CLOCKS,
    held: &[],
    pps: PPS,
    interval: Some("BH.X"),
    byte: &BYTE,
    p84: None,
    flag_nets: &[],
    flag_pins: &[],
    short: &SHORT_CASES,
};

/// The Z3816A's on its board's clocks (pins.rs, `ACLK_ALONE`): ACLK
/// alone, P7 held Low.
pub(super) static Z3816A_BOARD: Bench = Bench {
    image: "z3816a-board",
    clocks: ACLK_ALONE.clocks,
    held: ACLK_ALONE.held,
    ..Z3816A
};

static NETLIST_Z3816A: LazyLock<Netlist> = LazyLock::new(|| {
    Netlist::parse(include_str!("../z3816a.netlist")).expect("the Z3816A's netlist")
});

/// The Z3817A's (`z3817a_stimulus.v`): the Z3805A's clock, no
/// interpolator, P78 the internal 1 PPS, P84 the second 1 PPS, and the
/// status's bits 2 and 3 and the outputs recorded as flags.
pub(super) static Z3817A: Bench = Bench {
    image: "z3817a",
    netlist: &NETLIST_Z3817A_LCA,
    bitstream: include_bytes!("../../../../../../third_party/firmware/images/fpga/z3817a.bit"),
    fixture: NETLIST_Z3817A,
    clocks: &[TEN_MHZ],
    held: &[],
    pps: Pin(78),
    interval: None,
    byte: &[],
    p84: Some(Pin(84)),
    flag_nets: &["FI.X", "GJ.X"],
    flag_pins: &[
        ("+edges", &[(Pin(16), "P16"), (Pin(72), "P72")]),
        ("+p19", &[(Pin(19), "P19")]),
    ],
    short: &SHORT_CASES_Z3817A,
};

static NETLIST_Z3817A_LCA: LazyLock<Netlist> = LazyLock::new(|| {
    Netlist::parse(include_str!("../z3817a.netlist")).expect("the Z3817A's netlist")
});

/// Every bench.
pub(super) static BENCHES: [&Bench; 6] = [
    &Z3805A,
    &FPGA_58503A_Z3801A,
    &FPGA_58503B,
    &Z3815A,
    &Z3816A,
    &Z3817A,
];

/// Picoseconds a second.
const PS_PER_SECOND: u128 = 1_000_000_000_000;
/// `stimulus.vh`'s bus cycle, the select Low, and a write's data held a
/// quarter of one after it.
const BUS_CYCLE: u64 = 1_000_000;
const WRITE_HOLD: u64 = BUS_CYCLE / 4;
/// differential.py's GPS 1 PPS width, and the shortest pulse of the
/// start's interval it keeps as one.
const GPS_WIDTH: u64 = 100_000_000;
const GLITCH: u64 = 5_000;
/// A nanosecond past a case's end, as `stimulus.vh`'s `#1 $finish`.
const FINISH: u64 = 1_000;

/// differential.py's `to_ps`: a board time in cycles as the netlist's
/// picoseconds, rounded half to even as Python's `round`.
fn to_ps(cycles: u64) -> Picoseconds {
    let numerator = u128::from(cycles) * PS_PER_SECOND;
    let denominator = u128::from(CPU_HZ);
    let (quotient, remainder) = (numerator / denominator, numerator % denominator);
    let rounded = match (2 * remainder).cmp(&denominator) {
        std::cmp::Ordering::Less => quotient,
        std::cmp::Ordering::Greater => quotient + 1,
        std::cmp::Ordering::Equal => quotient + quotient % 2,
    };
    Picoseconds(CLOCK_PHASE + rounded as u64)
}

/// What the bench does at an instant.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) enum Action {
    Drive(Pin, Drive),
    Reset,
    /// The data bus read, as a read's cycle samples it.
    Sample,
    /// A pin's changes recorded from now on.
    Watch(Pin),
}

fn level(high: bool) -> Drive {
    Drive::Strong(high)
}

/// `pins` driven to `value`, bit 0 on the first.
fn bits(pins: &[Pin], value: u8) -> impl Iterator<Item = Action> + '_ {
    pins.iter()
        .enumerate()
        .map(move |(bit, pin)| Action::Drive(*pin, level(value >> bit & 1 != 0)))
}

/// `pin` High from `at` for `width`.
fn pulse(pin: Pin, at: Picoseconds, width: u64) -> [(Picoseconds, Action); 2] {
    [
        (at, Action::Drive(pin, level(true))),
        (Picoseconds(at.0 + width), Action::Drive(pin, level(false))),
    ]
}

/// differential.py's `to_tick`: the clock edge a change at `at` follows.
fn to_tick(at: Picoseconds) -> u128 {
    u128::from((at.0 - CLOCK_PHASE) / TICK)
}

/// The actions of `case`'s stimulus on `bench`, in time order, ending at
/// its end.
pub(super) fn actions(bench: &Bench, case: &Case) -> (Vec<(Picoseconds, Action)>, Picoseconds) {
    let mut actions = Vec::new();
    let mut end = Picoseconds::default();
    for (at, words) in &case.operations {
        let at = to_ps(*at);
        let words: Vec<&str> = words.iter().map(String::as_str).collect();
        let after = |delay: u64| Picoseconds(at.0 + delay);
        match words.as_slice() {
            ["reset"] => actions.push((at, Action::Reset)),
            ["pins", value] => {
                actions.extend(bits(bench.byte, hex(value)).map(|action| (at, action)));
            }
            ["write", register, value] => {
                let register: u8 = register.parse().expect("a register");
                actions.extend(bits(&ADDRESS, register).map(|action| (at, action)));
                actions.push((at, Action::Drive(READ_WRITE, level(false))));
                actions.extend(bits(&DATA, hex(value)).map(|action| (at, action)));
                actions.push((at, Action::Drive(CHIP_SELECT, level(false))));
                actions.push((after(BUS_CYCLE), Action::Drive(CHIP_SELECT, level(true))));
                let released = after(BUS_CYCLE + WRITE_HOLD);
                actions.extend(
                    DATA.iter()
                        .map(|pin| (released, Action::Drive(*pin, Drive::Pull(true)))),
                );
                actions.push((released, Action::Drive(READ_WRITE, level(true))));
            }
            ["read", _] => {
                actions.extend(bits(&ADDRESS, 0).map(|action| (at, action)));
                actions.push((at, Action::Drive(READ_WRITE, level(true))));
                actions.push((at, Action::Drive(CHIP_SELECT, level(false))));
                actions.push((after(BUS_CYCLE), Action::Sample));
                actions.push((after(BUS_CYCLE), Action::Drive(CHIP_SELECT, level(true))));
            }
            ["gps"] => actions.extend(pulse(GPS, at, GPS_WIDTH)),
            ["p84", width] => {
                let pin = bench.p84.expect("a P84 input");
                actions.extend(pulse(pin, at, width.parse().expect("a width")));
            }
            ["watch", pin] => actions.push((at, Action::Watch(Pin(pin.parse().expect("a pin"))))),
            ["end"] => end = at,
            _ => panic!("{}: {words:?}", case.name),
        }
    }
    // stable: the actions of an instant in the order the bench makes them
    actions.sort_by_key(|(at, _)| *at);
    (actions, end)
}

/// `case`'s actions made on `bench`, moved on between them by `advance`,
/// and the model at its end.
pub(super) fn replay(bench: &Bench, case: &Case, advance: &mut Advance<'_>) -> Lca<'static> {
    let mut lca = watched_bench(bench);
    let (actions, end) = actions(bench, case);
    for (at, action) in actions.into_iter().take_while(|(at, _)| *at <= end) {
        advance(&mut lca, at);
        act(&mut lca, action);
    }
    advance(&mut lca, end);
    lca
}

/// `action` made on `lca` now.  Returns the byte a sample reads.
pub(super) fn act(lca: &mut Lca<'static>, action: Action) -> Option<u8> {
    match action {
        Action::Drive(pin, drive) => lca.drive(pin, drive).expect("a bonded pin"),
        Action::Reset => lca.set_reset(true),
        Action::Watch(pin) => lca.watch(Probe::Pin(pin)).expect("a bonded pin"),
        Action::Sample => {
            return Some(DATA.iter().enumerate().fold(0, |byte, (bit, pin)| {
                byte | u8::from(lca.pin(*pin).expect("a data pin")) << bit
            }));
        }
    }
    None
}

/// What the netlist did on a case, as the fixture records it.
#[derive(Debug, Default, PartialEq, Eq)]
pub(super) struct Run {
    reads: Vec<u8>,
    pulses: Vec<u128>,
    intervals: Vec<u128>,
    edges: Vec<Edge>,
    /// The flags' changes, in order of their clock edge and name: the
    /// netlist prints those of an instant in no order of its own.
    flags: Vec<(u64, String, bool)>,
}

/// `flags` in order of their clock edge and name.
fn sorted(mut flags: Vec<(u64, String, bool)>) -> Vec<(u64, String, bool)> {
    flags.sort_by(|(tick, name, _), (other, other_name, _)| (tick, name).cmp(&(other, other_name)));
    flags
}

/// How a run moves the model on to a time, its pins unchanged.
pub(super) type Advance<'a> = dyn FnMut(&mut Lca<'static>, Picoseconds) + 'a;

/// The model moved on edge by edge, the reference.
fn clocked(lca: &mut Lca<'static>, until: Picoseconds) {
    lca.run_clocked(until);
}

/// `case` through the LCA model on `bench`, clocked edge by edge.
/// Returns what it did, and the blocks it evaluated.
fn run(bench: &Bench, case: &Case) -> (Run, u64) {
    run_with(bench, case, &mut clocked, &mut |_, _| {})
}

/// `case` through the LCA model on `bench`, moved on between its actions
/// by `advance`, and `inspect` shown the model before each action.
pub(super) fn run_with(
    bench: &Bench,
    case: &Case,
    advance: &mut Advance<'_>,
    inspect: &mut dyn FnMut(&Lca<'static>, &Action),
) -> (Run, u64) {
    let mut lca = watched_bench(bench);
    let flagged = flag_probes(bench, case);
    for (probe, _) in &flagged {
        lca.watch(*probe).expect("a flag");
    }
    let interval = interval_net(bench);
    let (actions, end) = actions(bench, case);
    let reset_at = actions
        .iter()
        .find(|(_, action)| *action == Action::Reset)
        .map_or(Picoseconds::default(), |(at, _)| *at);
    let mut result = Run::default();
    // the clock first at an instant both act on
    for (at, action) in actions.into_iter().take_while(|(at, _)| *at <= end) {
        advance(&mut lca, at);
        inspect(&lca, &action);
        result.reads.extend(act(&mut lca, action));
    }
    advance(&mut lca, end);
    lca.run_until(Picoseconds(end.0 + FINISH));
    // the interval's pulses past the reset's own glitch, the internal
    // 1 PPS's rises, the flags' changes past the reset and the watched
    // pins' changes
    let mut rise = Picoseconds::default();
    for transition in lca.take_transitions() {
        let Transition { at, probe, high } = transition;
        if probe == Probe::Pin(bench.pps) {
            if high {
                result.pulses.push(to_tick(at));
            }
        } else if interval.is_some_and(|net| probe == Probe::Net(net)) {
            if high {
                rise = at;
            } else if rise > reset_at && at.0 - rise.0 > GLITCH {
                result.intervals.push(u128::from(at.0 - rise.0));
            }
        } else if let Some((_, name)) = flagged.iter().find(|(watched, _)| *watched == probe) {
            if at > reset_at {
                let tick = u64::try_from(to_tick(at)).expect("a tick");
                result.flags.push((tick, (*name).to_owned(), high));
            }
        } else if let Probe::Pin(pin) = probe {
            result.edges.push(Edge {
                pin: pin.0,
                at: u128::from(at.0),
                high,
            });
        }
    }
    result.flags = sorted(result.flags);
    (result, lca.evaluations())
}

/// The probes whose changes `case` records on `bench` as flags, each
/// with its name.
fn flag_probes(bench: &Bench, case: &Case) -> Vec<(Probe, &'static str)> {
    let netlist: &Netlist = bench.netlist;
    let nets = bench.flag_nets.iter().map(|name| {
        let net = netlist.net(name).unwrap_or_else(|| panic!("no {name}"));
        (Probe::Net(net), *name)
    });
    let pins = case
        .options
        .iter()
        .flat_map(|given| {
            bench
                .flag_pins
                .iter()
                .find(|(option, _)| option == given)
                .unwrap_or_else(|| panic!("{}: no option {given}", case.name))
                .1
        })
        .map(|(pin, name)| (Probe::Pin(*pin), *name));
    nets.chain(pins).collect()
}

/// `bench`'s interval's net, if its design has one.
fn interval_net(bench: &Bench) -> Option<Net> {
    bench
        .interval
        .map(|name| bench.netlist.net(name).expect("the interval's net"))
}

/// `bench`'s netlist at time 0 with `stimulus.vh`'s levels before its
/// first action, its clock set to run: the select and R/W High, the data
/// bus pulled High, the rest Low, then the bench's own; RESET Low.
pub(super) fn bench(bench: &Bench) -> Lca<'static> {
    let mut lca = Lca::new(bench.netlist);
    let idle = [
        (CHIP_SELECT, level(true)),
        (READ_WRITE, level(true)),
        (GPS, level(false)),
    ];
    let low = ADDRESS
        .iter()
        .chain(bench.byte)
        .chain(&bench.p84)
        .map(|pin| (*pin, level(false)));
    let pulled = DATA.iter().map(|pin| (*pin, Drive::Pull(true)));
    let held = bench.held.iter().map(|&(pin, high)| (pin, level(high)));
    let initial: Vec<_> = idle
        .into_iter()
        .chain(low)
        .chain(pulled)
        .chain(held)
        .collect();
    for (pin, drive) in initial {
        lca.drive(pin, drive).expect("a bonded pin");
    }
    for clock in bench.clocks {
        lca.add_clock(*clock).expect("a clock's pin");
    }
    lca
}

/// `bench`'s netlist on its bench, the interval, if any, and the
/// internal 1 PPS watched.
pub(super) fn watched_bench(on: &Bench) -> Lca<'static> {
    let mut lca = bench(on);
    if let Some(interval) = interval_net(on) {
        lca.watch(Probe::Net(interval)).expect("a net");
    }
    lca.watch(Probe::Pin(on.pps)).expect("the internal 1 PPS");
    lca
}

/// `on`'s bench a microsecond after RESET rose at 1 us, at a period's
/// start.
pub(super) fn after_reset(on: &Bench) -> Lca<'static> {
    let mut lca = bench(on);
    lca.run_clocked(Picoseconds(1_000_000));
    lca.set_reset(true);
    lca.run_clocked(Picoseconds(2_000_000));
    lca.run_to_period();
    lca
}

/// What the fixture recorded for `case`.
pub(super) fn recorded(case: &Case) -> Run {
    Run {
        reads: case
            .operations
            .iter()
            .filter_map(|(_, words)| match words.as_slice() {
                [read, value] if read == "read" => Some(hex(value)),
                _ => None,
            })
            .collect(),
        pulses: case.pulses.clone(),
        intervals: case.intervals.clone(),
        edges: case.edges.clone(),
        flags: sorted(case.flags.clone()),
    }
}

/// `bench`'s fixture's case `name`.
pub(super) fn case(bench: &Bench, name: &str) -> Case {
    cases(bench.fixture)
        .into_iter()
        .find(|case| case.name == name)
        .unwrap_or_else(|| panic!("{}: no case {name}", bench.image))
}

/// The cases short enough to run unoptimized: the register port, the
/// read multiplexer and the preset, and mode 3's starts, the count of
/// six edges and the interpolator's references.
pub(super) const SHORT_CASES: [&str; 2] = ["registers", "references"];

/// The Z3817A's: the read map at either select and the P84 counter,
/// P72's and P16's edges, and P19's.
const SHORT_CASES_Z3817A: [&str; 3] = ["registers", "outputs", "p19"];

#[test]
fn each_netlist_names_its_bitstream() {
    for bench in BENCHES {
        let digest = Sha256::digest(bench.bitstream);
        let hex: String = digest.iter().map(|byte| format!("{byte:02x}")).collect();
        assert_eq!(bench.netlist.bitstream(), hex, "{}", bench.image);
    }
}

#[test]
fn the_lca_model_reads_as_the_netlist_does() {
    for bench in BENCHES {
        for name in bench.short {
            let case = case(bench, name);
            let (run, _) = run(bench, &case);
            assert_eq!(run, recorded(&case), "{}: {name}", bench.image);
        }
    }
}

/// Every case of `bench`'s fixture, seconds of the 10 MHz each, a
/// thread a case: optimized, a matter of minutes, so only when asked.  It
/// prints the evaluator's speed.
fn every_case(bench: &Bench) {
    let cases = cases(bench.fixture);
    let results: Vec<(Run, u64, f64)> = std::thread::scope(|scope| {
        let runs: Vec<_> = cases
            .iter()
            .map(|case| {
                scope.spawn(move || {
                    let started = Instant::now();
                    let (run, evaluations) = run(bench, case);
                    (run, evaluations, started.elapsed().as_secs_f64())
                })
            })
            .collect();
        runs.into_iter()
            .map(|run| run.join().expect("a case's run"))
            .collect()
    });
    let mut differences = Vec::new();
    for (case, (run, evaluations, elapsed)) in cases.iter().zip(results) {
        let simulated = case.operations.last().map_or(0, |(at, _)| *at) as f64 / CPU_HZ as f64;
        println!(
            "{} {}: {simulated:.3} s simulated in {elapsed:.3} s, {:.2e} evaluations a second",
            bench.image,
            case.name,
            evaluations as f64 / elapsed
        );
        if run != recorded(case) {
            differences.push(format!(
                "{}: {run:?}, the netlist {:?}",
                case.name,
                recorded(case)
            ));
        }
    }
    assert!(differences.is_empty(), "{}", differences.join("\n"));
}

#[test]
#[ignore = "every clock edge of seconds of the 10 MHz: minutes, optimized"]
fn the_lca_model_reads_as_the_netlist_does_in_every_case() {
    every_case(&Z3805A);
}

#[test]
#[ignore = "every clock edge of seconds of the 10 MHz: minutes, optimized"]
fn the_58503a_z3801a_lca_model_reads_as_its_netlist_does_in_every_case() {
    every_case(&FPGA_58503A_Z3801A);
}

#[test]
#[ignore = "every clock edge of seconds of the 10 MHz: minutes, optimized"]
fn the_58503b_lca_model_reads_as_its_netlist_does_in_every_case() {
    every_case(&FPGA_58503B);
}

#[test]
#[ignore = "every clock edge of seconds of the 20 MHz: minutes, optimized"]
fn the_z3815a_lca_model_reads_as_its_netlist_does_in_every_case() {
    every_case(&Z3815A);
}

#[test]
#[ignore = "every clock edge of seconds of the 20 MHz: minutes, optimized"]
fn the_z3816a_lca_model_reads_as_its_netlist_does_in_every_case() {
    every_case(&Z3816A);
}

#[test]
#[ignore = "every clock edge of seconds of the 10 MHz: minutes, optimized"]
fn the_z3817a_lca_model_reads_as_its_netlist_does_in_every_case() {
    every_case(&Z3817A);
}

#[test]
fn a_netlist_line_it_does_not_know_is_refused() {
    assert!(matches!(
        Netlist::parse("part XC3042\nclb AA f=zz"),
        Err(NetlistError::Malformed { line: 2, .. })
    ));
    assert!(matches!(
        Netlist::parse("tbuf TBUF1 I=AA.X T=AB.X O=TBUF1.O"),
        Err(NetlistError::Unsupported { line: 1, .. })
    ));
    let mut lca = Lca::new(&NETLIST_Z3805A);
    assert_eq!(
        lca.drive(Pin(1), Drive::Off),
        Err(PinError::NotBonded(Pin(1)))
    );
    assert_eq!(lca.now(), Picoseconds::default());
}

#[test]
fn two_clocks_repeat_in_their_joint_period() {
    let lca = bench(&Z3815A);
    let period = Picoseconds(TICK);
    assert_eq!(lca.period(), Some(period));
    assert_eq!(bench(&Z3805A).period(), Some(period));
    // P53's rise a whole period on, once P7 has begun at 50 ns
    let start = Picoseconds(TICK + TICK / 4);
    assert_eq!(lca.period_start(), Some(start));
    let at = |quarters: u64| Picoseconds(start.0 + quarters * TICK / 4);
    let (fast, slow) = (Pin(53), Pin(7));
    assert_eq!(
        lca.edges_between(start, Picoseconds(start.0 + period.0)),
        [
            (at(0), fast, true),
            (at(1), fast, false),
            (at(1), slow, true),
            (at(2), fast, true),
            (at(3), fast, false),
            (at(3), slow, false),
        ]
    );
    assert_eq!(lca.edges_in(period), 6);
}
