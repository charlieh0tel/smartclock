//! The LCA model: the XC3042's Logic Cell Array as a bitstream configures
//! it, executed, rather than a hand-written model of what the design does
//! (docs/firmware/fpga-codegen.md, "The LCA model").  It runs the netlist
//! `tools/fpga/lcanet.py` writes from the bitstream's decode, the same
//! decode `tools/fpga/netlist.py` writes as the Verilog Icarus runs, and
//! gives its blocks the behavior of that Verilog's models
//! (`tools/fpga/xc3000.v`), event by event, so that the two agree to the
//! picosecond:
//!
//! - every logic block's and I/O block's output, and an I/O block's pad
//!   driver, changes one nominal delay, `BLOCK_DELAY`, after what it
//!   follows, a change undone within the delay never reaching it (the
//!   Verilog's inertial `assign #1`); routing and the clock buffers take
//!   none, a buffer's output following its input before any block sees
//!   either, so that the order blocks are evaluated in at an instant
//!   never matters;
//! - a flip-flop takes its data on its clock's rising edge, or falling
//!   with its block's inversion, as its data stands at that instant; the
//!   chip's RESET, Low, and a logic block's RD reset them, as do latches
//!   (7-8, 7-9);
//! - a pad is what the I/O block drives on it, else what drives it from
//!   outside, else High with its pull-up and Low without, where the
//!   Verilog reads an undriven pad as unknown.
//!
//! `run_clocked` evaluates every edge of the model's own clock, which is
//! exact and far from real time; fast-forward (`fast_forward.rs`) moves it
//! on as `run_clocked` would without clocking most of them.  The board
//! runs it, fast-forwarded, where its file says `design = "lca"`
//! (executed.rs); its tests hold it to the netlist's Icarus run.

use std::collections::HashMap;
use std::collections::VecDeque;
use std::fmt;

use thiserror::Error;

/// Picoseconds of the evaluator's own time, from its start.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub(super) struct Picoseconds(pub(super) u64);

impl std::ops::Add for Picoseconds {
    type Output = Self;

    fn add(self, other: Self) -> Self {
        Self(self.0 + other.0)
    }
}

/// The delay of every logic and I/O block's output and pad driver:
/// xc3000.v's `BLOCK_DELAY`, one nanosecond.
const BLOCK_DELAY: Picoseconds = Picoseconds(1_000);

/// A net, by its index among the netlist's.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub(super) struct Net(u32);

impl Net {
    fn index(self) -> usize {
        self.0 as usize
    }
}

/// A package pin, `P1` to `P84`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub(super) struct Pin(pub(super) u8);

impl fmt::Display for Pin {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "P{}", self.0)
    }
}

/// A delayed output, a block's or a pad driver's, by its index among the
/// netlist's.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Slot(u32);

/// A block, by its index among the netlist's.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct BlockIndex(u32);

/// Why a netlist does not load.
#[derive(Debug, Error, PartialEq, Eq)]
pub(super) enum NetlistError {
    /// A line that is not lcanet.py's.
    #[error("netlist line {line}: {message}")]
    Malformed { line: usize, message: String },
    /// A line lcanet.py writes that this evaluator does not run.
    #[error("netlist line {line}: {what} is not modeled")]
    Unsupported { line: usize, what: String },
}

/// Why a pin cannot be driven or read.
#[derive(Debug, Error, PartialEq, Eq)]
pub(super) enum PinError {
    #[error("no I/O block is bonded to {0}")]
    NotBonded(Pin),
}

/// A table input's selection: xc3000.v's `select`, any other name
/// reading Low.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Selection {
    B,
    C,
    D,
    E,
    Qx,
    Qy,
    Low,
}

/// What a logic block's flip-flop takes: F, G or DI.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Data {
    F,
    G,
    Di,
}

/// A logic block's inputs, by their index in `Clb::inputs`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum ClbInput {
    A,
    B,
    C,
    D,
    E,
    Di,
    Ec,
    K,
    Rd,
}

/// The logic block's inputs by their names in the netlist.
const CLB_INPUTS: [(&str, ClbInput); 9] = [
    ("A", ClbInput::A),
    ("B", ClbInput::B),
    ("C", ClbInput::C),
    ("D", ClbInput::D),
    ("E", ClbInput::E),
    ("DI", ClbInput::Di),
    ("EC", ClbInput::Ec),
    ("K", ClbInput::K),
    ("RD", ClbInput::Rd),
];

/// A configurable logic block, xc3000.v's `xc3000_clb`: Figure 5 (7-9)
/// and Figure 6 (7-10).
#[derive(Debug, Clone, PartialEq, Eq)]
struct Clb {
    /// The F and G function tables: bit i is the value where input j is
    /// bit j of i, input 0 A.
    f_table: u16,
    g_table: u16,
    /// Inputs 1 to 3 of each table.
    f_inputs: [Selection; 3],
    g_inputs: [Selection; 3],
    /// Modes F and FGM: E selects G when High and F when Low, both
    /// outputs carrying the one result.
    merged: bool,
    /// What QX and QY take.
    x_data: Data,
    y_data: Data,
    /// X is QX rather than F; Y is QY rather than G.
    x_registered: bool,
    y_registered: bool,
    /// EC enables the clock; RD resets; K's falling edge clocks.
    enable_clock: bool,
    direct_reset: bool,
    clock_invert: bool,
    /// The net on each input, by `ClbInput`, none reading Low.
    inputs: [Option<Net>; CLB_INPUTS.len()],
    /// The X and Y outputs, where they drive a net.
    x: Option<Slot>,
    y: Option<Slot>,
}

