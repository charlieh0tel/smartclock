//! Fast-forward against the model clocked edge by edge.

use super::super::tests::NETLIST_Z3805A;
use super::super::tests::Z3805A;
use super::super::tests::after_reset;
use super::analysis::Graph;
use super::orbit::combinations;
use super::plan::Plan;

/// The plan at the bench after RESET finds the divider's prescaler, with
/// the 6-bit counter that clears it, as one member, and AL as another.
#[test]
fn the_plan_finds_the_divider() {
    let netlist = &*NETLIST_Z3805A;
    let graph = Graph::new(netlist);
    let lca = after_reset(&Z3805A);
    let plan = Plan::new(&graph, netlist, &lca, &[], &[]).expect("a plan");
    let names = |member: &super::plan::Member| -> Vec<String> {
        member
            .nodes
            .iter()
            .map(|node| graph.describe(netlist, *node))
            .collect()
    };
    let members: Vec<Vec<String>> = plan.members.iter().map(names).collect();
    let together = |one: &str, other: &str| {
        members.iter().any(|names| {
            names.iter().any(|name| name == one) && names.iter().any(|name| name == other)
        })
    };
    assert!(
        together("GI.QX", "IG.QX"),
        "{}",
        plan.describe(&graph, netlist)
    );
    assert!(
        together("AL.QX", "AL.QY"),
        "{}",
        plan.describe(&graph, netlist)
    );
    assert!(
        !together("GI.QX", "AL.QX"),
        "{}",
        plan.describe(&graph, netlist)
    );
}

use std::time::Instant;

use super::plan::Refusal;

use super::super::super::fixture::cases;
use super::super::Lca;
use super::super::Netlist;
use super::super::Picoseconds;
use super::super::Probe;
use super::super::Snapshot;
use super::super::Transition;
use super::super::tests::SHORT_CASES;
use super::super::tests::case;
use super::super::tests::recorded;
use super::super::tests::run_with;
use super::super::tests::tick_edge;
use super::FastForward;

/// Every case of `bench`'s fixture, fast-forwarded, reads as the netlist
/// did: seconds of each, a matter of minutes unoptimized.
fn forwarded_reads_as_the_netlist_does(bench: &Bench) {
    let mut differences = Vec::new();
    for case in cases(bench.fixture) {
        let mut fast = FastForward::new(bench.netlist);
        let started = Instant::now();
        let (run, _) = run_with(
            bench,
            &case,
            &mut |lca: &mut Lca<'static>, until: Picoseconds| fast.advance(lca, until),
            &mut |_, _| {},
        );
        println!(
            "{} {}: {:?} in {:.3} s",
            bench.image,
            case.name,
            fast.counts(),
            started.elapsed().as_secs_f64()
        );
        if run != recorded(&case) {
            differences.push(format!(
                "{}: {run:?}, the netlist {:?}",
                case.name,
                recorded(&case)
            ));
        }
    }
    assert!(differences.is_empty(), "{}", differences.join("\n"));
}

#[test]
#[ignore = "seconds of every case: minutes unoptimized"]
fn fast_forward_reads_as_the_netlist_does_in_every_case() {
    forwarded_reads_as_the_netlist_does(&Z3805A);
}

#[test]
#[ignore = "seconds of every case: minutes unoptimized"]
fn fast_forward_reads_as_the_netlist_does_in_every_58503a_z3801a_case() {
    forwarded_reads_as_the_netlist_does(&FPGA_58503A_Z3801A);
}

#[test]
#[ignore = "seconds of every case: minutes unoptimized"]
fn fast_forward_reads_as_the_netlist_does_in_every_58503b_case() {
    forwarded_reads_as_the_netlist_does(&FPGA_58503B);
}

#[test]
#[ignore = "seconds of every case: minutes unoptimized"]
fn fast_forward_reads_as_the_netlist_does_in_every_z3815a_case() {
    forwarded_reads_as_the_netlist_does(&Z3815A);
}

#[test]
#[ignore = "seconds of every case: minutes unoptimized"]
fn fast_forward_reads_as_the_netlist_does_in_every_z3816a_case() {
    forwarded_reads_as_the_netlist_does(&Z3816A);
}

