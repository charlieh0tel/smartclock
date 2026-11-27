//! The 68681 DUART on chip select 8 at `0x200000` (Motorola's MC68681
//! data sheet; `third_party/firmware/include/mc68681.h`): channel A, the GPS
//! engine's port, and channel B, the console; the interrupt vector,
//! mask and status; the counter, which the self-test times against the
//! periodic tick (duart.c, `duart_counter_test`); the input port and
//! the output port.
//!
//! Register n sits at `0x200000 + 2n + 1`.  A channel moves a character
//! a character time after it is written or after its start bit, at the
//! rate its clock-select register and ACR's baud-rate set give.  The far
//! end sends at the line's rate whatever the receiver does, as a line
//! with no flow control does: a character arriving while the receiver
//! is disabled is lost, and one arriving while the FIFO and the shift
//! register are full overruns (3.2, 4.2.9.4).  Local loopback
//! (MR2's mode, which `boot_duartb_loopback_check` and the loopback
//! self-tests set) turns the transmitter back into the receiver, which
//! takes it enabled or not (4.2.7.4), and sends nothing out.  The DUART's interrupt is level 2, vectored by
//! its IVR (restart.md, CS10).
//!
//! The output port's OP2 to OP7 drive the board's six LEDs
//! (docs/firmware/outputs.md, "The rest of the bank"), each reported
//! as it changes (`indicators`), in the bank `led`.
//!
//! What faces the board's nets, each behind a small interface: the
//! channels' serial lines, `Line`s, receive and transmit; the output port's
//! pins, `output_pins` (OP0 to OP7); the input port's, `set_input`
//! (IP0 to IP5).  The bus side is `read` and `write` at an offset from
//! the base, as chip select 8 decodes it, or `read_register` and
//! `write_register` by register number, as the part's RS1 to RS4 take
//! it; an interrupt acknowledge reads the IVR.

use std::collections::VecDeque;

use super::select::Selected;
use super::time::CPU_HZ;
use super::time::Cycles;
use super::time::character;
use crate::indicators::Bank;
use crate::indicators::Indicator;
use crate::indicators::Polarity;
use crate::line::Line;

/// The DUART's registers' span from `0x200000`.
pub(crate) const SIZE: u32 = 0x20;

/// The level the DUART interrupts at: chip select 10 answers the
/// acknowledge at level 2 with the IVR (docs/firmware/restart.md).
const LEVEL: u8 = 2;

/// The registers, by number (Table 4-1), read and written.
const MODE: u8 = 0;
const STATUS_CLOCK: u8 = 1;
const COMMAND: u8 = 2;
const DATA: u8 = 3;
const IPCR_ACR: u8 = 4;
const ISR_IMR: u8 = 5;
const CUR_CTUR: u8 = 6;
const CLR_CTLR: u8 = 7;
const CHANNEL_B: u8 = 8;
const IVR: u8 = 12;
const IP_OPCR: u8 = 13;
const START_SET: u8 = 14;
const STOP_RESET: u8 = 15;

