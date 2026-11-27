//! The board around the CPU: the devices behind the MC68331's chip
//! selects and its own modules, as the Z3805A 3543B's reset code maps
//! them (docs/firmware/restart.md, "The chip selects"), and time.
//! Chip select 5's 64 KiB window holds four devices 8 KiB apart,
//! ADDR13 and ADDR14 picking one: the lamp latch, the switch byte, the
//! FPGA's register port and the EFC's strobe.  The 58503A's map is the
//! same but for chip select 4, which it never sets up and nothing
//! addresses; what else differs is its profile (`profile`).
//!
//! The CPU core reaches every device through `Bus`, a byte at a time:
//! a word or long access is its bytes, high first, as the CPU32's
//! dynamic bus sizing makes it on these 8- and 16-bit ports.  Between
//! the core's runs the machine moves time on (`advance`), asks which
//! interrupt is requested (`interrupt`), and tells the device its
//! request was taken (`acknowledge`).

pub mod time;

pub(crate) mod eeprom;
pub(crate) mod front_panel;
pub mod profile;

mod adc;
pub(crate) mod analog;
mod clock;
mod dac;
mod duart;
mod fpga;
#[cfg(feature = "mc683xx")]
mod fpga_part;
mod gpt;
mod latch;
#[cfg(feature = "mc683xx")]
pub(crate) mod lifetime;
mod noise;
pub mod ocxo;
#[cfg(feature = "mc683xx")]
pub(crate) mod parameters;
#[cfg(feature = "mc683xx")]
pub(crate) mod participants;
mod qsm;
#[cfg(feature = "mc683xx")]
pub(crate) mod registry;
mod select;
mod sim;
#[cfg(feature = "mc683xx")]
mod snapshot;
#[cfg(feature = "mc683xx")]
mod spi;
mod strobe;
mod switch;
#[cfg(feature = "mc683xx")]
pub(crate) mod system_clock;

use std::collections::BTreeSet;

use self::adc::Adc;
use self::adc::Wired;
use self::analog::Wiring;
use self::dac::EfcDac;
use self::dac::HexDac;
use self::duart::Duart;
use self::eeprom::Eeprom;
use self::fpga::Fpga;
use self::fpga::Lines;
use self::fpga::interpolator::Trims;
use self::front_panel::FrontPanel;
use self::front_panel::Key;
use self::gpt::Gpt;
use self::latch::Latch;
use self::ocxo::Config as OcxoConfig;
use self::ocxo::Ocxo;
use self::ocxo::OcxoError;
use self::profile::Model;
use self::qsm::Peripherals;
use self::qsm::Qsm;
use self::select::Select;
use self::sim::Sim;
use self::strobe::Strobe;
use self::switch::Switch;
use self::time::Cycles;
use crate::indicators::Bank;
use crate::indicators::Indicator;
use crate::indicators::Polarity;
use crate::line::Line;
use crate::trace;

/// The flash, at address zero (CSBOOT and CS1).
pub(crate) const FLASH: u32 = 0x00_0000;

/// The seed the board's modeled noise starts from unless `--seed`
/// gives another: fixed, so that runs repeat.
pub const NOISE_SEED: u64 = 0x5a38_05a0;
pub(crate) const FLASH_SIZE: u32 = 0x8_0000;
/// The RAM (CS0, CS2, CS3).
pub(crate) const RAM: u32 = 0x10_0000;
pub(crate) const RAM_SIZE: u32 = 0x1_0000;
/// The DUART (CS8).
pub(crate) const DUART: u32 = 0x20_0000;
/// Chip select 5's block: the latch, the switch byte, the FPGA, the
/// EFC's strobe.
pub(crate) const CS5: u32 = 0x30_0000;
/// The EEPROM (CS9).
pub(crate) const EEPROM: u32 = 0x40_0000;
/// Chip select 4, which nothing in the firmware uses.
pub(crate) const CS4: u32 = 0x50_0000;
/// The MC68331's modules: the GPT, the SIM and the QSM.
pub(crate) const MODULES: u32 = 0xff_f000;
const GPT: u32 = 0xff_f900;
const SIM: u32 = 0xff_fa00;
const QSM: u32 = 0xff_fc00;