#[test]
#[ignore = "seconds of every case: minutes unoptimized"]
fn fast_forward_reads_as_the_netlist_does_in_every_z3817a_case() {
    forwarded_reads_as_the_netlist_does(&Z3817A);
}

/// The short cases, fast-forwarded and clocked, agree in every node at
/// every action.
#[test]
fn fast_forward_agrees_with_the_clock_at_every_action() {
    for name in SHORT_CASES {
        let case = case(&Z3805A, name);
        let mut clocked: Vec<Snapshot> = Vec::new();
        run_with(
            &Z3805A,
            &case,
            &mut |lca: &mut Lca<'static>, until: Picoseconds| lca.run_clocked(until),
            &mut |lca, _| clocked.push(lca.snapshot()),
        );
        let mut fast = FastForward::landing_short(&NETLIST_Z3805A);
        let mut forwarded: Vec<Snapshot> = Vec::new();
        run_with(
            &Z3805A,
            &case,
            &mut |lca: &mut Lca<'static>, until: Picoseconds| fast.advance(lca, until),
            &mut |lca, _| forwarded.push(lca.snapshot()),
        );
        assert_eq!(clocked.len(), forwarded.len());
        for (index, (clocked, forwarded)) in clocked.iter().zip(&forwarded).enumerate() {
            assert!(clocked == forwarded, "{name}: action {index} differs");
        }
        println!("{name}: {:?}", fast.counts());
    }
}

use super::super::super::fixture::Case;
use super::super::tests::BENCHES;
use super::super::tests::Bench;
use super::super::tests::FPGA_58503A_Z3801A;
use super::super::tests::FPGA_58503B;
use super::super::tests::Z3815A;
use super::super::tests::Z3815A_BOARD;
use super::super::tests::Z3816A;
use super::super::tests::Z3816A_BOARD;
use super::super::tests::Z3817A;
use super::super::tests::act;
use super::super::tests::actions;
use super::super::tests::watched_bench;

/// The telecom pair's benches on their boards' clocks, ACLK alone.
const BOARD_BENCHES: [&Bench; 2] = [&Z3815A_BOARD, &Z3816A_BOARD];

/// A small deterministic generator, for stop points.
struct XorShift(u64);

impl XorShift {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0
    }

    /// A number in `0..=bound`.
    fn upto(&mut self, bound: u64) -> u64 {
        self.next() % (bound + 1)
    }
}

/// Where two snapshots of `netlist` first differ, in its names.
fn difference(netlist: &Netlist, clocked: &Snapshot, forwarded: &Snapshot) -> String {
    if clocked.now != forwarded.now {
        return format!("now {:?} and {:?}", clocked.now, forwarded.now);
    }
    if let Some(net) =
        (0..clocked.nets.len()).find(|net| clocked.nets[*net] != forwarded.nets[*net])
    {
        return format!(
            "net {} {} and {}",
            netlist.nets[net], clocked.nets[net], forwarded.nets[net]
        );
    }
    if let Some(block) =
        (0..clocked.blocks.len()).find(|block| clocked.blocks[*block] != forwarded.blocks[*block])
    {
        return format!(
            "block {} {:?} and {:?}",
            netlist.names[block], clocked.blocks[block], forwarded.blocks[block]
        );
    }
    if let Some(slot) =
        (0..clocked.outputs.len()).find(|slot| clocked.outputs[*slot] != forwarded.outputs[*slot])
    {
        return format!(
            "slot {slot} {:?} {:?} and {:?}",
            netlist.slots[slot], clocked.outputs[slot], forwarded.outputs[slot]
        );
    }
    format!("events {:?} and {:?}", clocked.events, forwarded.events)
}

/// Where a lock step compares: from `from`, before which the case is
/// fast-forwarded alone, to `to`, or the case's end if that comes first.
#[derive(Debug, Clone, Copy)]
struct Window {
    from: Picoseconds,
    to: Picoseconds,
}

impl Window {
    /// The whole case.
    const WHOLE: Self = Self {
        from: Picoseconds(0),
        to: Picoseconds(u64::MAX),
    };
}

