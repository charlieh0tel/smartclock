/* The drivers, 0x1d000 to 0x24fff -- DUART channel A, the GPS
 * receiver's link, and channel B, the host port, with their rings; the
 * SCI; the QSPI and the devices on it, the ADC and the DACs; deferred
 * messages; the FPGA's loader and verifier; the health monitor and the
 * PPS monitors -- written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C").  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype.  The
 * drivers' state is docs/firmware/structures.md's "DUART channel A's
 * driver", "DUART channel B's driver", "The SCI port's driver" and
 * "The QSPI's lock"; the registers are mc68681.h's and mc68331.h's.
 * A kernel service's error is its result's low byte, which is all the
 * image tests.  The image's critical sections move to and from the
 * status register itself, which the decompiler's C leaves out; here
 * they are drivers_sr_read and drivers_sr_write. */

/* The control characters the drivers act on. */
enum {
	DRIVERS_CTRL_C = 0x03,
	DRIVERS_BACKSPACE = 0x08,
	DRIVERS_TAB = 0x09,
	DRIVERS_LF = 0x0a,
	DRIVERS_CR = 0x0d,
	DRIVERS_XON = 0x11,
	DRIVERS_XOFF = 0x13,
	DRIVERS_CTRL_X = 0x18,	/* cancels the line */
	DRIVERS_FIRST_PRINTABLE = 0x20,
	DRIVERS_CARET = '^',	/* echoes a control character as ^ and its letter */
	DRIVERS_CONTROL_TO_LETTER = 0x40,
	DRIVERS_ASCII_MASK = 0x7f,
	DRIVERS_MARK_BIT = 0x80,
};

/* A driver's flow byte (duartb_driver.flow, sci_driver.flow). */
enum {
	DRIVERS_FLOW_XOFF_SENT = 0x20,
	DRIVERS_FLOW_XOFF_RECEIVED = 0x80,	/* transmission held */
};

/* The signal duartb_transmit_interrupt gives the SCPI task when an
 * asynchronous send ends; psos-signals.h has no name for it. */
#define DRIVERS_SIGNAL_ASYNC_DONE 0x0020

/* XON/XOFF thresholds, in free bytes of a 256-byte ring. */
enum {
	DRIVERS_XOFF_BELOW_FREE = 0x40,	/* the interrupt sends XOFF with fewer free */
	DRIVERS_XON_ABOVE_FREE = 0x10,	/* a read sends XON once more are free */
};

/* The kernel's I/O devices, the device in the high byte, the minor in
 * the low (kernel_dispatch_io, 0x14c36). */
enum {
	DRIVERS_DEVICE_DUARTB = 0x000,
	DRIVERS_DEVICE_DUARTB_MINOR_1 = 0x001,	/* drtb_release_for_timecode's close */
	DRIVERS_DEVICE_SCI = 0x300,
};

/* The 68681's channel B settings (MC68681 Table 4-5). */
enum {
	DRIVERS_MR1_KEEP = MR1_RXRTS | MR1_RXIRQ_FFULL | MR1_ERROR_BLOCK,
	DRIVERS_MR1_SEVEN_BITS = 2,
	DRIVERS_MR1_EIGHT_BITS = 3,
	DRIVERS_MR2_ONE_STOP_BIT = 0x07,
	DRIVERS_MR2_TWO_STOP_BITS = 0x0f,
	DRIVERS_CSR_CLOCK_BITS = 4,	/* the receiver's clock in the high nibble, the transmitter's in the low */
	/* baud-rate set 2's clock selects, which ACR_BRG_SET_2 picks */
	DRIVERS_CSR_1200 = 0x6,
	DRIVERS_CSR_2400 = 0x8,
	DRIVERS_CSR_9600 = 0xb,
	DRIVERS_CSR_19200 = 0xc,
	DRIVERS_RECEIVE_ERRORS = SR_RECEIVED_BREAK | SR_FRAMING_ERROR | SR_PARITY_ERROR,
};

/* The serial settings' baud codes (duartb_settings_record.baud). */
enum {
	DRIVERS_BAUD_1200,
	DRIVERS_BAUD_2400,
	DRIVERS_BAUD_9600,
	DRIVERS_BAUD_19200,
};

/* The serial settings' parity codes (duartb_settings_record.parity,
 * sci_settings_record.parity). */
enum {
	DRIVERS_PARITY_NONE,
	DRIVERS_PARITY_EVEN,
	DRIVERS_PARITY_ODD,
	DRIVERS_PARITY_ONE,	/* the SCI's only: mark parity */
};

/* How long a reconfiguration waits for the transmitter to empty, in
 * ticks. */
#define DRIVERS_DRAIN_PAUSES 100

/* The GP port's bits 2 to 4, gp_port_select's selection. */
enum {
	DRIVERS_GP_SELECT_SHIFT = 2,
	DRIVERS_GP_SELECT_MASK = 0x1c,
	DRIVERS_GP_SELECT_MAX = 6,
};

/* The output port's bits, by duart_shadow.output_reset: bits 0 and 1
 * each a routine's own, bits 2 to 7 duart_output_bits'. */
enum {
	DRIVERS_OP0 = 0x01,
	DRIVERS_OP1 = 0x02,
	DRIVERS_OP_BITS = 0xfc,
};

/* The status register's interrupt mask, set_interrupt_mask's argument,
 * which it takes from bits 10 to 8 (0x1338e). */
enum {
	DRIVERS_SR_IPL_MASK = 0x700,
	DRIVERS_SR_IPL_2 = 0x200,
	DRIVERS_SR_IPL_3 = 0x300,
	DRIVERS_SR_IPL_4 = 0x400,
	DRIVERS_SR_IPL_6 = 0x600,
	DRIVERS_SR_IPL_7 = 0x700,
	/* what the output-port routines pass, meaning level 6 but taken as
	 * level 0, so that they leave interrupts enabled */
	DRIVERS_SR_IPL_6_UNSHIFTED = 6,
};

/* The status register, which C has no name for: the image's critical
 * sections save it, change its interrupt mask in place with ori and
 * andi, and move the saved word back, all in supervisor mode. */
static inline uint16_t drivers_sr_read(void)
{
	uint16_t sr;

	__asm__ volatile ("move.w %%sr,%0" : "=d"(sr) : : "memory");
	return sr;
}

static inline void drivers_sr_write(uint16_t sr)
{
	__asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory", "cc");
}

/* Raises the interrupt mask by ORing bits into it; returns the status
 * register as it was. */
static inline uint16_t drivers_interrupts_raise(uint16_t mask)
{
	uint16_t saved = drivers_sr_read();

	drivers_sr_write(saved | mask);
	return saved;
}

/* Sets the interrupt mask to a level, as ori and then andi do; returns
 * the status register as it was. */
static inline uint16_t drivers_interrupts_at(uint16_t level)
{
	uint16_t saved = drivers_interrupts_raise(level);

	drivers_sr_write(drivers_sr_read() & ~(DRIVERS_SR_IPL_MASK & ~level));
	return saved;
}

/* A no-op echo, a bare rts: duart_init and duartb_reconfigure put it in
 * duartb.echo when the settings' echo is off (0x1d570). */
void duartb_echo_none(char c)
{
}

/* Sets the GP port's bits 2 to 4 to a selection, 0 to 6, and returns
 * it shifted there (0x1d572). */
int32_t gp_port_select(char selection)
{
	if (selection > DRIVERS_GP_SELECT_MAX)
		selection = 0;
	gpt.portgp &= ~DRIVERS_GP_SELECT_MASK;
	gpt.portgp |= selection << DRIVERS_GP_SELECT_SHIFT;
	return selection << DRIVERS_GP_SELECT_SHIFT;
}

/* DUART channel B's mode registers from the serial settings: parity,
 * data bits and stop bits (0x1d594). */
void duartb_mode_from_settings(void)
{
	duart_shadow.mr1b &= DRIVERS_MR1_KEEP;
	if (duartb_settings.parity == DRIVERS_PARITY_NONE) {
		duartb.parity = 0;
		duart_shadow.mr1b |= MR1_PARITY_NONE;
	} else {
		duartb.parity = 1;
		if (duartb_settings.parity == DRIVERS_PARITY_ODD)
			duart_shadow.mr1b |= MR1_PARITY_ODD;
	}
	duart_shadow.mr1b |= duartb_settings.eight_bits ? DRIVERS_MR1_EIGHT_BITS : DRIVERS_MR1_SEVEN_BITS;
	duart_shadow.mr2b = (duart_shadow.mr2b & ~MR2_STOP_MASK) |
		(duartb_settings.two_stop_bits ? DRIVERS_MR2_TWO_STOP_BITS : DRIVERS_MR2_ONE_STOP_BIT);
	duart.b.cr = CR_RESET_MR_POINTER;
	duart.b.mr = duart_shadow.mr1b;
	duart.b.mr = duart_shadow.mr2b;
	duartb_applied.parity = duartb_settings.parity;
	duartb_applied.eight_bits = duartb_settings.eight_bits;
	duartb_applied.two_stop_bits = duartb_settings.two_stop_bits;
}

/* DUART channel B's baud rate from the serial settings, 19200 for a
 * code out of range (0x1d632). */
void duartb_baud_from_settings(void)
{
	uint8_t clock;

	switch (duartb_settings.baud) {
	case DRIVERS_BAUD_1200:
		clock = DRIVERS_CSR_1200;
		break;
	case DRIVERS_BAUD_2400:
		clock = DRIVERS_CSR_2400;
		break;
	case DRIVERS_BAUD_9600:
		clock = DRIVERS_CSR_9600;
		break;
	default:
		clock = DRIVERS_CSR_19200;
		break;
	}
	clock |= clock << DRIVERS_CSR_CLOCK_BITS;
	duart_shadow.csrb = clock;
	duart.b.sr_csr = clock;
	duartb_applied.baud = duartb_settings.baud;
}

/* Waits for DUART channel B's transmitter and writes a character;
 * returns the status bit it waited on (0x1d680). */
uint8_t duartb_putc_polled(uint8_t c)
{
	uint16_t sr = drivers_interrupts_at(DRIVERS_SR_IPL_2);
	uint8_t status;

	do
		status = duart.b.sr_csr;
	while ((status & SR_TXRDY) == 0);
	duart.b.rb_tb = c;
	drivers_sr_write(sr);
	return status & SR_TXRDY;
}

/* Writes a string to DUART channel B by interrupt, waiting until it is
 * out; returns how much went (0x1d6ba). */
int32_t drtb_puts(char *text)
{
	char *end = text;
	uint16_t sr;

	psos_get_v(SIG_SOQ, 0, 0);
	duartb.transmit_next = text;
	while (*end != 0)
		end++;
	duartb.transmit_count = end - text;
	sr = drivers_interrupts_raise(DRIVERS_SR_IPL_6);
	if (!duartb.reconfiguring) {
		duart_shadow.imr |= IMR_TXRDYB;
		duart.isr_imr = duart_shadow.imr;
	}
	drivers_sr_write(sr);
	if ((uint8_t)psos_wait_v(SIG_SOQ, 0, 0, 0) != 0)
		fatal_error("wait_v SIG_SOQ error in _drtb_puts");
	return duartb.transmit_next - text;
}

/* Reads a character from DUART channel B's ring, waiting for one, and
 * sends XON once the ring has room again (0x1d74a). */
