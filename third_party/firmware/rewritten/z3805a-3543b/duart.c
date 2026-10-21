/* The MC68681 DUART's drivers, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"): channel A,
 * the GPS receiver's link, and channel B, the host port, with their
 * rings and interrupts; console_write and console_read; the SCPI port's
 * open and close; the output port and the GP port's select; and the
 * DUART's self-tests.  The exported C includes this file after its own,
 * so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype.  The drivers' state is
 * docs/firmware/structures.md's "The DUART's shadows", "DUART channel
 * A's driver" and "DUART channel B's driver"; the registers are
 * mc68681.h's.  A kernel service's error is its result's low byte,
 * which is all the image tests.
 *
 * The files are included in sorted order, and this one comes before the
 * other drivers', so it also holds what they share: the control
 * characters, flow byte, thresholds and baud and parity codes sci.c
 * uses, the line reader duart_gets, which drtb_gets and sci_gets share,
 * and the status register's helpers.  The image's critical sections
 * move to and from the status register itself, which the decompiler's C
 * leaves out; here they are duart_sr_read and duart_sr_write, which
 * monitors.c, qspi.c, sci.c and spool.c use too. */

/* The control characters the drivers act on. */
enum {
	DUART_CTRL_C = 0x03,
	DUART_BACKSPACE = 0x08,
	DUART_TAB = 0x09,
	DUART_LF = 0x0a,
	DUART_CR = 0x0d,
	DUART_XON = 0x11,
	DUART_XOFF = 0x13,
	DUART_CTRL_X = 0x18,	/* cancels the line */
	DUART_FIRST_PRINTABLE = 0x20,
	DUART_CARET = '^',	/* echoes a control character as ^ and its letter */
	DUART_CONTROL_TO_LETTER = 0x40,
	DUART_ASCII_MASK = 0x7f,
	DUART_MARK_BIT = 0x80,
};

/* A driver's flow byte (duartb_driver.flow, sci_driver.flow). */
enum {
	DUART_FLOW_XOFF_SENT = 0x20,
	DUART_FLOW_XOFF_RECEIVED = 0x80,	/* transmission held */
};

/* The signal duartb_transmit_interrupt gives the SCPI task when an
 * asynchronous send ends; psos-signals.h has no name for it. */
#define DUART_SIGNAL_ASYNC_DONE 0x0020

/* XON/XOFF thresholds, in free bytes of a 256-byte ring. */
enum {
	DUART_XOFF_BELOW_FREE = 0x40,	/* the interrupt sends XOFF with fewer free */
	DUART_XON_ABOVE_FREE = 0x10,	/* a read sends XON once more are free */
};

/* The kernel's I/O devices, the device in the high byte, the minor in
 * the low (kernel_dispatch_io, 0x14c36). */
enum {
	DUART_DEVICE_DUARTB = 0x000,
	DUART_DEVICE_DUARTB_MINOR_1 = 0x001,	/* drtb_release_for_timecode's close */
};

/* The 68681's channel B settings (MC68681 Table 4-5). */
enum {
	DUART_MR1_KEEP = MR1_RXRTS | MR1_RXIRQ_FFULL | MR1_ERROR_BLOCK,
	DUART_MR1_SEVEN_BITS = 2,
	DUART_MR1_EIGHT_BITS = 3,
	DUART_MR2_ONE_STOP_BIT = 0x07,
	DUART_MR2_TWO_STOP_BITS = 0x0f,
	DUART_CSR_CLOCK_BITS = 4,	/* the receiver's clock in the high nibble, the transmitter's in the low */
	/* baud-rate set 2's clock selects, which ACR_BRG_SET_2 picks */
	DUART_CSR_1200 = 0x6,
	DUART_CSR_2400 = 0x8,
	DUART_CSR_9600 = 0xb,
	DUART_CSR_19200 = 0xc,
	DUART_RECEIVE_ERRORS = SR_RECEIVED_BREAK | SR_FRAMING_ERROR | SR_PARITY_ERROR,
};

