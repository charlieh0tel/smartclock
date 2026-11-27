//! Each member's transitions, learned from the evaluator itself: for a
//! state of the member and what its inputs do through a period, the
//! evaluator runs that period with the member's blocks alone evaluated,
//! its inputs driven as they were seen, and records the state the period
//! leaves and what its outputs did, glitches included.

use std::borrow::Borrow;
use std::collections::HashMap;
use std::hash::BuildHasherDefault;
use std::hash::Hash;
use std::hash::Hasher;

use super::super::BlockIndex;
use super::super::BlockState;
use super::super::Clb;
use super::super::ClbInput;
use super::super::Drive;
use super::super::Lca;
use super::super::Level;
use super::super::Net;
use super::super::Netlist;
use super::super::Picoseconds;
use super::super::Probe;
use super::super::Target;
use super::analysis::Graph;
use super::analysis::Node;
use super::analysis::NodeId;
use super::analysis::Value;
use super::plan::Channel;
use super::plan::Member;
use super::plan::channel_target;

/// rustc's `FxHasher`'s multiplier: odd, its bits mixed.
const MIX: u64 = 0x517c_c1b7_2722_0a95;

/// A hasher for fast-forward's tables, whose keys are small numbers and
/// tuples of them: a rotate, an exclusive or and a multiply a word, far
/// cheaper than the standard library's, which resists an adversary
/// these tables never meet.
#[derive(Debug, Default, Clone, Copy)]
pub(super) struct FastHasher(u64);

impl FastHasher {
    fn add(&mut self, word: u64) {
        self.0 = (self.0.rotate_left(5) ^ word).wrapping_mul(MIX);
    }
}

impl Hasher for FastHasher {
    fn finish(&self) -> u64 {
        self.0
    }

    fn write(&mut self, bytes: &[u8]) {
        for chunk in bytes.chunks(8) {
            let mut word = [0; 8];
            word[..chunk.len()].copy_from_slice(chunk);
            self.add(u64::from_le_bytes(word));
        }
    }

    fn write_u32(&mut self, value: u32) {
        self.add(u64::from(value));
    }

    fn write_u64(&mut self, value: u64) {
        self.add(value);
    }

    fn write_usize(&mut self, value: usize) {
        self.add(value as u64);
    }
}

/// A map hashed by `FastHasher`.
pub(super) type FastMap<K, V> = HashMap<K, V, BuildHasherDefault<FastHasher>>;

/// Values numbered as they are first seen, so that a table is keyed by
/// numbers.
#[derive(Debug)]
pub(super) struct Interner<T> {
    numbers: FastMap<T, u32>,
    values: Vec<T>,
}

impl<T> Default for Interner<T> {
    fn default() -> Self {
        Self {
            numbers: FastMap::default(),
            values: Vec::new(),
        }
    }
}

impl<T: Eq + Hash> Interner<T> {
    /// `value`'s number, a tuple's looked up by its slice, not copied
    /// unless it is new.
    pub(super) fn number<Q>(&mut self, value: &Q) -> u32
    where
        T: Borrow<Q>,
        Q: Eq + Hash + ToOwned<Owned = T> + ?Sized,
    {
        if let Some(number) = self.numbers.get(value) {
            return *number;
        }
        let number = self.values.len() as u32;
        self.values.push(value.to_owned());
        self.numbers.insert(value.to_owned(), number);
        number
    }

    pub(super) fn value(&self, number: u32) -> &T {
        &self.values[number as usize]
    }
}

/// A channel through one period: its level at the period's start, and
/// each change by its picoseconds from the start.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub(super) struct Waveform {
    pub(super) initial: bool,
    pub(super) changes: Vec<(u64, bool)>,
}

impl Waveform {
    /// It changes within the period.
    pub(super) fn active(&self) -> bool {
        !self.changes.is_empty()
    }
}

/// A member's transition through one period.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) struct Step {
    /// The state the period leaves, by its number in the member's table.
    pub(super) next: u32,
    /// What each of its outputs did, by the number of their waveforms'
    /// tuple.
    pub(super) outputs: u32,
    /// Nothing was still on its way at the period's end, so that the
    /// next period starts as quiet as this one did.
    pub(super) quiet: bool,
}

/// A member's transitions as learned, and its states numbered.
#[derive(Debug, Default)]
struct Table {
    states: Interner<u128>,
    steps: FastMap<(u32, u32), Step>,
}

/// Every member's tables, by the member's key, and the waveforms and
/// tuples of them they are written in.
#[derive(Debug, Default)]
pub(super) struct Learner {
    tables: FastMap<u32, Table>,
    pub(super) waveforms: Interner<Waveform>,
    pub(super) tuples: Interner<Vec<u32>>,
    /// Periods the evaluator ran to learn.
    pub(super) learned: u64,
    /// A tuple of waveforms being made, kept to save its allocation.
    pub(super) scratch: Vec<u32>,
}