/// The device regions the core maps to the bus, each a whole number of
/// 4 KiB pages: start and size.
pub(crate) const DEVICE_REGIONS: [(u32, u32); 5] = [
    (DUART, 0x1000),
    (CS5, fpga::SIZE),
    (EEPROM, eeprom::SIZE),
    (CS4, 0x1000),
    (MODULES, 0x1000),
];

/// What reads back from a device page where no register is: the
/// MC68331's bus would take a bus error for an unselected address, but
/// these are inside chip selects and modules, so they read as idle.
const NOTHING: u8 = 0;

/// The CPU's view of the devices.  An access comes with its own time,
/// the core's estimate within the slice running, at or after the
/// board's `now`, the slice's start: the EEPROM takes it, its byte
/// load time, 150 µs, being shorter than a slice, and the FPGA, whose
/// count runs four ticks past the tap that sets its stopped bit, so
/// that a read of the count after a read of the status sees what came
/// between (docs/emulator.md, "Time"); every other device keeps to the
/// board's `now`, as it did.
pub(crate) trait Bus {
    /// Reads `size` bytes, 1, 2 or 4, at `at`, high byte first, at `now`.
    fn read(&mut self, at: u32, size: u32, now: Cycles) -> u32;
    /// Writes `size` bytes of `value` at `at`, high byte first, at `now`.
    fn write(&mut self, at: u32, size: u32, value: u32, now: Cycles);
}

/// Which device asks for an interrupt.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Source {
    /// The SIM's periodic timer.
    Periodic,
    /// The GPT's output compares.
    Gpt,
    /// The QSPI's end of queue.
    Qspi,
    /// The SCI.
    Sci,
    /// The DUART.
    Duart,
}

/// An interrupt a device asks for.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Request {
    /// Its level, 1 to 7.
    pub(crate) level: u8,
    /// The vector the CPU takes it through.
    pub(crate) vector: u8,
    /// Who asks.
    pub(crate) source: Source,
}

/// Why the CPU is reset, each named as mc683xx's `ResetCause` names
/// it (the trace's `Restart`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Reset {
    /// Power-on, the board's start.
    PowerOn,
    /// The software watchdog timed out (restart.md, "The watchdog").
    Watchdog,
}

impl Reset {
    /// RSR after a reset for this cause (MC68331UM D.3.4): power-on
    /// sets POW and EXT, as the board has always started, the watchdog
    /// SW.
    fn status(self) -> u8 {
        match self {
            Self::PowerOn => sim::RSR_POWER_UP | sim::RSR_EXTERNAL,
            Self::Watchdog => sim::RSR_SOFTWARE_WATCHDOG,
        }
    }
}

/// The QSPI's devices, by the board's decode of its chip selects
/// (structures.md, "The QSPI's devices"); the board keeps them apart
/// from the QSM so that the QSM can run its queue against them.
#[derive(Debug, Default)]
struct QspiDevices {
    /// The ADC, on select 1, its inputs the board's sources.
    adc: Wired<Wiring>,
    /// The FPGA, on select 6.
    fpga: Fpga,
    /// The hex DAC, on select 2.
    hex_dac: HexDac,
    /// The EFC's DAC, on select 7.
    efc_dac: EfcDac,
    /// The 58503A's front panel, on select 0, all four lines low.
    front_panel: Option<FrontPanel>,
    /// Now, for the devices that change with time.
    now: Cycles,
}

/// The QSPI's chip selects.
const SELECT_FRONT_PANEL: u8 = 0;
const SELECT_ADC: u8 = 1;
const SELECT_HEX_DAC: u8 = 2;
const SELECT_FPGA: u8 = 6;
const SELECT_EFC_DAC: u8 = 7;

