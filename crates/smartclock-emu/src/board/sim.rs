//! The MC68331's system integration module at `0xfffa00`
//! (MC68331UM, section 4 and D.3; `third_party/firmware/include/mc68331.h`): the
//! clock synthesizer's lock, the reset status, ports E and F, the
//! software watchdog and the periodic interrupt timer.  The chip
//! selects are kept as written and not enforced: the memory map is the
//! one the reset code sets them to (docs/firmware/restart.md, "The
//! chip selects").

use super::time::CPU_PER_EXTAL;
use super::time::Cycles;

/// The SIM's register block, from `0xfffa00`.
pub(crate) const SIZE: u32 = 0x80;

/// Register offsets in the block (Table D-3).
const SYNCR_LOW: u32 = 0x05;
const RSR: u32 = 0x07;
const PORTE0: u32 = 0x11;
const PORTE1: u32 = 0x13;
const DDRE: u32 = 0x15;
const PORTF0: u32 = 0x19;
const PORTF1: u32 = 0x1b;
const DDRF: u32 = 0x1d;
const SYPCR: u32 = 0x21;
const PICR_HIGH: u32 = 0x22;
const PICR_LOW: u32 = 0x23;
const PITR_HIGH: u32 = 0x24;
const PITR_LOW: u32 = 0x25;
const SWSR: u32 = 0x27;

/// SYNCR's SLOCK: the synthesizer has locked, which the reset code may
/// wait for after it sets the frequency (D.3.3).  The model's is always
/// locked.
const SLOCK: u8 = 0x08;

/// SYPCR's bits (D.3.12): the watchdog enabled, its prescaler and its
/// time-out select.
const SYPCR_SWE: u8 = 0x80;
const SYPCR_SWP: u8 = 0x40;
const SYPCR_SWT_SHIFT: u8 = 4;
const SYPCR_SWT_MASK: u8 = 0x03;
/// SYPCR as the reset leaves it: the watchdog on, unprescaled, the
/// shortest time-out (MODCLK high; Table D-7).
const SYPCR_RESET: u8 = SYPCR_SWE;
/// The watchdog's time-out, as a power of two of reference cycles,
/// for SWT 0 without SWP; each step of SWT adds two, SWP nine (Table 4-4).
const WATCHDOG_LEAST_POWER: u32 = 9;
const WATCHDOG_SWT_STEP: u32 = 2;
const WATCHDOG_SWP_POWER: u32 = 9;

/// The two values written to SWSR in turn that service the watchdog
/// (D.3.15).
const SWSR_FIRST: u8 = 0x55;
const SWSR_SECOND: u8 = 0xaa;

/// PICR's level field, bits 10 to 8, and PITR's prescaler and modulus
/// (D.3.13, D.3.14).
const PIRQL_MASK: u8 = 0x07;
const PITR_PTP: u8 = 0x01;
const PIT_PRESCALE: u64 = 512;
/// Reference cycles a count of PITM, before the prescaler (4.7.3).
const PIT_CYCLES_A_COUNT: u64 = 4;

/// The reset status register's bits (D.3.4).
pub(crate) const RSR_EXTERNAL: u8 = 0x80;
pub(crate) const RSR_POWER_UP: u8 = 0x40;
pub(crate) const RSR_SOFTWARE_WATCHDOG: u8 = 0x20;

/// Port F's bit 3, the FPGA's DONE line (fpga.c, `FPGA_DONE`).
pub(crate) const PORTF_FPGA_DONE: u8 = 0x08;
/// Port F's bit 6, the FPGA's first chip's readback line (fpga.c,
/// `FPGA_READBACK_FIRST`).
pub(crate) const PORTF_FPGA_READBACK_FIRST: u8 = 0x40;
/// Port E's bit 6, the FPGA's PROGRAM line (fpga.h).
pub(crate) const PORTE_FPGA_PROGRAM: u8 = 0x40;

