//! A landing's plan: which nodes hold, which follow, and the stages, in
//! members whose transitions are learned, in the order they depend on
//! each other.

use std::collections::HashMap;
use std::sync::OnceLock;

use super::super::BlockIndex;
use super::super::BlockState;
use super::super::Drive;
use super::super::Lca;
use super::super::Netlist;
use super::super::Picoseconds;
use super::super::Pin;
use super::super::Target;
use super::analysis::Graph;
use super::analysis::Known;
use super::analysis::Node;
use super::analysis::NodeId;

/// The most bits a member's state may take.
const MAXIMUM_WIDTH: u32 = u128::BITS;

/// The most storage bits behind a clock its followers may be clocked
/// by, other than the pin's: such a clock, free running on the pin's
/// alone, edges at least once in two to the power of them periods.
const CLOCK_BITS: u32 = 4;

/// What a member's period is seen by: a delayed output's net, or a pad.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub(super) enum Channel {
    Net(NodeId),
    Pad(NodeId),
}

impl Channel {
    pub(super) fn node(self) -> NodeId {
        match self {
            Self::Net(node) | Self::Pad(node) => node,
        }
    }
}

/// A member: a strongly connected set of the stages' blocks, whose
/// period's transition is learned as one.
#[derive(Debug)]
pub(super) struct Member {
    pub(super) blocks: Vec<BlockIndex>,
    /// Its stage nodes, the bits of its state in order.
    pub(super) nodes: Vec<NodeId>,
    /// The other members' delayed outputs its stage nodes read, each by
    /// the member and the output it is of that member's.
    pub(super) inputs: Vec<(usize, usize)>,
    /// What other members read of it, and what is watched.
    pub(super) outputs: Vec<Channel>,
    /// Who it is, for its transitions' cache.
    pub(super) identity: Identity,
    /// Its identity's number among those fast-forward has met
    /// (`FastForward::number`), which its tables are kept by: exact, not
    /// a hash.
    pub(super) key: u32,
}

/// What decides a member's transitions, all of it: its nodes, the
/// channels it outputs and those it reads, in order, the chip's reset,
/// the clocks' edges through a period from its start, what drives its
/// pads from outside, and the value of every node that holds among
/// those its blocks hold or read.  Two members with the same identity
/// step alike from the same state and inputs.
#[derive(Debug, Clone, Default, PartialEq, Eq, Hash)]
pub(super) struct Identity {
    nodes: Vec<NodeId>,
    outputs: Vec<Channel>,
    inputs: Vec<Channel>,
    reset: bool,
    edges: Vec<(u64, Pin, bool)>,
    drives: Vec<Drive>,
    constants: Vec<(NodeId, u128)>,
}

/// Why a landing has no plan.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) enum Refusal {
    /// A member's state is too wide for a table.
    TooWide,
    /// A watched channel is on a clock pin's I/O block, or on a net
    /// that follows one through the clock buffers: no stage holds it, so
    /// a jump would pass over its changes.
    WatchedClock(Channel),
}

/// A landing's plan.
#[derive(Debug)]
pub(super) struct Plan {
    /// Each node, whether it holds, and whether it is a stage's.
    pub(super) constant: Vec<bool>,
    #[cfg(test)]
    pub(super) stage: Vec<bool>,
    /// The members, each after those it reads.
    pub(super) members: Vec<Member>,
    /// The periods the followers take to forget a state made up for
    /// them.
    pub(super) settle_periods: u64,
    /// Each moving node's sources among the moving.
    pub(super) depends: Vec<Vec<usize>>,
    /// What it watches: what was asked, and the frozen nodes.
    pub(super) watched: Vec<Channel>,
    /// How its members were grouped at the first landing it served, the
    /// groups' members, for the landings after.
    pub(super) grouping: OnceLock<Vec<Vec<usize>>>,
}