/// The status register's bits (Table 4-5).
const SR_RXRDY: u8 = 0x01;
const SR_FFULL: u8 = 0x02;
const SR_TXRDY: u8 = 0x04;
const SR_TXEMT: u8 = 0x08;
const SR_OVERRUN: u8 = 0x10;
/// The interrupt status bits of channel A; channel B's are four up.
const ISR_TXRDY: u8 = 0x01;
const ISR_RXRDY: u8 = 0x02;
const ISR_CHANNEL_B_SHIFT: u8 = 4;
/// MR1's receive interrupt on a full FIFO rather than on `RxRDY`.
const MR1_RXIRQ_FFULL: u8 = 0x40;
/// MR2's channel mode, and local loopback.
const MR2_MODE_MASK: u8 = 0xc0;
const MR2_LOCAL_LOOPBACK: u8 = 0x80;
/// The command register's fields (Table 4-5, CR).
const CR_RX_ENABLE: u8 = 0x01;
const CR_RX_DISABLE: u8 = 0x02;
const CR_TX_ENABLE: u8 = 0x04;
const CR_TX_DISABLE: u8 = 0x08;
const CR_MISCELLANEOUS_SHIFT: u8 = 4;
const CR_MISCELLANEOUS_MASK: u8 = 0x07;
const CR_RESET_MR_POINTER: u8 = 1;
const CR_RESET_RECEIVER: u8 = 2;
const CR_RESET_TRANSMITTER: u8 = 3;
const CR_RESET_ERROR: u8 = 4;
/// ACR's baud-rate set 2 (Table 4-5, ACR).
const ACR_BRG_SET_2: u8 = 0x80;
/// The receiver's FIFO holds three characters (4.2.3).
const FIFO_DEPTH: usize = 3;
/// The IVR as the reset leaves it (Table 4-5).
const IVR_RESET: u8 = 0x0f;
/// The input port's pins as the board leaves them: nothing pulls one
/// low (the firmware reads bits 0 to 2 only in routines nothing calls;
/// duart.c).  1.6 has D7 read as a one, which this does not.
const INPUT_PINS: u8 = 0x7f;
/// The crystal, 3.6864 MHz (the Z3805A's Y1, M3.68EL: board-atlas.html).
pub(crate) const CRYSTAL_HZ: u64 = 3_686_400;
/// The counter's clock in the firmware's counter mode, the crystal
/// over 16 (duart.c, `ACR_COUNTER_CRYSTAL_16`).
const COUNTER_HZ: u64 = CRYSTAL_HZ / 16;
/// Bits a character frame has, 8N1 as the firmware sets both channels.
const FRAME_BITS: u64 = 10;
/// The clock-select codes' rates (Table 4-5, CSR), in baud-rate set 1
/// and set 2; the codes past `0xc` are the counter and external pins,
/// which the firmware does not select.
const RATES_SET_1: [u64; 13] = [
    50, 110, 134, 200, 300, 600, 1200, 1050, 2400, 4800, 7200, 9600, 38400,
];
const RATES_SET_2: [u64; 13] = [
    75, 110, 134, 150, 300, 600, 1200, 2000, 2400, 4800, 1800, 9600, 19200,
];

/// One channel's state.
#[derive(Debug)]
struct Channel {
    /// MR1 and MR2.
    modes: [u8; 2],
    /// Which the mode register reads and writes next.
    mode_pointer: usize,
    /// The receive clock's select code, CSR's high nibble.
    clock: u8,
    /// Whether the transmitter and the receiver are enabled.
    transmitting: bool,
    receiving: bool,
    /// The transmit holding register's character.
    holding: Option<u8>,
    /// The character shifting out, and when it is done.
    shifting: Option<(u8, Cycles)>,
    /// The receive FIFO.
    fifo: VecDeque<u8>,
    /// A character assembled while the FIFO was full, waiting in the
    /// receive shift register for room (3.2).
    held: Option<u8>,
    /// The overrun error, SR[4]: a character lost (4.2.9.4).
    overrun: bool,
    /// The character on the line, from its start bit.
    incoming: Option<Incoming>,
    /// When the line is free for the next character's start bit: the
    /// last one's end.
    line_free: Cycles,
    /// When the line was last looked at.  The far end puts a character
    /// on it between looks, so its start bit is no earlier than this:
    /// for bytes a script gives at a slice's end, exactly then.
    looked: Cycles,
    /// The far end.
    line: Line,
}

/// A character on the receive line, from its start bit.
#[derive(Debug, Clone, Copy)]
struct Incoming {
    /// The character.
    byte: u8,
    /// When its stop bit ends.
    done: Cycles,
    /// Whether the receiver took its start bit and has been neither
    /// disabled nor reset since; else the character is lost (4.2.7.4).
    taken: bool,
}