/// An I/O block, xc3000.v's `xc3000_iob`: Figure 4 (7-7) and its
/// options (7-8).
#[derive(Debug, Clone, PartialEq, Eq)]
struct Iob {
    /// The pin it is bonded to, if any.
    pin: Option<Pin>,
    /// The output buffer is on; through the output flip-flop; inverted;
    /// that flip-flop on OK's falling edge.
    out_enable: bool,
    out_registered: bool,
    out_invert: bool,
    ok_invert: bool,
    /// 3-stated by T, Low on, or High on with the inversion.
    three_state: bool,
    three_state_invert: bool,
    /// Q is the input's storage, a latch or a flip-flop, on IK, falling
    /// with the inversion.
    in_registered: bool,
    in_latch: bool,
    ik_invert: bool,
    /// The passive pull-up.
    pullup: bool,
    /// The nets on O, T, OK and IK, none reading Low.
    o: Option<Net>,
    t: Option<Net>,
    ok: Option<Net>,
    ik: Option<Net>,
    /// The pad's driver, with the output buffer on.
    driver: Option<Slot>,
    /// The I and Q outputs, where they drive a net.
    i: Option<Slot>,
    q: Option<Slot>,
}

/// A clock buffer, GCLK or ACLK (7-17): its output follows its input at
/// once.
#[derive(Debug, Clone, PartialEq, Eq)]
struct Buffer {
    input: Option<Net>,
    output: Option<Net>,
}

/// A block of the netlist.
#[derive(Debug, Clone, PartialEq, Eq)]
enum Block {
    Clb(Clb),
    Iob(Iob),
    Buffer(Buffer),
}

/// What a delayed output drives.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Target {
    Net(Net),
    /// The pad of the I/O block.
    Pad(BlockIndex),
}

/// A design's netlist, as lcanet.py writes it.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub(super) struct Netlist {
    /// The SHA-256 of the bitstream it was decoded from, hex.
    bitstream: String,
    /// Each net's name, the pin that drives it.
    nets: Vec<String>,
    /// The blocks and their names.
    blocks: Vec<Block>,
    names: Vec<String>,
    /// What each delayed output drives.
    slots: Vec<Target>,
    /// The blocks each net reaches, clock buffers aside.
    fanout: Vec<Vec<BlockIndex>>,
    /// The nets clock buffers drive from each net.
    buffered: Vec<Vec<Net>>,
    /// The I/O block bonded to each pin.
    pins: HashMap<Pin, BlockIndex>,
}

/// A line's `KEY=VALUE` words.
struct Words<'a> {
    line: usize,
    words: HashMap<&'a str, &'a str>,
}

impl<'a> Words<'a> {
    fn new(line: usize, words: &[&'a str]) -> Result<Self, NetlistError> {
        let words = words
            .iter()
            .map(|word| {
                word.split_once('=').ok_or_else(|| NetlistError::Malformed {
                    line,
                    message: format!("{word} is not KEY=VALUE"),
                })
            })
            .collect::<Result<_, _>>()?;
        Ok(Self { line, words })
    }

    fn malformed(&self, message: String) -> NetlistError {
        NetlistError::Malformed {
            line: self.line,
            message,
        }
    }

    fn get(&self, key: &str) -> Result<&'a str, NetlistError> {
        self.words
            .get(key)
            .copied()
            .ok_or_else(|| self.malformed(format!("no {key}")))
    }

    fn flag(&self, key: &str) -> Result<bool, NetlistError> {
        match self.get(key)? {
            "0" => Ok(false),
            "1" => Ok(true),
            other => Err(self.malformed(format!("{key}={other} is not 0 or 1"))),
        }
    }

    fn table(&self, key: &str) -> Result<u16, NetlistError> {
        let value = self.get(key)?;
        u16::from_str_radix(value, 16)
            .map_err(|_| self.malformed(format!("{key}={value} is not a table")))
    }

    fn selections(&self, key: &str) -> Result<[Selection; 3], NetlistError> {
        let value = self.get(key)?;
        let selections: Vec<Selection> = value
            .split(',')
            .map(|name| match name {
                "B" => Ok(Selection::B),
                "C" => Ok(Selection::C),
                "D" => Ok(Selection::D),
                "E" => Ok(Selection::E),
                "QX" => Ok(Selection::Qx),
                "QY" => Ok(Selection::Qy),
                "-" => Ok(Selection::Low),
                other => Err(self.malformed(format!("{key}: {other} is not a selection"))),
            })
            .collect::<Result<_, _>>()?;
        selections
            .try_into()
            .map_err(|_| self.malformed(format!("{key}={value} is not three selections")))
    }

