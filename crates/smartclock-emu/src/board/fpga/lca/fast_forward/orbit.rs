//! The members in groups, each group's states period by period as an
//! orbit, and the jump: a group whose inputs hold runs along its orbit,
//! its state at any period an index into it, and a group whose inputs
//! change starts a new orbit from where it is.
//!
//! A group is a member whose inputs come from no other member, or the
//! members read by nothing but it, held whole while its orbit stays
//! short: a counter's digits, each clocked by the one before, are one
//! group, the period of the whole the product of theirs.  The orbit of
//! a group is its states from one, under inputs that hold, until a state
//! repeats: a tail and a cycle.

use std::hash::Hash;
use std::hash::Hasher;

use super::learn::Context;
use super::learn::FastHasher;
use super::learn::FastMap;
use super::learn::Interner;
use super::learn::Learner;
use super::plan::Channel;
use super::plan::Member;

/// The longest orbit kept, in periods: a group whose orbit runs longer
/// is not grown, and a jump stops where a longer one was cut.
pub(super) const ORBIT_LIMIT: usize = 1 << 16;

/// How many times less often a member's other parents must change what
/// it reads than the one it grows into: grown, it wakes at the others'
/// changes, each a bigger group's, rather than at that one's.
const WAKES_SAVED: usize = 16;

/// The most combinations of waveforms tried to find the inputs a group
/// holding still ignores.
const MAXIMUM_COMBINATIONS: usize = 64;

/// Where a member's input comes from within its group's period.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Source {
    /// An earlier member of the group, and its output.
    Member(usize, usize),
    /// The group's external input, by its index.
    External(usize),
}

/// A group: members run together, period by period.
#[derive(Debug, Clone)]
pub(super) struct Group {
    /// Its members, by their index among the plan's, in order.
    pub(super) members: Vec<usize>,
    /// Each member's inputs' sources.
    sources: Vec<Vec<Source>>,
    /// Its external inputs: a group before it and that group's output.
    pub(super) external: Vec<(usize, usize)>,
    /// What other groups read of it and what is watched: a member, by
    /// its position in the group, and its output.
    pub(super) outputs: Vec<(usize, usize)>,
    /// Which of its outputs are watched.
    pub(super) watched: Vec<bool>,
    /// Who it is, for the orbits' cache: the number of its members'
    /// keys in order among the groups met (`Orbits::groups`), exact.
    key: u32,
}

/// A group's states and outputs from one, period by period.
#[derive(Debug)]
pub(super) struct Orbit {
    /// Its members' states at each period's start, one more than the
    /// periods: the state after the last.
    states: Vec<u32>,
    /// Its outputs' waveforms through each period.
    outputs: Vec<u32>,
    members: usize,
    output_count: usize,
    /// The periods it records.
    periods: usize,
    /// Where its cycle starts, if it closed: the state after the last
    /// period is the state at this one.
    cycle: Option<usize>,
    /// The orbit and the period of it it runs on into after its last,
    /// if it reached a state an orbit of the group's under the same
    /// inputs already had: its states are those that orbit had not.
    exit: Option<(u32, usize)>,
    /// The periods before the first that ends with something still on
    /// its way.
    quiet: usize,
    /// For each output, the periods it changes in from the one before,
    /// in order, and those of the cycle, from the one before on a lap,
    /// the cycle's first from its last.
    changes: Vec<Vec<usize>>,
    cyclic_changes: Vec<Vec<usize>>,
    /// For each output, the periods its waveform is not level through,
    /// and those of the cycle.
    active: Vec<Vec<usize>>,
    cyclic_active: Vec<Vec<usize>>,
    /// For each output, the waveforms it takes, each once, and that
    /// set's number (`Orbits::sets`), which a group reading it keys what
    /// it ignores by.
    distinct: Vec<Vec<u32>>,
    distinct_sets: Vec<u32>,
}

impl Orbit {
    /// The orbit's period for `periods` after its first, if it records
    /// it.
    fn index(&self, periods: u64) -> Option<usize> {
        if periods <= self.periods as u64 && self.cycle.is_none() || periods < self.periods as u64 {
            return Some(periods as usize);
        }
        let start = self.cycle?;
        let length = (self.periods - start) as u64;
        Some(start + ((periods - start as u64) % length) as usize)
    }

    /// The periods it holds from its first, a jump going no further but
    /// where it exits into another.
    fn horizon(&self) -> u64 {
        if (self.cycle.is_some() || self.exit.is_some()) && self.quiet == self.periods {
            u64::MAX
        } else {
            self.quiet.min(self.periods) as u64
        }
    }

    pub(super) fn state(&self, index: usize) -> &[u32] {
        &self.states[index * self.members..(index + 1) * self.members]
    }

    fn output(&self, index: usize) -> &[u32] {
        &self.outputs[index * self.output_count..(index + 1) * self.output_count]
    }

    /// The first period after `after`, counted from the orbit's first,
    /// that `linear` lists before the cycle's first lap ends and
    /// `cyclic` on the laps after, if any.
    fn next_in(&self, linear: &[usize], cyclic: &[usize], after: u64) -> Option<u64> {
        let from = after + 1;
        if from < self.periods as u64 {
            let position = linear.partition_point(|index| (*index as u64) < from);
            if let Some(index) = linear.get(position) {
                return Some(*index as u64);
            }
        }
        let start = self.cycle?;
        let cycle = cyclic;
        let first = *cycle.first()?;
        let from = from.max(self.periods as u64);
        let length = (self.periods - start) as u64;
        let position = start + ((from - start as u64) % length) as usize;
        let at = cycle.partition_point(|index| *index < position);
        Some(match cycle.get(at) {
            Some(index) => from + (*index - position) as u64,
            None => from + (self.periods - position) as u64 + (first - start) as u64,
        })
    }
}

