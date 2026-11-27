//! Counter fast-forward (docs/firmware/fpga-codegen.md, "Counter
//! fast-forward"): the model moved on to a time as `Lca::run_clocked`
//! moves it, its pins other than the clock's held, without clocking
//! every edge.
//!
//! At a landing, a period's start with nothing on its way, the state is
//! read three-valued (analysis.rs): what cannot change while the pins
//! hold is constant; what forgets its value within a few clocks follows;
//! the rest, and all it reads, are the stages, in members, each a
//! strongly connected set of blocks (plan.rs).  Each member's transition
//! through a period is learned from the evaluator itself, the member's
//! blocks alone evaluated with their inputs driven as seen (learn.rs);
//! members run in groups along orbits, a counter's digits one group, and
//! the groups that only change now and then are moved on where their
//! inputs change (orbit.rs).  The jump stops a few periods short of where
//! it may go -- the first period a watched pin or net is not level
//! through, an orbit's end, or the time asked -- writes the stages'
//! states there, and clocks those periods, so that the followers forget
//! the state they were left in.

mod analysis;
mod learn;
mod orbit;
mod plan;
#[cfg(test)]
mod tests;

use super::BlockIndex;
use super::BlockState;
use super::Clock;
use super::Drive;
use super::Lca;
use super::Netlist;
use super::Picoseconds;
use super::Target;
use std::collections::HashMap;
use std::sync::Arc;

use self::analysis::Graph;
use self::analysis::Node;
use self::analysis::NodeId;
use self::analysis::Value;
use self::learn::Context;
use self::learn::Interner;
use self::learn::Learner;
use self::learn::derive_clocks;
use self::orbit::Jump;
use self::orbit::Orbits;
use self::plan::Channel;
use self::plan::Identity;
use self::plan::Member;
use self::plan::Plan;
use self::plan::Refusal;

/// The periods clocked where a landing has no plan, before trying again.
const REFUSED_PERIODS: u64 = 64;

/// The most nodes a member may take before a node of it is frozen to
/// break it up, and the most frozen at a landing: a member's table has
/// a row for each state it visits, and a counter's chain closed into
/// one loop visits millions.
const LARGE_MEMBER: usize = 48;
const MAXIMUM_FROZEN: usize = 4;

/// The periods clocked to see which of a large member's nodes change.
const SAMPLE_PERIODS: u64 = 2048;

/// The fewest periods worth a landing: fewer are clocked.  A landing
/// costs its plan, its groups' orbits started and the periods it
/// settles, more than clocking a bus cycle's span of a few tens of
/// periods outright, and the orbits a short jump starts are kept.
const SHORTEST_JUMP: u64 = 32;
/// The fewest periods the exactness tests land for, so that the short
/// cases' spans, a few bus cycles apart, still land and jump.
#[cfg(test)]
const TESTED_SHORTEST_JUMP: u64 = 8;

/// The plans kept for landings to come back to.
const KEPT_PLANS: usize = 16;

/// The most clock edges a period may have to be planned: clocks whose
/// joint period is longer, with no short common multiple of theirs,
/// are clocked, since a plan samples, learns and settles in whole
/// periods.  The telecom pair's benches have six.
const LONGEST_PERIOD_EDGES: u64 = 64;

/// What fast-forward has done, a measure of it.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub(super) struct Counts {
    /// Landings planned, and those refused.
    pub(super) plans: u64,
    pub(super) refusals: u64,
    /// Jumps made, and the periods they covered.
    pub(super) jumps: u64,
    pub(super) jumped: u64,
    /// Periods some group's inputs changed in, and orbits started.
    pub(super) events: u64,
    pub(super) anchors: u64,
    /// Groups made with copies of free-running sources.
    pub(super) copies: u64,
    /// Periods the evaluator ran to learn a member's transition.
    pub(super) learned: u64,
}

/// Fast-forward for one netlist: what it has read of it, and what it
/// has learned, kept from landing to landing.
#[derive(Debug)]
pub(in crate::board::fpga) struct FastForward<'a> {
    netlist: &'a Netlist,
    graph: Graph,
    learner: Learner,
    orbits: Orbits,
    counts: Counts,
    /// Every member identity met, numbered: a member's key.
    identities: Interner<Identity>,
    /// The node frozen to break up each too large member, by its key.
    freezes: HashMap<u32, Option<NodeId>>,
    /// The plans made lately, the latest first, each with what it holds
    /// for.
    plans: Vec<(Kept, Arc<Plan>)>,
    /// The fewest periods worth a landing (`SHORTEST_JUMP`).
    shortest_jump: u64,
}