/// The SIM's state: its registers as written, the reset's cause, and
/// the watchdog's and the periodic timer's clocks.
#[derive(Debug)]
pub(crate) struct Sim {
    /// The register block as last written.
    registers: [u8; SIZE as usize],
    /// The reset status register, as the last reset left it.
    reset_status: u8,
    /// The first of the two service values has been written.
    serviced_first: bool,
    /// When the watchdog times out, while it is on.
    watchdog_deadline: Option<Cycles>,
    /// When the periodic timer next times out, while it runs.
    next_tick: Option<Cycles>,
    /// A periodic interrupt not yet taken (4.7.3: the request is
    /// cleared by its acknowledge).
    tick_pending: bool,
}

impl Sim {
    /// The SIM after a reset for `reset_status`, at `now`.
    pub(crate) fn new(reset_status: u8, now: Cycles) -> Self {
        let mut sim = Self {
            registers: [0; SIZE as usize],
            reset_status,
            serviced_first: false,
            watchdog_deadline: None,
            next_tick: None,
            tick_pending: false,
        };
        sim.registers[SYPCR as usize] = SYPCR_RESET;
        sim.service(now);
        sim
    }

    /// A register byte.
    pub(crate) fn read(&self, offset: u32) -> u8 {
        let stored = self.registers[offset as usize];
        match offset {
            SYNCR_LOW => stored | SLOCK,
            RSR => self.reset_status,
            PORTE1 => self.registers[PORTE0 as usize],
            PORTF1 => self.registers[PORTF0 as usize],
            _ => stored,
        }
    }

    /// A register byte written at `now`.
    pub(crate) fn write(&mut self, offset: u32, value: u8, now: Cycles) {
        match offset {
            RSR => {}
            PORTE1 => self.registers[PORTE0 as usize] = value,
            PORTF1 => self.registers[PORTF0 as usize] = value,
            SWSR => {
                if value == SWSR_SECOND && self.serviced_first {
                    self.service(now);
                }
                self.serviced_first = value == SWSR_FIRST;
            }
            SYPCR => {
                self.registers[SYPCR as usize] = value;
                self.service(now);
            }
            PICR_HIGH | PICR_LOW | PITR_HIGH | PITR_LOW => {
                self.registers[offset as usize] = value;
                self.next_tick = self.tick_period().map(|period| now + period);
            }
            _ => self.registers[offset as usize] = value,
        }
    }

    /// Port E's data, as written.
    pub(crate) fn port_e(&self) -> u8 {
        self.registers[PORTE0 as usize]
    }

    /// Port E's data direction register.
    pub(crate) fn port_e_directions(&self) -> u8 {
        self.registers[DDRE as usize]
    }

    /// Port F's data, as written.
    pub(crate) fn port_f(&self) -> u8 {
        self.registers[PORTF0 as usize]
    }

    /// Port F's data direction register.
    pub(crate) fn port_f_directions(&self) -> u8 {
        self.registers[DDRF as usize]
    }

    /// Whether `offset` is port F's data register, which shows its
    /// input pins.
    pub(crate) fn is_port_f(offset: u32) -> bool {
        offset == PORTF0 || offset == PORTF1
    }

    /// Restarts the watchdog's time-out.
    fn service(&mut self, now: Cycles) {
        let sypcr = self.registers[SYPCR as usize];
        self.watchdog_deadline = (sypcr & SYPCR_SWE != 0).then(|| {
            let power = WATCHDOG_LEAST_POWER
                + WATCHDOG_SWT_STEP * u32::from(sypcr >> SYPCR_SWT_SHIFT & SYPCR_SWT_MASK)
                + if sypcr & SYPCR_SWP != 0 {
                    WATCHDOG_SWP_POWER
                } else {
                    0
                };
            now + Cycles((1 << power) * CPU_PER_EXTAL)
        });
    }

    /// The periodic timer's period, if it runs (PITM nonzero).
    fn tick_period(&self) -> Option<Cycles> {
        let modulus = u64::from(self.registers[PITR_LOW as usize]);
        let prescale = if self.registers[PITR_HIGH as usize] & PITR_PTP != 0 {
            PIT_PRESCALE
        } else {
            1
        };
        (modulus != 0).then(|| Cycles(modulus * PIT_CYCLES_A_COUNT * prescale * CPU_PER_EXTAL))
    }