/// `case` run clocked and fast-forwarded side by side through `window`,
/// their snapshots compared at every action and at `stops` random times
/// before each.  Returns the comparisons made, and the watched changes
/// in the window.
fn lock_step(
    bench: &Bench,
    case: &Case,
    fast: &mut FastForward<'static>,
    window: Window,
    stops: usize,
    seed: u64,
) -> (usize, Vec<Transition>) {
    let mut random = XorShift(seed);
    let mut forwarded = watched_bench(bench);
    let (actions, end) = actions(bench, case);
    let end = end.min(window.to);
    let from = window.from.min(end);
    let mut actions = actions
        .into_iter()
        .take_while(|(at, _)| *at <= end)
        .peekable();
    while let Some((at, action)) = actions.next_if(|(at, _)| *at < from) {
        fast.advance(&mut forwarded, at);
        act(&mut forwarded, action);
    }
    fast.advance(&mut forwarded, from);
    forwarded.take_transitions();
    let mut clocked = forwarded.clone();
    let mut previous = from;
    let mut compared = 0;
    let check = |clocked: &Lca<'static>, forwarded: &Lca<'static>, at: Picoseconds| {
        let (one, other) = (clocked.snapshot(), forwarded.snapshot());
        assert!(
            one == other,
            "{}: at {at:?}, {}",
            case.name,
            difference(bench.netlist, &one, &other)
        );
    };
    let steps = actions
        .map(|(at, action)| (at, Some(action)))
        .chain([(end, None)]);
    for (at, action) in steps {
        let mut times: Vec<u64> = (0..stops)
            .map(|_| previous.0 + random.upto(at.0 - previous.0))
            .collect();
        times.sort_unstable();
        for time in times.into_iter().chain([at.0]) {
            clocked.run_clocked(Picoseconds(time));
            fast.advance(&mut forwarded, Picoseconds(time));
            check(&clocked, &forwarded, Picoseconds(time));
            compared += 1;
        }
        if let Some(action) = action {
            assert_eq!(act(&mut clocked, action), act(&mut forwarded, action));
        }
        previous = at;
    }
    let transitions = clocked.take_transitions();
    assert_eq!(transitions, forwarded.take_transitions(), "{}", case.name);
    (compared, transitions)
}

/// Every bench's short cases in lock step, at every action and random
/// times: the telecom pair's two clocks with the others' one.
#[test]
fn fast_forward_agrees_with_the_clock_at_random_times() {
    for bench in BENCHES {
        let mut fast = FastForward::landing_short(bench.netlist);
        for name in bench.short {
            let (compared, _) = lock_step(
                bench,
                &case(bench, name),
                &mut fast,
                Window::WHOLE,
                3,
                0x5eed,
            );
            println!(
                "{} {name}: {compared} comparisons, {:?}",
                bench.image,
                fast.counts()
            );
        }
    }
}

/// The spans `fast_forward_agrees_with_the_clock_across_the_terminal_count`
/// compares, picoseconds: from
/// RESET's release, where its transients settle, and before and after
/// the first internal 1 PPS the fixture records: short enough to clock
/// unoptimized on every `cargo test`, long enough that the first catches
/// an orbit's wrap a period off (orbit.rs, `next_in`).
const AFTER_RESET: u64 = 30_000_000_000;
const BEFORE_THE_TERMINAL_COUNT: u64 = 10_000_000_000;
const AFTER_THE_TERMINAL_COUNT: u64 = 10_000_000_000;

/// The case `across_the_terminal_count` runs: past the first second.
const PAST_THE_FIRST_SECOND: &str = "near_tap";

/// `bench`'s `PAST_THE_FIRST_SECOND` case in lock step through `window`.
/// Returns whether an internal 1 PPS rose in it.
fn pulses_in_lock_step(bench: &Bench, window: Window) -> bool {
    let case = case(bench, PAST_THE_FIRST_SECOND);
    let mut fast = FastForward::landing_short(bench.netlist);
    let started = Instant::now();
    let (compared, transitions) = lock_step(bench, &case, &mut fast, window, 4, 0x5eed);
    println!(
        "{} {window:?}: {compared} comparisons in {:.1} s, {:?}",
        bench.image,
        started.elapsed().as_secs_f64(),
        fast.counts()
    );
    transitions
        .iter()
        .any(|transition| transition.probe == Probe::Pin(bench.pps) && transition.high)
}