/// Tarjan's strongly connected components of `count` vertices, each
/// before those it reaches.
fn components(count: usize, edges: &[Vec<usize>]) -> Vec<Vec<usize>> {
    struct Search<'e> {
        edges: &'e [Vec<usize>],
        index: Vec<Option<usize>>,
        low: Vec<usize>,
        on_stack: Vec<bool>,
        stack: Vec<usize>,
        next: usize,
        components: Vec<Vec<usize>>,
    }
    impl Search<'_> {
        fn visit(&mut self, vertex: usize) {
            self.index[vertex] = Some(self.next);
            self.low[vertex] = self.next;
            self.next += 1;
            self.stack.push(vertex);
            self.on_stack[vertex] = true;
            for &next in &self.edges[vertex] {
                match self.index[next] {
                    None => {
                        self.visit(next);
                        self.low[vertex] = self.low[vertex].min(self.low[next]);
                    }
                    Some(index) if self.on_stack[next] => {
                        self.low[vertex] = self.low[vertex].min(index);
                    }
                    Some(_) => {}
                }
            }
            if Some(self.low[vertex]) == self.index[vertex] {
                let mut component = Vec::new();
                while let Some(top) = self.stack.pop() {
                    self.on_stack[top] = false;
                    component.push(top);
                    if top == vertex {
                        break;
                    }
                }
                component.sort_unstable();
                self.components.push(component);
            }
        }
    }
    let mut search = Search {
        edges,
        index: vec![None; count],
        low: vec![0; count],
        on_stack: vec![false; count],
        stack: Vec::new(),
        next: 0,
        components: Vec::new(),
    };
    for vertex in 0..count {
        if search.index[vertex].is_none() {
            search.visit(vertex);
        }
    }
    search.components.reverse();
    search.components
}

