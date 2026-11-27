//! The receivers' own firmware on the LCA model, the FPGA's bitstream
//! executed (docs/firmware/fpga-codegen.md): `--fpga-design lca` in
//! place of the board file's hand model.  The configuration the firmware
//! sends picks the netlist, so the Z3805A 3543B boots on its
//! bitstream's netlist, and the 58503A 3704's configuration picks the
//! 58503A's and the Z3801A's, which the bitstream carries.  The LCA
//! model, fast-forwarded, is still slower than the hand model, and
//! unoptimized much slower, so the Z3805A's boot is ignored but when
//! asked; with
//! `--nocapture` it prints how much slower than the hand model and than
//! real time the board runs (`make bench-lca`).  Only with `mc683xx`.

#![cfg(feature = "mc683xx")]

mod common;

use std::path::Path;
use std::time::Instant;

use common::HP58503A;
use common::Output;
use common::Receiver;
use common::Scratch;
use common::Z3805A;
use common::run_receiver;
use common::run_receiver_with_engine;
use common::script;

/// The FPGA's design as `--fpga-design` names it.
const LCA: [&str; 2] = ["--fpga-design", "lca"];

/// The trace's line once the configuration has picked the Z3805A's
/// netlist.
const RUNNING: &str = "smartclock-emu: fpga: the LCA model runs z3805a.bit's netlist";

/// The board's seconds the Z3805A's boot runs: its prompt at about
/// 1.1 s, then `*IDN?` at 2 s (`boot_mc683xx.rs`).
const SESSION: &str = "# the Z3805A's boot on the LCA model
expect scpi >\\x20 within 5
at 2
send *IDN?
expect scpi >\\x20 within 5
stop
";

/// The longest a run may take, board seconds.
const LIMIT: &str = "30";

/// The seconds a run took: on the wall clock, and of the CPU, user and
/// system, where the host says (the emulator's children's, on Unix),
/// which a loaded host moves less.
#[derive(Debug, Clone, Copy)]
struct Took {
    wall: f64,
    cpu: Option<f64>,
}

/// The CPU's seconds this process's children have taken, where the
/// host says.
#[cfg(unix)]
fn children_cpu() -> Option<f64> {
    let usage =
        nix::sys::resource::getrusage(nix::sys::resource::UsageWho::RUSAGE_CHILDREN).ok()?;
    let seconds = |time: nix::sys::time::TimeVal| {
        time.tv_sec() as f64 + time.tv_usec() as f64 / MICROSECONDS_PER_SECOND
    };
    Some(seconds(usage.user_time()) + seconds(usage.system_time()))
}

#[cfg(not(unix))]
fn children_cpu() -> Option<f64> {
    None
}

/// Microseconds a second, for `children_cpu`'s reading.
#[cfg(unix)]
const MICROSECONDS_PER_SECOND: f64 = 1e6;

/// How a receiver is run: `run_receiver`, the GPS's port served and
/// nothing on it, or `run_receiver_with_engine`, the synthetic GPS
/// engine on it, its 1 PPS on the FPGA's pin, as the browser's module
/// runs it.
type Launch = fn(Receiver, &Path, &[&str]) -> Output;

/// `receiver` run by `launch` on `options` and the session `steps`: its
/// transcript, its trace, and the seconds it took.
fn timed(
    launch: Launch,
    receiver: Receiver,
    name: &str,
    steps: &str,
    options: &[&str],
) -> (String, Vec<String>, Took) {
    let eeprom = Scratch::new(name);
    let script = script(name, steps);
    let path = script.0.to_str().expect("a path");
    let started = Instant::now();
    let cpu = children_cpu();
    let (transcript, trace) = launch(
        receiver,
        &eeprom.0,
        &[&["--script", path, "--stop-after", LIMIT][..], options].concat(),
    );
    let took = Took {
        wall: started.elapsed().as_secs_f64(),
        cpu: children_cpu()
            .zip(cpu)
            .map(|(after, before)| after - before),
    };
    (
        String::from_utf8_lossy(&transcript).into_owned(),
        trace,
        took,
    )
}

