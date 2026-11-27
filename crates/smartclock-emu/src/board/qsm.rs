//! The MC68331's queued serial module at `0xfffc00` (MC68331UM,
//! section 6 and D.4; `third_party/firmware/include/mc68331.h`): the SCI, the
//! host port SCPI is spoken on, and the QSPI, the bus to the ADC, the
//! DACs and the FPGA (docs/firmware/structures.md, "The QSPI's
//! devices").
//!
//! The SCI moves a character a character time after it is written or
//! arrives, at the baud rate SCCR0 sets, so the firmware's interrupt
//! handlers see the flags in the order the part sets them.  The QSPI
//! runs its queue when SPCR1's SPE is set and finishes a short while
//! later, clearing SPE and setting SPIF, which is what `qspi_send`
//! polls and the end-of-queue interrupt reports (qspi.c).

use super::time::Cycles;
use crate::line::Line;

/// The QSM's register block and its RAM, from `0xfffc00`.
pub(crate) const SIZE: u32 = 0x150;

/// Register offsets (Table D-13).
const QILR: u32 = 0x04;
const QIVR: u32 = 0x05;
const SCCR0_HIGH: u32 = 0x08;
const SCCR0_LOW: u32 = 0x09;
const SCCR1_HIGH: u32 = 0x0a;
const SCCR1_LOW: u32 = 0x0b;
const SCSR_HIGH: u32 = 0x0c;
const SCSR_LOW: u32 = 0x0d;
const SCDR_LOW: u32 = 0x0f;
const SPCR0_HIGH: u32 = 0x18;
const SPCR0_LOW: u32 = 0x19;
const SPCR1_HIGH: u32 = 0x1a;
const SPCR1_LOW: u32 = 0x1b;
const SPCR2_HIGH: u32 = 0x1c;
const SPCR2_LOW: u32 = 0x1d;
const SPSR: u32 = 0x1f;
/// The QSPI's receive, transmit and command RAM (6.3.4).
const RECEIVE_RAM: u32 = 0x100;
const TRANSMIT_RAM: u32 = 0x120;
const COMMAND_RAM: u32 = 0x140;
const QUEUE_ENTRIES: usize = 16;

/// The registers' bytes a reset leaves other than 0 (MC68331UM D.4.3,
/// D.4.4, D.4.10, D.4.11): QIVR $0F, SCCR0 $0004, SPCR0 $0104 and SPCR1
/// $0404.
const RESET_VALUES: [(u32, u8); 6] = [
    (QIVR, 0x0f),
    (SCCR0_LOW, 0x04),
    (SPCR0_HIGH, 0x01),
    (SPCR0_LOW, 0x04),
    (SPCR1_HIGH, 0x04),
    (SPCR1_LOW, 0x04),
];

/// QILR's fields: the QSPI's level in bits 5 to 3, the SCI's in 2 to 0
/// (D.4.3).
const ILQSPI_SHIFT: u8 = 3;
const LEVEL_MASK: u8 = 0x07;
/// QIVR's low bit, which the QSM supplies: 0 for the SCI, 1 for the
/// QSPI (D.4.3).
const QSPI_VECTOR_BIT: u8 = 0x01;

/// SCCR0's baud divisor, SCBR, 13 bits (D.4.4): the rate is the system
/// clock over 32 times it (6.4.3.3).
const SCBR_HIGH_MASK: u8 = 0x1f;
const SCI_CLOCKS_A_BIT: u64 = 32;
/// SCCR1's bits (D.4.5): the high byte's M, eleven-bit frames; the low
/// byte's interrupt enables and the transmitter and receiver.
const SCCR1_M: u8 = 0x02;
const SCCR1_TIE: u8 = 0x80;
const SCCR1_TCIE: u8 = 0x40;
const SCCR1_RIE: u8 = 0x20;
const SCCR1_TE: u8 = 0x08;
const SCCR1_RE: u8 = 0x04;
/// SCSR's bits (D.4.6): TDRE in the high byte, TC, RDRF and OR in the
/// low.
const SCSR_TDRE: u8 = 0x01;
const SCSR_TC: u8 = 0x80;
const SCSR_RDRF: u8 = 0x40;
const SCSR_OR: u8 = 0x08;
/// Bits a frame has: a start bit, eight data bits and a stop bit, or
/// one more with M.
const FRAME_BITS: u64 = 10;
const FRAME_BITS_LONG: u64 = 11;