impl Channel {
    fn new(line: Line) -> Self {
        Self {
            modes: [0; 2],
            mode_pointer: 0,
            clock: 0,
            transmitting: false,
            receiving: false,
            holding: None,
            shifting: None,
            fifo: VecDeque::new(),
            held: None,
            overrun: false,
            incoming: None,
            line_free: Cycles(0),
            looked: Cycles(0),
            line,
        }
    }

    fn loopback(&self) -> bool {
        self.modes[1] & MR2_MODE_MASK == MR2_LOCAL_LOOPBACK
    }

    fn status(&self) -> u8 {
        let mut status = 0;
        if !self.fifo.is_empty() {
            status |= SR_RXRDY;
        }
        if self.fifo.len() >= FIFO_DEPTH {
            status |= SR_FFULL;
        }
        if self.overrun {
            status |= SR_OVERRUN;
        }
        if self.transmitting && self.holding.is_none() {
            status |= SR_TXRDY;
            if self.shifting.is_none() {
                status |= SR_TXEMT;
            }
        }
        status
    }

    /// Its interrupt status bits, channel A's places.
    fn interrupt_status(&self) -> u8 {
        let status = self.status();
        let receive = if self.modes[0] & MR1_RXIRQ_FFULL != 0 {
            SR_FFULL
        } else {
            SR_RXRDY
        };
        (if status & SR_TXRDY != 0 { ISR_TXRDY } else { 0 })
            | (if status & receive != 0 { ISR_RXRDY } else { 0 })
    }

    fn command(&mut self, value: u8) {
        if value & CR_RX_ENABLE != 0 {
            self.receiving = true;
        }
        if value & CR_RX_DISABLE != 0 {
            self.receiving = false;
            self.lose_incoming();
        }
        if value & CR_TX_ENABLE != 0 {
            self.transmitting = true;
        }
        if value & CR_TX_DISABLE != 0 {
            self.transmitting = false;
        }
        match value >> CR_MISCELLANEOUS_SHIFT & CR_MISCELLANEOUS_MASK {
            CR_RESET_MR_POINTER => self.mode_pointer = 0,
            // the FIFO and every receiver status bit (3.2)
            CR_RESET_RECEIVER => {
                self.receiving = false;
                self.fifo.clear();
                self.held = None;
                self.overrun = false;
                self.lose_incoming();
            }
            CR_RESET_ERROR => self.overrun = false,
            CR_RESET_TRANSMITTER => {
                self.transmitting = false;
                self.holding = None;
                self.shifting = None;
            }
            _ => {}
        }
    }

    fn character_time(&self, set_2: bool) -> Cycles {
        character(FRAME_BITS, self.baud(set_2))
    }

    /// Its rate, baud, from its clock-select code in the set ACR picks.
    fn baud(&self, set_2: bool) -> u64 {
        let rates = if set_2 { &RATES_SET_2 } else { &RATES_SET_1 };
        rates.get(usize::from(self.clock)).copied().unwrap_or(9600)
    }

    /// The character on the line, if any, is lost to the receiver.
    fn lose_incoming(&mut self) {
        if let Some(incoming) = &mut self.incoming {
            incoming.taken = false;
        }
    }

    /// A start bit: a character waiting in the shift register for room
    /// in the FIFO is lost to the new one, and the overrun error set
    /// (3.2).
    fn start_bit(&mut self) {
        if self.held.take().is_some() {
            self.overrun = true;
        }
    }

    /// A character assembled: into the FIFO, or held in the shift
    /// register while the FIFO is full (3.2).
    fn assembled(&mut self, byte: u8) {
        if self.fifo.len() < FIFO_DEPTH {
            self.fifo.push_back(byte);
        } else {
            self.held = Some(byte);
        }
    }

    /// The top of the FIFO, read: a character held in the shift
    /// register takes the room it leaves.
    fn pop(&mut self) -> u8 {
        let byte = self.fifo.pop_front().unwrap_or(0);
        if let Some(held) = self.held.take() {
            self.fifo.push_back(held);
        }
        byte
    }