/* The serial settings' baud codes (duartb_settings_record.baud). */
enum {
	DUART_BAUD_1200,
	DUART_BAUD_2400,
	DUART_BAUD_9600,
	DUART_BAUD_19200,
};

/* The serial settings' parity codes (duartb_settings_record.parity,
 * sci_settings_record.parity). */
enum {
	DUART_PARITY_NONE,
	DUART_PARITY_EVEN,
	DUART_PARITY_ODD,
	DUART_PARITY_ONE,	/* the SCI's only: mark parity */
};

/* How long a reconfiguration waits for the transmitter to empty, in
 * ticks. */
#define DUART_DRAIN_PAUSES 100

/* The GP port's bits 2 to 4, gp_port_select's selection. */
enum {
	DUART_GP_SELECT_SHIFT = 2,
	DUART_GP_SELECT_MASK = 0x1c,
	DUART_GP_SELECT_MAX = 6,
};

/* The output port's bits, by duart_shadow.output_reset: bits 0 and 1
 * each a routine's own, bits 2 to 7 duart_output_bits'. */
enum {
	DUART_OP0 = 0x01,
	DUART_OP1 = 0x02,
	DUART_OP_BITS = 0xfc,
};

/* The status register's interrupt mask, set_interrupt_mask's argument,
 * which it takes from bits 10 to 8 (0x1338e). */
enum {
	DUART_SR_IPL_MASK = 0x700,
	DUART_SR_IPL_2 = 0x200,
	DUART_SR_IPL_3 = 0x300,
	DUART_SR_IPL_4 = 0x400,
	DUART_SR_IPL_6 = 0x600,
	DUART_SR_IPL_7 = 0x700,
	/* what the output-port routines pass, meaning level 6 but taken as
	 * level 0, so that they leave interrupts enabled */
	DUART_SR_IPL_6_UNSHIFTED = 6,
};

/* The status register, which C has no name for: the image's critical
 * sections save it, change its interrupt mask in place with ori and
 * andi, and move the saved word back, all in supervisor mode. */
static inline uint16_t duart_sr_read(void)
{
	uint16_t sr;

	__asm__ volatile ("move.w %%sr,%0" : "=d"(sr) : : "memory");
	return sr;
}

static inline void duart_sr_write(uint16_t sr)
{
	__asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory", "cc");
}

/* Raises the interrupt mask by ORing bits into it; returns the status
 * register as it was. */
static inline uint16_t duart_interrupts_raise(uint16_t mask)
{
	uint16_t saved = duart_sr_read();

	duart_sr_write(saved | mask);
	return saved;
}

/* Sets the interrupt mask to a level, as ori and then andi do; returns
 * the status register as it was. */
static inline uint16_t duart_interrupts_at(uint16_t level)
{
	uint16_t saved = duart_interrupts_raise(level);

	duart_sr_write(duart_sr_read() & ~(DUART_SR_IPL_MASK & ~level));
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
	if (selection > DUART_GP_SELECT_MAX)
		selection = 0;
	gpt.portgp &= ~DUART_GP_SELECT_MASK;
	gpt.portgp |= selection << DUART_GP_SELECT_SHIFT;
	return selection << DUART_GP_SELECT_SHIFT;
}

/* DUART channel B's mode registers from the serial settings: parity,
 * data bits and stop bits (0x1d594). */
void duartb_mode_from_settings(void)
{
	duart_shadow.mr1b &= DUART_MR1_KEEP;
	if (duartb_settings.parity == DUART_PARITY_NONE) {
		duartb.parity = 0;
		duart_shadow.mr1b |= MR1_PARITY_NONE;
	} else {
		duartb.parity = 1;
		if (duartb_settings.parity == DUART_PARITY_ODD)
			duart_shadow.mr1b |= MR1_PARITY_ODD;
	}
	duart_shadow.mr1b |= duartb_settings.eight_bits ? DUART_MR1_EIGHT_BITS : DUART_MR1_SEVEN_BITS;
	duart_shadow.mr2b = (duart_shadow.mr2b & ~MR2_STOP_MASK) |
		(duartb_settings.two_stop_bits ? DUART_MR2_TWO_STOP_BITS : DUART_MR2_ONE_STOP_BIT);
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
	case DUART_BAUD_1200:
		clock = DUART_CSR_1200;
		break;
	case DUART_BAUD_2400:
		clock = DUART_CSR_2400;
		break;
	case DUART_BAUD_9600:
		clock = DUART_CSR_9600;
		break;
	default:
		clock = DUART_CSR_19200;
		break;
	}
	clock |= clock << DUART_CSR_CLOCK_BITS;
	duart_shadow.csrb = clock;
	duart.b.sr_csr = clock;
	duartb_applied.baud = duartb_settings.baud;
}