impl Peripherals for QspiDevices {
    fn exchange(&mut self, select: u8, entry: usize, word: u16, bits: u8) -> u16 {
        match select {
            SELECT_ADC => self.adc.exchange(word, self.now),
            SELECT_FPGA => self.fpga.exchange(word, bits),
            SELECT_HEX_DAC => self.hex_dac.exchange(entry, word),
            SELECT_EFC_DAC => self.efc_dac.exchange(word),
            SELECT_FRONT_PANEL => self
                .front_panel
                .as_mut()
                .map_or(0, |panel| panel.exchange(word, self.now)),
            _ => 0,
        }
    }
}

/// The 58503A's alarm output: PF5, low while the alarm is on
/// (58503a-3704.md, "The alarm outputs").
const PORTF_ALARM_BIT: u8 = 5;
const PORTF_ALARM: u8 = 1 << PORTF_ALARM_BIT;
const ALARM_OUTPUT: [Indicator; 1] = [Indicator {
    bit: PORTF_ALARM_BIT,
    name: "alarm",
}];

/// The alarm output's bank, port F's pins: PF5 off until the reset
/// code drives it, an input reading as high.
fn alarm_output() -> Bank {
    Bank::new("output", &ALARM_OUTPUT, Polarity::Low, u8::MAX)
}

/// The board.
#[derive(Debug)]
pub(crate) struct Board {
    /// Now, on the CPU's clock.
    now: Cycles,
    sim: Sim,
    gpt: Gpt,
    qsm: Qsm,
    duart: Duart,
    qspi: QspiDevices,
    eeprom: Eeprom,
    /// The oscillator, its EFC from the EFC's DAC; its clock is the
    /// FPGA's, on its P7.  A reset of the CPU does not touch it.
    ocxo: Ocxo,
    /// The front-panel latch, the lamps.
    latch: Latch,
    /// The switch byte beside it.
    switch: Switch,
    /// The EFC's strobe.
    strobe: Strobe,
    /// The 58503A's alarm output, PF5, as port F's pins drive it.
    alarm_output: Option<Bank>,
    /// A reset the board has asked for, not yet carried out.
    reset: Option<Reset>,
    /// Addresses an unmodeled access has been reported at, to report
    /// each once.
    reported: BTreeSet<u32>,
}

impl Board {
    /// `model`'s board at power-up: the SCI on `sci`, the DUART's
    /// channel A, the GPS engine's port, on `gps`, its channel B, the
    /// host port, on `host`, and `eeprom`.
    pub(crate) fn new(model: Model, sci: Line, gps: Line, host: Line, eeprom: Eeprom) -> Self {
        Self {
            now: Cycles(0),
            sim: Sim::new(Reset::PowerOn.status(), Cycles(0)),
            gpt: Gpt::new(Cycles(0)),
            qsm: Qsm::new(sci),
            duart: Duart::new(gps, host),
            qspi: QspiDevices {
                front_panel: model.has_front_panel().then(FrontPanel::default),
                ..QspiDevices::default()
            },
            eeprom,
            ocxo: Ocxo::default(),
            latch: Latch::new(model.lamps()),
            switch: Switch::default(),
            strobe: Strobe::default(),
            alarm_output: model.has_alarm_output().then(alarm_output),
            reset: None,
            reported: BTreeSet::new(),
        }
    }

    /// The board with its indicators, the lamps, the LEDs, the alarm
    /// output and the front panel's display, reported from power-up
    /// when `reported`.
    pub(crate) fn with_indicators(mut self, reported: bool) -> Self {
        if reported {
            self.latch.report_from(self.now);
            self.duart.report_indicators(self.now);
            if let Some(output) = &mut self.alarm_output {
                output.report_from(self.now);
            }
            if let Some(panel) = &mut self.qspi.front_panel {
                panel.report();
            }
        }
        self
    }

    /// `key` pressed on the front panel: whether the board has one.
    pub(crate) fn press(&mut self, key: Key) -> bool {
        self.qspi
            .front_panel
            .as_mut()
            .map(|panel| panel.press(key))
            .is_some()
    }

    /// The board with its modeled noise, the ADC's, from `seed`.
    pub(crate) fn with_noise_seed(mut self, seed: u64) -> Self {
        self.qspi.adc.adc = Adc::new(seed);
        self
    }