    fn data(&self, key: &str) -> Result<Data, NetlistError> {
        match self.get(key)? {
            "F" => Ok(Data::F),
            "G" => Ok(Data::G),
            "DI" => Ok(Data::Di),
            other => Err(self.malformed(format!("{key}={other} is not F, G or DI"))),
        }
    }

    fn output(&self, key: &str, registered: &str) -> Result<bool, NetlistError> {
        let value = self.get(key)?;
        if value == registered {
            Ok(true)
        } else if value.len() == 1 {
            Ok(false)
        } else {
            Err(self.malformed(format!("{key}={value} is not an output")))
        }
    }
}

/// A netlist as it is built, line by line.
#[derive(Default)]
struct Builder {
    netlist: Netlist,
    /// Each net by its name.
    named: HashMap<String, Net>,
    /// The nets a delayed output or a buffer drives.
    driven: Vec<bool>,
}

impl Builder {
    fn net(&mut self, name: &str) -> Net {
        if let Some(net) = self.named.get(name) {
            return *net;
        }
        let net = Net(self.netlist.nets.len() as u32);
        self.netlist.nets.push(name.to_owned());
        self.netlist.fanout.push(Vec::new());
        self.netlist.buffered.push(Vec::new());
        self.driven.push(false);
        self.named.insert(name.to_owned(), net);
        net
    }

    fn next_block(&self) -> BlockIndex {
        BlockIndex(self.netlist.blocks.len() as u32)
    }

    /// The net on the input `key`, which `block` reads.
    fn input(&mut self, words: &Words<'_>, key: &str, block: BlockIndex) -> Option<Net> {
        let name = words.words.get(key)?;
        let net = self.net(name);
        let fanout = &mut self.netlist.fanout[net.index()];
        if fanout.last() != Some(&block) {
            fanout.push(block);
        }
        Some(net)
    }

    /// The net `key` drives, if any, which it alone drives.
    fn driving(&mut self, words: &Words<'_>, key: &str) -> Result<Option<Net>, NetlistError> {
        let Some(name) = words.words.get(key) else {
            return Ok(None);
        };
        let net = self.net(name);
        if std::mem::replace(&mut self.driven[net.index()], true) {
            return Err(words.malformed(format!("{name} has two drivers")));
        }
        Ok(Some(net))
    }

    fn slot(&mut self, target: Target) -> Slot {
        let slot = Slot(self.netlist.slots.len() as u32);
        self.netlist.slots.push(target);
        slot
    }

    /// The delayed output `key` onto its net, if it drives one.
    fn output(&mut self, words: &Words<'_>, key: &str) -> Result<Option<Slot>, NetlistError> {
        Ok(self
            .driving(words, key)?
            .map(|net| self.slot(Target::Net(net))))
    }

    fn clb(&mut self, words: &Words<'_>) -> Result<Block, NetlistError> {
        let block = self.next_block();
        let mut inputs = [None; CLB_INPUTS.len()];
        for (name, input) in CLB_INPUTS {
            inputs[input as usize] = self.input(words, name, block);
        }
        Ok(Block::Clb(Clb {
            f_table: words.table("f")?,
            g_table: words.table("g")?,
            f_inputs: words.selections("finputs")?,
            g_inputs: words.selections("ginputs")?,
            merged: words.flag("merged")?,
            x_data: words.data("xdata")?,
            y_data: words.data("ydata")?,
            x_registered: words.output("xout", "QX")?,
            y_registered: words.output("yout", "QY")?,
            enable_clock: words.flag("ec")?,
            direct_reset: words.flag("rd")?,
            clock_invert: words.flag("kinvert")?,
            inputs,
            x: self.output(words, "X")?,
            y: self.output(words, "Y")?,
        }))
    }

    fn iob(&mut self, words: &Words<'_>) -> Result<Block, NetlistError> {
        let block = self.next_block();
        let pin = match words.get("pin")? {
            "-" => None,
            name => Some(
                name.strip_prefix('P')
                    .and_then(|number| number.parse().ok())
                    .map(Pin)
                    .ok_or_else(|| words.malformed(format!("{name} is not a pin")))?,
            ),
        };
        if let Some(pin) = pin
            && self.netlist.pins.insert(pin, block).is_some()
        {
            return Err(words.malformed(format!("{pin} is bonded twice")));
        }
        let out_enable = words.flag("out")?;
        Ok(Block::Iob(Iob {
            pin,
            out_enable,
            out_registered: words.flag("outregistered")?,
            out_invert: words.flag("outinvert")?,
            ok_invert: words.flag("okinvert")?,
            three_state: words.flag("threestate")?,
            three_state_invert: words.flag("threestateinvert")?,
            in_registered: words.flag("inregistered")?,
            in_latch: words.flag("inlatch")?,
            ik_invert: words.flag("ikinvert")?,
            pullup: words.flag("pullup")?,
            o: self.input(words, "O", block),
            t: self.input(words, "T", block),
            ok: self.input(words, "OK", block),
            ik: self.input(words, "IK", block),
            driver: out_enable.then(|| self.slot(Target::Pad(block))),
            i: self.output(words, "I")?,
            q: self.output(words, "Q")?,
        }))
    }