/// Where each state of a group's orbits under some inputs lies, by the
/// state's hash: the orbit and its period, every state of that hash.
type Places = FastMap<u64, Vec<(u32, u32)>>;

/// The orbits computed, by group and external inputs, and where each
/// state of each lies in them.  Every table is keyed exactly, by
/// numbers of values interned, or checks the value it finds: a hash
/// alone keys none, since a collision would make a jump inexact.
#[derive(Debug, Default)]
pub(super) struct Orbits {
    orbits: Vec<Orbit>,
    /// For a group's key and its inputs' tuple, each state's orbit and
    /// period, by the state's hash, each checked against the state.
    places: FastMap<(u32, u32), Places>,
    /// The inputs a group holding still ignores, by `ignored_key`'s
    /// words: group, inputs, the sets of waveforms its sources have and
    /// the state.
    ignored: FastMap<Vec<u32>, u64>,
    /// The groups found not to grow, by group, inputs and member.
    refused: FastMap<(u32, u32, u32), ()>,
    /// The groups met, each its members' keys in order, numbered.
    groups: Interner<Vec<u32>>,
    /// The sets of waveforms an orbit's outputs take, numbered.
    sets: Interner<Vec<u32>>,
    /// A key of `ignored`'s being made, kept to save its allocation.
    scratch: Vec<u32>,
}

/// The combinations of choices of the counts `counts`, if there are
/// some and at most `MAXIMUM_COMBINATIONS`: the product, stopped as soon
/// as it passes, so that it never overflows, on a 32-bit `usize` either.
pub(super) fn combinations(counts: impl IntoIterator<Item = usize>) -> Option<usize> {
    let product = counts.into_iter().try_fold(1usize, |product, count| {
        product
            .checked_mul(count)
            .filter(|product| *product <= MAXIMUM_COMBINATIONS)
    })?;
    (product > 0).then_some(product)
}

fn hash_state(state: &[u32]) -> u64 {
    let mut hasher = FastHasher::default();
    state.hash(&mut hasher);
    hasher.finish()
}

/// A group under way: the orbit it follows from the period it started
/// it in.
#[derive(Debug, Clone, Copy)]
struct Running {
    since: u64,
    orbit: u32,
    offset: usize,
    inputs: u32,
    /// The external inputs, by bit, whose changes it need not be told
    /// of: it holds its state and its outputs whatever waveforms they
    /// take of those their sources' orbits have.
    ignored: u64,
}

impl Group {
    /// One period of the group from `state` with its external inputs'
    /// waveforms `external`: in `next` the state it leaves, in `produced`
    /// each member's outputs' waveforms.  Returns whether it ends quiet.
    fn step(
        &self,
        learner: &mut Learner,
        context: &Context<'_, '_>,
        state: &[u32],
        external: &[u32],
        produced: &mut [Vec<u32>],
        next: &mut Vec<u32>,
    ) -> bool {
        next.clear();
        let mut quiet = true;
        let mut waveforms = std::mem::take(&mut learner.scratch);
        for position in 0..self.members.len() {
            waveforms.clear();
            waveforms.extend(self.sources[position].iter().map(|source| match source {
                Source::Member(earlier, output) => produced[*earlier][*output],
                Source::External(index) => external[*index],
            }));
            let sigma = learner.tuples.number(&waveforms[..]);
            let step = learner.step(context, self.members[position], state[position], sigma);
            quiet &= step.quiet;
            produced[position].clone_from(learner.tuples.value(step.outputs));
            next.push(step.next);
        }
        learner.scratch = waveforms;
        quiet
    }

    /// A group merged into another, left with nothing.
    fn empty() -> Self {
        Self {
            members: Vec::new(),
            sources: Vec::new(),
            external: Vec::new(),
            outputs: Vec::new(),
            watched: Vec::new(),
            key: u32::MAX,
        }
    }

    /// The members `chosen` as a group, after `groups`, `group_of` the
    /// group each placed member is in, its key numbered among `known`.
    fn new(
        members: &[Member],
        chosen: Vec<usize>,
        group_of: &[usize],
        groups: &[Group],
        known: &mut Interner<Vec<u32>>,
    ) -> Self {
        let mut external: Vec<(usize, usize)> = Vec::new();
        let mut sources = Vec::new();
        for (position, member) in chosen.iter().enumerate() {
            let mut own = Vec::new();
            for (from, output) in &members[*member].inputs {
                let source = match chosen[..position].iter().position(|known| known == from) {
                    Some(earlier) => Source::Member(earlier, *output),
                    None => {
                        let group = group_of[*from];
                        let index = groups[group]
                            .outputs
                            .iter()
                            .position(|(at, out)| {
                                groups[group].members[*at] == *from && out == output
                            })
                            .expect("a group's outputs hold what others read");
                        let input = (group, index);
                        let at = external
                            .iter()
                            .position(|known| *known == input)
                            .unwrap_or_else(|| {
                                external.push(input);
                                external.len() - 1
                            });
                        Source::External(at)
                    }
                };
                own.push(source);
            }
            sources.push(own);
        }
        let identity: Vec<u32> = chosen.iter().map(|member| members[*member].key).collect();
        let mut group = Self {
            members: chosen,
            sources,
            external,
            outputs: Vec::new(),
            watched: Vec::new(),
            key: known.number(&identity[..]),
        };
        group.outputs = (0..group.members.len())
            .flat_map(|position| {
                (0..members[group.members[position]].outputs.len())
                    .map(move |output| (position, output))
            })
            .collect();
        group
    }
}

