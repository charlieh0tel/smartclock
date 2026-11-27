//! Fast-forward's reading of a netlist and of the model's state at a
//! landing (docs/firmware/fpga-codegen.md, "Counter fast-forward"): the
//! state split into nodes, each flip-flop, latch, pad and delayed output
//! one; the nodes that cannot change while the pins hold, found by
//! three-valued evaluation; the followers, which forget what they held
//! within a few clocks; and the rest, the stages, grouped into the
//! members whose transitions are learned.

use super::super::Block;
use super::super::BlockIndex;
use super::super::BlockState;
use super::super::Clb;
use super::super::ClbInput;
use super::super::Data;
use super::super::Iob;
use super::super::Lca;
use super::super::Level;
use super::super::Net;
use super::super::Netlist;
use super::super::Slot;
use super::super::Target;

/// A node, by its index among the netlist's.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub(super) struct NodeId(pub(super) u32);

impl NodeId {
    pub(super) fn index(self) -> usize {
        self.0 as usize
    }
}

/// What a node is: a piece of the model's state the analysis follows by
/// itself.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) enum Node {
    /// A logic block's flip-flops.
    Qx(BlockIndex),
    Qy(BlockIndex),
    /// An I/O block's output flip-flop, input storage and pad.
    OutFlipFlop(BlockIndex),
    InStorage(BlockIndex),
    Pad(BlockIndex),
    /// A delayed output's level, and the block it is an output of.
    Slot(Slot, BlockIndex),
}

impl Node {
    /// The block it is part of.
    pub(super) fn block(self) -> BlockIndex {
        match self {
            Self::Qx(block)
            | Self::Qy(block)
            | Self::OutFlipFlop(block)
            | Self::InStorage(block)
            | Self::Pad(block)
            | Self::Slot(_, block) => block,
        }
    }

    /// The bits its value takes in a member's state: a level, Low, High
    /// or Off, two; the rest one.
    pub(super) fn width(self) -> u32 {
        match self {
            Self::Slot(..) => 2,
            _ => 1,
        }
    }
}

/// The netlist's nodes and how they connect: what never changes.
#[derive(Debug)]
pub(super) struct Graph {
    pub(super) nodes: Vec<Node>,
    /// Each block's nodes.
    pub(super) block_nodes: Vec<Vec<NodeId>>,
    /// The node of each delayed output.
    slot_nodes: Vec<NodeId>,
    /// The delayed output each net follows, through the clock buffers;
    /// none for a net nothing drives, which stays Low.
    net_sources: Vec<Option<NodeId>>,
    /// The nodes each node is an input of.
    readers: Vec<Vec<NodeId>>,
    /// Each net's buffered copies, the net itself first.
    pub(super) copies: Vec<Vec<Net>>,
}

/// A node's value, two-valued for storage and pads, a level for a
/// delayed output.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) enum Value {
    Bit(bool),
    Level(Level),
}

impl Value {
    /// Its bits in a member's state.
    pub(super) fn bits(self) -> u128 {
        match self {
            Self::Bit(bit) => u128::from(bit),
            Self::Level(Level::Low) => 0,
            Self::Level(Level::High) => 1,
            Self::Level(Level::Off) => 2,
        }
    }

    pub(super) fn from_bits(node: Node, bits: u128) -> Self {
        match node {
            Node::Slot(..) => Self::Level(match bits {
                0 => Level::Low,
                1 => Level::High,
                _ => Level::Off,
            }),
            _ => Self::Bit(bits != 0),
        }
    }
}