/// What a landing must share with the one a plan was made at for the
/// plan to hold there too: the constants' values, so that each proof
/// that one of them cannot change goes through as it did, and what
/// drives the pins, the reset, the clock and what is watched.  A node
/// that could be constant now and was not then is a stage or follower
/// still, which is no less exact.
#[derive(Debug)]
struct Kept {
    constants: Vec<(NodeId, Value)>,
    holding: Holding,
}

/// What a kept plan needs of a landing besides its constants' values.
#[derive(Debug, PartialEq)]
struct Holding {
    externals: Vec<Drive>,
    reset: bool,
    clocks: Vec<Clock>,
    watched: Vec<Channel>,
}

impl Kept {
    fn holds(&self, graph: &Graph, lca: &Lca<'_>, holding: &Holding) -> bool {
        self.holding == *holding
            && self
                .constants
                .iter()
                .all(|(node, value)| graph.value(lca, *node) == *value)
    }
}

impl<'a> FastForward<'a> {
    pub(in crate::board::fpga) fn new(netlist: &'a Netlist) -> Self {
        Self {
            netlist,
            graph: Graph::new(netlist),
            learner: Learner::default(),
            orbits: Orbits::default(),
            counts: Counts::default(),
            identities: Interner::default(),
            freezes: HashMap::new(),
            plans: Vec::new(),
            shortest_jump: SHORTEST_JUMP,
        }
    }

    /// One that lands for spans as short as `TESTED_SHORTEST_JUMP`, as
    /// the exactness tests run it.
    #[cfg(test)]
    pub(super) fn landing_short(netlist: &'a Netlist) -> Self {
        Self {
            shortest_jump: TESTED_SHORTEST_JUMP,
            ..Self::new(netlist)
        }
    }

    /// What it has done.
    #[cfg(test)]
    pub(super) fn counts(&self) -> Counts {
        Counts {
            learned: self.learner.learned,
            ..self.counts
        }
    }

    /// The channels `lca` watches: its watched nets' and pads' nodes.
    fn watched(&self, lca: &Lca<'a>) -> Vec<Channel> {
        let mut channels = Vec::new();
        for (net, watched) in lca.watched_nets.iter().enumerate() {
            if *watched && let Some(source) = self.graph.source(Some(super::Net(net as u32))) {
                channels.push(Channel::Net(source));
            }
        }
        for (block, watched) in lca.watched_pads.iter().enumerate() {
            if *watched {
                let pad = self.graph.block_nodes[block]
                    .iter()
                    .find(|node| matches!(self.graph.nodes[node.index()], Node::Pad(_)))
                    .expect("an I/O block's pad");
                channels.push(Channel::Pad(*pad));
            }
        }
        channels
    }

    /// `lca` moved on to `until` as `Lca::run_clocked` moves it, its pins
    /// other than its clock's holding meanwhile.
    pub(in crate::board::fpga) fn advance(&mut self, lca: &mut Lca<'a>, until: Picoseconds) {
        self.advance_watching(lca, until, false);
    }

    /// `lca` moved on toward `until` as `advance` moves it, but stopped
    /// once a watched net or pin has changed, if one does, at the end of
    /// the span it clocks the change in: the period it changes in, or
    /// past it by up to `REFUSED_PERIODS` where a landing has no plan,
    /// by a plan's settling periods after a jump, and to `until` itself
    /// where fewer periods than the shortest jump's are left.  A change
    /// is never inside a jump, so it is seen as it comes.
    pub(in crate::board::fpga) fn advance_to_change(
        &mut self,
        lca: &mut Lca<'a>,
        until: Picoseconds,
    ) {
        self.advance_watching(lca, until, true);
    }