/// The groups of a plan, running: the jump's arithmetic.
#[derive(Debug, Clone)]
pub(super) struct Jump {
    pub(super) groups: Vec<Group>,
    running: Vec<Running>,
    /// The periods some group's inputs changed in, and the orbits
    /// started there.
    pub(super) events: u64,
    pub(super) anchors: u64,
    /// The groups made with copies of free-running sources.
    pub(super) copies: u64,
    /// The orbits each group started.
    pub(super) group_anchors: Vec<u64>,
    /// Every orbit each group has run along, in the order it started
    /// them, so that its state is known at any period the run has passed.
    history: Vec<Vec<Running>>,
}

impl Orbits {
    /// The orbit `orbit` runs on into past its exits, if any: one that
    /// closed, or one cut short.
    fn settled(&self, orbit: u32) -> &Orbit {
        let mut settled = &self.orbits[orbit as usize];
        while let Some((next, _)) = settled.exit {
            settled = &self.orbits[next as usize];
        }
        settled
    }

    /// The orbits from `known` on, of `group` under `inputs`, forgotten:
    /// a refused growth's trial, which no group runs on, often cut at
    /// `ORBIT_LIMIT` periods.
    fn forget(&mut self, known: usize, group: &Group, inputs: u32) {
        let Some(places) = self.places.get_mut(&(group.key, inputs)) else {
            return;
        };
        for orbit in &self.orbits[known..] {
            for index in 0..orbit.periods {
                let hash = hash_state(orbit.state(index));
                if let Some(found) = places.get_mut(&hash) {
                    found.retain(|(number, _)| (*number as usize) < known);
                    if found.is_empty() {
                        places.remove(&hash);
                    }
                }
            }
        }
        self.orbits.truncate(known);
    }

    /// The orbit of `group` from `state` under the external waveforms
    /// `inputs`, cut at `limit` periods, and where `state` lies in it:
    /// an orbit that may exit into one known.
    fn find(
        &mut self,
        learner: &mut Learner,
        context: &Context<'_, '_>,
        group: &Group,
        state: &[u32],
        inputs: u32,
        limit: usize,
    ) -> (u32, usize) {
        let places = self.places.entry((group.key, inputs)).or_default();
        if let Some(found) = places.get(&hash_state(state)) {
            for (orbit, index) in found {
                let known = &self.orbits[*orbit as usize];
                if known.state(*index as usize) == state
                    && (known.cycle.is_some() || known.exit.is_some() || known.periods >= limit)
                {
                    return (*orbit, *index as usize);
                }
            }
        }
        let external: Vec<u32> = learner.tuples.value(inputs).clone();
        let members = group.members.len();
        let output_count = group.outputs.len();
        let mut states: Vec<u32> = state.to_vec();
        let mut outputs: Vec<u32> = Vec::new();
        // the periods seen by their states' hash: the latest of each
        // hash, and for each period the one before it of its hash
        let mut latest: FastMap<u64, usize> = FastMap::default();
        let mut before: Vec<Option<usize>> = vec![latest.insert(hash_state(state), 0)];
        let mut quiet = usize::MAX;
        let mut cycle = None;
        let mut exit = None;
        let mut periods = 0;
        let mut produced: Vec<Vec<u32>> = vec![Vec::new(); members];
        let mut current: Vec<u32> = Vec::with_capacity(members);
        let mut next: Vec<u32> = Vec::with_capacity(members);
        while periods < limit {
            current.clear();
            current.extend_from_slice(&states[periods * members..(periods + 1) * members]);
            let quiet_step = group.step(
                learner,
                context,
                &current,
                &external,
                &mut produced,
                &mut next,
            );
            if !quiet_step && quiet == usize::MAX {
                quiet = periods;
            }
            for (position, output) in &group.outputs {
                outputs.push(produced[*position][*output]);
            }
            periods += 1;
            let hash = hash_state(&next);
            let mut candidate = latest.get(&hash).copied();
            let repeat = loop {
                match candidate {
                    Some(index) if states[index * members..(index + 1) * members] == next[..] => {
                        break Some(index);
                    }
                    Some(index) => candidate = before[index],
                    None => break None,
                }
            };
            states.extend_from_slice(&next);
            if let Some(start) = repeat {
                cycle = Some(start);
                break;
            }
            // a state another orbit has, which it runs on from
            exit = places.get(&hash).and_then(|found| {
                found.iter().find_map(|(orbit, index)| {
                    let known = &self.orbits[*orbit as usize];
                    (*known.state(*index as usize) == next[..]
                        && (known.cycle.is_some() || known.exit.is_some()))
                    .then_some((*orbit, *index as usize))
                })
            });
            if exit.is_some() {
                break;
            }
            before.push(latest.insert(hash, periods));
        }
        let quiet = quiet.min(periods);
        let output_at = |index: usize, output: usize| outputs[index * output_count + output];
        let mut changes = vec![Vec::new(); output_count];
        let mut cyclic_changes = vec![Vec::new(); output_count];
        let mut active = vec![Vec::new(); output_count];
        let mut cyclic_active = vec![Vec::new(); output_count];
        for output in 0..output_count {
            for index in 0..periods {
                if index > 0 && output_at(index - 1, output) != output_at(index, output) {
                    changes[output].push(index);
                }
                if learner.waveforms.value(output_at(index, output)).active() {
                    active[output].push(index);
                }
            }
            if let Some(start) = cycle {
                if output_at(periods - 1, output) != output_at(start, output) {
                    cyclic_changes[output].push(start);
                }
                cyclic_changes[output]
                    .extend(changes[output].iter().filter(|index| **index > start));
                cyclic_active[output]
                    .extend(active[output].iter().filter(|index| **index >= start));
            }
        }
        let distinct: Vec<Vec<u32>> = (0..output_count)
            .map(|output| {
                let mut seen: Vec<u32> =
                    (0..periods).map(|index| output_at(index, output)).collect();
                seen.sort_unstable();
                seen.dedup();
                seen
            })
            .collect();
        let distinct_sets = distinct
            .iter()
            .map(|waveforms| self.sets.number(&waveforms[..]))
            .collect();
        let orbit = Orbit {
            distinct,
            distinct_sets,
            states,
            outputs,
            members,
            output_count,
            periods,
            cycle,
            exit,
            quiet,
            changes,
            cyclic_changes,
            active,
            cyclic_active,
        };
        let number = self.orbits.len() as u32;
        // the state after the last period starts nothing this orbit has
        for index in 0..orbit.periods {
            places
                .entry(hash_state(orbit.state(index)))
                .or_default()
                .push((number, index as u32));
        }
        self.orbits.push(orbit);
        (number, 0)
    }
}