uint8_t drtb_getc(void)
{
	uint8_t character;

	psos_get_v(SIG_SIQ, 0, 0);
	if (duartb_ring.write == duartb_ring.read &&
	    (uint8_t)psos_wait_v(SIG_SIQ, 0, 0, 0) != 0)
		fatal_error("wait_v SIG_SIQ error in getc");
	character = duartb_ring.data[duartb_ring.read++];
	if ((duartb.flow & DRIVERS_FLOW_XOFF_SENT) != 0 &&
	    (uint8_t)(duartb_ring.read - duartb_ring.write) > DRIVERS_XON_ABOVE_FREE) {
		duartb.flow &= ~DRIVERS_FLOW_XOFF_SENT;
		if (duartb.xon_xoff)
			drtb_putc(DRIVERS_XON);
	}
	return character;
}

/* A port's line reader, which drtb_gets and sci_gets share: reads into
 * a buffer of length bytes until CR or LF, or until it is full,
 * echoing through the port's echo; backspace takes back a character,
 * ctrl-X the line, and the CR or LF that is the other half of the last
 * line's end is skipped.  Returns the characters read, and moves the
 * port's receive errors to its read errors.  The SCI's compares a
 * character as a signed char, so that one with its top bit set is a
 * control character (0x1f89a), where channel B's is printable
 * (0x1d816). */
static int32_t drivers_gets(uint8_t *buffer, int32_t length, uint8_t (*getc)(void),
			    putc_function *const *echo, uint8_t *last_terminator,
			    uint8_t *receive_errors, uint8_t *read_errors,
			    char *error_message, bool top_bit_control)
{
	int32_t left = length - 1;
	int32_t count = left;
	uint8_t *next = buffer;

	while (left != 0) {
		uint8_t character = getc();
		bool printable = top_bit_control ? (int8_t)character >= DRIVERS_FIRST_PRINTABLE
						 : character >= DRIVERS_FIRST_PRINTABLE;

		if (printable || character == DRIVERS_TAB) {
			*last_terminator = character;
			*next++ = character;
			left--;
			(*echo)(character);
			continue;
		}
		switch (character) {
		case DRIVERS_BACKSPACE:
			if (count != left) {
				next--;
				left++;
				(*echo)(character);
			}
			break;
		case DRIVERS_LF:
		case DRIVERS_CR:
			if ((*last_terminator == DRIVERS_CR && character == DRIVERS_LF) ||
			    (*last_terminator == DRIVERS_LF && character == DRIVERS_CR)) {
				*last_terminator = 0;
			} else {
				(*echo)(DRIVERS_CR);
				(*echo)(DRIVERS_LF);
				count -= left;
				left = 0;
				*last_terminator = character;
			}
			break;
		case DRIVERS_CTRL_X:
			if (count != left) {
				next = buffer;
				left = count;
				(*echo)(DRIVERS_CR);
				(*echo)(DRIVERS_LF);
			}
			break;
		default:
			(*echo)(DRIVERS_CARET);
			(*echo)(character + DRIVERS_CONTROL_TO_LETTER);
			break;
		}
	}
	*next = 0;
	if (*receive_errors != 0) {
		task_debug_message(error_message);
		*read_errors = *receive_errors;
		*receive_errors = 0;
	} else {
		*read_errors = 0;
	}
	return count;
}

/* Reads a line from DUART channel B (0x1d7e4). */
int32_t drtb_gets(uint8_t *buffer, int32_t length)
{
	return drivers_gets(buffer, length, drtb_getc, &duartb.echo, &duartb.last_terminator,
			    &duartb.receive_errors, &duartb.read_errors, "DRTB receiver error\r\n",
			    false);
}

/* Resets DUART channel B and applies the serial settings: mode, baud
 * rate, flow control and echo (0x1dc7e). */
void duartb_reconfigure(void)
{
	uint16_t sr = drivers_interrupts_raise(DRIVERS_SR_IPL_2);

	duartb.reconfiguring = 1;
	duart_shadow.imr &= ~(IMR_RXRDYB | IMR_TXRDYB);
	duart.isr_imr = duart_shadow.imr;
	duart.b.cr = CR_RX_DISABLE;
	drivers_sr_write(sr);
	for (int32_t pauses = DRIVERS_DRAIN_PAUSES; pauses > 0 && (duart.b.sr_csr & SR_TXEMT) == 0;
	     pauses--)
		psos_pause(1);
	duart.b.cr = CR_RESET_RECEIVER;
	duart.b.cr = CR_RESET_TRANSMITTER;
	sr = drivers_interrupts_raise(DRIVERS_SR_IPL_2);
	duartb_mode_from_settings();
	duartb_baud_from_settings();
	duart.b.cr = CR_TX_ENABLE;
	if (!duartb_settings.xon_xoff) {
		if (duartb.xon_xoff && (duartb.flow & DRIVERS_FLOW_XOFF_SENT) != 0)
			duartb_putc_polled(DRIVERS_XON);
		duartb.xon_xoff = 0;
		duartb.flow &= ~(DRIVERS_FLOW_XOFF_SENT | DRIVERS_FLOW_XOFF_RECEIVED);
	} else {
		if (!duartb.xon_xoff && (duartb.flow & DRIVERS_FLOW_XOFF_SENT) != 0)
			duartb_putc_polled(DRIVERS_XOFF);
		duartb.xon_xoff = 1;
	}
	duartb.echo = duartb_settings.echo ? drtb_putc : duartb_echo_none;
	duart_shadow.imr |= IMR_RXRDYB | IMR_TXRDYB;
	duart.isr_imr = duart_shadow.imr;
	duart.b.cr = CR_RX_ENABLE;
	duartb.reconfiguring = 0;
	drivers_sr_write(sr);
}

/* Writes a character to channel B under the write lock, if it can take
 * it (0x1ddf6). */
void drtb_putc(char c)
{
	char text[2] = { c, 0 };

	/* mutex_acquire's result is positive when another holds it */
	if ((int8_t)mutex_acquire(&duartb.write_token) <= 0) {
		drtb_puts(text);
		mutex_release(&duartb.write_token);
	}
}

/* Starts a block out of channel B by interrupt without waiting, if an
 * asynchronous send is allowed and the port is free (0x1de32). */
void drtb_send_async(char *block, uint8_t count)
{
	if (duartb.async_allowed && !duartb.reconfiguring && !duartb.in_transmit_interrupt) {
		duartb.async_next = block;
		duartb.async_count = count;
		duartb.async_pending = 1;
		duart_shadow.imr |= IMR_TXRDYB;
		duart.isr_imr = duart_shadow.imr;
	}
}

/* Writes a string to the console, device 0; returns its length
 * (0x1de76). */
uint32_t console_write(char *s)
{
	uint32_t length;

	psos_io_write(DRIVERS_DEVICE_DUARTB, s, &length);
	return length;
}

/* Reads from the console, device 0, and its last read errors
 * (0x1debc). */
uint32_t console_read(void *buffer, uint32_t length, uint8_t *errors)
{
	psos_io_read(DRIVERS_DEVICE_DUARTB, buffer, &length);
	if (errors != 0)
		*errors = duartb.read_errors;
	return length;
}

/* Opens the SCPI port, device 0 (0x1dee6). */
void scpi_port_open(void)
{
	psos_io_open(DRIVERS_DEVICE_DUARTB, 0);
}

/* Closes the SCPI port, device 0 (0x1def4). */
void scpi_port_close(void)
{
	psos_io_close(DRIVERS_DEVICE_DUARTB);
}

/* The done hook of channel B's setting descriptors and its PRESet:
 * reconfigures device 0 (0x1df00). */
void duartb_settings_done(void)
{
	psos_io_control(DRIVERS_DEVICE_DUARTB, 0, 0);
}

/* Writes a string to channel B by polling, in supervisor mode; returns
 * its length (0x1df12). */
int32_t console_write_polled(char *s)
{
	int32_t count = 0;
	uint16_t sr = enter_supervisor();

	for (; *s != 0; s++, count++)
		duartb_putc_polled(*s);
	drivers_sr_write(sr);
	return count;
}

/* Whether channel B's write lock is held by another task (0x1df6e). */
bool drtb_try_lock_write(void)
{
	return (int8_t)mutex_acquire(&duartb.write_token) > 0;
}

/* Closes the console's device so that channel B can send the time
 * code, unless an XOFF holds it or another task is writing; returns
 * whether it did (0x1df8a). */
uint8_t drtb_release_for_timecode(void)
{
	if ((duartb.flow & DRIVERS_FLOW_XOFF_RECEIVED) == 0 && !drtb_try_lock_write()) {
		duartb.async_allowed = 1;
		psos_io_close(DRIVERS_DEVICE_DUARTB_MINOR_1);
		return 1;
	}
	return 0;
}

/* Sets or clears one of the output port's own bits, with interrupts
 * meant to be held off (DRIVERS_SR_IPL_6_UNSHIFTED): the register holds each pin's complement, so a bit kept set in
 * output_reset is reset in the port (MC68681 4.2.12). */
static void drivers_output_bit_set(uint8_t bit, char on)
{
	uint16_t sr = set_interrupt_mask(DRIVERS_SR_IPL_6_UNSHIFTED);

	if (!on) {
		duart_shadow.output_reset &= ~bit;
		duart.start_sopbc = bit;
	} else {
		duart_shadow.output_reset |= bit;
		duart.stop_ropbc = bit;
	}
	set_interrupt_mask(sr);
}

/* Sets or clears the output port's bit 0 (0x1dfe6). */
void duart_op0_set(char on)
{
	drivers_output_bit_set(DRIVERS_OP0, on);
}

/* Sets or clears the output port's bit 1 (0x1e02c). */
void duart_op1_set(char on)
{
	drivers_output_bit_set(DRIVERS_OP1, on);
}

/* Resets the output port's bits 2 to 7 in reset and sets those in set,
 * as drivers_output_bit_set does (0x1e126). */
void duart_output_bits(uint8_t reset, uint8_t set)
{
	uint16_t sr = set_interrupt_mask(DRIVERS_SR_IPL_6_UNSHIFTED);
	uint8_t resets = (reset | duart_shadow.output_reset) & DRIVERS_OP_BITS;

	duart.stop_ropbc = resets;
	duart_shadow.output_reset = (reset & DRIVERS_OP_BITS | duart_shadow.output_reset) &
		~(set & DRIVERS_OP_BITS);
	duart.start_sopbc = ~duart_shadow.output_reset & DRIVERS_OP_BITS;
	set_interrupt_mask(sr);
}

/* Sends a message to the GPS receiver on DUART channel A by interrupt,
 * waiting until it is out; only the GPS task may, and another gets 1
 * back (0x1e18a). */
uint8_t drta_send_message(char *message, int16_t length)
{
	uint16_t sr;

	if (psos_my_pid() != task_gpsm)
		return 1;
	psos_get_v(SIG_SOQA, 0, 0);
	duarta.transmit_next = message;
	duarta.transmit_count = length;
	sr = set_interrupt_mask(DRIVERS_SR_IPL_6);
	if (!duarta.held) {
		duart_shadow.imr |= IMR_TXRDYA;
		duart.isr_imr = duart_shadow.imr;
	}
	set_interrupt_mask(sr);
	if ((uint8_t)psos_wait_v(SIG_SOQA, 0, 0, 0) != 0)
		fatal_error("wait_v SIG_SOQA error in drta_send_message");
	return 0;
}

/* Whether DUART channel A's receive ring holds a byte (0x1e222). */
bool duarta_rx_pending(void)
{
	return duarta_ring.write != duarta_ring.read;
}

/* Reads a byte from the GPS receiver's ring on DUART channel A, waiting
 * for one; reports, and hands back through overrun, whether the
 * receiver overran since the last (0x1e238). */