impl Graph {
    pub(super) fn new(netlist: &Netlist) -> Self {
        let mut nodes = Vec::new();
        let mut block_nodes = vec![Vec::new(); netlist.blocks.len()];
        let mut slot_nodes = vec![NodeId(u32::MAX); netlist.slots.len()];
        let mut add = |node: Node, nodes: &mut Vec<Node>| {
            let id = NodeId(nodes.len() as u32);
            nodes.push(node);
            block_nodes[node.block().0 as usize].push(id);
            if let Node::Slot(slot, _) = node {
                slot_nodes[slot.0 as usize] = id;
            }
        };
        for (index, block) in netlist.blocks.iter().enumerate() {
            let block_index = BlockIndex(index as u32);
            match block {
                Block::Clb(clb) => {
                    add(Node::Qx(block_index), &mut nodes);
                    add(Node::Qy(block_index), &mut nodes);
                    for slot in [clb.x, clb.y].into_iter().flatten() {
                        add(Node::Slot(slot, block_index), &mut nodes);
                    }
                }
                Block::Iob(iob) => {
                    add(Node::OutFlipFlop(block_index), &mut nodes);
                    add(Node::InStorage(block_index), &mut nodes);
                    add(Node::Pad(block_index), &mut nodes);
                    for slot in [iob.driver, iob.i, iob.q].into_iter().flatten() {
                        add(Node::Slot(slot, block_index), &mut nodes);
                    }
                }
                Block::Buffer(_) => {}
            }
        }
        // a net's source, followed back through the clock buffers
        let mut net_sources = vec![None; netlist.nets.len()];
        for (slot, target) in netlist.slots.iter().enumerate() {
            if let Target::Net(net) = target {
                net_sources[net.index()] = Some(slot_nodes[slot]);
            }
        }
        let mut copies: Vec<Vec<Net>> = (0..netlist.nets.len())
            .map(|net| vec![Net(net as u32)])
            .collect();
        for copied in &mut copies {
            let mut index = 0;
            while index < copied.len() {
                let copy = copied[index];
                copied.extend_from_slice(&netlist.buffered[copy.index()]);
                index += 1;
            }
        }
        for (net, nets) in copies.iter().enumerate() {
            for copy in &nets[1..] {
                net_sources[copy.index()] = net_sources[net];
            }
        }
        let mut graph = Self {
            nodes,
            block_nodes,
            slot_nodes,
            net_sources,
            readers: Vec::new(),
            copies,
        };
        let mut readers = vec![Vec::new(); graph.nodes.len()];
        for id in 0..graph.nodes.len() {
            let node = NodeId(id as u32);
            for input in graph.inputs(netlist, node) {
                if !readers[input.index()].contains(&node) {
                    readers[input.index()].push(node);
                }
            }
        }
        graph.readers = readers;
        graph
    }

    /// The node a net follows, if any.
    pub(super) fn source(&self, net: Option<Net>) -> Option<NodeId> {
        net.and_then(|net| self.net_sources[net.index()])
    }

    pub(super) fn slot_node(&self, slot: Slot) -> NodeId {
        self.slot_nodes[slot.0 as usize]
    }

    /// The nodes `node` reads, by structure alone.
    fn inputs(&self, netlist: &Netlist, node: NodeId) -> Vec<NodeId> {
        let kind = self.nodes[node.index()];
        let block = kind.block();
        let own = &self.block_nodes[block.0 as usize];
        let mut inputs: Vec<NodeId> = Vec::new();
        match &netlist.blocks[block.0 as usize] {
            Block::Clb(clb) => {
                inputs.extend(clb.inputs.iter().filter_map(|net| self.source(*net)));
                inputs.extend(own.iter().filter(|other| {
                    matches!(self.nodes[other.index()], Node::Qx(_) | Node::Qy(_))
                }));
            }
            Block::Iob(iob) => {
                inputs.extend(
                    [iob.o, iob.t, iob.ok, iob.ik]
                        .iter()
                        .filter_map(|net| self.source(*net)),
                );
                inputs.extend(own.iter().filter(|other| match self.nodes[other.index()] {
                    Node::Slot(slot, _) => Some(slot) == iob.driver,
                    _ => true,
                }));
            }
            Block::Buffer(_) => {}
        }
        inputs.retain(|input| *input != node);
        inputs
    }

    /// `node`'s value in `lca`.
    pub(super) fn value(&self, lca: &Lca<'_>, node: NodeId) -> Value {
        match self.nodes[node.index()] {
            Node::Slot(slot, _) => Value::Level(lca.outputs[slot.0 as usize].level),
            kind => Value::Bit(match (kind, lca.blocks[kind.block().0 as usize]) {
                (Node::Qx(_), BlockState::Clb(state)) => state.qx,
                (Node::Qy(_), BlockState::Clb(state)) => state.qy,
                (Node::OutFlipFlop(_), BlockState::Iob(state)) => state.out_flip_flop,
                (Node::InStorage(_), BlockState::Iob(state)) => state.in_storage,
                (Node::Pad(_), BlockState::Iob(state)) => state.pad,
                (kind, state) => unreachable!("{kind:?} in {state:?}"),
            }),
        }
    }
}