/// A jump's groups as they are made, member by member: each group, the
/// group each member is in, and the orbit each group starts on.
struct Grouping {
    groups: Vec<Group>,
    group_of: Vec<usize>,
    running: Vec<Running>,
    /// The groups made with copies of free-running sources.
    copies: u64,
}

/// What a jump's groups are made in: the learner, the orbits, the
/// landing's context and its members' states.
struct Making<'m, 'c, 'b, 'a> {
    learner: &'m mut Learner,
    orbits: &'m mut Orbits,
    context: &'c Context<'b, 'a>,
    states: &'c [u32],
}

impl Making<'_, '_, '_, '_> {
    /// `group` running from its members' states, on the orbit its
    /// external inputs' first waveforms start, since the jump's start.
    fn start(&mut self, grouping: &Grouping, group: &Group) -> Running {
        let inputs = Jump::initial_inputs(self.learner, self.orbits, &grouping.running, group);
        let state: Vec<u32> = group
            .members
            .iter()
            .map(|member| self.states[*member])
            .collect();
        let (orbit, offset) = self.orbits.find(
            self.learner,
            self.context,
            group,
            &state,
            inputs,
            ORBIT_LIMIT,
        );
        Running {
            since: 0,
            orbit,
            offset,
            inputs,
            ignored: 0,
        }
    }
}

impl Grouping {
    fn new(members: usize) -> Self {
        Self {
            groups: Vec::new(),
            group_of: vec![usize::MAX; members],
            running: Vec::new(),
            copies: 0,
        }
    }

    /// `members`, those not yet placed or in one of the groups `from`,
    /// now in group `to`.
    fn assign(&mut self, members: &[usize], from: &[usize], to: usize) {
        for member in members {
            let group = &mut self.group_of[*member];
            if *group == usize::MAX || from.contains(group) {
                *group = to;
            }
        }
    }

    /// Member `index` placed as `Jump::new` places it: merged with the
    /// free-running parents it alone reads, grown into a parent, or a
    /// group of its own.
    fn place(&mut self, making: &mut Making<'_, '_, '_, '_>, index: usize) {
        let members = making.context.members;
        let member = &members[index];
        let mut parents: Vec<usize> = member
            .inputs
            .iter()
            .map(|(from, _)| self.group_of[*from])
            .collect();
        parents.sort_unstable();
        parents.dedup();
        // free-running parents it reads together, which nothing else
        // reads, merged with it, if the whole keeps a short orbit
        if parents.len() > 1 && self.merge(making, index, &parents) {
            return;
        }
        // grown into a parent, if what it reads of that changes and the
        // two together keep a short orbit, what it reads of the others
        // inputs of the whole: the parent whose outputs it reads change
        // most often first, a fast divider rather than the slow one whose
        // tick it compares the divider's with
        for parent in self.varying(making, index, &parents) {
            if self.grow(making, index, parent, &parents) {
                return;
            }
        }
        if self.copy(making, index) {
            return;
        }
        self.add(making, vec![index]);
    }

    /// Member `index` in a group of its own with a copy of each
    /// free-running source it reads, a member that reads no other, if
    /// the whole keeps an orbit that closes within `ORBIT_LIMIT`.
    /// Returns whether it was.  A source runs the same from the same
    /// state in any group, so its copy is exact, and its readers need
    /// not wake at its every change: the Z3816A's GCLK, ACLK halved in a
    /// flip-flop, is a source the 1 PPS chain's group holds, and a count
    /// on GCLK that also reads a later group cannot grow into that one.
    fn copy(&mut self, making: &mut Making<'_, '_, '_, '_>, index: usize) -> bool {
        let members = making.context.members;
        let mut chosen: Vec<usize> = members[index]
            .inputs
            .iter()
            .map(|(from, _)| *from)
            .filter(|from| members[*from].inputs.is_empty())
            .collect();
        chosen.sort_unstable();
        chosen.dedup();
        if chosen.is_empty() {
            return false;
        }
        chosen.push(index);
        let copied = Group::new(
            members,
            chosen,
            &self.group_of,
            &self.groups,
            &mut making.orbits.groups,
        );
        let inputs = Jump::initial_inputs(making.learner, making.orbits, &self.running, &copied);
        let refusal = (copied.key, inputs, members[index].key);
        if making.orbits.refused.contains_key(&refusal) {
            return false;
        }
        let known = making.orbits.orbits.len();
        let running = making.start(self, &copied);
        let orbit = making.orbits.settled(running.orbit);
        if orbit.cycle.is_none() || Self::holds_still(orbit, &copied) {
            making.orbits.refused.insert(refusal, ());
            making.orbits.forget(known, &copied, inputs);
            return false;
        }
        let at = self.groups.len();
        self.assign(&copied.members, &[], at);
        self.groups.push(copied);
        self.running.push(running);
        self.copies += 1;
        true
    }