/// SPCR0's word length, bits 13 to 10, 0 meaning 16 (D.4.10).
const SPCR0_BITS_SHIFT: u8 = 2;
const SPCR0_BITS_MASK: u8 = 0x0f;
const WORD_BITS: u8 = 16;
/// SPCR1's SPE, the queue runs (D.4.11).
const SPCR1_SPE: u8 = 0x80;
/// SPCR2's end-of-queue interrupt enable, and its ENDQP and NEWQP
/// (D.4.12).
const SPCR2_SPIFIE: u8 = 0x80;
const QUEUE_POINTER_MASK: u8 = 0x0f;
/// SPSR's SPIF, the queue is done, and its completed-queue pointer
/// (D.4.13).
const SPSR_SPIF: u8 = 0x80;
/// A command RAM entry's BITSE, a word of SPCR0's length rather than
/// eight bits, and its chip selects (D.4.16).
const COMMAND_BITSE: u8 = 0x40;
const COMMAND_SELECT_MASK: u8 = 0x0f;
const BYTE_BITS: u8 = 8;
/// How long a queue takes, a fixed span: about the slowest of the
/// firmware's transfers, sixteen words at divider 9 (structures.md).
const QUEUE_TIME: Cycles = Cycles(16 * 16 * 2 * 9);

/// The devices on the QSPI, as the board decodes its chip selects.
pub(crate) trait Peripherals {
    /// Sends `word`, of `bits` bits, the queue's `entry`th from its
    /// start, to the device `select` names, and gives what it shifted
    /// back.
    fn exchange(&mut self, select: u8, entry: usize, word: u16, bits: u8) -> u16;
}

/// The SCI's transmitter: the data register and the shift register
/// behind it.
#[derive(Debug, Default)]
struct Transmitter {
    /// A character written to SCDR not yet moved to the shifter.
    holding: Option<u8>,
    /// The character being shifted out, and when it is done.
    shifting: Option<(u8, Cycles)>,
    /// TC: the last character has gone and none follows (set after
    /// reset).
    complete: bool,
}

/// The SCI's receiver.
#[derive(Debug, Default)]
struct Receiver {
    /// The character in SCDR, while RDRF is set.
    data: Option<u8>,
    /// A character came while one was waiting.
    overrun: bool,
    /// The earliest the next character can arrive: one character time
    /// after the last.
    next: Cycles,
}

/// The QSM's state.
#[derive(Debug)]
pub(crate) struct Qsm {
    /// The registers and RAM as written.
    registers: [u8; SIZE as usize],
    /// The SCI's transmitter.
    transmitter: Transmitter,
    /// The SCI's receiver.
    receiver: Receiver,
    /// When the running queue is done, while SPE is set.
    queue_done: Option<Cycles>,
    /// The host port's line.
    line: Line,
}

impl Qsm {
    /// The QSM after a reset, its SCI on `line`.
    pub(crate) fn new(line: Line) -> Self {
        let registers = std::array::from_fn(|offset| {
            RESET_VALUES
                .iter()
                .find(|(at, _)| *at as usize == offset)
                .map_or(0, |(_, value)| *value)
        });
        Self {
            registers,
            transmitter: Transmitter {
                complete: true,
                ..Transmitter::default()
            },
            receiver: Receiver::default(),
            queue_done: None,
            line,
        }
    }

    /// The QSM reset, as MSTRST resets it (MC68331UM 4.6.2), its SCI
    /// still on its line: its registers to their reset values, and the
    /// QSPI RAM kept.  Inferred: the manual calls the RAM static RAM
    /// (6.3.2) and gives it no reset value where it gives the
    /// registers theirs (D.4), and mc683xx's QSPI keeps it.
    pub(crate) fn reset(&mut self) {
        let ram = RECEIVE_RAM as usize..SIZE as usize;
        let kept = self.registers[ram.clone()].to_vec();
        *self = Self::new(self.line.clone());
        self.registers[ram].copy_from_slice(&kept);
    }

    /// A register or RAM byte; reading SCDR takes its character.
    pub(crate) fn read(&mut self, offset: u32) -> u8 {
        match offset {
            SCSR_HIGH => {
                if self.transmitter.holding.is_none() {
                    SCSR_TDRE
                } else {
                    0
                }
            }
            SCSR_LOW => {
                (if self.transmitter.complete {
                    SCSR_TC
                } else {
                    0
                }) | (if self.receiver.data.is_some() {
                    SCSR_RDRF
                } else {
                    0
                }) | (if self.receiver.overrun { SCSR_OR } else { 0 })
            }
            SCDR_LOW => {
                self.receiver.overrun = false;
                self.receiver.data.take().unwrap_or(0)
            }
            _ => self.registers.get(offset as usize).copied().unwrap_or(0),
        }
    }