/// The 58503A's configuration, as the Z3805A's at about 1.07 s of the
/// board's time, picks the netlist of the bitstream it shares with the
/// Z3801A.
#[test]
fn the_58503as_configuration_picks_its_netlist() {
    let (_, trace, _) = timed(run_receiver, HP58503A, "lca-58503a", "at 1.1\nstop\n", &LCA);
    let running = "smartclock-emu: fpga: the LCA model runs 58503a-z3801a.bit's netlist";
    assert!(trace.iter().any(|line| line == running), "{trace:?}");
}

/// The Z3805A's configuration, at about 1.07 s of the board's time,
/// picks its bitstream's netlist, which runs from there.
#[test]
fn the_z3805as_configuration_picks_its_netlist() {
    let (_, trace, _) = timed(
        run_receiver,
        Z3805A,
        "lca-z3805a-configured",
        "at 1.1\nstop\n",
        &LCA,
    );
    assert!(trace.iter().any(|line| line == RUNNING), "{trace:?}");
}

/// The Z3805A boots on its own bitstream's netlist and answers as it
/// does on the hand model.
#[test]
#[ignore = "two seconds of the board on each model: optimized"]
fn the_z3805a_boots_on_its_bitstreams_netlist() {
    let (hand, ..) = timed(run_receiver, Z3805A, "lca-hand", SESSION, &[]);
    let (lca, trace, ..) = timed(run_receiver, Z3805A, "lca-z3805a", SESSION, &LCA);
    assert!(trace.iter().any(|line| line == RUNNING), "{trace:?}");
    assert_eq!(
        lca, "scpi > *IDN?\r\nHEWLETT-PACKARD,Z3805A,3625A01487,3543B-A\r\nscpi > ",
        "the image's revision and the EEPROM's serial number"
    );
    assert_eq!(lca, hand);
}

/// The board's seconds the speed is measured over, from just past the
/// configuration, as `smartclock-emu-web`'s `lca-speed.mjs` measures the
/// browser's module.
const CONFIGURED: f64 = 1.1;
const MEASURED: f64 = 10.0;

/// How much slower than real time, and than the hand model, the board
/// runs on the LCA model, the Z3805A's boot over `MEASURED` seconds
/// from `CONFIGURED`, the synthetic GPS engine on, as the browser's
/// module runs it (its 1 PPS lands the LCA model each second): each
/// design's run to the end less its run to the start, on the wall clock
/// and of the CPU.  Printed, a measure, not a check, on a host that may
/// be loaded.
#[test]
#[ignore = "ten seconds of the board, on each model, twice: a measure, optimized"]
fn the_lca_models_speed() {
    let took = |options: &[&str], until: f64| {
        let stop = format!("at {until}\nstop\n");
        timed(
            run_receiver_with_engine,
            Z3805A,
            "lca-speed",
            &stop,
            options,
        )
        .2
    };
    let [hand, lca] = [&[][..], &LCA].map(|options| {
        let (to, from) = (
            took(options, CONFIGURED + MEASURED),
            took(options, CONFIGURED),
        );
        Took {
            wall: to.wall - from.wall,
            cpu: to.cpu.zip(from.cpu).map(|(to, from)| to - from),
        }
    });
    let report = |what: &str, hand: f64, lca: f64| {
        println!(
            "native, {what}, the board's {MEASURED:.3} s from {CONFIGURED:.2} s: the hand model {hand:.3} s, \
             the LCA model {lca:.3} s, {:.1} times slower than real time, {:.0} times slower than the hand model",
            lca / MEASURED,
            lca / hand
        );
    };
    report("the wall clock", hand.wall, lca.wall);
    if let (Some(hand), Some(lca)) = (hand.cpu, lca.cpu) {
        report("the CPU", hand, lca);
    }
}