    /// The board with its oscillator's parameters `config`, its noise
    /// from `seed`; refused for a term not a finite number.
    pub(crate) fn with_ocxo(mut self, config: OcxoConfig, seed: u64) -> Result<Self, OcxoError> {
        self.ocxo = Ocxo::new(config, seed)?;
        Ok(self)
    }

    /// Now.
    pub(crate) fn now(&self) -> Cycles {
        self.now
    }

    /// Time moves on by `span`.
    pub(crate) fn advance(&mut self, span: Cycles) {
        self.now = self.now + span;
        let now = self.now;
        if self.sim.advance(now) {
            self.reset = Some(Reset::Watchdog);
        }
        self.gpt.advance(now);
        self.qspi.now = now;
        self.qsm.advance(now, &mut self.qspi);
        self.ocxo.advance(now);
        if let Some(pulse) = self.qspi.fpga.advance(now, &self.ocxo) {
            self.gpt.capture_1(pulse);
        }
        select::advance(&mut self.duart, now);
        select::advance(&mut self.eeprom, now);
    }

    /// The span until a device next changes by itself, at most `limit`.
    /// The EEPROM's write cycle is not among them: its end is seen at
    /// the next access or slice, as the firmware's polls see it.
    pub(crate) fn until_next_event(&self, limit: Cycles) -> Cycles {
        [
            self.sim.next_event(),
            self.gpt.next_event(),
            self.qsm.next_event(),
            select::next_event(&self.duart),
            self.qspi.fpga.next_event(&self.ocxo),
            self.ocxo.next_event(),
        ]
        .into_iter()
        .flatten()
        .map(|at| self.now.until(at))
        .fold(limit, Cycles::min)
    }

    /// The highest-level interrupt asked for.  Within a level the SIM
    /// comes first, then the GPT, the QSM and the DUART, by their
    /// arbitration numbers (startup.h: GPTMCR 12, QSMCR 8; the SIM's 15
    /// by reset; the DUART's acknowledge is external).
    pub(crate) fn interrupt(&self) -> Option<Request> {
        [
            self.sim
                .interrupt()
                .map(|request| (request, Source::Periodic)),
            self.gpt.interrupt().map(|request| (request, Source::Gpt)),
            self.qsm
                .qspi_interrupt()
                .map(|request| (request, Source::Qspi)),
            self.qsm
                .sci_interrupt()
                .map(|request| (request, Source::Sci)),
            self.duart
                .interrupt()
                .map(|request| (request, Source::Duart)),
        ]
        .into_iter()
        .flatten()
        .map(|((level, vector), source)| Request {
            level,
            vector,
            source,
        })
        .reduce(|best, next| if next.level > best.level { next } else { best })
    }

    /// The GPS engine's 1 PPS at `at`, the start of one of its seconds
    /// (`gps::engine::Synthetic::on_pulse`): on the GPT's PAI, which the pulse
    /// accumulator counts (monitors.c, `gps_pps_check`), and into the
    /// FPGA's P82, where it starts the interval count and a full sync
    /// restarts the divider on it (docs/firmware/counter.md, "What the
    /// netlist shows"), the restart's internal 1 PPS reaching input
    /// capture 1 as the FPGA's next event.
    pub(crate) fn gps_pps(&mut self, at: Cycles) {
        self.gpt.pulse_accumulator_rising();
        let QspiDevices { fpga, hex_dac, .. } = &mut self.qspi;
        fpga.gps_pps(at, &self.ocxo, Trims::of(hex_dac));
    }

    /// The CPU took `request`.
    pub(crate) fn acknowledge(&mut self, request: Request) {
        if request.source == Source::Periodic {
            self.sim.acknowledge();
        }
    }

    /// A reset the board asks for, taken.
    pub(crate) fn take_reset(&mut self) -> Option<Reset> {
        self.reset.take()
    }