    /// A register or RAM byte written at `now`.
    pub(crate) fn write(&mut self, offset: u32, value: u8, now: Cycles) {
        match offset {
            SCDR_LOW => {
                if self.sccr1() & SCCR1_TE != 0 {
                    self.transmitter.holding = Some(value);
                    self.transmitter.complete = false;
                    self.shift(now);
                }
            }
            SPCR1_HIGH => {
                self.registers[offset as usize] = value;
                if value & SPCR1_SPE != 0 && self.queue_done.is_none() {
                    self.queue_done = Some(now + QUEUE_TIME);
                }
            }
            SPSR => {
                // SPIF and the other flags clear when written zero; the
                // pointer is the QSPI's
                let kept = self.registers[SPSR as usize] & value & SPSR_SPIF;
                self.registers[SPSR as usize] =
                    kept | self.registers[SPSR as usize] & QUEUE_POINTER_MASK;
            }
            _ => {
                if let Some(slot) = self.registers.get_mut(offset as usize) {
                    *slot = value;
                }
            }
        }
    }

    fn sccr1(&self) -> u8 {
        self.registers[SCCR1_LOW as usize]
    }

    /// A character time at SCCR0's rate and SCCR1's frame.
    fn character_time(&self) -> Cycles {
        let divisor = u64::from(self.registers[SCCR0_HIGH as usize] & SCBR_HIGH_MASK) << 8
            | u64::from(self.registers[SCCR0_LOW as usize]);
        let bits = if self.registers[SCCR1_HIGH as usize] & SCCR1_M != 0 {
            FRAME_BITS_LONG
        } else {
            FRAME_BITS
        };
        Cycles(bits * SCI_CLOCKS_A_BIT * divisor.max(1))
    }

    /// Moves the held character to an idle shifter.
    fn shift(&mut self, now: Cycles) {
        if self.transmitter.shifting.is_none()
            && let Some(byte) = self.transmitter.holding.take()
        {
            self.transmitter.shifting = Some((byte, now + self.character_time()));
        }
    }

    /// Time moves on to `now`, with the QSPI's devices: characters go
    /// and come, and a queue finishes.
    pub(crate) fn advance(&mut self, now: Cycles, peripherals: &mut impl Peripherals) {
        while let Some((byte, done)) = self.transmitter.shifting
            && done <= now
        {
            self.line.transmit(byte);
            self.transmitter.shifting = None;
            self.shift(done);
            self.transmitter.complete =
                self.transmitter.shifting.is_none() && self.transmitter.holding.is_none();
        }
        if self.sccr1() & SCCR1_RE != 0 && self.receiver.next <= now && self.line.waiting() {
            if let Some(byte) = self.line.receive() {
                if self.receiver.data.is_some() {
                    self.receiver.overrun = true;
                } else {
                    self.receiver.data = Some(byte);
                }
            }
            self.receiver.next = now + self.character_time();
        }
        if let Some(done) = self.queue_done
            && done <= now
        {
            self.run_queue(peripherals);
            self.queue_done = None;
        }
    }

    /// Runs the queue from NEWQP to ENDQP: each entry's transmit word
    /// to the device its command selects, what comes back into its
    /// receive word; then SPE clears and SPIF sets.
    fn run_queue(&mut self, peripherals: &mut impl Peripherals) {
        let first = usize::from(self.registers[SPCR2_LOW as usize] & QUEUE_POINTER_MASK);
        let last = usize::from(self.registers[SPCR2_HIGH as usize] & QUEUE_POINTER_MASK);
        let word_bits =
            match self.registers[SPCR0_HIGH as usize] >> SPCR0_BITS_SHIFT & SPCR0_BITS_MASK {
                0 => WORD_BITS,
                bits => bits,
            };
        for entry in first..=last.min(QUEUE_ENTRIES - 1) {
            let command = self.registers[COMMAND_RAM as usize + entry];
            let bits = if command & COMMAND_BITSE != 0 {
                word_bits
            } else {
                BYTE_BITS
            };
            let at = TRANSMIT_RAM as usize + 2 * entry;
            let word = u16::from_be_bytes([self.registers[at], self.registers[at + 1]]);
            let reply =
                peripherals.exchange(command & COMMAND_SELECT_MASK, entry - first, word, bits);
            let at = RECEIVE_RAM as usize + 2 * entry;
            self.registers[at..at + 2].copy_from_slice(&reply.to_be_bytes());
        }
        self.registers[SPCR1_HIGH as usize] &= !SPCR1_SPE;
        // last fits the pointer's four bits
        self.registers[SPSR as usize] = SPSR_SPIF | (last as u8 & QUEUE_POINTER_MASK);
    }