/// Every bench's, and the telecom pair's on their boards' clocks, in lock step
/// from RESET, and across the one-second divider's terminal count, its
/// first internal 1 PPS: fast-forwarded alone to shortly before,
/// through the long jumps a short case never makes, then clocked and
/// fast-forwarded side by side past it.  A thread a span: the long
/// spans every run checks, beside the ignored `in_lock_step`'s seconds.
#[test]
fn fast_forward_agrees_with_the_clock_across_the_terminal_count() {
    std::thread::scope(|scope| {
        for bench in BENCHES.iter().copied().chain(BOARD_BENCHES) {
            let after_reset = Window {
                from: Picoseconds(0),
                to: Picoseconds(AFTER_RESET),
            };
            scope.spawn(move || pulses_in_lock_step(bench, after_reset));
            let case = case(bench, PAST_THE_FIRST_SECOND);
            let first = tick_edge(*case.pulses.first().expect("a pulse"));
            let across = Window {
                from: Picoseconds(first.0 - BEFORE_THE_TERMINAL_COUNT),
                to: Picoseconds(first.0 + AFTER_THE_TERMINAL_COUNT),
            };
            scope.spawn(move || {
                assert!(
                    pulses_in_lock_step(bench, across),
                    "{}: no internal 1 PPS in {across:?}",
                    bench.image
                );
            });
        }
    });
}

/// Periods `a_watched_clock_is_refused_a_plan` runs for.
const WATCHED_CLOCK_PERIODS: u64 = 2_000;

/// A channel watched on the clock pin, or on the net its I/O block
/// drives, is refused a plan, since no stage holds it; fast-forward then
/// clocks, and sees every one of its changes as the clocked model does.
#[test]
fn a_watched_clock_is_refused_a_plan() {
    let netlist = &*NETLIST_Z3805A;
    let graph = Graph::new(netlist);
    let pin = Z3805A.clocks[0].pin;
    let block = netlist.pins[&pin];
    let net = graph.block_nodes[block.0 as usize]
        .iter()
        .find_map(|node| match graph.nodes[node.index()] {
            super::analysis::Node::Slot(slot, _) => match netlist.slots[slot.0 as usize] {
                super::super::Target::Net(net) => Some(net),
                super::super::Target::Pad(_) => None,
            },
            _ => None,
        })
        .expect("the net the clock's pin drives");
    for probe in [Probe::Pin(pin), Probe::Net(net)] {
        let mut lca = after_reset(&Z3805A);
        lca.watch(probe).expect("watched");
        let mut fast = FastForward::landing_short(netlist);
        let watched = fast.watched(&lca);
        assert!(
            matches!(
                Plan::new(&graph, netlist, &lca, &watched, &[]),
                Err(Refusal::WatchedClock(_))
            ),
            "{probe:?}"
        );
        let until = Picoseconds(lca.now().0 + WATCHED_CLOCK_PERIODS * PERIOD);
        let mut clocked = lca.clone();
        clocked.run_clocked(until);
        fast.advance(&mut lca, until);
        let changes = clocked.take_transitions();
        assert!(changes.len() as u64 >= WATCHED_CLOCK_PERIODS, "{probe:?}");
        assert_eq!(changes, lca.take_transitions(), "{probe:?}");
        assert!(fast.counts().refusals > 0, "{probe:?}");
    }
}

/// The combinations a still group's ignored inputs are tried in: none
/// where a choice is empty or the product passes the most, which it
/// never computes past, so counts whose product overflows are refused.
#[test]
fn combinations_stop_at_the_most() {
    assert_eq!(combinations([4, 4, 4]), Some(64));
    assert_eq!(combinations([4, 4, 5]), None);
    assert_eq!(combinations([3, 0]), None);
    assert_eq!(combinations([]), Some(1));
    assert_eq!(combinations([usize::MAX, usize::MAX, 0]), None);
    assert_eq!(combinations([1 << 16, 1 << 16, 1 << 16, 1 << 16]), None);
}