    fn shift(&mut self, now: Cycles, set_2: bool) {
        if self.shifting.is_none()
            && let Some(byte) = self.holding.take()
        {
            self.shifting = Some((byte, now + self.character_time(set_2)));
            // in local loopback the receiver operates even if disabled
            // (4.2.7.4; SCC68692, MR2A[7:6]: "the receiver need not be
            // enabled")
            if self.loopback() {
                self.start_bit();
            }
        }
    }

    /// When the next character on the line starts, once one waits.
    fn next_start(&self) -> Cycles {
        self.line_free.max(self.looked)
    }

    fn advance(&mut self, now: Cycles, set_2: bool) {
        while let Some((byte, done)) = self.shifting
            && done <= now
        {
            self.shifting = None;
            if self.loopback() {
                self.assembled(byte);
            } else {
                self.line.transmit(byte);
            }
            self.shift(done, set_2);
        }
        // the line's characters, back to back from the first's start
        // bit; in local loopback the receiver takes the transmitter's
        // and loses the line's (3.3.2)
        loop {
            match self.incoming {
                Some(incoming) if incoming.done <= now => {
                    self.incoming = None;
                    if incoming.taken && self.receiving && !self.loopback() {
                        self.assembled(incoming.byte);
                    }
                }
                Some(_) => break,
                None => {
                    let start = self.next_start();
                    if start > now {
                        break;
                    }
                    let Some(byte) = self.line.receive() else {
                        break;
                    };
                    let taken = self.receiving && !self.loopback();
                    if taken {
                        self.start_bit();
                    }
                    let done = start + self.character_time(set_2);
                    self.incoming = Some(Incoming { byte, done, taken });
                    self.line_free = done;
                }
            }
        }
        self.looked = now;
    }

    fn next_event(&self, set_2: bool) -> Option<Cycles> {
        // a character's end: the one on the line's, or with one waiting
        // the next's; its start bit changes nothing the firmware reads
        // before then but the overrun, which can wait as long
        let arriving = match self.incoming {
            Some(incoming) => Some(incoming.done),
            None => self
                .line
                .waiting()
                .then(|| self.next_start() + self.character_time(set_2)),
        };
        [self.shifting.map(|(_, done)| done), arriving]
            .into_iter()
            .flatten()
            .min()
    }
}

/// The board's LEDs on the output port, by bit, named as `board_led`
/// (smartclock.h) names them.
pub(crate) const LEDS: [Indicator; 6] = [
    Indicator {
        bit: 2,
        name: "heartbeat",
    },
    Indicator {
        bit: 3,
        name: "installer",
    },
    Indicator {
        bit: 4,
        name: "cpu",
    },
    Indicator {
        bit: 5,
        name: "ram",
    },
    Indicator {
        bit: 6,
        name: "duart",
    },
    Indicator {
        bit: 7,
        name: "selftest",
    },
];

/// The output port register after a reset, which clears it (2.4).
const OPR_RESET: u8 = 0;

/// The LEDs, as the output port register drives them: a bit set drives
/// its pin low (4.2.12), and the code reads as if an LED lights on a
/// high pin (outputs.md), so an LED is lit while its bit is clear,
/// `Polarity::Low`.  The reset clears the register, lighting all six,
/// until the primary's reset code sets every bit (`0x144ca`, after the
/// boot code's flash checksums; the installer's path does at `0x746`;
/// restart.md).
fn leds() -> Bank {
    Bank::new("led", &LEDS, Polarity::Low, OPR_RESET)
}

/// The DUART's state.
#[derive(Debug)]
pub(crate) struct Duart {
    /// Channels A and B.
    channels: [Channel; 2],
    /// The auxiliary control register.
    acr: u8,
    /// The interrupt mask.
    imr: u8,
    /// The interrupt vector.
    ivr: u8,
    /// The counter's preload, CTUR and CTLR.
    preload: u16,
    /// When the counter started from its preload, while it runs.
    started: Option<Cycles>,
    /// The count it stopped at.
    stopped: u16,
    /// The output port register, as SOPBC and ROPBC leave it, and the
    /// LEDs it drives.
    output: u8,
    leds: Bank,
    /// The levels on the input port's pins, as the register reads them.
    input: u8,
}