impl Plan {
    /// The plan for `lca` as it stands, quiet at a period's start, its
    /// pins other than the clock's to hold, `watched` changes to be
    /// seen.
    pub(super) fn new<'a>(
        graph: &Graph,
        netlist: &'a Netlist,
        lca: &Lca<'a>,
        watched: &[Channel],
        frozen: &[NodeId],
    ) -> Result<Self, Refusal> {
        let count = graph.nodes.len();
        let clock = graph.clock_nodes(netlist, lca);
        if let Some(channel) = watched.iter().find(|channel| clock[channel.node().index()]) {
            return Err(Refusal::WatchedClock(*channel));
        }
        let mut is_frozen = vec![false; count];
        for node in frozen {
            is_frozen[node.index()] = true;
        }
        // a frozen node holds for the nodes that read it, and is watched,
        // a stage of its own, for the change that ends a jump
        let constant = graph.constants(netlist, lca, &clock, &is_frozen);
        let mut watched = watched.to_vec();
        watched.extend(frozen.iter().map(|node| Channel::Net(*node)));
        let watched = &watched[..];
        let known = Known {
            graph,
            netlist,
            lca,
            constant: &constant,
            clock: &clock,
        };
        let moving = |node: usize| !constant[node] && !clock[node] || is_frozen[node];
        let mut depends = vec![Vec::new(); count];
        let mut eligible = vec![false; count];
        let mut clocked_by = vec![None; count];
        for node in (0..count).filter(|node| moving(*node)) {
            let examined = known.examine(NodeId(node as u32));
            depends[node] = examined
                .depends
                .into_iter()
                .filter(|source| moving(source.index()))
                .map(NodeId::index)
                .collect();
            eligible[node] = examined.follower;
            clocked_by[node] = examined.clocked_by.map(NodeId::index);
        }
        // storage clocked by a small counter on the pin's clock alone
        // forgets within that counter's period
        let mut weight = vec![1u64; count];

        let mut toggles: HashMap<usize, bool> = HashMap::new();
        for node in 0..count {
            if let Some(source) = clocked_by[node]
                && let Some(bits) = Self::free_running(graph, count, &moving, &depends, source)
                && *toggles.entry(source).or_insert_with(|| {
                    Self::toggles(graph, netlist, lca, NodeId(source as u32), bits)
                })
            {
                eligible[node] = true;
                weight[node] = 1 << bits;
            }
        }
        for channel in watched {
            eligible[channel.node().index()] = false;
        }
        // the stages: whatever does not forget, and all it reads, a
        // follower in a loop not forgetting either
        let stage = loop {
            let mut stage = vec![false; count];
            let mut work: Vec<usize> = (0..count)
                .filter(|node| moving(*node) && !eligible[*node])
                .collect();
            for node in &work {
                stage[*node] = true;
            }
            while let Some(node) = work.pop() {
                for source in &depends[node] {
                    if !stage[*source] {
                        stage[*source] = true;
                        work.push(*source);
                    }
                }
            }
            let follower_edges: Vec<Vec<usize>> = (0..count)
                .map(|node| {
                    if moving(node) && !stage[node] {
                        depends[node]
                            .iter()
                            .copied()
                            .filter(|source| !stage[*source])
                            .collect()
                    } else {
                        Vec::new()
                    }
                })
                .collect();
            let looped: Vec<usize> = components(count, &follower_edges)
                .into_iter()
                .filter(|component| {
                    component.len() > 1 || follower_edges[component[0]].contains(&component[0])
                })
                .flatten()
                .collect();
            if looped.is_empty() {
                break stage;
            }
            for node in looped {
                eligible[node] = false;
            }
        };
        let settle_periods = Self::settle_periods(graph, count, &stage, &moving, &depends, &weight);
        let members = Self::members(graph, netlist, lca, &constant, &stage, &depends, watched)?;
        Ok(Self {
            constant,
            #[cfg(test)]
            stage,
            members,
            settle_periods,
            depends,
            watched: watched.to_vec(),
            grouping: OnceLock::new(),
        })
    }

    /// If `node` runs on the pin's clock alone, with at most `CLOCK_BITS`
    /// storage bits among all it reads however far back, itself too:
    /// those bits.
    fn free_running(
        graph: &Graph,
        count: usize,
        moving: &impl Fn(usize) -> bool,
        depends: &[Vec<usize>],
        node: usize,
    ) -> Option<u32> {
        (|| {
            if !moving(node) {
                return None;
            }
            let mut seen = vec![false; count];
            let mut work = vec![node];
            seen[node] = true;
            let mut bits = 0;
            while let Some(next) = work.pop() {
                if !matches!(graph.nodes[next], Node::Slot(..) | Node::Pad(_)) {
                    bits += 1;
                }
                if bits > CLOCK_BITS {
                    return None;
                }
                for source in &depends[next] {
                    if !seen[*source] {
                        seen[*source] = true;
                        work.push(*source);
                    }
                }
            }
            Some(bits)
        })()
    }

    /// The net delayed output `source` drives changes both ways in the
    /// second `2^bits` periods of `lca` clocked on: past any tail, a
    /// free-running counter of `bits` storage bits edges within its
    /// cycle.
    fn toggles<'a>(
        graph: &Graph,
        netlist: &'a Netlist,
        lca: &Lca<'a>,
        source: NodeId,
        bits: u32,
    ) -> bool {
        let Node::Slot(slot, _) = graph.nodes[source.index()] else {
            return false;
        };
        let Target::Net(net) = netlist.slots[slot.0 as usize] else {
            return false;
        };
        let Some(period) = lca.period() else {
            return false;
        };
        let window = 1u64 << bits;
        let mut sim = lca.clone();
        sim.run_clocked(Picoseconds(sim.now.0 + window * period.0));
        sim.watched_nets
            .iter_mut()
            .for_each(|watched| *watched = false);
        sim.watched_pads
            .iter_mut()
            .for_each(|watched| *watched = false);
        sim.transitions.clear();
        sim.watched_nets[net.index()] = true;
        sim.run_clocked(Picoseconds(sim.now.0 + window * period.0));
        let transitions = sim.take_transitions();
        transitions.iter().any(|transition| transition.high)
            && transitions.iter().any(|transition| !transition.high)
    }

    /// Twice the most storage nodes on any path through the followers,
    /// and a margin: each takes what it follows on the clock's next edge
    /// after that settles.
    fn settle_periods(
        graph: &Graph,
        count: usize,
        stage: &[bool],
        moving: &impl Fn(usize) -> bool,
        depends: &[Vec<usize>],
        weight: &[u64],
    ) -> u64 {
        let follower = |node: usize| moving(node) && !stage[node];
        let mut depth: Vec<Option<u64>> = vec![None; count];
        fn visit(
            node: usize,
            graph: &Graph,
            follower: &impl Fn(usize) -> bool,
            depends: &[Vec<usize>],
            weight: &[u64],
            depth: &mut Vec<Option<u64>>,
        ) -> u64 {
            if let Some(depth) = depth[node] {
                return depth;
            }
            let own = if matches!(graph.nodes[node], Node::Slot(..) | Node::Pad(_)) {
                0
            } else {
                weight[node]
            };
            let deepest = depends[node]
                .iter()
                .filter(|source| follower(**source))
                .map(|source| visit(*source, graph, follower, depends, weight, depth))
                .max()
                .unwrap_or(0);
            depth[node] = Some(own + deepest);
            own + deepest
        }
        let deepest = (0..count)
            .filter(|node| follower(*node))
            .map(|node| visit(node, graph, &follower, depends, weight, &mut depth))
            .max()
            .unwrap_or(0);
        2 * deepest + 2
    }

    fn members<'a>(
        graph: &Graph,
        netlist: &'a Netlist,
        lca: &Lca<'a>,
        constant: &[bool],
        stage: &[bool],
        depends: &[Vec<usize>],
        watched: &[Channel],
    ) -> Result<Vec<Member>, Refusal> {
        let count = graph.nodes.len();
        let block_of = |node: usize| graph.nodes[node].block().0 as usize;
        let blocks = netlist.blocks.len();
        let mut edges = vec![Vec::new(); blocks];
        let mut staged = vec![false; blocks];
        for node in (0..count).filter(|node| stage[*node]) {
            staged[block_of(node)] = true;
            for source in &depends[node] {
                let (from, to) = (block_of(*source), block_of(node));
                if from != to && !edges[from].contains(&to) {
                    edges[from].push(to);
                }
            }
        }
        let components: Vec<Vec<usize>> = components(blocks, &edges)
            .into_iter()
            .filter(|component| staged[component[0]])
            .collect();
        let mut member_of_block = vec![usize::MAX; blocks];
        for (member, component) in components.iter().enumerate() {
            for block in component {
                member_of_block[*block] = member;
            }
        }
        let mut members: Vec<Member> = components
            .iter()
            .map(|component| {
                let nodes: Vec<NodeId> = component
                    .iter()
                    .flat_map(|block| graph.block_nodes[*block].iter().copied())
                    .filter(|node| stage[node.index()])
                    .collect();
                Member {
                    blocks: component
                        .iter()
                        .map(|block| BlockIndex(*block as u32))
                        .collect(),
                    nodes,
                    inputs: Vec::new(),
                    outputs: Vec::new(),
                    identity: Identity::default(),
                    key: 0,
                }
            })
            .collect();
        for member in &members {
            let width: u32 = member
                .nodes
                .iter()
                .map(|node| graph.nodes[node.index()].width())
                .sum();
            if width > MAXIMUM_WIDTH {
                return Err(Refusal::TooWide);
            }
        }
        // each member's inputs, the outputs of those before it they are
        let mut outputs: Vec<Vec<Channel>> = vec![Vec::new(); members.len()];
        let mut inputs: Vec<Vec<(usize, usize)>> = vec![Vec::new(); members.len()];
        for (index, member) in members.iter().enumerate() {
            let mut sources: Vec<NodeId> = member
                .nodes
                .iter()
                .flat_map(|node| depends[node.index()].iter())
                .filter(|source| member_of_block[block_of(**source)] != index)
                .map(|source| NodeId(*source as u32))
                .collect();
            sources.sort_unstable();
            sources.dedup();
            for source in sources {
                let from = member_of_block[block_of(source.index())];
                debug_assert!(from < index, "the members are in order");
                let channel = Channel::Net(source);
                let output = match outputs[from].iter().position(|known| *known == channel) {
                    Some(output) => output,
                    None => {
                        outputs[from].push(channel);
                        outputs[from].len() - 1
                    }
                };
                inputs[index].push((from, output));
            }
        }
        for channel in watched {
            let block = block_of(channel.node().index());
            if member_of_block[block] != usize::MAX {
                let member = member_of_block[block];
                if !outputs[member].contains(channel) {
                    outputs[member].push(*channel);
                }
            }
        }
        let reads: Vec<Vec<Channel>> = inputs
            .iter()
            .map(|own| {
                own.iter()
                    .map(|(from, output)| outputs[*from][*output])
                    .collect()
            })
            .collect();
        for (index, member) in members.iter_mut().enumerate() {
            member.inputs = std::mem::take(&mut inputs[index]);
            member.outputs = std::mem::take(&mut outputs[index]);
            member.identity = Self::identity(graph, netlist, lca, constant, member, &reads[index]);
        }
        Ok(members)
    }

    /// `member`'s identity, `reads` the channels it reads in order.
    fn identity<'a>(
        graph: &Graph,
        netlist: &'a Netlist,
        lca: &Lca<'a>,
        constant: &[bool],
        member: &Member,
        reads: &[Channel],
    ) -> Identity {
        // a waveform's times are from its period's start, as the clocks'
        let edges = lca.period().map_or_else(Vec::new, |period| {
            lca.edges_between(lca.now, lca.now + period)
                .into_iter()
                .map(|(at, pin, high)| (at.0 - lca.now.0, pin, high))
                .collect()
        });
        let mut drives = Vec::new();
        let mut read: HashMap<NodeId, ()> = HashMap::new();
        for block in &member.blocks {
            for node in &graph.block_nodes[block.0 as usize] {
                read.insert(*node, ());
            }
            let nets = match &netlist.blocks[block.0 as usize] {
                super::super::Block::Clb(clb) => clb.inputs.to_vec(),
                super::super::Block::Iob(iob) => vec![iob.o, iob.t, iob.ok, iob.ik],
                super::super::Block::Buffer(_) => Vec::new(),
            };
            for net in nets {
                if let Some(source) = graph.source(net) {
                    read.insert(source, ());
                }
            }
            if let BlockState::Iob(state) = lca.blocks[block.0 as usize] {
                drives.push(state.external);
            }
        }
        let mut read: Vec<NodeId> = read
            .into_keys()
            .filter(|node| constant[node.index()])
            .collect();
        read.sort_unstable();
        Identity {
            nodes: member.nodes.clone(),
            outputs: member.outputs.clone(),
            inputs: reads.to_vec(),
            reset: lca.reset,
            edges,
            drives,
            constants: read
                .into_iter()
                .map(|node| (node, graph.value(lca, node).bits()))
                .collect(),
        }
    }
}

