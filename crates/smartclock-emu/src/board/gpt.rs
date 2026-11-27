//! The MC68331's general-purpose timer at `0xfff900` (MC68331UM,
//! section 7 and D.2): its free-running counter, which counts the
//! system clock through its prescaler; its four output compares, whose
//! flags set as the counter passes them and interrupt through ICR's
//! level and vectors; and the pulse accumulator's control register,
//! whose PCLKS bit shows the FPGA's INIT line (fpga.c, `FPGA_INIT`).
//!
//! Input capture 1 takes the FPGA's internal 1 PPS (pps.c: its
//! interrupt, vector 81, ticks the clock): each pulse latches the
//! counter in TIC1 and sets IC1F, when TCTL2's EDGE1 captures a rising
//! edge, as `gpt_init` sets it (startup.h).  The pulse accumulator
//! counts the GPS engine's 1 PPS on PAI, as `gps_pps_check`
//! (monitors.c) sets it, PAEN and PEDGE: a rising edge counts in PACNT
//! (D.2.7); with no engine it stays at zero.  The other captures see
//! nothing.  The output compares are what the time code and the
//! ES message wait on (timecode.c, `timecode_schedule`): a compare a
//! delay after the last capture, its interrupt, vector 86 or 87, sends
//! the message.

use super::time::Cycles;

/// The GPT's register block, from `0xfff900`.
pub(crate) const SIZE: u32 = 0x30;

/// Register offsets (Table D-2).
const ICR_HIGH: u32 = 0x04;
const ICR_LOW: u32 = 0x05;
const TCNT_HIGH: u32 = 0x0a;
const TCNT_LOW: u32 = 0x0b;
const PACTL: u32 = 0x0c;
const PACNT: u32 = 0x0d;
const TIC1_HIGH: u32 = 0x0e;
const TIC1_LOW: u32 = 0x0f;
const TOC_FIRST: u32 = 0x14;
const TCTL2: u32 = 0x1f;
const TMSK1: u32 = 0x20;
const TMSK2: u32 = 0x21;
const TFLG1: u32 = 0x22;

/// ICR's interrupt level, bits 10 to 8, and its vector base, bits 7 to
/// 4 (D.2.3).
const ICR_LEVEL_MASK: u8 = 0x07;
const ICR_VECTOR_BASE_MASK: u8 = 0xf0;
/// PACTL's PCLKS, the PCLK pin's state (D.2.7).
const PACTL_PCLKS: u8 = 0x08;
/// PACTL's PAEN, PAMOD and PEDGE (D.2.7): enabled, counting events
/// rather than gating time, on PAI's rising edges.
const PACTL_PAEN: u8 = 0x40;
const PACTL_PAMOD: u8 = 0x20;
const PACTL_PEDGE: u8 = 0x10;
/// TMSK2's prescaler select, CPR (D.2.12): the system clock over 4
/// shifted left by it, for its first seven values; the eighth is PCLK,
/// which the firmware does not select.
const TMSK2_CPR_MASK: u8 = 0x07;
const PRESCALER_SHIFT_LEAST: u8 = 2;
/// The output compares, 1 to 4: each one's flag in TFLG1 and enable in
/// TMSK1, OC1F 0x08 to OC4F 0x40 (D.2.12, D.2.13), and its vector's low
/// nibble, 4 to 7 (Table 7-2).
const COMPARES: usize = 4;
const COMPARE_FLAG_FIRST: u8 = 0x08;
const COMPARE_VECTOR_FIRST: u8 = 4;
/// Input capture 1: TCTL2's EDGE1, whose low bit captures a rising
/// edge (D.2.11), its flag in TFLG1 and enable in TMSK1, IC1F and IC1I
/// (D.2.12, D.2.13), and its vector's low nibble (Table 7-2).
const EDGE1_RISING: u8 = 0x01;
const CAPTURE_1_FLAG: u8 = 0x01;
const CAPTURE_1_VECTOR: u8 = 1;
/// The counter's span.
const COUNTER_SPAN: u64 = 1 << 16;

/// The GPT's state.
#[derive(Debug)]
pub(crate) struct Gpt {
    /// The register block as written; TFLG1 holds the flags.
    registers: [u8; SIZE as usize],
    /// When time last moved on, for the compares it passed.
    last: Cycles,
}

impl Gpt {
    /// The GPT after a reset at `now`.
    pub(crate) fn new(now: Cycles) -> Self {
        Self {
            registers: [0; SIZE as usize],
            last: now,
        }
    }