uint8_t drta_get_byte(char *overrun)
{
	if (overrun != 0)
		*overrun = duarta.overrun;
	if (duarta.overrun)
		task_debug_message("DRTA receiver overrun ocurred\r\n");
	duarta.overrun = 0;
	psos_get_v(SIG_SIQ, 0, 0);
	if (duarta_ring.write == duarta_ring.read &&
	    (uint8_t)psos_wait_v(SIG_SIQ, 0, 0, 0) != 0)
		fatal_error("wait_v SIG_SIQ error in drta_get_byte");
	return duarta_ring.data[duarta_ring.read++];
}

/* The DUART self-test's bits in selftest.uart. */
enum {
	DRIVERS_UART_IVR_FAILED = 0x01,
	DRIVERS_UART_COUNTER_FAILED = 0x02,
	DRIVERS_UART_A_MISMATCH = 0x04,
	DRIVERS_UART_A_TIMEOUT = 0x08,
	DRIVERS_UART_B_MISMATCH = 0x10,
	DRIVERS_UART_B_TIMEOUT = 0x20,
};

/* How long the loopback test spins for its string to come back. */
#define DRIVERS_LOOPBACK_SPINS 10000

/* A loopback test's channel: its registers and mode shadow, its
 * interrupts, the flag that keeps its driver off the transmitter, the
 * flag that hands its interrupts to the test, and its failure bit. */
typedef struct drivers_loopback_channel {
	volatile duart_channel *registers;
	uint8_t *mr2;
	uint8_t interrupts;
	uint8_t *held;
	uint8_t *loopback_test;
	int16_t timeout_bit;
} drivers_loopback_channel;

/* Puts a channel in local loopback and has the interrupt routines send
 * "DUART test string" through it, checking each character back; sets
 * the channel's failure bit if it does not all come back
 * (duart_loopback_test_a and duart_loopback_test_b). */
static void drivers_loopback_test(const drivers_loopback_channel *channel)
{
	uint16_t sr = set_interrupt_mask(DRIVERS_SR_IPL_7);
	int32_t spins;

	*channel->held = 1;
	duart_shadow.imr &= ~channel->interrupts;
	duart.isr_imr = duart_shadow.imr;
	channel->registers->cr = CR_RX_DISABLE;
	set_interrupt_mask(sr);
	for (int32_t pauses = DRIVERS_DRAIN_PAUSES;
	     pauses > 0 && (channel->registers->sr_csr & SR_TXEMT) == 0; pauses--)
		psos_pause(1);
	sr = set_interrupt_mask(DRIVERS_SR_IPL_7);
	channel->registers->cr = CR_RESET_RECEIVER;
	channel->registers->cr = CR_RESET_TRANSMITTER;
	*channel->loopback_test = 1;
	/* reading MR1 moves the pointer on to MR2; the image starts its
	 * count from what the read gives (0x1e346) */
	spins = channel->registers->mr;
	*channel->mr2 = (*channel->mr2 & ~MR2_MODE_MASK) | MR2_MODE_LOCAL_LOOPBACK;
	channel->registers->mr = *channel->mr2;
	loopback_send_next = (uint8_t *)s_DUART_test_string_0001ee6e;
	loopback_expect_next = s_DUART_test_string_0001ee6e;
	duart_shadow.imr |= channel->interrupts;
	duart.isr_imr = duart_shadow.imr;
	channel->registers->cr = CR_TX_ENABLE | CR_RX_ENABLE;
	set_interrupt_mask(sr);
	while (spins < DRIVERS_LOOPBACK_SPINS && *loopback_expect_next != 0)
		spins++;
	sr = set_interrupt_mask(DRIVERS_SR_IPL_7);
	if (*loopback_expect_next != 0)
		selftest.uart |= channel->timeout_bit;
	loopback_send_next = (uint8_t *)loopback_idle_text;
	loopback_expect_next = loopback_idle_text;
	channel->registers->cr = CR_RESET_RECEIVER;
	channel->registers->cr = CR_RESET_TRANSMITTER;
	(void)channel->registers->mr;
	*channel->mr2 &= ~MR2_MODE_MASK;
	channel->registers->mr = *channel->mr2;
	duart_shadow.imr |= channel->interrupts;
	duart.isr_imr = duart_shadow.imr;
	channel->registers->cr = CR_TX_ENABLE | CR_RX_ENABLE;
	*channel->held = 0;
	*channel->loopback_test = 0;
	set_interrupt_mask(sr);
}

/* The loopback test on channel A, the GPS receiver's (0x1e2c0). */
void duart_loopback_test_a(void)
{
	const drivers_loopback_channel channel = {
		&duart.a, &duart_shadow.mr2a, IMR_RXRDYA | IMR_TXRDYA,
		&duarta.held, &duarta.loopback_test, DRIVERS_UART_A_TIMEOUT,
	};

	drivers_loopback_test(&channel);
}

/* The loopback test on channel B, the host port (0x1e41a). */
void duart_loopback_test_b(void)
{
	const drivers_loopback_channel channel = {
		&duart.b, &duart_shadow.mr2b, IMR_RXRDYB | IMR_TXRDYB,
		&duartb.reconfiguring, &duartb.loopback_test, DRIVERS_UART_B_TIMEOUT,
	};

	drivers_loopback_test(&channel);
}

/* The counter test: the 68681's counter, preloaded with 0xffff, counts
 * down from the crystal over 16 for ten ticks, at the highest
 * priority. */
enum {
	DRIVERS_COUNTER_PRELOAD = 0xff,	/* each half */
	DRIVERS_ACR_COUNTER_CRYSTAL_16 = 0x30,	/* MC68681 Table 4-4 */
	DRIVERS_ACR_COUNTER_TXCB = 0x20,
	DRIVERS_COUNTER_PAUSES = 10,
	DRIVERS_COUNTER_LEAST = 0xf533,
	DRIVERS_COUNTER_MOST = 0xf8cc,
	DRIVERS_PRIORITY_HIGHEST = 0xff,
	DRIVERS_BYTE_BITS = 8,
};

/* Preloads the DUART's counter with 0xffff, both halves, shadowed. */
static void drivers_counter_preload(void)
{
	duart_shadow.ctur = DRIVERS_COUNTER_PRELOAD;
	duart_shadow.ctlr = DRIVERS_COUNTER_PRELOAD;
	duart.cur_ctur = duart_shadow.ctur;
	duart.clr_ctlr = duart_shadow.ctlr;
}

/* The DUART self-test's counter test: counts ten ticks and fails if
 * the count left is out of range (0x1e574). */
void duart_counter_test(void)
{
	uint8_t priority;
	uint8_t upper;
	uint8_t lower;
	uint16_t count;

	(void)duart.stop_ropbc;
	drivers_counter_preload();
	psos_priority(0, DRIVERS_PRIORITY_HIGHEST, &priority);
	duart_shadow.acr = (duart_shadow.acr & ~ACR_COUNTER_MASK) | DRIVERS_ACR_COUNTER_CRYSTAL_16;
	duart.ipcr_acr = duart_shadow.acr;
	(void)duart.start_sopbc;
	psos_pause(DRIVERS_COUNTER_PAUSES);
	(void)duart.stop_ropbc;
	upper = duart.cur_ctur;
	lower = duart.clr_ctlr;
	duart_shadow.acr = (duart_shadow.acr & ~ACR_COUNTER_MASK) | DRIVERS_ACR_COUNTER_TXCB;
	duart.ipcr_acr = duart_shadow.acr;
	drivers_counter_preload();
	task_set_priority(0, priority);
	count = upper << DRIVERS_BYTE_BITS | lower;
	if (count > DRIVERS_COUNTER_MOST || count < DRIVERS_COUNTER_LEAST)
		selftest.uart |= DRIVERS_UART_COUNTER_FAILED;
}

/* Values the self-tests write to the interrupt vector register and
 * read back, and the vector it is left with, user vector 67, whose
 * handler is primary_user_67. */
enum {
	DRIVERS_IVR_PATTERN_1 = 0x55,
	DRIVERS_IVR_PATTERN_2 = 0xaa,
	DRIVERS_IVR_VECTOR = 0x43,
};

/* Whether the interrupt vector register holds a value written to it. */
static bool drivers_ivr_holds(uint8_t value)
{
	duart.ivr = value;
	return duart.ivr == value;
}

/* Tests the interrupt vector register with two patterns, then sets the
 * vector. */
static void drivers_ivr_test(void)
{
	if (!drivers_ivr_holds(DRIVERS_IVR_PATTERN_1))
		selftest.uart |= DRIVERS_UART_IVR_FAILED;
	if (!drivers_ivr_holds(DRIVERS_IVR_PATTERN_2))
		selftest.uart |= DRIVERS_UART_IVR_FAILED;
	duart.ivr = DRIVERS_IVR_VECTOR;
}

/* The DUART self-test: its interrupt vector register, then its counter
 * and both channels' loopbacks (0x1e650). */
void duart_test(void)
{
	uint16_t sr;

	selftest.uart = 0;
	sr = set_interrupt_mask(DRIVERS_SR_IPL_7);
	drivers_ivr_test();
	set_interrupt_mask(sr);
	if (selftest.uart == 0) {
		duart_counter_test();
		duart_loopback_test_a();
		duart_loopback_test_b();
	}
}

/* The loopback test's receive: checks a character against the next
 * expected. */
static void drivers_test_receive(volatile duart_channel *channel, int16_t mismatch_bit)
{
	char expected = *loopback_expect_next++;

	/* a signed char against the byte read: one with its top bit set never matches */
	if (channel->rb_tb != expected)
		selftest.uart |= mismatch_bit;
}

/* The loopback test's receive on channel A (0x1e6be). */
void duart_test_rx_a(void)
{
	drivers_test_receive(&duart.a, DRIVERS_UART_A_MISMATCH);
}

/* The loopback test's receive on channel B (0x1e6e2). */
void duart_test_rx_b(void)
{
	drivers_test_receive(&duart.b, DRIVERS_UART_B_MISMATCH);
}

/* The loopback test's transmit: sends the next character, or at the
 * string's end turns the transmit interrupt off. */
static void drivers_test_transmit(volatile duart_channel *channel, uint8_t interrupt)
{
	if (*loopback_send_next == 0) {
		duart_shadow.imr &= ~interrupt;
		duart.isr_imr = duart_shadow.imr;
	} else {
		channel->rb_tb = *loopback_send_next++;
	}
}

/* The loopback test's transmit on channel A (0x1e706). */
void duart_test_tx_a(void)
{
	drivers_test_transmit(&duart.a, IMR_TXRDYA);
}

/* The loopback test's transmit on channel B (0x1e736). */
void duart_test_tx_b(void)
{
	drivers_test_transmit(&duart.b, IMR_TXRDYB);
}

/* Takes each byte channel B received into its ring for the read
 * token's task, signaling it once at the end; a ctrl-C signals the
 * console task instead, and with XON/XOFF on, XON and XOFF start and
 * hold transmission, and a ring running out of room sends XOFF.
 * Returns the overrun bit of the status register (0x1e7f6). */