/// What a member's period is learned in: the landing's model, its
/// netlist and nodes, and the clock.
pub(super) struct Context<'b, 'a> {
    pub(super) graph: &'b Graph,
    pub(super) netlist: &'a Netlist,
    pub(super) base: &'b Lca<'a>,
    pub(super) members: &'b [Member],
}

impl Learner {
    /// `member`'s state in `lca`, by its number in the member's table.
    pub(super) fn state(&mut self, graph: &Graph, lca: &Lca<'_>, member: &Member) -> u32 {
        let mut bits = 0u128;
        let mut shift = 0;
        for node in &member.nodes {
            bits |= graph.value(lca, *node).bits() << shift;
            shift += graph.nodes[node.index()].width();
        }
        self.tables
            .entry(member.key)
            .or_default()
            .states
            .number(&bits)
    }

    /// `member`'s state `state` written into `lca`, its nets with it, no
    /// block told.
    pub(super) fn write(&self, graph: &Graph, lca: &mut Lca<'_>, member: &Member, state: u32) {
        let bits = *self.tables[&member.key].states.value(state);
        let mut shift = 0;
        for node in &member.nodes {
            let kind = graph.nodes[node.index()];
            let width = kind.width();
            let value = Value::from_bits(kind, bits >> shift & ((1 << width) - 1));
            shift += width;
            write_node(graph, lca, *node, value);
        }
    }

    /// `member`'s transition from `state` with its inputs' waveforms the
    /// tuple `inputs`, learned if it is not known.
    pub(super) fn step(
        &mut self,
        context: &Context<'_, '_>,
        member: usize,
        state: u32,
        inputs: u32,
    ) -> Step {
        let key = context.members[member].key;
        if let Some(step) = self
            .tables
            .get(&key)
            .and_then(|table| table.steps.get(&(state, inputs)))
        {
            return *step;
        }
        let step = self.learn(context, member, state, inputs);
        self.tables
            .get_mut(&key)
            .expect("the member's table")
            .steps
            .insert((state, inputs), step);
        step
    }

    fn learn(&mut self, context: &Context<'_, '_>, index: usize, state: u32, inputs: u32) -> Step {
        self.learned += 1;
        let Context {
            graph,
            netlist,
            base,
            members,
        } = *context;
        let member = &members[index];
        let period = base.period().expect("fast-forward runs a clocked model");
        let mut sim = base.clone();
        sim.queue.clear();
        sim.dirty.clear();
        sim.queued.iter_mut().for_each(|queued| *queued = false);
        sim.watched_nets
            .iter_mut()
            .for_each(|watched| *watched = false);
        sim.watched_pads
            .iter_mut()
            .for_each(|watched| *watched = false);
        sim.transitions.clear();
        self.write(graph, &mut sim, member, state);
        // the inputs at their levels as the period starts, and their
        // changes to come
        let waveforms: Vec<u32> = self.tuples.value(inputs).clone();
        let start = sim.now;
        let mut changes: Vec<(Picoseconds, Net, bool)> = Vec::new();
        for ((from, output), waveform) in member.inputs.iter().zip(&waveforms) {
            let channel = members[*from].outputs[*output];
            let Target::Net(net) = channel_target(graph, netlist, channel) else {
                unreachable!("an input is a net")
            };
            let waveform = self.waveforms.value(*waveform);
            write_net(graph, &mut sim, net, waveform.initial);
            changes.extend(
                waveform
                    .changes
                    .iter()
                    .map(|(offset, high)| (Picoseconds(start.0 + offset), net, *high)),
            );
        }
        changes.sort_by_key(|(at, _, _)| *at);
        let mut mask = vec![false; netlist.blocks.len()];
        for block in &member.blocks {
            mask[block.0 as usize] = true;
            derive_clocks(netlist, &mut sim, block.0 as usize);
        }
        let end = Picoseconds(start.0 + period.0);
        let edges: Vec<(Picoseconds, BlockIndex, bool)> = base
            .edges_between(start, end)
            .into_iter()
            .map(|(at, pin, high)| (at, netlist.pins[&pin], high))
            .collect();
        for (_, block, _) in &edges {
            mask[block.0 as usize] = true;
        }
        for channel in &member.outputs {
            match channel_target(graph, netlist, *channel) {
                Target::Net(net) => sim.watched_nets[net.index()] = true,
                Target::Pad(block) => sim.watched_pads[block.0 as usize] = true,
            }
        }
        let (mut change, mut edge) = (0, 0);
        loop {
            let queued = sim.queue.front().map(|event| event.at);
            let next = [
                queued,
                changes.get(change).map(|(at, _, _)| *at),
                edges.get(edge).map(|(at, _, _)| *at),
            ]
            .into_iter()
            .flatten()
            .min();
            let Some(now) = next.filter(|now| *now <= end) else {
                break;
            };
            sim.now = now;
            while let Some(event) = sim.queue.front().copied()
                && event.at == now
            {
                sim.queue.pop_front();
                sim.apply(event);
            }
            while let Some((at, net, high)) = changes.get(change).copied()
                && at == now
            {
                sim.set_net(net, high);
                change += 1;
            }
            while let Some((at, block, high)) = edges.get(edge).copied()
                && at == now
            {
                if let BlockState::Iob(state) = &mut sim.blocks[block.0 as usize] {
                    state.external = Drive::Strong(high);
                }
                sim.repad(block);
                edge += 1;
            }
            while let Some(block) = sim.dirty.pop() {
                sim.queued[block.0 as usize] = false;
                if mask[block.0 as usize] {
                    sim.evaluate(block);
                }
            }
        }
        let quiet = sim.queue.iter().all(|event| {
            let output = &sim.outputs[event.slot.0 as usize];
            output.generation != event.generation || output.pending.is_none()
        });
        let next = self.state(graph, &sim, member);
        // each output's level at the start, from the state, and its changes
        let mut outputs = Vec::with_capacity(member.outputs.len());
        for channel in &member.outputs {
            let initial = initial_level(graph, base, member, *channel, state, self);
            let probe = match channel_target(graph, netlist, *channel) {
                Target::Net(net) => Probe::Net(net),
                Target::Pad(block) => {
                    Probe::Pin(netlist.iob(block).pin.expect("a watched pad is bonded"))
                }
            };
            let changes: Vec<(u64, bool)> = sim
                .transitions
                .iter()
                .filter(|transition| transition.probe == probe)
                .map(|transition| (transition.at.0 - start.0, transition.high))
                .collect();
            outputs.push(self.waveforms.number(&Waveform { initial, changes }));
        }
        Step {
            next,
            outputs: self.tuples.number(&outputs),
            quiet,
        }
    }
}