impl Duart {
    /// The DUART after a reset, channel A on `gps` and B on `console`.
    pub(crate) fn new(gps: Line, console: Line) -> Self {
        Self {
            channels: [Channel::new(gps), Channel::new(console)],
            acr: 0,
            imr: 0,
            ivr: IVR_RESET,
            preload: 0,
            started: None,
            stopped: 0,
            output: OPR_RESET,
            leds: leds(),
            input: INPUT_PINS,
        }
    }

    /// The register at `offset` from the base, if it is one: the odd
    /// bytes.
    fn register(offset: u32) -> Option<u8> {
        // the registers' span is 32 bytes, so the number fits a byte
        (offset % 2 == 1).then_some((offset / 2) as u8)
    }

    /// The channel a register belongs to, and its number within the
    /// channel's four; the common registers are given as channel A's,
    /// numbered as they are.
    fn channel(register: u8) -> (usize, u8) {
        if (CHANNEL_B..IVR).contains(&register) {
            (1, register - CHANNEL_B)
        } else {
            (0, register)
        }
    }

    fn set_2(&self) -> bool {
        self.acr & ACR_BRG_SET_2 != 0
    }

    /// The counter's value at `now`: counting down from its preload
    /// while it runs.
    fn count(&self, now: Cycles) -> u16 {
        self.started.map_or(self.stopped, |started| {
            let ticks = (now.0 - started.0) * COUNTER_HZ / CPU_HZ;
            // the counter is sixteen bits and wraps
            self.preload.wrapping_sub(ticks as u16)
        })
    }

    /// A register byte at `offset` from the base at `now`; some reads
    /// act.
    pub(crate) fn read(&mut self, offset: u32, now: Cycles) -> u8 {
        Self::register(offset).map_or(0xff, |register| self.read_register(register, now))
    }

    /// Register `register`, 0 to 15, read at `now`; some reads act.
    pub(crate) fn read_register(&mut self, register: u8, now: Cycles) -> u8 {
        let (channel, local) = Self::channel(register);
        match (register, local) {
            (IVR, _) => self.ivr,
            (IP_OPCR, _) => self.input,
            (START_SET, _) => {
                self.started = Some(now);
                0xff
            }
            (STOP_RESET, _) => {
                self.stopped = self.count(now);
                self.started = None;
                0xff
            }
            (IPCR_ACR, _) => 0,
            (ISR_IMR, _) => self.interrupt_status(),
            (CUR_CTUR, _) => self.count(now).to_be_bytes()[0],
            (CLR_CTLR, _) => self.count(now).to_be_bytes()[1],
            (_, MODE) => {
                let channel = &mut self.channels[channel];
                let mode = channel.modes[channel.mode_pointer];
                channel.mode_pointer = 1;
                mode
            }
            (_, STATUS_CLOCK) => self.channels[channel].status(),
            (_, DATA) => self.channels[channel].pop(),
            _ => 0xff,
        }
    }

    /// A register byte at `offset` from the base written at `now`.
    pub(crate) fn write(&mut self, offset: u32, value: u8, now: Cycles) {
        if let Some(register) = Self::register(offset) {
            self.write_register(register, value, now);
        }
    }