    /// The board's devices reset, as the CPU's reset line does, for
    /// `cause`; the EEPROM and the lines keep what they hold.
    pub(crate) fn reset(&mut self, cause: Reset) {
        // the watchdog's MSTRST goes to every module (MC68331UM 4.6.2),
        // as mc683xx's chip resets them all
        self.sim = Sim::new(cause.status(), self.now);
        self.sim_pins_changed(self.now);
        self.gpt = Gpt::new(self.now);
        self.qsm.reset();
        // no QSPI device is on the CPU's reset line, as on mc683xx: the
        // ADC's readings are the board's, the front panel is a board of
        // its own, held in reset by the latch, the FPGA's RESET is port
        // E's PROGRAM, so that it keeps its configuration and its 1 PPS's
        // phase, which the warm start takes as kept, and the DACs keep
        // their codes
    }

    /// Port E's and port F's pins may have changed at `now`: the FPGA's
    /// configuration lines and the 58503A's alarm output follow them.
    fn sim_pins_changed(&mut self, now: Cycles) {
        self.qspi.fpga.set_lines(self.fpga_lines(), now, &self.ocxo);
        // a port F pin not driven reads high, the output off
        let pins = self.sim.port_f() | !self.sim.port_f_directions();
        if let Some(output) = &mut self.alarm_output {
            output.write_reporting(now, pins | !PORTF_ALARM);
        }
    }

    /// The FPGA's configuration lines as ports E and F drive them: a
    /// line not driven is pulled up, as mc683xx's undriven pins read.
    fn fpga_lines(&self) -> Lines {
        let port_e = self.sim.port_e() | !self.sim.port_e_directions();
        let port_f = self.sim.port_f();
        let driven = self.sim.port_f_directions();
        Lines {
            program: port_e & sim::PORTE_FPGA_PROGRAM != 0,
            done_held_low: driven & sim::PORTF_FPGA_DONE != 0 && port_f & sim::PORTF_FPGA_DONE == 0,
            readback: driven & sim::PORTF_FPGA_READBACK_FIRST == 0
                || port_f & sim::PORTF_FPGA_READBACK_FIRST != 0,
        }
    }

    /// Whether the QSPI runs a queue whose first entry selects the
    /// front panel.
    fn qspi_to_front_panel(&self) -> bool {
        self.qsm.queue_first_select() == Some(SELECT_FRONT_PANEL)
    }

    /// Reports an access no model answers, once an address.
    fn unmodeled(&mut self, at: u32, write: Option<u8>) {
        if self.reported.insert(at) {
            match write {
                Some(value) => {
                    trace::note(format_args!("unmodeled write of {value:#04x} at {at:#08x}"))
                }
                None => trace::note(format_args!("unmodeled read at {at:#08x}")),
            }
        }
    }

    /// A byte at `at`, read at `access`, the access's own time.
    fn read_byte(&mut self, at: u32, access: Cycles) -> u8 {
        let now = self.now;
        match at {
            _ if (DUART..DUART + duart::SIZE).contains(&at) => {
                select::read(&mut self.duart, Select::CS8, at, now)
            }
            _ if at == CS5 + latch::OFFSET => select::read(&mut self.latch, Select::CS5, at, now),
            _ if at == CS5 + switch::OFFSET => select::read(&mut self.switch, Select::CS5, at, now),
            _ if (CS5..CS5 + fpga::SIZE).contains(&at) => {
                self.qspi
                    .fpga
                    .read(at - CS5, access.max(now), &self.ocxo, None)
            }
            _ if (EEPROM..EEPROM + eeprom::SIZE).contains(&at) => {
                select::read(&mut self.eeprom, Select::CS9, at, access.max(now))
            }
            _ if (GPT..GPT + gpt::SIZE).contains(&at) => {
                self.gpt.read(at - GPT, now, self.qspi.fpga.init())
            }
            _ if (SIM..SIM + sim::SIZE).contains(&at) => {
                let offset = at - SIM;
                let value = self.sim.read(offset);
                if Sim::is_port_f(offset) {
                    // DONE is an input unless port F drives it
                    let driven = self.sim.port_f_directions() & sim::PORTF_FPGA_DONE != 0;
                    let pin = if driven {
                        value & sim::PORTF_FPGA_DONE
                    } else if self.qspi.fpga.done() {
                        sim::PORTF_FPGA_DONE
                    } else {
                        0
                    };
                    value & !sim::PORTF_FPGA_DONE | pin
                } else {
                    value
                }
            }
            _ if (QSM..QSM + qsm::SIZE).contains(&at) => self.qsm.read(at - QSM),
            _ => {
                self.unmodeled(at, None);
                NOTHING
            }
        }
    }