    fn buffer(&mut self, words: &Words<'_>) -> Result<Block, NetlistError> {
        let input = words.words.get("I").map(|name| self.net(name));
        let output = self.driving(words, "O")?;
        if let (Some(input), Some(output)) = (input, output) {
            self.netlist.buffered[input.index()].push(output);
        }
        Ok(Block::Buffer(Buffer { input, output }))
    }
}

impl Netlist {
    /// The netlist lcanet.py wrote as `text`.
    pub(super) fn parse(text: &str) -> Result<Self, NetlistError> {
        let mut builder = Builder::default();
        for (index, line) in text.lines().enumerate() {
            let line_number = index + 1;
            let words: Vec<&str> = line.split_whitespace().collect();
            let unsupported = |what: &str| NetlistError::Unsupported {
                line: line_number,
                what: what.to_owned(),
            };
            let block = match words.as_slice() {
                [] => continue,
                [first, ..] if first.starts_with('#') => continue,
                ["part", "XC3042"] | ["decoder", _] => continue,
                ["part", part] => return Err(unsupported(part)),
                ["bitstream", hash] => {
                    builder.netlist.bitstream = (*hash).to_owned();
                    continue;
                }
                ["tri", ..] => return Err(unsupported("a 3-state Longline")),
                ["tbuf", ..] => return Err(unsupported("a 3-state buffer")),
                [kind @ ("clb" | "iob" | "buffer"), name, rest @ ..] => {
                    let words = Words::new(line_number, rest)?;
                    let block = match *kind {
                        "clb" => builder.clb(&words)?,
                        "iob" => builder.iob(&words)?,
                        _ => builder.buffer(&words)?,
                    };
                    builder.netlist.names.push((*name).to_owned());
                    block
                }
                _ => {
                    return Err(NetlistError::Malformed {
                        line: line_number,
                        message: format!("{line:?} is not a netlist line"),
                    });
                }
            };
            builder.netlist.blocks.push(block);
        }
        Ok(builder.netlist)
    }

    /// The net named `name`, the pin that drives it.
    #[cfg(test)]
    pub(super) fn net(&self, name: &str) -> Option<Net> {
        self.nets
            .iter()
            .position(|known| known == name)
            .map(|index| Net(index as u32))
    }

    /// The SHA-256 of the bitstream it was decoded from, hex.
    #[cfg(test)]
    pub(super) fn bitstream(&self) -> &str {
        &self.bitstream
    }

    fn iob(&self, block: BlockIndex) -> &Iob {
        match &self.blocks[block.0 as usize] {
            Block::Iob(iob) => iob,
            other => unreachable!("{other:?} is not an I/O block"),
        }
    }
}

/// What drives a pad from outside the part.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Hash)]
pub(super) enum Drive {
    /// Nothing.
    #[default]
    Off,
    /// A driver, which the part's own output buffer overrides.
    Strong(bool),
    /// A pull-up or pull-down, which any driver overrides.
    Pull(bool),
}

/// A delayed output's level: Low, High, or a pad driver off.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
enum Level {
    #[default]
    Low,
    High,
    Off,
}

impl From<bool> for Level {
    fn from(high: bool) -> Self {
        if high { Self::High } else { Self::Low }
    }
}

/// A delayed output's state: what it drives now, and the change on its
/// way, which a later change replaces.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
struct Output {
    level: Level,
    pending: Option<Level>,
    /// Counts the changes scheduled and dropped, so that an event of a
    /// dropped one is passed over.
    generation: u32,
}

/// A change of a delayed output, due `BLOCK_DELAY` after it was
/// scheduled.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Event {
    at: Picoseconds,
    slot: Slot,
    generation: u32,
}

/// A logic block's storage: its flip-flops and its clock as last seen,
/// for the clock's edges.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
struct ClbState {
    qx: bool,
    qy: bool,
    clock: bool,
}

/// An I/O block's storage, its clocks as last seen, its pad and what
/// drives that from outside.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
struct IobState {
    out_flip_flop: bool,
    in_storage: bool,
    out_clock: bool,
    in_clock: bool,
    pad: bool,
    external: Drive,
}

/// A block's state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum BlockState {
    Clb(ClbState),
    Iob(IobState),
    Buffer,
}

/// What a transition was seen on.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) enum Probe {
    Net(Net),
    Pin(Pin),
}

/// A watched net's or pad's change.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) struct Transition {
    pub(super) at: Picoseconds,
    pub(super) probe: Probe,
    pub(super) high: bool,
}

/// A free-running clock on a pin, which the model drives itself: High at
/// `first_rise`, then Low and High in turn every `half_period`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) struct Clock {
    pub(super) pin: Pin,
    pub(super) first_rise: Picoseconds,
    pub(super) half_period: Picoseconds,
}