    /// The chip selects the running queue's first entry, NEWQP's, names,
    /// while SPE is set (D.4.11, D.4.12, D.4.16): which device the QSPI
    /// is exchanging with.
    pub(crate) fn queue_first_select(&self) -> Option<u8> {
        let first = usize::from(self.registers[SPCR2_LOW as usize] & QUEUE_POINTER_MASK);
        (self.registers[SPCR1_HIGH as usize] & SPCR1_SPE != 0)
            .then(|| self.registers[COMMAND_RAM as usize + first] & COMMAND_SELECT_MASK)
    }

    /// The SCI's interrupt, its level and vector, while it requests one.
    pub(crate) fn sci_interrupt(&self) -> Option<(u8, u8)> {
        let sccr1 = self.sccr1();
        let requesting = sccr1 & SCCR1_TIE != 0 && self.transmitter.holding.is_none()
            || sccr1 & SCCR1_TCIE != 0 && self.transmitter.complete
            || sccr1 & SCCR1_RIE != 0 && (self.receiver.data.is_some() || self.receiver.overrun);
        let level = self.registers[QILR as usize] & LEVEL_MASK;
        (requesting && level != 0)
            .then(|| (level, self.registers[QIVR as usize] & !QSPI_VECTOR_BIT))
    }

    /// The QSPI's end-of-queue interrupt, while it requests one.
    pub(crate) fn qspi_interrupt(&self) -> Option<(u8, u8)> {
        let requesting = self.registers[SPCR2_HIGH as usize] & SPCR2_SPIFIE != 0
            && self.registers[SPSR as usize] & SPSR_SPIF != 0;
        let level = self.registers[QILR as usize] >> ILQSPI_SHIFT & LEVEL_MASK;
        (requesting && level != 0).then(|| (level, self.registers[QIVR as usize] | QSPI_VECTOR_BIT))
    }

    /// When the QSM next changes by itself: a character going or
    /// arriving, or the queue finishing.
    pub(crate) fn next_event(&self) -> Option<Cycles> {
        let arriving =
            (self.sccr1() & SCCR1_RE != 0 && self.line.waiting()).then_some(self.receiver.next);
        [
            self.transmitter.shifting.map(|(_, done)| done),
            arriving,
            self.queue_done,
        ]
        .into_iter()
        .flatten()
        .min()
    }
}

/// The SCI's baud rate for a divisor, for messages and tests.
#[cfg(test)]
fn baud(divisor: u64) -> u64 {
    super::time::CPU_HZ / (SCI_CLOCKS_A_BIT * divisor)
}

#[cfg(test)]
mod tests {
    use super::super::time::Cycles;
    use super::Peripherals;
    use super::Qsm;
    use super::baud;
    use crate::line::Line;

    /// Devices that answer each word with its complement, and remember
    /// what they were sent.
    #[derive(Default)]
    struct Echo(Vec<(u8, u16, u8)>);

    impl Peripherals for Echo {
        fn exchange(&mut self, select: u8, entry: usize, word: u16, bits: u8) -> u16 {
            assert_eq!(entry, self.0.len(), "entries in order from the first");
            self.0.push((select, word, bits));
            !word
        }
    }

    /// The SCI as `sci_init` leaves it at 9600 baud, SCBR 55, with the
    /// transmitter and receiver on and the receive interrupt; QILR
    /// and QIVR as `qsm_init` sets them (startup.h).
    fn sci(line: &Line) -> Qsm {
        let mut qsm = Qsm::new(line.clone());
        qsm.write(0x04, 0x23, Cycles(0));
        qsm.write(0x05, 0x40, Cycles(0));
        qsm.write(0x08, 0, Cycles(0));
        qsm.write(0x09, 55, Cycles(0));
        qsm.write(0x0b, 0x2c, Cycles(0));
        qsm
    }