    /// Register `register`, 0 to 15, written `value` at `now`.
    pub(crate) fn write_register(&mut self, register: u8, value: u8, now: Cycles) {
        let set_2 = self.set_2();
        let (channel, local) = Self::channel(register);
        match (register, local) {
            (IVR, _) => self.ivr = value,
            (IP_OPCR, _) => {}
            (START_SET, _) => {
                self.output |= value;
                self.leds.write_reporting(now, self.output);
            }
            (STOP_RESET, _) => {
                self.output &= !value;
                self.leds.write_reporting(now, self.output);
            }
            (IPCR_ACR, _) => self.acr = value,
            (ISR_IMR, _) => self.imr = value,
            (CUR_CTUR, _) => self.preload = self.preload & 0x00ff | u16::from(value) << 8,
            (CLR_CTLR, _) => self.preload = self.preload & 0xff00 | u16::from(value),
            (_, MODE) => {
                let channel = &mut self.channels[channel];
                channel.modes[channel.mode_pointer] = value;
                channel.mode_pointer = 1;
            }
            (_, STATUS_CLOCK) => self.channels[channel].clock = value >> 4,
            (_, COMMAND) => self.channels[channel].command(value),
            (_, DATA) => {
                let channel = &mut self.channels[channel];
                if channel.transmitting {
                    channel.holding = Some(value);
                    channel.shift(now, set_2);
                }
            }
            _ => {}
        }
    }

    /// The interrupt status: the channels' bits.  The counter's ready
    /// bit is not modeled: the firmware reads the counter's value, not
    /// its interrupt (duart.c).
    fn interrupt_status(&self) -> u8 {
        self.channels[0].interrupt_status()
            | self.channels[1].interrupt_status() << ISR_CHANNEL_B_SHIFT
    }

    /// Reports the LEDs from `at` on: what is lit then, and each change
    /// after.
    pub(crate) fn report_indicators(&mut self, at: Cycles) {
        self.leds.report_from(at);
    }

    /// The output port's pins, the register's complement (4.2.12).
    #[cfg(test)]
    pub(crate) fn output_pins(&self) -> u8 {
        !self.output
    }

    /// Time moves on to `now`.
    pub(crate) fn advance(&mut self, now: Cycles) {
        let set_2 = self.set_2();
        for channel in &mut self.channels {
            channel.advance(now, set_2);
        }
    }

    /// The interrupt, level 2 and the IVR, while the mask passes a
    /// status bit.
    pub(crate) fn interrupt(&self) -> Option<(u8, u8)> {
        (self.interrupt_status() & self.imr != 0).then_some((LEVEL, self.ivr))
    }

    /// When a channel next changes by itself.
    pub(crate) fn next_event(&self) -> Option<Cycles> {
        let set_2 = self.set_2();
        self.channels
            .iter()
            .filter_map(|channel| channel.next_event(set_2))
            .min()
    }
}

impl Selected for Duart {
    fn read_byte(&mut self, address: u32, now: Cycles) -> u8 {
        self.read(address % SIZE, now)
    }

    fn write_byte(&mut self, address: u32, value: u8, now: Cycles) {
        self.write(address % SIZE, value, now);
    }

    fn next_change(&self) -> Option<Cycles> {
        self.next_event()
    }

    fn advance_to(&mut self, now: Cycles) {
        self.advance(now);
    }
}

#[cfg(test)]
mod tests {
    use super::super::time::CPU_HZ;
    use super::super::time::Cycles;
    use super::Duart;
    use crate::line::Line;

    /// Register n's address offset.
    fn at(register: u32) -> u32 {
        2 * register + 1
    }

    /// The DUART as `duart_early_init` leaves channel B: reset, 8N1 at
    /// 9600 in set 2, enabled.
    fn console(console: &Line) -> Duart {
        let mut duart = Duart::new(Line::new("gps", false), console.clone());
        for (register, value) in [
            (12, 0x43),
            (10, 0x20),
            (10, 0x30),
            (10, 0x10),
            (8, 0x13),
            (8, 0x07),
            (9, 0xbb),
            (4, 0xb0),
            (10, 0x05),
        ] {
            duart.write(at(register), value, Cycles(0));
        }
        duart
    }

    #[test]
    fn the_ivr_holds_what_is_written() {
        let mut duart = Duart::new(Line::new("gps", false), Line::new("console", false));
        assert_eq!(duart.read(at(12), Cycles(0)), 0x0f);
        duart.write(at(12), 0x55, Cycles(0));
        assert_eq!(duart.read(at(12), Cycles(0)), 0x55);
    }