/// The Z3816A's short cases on its board's clocks in lock step, at
/// every action and random times: there GCLK, ACLK halved in a
/// flip-flop, is a source the 1 PPS chain's group holds, and the count
/// on it, which also reads a later group, runs with a copy of it.
#[test]
fn fast_forward_agrees_with_the_clock_on_the_z3816a_board() {
    let mut copies = 0;
    for name in SHORT_CASES {
        let mut fast = FastForward::landing_short(Z3816A_BOARD.netlist);
        let seed = 0x5eed ^ name.len() as u64;
        let (compared, _) = lock_step(
            &Z3816A_BOARD,
            &case(&Z3816A_BOARD, name),
            &mut fast,
            Window::WHOLE,
            4,
            seed,
        );
        println!("{name}: {compared} comparisons, {:?}", fast.counts());
        copies += fast.counts().copies;
    }
    assert!(copies > 0, "no copies made");
}

/// Every case of `bench`'s in lock step, a thread a case: every edge of
/// seconds of the clock on the clocked side, so only when asked,
/// optimized.
fn in_lock_step(bench: &Bench) {
    std::thread::scope(|scope| {
        for case in cases(bench.fixture) {
            scope.spawn(move || {
                let mut fast = FastForward::landing_short(bench.netlist);
                let started = Instant::now();
                let seed = 0x5eed ^ case.name.len() as u64;
                let (compared, _) = lock_step(bench, &case, &mut fast, Window::WHOLE, 4, seed);
                println!(
                    "{} {}: {compared} comparisons in {:.1} s, {:?}",
                    bench.image,
                    case.name,
                    started.elapsed().as_secs_f64(),
                    fast.counts()
                );
            });
        }
    });
}

#[test]
#[ignore = "every clock edge of seconds of the 10 MHz: minutes, optimized"]
fn fast_forward_agrees_with_the_clock_in_every_case() {
    in_lock_step(&Z3805A);
}

#[test]
#[ignore = "every clock edge of seconds of the 10 MHz: minutes, optimized"]
fn fast_forward_agrees_with_the_clock_in_every_58503a_z3801a_case() {
    in_lock_step(&FPGA_58503A_Z3801A);
}

#[test]
#[ignore = "every clock edge of seconds of the 10 MHz: minutes, optimized"]
fn fast_forward_agrees_with_the_clock_in_every_58503b_case() {
    in_lock_step(&FPGA_58503B);
}

#[test]
#[ignore = "every clock edge of seconds of the 20 MHz: minutes, optimized"]
fn fast_forward_agrees_with_the_clock_in_every_z3815a_case() {
    in_lock_step(&Z3815A);
}

#[test]
#[ignore = "every clock edge of seconds of the 20 MHz: minutes, optimized"]
fn fast_forward_agrees_with_the_clock_in_every_z3816a_case() {
    in_lock_step(&Z3816A);
}

#[test]
#[ignore = "every clock edge of seconds of ACLK: minutes, optimized"]
fn fast_forward_agrees_with_the_clock_in_every_z3815a_board_case() {
    in_lock_step(&Z3815A_BOARD);
}

#[test]
#[ignore = "every clock edge of seconds of ACLK: minutes, optimized"]
fn fast_forward_agrees_with_the_clock_in_every_z3816a_board_case() {
    in_lock_step(&Z3816A_BOARD);
}

#[test]
#[ignore = "every clock edge of seconds of the 10 MHz: minutes, optimized"]
fn fast_forward_agrees_with_the_clock_in_every_z3817a_case() {
    in_lock_step(&Z3817A);
}

/// This thread's time on a CPU, seconds, where Linux says
/// (`/proc/thread-self/schedstat`), else none: the machine a measure
/// runs on may be busy with other work.
fn cpu_seconds() -> Option<f64> {
    let text = std::fs::read_to_string("/proc/thread-self/schedstat").ok()?;
    let nanoseconds: u64 = text.split_whitespace().next()?.parse().ok()?;
    Some(nanoseconds as f64 / 1e9)
}

/// The bench `FF_IMAGE` names, differential.py's name for its image or
/// `z3815a-board` or `z3816a-board`, or the Z3805A's.
fn measured_bench() -> &'static Bench {
    let image = std::env::var("FF_IMAGE").unwrap_or_else(|_| Z3805A.image.to_owned());
    BENCHES
        .iter()
        .copied()
        .chain(BOARD_BENCHES)
        .find(|bench| bench.image == image)
        .unwrap_or_else(|| panic!("no bench {image}"))
}