uint8_t duartb_isr(void)
{
	bool received = false;
	uint8_t status;

	while ((duart.b.sr_csr & SR_RXRDY) != 0) {
		uint8_t character;

		duartb.receive_errors |= duart.b.sr_csr & DRIVERS_RECEIVE_ERRORS;
		character = duart.b.rb_tb;
		if (!duartb_applied.eight_bits)
			character &= DRIVERS_ASCII_MASK;
		if (character == DRIVERS_CTRL_C && duartb.read_token.task == task_console) {
			if (duartb.read_token.task != 0 &&
			    (uint8_t)psos_signal_v(duartb.read_token.task, SIGINTR) != 0)
				fatal_error("DUARTB isr signaling SIGINTR");
			if (duartb.xon_xoff)
				duartb.flow &= ~DRIVERS_FLOW_XOFF_RECEIVED;
			if (!duartb.reconfiguring) {
				duart_shadow.imr |= IMR_TXRDYB;
				duart.isr_imr = duart_shadow.imr;
			}
		} else if (character == DRIVERS_XON) {
			if (duartb.xon_xoff) {
				duartb.flow &= ~DRIVERS_FLOW_XOFF_RECEIVED;
				if (!duartb.reconfiguring) {
					duart_shadow.imr |= IMR_TXRDYB;
					duart.isr_imr = duart_shadow.imr;
				}
			}
		} else if (character == DRIVERS_XOFF) {
			if (duartb.xon_xoff) {
				duartb.flow |= DRIVERS_FLOW_XOFF_RECEIVED;
				if (!duartb.async_allowed) {
					duart_shadow.imr &= ~IMR_TXRDYB;
					duart.isr_imr = duart_shadow.imr;
				}
			}
		} else if (duartb.read_token.task != 0) {
			uint8_t free_bytes;

			duartb_ring.data[duartb_ring.write++] = character;
			if (duartb_ring.write == duartb_ring.read)
				duartb.receive_errors |= SR_OVERRUN;
			received = true;
			if ((uint8_t)(duartb_ring.read - duartb_ring.write) < DRIVERS_XOFF_BELOW_FREE) {
				duartb.flow |= DRIVERS_FLOW_XOFF_SENT;
				if (duartb.xon_xoff)
					duartb_putc_polled(DRIVERS_XOFF);
			}
			free_bytes = duartb_ring.read - duartb_ring.write;
			if (free_bytes < duartb.least_free)
				duartb.least_free = free_bytes;
		}
	}
	if (received && (uint8_t)psos_signal_v(duartb.read_token.task, SIG_SIQ) != 0)
		fatal_error("DUARTB isr signaling SIG_SIQ");
	status = duart.b.sr_csr;
	if ((status & SR_OVERRUN) != 0) {
		duartb.receive_errors |= SR_OVERRUN;
		duart.b.cr = CR_RESET_ERROR;
	}
	return status & SR_OVERRUN;
}

/* Channel A's transmit interrupt: sends the message's next byte, or at
 * its end turns the interrupt off and signals the GPS task
 * (0x1e9ca). */
void duarta_tx_interrupt(void)
{
	if (duarta.transmit_count != 0) {
		duart.a.rb_tb = *duarta.transmit_next++;
		duarta.transmit_count--;
	} else {
		duart_shadow.imr &= ~IMR_TXRDYA;
		duart.isr_imr = duart_shadow.imr;
		psos_signal_v(task_gpsm, SIG_SOQA);
	}
}

/* Channel B's transmit interrupt: sends the asynchronous block's next
 * byte, or drtb_puts' unless an XOFF holds it; at drtb_puts' end turns
 * the interrupt off and signals the write token's task, and at the
 * block's end signals the SCPI task (0x1ea10). */
void duartb_transmit_interrupt(void)
{
	duartb.in_transmit_interrupt = 1;
	if (duartb.async_count != 0) {
		duart.b.rb_tb = *duartb.async_next++;
		duartb.async_count--;
	} else if (duartb.transmit_count != 0 &&
		   (duartb.flow & DRIVERS_FLOW_XOFF_RECEIVED) == 0) {
		duart.b.rb_tb = *duartb.transmit_next++;
		duartb.transmit_count--;
	} else {
		duart_shadow.imr &= ~IMR_TXRDYB;
		duart.isr_imr = duart_shadow.imr;
		if (duartb.transmit_count == 0 && duartb.write_token.task != 0 &&
		    (uint8_t)psos_signal_v(duartb.write_token.task, SIG_SOQ) != 0)
			fatal_error("DUARTB isr signaling SIG_SOQ");
	}
	if (duartb.async_pending && duartb.async_count == 0) {
		duartb.async_pending = 0;
		duartb.async_allowed = 0;
		if (task_scpi != 0)
			psos_signal_v(task_scpi, DRIVERS_SIGNAL_ASYNC_DONE);
	}
	duartb.in_transmit_interrupt = 0;
}

/* Stops both DUART channels and masks its interrupts (0x1ebbc). */
void duart_stop(void)
{
	duart.a.cr = CR_RX_DISABLE;
	duart.a.cr = CR_TX_DISABLE;
	duart.b.cr = CR_RX_DISABLE;
	duart.b.cr = CR_TX_DISABLE;
	duart.isr_imr = 0;
}

/* How long duartb_loopback_check spins for each character back. */
#define DRIVERS_LOOPBACK_CHECK_SPINS 3000

/* At reset, before interrupts: sends "DUART" through channel B in local
 * loopback by polling and checks each character back (0x1ebe4). */
void duartb_loopback_check(void)
{
	char *text = "DUART";

	(void)duart.b.mr;
	duart.b.mr = MR2_MODE_LOCAL_LOOPBACK | DRIVERS_MR2_ONE_STOP_BIT;
	loopback_send_next = (uint8_t *)text;
	loopback_expect_next = text;
	duart.b.cr = CR_TX_ENABLE | CR_RX_ENABLE;
	duart.b.rb_tb = *loopback_send_next++;
	while (*loopback_expect_next != 0 && selftest.uart == 0) {
		for (int32_t spins = DRIVERS_LOOPBACK_CHECK_SPINS;
		     spins > 0 && (duart.b.sr_csr & SR_RXRDY) == 0; spins--)
			;
		if ((duart.b.sr_csr & SR_RXRDY) != 0) {
			char expected = *loopback_expect_next++;

			if (duart.b.rb_tb != expected)
				selftest.uart |= DRIVERS_UART_B_MISMATCH;
			if (*loopback_send_next != 0)
				duart.b.rb_tb = *loopback_send_next++;
		} else {
			selftest.uart |= DRIVERS_UART_B_TIMEOUT;
		}
	}
	duart.b.cr = CR_RESET_RECEIVER;
	duart.b.cr = CR_RESET_TRANSMITTER;
	(void)duart.b.mr;
	duart.b.mr = DRIVERS_MR2_ONE_STOP_BIT;
}

/* At reset: tests the DUART's interrupt vector register and sets
 * channel B up for the console, 9600 8N1, then checks it in loopback
 * (0x1ecae). */
void duart_early_init(void)
{
	selftest.uart = 0;
	drivers_ivr_test();
	if (selftest.uart == 0) {
		duart.b.cr = CR_RESET_RECEIVER;
		duart.b.cr = CR_RESET_TRANSMITTER;
		duart.b.cr = CR_RESET_MR_POINTER;
		duart.b.mr = MR1_PARITY_NONE | DRIVERS_MR1_EIGHT_BITS;
		duart.b.mr = DRIVERS_MR2_ONE_STOP_BIT;
		duart.b.sr_csr = DRIVERS_CSR_9600 << DRIVERS_CSR_CLOCK_BITS | DRIVERS_CSR_9600;
		duart.ipcr_acr = ACR_BRG_SET_2 | DRIVERS_ACR_COUNTER_TXCB;
		duartb_loopback_check();
	}
}

/* A length-prefixed message and ", %08X" with a code, to fatal_error;
 * the two arguments before the code are unread (0x1eefc). */
void fatal_error_with_code(char *message, uint32_t unused_1, uint32_t unused_2, uint32_t code)
{
	char text[100];

	memcpy(text, message + 1, *message);
	sprintf(text + *message, ", %08X", code);
	fatal_error(text);
}

/* Writes a string by polling while debug output is on; returns its
 * length, 0 when off (0x1ef74). */
int32_t debug_write(char *text)
{
	if (!debug_enabled)
		return 0;
	return console_write_polled(text);
}

/* The calling task's id, from ident with no name. */
static uint32_t drivers_my_task(void)
{
	uint32_t task;
	uint8_t error;

	if ((uint8_t)psos_ident(0, &task, &error) != 0)
		fatal_error("dmessage");
	return task;
}

/* Prints "task: message" with the calling task's name, while debug
 * output is on (0x1eff6). */
void task_debug_message(char *message)
{
	if (debug_enabled) {
		uint32_t task = drivers_my_task();

		debug_printf("%s: %s", task_name((void *)task), message);
	}
}

/* A task and its dmes_ flag, which task_dmessage looks the caller up
 * in. */
typedef struct drivers_task_flag {
	uint32_t *task;
	uint32_t *enabled;
} drivers_task_flag;

/* The tasks task_dmessage prints for, in the order it checks them. */
static const drivers_task_flag drivers_task_flags[] = {
	{ &task_gpsm, &dmes_gpsm },
	{ &task_hmon, &dmes_hmon },
	{ &task_klok, &dmes_klok },
	{ &task_pllp, &dmes_pllp },
	{ &task_root, &dmes_root },
	{ &task_scpi, &dmes_scpi },
	{ &task_spoo, &dmes_spoo },
	{ &task_curv, &dmes_curv },
};

/* Prints "task: message" with the calling task's name, while debug
 * output and the task's dmes_ flag are on (0x1f056). */
void task_dmessage(char *message)
{
	uint32_t task;
	bool enabled = false;

	if (!debug_enabled)
		return;
	task = drivers_my_task();
	for (uint32_t i = 0; i < sizeof drivers_task_flags / sizeof drivers_task_flags[0]; i++) {
		if (task == *drivers_task_flags[i].task && *drivers_task_flags[i].enabled != 0) {
			enabled = true;
			break;
		}
	}
	if (enabled)
		debug_printf("%s: %s", task_name((void *)task), message);
}

/* crash_message's size, and the length below which it takes ", " and
 * a task's name. */
enum {
	DRIVERS_CRASH_MESSAGE_SIZE = 80,
	DRIVERS_CRASH_ROOM_FOR_NAME = 74,
	DRIVERS_FATAL_PAUSES = 10,
};

/* Keeps the message, and the task's name, at crash_message; prints
 * them and "RESET to recover." while debug output is on; then spins
 * until the watchdog restarts the processor (0x1f176). */
void fatal_error(char *message)
{
	uint32_t task;

	strncpy(crash_message, message, DRIVERS_CRASH_MESSAGE_SIZE);
	crash_message[DRIVERS_CRASH_MESSAGE_SIZE - 1] = 0;
	if (kernel_running && (uint8_t)psos_ident(0, &task, 0) == 0) {
		uint32_t length = 0;

		while (crash_message[length] != 0)
			length++;
		if (length < DRIVERS_CRASH_ROOM_FOR_NAME) {
			strcat(crash_message, ", ");
			strcat(crash_message, task_name((void *)task));
		}
		if (debug_enabled) {
			printf_polled("%s: %s", task_name((void *)task), "FATAL ERROR: ");
			printf_polled("%s\r\nRESET to recover.\r\n", message);
			psos_pause(DRIVERS_FATAL_PAUSES);
		}
		set_interrupt_mask(DRIVERS_SR_IPL_4);
	}
	for (;;)
		;
}

/* The QSPI's devices, by the minor number of I/O device 1, the QSPI
 * (qspi_queue_setup's cases). */
enum {
	DRIVERS_QSPI_DEVICE = 0x100,
	DRIVERS_QSPI_ADC = 1,
	DRIVERS_QSPI_HDAC = 2,
	DRIVERS_QSPI_EFC = 7,
};

/* The ADC's command word, as adc_read builds it (0x1f2ce), and its
 * reply, the reading in bits 9 to 2. */
enum {
	DRIVERS_ADC_COMMAND = 0xc000,
	DRIVERS_ADC_CHANNEL_SHIFT = 11,
	DRIVERS_ADC_REPLY_SHIFT = 2,
};