/// What three-valued evaluation found of a function of some known and
/// some unknown inputs.
struct Outcome<T> {
    /// Its value, if the unknowns cannot change it.
    value: Option<T>,
    /// The unknown inputs it can depend on, by their index.
    sensitive: u32,
}

/// The most unknown inputs `enumerate` tries every assignment of; with
/// more, it takes the function to depend on them all.
const MAXIMUM_UNKNOWN: usize = 8;

/// `function` over every assignment of the unknown `inputs`, at most
/// `CLB_VARIABLES` of them.
fn enumerate<T: Copy + PartialEq>(
    inputs: &[Option<bool>],
    function: impl Fn(&[bool]) -> T,
) -> Outcome<T> {
    let mut unknown = [0usize; CLB_VARIABLES];
    let mut count = 0;
    let mut values = [false; CLB_VARIABLES];
    for (index, input) in inputs.iter().enumerate() {
        match input {
            None => {
                unknown[count] = index;
                count += 1;
            }
            Some(value) => values[index] = *value,
        }
    }
    let unknown = &unknown[..count];
    let every = unknown.iter().fold(0, |mask, index| mask | 1 << index);
    if count > MAXIMUM_UNKNOWN {
        return Outcome {
            value: None,
            sensitive: every,
        };
    }
    let values = &mut values[..inputs.len()];
    let first = function(values);
    let mut results = [first; 1 << MAXIMUM_UNKNOWN];
    for (assignment, result) in results.iter_mut().enumerate().take(1 << count).skip(1) {
        for (bit, index) in unknown.iter().enumerate() {
            values[*index] = assignment >> bit & 1 != 0;
        }
        *result = function(values);
    }
    let results = &results[..1 << count];
    let value = results
        .iter()
        .all(|result| *result == first)
        .then_some(first);
    let mut sensitive = 0;
    if value.is_none() {
        for (bit, index) in unknown.iter().enumerate() {
            if (0..results.len())
                .any(|assignment| results[assignment] != results[assignment ^ 1 << bit])
            {
                sensitive |= 1 << index;
            }
        }
    }
    Outcome { value, sensitive }
}

/// The logic block's inputs and flip-flops, as `enumerate` takes them.
const CLB_VARIABLES: usize = 11;
const VARIABLE_QX: usize = 9;
const VARIABLE_QY: usize = 10;

fn clb_input(values: &[bool], input: ClbInput) -> bool {
    values[input as usize]
}

/// What three-valued evaluation found of a node.
#[derive(Debug, Default)]
pub(super) struct Examined {
    /// It cannot change while its unknown inputs do whatever they may.
    pub(super) constant: bool,
    /// The unknown nodes it can depend on.
    pub(super) depends: Vec<NodeId>,
    /// It forgets its value within a clock: a delayed output or a pad,
    /// which follow their inputs, or storage that takes something other
    /// than itself on every period of the clock.
    pub(super) follower: bool,
    /// Storage that takes something other than itself on every edge of
    /// a clock other than the pin's: the node that clock follows, so
    /// that the plan may find that it edges often enough.
    pub(super) clocked_by: Option<NodeId>,
}

/// The model's state at a landing, read three-valued: each node known,
/// its value now, or not.
pub(super) struct Known<'b, 'a> {
    pub(super) graph: &'b Graph,
    pub(super) netlist: &'a Netlist,
    pub(super) lca: &'b Lca<'a>,
    /// Each node known to keep its value.
    pub(super) constant: &'b [bool],
    /// The nodes the clock's pin drives, which change every period.
    pub(super) clock: &'b [bool],
}

impl Known<'_, '_> {
    fn node(&self, node: NodeId) -> Option<bool> {
        self.constant[node.index()].then(|| match self.graph.value(self.lca, node) {
            Value::Bit(bit) => bit,
            Value::Level(level) => level == Level::High,
        })
    }