/// Every case of `FF_IMAGE`'s bench (the Z3805A's by default)
/// fast-forwarded, optimized: the CPU time each takes against the time
/// it simulates, a fresh fast-forward each, what it learns counted in,
/// and the same clocked edge by edge for the first `FF_CLOCKED` seconds
/// of each (0.05 by default) for the ratio.
#[test]
#[ignore = "a measure, optimized"]
fn fast_forward_speed() {
    let bench = measured_bench();
    let clocked_seconds: f64 =
        std::env::var("FF_CLOCKED").map_or(0.05, |seconds| seconds.parse().expect("seconds"));
    let now = || cpu_seconds().unwrap_or_else(|| Instant::now().elapsed().as_secs_f64());
    let (mut simulated, mut taken) = (0.0, 0.0);
    let forward = |fast: &mut FastForward<'static>, case: &Case| {
        replay(
            bench,
            case,
            &mut |lca: &mut Lca<'static>, until: Picoseconds| fast.advance(lca, until),
        );
    };
    for case in cases(bench.fixture) {
        let mut fast = FastForward::new(bench.netlist);
        let started = now();
        forward(&mut fast, &case);
        let elapsed = now() - started;
        let seconds = case.operations.last().map_or(0, |(at, _)| *at) as f64
            / super::super::super::super::time::CPU_HZ as f64;
        simulated += seconds;
        taken += elapsed;
        println!(
            "{}: {seconds:.3} s simulated in {elapsed:.3} s of CPU, {:.2}% of real time, {:?}",
            case.name,
            100.0 * elapsed / seconds.max(1e-9),
            fast.counts()
        );
    }
    println!(
        "all, fast-forwarded: {simulated:.3} s simulated in {taken:.3} s of CPU, {:.2}% of real time",
        100.0 * taken / simulated
    );
    // again, one fast-forward for every case, all it needs learned
    let mut fast = FastForward::new(bench.netlist);
    let each = |fast: &mut FastForward<'static>| {
        for case in cases(bench.fixture) {
            forward(fast, &case);
        }
    };
    each(&mut fast);
    let before = fast.counts();
    let started = now();
    each(&mut fast);
    let elapsed = now() - started;
    let after = fast.counts();
    println!(
        "all, learned: {simulated:.3} s simulated in {elapsed:.3} s of CPU, {:.2}% of real time, {} plans, {} periods learned",
        100.0 * elapsed / simulated,
        after.plans - before.plans,
        after.learned - before.learned
    );
    let mut lca = after_reset(bench);
    let started = now();
    lca.run_clocked(Picoseconds(lca.now().0 + (clocked_seconds * 1e12) as u64));
    let elapsed = now() - started;
    println!(
        "clocked edge by edge: {clocked_seconds:.3} s simulated in {elapsed:.3} s of CPU, {:.0}% of real time",
        100.0 * elapsed / clocked_seconds
    );
}

/// The plan and groups `FF_CASE` (early by default) of `FF_IMAGE`'s
/// bench (the Z3805A's by default) shows at
/// `FF_CYCLES` CPU cycles (just past its GPS 1 PPS by default), in words.
#[test]
#[ignore = "a diagnosis"]
fn describe_counting() {
    let bench = measured_bench();
    let mut early = case(
        bench,
        &std::env::var("FF_CASE").unwrap_or_else(|_| "early".to_owned()),
    );
    let at: u64 =
        std::env::var("FF_CYCLES").map_or(14_582_913, |cycles| cycles.parse().expect("cycles"));
    early.operations.retain(|(time, _)| *time < at);
    early.operations.push((at, vec!["end".to_owned()]));
    let mut fast = FastForward::new(bench.netlist);
    let mut lca = super::super::tests::replay(
        bench,
        &early,
        &mut |lca: &mut Lca<'static>, until: Picoseconds| fast.advance(lca, until),
    );
    println!("{}", fast.describe(&mut lca, 100_000));
}

use std::sync::LazyLock;
use std::sync::Mutex;

use proptest::collection::vec;
use proptest::prelude::Just;
use proptest::prelude::ProptestConfig;
use proptest::prelude::Strategy;
use proptest::prelude::any;
use proptest::prop_assert;
use proptest::prop_oneof;
use proptest::proptest;
use proptest::test_runner::TestCaseError;

use super::super::tests::replay;