/* Reads an ADC channel through the QSPI (0x1f2c0). */
int32_t adc_read(adc_channel channel)
{
	uint16_t command = channel << DRIVERS_ADC_CHANNEL_SHIFT | DRIVERS_ADC_COMMAND;
	uint16_t reply;
	qspi_buffers buffers = { &command, &reply };

	qspi_read(DRIVERS_QSPI_ADC, &buffers, 1);
	return reply >> DRIVERS_ADC_REPLY_SHIFT;
}

/* A channel's reading times a scale, of its low byte. */
static float drivers_adc_scaled(adc_channel channel, float scale)
{
	return float_multiply(unsigned_to_float((uint8_t)adc_read(channel)), scale);
}

/* A channel's reading times a scale, corrected by the 5 V supply's
 * reading times another: both channels read first. */
static float drivers_adc_against_5v(adc_channel channel, float scale, float scale_5v)
{
	uint8_t reading = adc_read(channel);
	uint8_t reading_5v = adc_read(ADC_5V);
	float value = float_multiply(unsigned_to_float(reading), scale);

	return float_add(value, float_multiply(unsigned_to_float(reading_5v), scale_5v));
}

/* The ADC's scales, the image's own constants. */
#define DRIVERS_ADC_TEMPERATURE_SCALE 0.273f
#define DRIVERS_ADC_5V_SCALE 0.0242f
#define DRIVERS_ADC_OVEN_SCALE 0.0743f
#define DRIVERS_ADC_OVEN_5V_SCALE -0.0472f
#define DRIVERS_ADC_PLUS_15V_SCALE 0.0763f
#define DRIVERS_ADC_MINUS_15V_SCALE 0.0438f
#define DRIVERS_ADC_MINUS_15V_5V_SCALE -0.084f
#define DRIVERS_ADC_CURRENT_FULL_SCALE 250.0f
#define DRIVERS_ADC_FULL_SCALE 255.0f

/* The temperature, "Temperature: %f" (0x1f308). */
float read_adc_temp(void)
{
	return drivers_adc_scaled(ADC_TEMPERATURE, DRIVERS_ADC_TEMPERATURE_SCALE);
}

/* The 5 V supply, "5V reading: %f" (0x1f32e). */
float read_adc_5v(void)
{
	return drivers_adc_scaled(ADC_5V, DRIVERS_ADC_5V_SCALE);
}

/* The double oven, "Double oven: %f" (0x1f356). */
float read_adc_doven(void)
{
	return drivers_adc_against_5v(ADC_DOUBLE_OVEN, DRIVERS_ADC_OVEN_SCALE,
				      DRIVERS_ADC_OVEN_5V_SCALE);
}

/* The oven, "Oven: %f" (0x1f3ae). */
float read_adc_oven(void)
{
	return drivers_adc_against_5v(ADC_OVEN, DRIVERS_ADC_OVEN_SCALE, DRIVERS_ADC_OVEN_5V_SCALE);
}

/* The +15 V supply, "+15V reading: %f" (0x1f406). */
float read_adc_p15v(void)
{
	return drivers_adc_scaled(ADC_PLUS_15V, DRIVERS_ADC_PLUS_15V_SCALE);
}

/* The -15 V supply, "-15V reading: %f" (0x1f42e). */
float read_adc_m15v(void)
{
	return drivers_adc_against_5v(ADC_MINUS_15V, DRIVERS_ADC_MINUS_15V_SCALE,
				      DRIVERS_ADC_MINUS_15V_5V_SCALE);
}

/* Channel 3's current, 250/255 a count. */
static float drivers_adc_current(void)
{
	return float_divide(drivers_adc_scaled(ADC_CURRENT, DRIVERS_ADC_CURRENT_FULL_SCALE),
			    DRIVERS_ADC_FULL_SCALE);
}

/* :DIAG:ROSC:CURR?'s reading, channel 3's current (0x1f486). */
float read_adc_osc_curr(void)
{
	return drivers_adc_current();
}

/* The antenna current, "Antenna current: %f", the same channel
 * (0x1f4ba). */
float read_adc_ant_curr(void)
{
	return drivers_adc_current();
}

/* The hex DAC: six 6-bit channels, two to each of hdac_shadow's three
 * words, sent to QSPI device 2. */
enum {
	DRIVERS_HDAC_CHANNELS = 6,
	DRIVERS_HDAC_BITS = 6,
	DRIVERS_HDAC_MASK = 0x3f,
	DRIVERS_HDAC_WORDS = 3,
	DRIVERS_HDAC_DEFAULT_SLOT = 3,	/* a channel out of range */
};

/* The hdac_write word's: a value into channel 1 to 6, slot 6 - channel,
 * or slot 3 for any other (0x1f4ee). */
void hdac_write(uint8_t value, uint8_t channel)
{
	uint8_t slot = DRIVERS_HDAC_DEFAULT_SLOT;

	if (channel >= 1 && channel <= DRIVERS_HDAC_CHANNELS)
		slot = DRIVERS_HDAC_CHANNELS - channel;
	hdac_set(value, slot);
}

/* A 6-bit value into slot 0 to 5 of hdac_shadow, the high half of
 * word slot / 2 for an even slot, the low for an odd, and the three
 * words to the DAC (0x1f53a). */
void hdac_set(uint8_t value, uint8_t slot)
{
	if (slot < DRIVERS_HDAC_CHANNELS) {
		uint16_t *word = &hdac_shadow[slot >> 1];

		if (slot % 2 == 0) {
			*word &= DRIVERS_HDAC_MASK;
			*word |= (value & DRIVERS_HDAC_MASK) << DRIVERS_HDAC_BITS;
		} else {
			*word &= DRIVERS_HDAC_MASK << DRIVERS_HDAC_BITS;
			*word |= value & DRIVERS_HDAC_MASK;
		}
	}
	qspi_write(DRIVERS_QSPI_HDAC, hdac_shadow, DRIVERS_HDAC_WORDS);
	psos_pause(1);
}

/* Two channels' values as one of hdac_shadow's words. */
static uint16_t drivers_hdac_word(uint8_t low, uint8_t high)
{
	return (low & DRIVERS_HDAC_MASK) | (high & DRIVERS_HDAC_MASK) << DRIVERS_HDAC_BITS;
}

/* The hdac_all word's: all six channels at once (0x1f5bc). */
void hdac_write_all(uint8_t channel_1, uint8_t channel_2, uint8_t channel_3, uint8_t channel_4,
		    uint8_t channel_5, uint8_t channel_6)
{
	hdac_shadow[0] = drivers_hdac_word(channel_5, channel_6);
	hdac_shadow[1] = drivers_hdac_word(channel_3, channel_4);
	hdac_shadow[2] = drivers_hdac_word(channel_1, channel_2);
	qspi_write(DRIVERS_QSPI_HDAC, hdac_shadow, DRIVERS_HDAC_WORDS);
	psos_pause(1);
}

/* Sends hdac_shadow to the DAC by polling (0x1f62e). */
void hdac_update(void)
{
	qspi_send(DRIVERS_QSPI_HDAC, hdac_shadow, DRIVERS_HDAC_WORDS);
}

/* The efc_write word's: the value to the EFC's DAC, then its strobe
 * (0x1f646). */
void efc_write(uint16_t value)
{
	qspi_write(DRIVERS_QSPI_EFC, &value, 1);
	efc_strobe = 0;
}

/* A no-op echo, a bare rts: sci_init and sci_reconfigure put it in
 * sci.echo when the settings' echo is off (0x1f6c8). */
void sci_echo_none(char c)
{
}

/* The SCI's baud divisors for SCCR0 (MC68331UM D.4.4), and the
 * per-baud value sci_baud_set keeps beside each. */
enum {
	DRIVERS_SCI_DIVISOR_1200 = 0x1b5,
	DRIVERS_SCI_DIVISOR_2400 = 0xda,
	DRIVERS_SCI_DIVISOR_9600 = 0x37,
	DRIVERS_SCI_DIVISOR_19200 = 0x1b,
	DRIVERS_SCI_CONSTANT_1200 = 0x2222,
	DRIVERS_SCI_CONSTANT_2400 = 0x1111,
	DRIVERS_SCI_CONSTANT_9600 = 0x444,
	DRIVERS_SCI_CONSTANT_19200 = 0x222,
};

/* Sets the SCI's baud rate from a settings code, 9600 for a code out of
 * range (0x1f6ca). */
void sci_baud_set(char baud)
{
	uint16_t divisor;

	switch ((uint8_t)baud) {
	case DRIVERS_BAUD_1200:
		divisor = DRIVERS_SCI_DIVISOR_1200;
		sci.baud_constant = DRIVERS_SCI_CONSTANT_1200;
		break;
	case DRIVERS_BAUD_2400:
		divisor = DRIVERS_SCI_DIVISOR_2400;
		sci.baud_constant = DRIVERS_SCI_CONSTANT_2400;
		break;
	case DRIVERS_BAUD_19200:
		divisor = DRIVERS_SCI_DIVISOR_19200;
		sci.baud_constant = DRIVERS_SCI_CONSTANT_19200;
		break;
	default:
		divisor = DRIVERS_SCI_DIVISOR_9600;
		sci.baud_constant = DRIVERS_SCI_CONSTANT_9600;
		break;
	}
	qsm.sccr0 = divisor;
}

/* Waits for the SCI's transmitter and writes a character, its top bit
 * set for mark parity; returns the word written (0x1f71a). */
uint16_t sci_putc_polled(char c)
{
	uint16_t sr = drivers_interrupts_at(DRIVERS_SR_IPL_3);
	uint16_t data = c;

	while ((qsm.scsr & SCSR_TDRE) == 0)
		;
	if (sci.mark_bit)
		data |= DRIVERS_MARK_BIT;
	qsm.scdr = data;
	drivers_sr_write(sr);
	return data;
}

/* Writes a string to the SCI by interrupt, to its NUL, waiting until it
 * is out; returns how much went (0x1f76c). */
int32_t sci_puts(char *text)
{
	int32_t count;

	psos_get_v(SIG_SOQ_SCI, 0, 0);
	sci.transmit_next = text;
	if (!sci.reconfiguring)
		qsm.sccr1 |= SCCR1_TIE;
	if ((uint8_t)psos_wait_v(SIG_SOQ_SCI, 0, 0, 0) != 0)
		fatal_error("wait_v SIG_SOQ_SCI error in _sci_puts");
	count = sci.transmit_next - text;
	sci.transmit_next = "";
	return count;
}

/* Reads a character from the SCI's ring, waiting for one, and sends XON
 * by polling once the ring has room again (0x1f7d0). */
uint8_t sci_getc(void)
{
	uint8_t character;

	psos_get_v(SIG_SIQ_SCI, 0, 0);
	if (sci_ring.write == sci_ring.read &&
	    (uint8_t)psos_wait_v(SIG_SIQ_SCI, 0, 0, 0) != 0)
		fatal_error("wait_v SIG_SIQ_SCI error in getc");
	character = sci_ring.data[sci_ring.read++];
	if ((sci.flow & DRIVERS_FLOW_XOFF_SENT) != 0 &&
	    (uint8_t)(sci_ring.read - sci_ring.write) > DRIVERS_XON_ABOVE_FREE) {
		sci.flow &= ~DRIVERS_FLOW_XOFF_SENT;
		if (sci.xon_xoff)
			sci_putc_polled(DRIVERS_XON);
	}
	return character;
}

/* Reads a line from the SCI (0x1f868). */
int32_t sci_gets(char *buffer, int32_t length)
{
	return drivers_gets((uint8_t *)buffer, length, sci_getc, &sci.echo, &sci.last_terminator,
			    &sci.receive_errors, &sci.read_errors, "SCI receiver error ocurred\r\n",
			    true);
}

