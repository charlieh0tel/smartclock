//! tools/fpga/differential.py's fixtures, an image's netlist run in
//! Icarus Verilog on a set of cases, as the tests of the models of its
//! design read them: the LCA model's (lca.rs), for every image, and the
//! hand-written models', the Z3805A's (z3805a.rs) and the Z3817A's
//! (z3817a.rs), for theirs.

/// The Z3805A's netlist's reads and internal 1 PPS for each of
/// tools/fpga/differential.py's cases: the fixture it writes.
pub(super) const NETLIST: &str = include_str!("z3805a.fixture");
/// The Z3817A's (`differential.py --image z3817a`).
pub(super) const NETLIST_Z3817A: &str = include_str!("z3817a.fixture");

/// A case's stimulus, as differential.py writes it.
#[derive(Debug, Default)]
pub(super) struct Case {
    /// Its name, differential.py's.
    pub(super) name: String,
    /// The harness's options it ran with (`+edges`, `+p19`), which say
    /// what the harness printed.
    pub(super) options: Vec<String>,
    /// Each operation's board time, cycles, and its words.
    pub(super) operations: Vec<(u64, Vec<String>)>,
    /// The clock edges the netlist's internal 1 PPS rose on.
    pub(super) pulses: Vec<u128>,
    /// The start's interval's widths, picoseconds.
    pub(super) intervals: Vec<u128>,
    /// The watched pins' changes, in time order.
    pub(super) edges: Vec<Edge>,
    /// The Z3817A's status bits and outputs as they changed: the clock
    /// edge after which each did, its net's name and its new level.
    pub(super) flags: Vec<(u64, String, bool)>,
}

/// A watched pin's change.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) struct Edge {
    /// The package pin, by number.
    pub(super) pin: u8,
    /// When, picoseconds of the netlist's time.
    pub(super) at: u128,
    pub(super) high: bool,
}

/// The cases of the fixture `text`, in its order.
pub(super) fn cases(text: &str) -> Vec<Case> {
    let mut cases: Vec<Case> = Vec::new();
    for line in text.lines() {
        let words: Vec<&str> = line.split_whitespace().collect();
        match words.as_slice() {
            ["case", name] => cases.push(Case {
                name: (*name).to_owned(),
                ..Case::default()
            }),
            ["options", options @ ..] => {
                cases.last_mut().expect("a case").options =
                    options.iter().map(|option| (*option).to_owned()).collect();
            }
            ["pps", tick] => cases
                .last_mut()
                .expect("a case")
                .pulses
                .push(tick.parse().expect("a tick")),
            ["interval", width] => cases
                .last_mut()
                .expect("a case")
                .intervals
                .push(width.parse().expect("a width")),
            ["flag", tick, name, level] => cases.last_mut().expect("a case").flags.push((
                tick.parse().expect("a tick"),
                (*name).to_owned(),
                *level == "1",
            )),
            ["edge", pin, at, level] => cases.last_mut().expect("a case").edges.push(Edge {
                pin: pin.parse().expect("a pin"),
                at: at.parse().expect("a time"),
                high: match *level {
                    "0" => false,
                    "1" => true,
                    other => panic!("{other} is not a level"),
                },
            }),
            [at, rest @ ..] => cases.last_mut().expect("a case").operations.push((
                at.parse().expect("a time"),
                rest.iter().map(|word| (*word).to_owned()).collect(),
            )),
            [] => {}
        }
    }
    cases
}

/// A byte the fixture writes in hex.
pub(super) fn hex(word: &str) -> u8 {
    u8::from_str_radix(word, 16).expect("hex")
}