impl Clock {
    /// The time of edge `edge`, the first rise 0.
    pub(super) fn edge(&self, edge: u64) -> Picoseconds {
        Picoseconds(self.first_rise.0 + edge * self.half_period.0)
    }

    /// The level edge `edge` drives.
    fn rises(edge: u64) -> bool {
        edge.is_multiple_of(2)
    }

    /// One period, two half periods.
    pub(super) fn period(&self) -> Picoseconds {
        Picoseconds(2 * self.half_period.0)
    }

    /// The first of its edges at `at` or after.
    fn edge_at(&self, at: Picoseconds) -> u64 {
        at.0.saturating_sub(self.first_rise.0)
            .div_ceil(self.half_period.0)
    }
}

/// A clock the model drives, and the next of its edges to drive.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Driven {
    clock: Clock,
    next_edge: u64,
}

/// The greatest common divisor of `one` and `other`.
fn divisor(one: u64, other: u64) -> u64 {
    if other == 0 {
        one
    } else {
        divisor(other, one % other)
    }
}

/// What decides everything the model does next, to compare two runs of
/// it: the generations of the delayed outputs, which count changes
/// dropped, aside, and the changes on their way in time order alone.
#[cfg(test)]
#[derive(Debug, PartialEq, Eq)]
pub(super) struct Snapshot {
    now: Picoseconds,
    reset: bool,
    nets: Vec<bool>,
    blocks: Vec<BlockState>,
    outputs: Vec<(Level, Option<Level>)>,
    events: Vec<(Picoseconds, u32)>,
    next_edges: Vec<u64>,
}

/// The netlist running: the LCA model's state.
#[derive(Debug, Clone)]
pub(super) struct Lca<'a> {
    netlist: &'a Netlist,
    /// The time it has run to.
    now: Picoseconds,
    /// Each net's level.
    nets: Vec<bool>,
    /// The chip's global reset, RESET Low.
    reset: bool,
    blocks: Vec<BlockState>,
    outputs: Vec<Output>,
    /// The delayed outputs' changes in time order: every delay is the
    /// same, so the order they were scheduled in.
    queue: VecDeque<Event>,
    /// The blocks to evaluate at this instant, and whether each is among
    /// them.
    dirty: Vec<BlockIndex>,
    queued: Vec<bool>,
    /// The nets and the I/O blocks' pads watched, and what they did.
    watched_nets: Vec<bool>,
    watched_pads: Vec<bool>,
    transitions: Vec<Transition>,
    /// The blocks evaluated, a measure of the work done.
    evaluations: u64,
    /// The clocks it drives itself, in the order they edge at an
    /// instant several do.
    clocks: Vec<Driven>,
}

impl<'a> Lca<'a> {
    /// `netlist` configured, at time 0, RESET Low and nothing driving
    /// its pads.
    pub(super) fn new(netlist: &'a Netlist) -> Self {
        let blocks = netlist
            .blocks
            .iter()
            .map(|block| match block {
                Block::Clb(_) => BlockState::Clb(ClbState::default()),
                Block::Iob(_) => BlockState::Iob(IobState::default()),
                Block::Buffer(_) => BlockState::Buffer,
            })
            .collect();
        let outputs = netlist
            .slots
            .iter()
            .map(|target| Output {
                level: match target {
                    Target::Net(_) => Level::Low,
                    Target::Pad(_) => Level::Off,
                },
                ..Output::default()
            })
            .collect();
        let count = netlist.blocks.len();
        let mut lca = Self {
            netlist,
            now: Picoseconds::default(),
            nets: vec![false; netlist.nets.len()],
            reset: true,
            blocks,
            outputs,
            queue: VecDeque::new(),
            dirty: Vec::new(),
            queued: vec![false; count],
            watched_nets: vec![false; netlist.nets.len()],
            watched_pads: vec![false; count],
            transitions: Vec::new(),
            evaluations: 0,
            clocks: Vec::new(),
        };
        for block in 0..count {
            lca.mark(BlockIndex(block as u32));
        }
        lca.settle();
        lca
    }

    /// The time it has run to.
    pub(super) fn now(&self) -> Picoseconds {
        self.now
    }

    /// The blocks evaluated so far.
    #[cfg(test)]
    pub(super) fn evaluations(&self) -> u64 {
        self.evaluations
    }

    /// Its state, to compare with another run's.
    #[cfg(test)]
    pub(super) fn snapshot(&self) -> Snapshot {
        let mut events: Vec<(Picoseconds, u32)> = self
            .queue
            .iter()
            .filter(|event| {
                let output = &self.outputs[event.slot.0 as usize];
                output.generation == event.generation && output.pending.is_some()
            })
            .map(|event| (event.at, event.slot.0))
            .collect();
        events.sort_unstable();
        Snapshot {
            now: self.now,
            reset: self.reset,
            nets: self.nets.clone(),
            blocks: self.blocks.clone(),
            outputs: self
                .outputs
                .iter()
                .map(|output| (output.level, output.pending))
                .collect(),
            events,
            next_edges: self.clocks.iter().map(|driven| driven.next_edge).collect(),
        }
    }