/// One fast-forward for every property case of a bench, its tables kept
/// between them as the board keeps them between landings: the Z3805A's
/// one clock, the Z3815A's two, and the Z3816A's on its board's ACLK.
static SHARED: LazyLock<Mutex<FastForward<'static>>> =
    LazyLock::new(|| Mutex::new(FastForward::landing_short(&NETLIST_Z3805A)));
static SHARED_TWO_CLOCKS: LazyLock<Mutex<FastForward<'static>>> =
    LazyLock::new(|| Mutex::new(FastForward::landing_short(Z3815A.netlist)));
static SHARED_BOARD: LazyLock<Mutex<FastForward<'static>>> =
    LazyLock::new(|| Mutex::new(FastForward::landing_short(Z3816A_BOARD.netlist)));

/// The property tests' cases: `FF_CASES`, or a dozen, which an
/// unoptimized `cargo test` runs in reasonable time.
fn property_cases() -> u32 {
    std::env::var("FF_CASES").map_or(12, |cases| cases.parse().expect("a count"))
}

/// The benches' period, picoseconds: the single clock's, and the
/// telecom pair's two clocks' joint one.
const PERIOD: u64 = 100_000;

/// A bench operation, as differential.py writes it.
fn operation() -> impl Strategy<Value = Vec<String>> {
    prop_oneof![
        (0u8..4, any::<u8>()).prop_map(|(register, value)| vec![
            "write".to_owned(),
            register.to_string(),
            format!("{value:02x}")
        ]),
        Just(vec!["gps".to_owned()]),
        any::<u8>().prop_map(|value| vec!["pins".to_owned(), format!("{value:02x}")]),
    ]
}

/// A case from RESET's release through `operations`, each `gap` CPU
/// cycles after the last, to `rest` cycles past the last.
fn random_case(operations: Vec<(u64, Vec<String>)>, rest: u64) -> Case {
    let mut at = 48;
    let mut case = Case {
        name: "random".to_owned(),
        ..Case::default()
    };
    case.operations.push((16, vec!["reset".to_owned()]));
    case.operations
        .push((at, vec!["pins".to_owned(), "5a".to_owned()]));
    for (gap, words) in operations {
        at += gap;
        case.operations.push((at, words));
    }
    case.operations.push((at + rest, vec!["end".to_owned()]));
    case
}