/* Writes a character to the SCI under its write lock, if it can take
 * it (0x201b0). */
void sci_putc(char c)
{
	char text[2] = { c, 0 };

	/* sci_lock's result is positive when another holds the lock */
	if (sci_lock(1) <= 0) {
		sci_puts(text);
		sci_unlock(1);
	}
}

/* Starts a block out of the SCI by interrupt without waiting, if an
 * asynchronous send is allowed and the port is free (0x201e0). */
void sci_send_async(char *block, uint8_t count)
{
	if (sci.async_allowed && !sci.reconfiguring && !sci.field_11) {
		sci.async_next = block;
		sci.async_count = count;
		sci.async_pending = 1;
		qsm.sccr1 |= SCCR1_TIE;
	}
}

/* Writes a string to the SCI's device; returns its length
 * (0x2021a). */
uint32_t sci_port_write(void *text)
{
	uint32_t length;

	psos_io_write(DRIVERS_DEVICE_SCI, text, &length);
	return length;
}

/* The done hook of the SCI's setting descriptors and its PRESet:
 * reconfigures device 3 (0x202ac). */
void sci_settings_done(void)
{
	psos_io_control(DRIVERS_DEVICE_SCI, 0, 0);
}

/* Writes a string to the SCI by polling, in supervisor mode; returns
 * its length (0x202c0). */
int32_t sci_puts_polled(char *text)
{
	int32_t count = 0;
	uint16_t sr = enter_supervisor();

	for (; *text != 0; text++, count++)
		sci_putc_polled(*text);
	drivers_sr_write(sr);
	return count;
}

/* The QSPI lock's ring of waiting tasks wraps at 16. */
#define DRIVERS_QSPI_WAITING_MASK 0xf

/* Hands the QSPI lock to the next task waiting, or frees it. */
static void drivers_qspi_hand_on(void)
{
	if (qspi.write == qspi.read) {
		qspi.busy = 0;
		return;
	}
	if ((uint8_t)psos_signal_v(qspi.waiting[qspi.read], SIG_QSPI) != 0)
		fatal_error("SPI isr signaling SPI owner");
	qspi.read = (qspi.read + 1) & DRIVERS_QSPI_WAITING_MASK;
}

/* Releases the QSPI lock to the next task waiting, at interrupt level
 * 4 (0x2050c). */
void qspi_release(void)
{
	uint16_t sr = drivers_interrupts_at(DRIVERS_SR_IPL_4);

	drivers_qspi_hand_on();
	drivers_sr_write(sr);
}

/* Sets a transfer up and starts it, or on a device it does not know
 * releases the lock; then waits while the interrupt has the transfer
 * running (0x2081e). */
void qspi_start(uint8_t device, uint16_t *transmit, uint16_t count, char read)
{
	uint16_t spcr1;

	qspi.running = 0;
	/* qspi_queue_setup's low byte is nonzero for a device it does not know */
	if ((uint8_t)qspi_queue_setup(device, transmit, count, read, &spcr1) == 0) {
		qsm.spcr1 = spcr1;
	} else {
		qspi_release();
		if (read)
			qspi.copy_pending = 0;
	}
	while (qspi.running)
		;
}

/* Takes the QSPI lock, or queues the task and waits until it is handed
 * on, at interrupt level 4 while it looks (0x20874). */
void qspi_acquire(void)
{
	uint16_t sr = drivers_interrupts_at(DRIVERS_SR_IPL_4);

	if (!qspi.busy) {
		qspi.busy = 1;
		drivers_sr_write(sr);
		return;
	}
	qspi.waiting[qspi.write] = psos_my_pid();
	qspi.write = (qspi.write + 1) & DRIVERS_QSPI_WAITING_MASK;
	drivers_sr_write(sr);
	if ((uint8_t)psos_wait_v(SIG_QSPI, 0, 0, 0) != 0)
		fatal_error("wait_v SIG_QSPI");
}

/* The QSPI's end of queue: copies the receive RAM out if a read wants
 * it, hands the lock on, and clears SPIF, reading SPSR first as the
 * clear requires (MC68331UM 6.3.5.1) (0x20972). */
void qspi_isr(void)
{
	qspi.running = 0;
	if (qspi.copy_pending) {
		volatile uint16_t *from = qsm.rr;
		uint16_t *end = qspi.receive_buffer + qspi.receive_count;

		for (uint16_t *to = qspi.receive_buffer; to < end; to++)
			*to = *from++;
		qspi.copy_pending = 0;
	}
	drivers_qspi_hand_on();
	(void)qsm.spsr;
	qsm.spsr = 0;
}

/* Writes a QSPI device through the kernel, device 1 (0x20acc). */
void qspi_write(uint8_t device, void *buffers, uint32_t count)
{
	psos_io_write(DRIVERS_QSPI_DEVICE | device, buffers, &count);
}

/* Reads a QSPI device through the kernel, device 1 (0x20aec). */
void qspi_read(uint8_t device, void *buffers, uint32_t count)
{
	psos_io_read(DRIVERS_QSPI_DEVICE | device, buffers, &count);
}

/* Sends to a QSPI device by polling, in supervisor mode with interrupts
 * masked, without the receive RAM's copy; finishes a transfer still
 * pending first (0x20b0c). */
void qspi_send(uint8_t device, uint16_t *transmit, uint16_t count)
{
	uint16_t sr = enter_supervisor();
	uint16_t spcr1;

	drivers_interrupts_raise(DRIVERS_SR_IPL_7);
	qspi.field_00 = 1;
	while ((qsm.spcr1 & SPCR1_SPE) != 0)
		;
	if ((qsm.spsr & SPSR_SPIF) != 0)
		qspi_isr();
	if ((uint8_t)qspi_queue_setup(device, transmit, count, 0, &spcr1) == 0) {
		qsm.spcr1 = spcr1;
		while ((qsm.spsr & SPSR_SPIF) == 0)
			;
		(void)qsm.spsr;
		qsm.spsr = 0;
	}
	drivers_sr_write(sr);
}

/* The spool task: its name, its stack sizes in words (supervisor and
 * user), its exchange's create_x arguments and its request_x's, which
 * psos.md leaves unnamed, and the request error on which it prints the
 * deferred messages instead (structures.md, "Others"). */
enum {
	DRIVERS_SPOO_SUPERVISOR_STACK = 0x600,
	DRIVERS_SPOO_USER_STACK = 0x10,
	DRIVERS_SPOO_EXCHANGE_A = 2,
	DRIVERS_SPOO_EXCHANGE_B = 5,
	DRIVERS_SPOO_REQUEST_B = 0x400,
	DRIVERS_SPOO_FLUSH_ERROR = 0x81,
	DRIVERS_MODE_NO_PREEMPT = 0x80,	/* mode's bit (psos.md, service 32) */
};
#define DRIVERS_SPOO_NAME "spoo"

/* Creates the spool task at a priority, unless one is running, with its
 * exchange, and starts it; returns 0, else the kernel's error, or 1 for
 * a spool task already there (0x20c96). */
uint32_t spoo_task_create(uint32_t priority)
{
	uint32_t error = (uint8_t)psos_ident(DRIVERS_SPOO_NAME, 0, 0) == 0;
	char text[80];

	if (error == 0)
		error = (uint8_t)psos_create_x(DRIVERS_SPOO_NAME, DRIVERS_SPOO_EXCHANGE_A,
					       DRIVERS_SPOO_EXCHANGE_B, &spoo_exchange);
	if (error == 0)
		error = (uint8_t)psos_create(DRIVERS_SPOO_NAME, 0, priority,
					     DRIVERS_SPOO_SUPERVISOR_STACK, DRIVERS_SPOO_USER_STACK, 0,
					     &task_spoo);
	if (error == 0)
		error = (uint8_t)psos_start(task_spoo, spoo_task);
	if (error == 0) {
		spoo_started = 1;
	} else {
		sprintf(text, "Spool_go error %#04x", error);
		fatal_error(text);
	}
	return error;
}

/* Runs a function with three arguments: itself before the spool task
 * starts, returning 1, and after by sending it to the spool task,
 * returning send_x's error; 0 for no function (0x20d66). */
uint32_t spoo_run(spoo_function *function, uint32_t a, uint32_t b, uint32_t c)
{
	spoo_request request;

	if (function == 0)
		return 0;
	if (!spoo_started) {
		function(a, b, c);
		return 1;
	}
	request.field_04 = 0;
	request.function = function;
	request.arguments[0] = a;
	request.arguments[1] = b;
	request.arguments[2] = c;
	return (uint8_t)psos_send_x(spoo_exchange, &request);
}

/* deferred_text's room for a message: one starts only below this. */
#define DRIVERS_DEFERRED_LAST_START 944

/* Formats a message into deferred_text, which the spool task prints;
 * returns vsprintf's count, -1 when there is no room (0x20dd2). */
int32_t deferred_vprintf(char *format, void *arguments)
{
	int32_t count = -1;

	/* with preemption off, empties the text once it has all been printed */
	psos_mode(DRIVERS_MODE_NO_PREEMPT, DRIVERS_MODE_NO_PREEMPT, 0);
	if (deferred_start >= deferred_end) {
		deferred_end = 0;
		deferred_start = 0;
	}
	psos_mode(~DRIVERS_MODE_NO_PREEMPT, DRIVERS_MODE_NO_PREEMPT, 0);
	if (deferred_end < DRIVERS_DEFERRED_LAST_START) {
		count = vsprintf(deferred_text + deferred_end, format, arguments);
		if (count >= 0)
			deferred_end += count + 1;
	}
	return count;
}

/* The spool task: runs each function spoo_run sends, and prints the
 * deferred messages when its request ends with DRIVERS_SPOO_FLUSH_ERROR
 * (0x20e4e). */
void spoo_task(void)
{
	psos_supervisor();
	for (;;) {
		spoo_receive_error = (uint8_t)psos_request_x(spoo_exchange, &spoo_message, 0,
							     DRIVERS_SPOO_REQUEST_B);
		watchdog_spool++;
		if (spoo_receive_error == 0)
			spoo_message.function(spoo_message.arguments[0], spoo_message.arguments[1],
					      spoo_message.arguments[2]);
		else if (spoo_receive_error == DRIVERS_SPOO_FLUSH_ERROR)
			deferred_flush();
	}
}

/* Prints deferred_text's messages from deferred_start, each with CR LF
 * (0x20ebe). */
void deferred_flush(void)
{
	while (deferred_start < deferred_end) {
		int32_t count = printf("%s\r\n", deferred_text + deferred_start);

		if (count < 0)
			break;
		/* past the message and its NUL, CR LF not stored */
		deferred_start += count - 1;
	}
}

/* Prints a message through task_debug_message, with the task's name,
 * or else through debug_write by polling (0x22d4a). */
void debug_message(char *message, char with_task_name)
{
	if (with_task_name)
		task_debug_message(message);
	else
		debug_write(message);
}

/* Loads the FPGA's first configuration, under the kernel, and marks it
 * loaded if it took (0x22d70). */
void fpga_setup(void)
{
	task_dmessage("setup fpga\r\n");
	if (fpga_program(0, 1)) {
		pll.fpga_load_pending = 0;
		pll.fpga_loaded = 1;
	}
}

/* The FPGA's configurations, which fpga_program and fpga_verify take
 * by number, and the chip fpga_verify checks. */
enum {
	DRIVERS_FPGA_CONFIGURATIONS = 2,
	DRIVERS_FPGA_VERIFY_CHIP = 1,
};

/* Verifies a configuration, 0 or 1, against the FPGA's readback; false
 * for another (0x23980). */
