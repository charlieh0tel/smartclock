//! The telecom netlists on their boards' clocks (pins.rs, `Wiring`):
//! ACLK alone, P7 held Low, reads, pulses and intervals as each does
//! with P7 running at the 10 MHz the board locks ACLK to.  P7 clocks
//! only the PLL's P7 divider and its phase detector, P38 and P40
//! (z3816a-trace.md and z3815a-trace.md, "Clocks"); the one path from its domain into the
//! rest, IH.Y into JH, carries nothing under any sync field the
//! fixture's cases set.  So the board drops P7, and fast-forward sees
//! one clock.

use super::super::fixture::Case;
use super::super::fixture::cases;
use super::super::pins::ACLK;
use super::super::pins::ACLK_ALONE;
use super::super::pins::ACLK_TICK;
use super::super::pins::ACLK_Z3815A;
use super::super::pins::P7;
use super::super::pins::Wiring;
use super::Clock;
use super::Lca;
use super::Picoseconds;
use super::Pin;
use super::tests::Bench;
use super::tests::Run;
use super::tests::SHORT_CASES;
use super::tests::Z3815A;
use super::tests::Z3816A;
use super::tests::case;
use super::tests::run_with;

/// The 10 MHz on P7 at the board's ratio to ACLK, as the PLL locks them.
const P7_LOCKED: Clock = Clock {
    pin: P7,
    first_rise: Picoseconds(0),
    half_period: Picoseconds(ACLK_TICK / 2),
};

/// The pins besides P83 and the interval that the firmware sees or that
/// the 1 PPS chain drives: P20, the pulse ahead of P83, and P81, the
/// interval's pin; P14 and P28, the every-other-second outputs.
const WATCHED: [&str; 4] = ["20", "81", "14", "28"];

/// The phase detector's outputs, the PLL's, which differ by design.
const DETECTOR: [&str; 2] = ["38", "40"];

/// `case` with `WATCHED` watched from its start, and the detector not.
fn watching(case: &Case) -> Case {
    let watches = WATCHED
        .iter()
        .map(|pin| (0, vec!["watch".to_owned(), (*pin).to_owned()]));
    let operations = case.operations.iter().filter(|(_, words)| {
        !matches!(words.as_slice(), [watch, pin] if watch == "watch" && DETECTOR.contains(&pin.as_str()))
    });
    Case {
        name: case.name.clone(),
        operations: watches.chain(operations.cloned()).collect(),
        ..Case::default()
    }
}

/// A telecom design and how its board wires it: ACLK alone, P7 held Low
/// beside the inputs its bench holds.
struct Board {
    design: &'static Bench,
    wiring: &'static Wiring,
}

/// The telecom designs on their boards.
const BOARDS: [Board; 2] = [
    Board {
        design: &Z3815A,
        wiring: &ACLK_Z3815A,
    },
    Board {
        design: &Z3816A,
        wiring: &ACLK_ALONE,
    },
];

/// The two clocks the board locks: ACLK, and P7 at its ratio to it.
const LOCKED: [Clock; 2] = [ACLK, P7_LOCKED];

/// `case` on `design`'s netlist with `clocks` and `held`, clocked edge
/// by edge: what it read, pulsed, measured and the watched pins did.
fn outputs(
    design: &'static Bench,
    clocks: &'static [Clock],
    held: &'static [(Pin, bool)],
    case: &Case,
) -> Run {
    let bench = Bench {
        clocks,
        held,
        ..*design
    };
    let (run, _) = run_with(
        &bench,
        &watching(case),
        &mut |lca: &mut Lca<'static>, until| lca.run_clocked(until),
        &mut |_, _| {},
    );
    run
}

/// `case`'s outputs on `board` with P7 held and with it running alike.
fn alike(board: &Board, case: &Case) {
    let held = outputs(board.design, board.wiring.clocks, board.wiring.held, case);
    let running = outputs(board.design, &LOCKED, board.design.held, case);
    assert_eq!(held, running, "{} {}", board.design.image, case.name);
}

/// Each board holds what its design's bench holds, and P7.
#[test]
fn each_board_holds_its_benchs_inputs_and_p7() {
    for board in &BOARDS {
        let mut held = board.design.held.to_vec();
        held.push((P7, false));
        held.sort_by_key(|(pin, _)| pin.0);
        let mut wired = board.wiring.held.to_vec();
        wired.sort_by_key(|(pin, _)| pin.0);
        assert_eq!(wired, held, "{}", board.design.image);
        assert_eq!(board.wiring.clocks, [ACLK], "{}", board.design.image);
    }
}

#[test]
fn the_telecom_designs_read_alike_with_p7_held_or_running() {
    for board in &BOARDS {
        for name in SHORT_CASES.iter().chain(&["outputs"]) {
            alike(board, &case(board.design, name));
        }
    }
}

#[test]
#[ignore = "seconds of both clocks edge by edge: minutes, optimized"]
fn the_telecom_designs_read_alike_with_p7_held_or_running_in_every_case() {
    std::thread::scope(|scope| {
        for board in &BOARDS {
            for case in cases(board.design.fixture) {
                scope.spawn(move || alike(board, &case));
            }
        }
    });
}