    fn net(&self, net: Option<Net>) -> Option<bool> {
        match self.graph.source(net) {
            None => Some(false),
            Some(source) => self.node(source),
        }
    }

    fn is_clock(&self, net: Option<Net>) -> bool {
        self.graph
            .source(net)
            .is_some_and(|source| self.clock[source.index()])
    }

    /// The unknown nodes behind the sensitive inputs among `nets`, each
    /// a net or a node of the block itself.
    fn sources(&self, inputs: &[Input], sensitive: u32, depends: &mut Vec<NodeId>) {
        for (index, input) in inputs.iter().enumerate() {
            if sensitive >> index & 1 == 0 {
                continue;
            }
            let source = match input {
                Input::Net(net) => self.graph.source(*net),
                Input::Node(node) => Some(*node),
            };
            if let Some(source) = source
                && !self.constant[source.index()]
                && !depends.contains(&source)
            {
                depends.push(source);
            }
        }
    }

    fn values(&self, inputs: &[Input]) -> Vec<Option<bool>> {
        inputs
            .iter()
            .map(|input| match input {
                Input::Net(net) => self.net(*net),
                Input::Node(node) => self.node(*node),
            })
            .collect()
    }

    /// `node` evaluated three-valued.
    pub(super) fn examine(&self, node: NodeId) -> Examined {
        let kind = self.graph.nodes[node.index()];
        let block = kind.block();
        let current = self.graph.value(self.lca, node);
        match &self.netlist.blocks[block.0 as usize] {
            Block::Clb(clb) => self.examine_clb(clb, block, kind, current),
            Block::Iob(iob) => self.examine_iob(iob, block, kind, current),
            Block::Buffer(_) => unreachable!("a buffer has no nodes"),
        }
    }

    fn own(&self, block: BlockIndex, wanted: fn(Node) -> bool) -> NodeId {
        *self.graph.block_nodes[block.0 as usize]
            .iter()
            .find(|node| wanted(self.graph.nodes[node.index()]))
            .expect("the block's node")
    }

    fn examine_clb(&self, clb: &Clb, block: BlockIndex, kind: Node, current: Value) -> Examined {
        let qx = self.own(block, |node| matches!(node, Node::Qx(_)));
        let qy = self.own(block, |node| matches!(node, Node::Qy(_)));
        let mut inputs: Vec<Input> = clb.inputs.iter().map(|net| Input::Net(*net)).collect();
        inputs.push(Input::Node(qx));
        inputs.push(Input::Node(qy));
        debug_assert_eq!(inputs.len(), CLB_VARIABLES);
        // the functions read neither the clock, its enable nor the reset
        let mut values = self.values(&inputs);
        for control in [ClbInput::Ec, ClbInput::K, ClbInput::Rd] {
            values[control as usize] = Some(false);
        }
        let functions = |values: &[bool]| {
            clb.functions(
                &|input| clb_input(values, input),
                values[VARIABLE_QX],
                values[VARIABLE_QY],
            )
        };
        let mut examined = Examined::default();
        match kind {
            Node::Slot(slot, _) => {
                let (registered, x) = if Some(slot) == clb.x {
                    (clb.x_registered, true)
                } else {
                    (clb.y_registered, false)
                };
                let outcome = enumerate(&values, |values| {
                    if registered {
                        values[if x { VARIABLE_QX } else { VARIABLE_QY }]
                    } else {
                        let (f, g) = functions(values);
                        if x { f } else { g }
                    }
                });
                examined.constant = outcome
                    .value
                    .is_some_and(|value| Value::Level(Level::from(value)) == current);
                self.sources(&inputs, outcome.sensitive, &mut examined.depends);
                examined.follower = true;
            }
            Node::Qx(_) | Node::Qy(_) => {
                let Value::Bit(current) = current else {
                    unreachable!("a flip-flop is a bit")
                };
                let source = if matches!(kind, Node::Qx(_)) {
                    clb.x_data
                } else {
                    clb.y_data
                };
                let net = |input: ClbInput| clb.inputs[input as usize];
                let held = self.lca.reset;
                let clock = self.net(net(ClbInput::K));
                let enable = if clb.enable_clock {
                    self.net(net(ClbInput::Ec))
                } else {
                    Some(true)
                };
                let reset = if clb.direct_reset {
                    self.net(net(ClbInput::Rd))
                } else {
                    Some(false)
                };
                let capture =
                    !held && clock.is_none() && enable != Some(false) && reset != Some(true);
                let resets = !held && reset != Some(false);
                let outcome = enumerate(&values, |values| {
                    let (f, g) = functions(values);
                    match source {
                        Data::F => f,
                        Data::G => g,
                        Data::Di => clb_input(values, ClbInput::Di),
                    }
                });
                examined.constant = held
                    || reset == Some(true)
                    || (!capture || outcome.value == Some(current)) && (!resets || !current);
                let mut control = Vec::new();
                if clock.is_none() && !self.is_clock(net(ClbInput::K)) {
                    control.push(Input::Net(net(ClbInput::K)));
                }
                if enable.is_none() {
                    control.push(Input::Net(net(ClbInput::Ec)));
                }
                if reset.is_none() {
                    control.push(Input::Net(net(ClbInput::Rd)));
                }
                self.sources(&control, u32::MAX, &mut examined.depends);
                if capture {
                    self.sources(&inputs, outcome.sensitive, &mut examined.depends);
                }
                let own = if matches!(kind, Node::Qx(_)) { qx } else { qy };
                let forgets = enable == Some(true) && !examined.depends.contains(&own);
                examined.follower = forgets && self.is_clock(net(ClbInput::K));
                if forgets && !examined.follower && clock.is_none() {
                    examined.clocked_by = self.graph.source(net(ClbInput::K));
                }
            }
            other => unreachable!("{other:?} in a logic block"),
        }
        examined
    }