#[cfg(test)]
impl Plan {
    /// The plan in words, member by member, for a test to print.
    pub(super) fn describe(&self, graph: &Graph, netlist: &Netlist) -> String {
        let mut lines = Vec::new();
        for (index, member) in self.members.iter().enumerate() {
            let names: Vec<String> = member
                .nodes
                .iter()
                .map(|node| graph.describe(netlist, *node))
                .collect();
            let inputs: Vec<String> = member
                .inputs
                .iter()
                .map(|(from, output)| format!("{from}:{output}"))
                .collect();
            lines.push(format!(
                "{index}: {} <- [{}] -> {} outputs",
                names.join(" "),
                inputs.join(" "),
                member.outputs.len()
            ));
        }
        let followers = self
            .stage
            .iter()
            .zip(&self.constant)
            .filter(|(stage, constant)| !**stage && !**constant)
            .count();
        lines.push(format!(
            "{followers} followers, {} periods to settle",
            self.settle_periods
        ));
        lines.join("\n")
    }
}

/// What a channel's node is, to watch it in a run.
pub(super) fn channel_target(graph: &Graph, netlist: &Netlist, channel: Channel) -> Target {
    match graph.nodes[channel.node().index()] {
        Node::Slot(slot, _) => netlist.slots[slot.0 as usize],
        node => Target::Pad(node.block()),
    }
}