    fn advance_watching(&mut self, lca: &mut Lca<'a>, until: Picoseconds, stop: bool) {
        let Some(period) = lca
            .period()
            .filter(|period| lca.edges_in(*period) <= LONGEST_PERIOD_EDGES)
        else {
            lca.run_clocked(until);
            return;
        };
        let period = period.0;
        loop {
            let start = lca
                .period_start()
                .expect("a model with a period has its start");
            let periods = until.0.saturating_sub(start.0) / period;
            let clock_to =
                |periods: u64| Picoseconds((start.0 + periods * period - 1).min(until.0));
            if periods <= self.shortest_jump {
                lca.run_clocked(until);
                return;
            }
            lca.run_to_period();
            if !lca.quiet() {
                lca.run_clocked(clock_to(1));
                if stop && lca.changed() {
                    return;
                }
                continue;
            }
            self.counts.plans += 1;
            let Ok(plan) = self.plan(lca) else {
                self.counts.refusals += 1;
                lca.run_clocked(clock_to(REFUSED_PERIODS));
                if stop && lca.changed() {
                    return;
                }
                continue;
            };
            let context = Context {
                graph: &self.graph,
                netlist: self.netlist,
                base: lca,
                members: &plan.members,
            };
            let states: Vec<u32> = plan
                .members
                .iter()
                .map(|member| self.learner.state(&self.graph, lca, member))
                .collect();
            let mut jump = match plan.grouping.get() {
                Some(grouping) => Jump::regroup(
                    &mut self.learner,
                    &mut self.orbits,
                    &context,
                    &states,
                    &plan.watched,
                    grouping,
                ),
                None => {
                    let jump = Jump::new(
                        &mut self.learner,
                        &mut self.orbits,
                        &context,
                        &states,
                        &plan.watched,
                    );
                    let _ = plan.grouping.set(jump.grouping());
                    jump
                }
            };
            // how far it may go: the stages' states a few periods short
            let gone = jump.run(&mut self.learner, &mut self.orbits, &context, periods);
            let settle = plan.settle_periods;
            if gone <= settle {
                // the change, clocked a period at a time to stop after it
                let periods = if stop { 1 } else { gone.max(1) };
                lca.run_clocked(clock_to(periods));
                if stop && lca.changed() {
                    return;
                }
                continue;
            }
            let target = gone - settle;
            self.counts.events += jump.events;
            self.counts.anchors += jump.anchors;
            self.counts.copies += jump.copies;
            let written: Vec<(usize, u32)> = jump
                .groups
                .iter()
                .enumerate()
                .flat_map(|(index, group)| {
                    let state = jump.state_at(&self.orbits, index, target);
                    group
                        .members
                        .iter()
                        .copied()
                        .zip(state.iter().copied())
                        .collect::<Vec<_>>()
                })
                .collect();
            for (member, state) in written {
                self.learner
                    .write(&self.graph, lca, &plan.members[member], state);
            }
            self.land(lca, Picoseconds(start.0 + target * period));
            self.counts.jumps += 1;
            self.counts.jumped += target;
            lca.run_clocked(clock_to(target + settle));
            if stop && lca.changed() {
                return;
            }
        }
    }

    /// The plan and the groups at `lca`, and the orbits each starts in
    /// `periods`, in words.
    #[cfg(test)]
    pub(super) fn describe(&mut self, lca: &mut Lca<'a>, periods: u64) -> String {
        lca.run_to_period();
        let plan = self.plan(lca).expect("a plan");
        let context = Context {
            graph: &self.graph,
            netlist: self.netlist,
            base: lca,
            members: &plan.members,
        };
        let states: Vec<u32> = plan
            .members
            .iter()
            .map(|member| self.learner.state(&self.graph, lca, member))
            .collect();
        let mut jump = Jump::new(
            &mut self.learner,
            &mut self.orbits,
            &context,
            &states,
            &plan.watched,
        );
        let gone = jump.run(&mut self.learner, &mut self.orbits, &context, periods);
        let mut lines = vec![
            plan.describe(&self.graph, self.netlist),
            format!("gone {gone}"),
        ];
        for (index, group) in jump.groups.iter().enumerate() {
            lines.push(format!(
                "group {index}: members {:?} external {:?} anchors {}",
                group.members, group.external, jump.group_anchors[index]
            ));
        }
        lines.join("\n")
    }

    /// The plan at `lca`, a landing: a member too large to learn broken
    /// up by freezing a node of it that does not change for a while,
    /// the node watched so that a jump stops where it does.
    fn plan(&mut self, lca: &Lca<'a>) -> Result<Arc<Plan>, Refusal> {
        let watched = self.watched(lca);
        let holding = self.holding(lca, &watched);
        if let Some(position) = self
            .plans
            .iter()
            .position(|(kept, _)| kept.holds(&self.graph, lca, &holding))
        {
            let kept = self.plans.remove(position);
            let plan = Arc::clone(&kept.1);
            self.plans.insert(0, kept);
            return Ok(plan);
        }
        let plan = Arc::new(self.fresh_plan(lca, &watched)?);
        let kept = Kept {
            constants: (0..self.graph.nodes.len())
                .filter(|node| plan.constant[*node])
                .map(|node| {
                    (
                        NodeId(node as u32),
                        self.graph.value(lca, NodeId(node as u32)),
                    )
                })
                .collect(),
            holding,
        };
        self.plans.insert(0, (kept, Arc::clone(&plan)));
        self.plans.truncate(KEPT_PLANS);
        Ok(plan)
    }