    fn examine_iob(&self, iob: &Iob, block: BlockIndex, kind: Node, current: Value) -> Examined {
        let off = self.own(block, |node| matches!(node, Node::OutFlipFlop(_)));
        let storage = self.own(block, |node| matches!(node, Node::InStorage(_)));
        let pad = self.own(block, |node| matches!(node, Node::Pad(_)));
        let held = self.lca.reset;
        let mut examined = Examined::default();
        let bit = |value: Value| match value {
            Value::Bit(bit) => bit,
            Value::Level(_) => unreachable!("storage is a bit"),
        };
        match kind {
            Node::Slot(slot, _) if Some(slot) == iob.driver => {
                let inputs = [Input::Net(iob.t), Input::Net(iob.o), Input::Node(off)];
                let values = self.values(&inputs);
                let outcome = enumerate(&values, |values| {
                    let driving = !iob.three_state || values[0] == iob.three_state_invert;
                    let out = if iob.out_registered {
                        values[2]
                    } else {
                        values[1] != iob.out_invert
                    };
                    if driving {
                        Level::from(out)
                    } else {
                        Level::Off
                    }
                });
                examined.constant = outcome
                    .value
                    .is_some_and(|value| Value::Level(value) == current);
                self.sources(&inputs, outcome.sensitive, &mut examined.depends);
                examined.follower = true;
            }
            Node::Slot(slot, _) => {
                let (input, registered) = if Some(slot) == iob.i {
                    (pad, false)
                } else {
                    (storage, true)
                };
                let known = if registered && !iob.in_registered {
                    Some(false)
                } else {
                    self.node(input)
                };
                examined.constant =
                    known.is_some_and(|value| Value::Level(Level::from(value)) == current);
                if known.is_none() {
                    examined.depends.push(input);
                }
                examined.follower = true;
            }
            Node::Pad(_) => {
                let driver = iob.driver.map(|slot| self.graph.slot_node(slot));
                // what drives it from outside holds, the clock's pin aside,
                // whose nodes are never constant
                let driver_known = driver.is_none_or(|driver| self.constant[driver.index()]);
                examined.constant = driver_known;
                if let Some(driver) = driver
                    && !driver_known
                {
                    examined.depends.push(driver);
                }
                examined.follower = true;
            }
            Node::OutFlipFlop(_) => {
                let clock = self.net(iob.ok);
                let data = self.net(iob.o).map(|data| data != iob.out_invert);
                examined.constant = held || clock.is_some() || data == Some(bit(current));
                if clock.is_none() && !self.is_clock(iob.ok) {
                    self.sources(&[Input::Net(iob.ok)], u32::MAX, &mut examined.depends);
                }
                if clock.is_none() && data.is_none() {
                    self.sources(&[Input::Net(iob.o)], u32::MAX, &mut examined.depends);
                }
                examined.follower = self.is_clock(iob.ok);
                if !examined.follower && clock.is_none() {
                    examined.clocked_by = self.graph.source(iob.ok);
                }
            }
            Node::InStorage(_) => {
                let clock = self.net(iob.ik).map(|clock| clock != iob.ik_invert);
                let value = self.node(pad);
                let takes = if iob.in_latch {
                    clock != Some(true)
                } else {
                    clock.is_none()
                };
                examined.constant = held || !takes || value == Some(bit(current));
                if clock.is_none() && !self.is_clock(iob.ik) {
                    self.sources(&[Input::Net(iob.ik)], u32::MAX, &mut examined.depends);
                }
                if takes && value.is_none() {
                    examined.depends.push(pad);
                }
                examined.follower = iob.in_latch && clock == Some(false) || self.is_clock(iob.ik);
            }
            other => unreachable!("{other:?} in an I/O block"),
        }
        examined
    }
}