/// `bench` after `case`, fast-forwarded by `shared`.
fn reached(bench: &Bench, shared: &Mutex<FastForward<'static>>, case: &Case) -> Lca<'static> {
    let mut fast = shared.lock().expect("the shared fast-forward");
    replay(
        bench,
        case,
        &mut |lca: &mut Lca<'static>, until: Picoseconds| fast.advance(lca, until),
    )
}

/// From `start`, fast-forward by no time is no change, and to `at` and
/// a period more is to `at` and the period clocked.
fn iterated(
    bench: &Bench,
    shared: &Mutex<FastForward<'static>>,
    start: &Lca<'static>,
    at: Picoseconds,
) -> Result<(), TestCaseError> {
    let mut fast = shared.lock().expect("the shared fast-forward");
    let mut zero = start.clone();
    fast.advance(&mut zero, start.now());
    prop_assert!(zero.snapshot() == start.snapshot());
    let mut stepped = start.clone();
    fast.advance(&mut stepped, at);
    stepped.run_clocked(Picoseconds(at.0 + PERIOD));
    let mut forwarded = start.clone();
    fast.advance(&mut forwarded, Picoseconds(at.0 + PERIOD));
    prop_assert!(
        stepped.snapshot() == forwarded.snapshot(),
        "{}",
        difference(bench.netlist, &stepped.snapshot(), &forwarded.snapshot())
    );
    Ok(())
}

/// From `start`, fast-forward to `at` is clocking to it.
fn clocked_to(
    bench: &Bench,
    shared: &Mutex<FastForward<'static>>,
    start: &Lca<'static>,
    at: Picoseconds,
) -> Result<(), TestCaseError> {
    let mut fast = shared.lock().expect("the shared fast-forward");
    let mut clocked = start.clone();
    clocked.run_clocked(at);
    let mut forwarded = start.clone();
    fast.advance(&mut forwarded, at);
    prop_assert!(
        clocked.snapshot() == forwarded.snapshot(),
        "{}",
        difference(bench.netlist, &clocked.snapshot(), &forwarded.snapshot())
    );
    Ok(())
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(property_cases()))]

    /// From a state a random stimulus reaches, fast-forward by no time
    /// is no change, and by a period more is one period clocked more:
    /// F(s, 0) = s and F(s, n + 1) = T(F(s, n)).
    #[test]
    fn fast_forward_is_the_period_iterated(
        operations in vec((64u64..1_000_000, operation()), 0..4),
        rest in 64u64..1_000_000,
        periods in 0u64..200_000,
        phase in 0u64..PERIOD,
    ) {
        let start = reached(&Z3805A, &SHARED, &random_case(operations, rest));
        let at = Picoseconds(start.now().0 + periods * PERIOD + phase);
        iterated(&Z3805A, &SHARED, &start, at)?;
    }

    /// Fast-forward a few thousand periods is them clocked:
    /// F(s, n) = T^n(s).
    #[test]
    fn fast_forward_is_the_clock(
        operations in vec((64u64..1_000_000, operation()), 0..4),
        rest in 64u64..1_000_000,
        periods in 0u64..3_000,
        phase in 0u64..PERIOD,
    ) {
        let start = reached(&Z3805A, &SHARED, &random_case(operations, rest));
        let at = Picoseconds(start.now().0 + periods * PERIOD + phase);
        clocked_to(&Z3805A, &SHARED, &start, at)?;
    }

    /// F(s, 0) = s and F(s, n + 1) = T(F(s, n)) on two clocks, the
    /// Z3815A's.
    #[test]
    fn two_clocks_fast_forward_is_the_period_iterated(
        operations in vec((64u64..1_000_000, operation()), 0..4),
        rest in 64u64..1_000_000,
        periods in 0u64..200_000,
        phase in 0u64..PERIOD,
    ) {
        let start = reached(&Z3815A, &SHARED_TWO_CLOCKS, &random_case(operations, rest));
        let at = Picoseconds(start.now().0 + periods * PERIOD + phase);
        iterated(&Z3815A, &SHARED_TWO_CLOCKS, &start, at)?;
    }

    /// F(s, n) = T^n(s) on two clocks, the Z3815A's.
    #[test]
    fn two_clocks_fast_forward_is_the_clock(
        operations in vec((64u64..1_000_000, operation()), 0..4),
        rest in 64u64..1_000_000,
        periods in 0u64..3_000,
        phase in 0u64..PERIOD,
    ) {
        let start = reached(&Z3815A, &SHARED_TWO_CLOCKS, &random_case(operations, rest));
        let at = Picoseconds(start.now().0 + periods * PERIOD + phase);
        clocked_to(&Z3815A, &SHARED_TWO_CLOCKS, &start, at)?;
    }

    /// F(s, n) = T^n(s) on the Z3816A's board's ACLK, where a count
    /// runs with a copy of GCLK.
    #[test]
    fn board_fast_forward_is_the_clock(
        operations in vec((64u64..1_000_000, operation()), 0..4),
        rest in 64u64..1_000_000,
        periods in 0u64..3_000,
        phase in 0u64..PERIOD,
    ) {
        let start = reached(&Z3816A_BOARD, &SHARED_BOARD, &random_case(operations, rest));
        let at = Picoseconds(start.now().0 + periods * PERIOD + phase);
        clocked_to(&Z3816A_BOARD, &SHARED_BOARD, &start, at)?;
    }
}

/// A plan made `FF_PLANS` times (1000 by default) at the bench after
/// RESET, for a profile.
#[test]
#[ignore = "a measure, optimized"]
fn plan_speed() {
    let plans: usize =
        std::env::var("FF_PLANS").map_or(1000, |plans| plans.parse().expect("a count"));
    let netlist = &*NETLIST_Z3805A;
    let graph = Graph::new(netlist);
    let lca = after_reset(&Z3805A);
    let started = cpu_seconds();
    for _ in 0..plans {
        Plan::new(&graph, netlist, &lca, &[], &[]).expect("a plan");
    }
    if let (Some(started), Some(now)) = (started, cpu_seconds()) {
        println!("{:.1} us a plan", (now - started) * 1e6 / plans as f64);
    }
}