    /// Whether `group`'s last member holds its state and its outputs all
    /// along `orbit`: alone it would hold still, ignoring what it reads
    /// of the sources copied with it, and cost nothing, where with them
    /// its group runs their cycle and wakes at every change of its
    /// other inputs.
    fn holds_still(orbit: &Orbit, group: &Group) -> bool {
        let last = group.members.len() - 1;
        let outputs: Vec<usize> = (0..group.outputs.len())
            .filter(|output| group.outputs[*output].0 == last)
            .collect();
        (0..orbit.periods).all(|index| {
            orbit.state(index + 1)[last] == orbit.state(0)[last]
                && outputs
                    .iter()
                    .all(|output| orbit.output(index)[*output] == orbit.output(0)[*output])
        })
    }

    /// Member `index`'s `parents` worth growing into, those whose outputs
    /// it reads change most often a period first: of one, if they change
    /// on its orbit's cycle; of several, if the others change what it
    /// reads `WAKES_SAVED` times less often, all on cycles.
    fn varying(
        &self,
        making: &Making<'_, '_, '_, '_>,
        index: usize,
        parents: &[usize],
    ) -> Vec<usize> {
        let member = &making.context.members[index];
        // each parent's changes a cycle, and the cycle's periods
        let rates: Vec<Option<(usize, usize)>> = parents
            .iter()
            .map(|parent| {
                let group = &self.groups[*parent];
                let orbit = making.orbits.settled(self.running[*parent].orbit);
                let length = orbit.periods - orbit.cycle?;
                let changes = member
                    .inputs
                    .iter()
                    .filter_map(|(from, output)| {
                        group
                            .outputs
                            .iter()
                            .position(|(at, out)| group.members[*at] == *from && out == output)
                    })
                    .map(|output| orbit.cyclic_changes[output].len())
                    .sum();
                Some((changes, length))
            })
            .collect();
        let rate = |(changes, length): (usize, usize)| changes as f64 / length as f64;
        let mut chosen: Vec<(usize, f64)> = parents
            .iter()
            .zip(&rates)
            .filter_map(|(parent, own)| {
                let own = rate((*own)?);
                let others = parents
                    .iter()
                    .zip(&rates)
                    .filter(|(other, _)| *other != parent)
                    .map(|(_, other)| other.map(rate))
                    .sum::<Option<f64>>()?;
                (own > 0.0 && others * WAKES_SAVED as f64 <= own).then_some((*parent, own))
            })
            .collect();
        chosen.sort_by(|one, other| other.1.total_cmp(&one.1));
        chosen.into_iter().map(|(parent, _)| parent).collect()
    }

    /// Member `index` grown into its parent `parent`, its other
    /// `parents`' outputs the whole's inputs, if the whole's orbit from
    /// the members' states closes within `ORBIT_LIMIT`.  Returns whether
    /// it was.  A group comes after those it reads, so a parent before
    /// another is grown only if no other group reads it, the whole moved
    /// last and the parent left empty.
    fn grow(
        &mut self,
        making: &mut Making<'_, '_, '_, '_>,
        index: usize,
        parent: usize,
        parents: &[usize],
    ) -> bool {
        let moves = parents.iter().any(|other| *other > parent);
        if moves
            && self
                .groups
                .iter()
                .any(|group| group.external.iter().any(|(from, _)| *from == parent))
        {
            return false;
        }
        let members = making.context.members;
        let mut chosen = self.groups[parent].members.clone();
        chosen.push(index);
        let grown = Group::new(
            members,
            chosen,
            &self.group_of,
            &self.groups,
            &mut making.orbits.groups,
        );
        let inputs = Jump::initial_inputs(making.learner, making.orbits, &self.running, &grown);
        let refusal = (self.groups[parent].key, inputs, members[index].key);
        if self.running[parent].since != 0 || making.orbits.refused.contains_key(&refusal) {
            return false;
        }
        let state: Vec<u32> = grown
            .members
            .iter()
            .map(|member| making.states[*member])
            .collect();
        let known = making.orbits.orbits.len();
        let (number, offset) = making.orbits.find(
            making.learner,
            making.context,
            &grown,
            &state,
            inputs,
            ORBIT_LIMIT,
        );
        if making.orbits.settled(number).cycle.is_none() {
            making.orbits.refused.insert(refusal, ());
            making.orbits.forget(known, &grown, inputs);
            return false;
        }
        let running = Running {
            since: 0,
            orbit: number,
            offset,
            inputs,
            ignored: 0,
        };
        if moves {
            let last = self.groups.len();
            self.assign(&grown.members, &[parent], last);
            self.groups[parent] = Group::empty();
            self.groups.push(grown);
            self.running.push(running);
        } else {
            self.group_of[index] = parent;
            self.groups[parent] = grown;
            self.running[parent] = running;
        }
        true
    }

    /// The members `chosen` added as a group of their own.
    fn add(&mut self, making: &mut Making<'_, '_, '_, '_>, chosen: Vec<usize>) {
        let index = self.groups.len();
        let group = Group::new(
            making.context.members,
            chosen,
            &self.group_of,
            &self.groups,
            &mut making.orbits.groups,
        );
        self.assign(&group.members, &[], index);
        let running = making.start(self, &group);
        self.groups.push(group);
        self.running.push(running);
    }