    #[test]
    fn local_loopback_gives_back_what_is_sent_and_sends_nothing() {
        let line = Line::new("console", false);
        let _far = line.attach();
        let mut duart = console(&line);
        duart.write(at(10), 0x10, Cycles(0));
        let _ = duart.read(at(8), Cycles(0));
        duart.write(at(8), 0x87, Cycles(0));
        duart.write(at(11), b'D', Cycles(0));
        assert_eq!(duart.read(at(9), Cycles(0)) & 0x01, 0, "nothing yet");
        duart.advance(Cycles(CPU_HZ / 100));
        assert_eq!(duart.read(at(9), Cycles(0)) & 0x01, 0x01, "RxRDY");
        assert_eq!(duart.read(at(11), Cycles(0)), b'D');
        assert!(line.sent().is_empty());
    }

    /// "If the DUART has been programmed to operate in the local
    /// loopback ... mode, the receiver operates even if it is disabled"
    /// (4.2.7.4).
    #[test]
    fn local_loopback_receives_with_the_receiver_disabled() {
        let line = Line::new("console", false);
        let mut duart = console(&line);
        duart.write(at(10), 0x10, Cycles(0));
        let _ = duart.read(at(8), Cycles(0));
        duart.write(at(8), 0x87, Cycles(0));
        duart.write(at(10), 0x02, Cycles(0));
        duart.write(at(11), b'L', Cycles(0));
        duart.advance(Cycles(CPU_HZ / 100));
        assert_eq!(duart.read(at(9), Cycles(0)) & 0x01, 0x01, "RxRDY");
        assert_eq!(duart.read(at(11), Cycles(0)), b'L');
    }

    #[test]
    fn channel_b_sends_and_raises_its_interrupt_by_the_mask() {
        let line = Line::new("console", false);
        let mut far = line.attach();
        let mut duart = console(&line);
        assert_eq!(duart.interrupt(), None);
        duart.write(at(5), 0x10, Cycles(0));
        assert_eq!(duart.interrupt(), Some((2, 0x43)), "TxRDYB");
        // the first goes straight to the shifter, the second waits in
        // the holding register
        duart.write(at(11), b'p', Cycles(0));
        assert_eq!(duart.interrupt(), Some((2, 0x43)), "TxRDYB again");
        duart.write(at(11), b'q', Cycles(0));
        assert_eq!(duart.interrupt(), None);
        duart.advance(Cycles(CPU_HZ / 100));
        assert_eq!(line.sent(), b"pq");
        duart.write(at(5), 0x20, Cycles(0));
        std::io::Write::write_all(&mut far, b"x").expect("written");
        duart.advance(Cycles(CPU_HZ / 50));
        assert_eq!(duart.interrupt(), Some((2, 0x43)), "RxRDYB");
    }

    /// Channel B's character time at 9600 baud, 8N1.
    const CHARACTER: Cycles = Cycles(super::FRAME_BITS * CPU_HZ / 9600);

    #[test]
    fn the_first_character_arrives_a_character_time_after_its_start_bit() {
        let line = Line::new("console", false);
        let mut far = line.attach();
        let mut duart = console(&line);
        let start = Cycles(1000);
        duart.advance(start);
        std::io::Write::write_all(&mut far, b"x").expect("written");
        assert_eq!(duart.next_event(), Some(start + CHARACTER));
        duart.advance(start + CHARACTER - Cycles(1));
        assert_eq!(duart.read(at(9), start) & 0x01, 0, "still on the line");
        duart.advance(start + CHARACTER);
        assert_eq!(duart.read(at(9), start) & 0x01, 0x01, "RxRDY");
        assert_eq!(duart.read(at(11), start), b'x');
    }