/// An input of a node: a net, or another node of its block.
#[derive(Debug, Clone, Copy)]
enum Input {
    Net(Option<Net>),
    Node(NodeId),
}

impl Graph {
    /// The nodes that keep their value whatever the others do, while the
    /// pins other than the clock's hold: the greatest set each of whose
    /// nodes, the set's values known and the rest's not, cannot change,
    /// the `frozen` nodes taken to keep theirs.
    pub(super) fn constants<'a>(
        &self,
        netlist: &'a Netlist,
        lca: &Lca<'a>,
        clock: &[bool],
        frozen: &[bool],
    ) -> Vec<bool> {
        let mut constant: Vec<bool> = clock.iter().map(|clock| !clock).collect();
        let mut work: Vec<NodeId> = (0..self.nodes.len() as u32).map(NodeId).collect();
        let mut queued = vec![true; self.nodes.len()];
        while let Some(node) = work.pop() {
            queued[node.index()] = false;
            if !constant[node.index()] || frozen[node.index()] {
                continue;
            }
            let known = Known {
                graph: self,
                netlist,
                lca,
                constant: &constant,
                clock,
            };
            if known.examine(node).constant {
                continue;
            }
            constant[node.index()] = false;
            for reader in &self.readers[node.index()] {
                if constant[reader.index()] && !queued[reader.index()] {
                    queued[reader.index()] = true;
                    work.push(*reader);
                }
            }
        }
        constant
    }

    /// The nodes of the I/O blocks the clocks' pins are bonded to.
    pub(super) fn clock_nodes(&self, netlist: &Netlist, lca: &Lca<'_>) -> Vec<bool> {
        let mut clock = vec![false; self.nodes.len()];
        for block in lca
            .clocks()
            .filter_map(|clock| netlist.pins.get(&clock.pin))
        {
            for node in &self.block_nodes[block.0 as usize] {
                clock[node.index()] = true;
            }
        }
        clock
    }
}

#[cfg(test)]
impl Graph {
    pub(super) fn describe(&self, netlist: &Netlist, node: NodeId) -> String {
        let kind = self.nodes[node.index()];
        let name = &netlist.names[kind.block().0 as usize];
        match kind {
            Node::Qx(_) => format!("{name}.QX"),
            Node::Qy(_) => format!("{name}.QY"),
            Node::OutFlipFlop(_) => format!("{name}.OFF"),
            Node::InStorage(_) => format!("{name}.IN"),
            Node::Pad(_) => format!("{name}.PAD"),
            Node::Slot(slot, _) => match netlist.slots[slot.0 as usize] {
                Target::Net(net) => netlist.nets[net.index()].clone(),
                Target::Pad(_) => format!("{name}.DRIVER"),
            },
        }
    }
}