    /// A register byte at `now`, with the PCLK pin at `pclk`.
    pub(crate) fn read(&self, offset: u32, now: Cycles, pclk: bool) -> u8 {
        // the counter is sixteen bits and wraps
        let [high, low] = (self.count(now) as u16).to_be_bytes();
        match offset {
            TCNT_HIGH => high,
            TCNT_LOW => low,
            PACTL => {
                self.registers[PACTL as usize] & !PACTL_PCLKS | if pclk { PACTL_PCLKS } else { 0 }
            }
            _ => self.registers[offset as usize],
        }
    }

    /// A register byte written; a flag clears where TFLG1 is written
    /// zero (D.2.13).
    pub(crate) fn write(&mut self, offset: u32, value: u8) {
        match offset {
            TFLG1 => self.registers[TFLG1 as usize] &= value,
            _ => self.registers[offset as usize] = value,
        }
    }

    /// The prescaler's shift: the counter counts the clock shifted
    /// right by it.
    fn shift(&self) -> u8 {
        let select = self.registers[TMSK2 as usize] & TMSK2_CPR_MASK;
        PRESCALER_SHIFT_LEAST + select.min(TMSK2_CPR_MASK - 1)
    }

    /// The counter at `now`, unwrapped.
    fn count(&self, now: Cycles) -> u64 {
        now.0 >> self.shift()
    }

    /// Output compare `index`'s register, 0 for OC1.
    fn compare(&self, index: usize) -> u64 {
        let at = TOC_FIRST as usize + 2 * index;
        u64::from(u16::from_be_bytes([
            self.registers[at],
            self.registers[at + 1],
        ]))
    }

    /// The unwrapped count, after `count`, at which the counter next
    /// equals compare `index`.
    fn next_match(&self, count: u64, index: usize) -> u64 {
        let ahead =
            (self.compare(index) + COUNTER_SPAN - (count + 1) % COUNTER_SPAN) % COUNTER_SPAN;
        count + 1 + ahead
    }

    /// Time moves on to `now`: a compare the counter passed sets its
    /// flag.
    pub(crate) fn advance(&mut self, now: Cycles) {
        let from = self.count(self.last);
        let to = self.count(now);
        for index in 0..COMPARES {
            if self.next_match(from, index) <= to {
                self.registers[TFLG1 as usize] |= COMPARE_FLAG_FIRST << index;
            }
        }
        self.last = now;
    }

    /// A rising edge on input capture 1's pin at `at`.
    pub(crate) fn capture_1(&mut self, at: Cycles) {
        if self.registers[TCTL2 as usize] & EDGE1_RISING == 0 {
            return;
        }
        // the counter is sixteen bits and wraps
        let [high, low] = (self.count(at) as u16).to_be_bytes();
        self.registers[TIC1_HIGH as usize] = high;
        self.registers[TIC1_LOW as usize] = low;
        self.registers[TFLG1 as usize] |= CAPTURE_1_FLAG;
    }

    /// A rising edge on PAI: counted while the accumulator counts rising
    /// events.  Its overflow flag is not modeled.
    pub(crate) fn pulse_accumulator_rising(&mut self) {
        let pactl = self.registers[PACTL as usize];
        if pactl & (PACTL_PAEN | PACTL_PAMOD | PACTL_PEDGE) == PACTL_PAEN | PACTL_PEDGE {
            let count = &mut self.registers[PACNT as usize];
            *count = count.wrapping_add(1);
        }
    }

    /// When an enabled compare's flag next sets.
    pub(crate) fn next_event(&self) -> Option<Cycles> {
        let count = self.count(self.last);
        (0..COMPARES)
            .filter(|index| self.registers[TMSK1 as usize] & COMPARE_FLAG_FIRST << index != 0)
            .map(|index| Cycles(self.next_match(count, index) << self.shift()))
            .min()
    }

    /// The interrupt, ICR's level and the vector of the first source
    /// both flagged and enabled, while one is: input capture 1, then
    /// the compares, in their vectors' order of priority (7.4.2).
    pub(crate) fn interrupt(&self) -> Option<(u8, u8)> {
        let pending = self.registers[TFLG1 as usize] & self.registers[TMSK1 as usize];
        let vector = if pending & CAPTURE_1_FLAG != 0 {
            CAPTURE_1_VECTOR
        } else {
            let index = (0..COMPARES).find(|index| pending & COMPARE_FLAG_FIRST << index != 0)?;
            // four compares fit the vector's nibble
            COMPARE_VECTOR_FIRST + index as u8
        };
        let level = self.registers[ICR_HIGH as usize] & ICR_LEVEL_MASK;
        let base = self.registers[ICR_LOW as usize] & ICR_VECTOR_BASE_MASK;
        (level != 0).then_some((level, base | vector))
    }
}

