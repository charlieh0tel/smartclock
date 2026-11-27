//! Board time and the models' cycles, through the chip's system clock
//! (mc683xx-board's `TimeBase`): a model counts the chip's system
//! clocks since power-on, which its rates assume run at the reset
//! code's 16.777216 MHz (`time`), as they do once SYNCR is set.  A part
//! takes the base from each call's `Drive`, and `next_event` from its
//! argument.  Each part's `advance` moves its
//! `next_event` past `now`, as the board's run requires: a model's next
//! event is on its own cycles, `time` rounds it down and `cycles` back
//! up to the same cycle, so the model sees its event's cycle and moves
//! on (the FPGA's counter stop, which stays past, is no event:
//! `Z3805a::next_event`).
//!
//! A model the chip does not clock, the oscillator and its ovens, the
//! temperature and the GPS engine, counts the board's own cycles
//! instead (`board_cycles`): the same rate from power-on, whatever the
//! chip's clock does.  A reset puts the chip's synthesizer back to its
//! slower default until the reset code sets SYNCR, so on the chip's
//! clocks such a model would fall behind board time at each one: the
//! GPS engine's 1 PPS fell some 54 us behind the oscillator's at a
//! watchdog reset, which the warm start took for a time error past
//! its holdover limit (docs/emulator.md, "The mc683xx backend").

use mc683xx_board::peripheral::Drive;
#[cfg(test)]
use mc683xx_board::time::Moment;
use mc683xx_board::time::Time;
use mc683xx_board::time::TimeBase;

use mc683xx_cpu32::Frequency;

use super::time::CPU_HZ;
use super::time::Cycles;

/// `at` as the model counts it: the first system clock at or after it.
pub(crate) fn cycles(drive: &Drive<'_>, at: Time) -> Cycles {
    Cycles(drive.system_clock().cycle(at).clocks())
}

/// The model's `at` on board time: the last picosecond at or before it.
pub(crate) fn time(drive: &Drive<'_>, at: Cycles) -> Time {
    drive.system_clock().time(mc683xx_cycle(at))
}

/// The board's own clock: its cycles at the reset code's rate from
/// power-on.
const BOARD: TimeBase = TimeBase::steady(Frequency::from_hz(CPU_HZ));

/// `at` as a model the chip does not clock counts it: the first of the
/// board's own cycles at or after it.
pub(crate) fn board_cycles(at: Time) -> Cycles {
    Cycles(BOARD.cycle(at).clocks())
}

/// Such a model's `at` on board time: the last picosecond at or before
/// it.
pub(crate) fn board_time(at: Cycles) -> Time {
    BOARD.time(mc683xx_cycle(at))
}

/// The model's cycles as the chip counts them: the same.
pub(crate) const fn mc683xx_cycle(at: Cycles) -> mc683xx_cpu32::Cycle {
    mc683xx_cpu32::Cycle::from_clocks(at.0)
}

/// A drive for a test's call at `now`, the system clock at 16.777216
/// MHz from power-on.
#[cfg(test)]
pub(crate) fn test_drive(now: Time) -> Drive<'static> {
    Drive::new(test_moment(now))
}

/// A test's call's moment at `now`, the system clock at 16.777216 MHz
/// from power-on.
#[cfg(test)]
pub(crate) fn test_moment(now: Time) -> Moment {
    Moment::new(now, test_base())
}

/// The QSPI's serial clock out of reset, for a test's transfers: CPOL
/// 0, CPHA 1, SPBR 4 (QSMRM 4.3.1, SPCR0).
#[cfg(test)]
pub(crate) const fn test_qspi_clock() -> mc683xx_modules::peripheral::QspiClock {
    mc683xx_modules::peripheral::QspiClock::at_reset(mc683xx_cpu32::Frequency::from_hz(
        super::time::CPU_HZ,
    ))
}

/// The QSPI's serial clock in SPI mode `number` (`Mode::try_from`),
/// otherwise as out of reset, for a test's transfers.
#[cfg(test)]
pub(crate) fn test_qspi_clock_in(number: u8) -> mc683xx_modules::peripheral::QspiClock {
    let mode = mc683xx_board::spi::Mode::try_from(number).expect("an SPI mode");
    let mut clock = test_qspi_clock();
    clock.polarity = mode.polarity;
    clock.phase = mode.phase;
    clock
}

/// The base a test's parts are told.
#[cfg(test)]
pub(crate) fn test_base() -> TimeBase {
    BOARD
}