    #[test]
    fn a_character_past_the_fifo_and_the_shift_register_overruns() {
        let line = Line::new("console", false);
        let mut far = line.attach();
        let mut duart = console(&line);
        std::io::Write::write_all(&mut far, b"abcdef").expect("written");
        let mut now = Cycles(0);
        while let Some(next) = duart.next_event() {
            assert!(next > now, "nothing due in the past");
            now = next;
            duart.advance(now);
        }
        assert_eq!(now, Cycles(6 * CHARACTER.0), "back to back");
        // a, b and c in the FIFO, d held in the shift register and lost
        // to e's start bit, e to f's: f held
        let status = duart.read(at(9), now);
        assert_eq!(status & 0x13, 0x13, "RxRDY, FFULL and OE: {status:#04x}");
        let read: Vec<u8> = (0..4).map(|_| duart.read(at(11), now)).collect();
        assert_eq!(read, b"abcf");
        assert_eq!(duart.read(at(9), now) & 0x10, 0x10, "OE until reset");
        duart.write(at(10), 0x40, now);
        assert_eq!(duart.read(at(9), now) & 0x10, 0, "reset error status");
    }

    #[test]
    fn a_character_arriving_while_the_receiver_is_disabled_is_lost() {
        let line = Line::new("console", false);
        let mut far = line.attach();
        let mut duart = console(&line);
        duart.write(at(10), 0x02, Cycles(0));
        std::io::Write::write_all(&mut far, b"x").expect("written");
        duart.advance(CHARACTER);
        duart.write(at(10), 0x01, CHARACTER);
        duart.advance(Cycles(4 * CHARACTER.0));
        assert_eq!(duart.read(at(9), CHARACTER) & 0x01, 0, "nothing received");
        assert!(duart.next_event().is_none());
    }

    #[test]
    fn the_counter_counts_down_at_the_crystal_over_16() {
        let mut duart = Duart::new(Line::new("gps", false), Line::new("console", false));
        duart.write(at(6), 0xff, Cycles(0));
        duart.write(at(7), 0xff, Cycles(0));
        let _ = duart.read(at(14), Cycles(0));
        // ten ticks of 1024 a second, as duart_counter_test pauses
        let _ = duart.read(at(15), Cycles(10 * CPU_HZ / 1024));
        let count =
            u16::from(duart.read(at(6), Cycles(0))) << 8 | u16::from(duart.read(at(7), Cycles(0)));
        assert!((0xf533..=0xf8cc).contains(&count), "{count:#x}");
    }

    #[test]
    fn an_led_lights_as_its_bit_is_reset_and_goes_out_as_it_is_set() {
        // lit by the reset, then the reset code's SOPBC of every bit,
        // then a failed RAM test's ROPBC of OP5, then OP0 and OP1,
        // which drive no LED
        const ALL: u8 = 0xff;
        const OP5: u8 = 0x20;
        const OP0_OP1: u8 = 0x03;
        let mut leds = super::leds();
        assert_eq!(leds.lit(Cycles(0)).len(), 6, "lit by the reset");
        let out = leds.write(Cycles(0), ALL);
        assert!(out.len() == 6 && out.iter().all(|change| !change.lit));
        assert_eq!(
            names_lit(&leds.write(Cycles(1), ALL & !OP5)),
            [("ram", true)]
        );
        assert!(leds.write(Cycles(2), ALL & !OP5 & !OP0_OP1).is_empty());
        assert_eq!(names_lit(&leds.write(Cycles(3), ALL)), [("ram", false)]);
    }

    /// Each change's LED and whether it lit.
    fn names_lit(changes: &[crate::indicators::Change]) -> Vec<(&'static str, bool)> {
        changes
            .iter()
            .map(|change| (change.name, change.lit))
            .collect()
    }

    #[test]
    fn the_output_port_drives_the_complement_of_its_register() {
        let mut duart = Duart::new(Line::new("gps", false), Line::new("console", false));
        duart.write(at(14), 0x05, Cycles(0));
        assert_eq!(duart.output_pins(), 0xfa);
        duart.write(at(15), 0x01, Cycles(0));
        assert_eq!(duart.output_pins(), 0xfb);
    }
}