#[cfg(test)]
mod tests {
    use super::super::time::Cycles;
    use super::Gpt;

    /// The GPT as `gpt_init` leaves it (startup.h): level 6, vectors
    /// from 0x50, the counter at the clock over 32.
    fn gpt() -> Gpt {
        let mut gpt = Gpt::new(Cycles(0));
        gpt.write(0x04, 0x06);
        gpt.write(0x05, 0x50);
        gpt.write(0x21, 3);
        gpt
    }

    #[test]
    fn the_counter_counts_the_clock_over_its_prescaler() {
        let gpt = gpt();
        assert_eq!(gpt.read(0x0a, Cycles(32 * 0x1234), false), 0x12);
        assert_eq!(gpt.read(0x0b, Cycles(32 * 0x1234), false), 0x34);
    }

    #[test]
    fn pclks_shows_the_fpga_init_line() {
        let mut gpt = gpt();
        gpt.write(0x0c, 0x04);
        assert_eq!(gpt.read(0x0c, Cycles(0), true), 0x0c);
        assert_eq!(gpt.read(0x0c, Cycles(0), false), 0x04);
    }

    #[test]
    fn output_compare_3_interrupts_through_vector_86_when_passed() {
        let mut gpt = gpt();
        // TOC3 at 0x100, OC3's interrupt enabled, as timecode_schedule
        // sets them
        gpt.write(0x18, 0x01);
        gpt.write(0x19, 0x00);
        gpt.write(0x20, 0x20);
        assert_eq!(gpt.next_event(), Some(Cycles(32 * 0x100)));
        gpt.advance(Cycles(32 * 0xff));
        assert_eq!(gpt.interrupt(), None);
        gpt.advance(Cycles(32 * 0x100));
        assert_eq!(gpt.interrupt(), Some((6, 86)));
        // cleared as the handler clears it, writing the flag zero
        gpt.write(0x22, !0x20);
        assert_eq!(gpt.interrupt(), None);
        assert_eq!(gpt.next_event(), Some(Cycles(32 * 0x1_0100)));
    }

    #[test]
    fn the_internal_1_pps_is_captured_and_interrupts_through_vector_81() {
        let mut gpt = gpt();
        gpt.capture_1(Cycles(32 * 0x1234));
        assert_eq!(
            gpt.read(0x22, Cycles(0), false) & 0x01,
            0,
            "not before EDGE1"
        );
        // TCTL2 as gpt_init sets it, then IC1I as pps_init does
        gpt.write(0x1f, 0x05);
        gpt.write(0x20, 0x01);
        gpt.capture_1(Cycles(32 * 0x1234));
        assert_eq!(gpt.read(0x0e, Cycles(0), false), 0x12);
        assert_eq!(gpt.read(0x0f, Cycles(0), false), 0x34);
        assert_eq!(gpt.interrupt(), Some((6, 81)));
        gpt.write(0x22, !0x01);
        assert_eq!(gpt.interrupt(), None);
    }

    #[test]
    fn the_pulse_accumulator_counts_the_gps_1_pps_once_enabled() {
        let mut gpt = gpt();
        // PACTL as gpt_init leaves it, input capture 4 only
        gpt.write(0x0c, 0x04);
        gpt.pulse_accumulator_rising();
        assert_eq!(gpt.read(0x0d, Cycles(0), false), 0);
        // as gps_pps_check sets it, then clears the count
        gpt.write(0x0c, 0x50);
        gpt.write(0x0d, 0);
        gpt.pulse_accumulator_rising();
        gpt.pulse_accumulator_rising();
        assert_eq!(gpt.read(0x0d, Cycles(0), false), 2);
    }

    #[test]
    fn a_compare_flags_without_its_interrupt_enabled() {
        let mut gpt = gpt();
        gpt.write(0x1a, 0x00);
        gpt.write(0x1b, 0x10);
        gpt.advance(Cycles(32 * 0x20));
        assert_eq!(gpt.read(0x22, Cycles(0), false) & 0x40, 0x40, "OC4F");
        assert_eq!(gpt.interrupt(), None);
    }
}