    /// What a kept plan needs of `lca` besides its constants' values.
    fn holding(&self, lca: &Lca<'a>, watched: &[Channel]) -> Holding {
        Holding {
            externals: lca
                .blocks
                .iter()
                .filter_map(|state| match state {
                    BlockState::Iob(state) => Some(state.external),
                    _ => None,
                })
                .collect(),
            reset: lca.reset,
            clocks: lca.clocks().collect(),
            watched: watched.to_vec(),
        }
    }

    fn fresh_plan(&mut self, lca: &Lca<'a>, watched: &[Channel]) -> Result<Plan, Refusal> {
        let mut frozen: Vec<NodeId> = Vec::new();
        loop {
            let mut plan = Plan::new(&self.graph, self.netlist, lca, watched, &frozen)?;
            for member in &mut plan.members {
                member.key = self.identities.number(&member.identity);
            }
            let Some(large) = plan
                .members
                .iter()
                .find(|member| member.nodes.len() > LARGE_MEMBER)
            else {
                return Ok(plan);
            };
            if frozen.len() >= MAXIMUM_FROZEN {
                return Ok(plan);
            }
            let key = large.key;
            let choice = match self.freezes.get(&key) {
                Some(choice) => *choice,
                None => {
                    let choice = self.choose(lca, &plan, large, &frozen);
                    self.freezes.insert(key, choice);
                    choice
                }
            };
            match choice {
                Some(node) if !frozen.contains(&node) => frozen.push(node),
                _ => return Ok(plan),
            }
        }
    }

    /// The node of `member` to freeze, other than those `frozen` already:
    /// of the delayed outputs onto nets
    /// that did not change in `SAMPLE_PERIODS` clocked on from `lca`, the
    /// one furthest downstream of the node that changed most, the end of
    /// a counter's chain.
    fn choose(
        &self,
        lca: &Lca<'a>,
        plan: &Plan,
        member: &Member,
        frozen: &[NodeId],
    ) -> Option<NodeId> {
        let period = lca.period()?;
        let mut sim = lca.clone();
        let mut last: Vec<Value> = member
            .nodes
            .iter()
            .map(|node| self.graph.value(&sim, *node))
            .collect();
        let mut changes = vec![0u64; member.nodes.len()];
        for _ in 0..SAMPLE_PERIODS {
            sim.run_clocked(Picoseconds(sim.now.0 + period.0));
            for (index, node) in member.nodes.iter().enumerate() {
                let value = self.graph.value(&sim, *node);
                if value != last[index] {
                    changes[index] += 1;
                    last[index] = value;
                }
            }
        }
        let fastest = (0..member.nodes.len()).max_by_key(|index| changes[*index])?;
        // distances from the fastest along what reads what
        let position: HashMap<usize, usize> = member
            .nodes
            .iter()
            .enumerate()
            .map(|(index, node)| (node.index(), index))
            .collect();
        let mut readers = vec![Vec::new(); member.nodes.len()];
        for (index, node) in member.nodes.iter().enumerate() {
            for source in &plan.depends[node.index()] {
                if let Some(from) = position.get(source) {
                    readers[*from].push(index);
                }
            }
        }
        let mut distance = vec![None; member.nodes.len()];
        distance[fastest] = Some(0u64);
        let mut work = std::collections::VecDeque::from([fastest]);
        while let Some(index) = work.pop_front() {
            for reader in &readers[index] {
                if distance[*reader].is_none() {
                    distance[*reader] = distance[index].map(|distance| distance + 1);
                    work.push_back(*reader);
                }
            }
        }
        (0..member.nodes.len())
            .filter(|index| changes[*index] == 0 && !frozen.contains(&member.nodes[*index]))
            .filter(
                |index| match self.graph.nodes[member.nodes[*index].index()] {
                    Node::Slot(slot, _) => {
                        matches!(self.netlist.slots[slot.0 as usize], Target::Net(_))
                    }
                    _ => false,
                },
            )
            .filter_map(|index| distance[index].map(|distance| (distance, index)))
            .max()
            .map(|(_, index)| member.nodes[index])
    }

    /// `lca` made to stand at the start of the period at `at`, its
    /// stages' states written: every block's clocks as last seen made
    /// what its clock inputs are, and every block evaluated, so that
    /// the followers start on what they follow.
    fn land(&self, lca: &mut Lca<'a>, at: Picoseconds) {
        lca.place(at);
        for (index, block) in self.netlist.blocks.iter().enumerate() {
            if !matches!(block, super::Block::Buffer(_)) {
                derive_clocks(self.netlist, lca, index);
                lca.mark(BlockIndex(index as u32));
            }
        }
        // a pad follows its own driver and what drives it from outside
        for index in 0..self.netlist.blocks.len() {
            if matches!(lca.blocks[index], BlockState::Iob(_)) {
                lca.repad(BlockIndex(index as u32));
            }
        }
        lca.settle();
    }
}