uint8_t fpga_verify(uint8_t configuration, char with_task_name)
{
	if (configuration >= DRIVERS_FPGA_CONFIGURATIONS)
		return 0;
	return fpga_verify_chip(configuration, with_task_name, DRIVERS_FPGA_VERIFY_CHIP);
}

/* The health monitor's averages: ten samples, and the oscillator
 * current's filter, 0.9 of the old value and 0.1 of the new, which goes
 * to 0 below 1e-20. */
#define DRIVERS_MONITOR_SAMPLES 10
#define DRIVERS_FILTER_KEEP 0.9f
#define DRIVERS_FILTER_TAKE 0.1f
#define DRIVERS_FILTER_FLOOR 1e-20f

/* Puts a reading in a monitor's ring of ten and, once ten are in, keeps
 * their mean (0x23c5a). */
void monitor_average_add(adc_monitor *monitor, float reading)
{
	monitor->samples[monitor->sample_index++] = reading;
	if (monitor->sample_index >= DRIVERS_MONITOR_SAMPLES) {
		monitor->sample_index = 0;
		monitor->valid = 1;
	}
	if (monitor->valid) {
		float sum = 0.0f;

		for (uint32_t i = 0; i < DRIVERS_MONITOR_SAMPLES; i++)
			sum = float_add(sum, monitor->samples[i]);
		monitor->value = float_divide(sum, DRIVERS_MONITOR_SAMPLES);
	}
}

/* Filters a reading into a monitor, the first taken as it is
 * (0x23cc4). */
void monitor_filter_add(adc_monitor *monitor, float reading)
{
	if (!monitor->valid) {
		monitor->valid = 1;
		monitor->value = reading;
		return;
	}
	monitor->value = float_add(float_multiply(monitor->value, DRIVERS_FILTER_KEEP),
				   float_multiply(reading, DRIVERS_FILTER_TAKE));
	if (float_compare(DRIVERS_FILTER_FLOOR, monitor->value) > 0)
		monitor->value = 0.0f;
}

/* Counts a valid monitor's consecutive values outside minimum to
 * maximum, held at limit; true once the count passes the limit
 * (0x23d28). */
uint8_t monitor_out_of_range(adc_monitor *monitor, float minimum, float maximum, uint8_t limit)
{
	bool past_limit;

	if (!monitor->valid)
		return 0;
	if (float_compare(monitor->value, minimum) < 0 || float_compare(monitor->value, maximum) > 0)
		monitor->out_of_range_count++;
	else
		monitor->out_of_range_count = 0;
	past_limit = monitor->out_of_range_count > limit;
	if (past_limit)
		monitor->out_of_range_count = limit;
	return past_limit;
}

/* hmon_check_supplies' readings in the order hmon_supply_step takes
 * them, which is also adc_monitors' order and the ADC channels'
 * (structures.md, "The health monitor's ADC monitors"). */
enum {
	DRIVERS_STEP_TEMPERATURE,
	DRIVERS_STEP_DOUBLE_OVEN,
	DRIVERS_STEP_PLUS_15V,
	DRIVERS_STEP_OSCILLATOR_CURRENT,
	DRIVERS_STEP_5V,
	DRIVERS_STEP_OVEN,
	DRIVERS_STEP_MINUS_15V,
	DRIVERS_STEP_ANTENNA_CURRENT,
	DRIVERS_STEPS,
};

/* The supplies' tolerances and the readings outside one that count as
 * leaving it. */
#define DRIVERS_TOLERANCE_LIMIT 5
#define DRIVERS_5V_LEAST 4.5f
#define DRIVERS_5V_MOST 5.5f
#define DRIVERS_PLUS_15V_LEAST 14.0f
#define DRIVERS_PLUS_15V_MOST 16.0f
#define DRIVERS_MINUS_15V_LEAST -16.0f
#define DRIVERS_MINUS_15V_MOST -14.0f
#define DRIVERS_DOUBLE_OVEN_LEAST -1e37f
#define DRIVERS_DOUBLE_OVEN_MOST 6.8f

/* A supply whose tolerance hmon_check_supplies reports: its name, as
 * the messages give it, and the status happenings for leaving and
 * returning. */
typedef struct drivers_supply {
	adc_channel channel;
	char *out_message;
	char *out_log;
	char *in_message;
	char out_happening;
	char in_happening;
} drivers_supply;

/* Reports a supply leaving its tolerance, or returning to it, as
 * out_of_range says, if that changes the monitor's state. */
static void drivers_supply_report(const drivers_supply *supply, bool out_of_range)
{
	adc_monitor *monitor = &adc_monitors[supply->channel];

	if (!out_of_range) {
		if (monitor->out_of_tolerance) {
			task_debug_message(supply->in_message);
			status_happening(supply->in_happening);
			monitor->out_of_tolerance = 0;
		}
	} else if (!monitor->out_of_tolerance) {
		float value = adc_average(supply->channel);
		char text[80];

		log_write(supply->out_log, float_to_double(value));
		debug_sprintf(text, supply->out_message, float_to_double(value));
		task_debug_message(text);
		status_happening(supply->out_happening);
		monitor->out_of_tolerance = 1;
	}
}

static const drivers_supply drivers_5v = {
	ADC_5V, "5V is out of tolerance, value: %.2f\r\n", "5V is out of tolerance, value: %.2f",
	"5V is now within tolerance\r\n", ':', ';',
};
static const drivers_supply drivers_double_oven = {
	ADC_DOUBLE_OVEN, "Secondary oven voltage is out of tolerance, value: %.2f\r\n",
	"Secondary oven voltage is out of tolerance, value: %.2f",
	"Secondary oven voltage is now within tolerance\r\n", '>', '?',
};
static const drivers_supply drivers_plus_15v = {
	ADC_PLUS_15V, "15V is out of tolerance, value: %.2f\r\n",
	"15V is out of tolerance, value: %.2f", "15V is now within tolerance\r\n", '6', '7',
};
static const drivers_supply drivers_minus_15v = {
	ADC_MINUS_15V, "-15V is out of tolerance, value: %.2f\r\n",
	"-15V is out of tolerance, value: %.2f", "-15V is now within tolerance\r\n", '8', '9',
};

/* Takes one of the eight readings into its monitor, hmon_supply_step's,
 * and reports the supplies' and the double oven's tolerances; then
 * steps on (0x23d80). */
void hmon_check_supplies(void)
{
	adc_monitor *monitor = &adc_monitors[hmon_supply_step];

	switch (hmon_supply_step) {
	case DRIVERS_STEP_TEMPERATURE:
		monitor_average_add(monitor, read_adc_temp());
		break;
	case DRIVERS_STEP_DOUBLE_OVEN:
		monitor_average_add(monitor, read_adc_doven());
		/* the double oven's tolerance only while the outer oven is on */
		drivers_supply_report(&drivers_double_oven,
				      outer_oven_enabled &&
				      monitor_out_of_range(monitor, DRIVERS_DOUBLE_OVEN_LEAST,
							   DRIVERS_DOUBLE_OVEN_MOST,
							   DRIVERS_TOLERANCE_LIMIT));
		break;
	case DRIVERS_STEP_PLUS_15V:
		monitor_average_add(monitor, read_adc_p15v());
		drivers_supply_report(&drivers_plus_15v,
				      monitor_out_of_range(monitor, DRIVERS_PLUS_15V_LEAST,
							   DRIVERS_PLUS_15V_MOST,
							   DRIVERS_TOLERANCE_LIMIT));
		break;
	case DRIVERS_STEP_OSCILLATOR_CURRENT:
		monitor_filter_add(monitor, read_adc_osc_curr());
		break;
	case DRIVERS_STEP_5V:
		monitor_average_add(monitor, read_adc_5v());
		drivers_supply_report(&drivers_5v,
				      monitor_out_of_range(monitor, DRIVERS_5V_LEAST, DRIVERS_5V_MOST,
							   DRIVERS_TOLERANCE_LIMIT));
		break;
	case DRIVERS_STEP_OVEN:
		monitor_average_add(monitor, read_adc_oven());
		break;
	case DRIVERS_STEP_MINUS_15V:
		monitor_average_add(monitor, read_adc_m15v());
		drivers_supply_report(&drivers_minus_15v,
				      monitor_out_of_range(monitor, DRIVERS_MINUS_15V_LEAST,
							   DRIVERS_MINUS_15V_MOST,
							   DRIVERS_TOLERANCE_LIMIT));
		break;
	case DRIVERS_STEP_ANTENNA_CURRENT:
		monitor_average_add(monitor, read_adc_ant_curr());
		break;
	}
	if (++hmon_supply_step >= DRIVERS_STEPS)
		hmon_supply_step = 0;
}

/* What adc_average gives a channel while its monitor has no value, by
 * channel. */
static const float drivers_adc_defaults[] = {
	[ADC_TEMPERATURE] = 42.0f,
	[ADC_DOUBLE_OVEN] = 4.0f,
	[ADC_PLUS_15V] = 15.0f,
	[ADC_CURRENT] = 250.0f,
	[ADC_5V] = 5.0f,
	[ADC_OVEN] = 4.0f,
	[ADC_MINUS_15V] = -15.0f,
};

/* A channel's monitored value, or its default while it has none; 0
 * for a channel out of range (0x2410e). */
float adc_average(adc_channel channel)
{
	if (channel > ADC_MINUS_15V)
		return 0.0f;
	if (!adc_monitors[channel].valid)
		return drivers_adc_defaults[channel];
	return adc_monitors[channel].value;
}

/* The ovens' warm-up thresholds, in the readings' volts. */
#define DRIVERS_DOUBLE_OVEN_WARM 1.5f
#define DRIVERS_OVEN_WARM -2.0f

/* Whether the double oven reads above 1.5; nothing calls it
 * (0x241cc). */
bool double_oven_warmed_up(void)
{
	return float_compare(adc_average(ADC_DOUBLE_OVEN), DRIVERS_DOUBLE_OVEN_WARM) > 0;
}

/* Whether the oven reads below -2, powerup's warm-up test (0x241f4). */
bool oven_warmed_up(void)
{
	return float_compare(adc_average(ADC_OVEN), DRIVERS_OVEN_WARM) < 0;
}

/* The EFC's range: 20 bits, its DAC taking the top 16, and the clamps
 * and near-end bounds the health monitor keeps it to. */
enum {
	DRIVERS_EFC_MOST = 0xffff0,
	DRIVERS_EFC_DAC_SHIFT = 4,
	DRIVERS_EFC_NEAR_END_CHECKS = 5,
};
#define DRIVERS_EFC_MOST_FLOAT 1048560.0f
#define DRIVERS_EFC_NEAR_LOW 52428.0f	/* 5% of 2^20 */
#define DRIVERS_EFC_NEAR_HIGH 996132.0f	/* 95% */

/* Writes pll.efc, rounded, plus random_dither, clamped to 0 to
 * 0xffff0, to the EFC's DAC (0x2421c). */
void efc_write_dithered(void)
{
	int32_t efc = double_round_to_long(float_to_double(pll.efc));

	if (efc >= 0 && efc <= DRIVERS_EFC_MOST)
		efc += random_dither();
	if (efc < 0)
		efc = 0;
	else if (efc > DRIVERS_EFC_MOST)
		efc = DRIVERS_EFC_MOST;
	efc_write(efc >> DRIVERS_EFC_DAC_SHIFT);
}

/* The status happenings efc_clamp and hmon_check_efc report. */
enum {
	DRIVERS_HAPPENING_EFC_NEAR_END = '@',
	DRIVERS_HAPPENING_EFC_NOT_NEAR_END = 'A',
	DRIVERS_HAPPENING_EFC_CLAMPED = 'B',
	DRIVERS_HAPPENING_EFC_IN_RANGE = 'C',
};