    /// A byte written at `at` at `access`, the access's own time.
    fn write_byte(&mut self, at: u32, value: u8, access: Cycles) {
        let now = self.now;
        match at {
            _ if (DUART..DUART + duart::SIZE).contains(&at) => {
                select::write(&mut self.duart, Select::CS8, at, value, now);
            }
            _ if at == CS5 + latch::OFFSET => {
                select::write(&mut self.latch, Select::CS5, at, value, now);
                let in_flight = self.qspi_to_front_panel();
                let outputs = self.latch.outputs();
                if let Some(panel) = &mut self.qspi.front_panel {
                    panel.control(outputs, in_flight, now);
                }
            }
            _ if at == CS5 + strobe::OFFSET => {
                select::write(&mut self.strobe, Select::CS5, at, value, now);
                if let Some(strobed) = self.strobe.take() {
                    self.ocxo.set_efc(self.qspi.efc_dac.word(), strobed);
                }
            }
            _ if (CS5..CS5 + fpga::SIZE).contains(&at) => {
                let QspiDevices { fpga, hex_dac, .. } = &mut self.qspi;
                fpga.write(
                    at - CS5,
                    value,
                    access.max(now),
                    &self.ocxo,
                    Trims::of(hex_dac),
                );
            }
            _ if (EEPROM..EEPROM + eeprom::SIZE).contains(&at) => {
                select::write(&mut self.eeprom, Select::CS9, at, value, access.max(now));
            }
            _ if (GPT..GPT + gpt::SIZE).contains(&at) => self.gpt.write(at - GPT, value),
            _ if (SIM..SIM + sim::SIZE).contains(&at) => {
                let offset = at - SIM;
                self.sim.write(offset, value, now);
                self.sim_pins_changed(now);
            }
            _ if (QSM..QSM + qsm::SIZE).contains(&at) => self.qsm.write(at - QSM, value, now),
            _ => self.unmodeled(at, Some(value)),
        }
    }
}

impl Bus for Board {
    fn read(&mut self, at: u32, size: u32, now: Cycles) -> u32 {
        (0..size).fold(0, |value, index| {
            value << 8 | u32::from(self.read_byte(at.wrapping_add(index), now))
        })
    }