    /// The registers read as D.4 gives them after a reset: QIVR $0F,
    /// SCCR0 $0004, SPCR0 $0104, SPCR1 $0404, the rest of these 0.
    #[test]
    fn a_reset_leaves_the_manuals_values() {
        let mut qsm = Qsm::new(Line::new("host", false));
        let read = |qsm: &mut Qsm, offsets: [u32; 2]| offsets.map(|offset| qsm.read(offset));
        assert_eq!(read(&mut qsm, [0x04, 0x05]), [0x00, 0x0f], "QILR, QIVR");
        assert_eq!(read(&mut qsm, [0x08, 0x09]), [0x00, 0x04], "SCCR0");
        assert_eq!(read(&mut qsm, [0x0a, 0x0b]), [0x00, 0x00], "SCCR1");
        assert_eq!(read(&mut qsm, [0x18, 0x19]), [0x01, 0x04], "SPCR0");
        assert_eq!(read(&mut qsm, [0x1a, 0x1b]), [0x04, 0x04], "SPCR1");
        assert_eq!(read(&mut qsm, [0x1c, 0x1d]), [0x00, 0x00], "SPCR2");
    }

    /// The running queue's first entry's chip selects, NEWQP's, while
    /// SPE is set; none while it is clear.
    #[test]
    fn the_queues_first_select_is_newqps_while_it_runs() {
        let mut qsm = Qsm::new(Line::new("host", false));
        // NEWQP 2, ENDQP 2; entry 2's command, select 0xd
        qsm.write(0x1c, 0x02, Cycles(0));
        qsm.write(0x1d, 0x02, Cycles(0));
        qsm.write(0x142, 0x4d, Cycles(0));
        assert_eq!(qsm.queue_first_select(), None, "SPE clear");
        qsm.write(0x1a, 0x80, Cycles(0));
        assert_eq!(qsm.queue_first_select(), Some(0x0d));
    }

    #[test]
    fn scbr_55_is_the_manuals_9600_baud_near_enough() {
        assert_eq!(baud(55), 9532);
    }

    #[test]
    fn a_character_written_goes_a_character_time_later() {
        let line = Line::new("host", false);
        let _far = line.attach();
        let mut qsm = sci(&line);
        assert_eq!(qsm.read(0x0c) & 0x01, 0x01, "TDRE");
        qsm.write(0x0f, b'A', Cycles(100));
        assert_eq!(qsm.read(0x0d) & 0x80, 0, "TC cleared");
        qsm.advance(Cycles(100 + 10 * 32 * 55 - 1), &mut Echo::default());
        assert!(line.sent().is_empty());
        qsm.advance(Cycles(100 + 10 * 32 * 55), &mut Echo::default());
        assert_eq!(line.sent(), b"A");
        assert_eq!(qsm.read(0x0d) & 0x80, 0x80, "TC");
    }

    #[test]
    fn a_character_arriving_sets_rdrf_and_asks_for_the_interrupt() {
        let line = Line::new("host", false);
        let mut far = line.attach();
        let mut qsm = sci(&line);
        assert_eq!(qsm.sci_interrupt(), None);
        std::io::Write::write_all(&mut far, b"*").expect("written");
        qsm.advance(Cycles(1), &mut Echo::default());
        assert_eq!(qsm.read(0x0d) & 0x40, 0x40, "RDRF");
        assert_eq!(qsm.sci_interrupt(), Some((3, 0x40)));
        assert_eq!(qsm.read(0x0f), b'*');
        assert_eq!(qsm.read(0x0d) & 0x40, 0, "RDRF cleared");
        assert_eq!(qsm.sci_interrupt(), None);
    }

    #[test]
    fn a_queue_runs_its_entries_and_ends_with_spif_and_the_interrupt() {
        let line = Line::new("host", false);
        let mut qsm = sci(&line);
        // one word to the ADC, chip select 1, as adc_read queues it
        qsm.write(0x120, 0xc0, Cycles(0));
        qsm.write(0x121, 0x00, Cycles(0));
        qsm.write(0x140, 0x41, Cycles(0));
        qsm.write(0x18, 0x83, Cycles(0));
        qsm.write(0x1c, 0x80, Cycles(0));
        qsm.write(0x1d, 0x00, Cycles(0));
        qsm.write(0x1a, 0x81, Cycles(0));
        assert_eq!(qsm.read(0x1a) & 0x80, 0x80, "SPE while it runs");
        let mut devices = Echo::default();
        qsm.advance(Cycles(1_000_000), &mut devices);
        assert_eq!(devices.0, [(1, 0xc000, 16)]);
        assert_eq!(qsm.read(0x1a) & 0x80, 0, "SPE cleared");
        assert_eq!(qsm.read(0x1f) & 0x80, 0x80, "SPIF");
        assert_eq!((qsm.read(0x100), qsm.read(0x101)), (0x3f, 0xff));
        assert_eq!(qsm.qspi_interrupt(), Some((4, 0x41)));
        qsm.write(0x1f, 0, Cycles(1_000_000));
        assert_eq!(qsm.qspi_interrupt(), None);
    }
}