/* Clamps an EFC to 0 to 1048560, reporting each clamp, and the first
 * value back in range (0x2426e). */
float efc_clamp(float efc)
{
	if (float_compare(0.0f, efc) > 0) {
		efc = 0.0f;
	} else if (float_compare(efc, DRIVERS_EFC_MOST_FLOAT) > 0) {
		efc = DRIVERS_EFC_MOST_FLOAT;
	} else {
		if (efc_range.clamped) {
			status_happening(DRIVERS_HAPPENING_EFC_IN_RANGE);
			efc_range.clamped = 0;
		}
		return efc;
	}
	status_happening(DRIVERS_HAPPENING_EFC_CLAMPED);
	efc_range.clamped = 1;
	return efc;
}

/* Clears efc_clamp's and hmon_check_efc's state (0x242de). */
void efc_clamp_reset(void)
{
	efc_range.clamped = 0;
	efc_range.near_end = 0;
	efc_range.near_end_checks = 0;
}

/* Reports pll.efc near the end of its range, outside 5% to 95%, once
 * five checks in a row find it there, and back inside at once
 * (0x242f0). */
void hmon_check_efc(void)
{
	if (float_compare(pll.efc, DRIVERS_EFC_NEAR_LOW) >= 0 &&
	    float_compare(pll.efc, DRIVERS_EFC_NEAR_HIGH) <= 0) {
		efc_range.near_end_checks = 0;
		if (efc_range.near_end) {
			efc_range.near_end = 0;
			status_happening(DRIVERS_HAPPENING_EFC_NOT_NEAR_END);
		}
		return;
	}
	if (++efc_range.near_end_checks > DRIVERS_EFC_NEAR_END_CHECKS) {
		efc_range.near_end_checks = DRIVERS_EFC_NEAR_END_CHECKS;
		if (!efc_range.near_end) {
			task_debug_message("EFC near end of range\r\n");
			status_happening(DRIVERS_HAPPENING_EFC_NEAR_END);
			efc_range.near_end = 1;
		}
	}
}

/* The watchdog: each task counts its passes, and the health monitor
 * services the MC68331's software watchdog (MC68331UM D.3.15, SWSR's
 * 0x55 then 0xaa) only while every count since the last report is
 * enough, and for eight reports after. */
enum {
	DRIVERS_WATCHDOG_GPS_LEAST = 21,
	DRIVERS_WATCHDOG_MONITOR_LEAST = 51,
	DRIVERS_WATCHDOG_GRACE = 8,
	DRIVERS_SWSR_FIRST = 0x55,
	DRIVERS_SWSR_SECOND = 0xaa,
};

/* Checks the tasks' watchdog counts, printing them if one is short,
 * and services the watchdog while the grace lasts; then clears them
 * (0x2437a). */
void watchdog_report(void)
{
	if (watchdog_clock != 0 && watchdog_gps >= DRIVERS_WATCHDOG_GPS_LEAST &&
	    watchdog_monitor >= DRIVERS_WATCHDOG_MONITOR_LEAST && watchdog_spool != 0 &&
	    watchdog_pll != 0)
		watchdog_countdown = DRIVERS_WATCHDOG_GRACE;
	else
		debug_printf("watchdog: clock: %4d gps: %4d pll: %4d mon: %4d spool %4d\r\n",
			     watchdog_clock, watchdog_gps, watchdog_pll, watchdog_monitor,
			     watchdog_spool);
	if (watchdog_countdown != 0)
		watchdog_countdown--;
	if (watchdog_countdown != 0) {
		uint16_t sr = enter_supervisor();

		sim.swsr = DRIVERS_SWSR_FIRST;
		sim.swsr = DRIVERS_SWSR_SECOND;
		drivers_sr_write(sr);
	}
	watchdog_clock = 0;
	watchdog_gps = 0;
	watchdog_pll = 0;
	watchdog_monitor = 0;
	watchdog_spool = 0;
}

/* Clears the last exception and the crash message (0x2442c). */
void exception_record_clear(void)
{
	reset_record_count();
	last_exception.pending = 0;
	crash_message[0] = 0;
}

/* An exception's vector word: its format in the top nibble, 0 for a
 * frame with no fault address. */
#define DRIVERS_VECTOR_FORMAT_MASK 0xf000

/* Logs why the last reset happened, with the time: the exception, the
 * fatal message, or, with neither and the watchdog's grace run out,
 * the watchdog counts; then clears both (0x24440). */
void log_reset_cause(void)
{
	char text[DRIVERS_CRASH_MESSAGE_SIZE];
	uint32_t date;
	time_of_day time;

	reset_record_count();
	get_date_time(&date, &time);
	text[0] = 0;
	if (last_exception.pending) {
		if ((last_exception.vector & DRIVERS_VECTOR_FORMAT_MASK) == 0)
			sprintf(text, "Exception: %s, vector %04X, PC = %08X, SR = %04X",
				last_exception.name, last_exception.vector, last_exception.pc,
				last_exception.sr);
		else
			sprintf(text, "Exception: %s, vector %04X, PC = %08X, SR = %04X, addr = %08X",
				last_exception.name, last_exception.vector, last_exception.pc,
				last_exception.sr, last_exception.address);
		reset_record_write(date, time, text);
	}
	if (crash_message[0] != 0) {
		strncpy(text, crash_message, DRIVERS_CRASH_MESSAGE_SIZE);
		text[DRIVERS_CRASH_MESSAGE_SIZE - 1] = 0;
		reset_record_write(date, time, text);
	}
	if (watchdog_countdown == 0 && crash_message[0] == 0 && !last_exception.pending) {
		sprintf(text, "Watchdog timeout: clk %1d gps %4d mon %4d pll %1d spl %1d",
			watchdog_clock, watchdog_gps, watchdog_monitor, watchdog_pll, watchdog_spool);
		reset_record_write(date, time, text);
	}
	last_exception.pending = 0;
	crash_message[0] = 0;
}

/* Clears the GPS 1PPS monitor's state (0x2458c). */
void gps_pps_monitor_reset(void)
{
	gps_pps_skipped_checks = 0;
	gps_pps_unconfirmed = 0;
	gps_pps_dead = 0;
	gps_pps_counting = 0;
}

/* Clears the internal 1PPS monitor's state (0x2470c). */
void internal_pps_monitor_reset(void)
{
	internal_pps_dead = 0;
	internal_pps_started = 0;
}

/* The internal 1PPS's count between checks: 1 to 3 is alive. */
enum {
	DRIVERS_PPS_LEAST = 1,
	DRIVERS_PPS_MOST = 3,
};

/* Whether the 1PPS monitors may check: no self-test running and the
 * FPGA, which counts the internal 1PPS, loaded. */
static bool drivers_pps_checkable(void)
{
	return !selftest.running && pll.fpga_loaded && !fpga_programming;
}

/* Checks the internal 1PPS by its count since the last check, from the
 * second call that may on, reporting it dead or back; returns whether
 * it checked (0x2471a). */
uint8_t internal_pps_check(void)
{
	bool checkable = drivers_pps_checkable();
	int8_t count;

	if (!checkable || !internal_pps_started) {
		if (checkable)
			internal_pps_started = 1;
		internal_pps_last_count = internal_pps_count;
		return 0;
	}
	count = internal_pps_count - internal_pps_last_count;
	internal_pps_last_count = internal_pps_count;
	if (count >= DRIVERS_PPS_LEAST && count <= DRIVERS_PPS_MOST) {
		if (internal_pps_dead) {
			task_dmessage("Internal 1pps signal is ok\r\n");
			internal_pps_dead = 0;
		}
	} else if (!internal_pps_dead) {
		task_dmessage("Internal 1pps signal is dead\r\n");
		debug_printf("count %d\r\n", count);
		internal_pps_dead = 1;
	}
	return 1;
}

/* The GPS 1PPS monitor: the GPT's pulse accumulator counts its rising
 * edges (PACTL's PAEN and PEDGE, MC68331UM D.2.7); each check wants 1
 * to 3 pulses since the last, and every fifth check's gate 8 to 12 in
 * all.  While the check is unconfirmed it skips 15 checks first. */
enum {
	DRIVERS_PACTL_COUNT_RISING = 0x50,
	DRIVERS_PPS_GATE_STEP = 2,
	DRIVERS_PPS_GATE = 10,
	DRIVERS_PPS_GATE_LEAST = 8,
	DRIVERS_PPS_GATE_MOST = 12,
	DRIVERS_PPS_SKIPS = 15,
};

/* Checks the GPS 1PPS by the pulses the GPT has counted, reporting it
 * dead, at the wrong frequency, or back; returns 0 when it could not
 * check (0x245a6). */
uint8_t gps_pps_check(void)
{
	uint8_t pulses;
	int8_t since_last;
	bool short_gate_bad = false;
	uint8_t dead;

	if (gps.pps_check_disabled) {
		gps_pps_unconfirmed = 1;
		gps_pps_skipped_checks = 0;
		gps_pps_dead = 1;
		gpt.pacnt = 0;
		gps_pps_gate = 0;
		gps_pps_last_count = 0;
		return 1;
	}
	if (gps_pps_unconfirmed && gps_pps_skipped_checks < DRIVERS_PPS_SKIPS) {
		gps_pps_skipped_checks++;
		return 1;
	}
	if (!drivers_pps_checkable() || !gps_pps_counting) {
		gpt.pacnt = 0;
		if (drivers_pps_checkable()) {
			gpt.pactl = DRIVERS_PACTL_COUNT_RISING;
			gps_pps_counting = 1;
		}
		gps_pps_gate = 0;
		gps_pps_last_count = 0;
		return 0;
	}
	pulses = gpt.pacnt;
	since_last = pulses - gps_pps_last_count;
	dead = gps_pps_dead;
	if (since_last < DRIVERS_PPS_LEAST || since_last > DRIVERS_PPS_MOST) {
		short_gate_bad = true;
		if (!dead) {
			task_dmessage("GPS 1pps signal is dead\r\n");
			debug_printf("short gate, count %d\r\n", since_last);
			dead = 1;
		}
	}
	gps_pps_gate += DRIVERS_PPS_GATE_STEP;
	if (gps_pps_gate == DRIVERS_PPS_GATE) {
		gpt.pacnt = 0;
		if (pulses < DRIVERS_PPS_GATE_LEAST || pulses > DRIVERS_PPS_GATE_MOST) {
			if (!dead) {
				task_dmessage("GPS 1pps signal frequency is incorrect\r\n");
				debug_printf("long gate, count %d\r\n", pulses);
				dead = 1;
			}
		} else if (!short_gate_bad) {
			if (dead) {
				task_dmessage("GPS 1pps signal is ok\r\n");
				dead = 0;
			}
			gps_pps_unconfirmed = 0;
		}
	} else if (gps_pps_gate < DRIVERS_PPS_GATE) {
		gps_pps_dead = dead;
		gps_pps_last_count = pulses;
		return 1;
	} else {
		gpt.pacnt = 0;
	}
	gps_pps_dead = dead;
	gps_pps_last_count = 0;
	gps_pps_gate = 0;
	return 1;
}

/* Sets the FPGA up again and reloads the counter's registers
 * (0x24976). */
void counter_reset(void)
{
	fpga_setup();
	counter_load_registers();
}

/* Clears both 1PPS monitors and the outer oven's flag (0x24984). */
void pps_monitors_reset(void)
{
	gps_pps_monitor_reset();
	internal_pps_monitor_reset();
	outer_oven_enabled = 0;
}