    fn write(&mut self, at: u32, size: u32, value: u32, now: Cycles) {
        for index in 0..size {
            let shift = 8 * (size - 1 - index);
            self.write_byte(at.wrapping_add(index), (value >> shift) as u8, now);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Board;
    use super::Bus;
    use super::Request;
    use super::Reset;
    use super::Source;
    use super::eeprom::Eeprom;
    use super::front_panel::Key;
    use super::profile::Model;
    use super::qsm::Peripherals;
    use super::time::CPU_HZ;
    use super::time::Cycles;
    use crate::line::Line;

    fn board() -> Board {
        board_of(Model::Z3805A)
    }

    fn board_of(model: Model) -> Board {
        Board::new(
            model,
            Line::new("sci", false),
            Line::new("gps", false),
            Line::new("host", false),
            Eeprom::new(vec![0x12, 0x34], None),
        )
    }

    #[test]
    fn a_word_is_its_two_bytes_high_first() {
        let mut board = board();
        assert_eq!(board.read(0x40_0000, 2, Cycles(0)), 0x1234);
        // QILR and QIVR, as qsm_init writes them in one word
        board.write(0xff_fc04, 2, 0x2340, Cycles(0));
        assert_eq!(board.read(0xff_fc04, 1, Cycles(0)), 0x23);
        assert_eq!(board.read(0xff_fc05, 1, Cycles(0)), 0x40);
    }

    #[test]
    fn the_periodic_interrupt_is_asked_for_and_acknowledged() {
        let mut board = board();
        board.write(0xff_fa22, 2, 0x0542, Cycles(0));
        board.write(0xff_fa24, 2, 0x0008, Cycles(0));
        assert_eq!(board.until_next_event(Cycles(1 << 20)), Cycles(16384));
        board.advance(Cycles(CPU_HZ / 1024));
        let request = board.interrupt().expect("a tick");
        assert_eq!(
            request,
            Request {
                level: 5,
                vector: 0x42,
                source: Source::Periodic
            }
        );
        board.acknowledge(request);
        assert_eq!(board.interrupt(), None);
    }

    #[test]
    fn the_highest_level_wins() {
        let mut board = board();
        board.write(0xff_fa22, 2, 0x0542, Cycles(0));
        board.write(0xff_fa24, 2, 0x0008, Cycles(0));
        // the DUART's TxRDYB, at level 2, with channel B enabled
        board.write(0x20_0015, 1, 0x04, Cycles(0));
        board.write(0x20_000b, 1, 0x10, Cycles(0));
        assert_eq!(board.interrupt().map(|request| request.level), Some(2));
        board.advance(Cycles(CPU_HZ / 1024));
        assert_eq!(board.interrupt().map(|request| request.level), Some(5));
    }

    #[test]
    fn port_f_shows_done_once_the_fpga_is_configured() {
        let mut board = board();
        // PROGRAM low, then high, as fpga_program pulses it
        board.write(0xff_fa11, 1, 0x00, Cycles(0));
        board.write(0xff_fa11, 1, 0x40, Cycles(0));
        assert_eq!(board.read(0xff_fa19, 1, Cycles(0)) & 0x08, 0);
        assert_eq!(
            board.read(0xff_f90c, 1, Cycles(0)) & 0x08,
            0x08,
            "INIT high"
        );
        // a bitstream's header with a length count of 62 bits, done at
        // 64 with the start-up's clocks
        for word in [0xff20, 0x0003, 0xefff, 0xffff] {
            board.qspi.fpga.exchange(word, 16);
        }
        assert_eq!(board.read(0xff_fa19, 1, Cycles(0)) & 0x08, 0x08);
        // driven low by port F's direction, as fpga_program holds it
        board.write(0xff_fa1d, 1, 0x08, Cycles(0));
        assert_eq!(board.read(0xff_fa19, 1, Cycles(0)) & 0x08, 0);
    }

    #[test]
    fn the_latch_reads_back_as_written() {
        let mut board = board();
        board.write(0x30_0000, 1, 0x31, Cycles(0));
        assert_eq!(board.read(0x30_0000, 1, Cycles(0)), 0x31);
        assert_eq!(
            board.read(0x30_2000, 1, Cycles(0)),
            0xff,
            "the switch byte beside it"
        );
    }

    /// A front-panel command on `board` as `0x53736` and `0x53796` send
    /// it: the latch's bit 7 high and bit 6 low, the byte through QSPI
    /// device 0, bit 6 high again; the byte shifted back.
    fn panel_command(board: &mut Board, byte: u8) -> u16 {
        board.write(0x30_0000, 1, 0x80, Cycles(0));
        let reply = board.qspi.exchange(0, 0, u16::from(byte), 8);
        board.write(0x30_0000, 1, 0xc0, Cycles(0));
        reply
    }

    #[test]
    fn the_58503a_s_front_panel_passes_its_self_test_and_gives_keys() {
        let mut board = board_of(Model::Hp58503A);
        for byte in [0x00, 0x08] {
            panel_command(&mut board, byte);
        }
        assert_eq!(panel_command(&mut board, 0x10), 0x55);
        assert_eq!(panel_command(&mut board, 0x00), 0xaa);
        assert!(board.press(Key::Status));
        panel_command(&mut board, 0x00);
        assert_eq!(panel_command(&mut board, 0x00), 6);
    }

    #[test]
    fn the_z3805a_has_no_front_panel() {
        let mut board = board();
        assert_eq!(panel_command(&mut board, 0x08), 0);
        assert_eq!(panel_command(&mut board, 0x10), 0);
        assert!(!board.press(Key::Status));
    }

    #[test]
    fn the_58503a_s_alarm_output_is_pf5_low() {
        let mut board = board_of(Model::Hp58503A);
        let lit = |board: &Board| {
            board
                .alarm_output
                .as_ref()
                .is_some_and(|output| !output.lit(Cycles(0)).is_empty())
        };
        // DDRF and PORTF0 as the reset code writes them: PF5 high
        board.write(0xff_fa1d, 1, 0xf7, Cycles(0));
        board.write(0xff_fa19, 1, 0xf7, Cycles(0));
        assert!(!lit(&board));
        // the alarm on, as 0x40828 clears bit 5
        board.write(0xff_fa19, 1, 0xd7, Cycles(0));
        assert!(lit(&board));
        // a reset makes PF5 an input again, read high: the alarm off
        board.reset(Reset::Watchdog);
        assert!(!lit(&board), "off from the reset");
    }

    /// A word on the EFC's DAC moves the oscillator only with its
    /// strobe, as `efc_write` sends it and `qspi_efc_test` does not.
    #[test]
    fn the_efc_strobe_loads_the_dacs_word_into_the_oscillator() {
        let mut board = board();
        board.qspi.efc_dac.exchange(0x8000 + 1000);
        assert_eq!(board.ocxo.frequency(Cycles(0)), 0.0, "no strobe yet");
        board.write(0x30_6000, 1, 0, Cycles(0));
        let frequency = board.ocxo.frequency(Cycles(0));
        assert!((frequency - 1e-8).abs() < 1e-20, "{frequency}");
        // the self-test's pattern, with no strobe, leaves it
        board.qspi.efc_dac.exchange(0x5555);
        assert_eq!(board.ocxo.frequency(Cycles(0)), frequency);
    }

    #[test]
    fn the_watchdog_asks_for_a_reset() {
        let mut board = board();
        board.write(0xff_fa21, 1, 0xcc, Cycles(0));
        board.advance(Cycles(8 * CPU_HZ));
        assert!(board.take_reset().is_some());
    }

    /// The watchdog's reset clears the QSM's SCI and QSPI setup, as
    /// MSTRST does (MC68331UM 4.6.2), the firmware's start-up setting
    /// them again, and keeps the QSPI RAM, which has no reset value
    /// (6.3.2).
    #[test]
    fn the_watchdogs_reset_resets_the_qsm() {
        let mut board = board();
        // QILR and QIVR, as qsm_init writes them; SCCR1's TE, RE and
        // RIE, as sci_init; SPCR1's SPE
        let registers = [
            (0xff_fc04, 2, 0x2340),
            (0xff_fc0b, 1, 0x2c),
            (0xff_fc1a, 1, 0x80),
        ];
        board.write(0xff_fa21, 1, 0xcc, Cycles(0));
        let fired = Cycles(8 * CPU_HZ);
        board.advance(fired);
        let reset = board.take_reset().expect("the watchdog's");
        // set up as the reset is taken, before the queue can finish
        for (at, size, value) in registers {
            board.write(at, size, value, fired);
        }
        let read = |board: &mut Board| registers.map(|(at, size, _)| board.read(at, size, fired));
        assert_eq!(read(&mut board), registers.map(|(_, _, value)| value));
        // a word of each of the QSPI RAM's receive, transmit and
        // command parts
        let ram = [0xff_fd00, 0xff_fd20, 0xff_fd40];
        for (word, at) in (1..).zip(ram) {
            board.write(at, 2, word, fired);
        }
        let read_ram = |board: &mut Board| ram.map(|at| board.read(at, 2, fired));
        board.reset(reset);
        // QIVR's $0F and SPCR1's $04, the reset's (MC68331UM D.4.3, D.4.11)
        assert_eq!(read(&mut board), [0x000f, 0, 0x04]);
        assert_eq!(read_ram(&mut board), [1, 2, 3]);
    }
}