    /// The chip's RESET pin driven to `high`, now.
    pub(super) fn set_reset(&mut self, high: bool) {
        if self.reset == !high {
            return;
        }
        self.reset = !high;
        for (index, state) in self.blocks.iter().enumerate() {
            if !matches!(state, BlockState::Buffer) && !self.queued[index] {
                self.queued[index] = true;
                self.dirty.push(BlockIndex(index as u32));
            }
        }
        self.settle();
    }

    fn bonded(&self, pin: Pin) -> Result<BlockIndex, PinError> {
        self.netlist
            .pins
            .get(&pin)
            .copied()
            .ok_or(PinError::NotBonded(pin))
    }

    /// `pin` driven from outside as `drive`, now.
    pub(super) fn drive(&mut self, pin: Pin, drive: Drive) -> Result<(), PinError> {
        let block = self.bonded(pin)?;
        if let BlockState::Iob(state) = &mut self.blocks[block.0 as usize] {
            state.external = drive;
        }
        self.repad(block);
        self.settle();
        Ok(())
    }

    /// `pin`'s level now.
    pub(super) fn pin(&self, pin: Pin) -> Result<bool, PinError> {
        let block = self.bonded(pin)?;
        Ok(match self.blocks[block.0 as usize] {
            BlockState::Iob(state) => state.pad,
            _ => false,
        })
    }

    /// `probe`'s changes logged from now on.
    pub(super) fn watch(&mut self, probe: Probe) -> Result<(), PinError> {
        match probe {
            Probe::Net(net) => self.watched_nets[net.index()] = true,
            Probe::Pin(pin) => {
                let block = self.bonded(pin)?;
                self.watched_pads[block.0 as usize] = true;
            }
        }
        Ok(())
    }

    /// A watched net or pin has changed since the changes were last
    /// taken.
    pub(super) fn changed(&self) -> bool {
        !self.transitions.is_empty()
    }

    /// The watched changes since the last call.
    pub(super) fn take_transitions(&mut self) -> Vec<Transition> {
        std::mem::take(&mut self.transitions)
    }

    /// `clock` driven from now on, beside those it drives already, its
    /// pin at once at the level before its next edge, an edge due now
    /// still to come.  At an instant several clocks edge, the one added
    /// first edges first.
    pub(super) fn add_clock(&mut self, clock: Clock) -> Result<(), PinError> {
        let next_edge = clock.edge_at(self.now);
        self.drive(clock.pin, Drive::Strong(!Clock::rises(next_edge)))?;
        self.clocks.push(Driven { clock, next_edge });
        Ok(())
    }