    /// Member `index`'s free-running `parents`, none read by any other
    /// group but the first, merged with it into the first, the others
    /// left empty, if what it reads of them changes and the whole keeps
    /// an orbit that closes within `ORBIT_LIMIT`.  Returns whether they
    /// were.
    fn merge(
        &mut self,
        making: &mut Making<'_, '_, '_, '_>,
        index: usize,
        parents: &[usize],
    ) -> bool {
        let members = making.context.members;
        let groups = &self.groups;
        let free = parents
            .iter()
            .all(|parent| groups[*parent].external.is_empty());
        let read = |parent: usize| {
            groups
                .iter()
                .any(|group| group.external.iter().any(|(from, _)| *from == parent))
        };
        if !free || parents[1..].iter().any(|parent| read(*parent)) {
            return false;
        }
        let varies = members[index].inputs.iter().any(|(from, output)| {
            let group = self.group_of[*from];
            let at = groups[group]
                .outputs
                .iter()
                .position(|(position, out)| {
                    groups[group].members[*position] == *from && out == output
                })
                .expect("an output of the parent");
            !making
                .orbits
                .settled(self.running[group].orbit)
                .cyclic_changes[at]
                .is_empty()
        });
        let parent_keys: Vec<u32> = parents.iter().map(|parent| groups[*parent].key).collect();
        let refusal = (
            making.orbits.groups.number(&parent_keys[..]),
            0,
            members[index].key,
        );
        if !varies || making.orbits.refused.contains_key(&refusal) {
            return false;
        }
        let mut chosen: Vec<usize> = parents
            .iter()
            .flat_map(|parent| groups[*parent].members.iter().copied())
            .collect();
        chosen.push(index);
        let merged = Group::new(
            members,
            chosen,
            &self.group_of,
            groups,
            &mut making.orbits.groups,
        );
        let state: Vec<u32> = merged
            .members
            .iter()
            .map(|member| making.states[*member])
            .collect();
        let inputs = making.learner.tuples.number(&[][..]);
        let known = making.orbits.orbits.len();
        let (number, offset) = making.orbits.find(
            making.learner,
            making.context,
            &merged,
            &state,
            inputs,
            ORBIT_LIMIT,
        );
        if making.orbits.settled(number).cycle.is_none() {
            making.orbits.refused.insert(refusal, ());
            making.orbits.forget(known, &merged, inputs);
            return false;
        }
        let first = parents[0];
        self.assign(&merged.members, parents, first);
        for parent in &parents[1..] {
            self.groups[*parent] = Group::empty();
        }
        self.groups[first] = merged;
        self.running[first] = Running {
            since: 0,
            orbit: number,
            offset,
            inputs,
            ignored: 0,
        };
        true
    }
}

impl Jump {
    /// The plan's members in groups, each running from the state
    /// `states` gives it, `watched` among what they output.
    pub(super) fn new(
        learner: &mut Learner,
        orbits: &mut Orbits,
        context: &Context<'_, '_>,
        states: &[u32],
        watched: &[Channel],
    ) -> Self {
        let members = context.members;
        let mut making = Making {
            learner,
            orbits,
            context,
            states,
        };
        let mut grouping = Grouping::new(members.len());
        for index in 0..members.len() {
            grouping.place(&mut making, index);
        }
        Self::finish(members, grouping, watched)
    }

    /// The groups `grouping` names, as `new` grouped a plan's members
    /// at an earlier landing, each running from the state `states` gives
    /// it, but a member that was alone there, or alone with copies of
    /// the sources it reads, placed as `new` places it:
    /// what it reads may vary here and not there, a count running that
    /// was stopped.  How members are grouped makes a jump faster or
    /// slower, never other than exact.
    pub(super) fn regroup(
        learner: &mut Learner,
        orbits: &mut Orbits,
        context: &Context<'_, '_>,
        states: &[u32],
        watched: &[Channel],
        grouping: &[Vec<usize>],
    ) -> Self {
        let members = context.members;
        let mut making = Making {
            learner,
            orbits,
            context,
            states,
        };
        let mut made = Grouping::new(members.len());
        for chosen in grouping {
            if chosen.is_empty() {
                made.groups.push(Group::empty());
                let running = made.running.first().copied().unwrap_or(Running {
                    since: 0,
                    orbit: 0,
                    offset: 0,
                    inputs: 0,
                    ignored: 0,
                });
                made.running.push(running);
                continue;
            }
            // its own members, not the copies of sources it took
            let own: Vec<usize> = chosen
                .iter()
                .copied()
                .filter(|member| made.group_of[*member] == usize::MAX)
                .collect();
            match own[..] {
                // alone then, perhaps not now: a parent that held still
                // there may run here, and it grows as a fresh jump's would
                [member] => made.place(&mut making, member),
                _ => made.add(&mut making, chosen.clone()),
            }
        }
        Self::finish(members, made, watched)
    }

    /// The members each group holds, for `regroup`.
    pub(super) fn grouping(&self) -> Vec<Vec<usize>> {
        self.groups
            .iter()
            .map(|group| group.members.clone())
            .collect()
    }

    fn finish(members: &[Member], grouping: Grouping, watched: &[Channel]) -> Self {
        let Grouping {
            mut groups,
            running,
            copies,
            ..
        } = grouping;
        let count = groups.len();
        for group in &mut groups {
            group.watched = group
                .outputs
                .iter()
                .map(|(position, output)| {
                    watched.contains(&members[group.members[*position]].outputs[*output])
                })
                .collect();
        }
        Self {
            history: running.iter().map(|run| vec![*run]).collect(),
            groups,
            running,
            events: 0,
            anchors: 0,
            copies,
            group_anchors: vec![0; count],
        }
    }