    /// The periodic interrupt's level, zero for none.
    fn tick_level(&self) -> u8 {
        self.registers[PICR_HIGH as usize] & PIRQL_MASK
    }

    /// The periodic interrupt's level and vector, while one is pending.
    pub(crate) fn interrupt(&self) -> Option<(u8, u8)> {
        (self.tick_pending && self.tick_level() != 0)
            .then(|| (self.tick_level(), self.registers[PICR_LOW as usize]))
    }

    /// The CPU took the periodic interrupt.
    pub(crate) fn acknowledge(&mut self) {
        self.tick_pending = false;
    }

    /// Time moves on to `now`; true if the watchdog timed out.
    pub(crate) fn advance(&mut self, now: Cycles) -> bool {
        if let (Some(next), Some(period)) = (self.next_tick, self.tick_period())
            && next <= now
        {
            let missed = (now.0 - next.0) / period.0;
            self.next_tick = Some(Cycles(next.0 + (missed + 1) * period.0));
            self.tick_pending = self.tick_level() != 0;
        }
        self.watchdog_deadline
            .is_some_and(|deadline| deadline <= now)
    }

    /// When the SIM next changes by itself.
    pub(crate) fn next_event(&self) -> Option<Cycles> {
        [self.next_tick, self.watchdog_deadline]
            .into_iter()
            .flatten()
            .min()
    }
}

#[cfg(test)]
mod tests {
    use super::super::time::CPU_HZ;
    use super::super::time::Cycles;
    use super::RSR_POWER_UP;
    use super::Sim;

    /// The reset code's PICR and PITR: level 5, vector 66, 1024 ticks a
    /// second (startup.h; restart.md, "The watchdog").
    fn ticking() -> Sim {
        let mut sim = Sim::new(RSR_POWER_UP, Cycles(0));
        sim.write(0x22, 0x05, Cycles(0));
        sim.write(0x23, 0x42, Cycles(0));
        sim.write(0x24, 0x00, Cycles(0));
        sim.write(0x25, 0x08, Cycles(0));
        sim
    }

    #[test]
    fn the_periodic_timer_ticks_1024_times_a_second_at_its_level_and_vector() {
        let mut sim = ticking();
        assert_eq!(sim.interrupt(), None);
        sim.advance(Cycles(CPU_HZ / 1024 - 1));
        assert_eq!(sim.interrupt(), None);
        sim.advance(Cycles(CPU_HZ / 1024));
        assert_eq!(sim.interrupt(), Some((5, 0x42)));
        sim.acknowledge();
        assert_eq!(sim.interrupt(), None);
        assert_eq!(sim.next_event(), Some(Cycles(2 * CPU_HZ / 1024)));
    }

    #[test]
    fn the_watchdog_times_out_eight_seconds_after_its_last_service() {
        let mut sim = Sim::new(RSR_POWER_UP, Cycles(0));
        sim.write(0x21, 0xcc, Cycles(0));
        assert!(!sim.advance(Cycles(8 * CPU_HZ - 1)));
        sim.write(0x27, 0x55, Cycles(8 * CPU_HZ - 1));
        sim.write(0x27, 0xaa, Cycles(8 * CPU_HZ - 1));
        assert!(!sim.advance(Cycles(15 * CPU_HZ)));
        assert!(sim.advance(Cycles(16 * CPU_HZ)));
    }

    #[test]
    fn the_watchdog_wants_its_two_values_in_order() {
        let mut sim = Sim::new(RSR_POWER_UP, Cycles(0));
        sim.write(0x21, 0xcc, Cycles(0));
        sim.write(0x27, 0xaa, Cycles(4 * CPU_HZ));
        sim.write(0x27, 0x55, Cycles(4 * CPU_HZ));
        assert!(sim.advance(Cycles(8 * CPU_HZ)));
    }

    #[test]
    fn the_synthesizer_reads_locked_and_rsr_the_reset() {
        let sim = Sim::new(RSR_POWER_UP, Cycles(0));
        assert_eq!(sim.read(0x05) & 0x08, 0x08);
        assert_eq!(sim.read(0x07), RSR_POWER_UP);
    }
}