    /// The clocks it drives, in the order they edge at an instant.
    pub(super) fn clocks(&self) -> impl Iterator<Item = Clock> + '_ {
        self.clocks.iter().map(|driven| driven.clock)
    }

    /// The period its clocks repeat in together, the least common
    /// multiple of theirs: none without a clock, or where it overflows.
    pub(super) fn period(&self) -> Option<Picoseconds> {
        let mut periods = self.clocks().map(|clock| clock.period().0);
        let first = periods.next()?;
        periods
            .try_fold(first, |joint, period| {
                joint.checked_mul(period / divisor(joint, period))
            })
            .map(Picoseconds)
    }

    /// The edges its clocks make in `period`, one of theirs.
    pub(super) fn edges_in(&self, period: Picoseconds) -> u64 {
        self.clocks()
            .map(|clock| period.0 / clock.half_period.0)
            .sum()
    }

    /// The start of its next period, still to come: a rise of the first
    /// clock, a whole number of periods from its first, once every clock
    /// has begun, since before its first rise a clock's pin holds Low.
    pub(super) fn period_start(&self) -> Option<Picoseconds> {
        let period = self.period()?.0;
        let first = self.clocks.first()?;
        let edges = period / first.clock.half_period.0;
        let begun = self.clocks().map(|clock| clock.first_rise.0).max()?;
        let edge = first.next_edge.next_multiple_of(edges);
        let behind = begun.saturating_sub(first.clock.edge(edge).0);
        Some(first.clock.edge(edge + behind.div_ceil(period) * edges))
    }

    /// The clocks' edges from `start` to before `end`, in the order the
    /// model makes them: in time, and at an instant in the clocks' order.
    /// Each is its time, its clock's pin and the level it drives.
    pub(super) fn edges_between(
        &self,
        start: Picoseconds,
        end: Picoseconds,
    ) -> Vec<(Picoseconds, Pin, bool)> {
        let mut edges = Vec::new();
        for (index, clock) in self.clocks().enumerate() {
            let mut edge = clock.edge_at(start);
            while clock.edge(edge) < end {
                edges.push((clock.edge(edge), index, clock.pin, Clock::rises(edge)));
                edge += 1;
            }
        }
        edges.sort_by_key(|(at, index, _, _)| (*at, *index));
        edges
            .into_iter()
            .map(|(at, _, pin, high)| (at, pin, high))
            .collect()
    }

    /// The clock that edges next, the first of them at an instant several
    /// do, and when.
    fn next_clock_edge(&self) -> Option<(usize, Picoseconds)> {
        self.clocks
            .iter()
            .enumerate()
            .map(|(index, driven)| (index, driven.clock.edge(driven.next_edge)))
            .min_by_key(|(index, at)| (*at, *index))
    }

    /// Time set to `at`, each clock's next edge its first at `at` or
    /// after, nothing else told.
    fn place(&mut self, at: Picoseconds) {
        self.now = at;
        for driven in &mut self.clocks {
            driven.next_edge = driven.clock.edge_at(at);
        }
    }

    /// Time moved on to `until` with the clocks driven edge by edge, an
    /// edge due at `until` driven: exact, and the reference fast-forward
    /// is held to.
    pub(super) fn run_clocked(&mut self, until: Picoseconds) {
        while let Some((index, at)) = self.next_clock_edge().filter(|(_, at)| *at <= until) {
            self.run_until(at);
            let driven = &mut self.clocks[index];
            let (pin, high) = (driven.clock.pin, Clock::rises(driven.next_edge));
            driven.next_edge += 1;
            self.drive(pin, Drive::Strong(high))
                .expect("a clock's pin was bonded when it was added");
        }
        self.run_until(until);
    }

    /// Nothing is on its way: no delayed output is changing.
    pub(super) fn quiet(&self) -> bool {
        self.queue.iter().all(|event| {
            let output = &self.outputs[event.slot.0 as usize];
            output.generation != event.generation || output.pending.is_none()
        })
    }

    /// Time moved on, clocked, to its next period's start, every change
    /// due by then made but the edges due then still to come.
    pub(super) fn run_to_period(&mut self) {
        let Some(at) = self.period_start() else {
            return;
        };
        if at > self.now {
            self.run_clocked(Picoseconds(at.0 - 1));
        }
        self.run_until(at);
    }

    /// Time moved on to `until`, every change due by then made.
    pub(super) fn run_until(&mut self, until: Picoseconds) {
        while let Some(event) = self.queue.front().copied()
            && event.at <= until
        {
            self.now = event.at;
            while let Some(event) = self.queue.front().copied()
                && event.at == self.now
            {
                self.queue.pop_front();
                self.apply(event);
            }
            self.settle();
        }
        self.now = self.now.max(until);
    }

    fn mark(&mut self, block: BlockIndex) {
        let index = block.0 as usize;
        if !self.queued[index] {
            self.queued[index] = true;
            self.dirty.push(block);
        }
    }

    /// Every block whose inputs changed at this instant evaluated, until
    /// none is left: only a clock buffer's output changes at once.
    fn settle(&mut self) {
        while let Some(block) = self.dirty.pop() {
            self.queued[block.0 as usize] = false;
            self.evaluate(block);
        }
    }

    fn set_net(&mut self, net: Net, high: bool) {
        if self.nets[net.index()] == high {
            return;
        }
        self.nets[net.index()] = high;
        if self.watched_nets[net.index()] {
            self.transitions.push(Transition {
                at: self.now,
                probe: Probe::Net(net),
                high,
            });
        }
        let netlist = self.netlist;
        for block in &netlist.fanout[net.index()] {
            self.mark(*block);
        }
        for output in &netlist.buffered[net.index()] {
            self.set_net(*output, high);
        }
    }

    /// The I/O block's pad resolved again: its own driver, else what
    /// drives it from outside, else its pull-up.
    fn repad(&mut self, block: BlockIndex) {
        let netlist = self.netlist;
        let iob = netlist.iob(block);
        let driven = iob
            .driver
            .map_or(Level::Off, |slot| self.outputs[slot.0 as usize].level);
        let BlockState::Iob(state) = &mut self.blocks[block.0 as usize] else {
            return;
        };
        let pad = match (driven, state.external) {
            (Level::High, _) => true,
            (Level::Low, _) => false,
            (Level::Off, Drive::Strong(high) | Drive::Pull(high)) => high,
            (Level::Off, Drive::Off) => iob.pullup,
        };
        if state.pad == pad {
            return;
        }
        state.pad = pad;
        if self.watched_pads[block.0 as usize]
            && let Some(pin) = iob.pin
        {
            self.transitions.push(Transition {
                at: self.now,
                probe: Probe::Pin(pin),
                high: pad,
            });
        }
        self.mark(block);
    }

    fn apply(&mut self, event: Event) {
        let output = &mut self.outputs[event.slot.0 as usize];
        if output.generation != event.generation {
            return;
        }
        let Some(level) = output.pending.take() else {
            return;
        };
        output.level = level;
        match self.netlist.slots[event.slot.0 as usize] {
            Target::Net(net) => self.set_net(net, level == Level::High),
            Target::Pad(block) => self.repad(block),
        }
    }

    /// The delayed output `slot` to follow `level`, `BLOCK_DELAY` from
    /// now, in place of any change still on its way.
    fn schedule(&mut self, slot: Option<Slot>, level: Level) {
        let Some(slot) = slot else {
            return;
        };
        let output = &mut self.outputs[slot.0 as usize];
        if output.pending.unwrap_or(output.level) == level {
            return;
        }
        output.generation = output.generation.wrapping_add(1);
        if output.level == level {
            output.pending = None;
            return;
        }
        output.pending = Some(level);
        let event = Event {
            at: self.now + BLOCK_DELAY,
            slot,
            generation: output.generation,
        };
        self.queue.push_back(event);
    }

    fn level(&self, net: Option<Net>) -> bool {
        net.is_some_and(|net| self.nets[net.index()])
    }

    fn evaluate(&mut self, block: BlockIndex) {
        self.evaluations += 1;
        let netlist = self.netlist;
        let index = block.0 as usize;
        match (&netlist.blocks[index], self.blocks[index]) {
            (Block::Clb(clb), BlockState::Clb(state)) => self.evaluate_clb(index, clb, state),
            (Block::Iob(iob), BlockState::Iob(state)) => self.evaluate_iob(index, iob, state),
            (Block::Buffer(buffer), _) => {
                if let Some(output) = buffer.output {
                    self.set_net(output, self.level(buffer.input));
                }
            }
            (block, state) => unreachable!("{block:?} in the state {state:?}"),
        }
    }

    fn evaluate_clb(&mut self, index: usize, clb: &Clb, state: ClbState) {
        let input = |which: ClbInput| self.level(clb.inputs[which as usize]);
        let clock = input(ClbInput::K) != clb.clock_invert;
        let reset = self.reset || clb.direct_reset && input(ClbInput::Rd);
        let enabled = !clb.enable_clock || input(ClbInput::Ec);
        let (qx, qy) = if reset {
            (false, false)
        } else if clock && !state.clock && enabled {
            let (f, g) = clb.functions(&input, state.qx, state.qy);
            let data = |data: Data| match data {
                Data::F => f,
                Data::G => g,
                Data::Di => input(ClbInput::Di),
            };
            (data(clb.x_data), data(clb.y_data))
        } else {
            (state.qx, state.qy)
        };
        let (f, g) = clb.functions(&input, qx, qy);
        self.blocks[index] = BlockState::Clb(ClbState { qx, qy, clock });
        self.schedule(clb.x, Level::from(if clb.x_registered { qx } else { f }));
        self.schedule(clb.y, Level::from(if clb.y_registered { qy } else { g }));
    }

    fn evaluate_iob(&mut self, index: usize, iob: &Iob, state: IobState) {
        let out_data = self.level(iob.o) != iob.out_invert;
        let out_clock = self.level(iob.ok) != iob.ok_invert;
        let out_flip_flop = if self.reset {
            false
        } else if out_clock && !state.out_clock {
            out_data
        } else {
            state.out_flip_flop
        };
        let in_clock = self.level(iob.ik) != iob.ik_invert;
        let in_storage = if self.reset {
            false
        } else if iob.in_latch && !in_clock || !iob.in_latch && in_clock && !state.in_clock {
            // a latch transparent while its clock is Low (Figure 4)
            state.pad
        } else {
            state.in_storage
        };
        self.blocks[index] = BlockState::Iob(IobState {
            out_flip_flop,
            in_storage,
            out_clock,
            in_clock,
            ..state
        });
        let out = if iob.out_registered {
            out_flip_flop
        } else {
            out_data
        };
        let driving = !iob.three_state || self.level(iob.t) == iob.three_state_invert;
        self.schedule(
            iob.driver,
            if driving {
                Level::from(out)
            } else {
                Level::Off
            },
        );
        self.schedule(iob.i, Level::from(state.pad));
        self.schedule(iob.q, Level::from(iob.in_registered && in_storage));
    }
}

impl Clb {
    /// F and G from the inputs and the flip-flops `qx` and `qy`.
    fn functions(&self, input: &impl Fn(ClbInput) -> bool, qx: bool, qy: bool) -> (bool, bool) {
        let select = |selection: Selection| match selection {
            Selection::B => input(ClbInput::B),
            Selection::C => input(ClbInput::C),
            Selection::D => input(ClbInput::D),
            Selection::E => input(ClbInput::E),
            Selection::Qx => qx,
            Selection::Qy => qy,
            Selection::Low => false,
        };
        let table = |table: u16, inputs: [Selection; 3]| {
            let index = inputs.iter().enumerate().fold(
                usize::from(input(ClbInput::A)),
                |index, (bit, selection)| index | usize::from(select(*selection)) << (bit + 1),
            );
            table >> index & 1 != 0
        };
        let f = table(self.f_table, self.f_inputs);
        let g = table(self.g_table, self.g_inputs);
        if self.merged {
            let merged = if input(ClbInput::E) { g } else { f };
            (merged, merged)
        } else {
            (f, g)
        }
    }
}

#[cfg(test)]
mod board_clocks;
pub(super) mod fast_forward;
#[cfg(test)]
mod tests;