    fn orbit<'o>(orbits: &'o Orbits, running: &Running) -> &'o Orbit {
        &orbits.orbits[running.orbit as usize]
    }

    /// The tuple of `group`'s external waveforms in the first period.
    fn initial_inputs(
        learner: &mut Learner,
        orbits: &Orbits,
        running: &[Running],
        group: &Group,
    ) -> u32 {
        let waveforms: Vec<u32> = group
            .external
            .iter()
            .map(|(from, output)| {
                let run = &running[*from];
                let orbit = Self::orbit(orbits, run);
                orbit.output(run.offset)[*output]
            })
            .collect();
        learner.tuples.number(&waveforms)
    }

    /// The groups run on, period by period, as far as they can go of
    /// `periods`: to the first period a watched output is not level
    /// through, or an orbit's end.  Returns the periods gone.
    pub(super) fn run(
        &mut self,
        learner: &mut Learner,
        orbits: &mut Orbits,
        context: &Context<'_, '_>,
        periods: u64,
    ) -> u64 {
        let count = self.groups.len();
        let mut children: Vec<Vec<usize>> = vec![Vec::new(); count];
        for (index, group) in self.groups.iter().enumerate() {
            for (from, _) in &group.external {
                if !children[*from].contains(&index) {
                    children[*from].push(index);
                }
            }
        }
        for index in 0..count {
            self.running[index].ignored = self.ignored(learner, orbits, context, index);
        }
        let mut limits: Vec<u64> = (0..count)
            .map(|index| self.limit(orbits, index, 0))
            .collect();
        let mut inputs: Vec<u64> = (0..count)
            .map(|index| self.next_input(orbits, index, 0))
            .collect();
        let mut switches: Vec<u64> = (0..count).map(|index| self.switch(orbits, index)).collect();
        let mut dirty = vec![false; count];
        let mut waveforms: Vec<u32> = Vec::new();
        loop {
            let stop = limits.iter().copied().fold(periods, u64::min);
            let input = inputs.iter().copied().fold(u64::MAX, u64::min);
            let next = switches.iter().copied().fold(input, u64::min);
            if next >= stop {
                return stop;
            }
            let now = next;
            if input == now {
                self.events += 1;
            }
            for (index, input) in inputs.iter().enumerate() {
                dirty[index] = *input == now;
            }
            // a group at its orbit's exit on along the orbit it exits
            // into, the groups reading it told
            for index in 0..count {
                if switches[index] != now {
                    continue;
                }
                let run = self.running[index];
                let (orbit, offset) = Self::orbit(orbits, &run)
                    .exit
                    .expect("a group switches orbits at an exit");
                self.running[index] = Running {
                    since: now,
                    orbit,
                    offset,
                    inputs: run.inputs,
                    ignored: 0,
                };
                self.running[index].ignored = self.ignored(learner, orbits, context, index);
                self.history[index].push(self.running[index]);
                limits[index] = self.limit(orbits, index, now);
                inputs[index] = self.next_input(orbits, index, now);
                switches[index] = self.switch(orbits, index);
                for child in &children[index] {
                    dirty[*child] = true;
                }
            }
            for index in 0..count {
                if !std::mem::take(&mut dirty[index]) {
                    continue;
                }
                waveforms.clear();
                waveforms.extend(self.groups[index].external.iter().map(|(from, output)| {
                    let run = &self.running[*from];
                    let orbit = Self::orbit(orbits, run);
                    let at = orbit
                        .index(now - run.since + run.offset as u64)
                        .expect("a period within the orbit");
                    orbit.output(at)[*output]
                }));
                if learner.tuples.value(self.running[index].inputs)[..] == waveforms[..] {
                    self.running[index].ignored = self.ignored(learner, orbits, context, index);
                    inputs[index] = self.next_input(orbits, index, now);
                    continue;
                }
                self.anchors += 1;
                self.group_anchors[index] += 1;
                let tuple = learner.tuples.number(&waveforms);
                // on the orbit it runs on now, the last of its history
                let state = Self::state_on(orbits, &self.running[index], now).to_vec();
                let group = &self.groups[index];
                let (orbit, offset) =
                    orbits.find(learner, context, group, &state, tuple, ORBIT_LIMIT);
                self.running[index] = Running {
                    since: now,
                    orbit,
                    offset,
                    inputs: tuple,
                    ignored: 0,
                };
                self.running[index].ignored = self.ignored(learner, orbits, context, index);
                self.history[index].push(self.running[index]);
                limits[index] = self.limit(orbits, index, now);
                inputs[index] = self.next_input(orbits, index, now);
                switches[index] = self.switch(orbits, index);
                for child in &children[index] {
                    dirty[*child] = true;
                }
            }
        }
    }

    /// The period group `index` leaves its orbit at for the one it exits
    /// into, if it does.
    fn switch(&self, orbits: &Orbits, index: usize) -> u64 {
        if self.groups[index].members.is_empty() {
            return u64::MAX;
        }
        let run = &self.running[index];
        let orbit = Self::orbit(orbits, run);
        match orbit.exit {
            Some(_) => run.since + (orbit.periods - run.offset) as u64,
            None => u64::MAX,
        }
    }

    /// The first period from `now` group `index`'s orbit does not reach,
    /// or a watched output of it is not level through.
    fn limit(&self, orbits: &Orbits, index: usize, now: u64) -> u64 {
        if self.groups[index].members.is_empty() {
            return u64::MAX;
        }
        let run = &self.running[index];
        let orbit = Self::orbit(orbits, run);
        let start = run.offset as u64;
        let mut limit = orbit
            .horizon()
            .saturating_sub(start)
            .saturating_add(run.since);
        let here = now - run.since + start;
        for (output, watched) in self.groups[index].watched.iter().enumerate() {
            if !watched {
                continue;
            }
            let active_now = orbit
                .index(here)
                .is_some_and(|at| orbit.active[output].binary_search(&at).is_ok());
            let first = if active_now {
                Some(here)
            } else {
                orbit.next_in(&orbit.active[output], &orbit.cyclic_active[output], here)
            };
            if let Some(first) = first {
                limit = limit.min(first - start + run.since);
            }
        }
        limit
    }

    /// The first period after `now` an external input of group `index`
    /// changes in, as its sources run now.
    fn next_input(&self, orbits: &Orbits, index: usize, now: u64) -> u64 {
        let mut next = u64::MAX;
        let ignored = self.running[index].ignored;
        for (input, (from, output)) in self.groups[index].external.iter().enumerate() {
            if ignored
                .checked_shr(input as u32)
                .is_some_and(|bits| bits & 1 != 0)
            {
                continue;
            }
            let source = &self.running[*from];
            let orbit = Self::orbit(orbits, source);
            let here = now - source.since + source.offset as u64;
            if let Some(change) = orbit.next_in(
                &orbit.changes[*output],
                &orbit.cyclic_changes[*output],
                here,
            ) {
                next = next.min(change - source.offset as u64 + source.since);
            }
        }
        next
    }

    /// The external inputs of group `index`, by bit, it ignores as it
    /// runs: where it holds still, those whose every waveform in its
    /// source's orbit, with the others ignored too in every combination,
    /// leaves its state and outputs as they are.
    fn ignored(
        &self,
        learner: &mut Learner,
        orbits: &mut Orbits,
        context: &Context<'_, '_>,
        index: usize,
    ) -> u64 {
        let mut key = std::mem::take(&mut orbits.scratch);
        let ignored = self.ignored_keyed(learner, orbits, context, index, &mut key);
        orbits.scratch = key;
        ignored
    }

    /// `ignored`, `key` the scratch its key is made in.
    fn ignored_keyed(
        &self,
        learner: &mut Learner,
        orbits: &mut Orbits,
        context: &Context<'_, '_>,
        index: usize,
        key: &mut Vec<u32>,
    ) -> u64 {
        let group = &self.groups[index];
        let run = &self.running[index];
        let orbit = Self::orbit(orbits, run);
        let still = orbit
            .cycle
            .is_some_and(|start| orbit.periods - start == 1 && run.offset >= start);
        if !still || group.external.len() > u64::BITS as usize {
            return 0;
        }
        let state = orbit.state(run.offset);
        // the group, what its sources may do, by the waveforms their
        // orbits have, and its state: the group fixes how many of each
        key.clear();
        key.extend([group.key, run.inputs]);
        key.extend(group.external.iter().map(|(from, output)| {
            Self::orbit(orbits, &self.running[*from]).distinct_sets[*output]
        }));
        key.extend_from_slice(state);
        if let Some(ignored) = orbits.ignored.get(&key[..]) {
            return *ignored;
        }
        let ignored = self.find_ignored(learner, orbits, context, index, state);
        orbits.ignored.insert(key.clone(), ignored);
        ignored
    }

    fn find_ignored(
        &self,
        learner: &mut Learner,
        orbits: &Orbits,
        context: &Context<'_, '_>,
        index: usize,
        state: &[u32],
    ) -> u64 {
        let group = &self.groups[index];
        let run = &self.running[index];
        let state = state.to_vec();
        let current: Vec<u32> = learner.tuples.value(run.inputs).clone();
        let mut produced: Vec<Vec<u32>> = vec![Vec::new(); group.members.len()];
        let mut next = Vec::with_capacity(group.members.len());
        let quiet = group.step(learner, context, &state, &current, &mut produced, &mut next);
        if !quiet {
            return 0;
        }
        let held = produced.clone();
        let distinct = |input: usize| {
            let (from, output) = group.external[input];
            &Self::orbit(orbits, &self.running[from]).distinct[output][..]
        };
        let mut ignored: Vec<usize> = Vec::new();
        for input in 0..group.external.len() {
            let mut trial = ignored.clone();
            trial.push(input);
            let choices: Vec<&[u32]> = trial.iter().map(|input| distinct(*input)).collect();
            let Some(combinations) = combinations(choices.iter().map(|choice| choice.len())) else {
                continue;
            };
            let holds = (0..combinations).all(|combination| {
                let mut external = current.clone();
                let mut rest = combination;
                for (position, choice) in trial.iter().zip(&choices) {
                    external[*position] = choice[rest % choice.len()];
                    rest /= choice.len();
                }
                let quiet = group.step(
                    learner,
                    context,
                    &state,
                    &external,
                    &mut produced,
                    &mut next,
                );
                quiet && next == state && produced == held
            });
            if holds {
                ignored = trial;
            }
        }
        ignored.iter().fold(0, |mask, input| mask | 1 << input)
    }

    /// Group `index`'s members' states at `period`: none for a group
    /// left empty by another's growth, whose orbit, its members' before
    /// they moved, is not switched at its exit (`switch`).
    pub(super) fn state_at<'o>(&self, orbits: &'o Orbits, index: usize, period: u64) -> &'o [u32] {
        if self.groups[index].members.is_empty() {
            return &[];
        }
        let history = &self.history[index];
        let run = &history[history.partition_point(|run| run.since <= period).max(1) - 1];
        Self::state_on(orbits, run, period)
    }

    /// The members' states at `period` of a group that runs `run` then.
    fn state_on<'o>(orbits: &'o Orbits, run: &Running, period: u64) -> &'o [u32] {
        let orbit = Self::orbit(orbits, run);
        let at = orbit
            .index(period - run.since + run.offset as u64)
            .expect("a period within the orbit");
        orbit.state(at)
    }
}