/* Waits for DUART channel B's transmitter and writes a character;
 * returns the status bit it waited on (0x1d680). */
uint8_t duartb_putc_polled(uint8_t c)
{
	uint16_t sr = duart_interrupts_at(DUART_SR_IPL_2);
	uint8_t status;

	do
		status = duart.b.sr_csr;
	while ((status & SR_TXRDY) == 0);
	duart.b.rb_tb = c;
	duart_sr_write(sr);
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
	sr = duart_interrupts_raise(DUART_SR_IPL_6);
	if (!duartb.reconfiguring) {
		duart_shadow.imr |= IMR_TXRDYB;
		duart.isr_imr = duart_shadow.imr;
	}
	duart_sr_write(sr);
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
	if ((duartb.flow & DUART_FLOW_XOFF_SENT) != 0 &&
	    (uint8_t)(duartb_ring.read - duartb_ring.write) > DUART_XON_ABOVE_FREE) {
		duartb.flow &= ~DUART_FLOW_XOFF_SENT;
		if (duartb.xon_xoff)
			drtb_putc(DUART_XON);
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
static int32_t duart_gets(uint8_t *buffer, int32_t length, uint8_t (*getc)(void),
			    putc_function *const *echo, uint8_t *last_terminator,
			    uint8_t *receive_errors, uint8_t *read_errors,
			    char *error_message, bool top_bit_control)
{
	int32_t left = length - 1;
	int32_t count = left;
	uint8_t *next = buffer;

	while (left != 0) {
		uint8_t character = getc();
		bool printable = top_bit_control ? (int8_t)character >= DUART_FIRST_PRINTABLE
						 : character >= DUART_FIRST_PRINTABLE;

		if (printable || character == DUART_TAB) {
			*last_terminator = character;
			*next++ = character;
			left--;
			(*echo)(character);
			continue;
		}
		switch (character) {
		case DUART_BACKSPACE:
			if (count != left) {
				next--;
				left++;
				(*echo)(character);
			}
			break;
		case DUART_LF:
		case DUART_CR:
			if ((*last_terminator == DUART_CR && character == DUART_LF) ||
			    (*last_terminator == DUART_LF && character == DUART_CR)) {
				*last_terminator = 0;
			} else {
				(*echo)(DUART_CR);
				(*echo)(DUART_LF);
				count -= left;
				left = 0;
				*last_terminator = character;
			}
			break;
		case DUART_CTRL_X:
			if (count != left) {
				next = buffer;
				left = count;
				(*echo)(DUART_CR);
				(*echo)(DUART_LF);
			}
			break;
		default:
			(*echo)(DUART_CARET);
			(*echo)(character + DUART_CONTROL_TO_LETTER);
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
	return duart_gets(buffer, length, drtb_getc, &duartb.echo, &duartb.last_terminator,
			    &duartb.receive_errors, &duartb.read_errors, "DRTB receiver error\r\n",
			    false);
}

/* Resets DUART channel B and applies the serial settings: mode, baud
 * rate, flow control and echo (0x1dc7e). */
void duartb_reconfigure(void)
{
	uint16_t sr = duart_interrupts_raise(DUART_SR_IPL_2);

	duartb.reconfiguring = 1;
	duart_shadow.imr &= ~(IMR_RXRDYB | IMR_TXRDYB);
	duart.isr_imr = duart_shadow.imr;
	duart.b.cr = CR_RX_DISABLE;
	duart_sr_write(sr);
	for (int32_t pauses = DUART_DRAIN_PAUSES; pauses > 0 && (duart.b.sr_csr & SR_TXEMT) == 0;
	     pauses--)
		psos_pause(1);
	duart.b.cr = CR_RESET_RECEIVER;
	duart.b.cr = CR_RESET_TRANSMITTER;
	sr = duart_interrupts_raise(DUART_SR_IPL_2);
	duartb_mode_from_settings();
	duartb_baud_from_settings();
	duart.b.cr = CR_TX_ENABLE;
	if (!duartb_settings.xon_xoff) {
		if (duartb.xon_xoff && (duartb.flow & DUART_FLOW_XOFF_SENT) != 0)
			duartb_putc_polled(DUART_XON);
		duartb.xon_xoff = 0;
		duartb.flow &= ~(DUART_FLOW_XOFF_SENT | DUART_FLOW_XOFF_RECEIVED);
	} else {
		if (!duartb.xon_xoff && (duartb.flow & DUART_FLOW_XOFF_SENT) != 0)
			duartb_putc_polled(DUART_XOFF);
		duartb.xon_xoff = 1;
	}
	duartb.echo = duartb_settings.echo ? drtb_putc : duartb_echo_none;
	duart_shadow.imr |= IMR_RXRDYB | IMR_TXRDYB;
	duart.isr_imr = duart_shadow.imr;
	duart.b.cr = CR_RX_ENABLE;
	duartb.reconfiguring = 0;
	duart_sr_write(sr);
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

	psos_io_write(DUART_DEVICE_DUARTB, s, &length);
	return length;
}

/* Reads from the console, device 0, and its last read errors
 * (0x1debc). */
uint32_t console_read(void *buffer, uint32_t length, uint8_t *errors)
{
	psos_io_read(DUART_DEVICE_DUARTB, buffer, &length);
	if (errors != 0)
		*errors = duartb.read_errors;
	return length;
}

/* Opens the SCPI port, device 0 (0x1dee6). */
void scpi_port_open(void)
{
	psos_io_open(DUART_DEVICE_DUARTB, 0);
}

/* Closes the SCPI port, device 0 (0x1def4). */
void scpi_port_close(void)
{
	psos_io_close(DUART_DEVICE_DUARTB);
}

/* The done hook of channel B's setting descriptors and its PRESet:
 * reconfigures device 0 (0x1df00). */
void duartb_settings_done(void)
{
	psos_io_control(DUART_DEVICE_DUARTB, 0, 0);
}

/* Writes a string to channel B by polling, in supervisor mode; returns
 * its length (0x1df12). */
int32_t console_write_polled(char *s)
{
	int32_t count = 0;
	uint16_t sr = enter_supervisor();

	for (; *s != 0; s++, count++)
		duartb_putc_polled(*s);
	duart_sr_write(sr);
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
	if ((duartb.flow & DUART_FLOW_XOFF_RECEIVED) == 0 && !drtb_try_lock_write()) {
		duartb.async_allowed = 1;
		psos_io_close(DUART_DEVICE_DUARTB_MINOR_1);
		return 1;
	}
	return 0;
}

/* Sets or clears one of the output port's own bits, with interrupts
 * meant to be held off (DUART_SR_IPL_6_UNSHIFTED): the register holds each pin's complement, so a bit kept set in
 * output_reset is reset in the port (MC68681 4.2.12). */
static void duart_output_bit_set(uint8_t bit, char on)
{
	uint16_t sr = set_interrupt_mask(DUART_SR_IPL_6_UNSHIFTED);

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
	duart_output_bit_set(DUART_OP0, on);
}

/* Sets or clears the output port's bit 1 (0x1e02c). */
void duart_op1_set(char on)
{
	duart_output_bit_set(DUART_OP1, on);
}

/* Resets the output port's bits 2 to 7 in reset and sets those in set,
 * as duart_output_bit_set does (0x1e126). */
void duart_output_bits(uint8_t reset, uint8_t set)
{
	uint16_t sr = set_interrupt_mask(DUART_SR_IPL_6_UNSHIFTED);
	uint8_t resets = (reset | duart_shadow.output_reset) & DUART_OP_BITS;

	duart.stop_ropbc = resets;
	duart_shadow.output_reset = (reset & DUART_OP_BITS | duart_shadow.output_reset) &
		~(set & DUART_OP_BITS);
	duart.start_sopbc = ~duart_shadow.output_reset & DUART_OP_BITS;
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
	sr = set_interrupt_mask(DUART_SR_IPL_6);
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
	DUART_UART_IVR_FAILED = 0x01,
	DUART_UART_COUNTER_FAILED = 0x02,
	DUART_UART_A_MISMATCH = 0x04,
	DUART_UART_A_TIMEOUT = 0x08,
	DUART_UART_B_MISMATCH = 0x10,
	DUART_UART_B_TIMEOUT = 0x20,
};

/* How long the loopback test spins for its string to come back. */
#define DUART_LOOPBACK_SPINS 10000

/* A loopback test's channel: its registers and mode shadow, its
 * interrupts, the flag that keeps its driver off the transmitter, the
 * flag that hands its interrupts to the test, and its failure bit. */
typedef struct duart_loopback_channel {
	volatile duart_channel *registers;
	uint8_t *mr2;
	uint8_t interrupts;
	uint8_t *held;
	uint8_t *loopback_test;
	int16_t timeout_bit;
} duart_loopback_channel;

/* Puts a channel in local loopback and has the interrupt routines send
 * "DUART test string" through it, checking each character back; sets
 * the channel's failure bit if it does not all come back
 * (duart_loopback_test_a and duart_loopback_test_b). */
static void duart_loopback_test(const duart_loopback_channel *channel)
{
	uint16_t sr = set_interrupt_mask(DUART_SR_IPL_7);
	int32_t spins;

	*channel->held = 1;
	duart_shadow.imr &= ~channel->interrupts;
	duart.isr_imr = duart_shadow.imr;
	channel->registers->cr = CR_RX_DISABLE;
	set_interrupt_mask(sr);
	for (int32_t pauses = DUART_DRAIN_PAUSES;
	     pauses > 0 && (channel->registers->sr_csr & SR_TXEMT) == 0; pauses--)
		psos_pause(1);
	sr = set_interrupt_mask(DUART_SR_IPL_7);
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
	while (spins < DUART_LOOPBACK_SPINS && *loopback_expect_next != 0)
		spins++;
	sr = set_interrupt_mask(DUART_SR_IPL_7);
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
	const duart_loopback_channel channel = {
		&duart.a, &duart_shadow.mr2a, IMR_RXRDYA | IMR_TXRDYA,
		&duarta.held, &duarta.loopback_test, DUART_UART_A_TIMEOUT,
	};

	duart_loopback_test(&channel);
}

/* The loopback test on channel B, the host port (0x1e41a). */
void duart_loopback_test_b(void)
{
	const duart_loopback_channel channel = {
		&duart.b, &duart_shadow.mr2b, IMR_RXRDYB | IMR_TXRDYB,
		&duartb.reconfiguring, &duartb.loopback_test, DUART_UART_B_TIMEOUT,
	};

	duart_loopback_test(&channel);
}

/* The counter test: the 68681's counter, preloaded with 0xffff, counts
 * down from the crystal over 16 for ten ticks, at the highest
 * priority. */
enum {
	DUART_COUNTER_PRELOAD = 0xff,	/* each half */
	DUART_ACR_COUNTER_CRYSTAL_16 = 0x30,	/* MC68681 Table 4-4 */
	DUART_ACR_COUNTER_TXCB = 0x20,
	DUART_COUNTER_PAUSES = 10,
	DUART_COUNTER_LEAST = 0xf533,
	DUART_COUNTER_MOST = 0xf8cc,
	DUART_PRIORITY_HIGHEST = 0xff,
	DUART_BYTE_BITS = 8,
};

/* Preloads the DUART's counter with 0xffff, both halves, shadowed. */
static void duart_counter_preload(void)
{
	duart_shadow.ctur = DUART_COUNTER_PRELOAD;
	duart_shadow.ctlr = DUART_COUNTER_PRELOAD;
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
	duart_counter_preload();
	psos_priority(0, DUART_PRIORITY_HIGHEST, &priority);
	duart_shadow.acr = (duart_shadow.acr & ~ACR_COUNTER_MASK) | DUART_ACR_COUNTER_CRYSTAL_16;
	duart.ipcr_acr = duart_shadow.acr;
	(void)duart.start_sopbc;
	psos_pause(DUART_COUNTER_PAUSES);
	(void)duart.stop_ropbc;
	upper = duart.cur_ctur;
	lower = duart.clr_ctlr;
	duart_shadow.acr = (duart_shadow.acr & ~ACR_COUNTER_MASK) | DUART_ACR_COUNTER_TXCB;
	duart.ipcr_acr = duart_shadow.acr;
	duart_counter_preload();
	task_set_priority(0, priority);
	count = upper << DUART_BYTE_BITS | lower;
	if (count > DUART_COUNTER_MOST || count < DUART_COUNTER_LEAST)
		selftest.uart |= DUART_UART_COUNTER_FAILED;
}

/* Values the self-tests write to the interrupt vector register and
 * read back, and the vector it is left with, user vector 67, whose
 * handler is primary_user_67. */
enum {
	DUART_IVR_PATTERN_1 = 0x55,
	DUART_IVR_PATTERN_2 = 0xaa,
	DUART_IVR_VECTOR = 0x43,
};

/* Whether the interrupt vector register holds a value written to it. */
static bool duart_ivr_holds(uint8_t value)
{
	duart.ivr = value;
	return duart.ivr == value;
}

/* Tests the interrupt vector register with two patterns, then sets the
 * vector. */
static void duart_ivr_test(void)
{
	if (!duart_ivr_holds(DUART_IVR_PATTERN_1))
		selftest.uart |= DUART_UART_IVR_FAILED;
	if (!duart_ivr_holds(DUART_IVR_PATTERN_2))
		selftest.uart |= DUART_UART_IVR_FAILED;
	duart.ivr = DUART_IVR_VECTOR;
}

/* The DUART self-test: its interrupt vector register, then its counter
 * and both channels' loopbacks (0x1e650). */
void duart_test(void)
{
	uint16_t sr;

	selftest.uart = 0;
	sr = set_interrupt_mask(DUART_SR_IPL_7);
	duart_ivr_test();
	set_interrupt_mask(sr);
	if (selftest.uart == 0) {
		duart_counter_test();
		duart_loopback_test_a();
		duart_loopback_test_b();
	}
}

/* The loopback test's receive: checks a character against the next
 * expected. */
static void duart_test_receive(volatile duart_channel *channel, int16_t mismatch_bit)
{
	char expected = *loopback_expect_next++;

	/* a signed char against the byte read: one with its top bit set never matches */
	if (channel->rb_tb != expected)
		selftest.uart |= mismatch_bit;
}

/* The loopback test's receive on channel A (0x1e6be). */
void duart_test_rx_a(void)
{
	duart_test_receive(&duart.a, DUART_UART_A_MISMATCH);
}

/* The loopback test's receive on channel B (0x1e6e2). */
void duart_test_rx_b(void)
{
	duart_test_receive(&duart.b, DUART_UART_B_MISMATCH);
}

/* The loopback test's transmit: sends the next character, or at the
 * string's end turns the transmit interrupt off. */
static void duart_test_transmit(volatile duart_channel *channel, uint8_t interrupt)
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
	duart_test_transmit(&duart.a, IMR_TXRDYA);
}

/* The loopback test's transmit on channel B (0x1e736). */
void duart_test_tx_b(void)
{
	duart_test_transmit(&duart.b, IMR_TXRDYB);
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

		duartb.receive_errors |= duart.b.sr_csr & DUART_RECEIVE_ERRORS;
		character = duart.b.rb_tb;
		if (!duartb_applied.eight_bits)
			character &= DUART_ASCII_MASK;
		if (character == DUART_CTRL_C && duartb.read_token.task == task_console) {
			if (duartb.read_token.task != 0 &&
			    (uint8_t)psos_signal_v(duartb.read_token.task, SIGINTR) != 0)
				fatal_error("DUARTB isr signaling SIGINTR");
			if (duartb.xon_xoff)
				duartb.flow &= ~DUART_FLOW_XOFF_RECEIVED;
			if (!duartb.reconfiguring) {
				duart_shadow.imr |= IMR_TXRDYB;
				duart.isr_imr = duart_shadow.imr;
			}
		} else if (character == DUART_XON) {
			if (duartb.xon_xoff) {
				duartb.flow &= ~DUART_FLOW_XOFF_RECEIVED;
				if (!duartb.reconfiguring) {
					duart_shadow.imr |= IMR_TXRDYB;
					duart.isr_imr = duart_shadow.imr;
				}
			}
		} else if (character == DUART_XOFF) {
			if (duartb.xon_xoff) {
				duartb.flow |= DUART_FLOW_XOFF_RECEIVED;
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
			if ((uint8_t)(duartb_ring.read - duartb_ring.write) < DUART_XOFF_BELOW_FREE) {
				duartb.flow |= DUART_FLOW_XOFF_SENT;
				if (duartb.xon_xoff)
					duartb_putc_polled(DUART_XOFF);
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
		   (duartb.flow & DUART_FLOW_XOFF_RECEIVED) == 0) {
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
			psos_signal_v(task_scpi, DUART_SIGNAL_ASYNC_DONE);
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
#define DUART_LOOPBACK_CHECK_SPINS 3000

/* At reset, before interrupts: sends "DUART" through channel B in local
 * loopback by polling and checks each character back (0x1ebe4). */
void duartb_loopback_check(void)
{
	char *text = "DUART";

	(void)duart.b.mr;
	duart.b.mr = MR2_MODE_LOCAL_LOOPBACK | DUART_MR2_ONE_STOP_BIT;
	loopback_send_next = (uint8_t *)text;
	loopback_expect_next = text;
	duart.b.cr = CR_TX_ENABLE | CR_RX_ENABLE;
	duart.b.rb_tb = *loopback_send_next++;
	while (*loopback_expect_next != 0 && selftest.uart == 0) {
		for (int32_t spins = DUART_LOOPBACK_CHECK_SPINS;
		     spins > 0 && (duart.b.sr_csr & SR_RXRDY) == 0; spins--)
			;
		if ((duart.b.sr_csr & SR_RXRDY) != 0) {
			char expected = *loopback_expect_next++;

			if (duart.b.rb_tb != expected)
				selftest.uart |= DUART_UART_B_MISMATCH;
			if (*loopback_send_next != 0)
				duart.b.rb_tb = *loopback_send_next++;
		} else {
			selftest.uart |= DUART_UART_B_TIMEOUT;
		}
	}
	duart.b.cr = CR_RESET_RECEIVER;
	duart.b.cr = CR_RESET_TRANSMITTER;
	(void)duart.b.mr;
	duart.b.mr = DUART_MR2_ONE_STOP_BIT;
}

/* At reset: tests the DUART's interrupt vector register and sets
 * channel B up for the console, 9600 8N1, then checks it in loopback
 * (0x1ecae). */
void duart_early_init(void)
{
	selftest.uart = 0;
	duart_ivr_test();
	if (selftest.uart == 0) {
		duart.b.cr = CR_RESET_RECEIVER;
		duart.b.cr = CR_RESET_TRANSMITTER;
		duart.b.cr = CR_RESET_MR_POINTER;
		duart.b.mr = MR1_PARITY_NONE | DUART_MR1_EIGHT_BITS;
		duart.b.mr = DUART_MR2_ONE_STOP_BIT;
		duart.b.sr_csr = DUART_CSR_9600 << DUART_CSR_CLOCK_BITS | DUART_CSR_9600;
		duart.ipcr_acr = ACR_BRG_SET_2 | DUART_ACR_COUNTER_TXCB;
		duartb_loopback_check();
	}
}