/// `channel`'s level with `member` in `state`.
fn initial_level(
    graph: &Graph,
    base: &Lca<'_>,
    member: &Member,
    channel: Channel,
    state: u32,
    learner: &Learner,
) -> bool {
    let bits = *learner.tables[&member.key].states.value(state);
    let mut shift = 0;
    for node in &member.nodes {
        let kind = graph.nodes[node.index()];
        let width = kind.width();
        if *node == channel.node() {
            return match Value::from_bits(kind, bits >> shift & ((1 << width) - 1)) {
                Value::Bit(bit) => bit,
                Value::Level(level) => level == Level::High,
            };
        }
        shift += width;
    }
    match graph.value(base, channel.node()) {
        Value::Bit(bit) => bit,
        Value::Level(level) => level == Level::High,
    }
}

/// `net` and its buffered copies set to `high`, no block told.
pub(super) fn write_net(graph: &Graph, lca: &mut Lca<'_>, net: Net, high: bool) {
    for copy in &graph.copies[net.index()] {
        lca.nets[copy.index()] = high;
    }
}

/// `node` set to `value` in `lca`, with the net it drives, no block told.
pub(super) fn write_node(graph: &Graph, lca: &mut Lca<'_>, node: NodeId, value: Value) {
    let kind = graph.nodes[node.index()];
    match (kind, value) {
        (Node::Slot(slot, _), Value::Level(level)) => {
            let output = &mut lca.outputs[slot.0 as usize];
            output.level = level;
            output.pending = None;
            if let Target::Net(net) = lca.netlist.slots[slot.0 as usize] {
                write_net(graph, lca, net, level == Level::High);
            }
        }
        (kind, Value::Bit(bit)) => match (kind, &mut lca.blocks[kind.block().0 as usize]) {
            (Node::Qx(_), BlockState::Clb(state)) => state.qx = bit,
            (Node::Qy(_), BlockState::Clb(state)) => state.qy = bit,
            (Node::OutFlipFlop(_), BlockState::Iob(state)) => state.out_flip_flop = bit,
            (Node::InStorage(_), BlockState::Iob(state)) => state.in_storage = bit,
            (Node::Pad(_), BlockState::Iob(state)) => state.pad = bit,
            (kind, state) => unreachable!("{kind:?} in {state:?}"),
        },
        (kind, value) => unreachable!("{kind:?} as {value:?}"),
    }
}

/// A block's clocks as last seen made its clock inputs' levels now, as
/// they are wherever the model is quiet.
pub(super) fn derive_clocks(netlist: &Netlist, lca: &mut Lca<'_>, block: usize) {
    let level = |lca: &Lca<'_>, net: Option<Net>| net.is_some_and(|net| lca.nets[net.index()]);
    match (&netlist.blocks[block], lca.blocks[block]) {
        (super::super::Block::Clb(clb), BlockState::Clb(mut state)) => {
            state.clock = level(lca, clock_net(clb)) != clb.clock_invert;
            lca.blocks[block] = BlockState::Clb(state);
        }
        (super::super::Block::Iob(iob), BlockState::Iob(mut state)) => {
            state.out_clock = level(lca, iob.ok) != iob.ok_invert;
            state.in_clock = level(lca, iob.ik) != iob.ik_invert;
            lca.blocks[block] = BlockState::Iob(state);
        }
        _ => {}
    }
}

fn clock_net(clb: &Clb) -> Option<Net> {
    clb.inputs[ClbInput::K as usize]
}
