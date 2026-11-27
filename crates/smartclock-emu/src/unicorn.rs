//! The run on the Unicorn core, the development core (docs/emulator.md,
//! "The cores"): the image on smartclock's own map of the board,
//! `board`, run by the machine (`machine.rs`), its ports' far ends
//! alongside it.

use std::cell::RefCell;
use std::rc::Rc;

use crate::board::Board;
use crate::board::eeprom::Eeprom;
use crate::board::time::Cycles;
use crate::config::ConfigError;
use crate::config::Eeprom as ConfigEeprom;
use crate::config::GpsSource;
use crate::config::RunConfig;
use crate::control::Control;
use crate::cpu;
use crate::files::Files;
use crate::gps::engine::EngineError;
use crate::gps::engine::Synthetic;
use crate::gps::external::Pulse;
use crate::gps::external::PulseClock;
use crate::gps::range::OutsideError;
use crate::host::HostError;
use crate::host::Ports;
use crate::host::attach;
use crate::host::host_side;
use crate::host::sci_line;
use crate::host::serve;
use crate::line::Line;
use crate::machine::Machine;
use crate::machine::Options;
use crate::names::Named as _;
use crate::ports::GPS;
use crate::ports::HOST;
use crate::run::no_front_panel;
use crate::script::Heard;
use crate::script::Injected;
use crate::script::ScriptWiring;
use crate::trace;

/// Why a run on the Unicorn core could not start, or stopped short.
#[derive(Debug, thiserror::Error)]
pub enum Error {
    /// A line's far end not put on.
    #[error(transparent)]
    Host(#[from] HostError),
    /// The synthetic engine's position not one it takes.
    #[error(transparent)]
    Position(#[from] OutsideError),
    /// The synthetic engine not made as configured.
    #[error(transparent)]
    Engine(#[from] EngineError),
    /// The core's own: the board, the CPU or the run.
    #[error(transparent)]
    Core(Box<dyn std::error::Error + Send + Sync>),
}

/// The core's own error `error`.
fn core(error: impl std::error::Error + Send + Sync + 'static) -> Error {
    Error::Core(Box::new(error))
}

/// Runs `config`'s image on the Unicorn core, on its model's board as
/// `board` maps it, its lines' far ends `ports`', its script read from
/// `files`.
///
/// # Errors
///
/// The board not built, a far end not put on, or the run stopped short.
pub fn run(config: &RunConfig, ports: &mut dyn Ports, files: &dyn Files) -> Result<(), Error> {
    let model = config.model;
    let sci = sci_line(config, ports)?;
    let gps = Line::new(GPS, config.tap);
    let host = Line::new(HOST, config.tap);
    let until = config.until.map(|until| until.cycles());
    let ConfigEeprom::File { path, dump } = &config.eeprom else {
        return Err(core(ConfigError::MemoryOnUnicorn));
    };
    let eeprom = Eeprom::open(path, dump.as_deref()).map_err(core)?;
    let board = Rc::new(RefCell::new(
        Board::new(model, sci.clone(), gps.clone(), host.clone(), eeprom)
            .with_noise_seed(config.seed)
            .with_ocxo(config.ocxo.over(config.model.ocxo()), config.seed)
            .map_err(core)?
            .with_indicators(config.indicators),
    ));
    let cpu = cpu::core(board.clone()).map_err(core)?;
    let options = Options {
        pacing: config.pacing,
        tracing: config.trace.tracing(),
        watch: config.trace.watch.clone(),
        watch_code: config.trace.watch_code.clone(),
        count_instructions: config.trace.count_instructions,
    };
    let mut machine = Machine::new(cpu, board.clone(), &config.image, &options).map_err(core)?;
    let panel = Rc::clone(&board);
    // what the engine is sent, for a script's expect-gps steps only
    let heard = config.scripted().then(Heard::default);
    let injected = Injected::default();
    let wiring = ScriptWiring {
        keys: Some(Box::new(move |key| {
            if !panel.borrow_mut().press(key) {
                no_front_panel(key);
            }
        })),
        controls: Some(Box::new(not_on_unicorn)),
        gps_heard: heard.as_ref().map(Rc::clone),
        gps_injected: Some(Rc::clone(&injected)),
    };
    if let Some(alongside) = host_side(config, ports, files, &host, wiring)? {
        machine.run_alongside(alongside);
    }
    serve(ports, config.sci_address(), sci)?;
    match &config.gps {
        GpsSource::External(address) => {
            let name = gps.name();
            let far = attach(gps.clone());
            ports
                .serve(address, name, far)
                .map_err(|source| HostError::Serve {
                    address: address.clone(),
                    source,
                })?;
            // the 1 PPS on the run's whole seconds, or the host's in real
            // time (`gps::external`)
            machine.run_alongside(Box::new(Pulse::new(
                board,
                Cycles(0),
                PulseClock::of(config.pacing),
            )));
        }
        GpsSource::Synthetic => {
            let mut engine =
                Synthetic::new(gps.attach(), config.synthetic.config(model)?, config.tap)?;
            if let Some(heard) = heard {
                engine.on_heard(Box::new(move |bytes| {
                    heard.borrow_mut().extend_from_slice(bytes);
                }));
            }
            engine.on_injected(injected);
            // the engine's 1 PPS, at each of its seconds, to the board
            engine.on_pulse(Box::new(move |pulse| {
                board.borrow_mut().gps_pps(pulse.cycles());
            }));
            machine.run_alongside(Box::new(engine));
        }
    }
    let stopped = machine.run(until).map_err(core)?;
    trace::emit(&stopped);
    Ok(())
}

/// Says that the Unicorn core applies no simulation control.
fn not_on_unicorn(control: Control) {
    trace::note(format_args!(
        "the Unicorn core takes no controls; event {} not applied",
        control.name()
    ));
}
