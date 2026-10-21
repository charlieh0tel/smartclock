/* The start-up's path and small task routines that lie among the clock's,
 * written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"). */

enum {
	/* The SIM's software watchdog is serviced by writing these two to
	 * SWSR in turn (MC68331UM, "Software Watchdog"). */
	CLOCK_WATCHDOG_SERVICE_FIRST = 0x55,
	CLOCK_WATCHDOG_SERVICE_SECOND = 0xaa,
	/* busy_wait's empty loops for each count (0x13432). */
	CLOCK_BUSY_WAIT_SPINS = 0x2d9,
	/* sleep_seconds: the kernel's ticks a second, and the shortest and
	 * longest pauses it takes, a tick and 2^21 seconds. */
	CLOCK_TICKS_PER_SECOND = 1024,
	CLOCK_SLEEP_LONGEST = 1 << 21,
	/* stack_measure's user stack's use and size, then its supervisor
	 * stack's. */
	CLOCK_STACK_USAGE_LONGS = 4,
	CLOCK_STACK_MESSAGE_SIZE = 80,
	/* The periodic interrupt, primary_user_66, 1024 a second: its
	 * vector, 66, and its level (docs/firmware/structures.md). */
	CLOCK_PERIODIC_VECTOR = 0x42,
	CLOCK_PERIODIC_LEVEL = 5,
};

/* The reset's path: the exception hook cleared, the watchdog serviced, the
 * QSM and GPT set up, system_init, and the kernel started with trap #6
 * (0x12d6e). */
void primary_start(uint32_t a, uint32_t b, uint32_t c)
{
	exception_hook_reset();
	sim.swsr = CLOCK_WATCHDOG_SERVICE_FIRST;
	sim.swsr = CLOCK_WATCHDOG_SERVICE_SECOND;
	qsm_init();
	gpt_init();
	start_nothing();
	system_init();
	psos_config_init();
	sim.swsr = CLOCK_WATCHDOG_SERVICE_FIRST;
	sim.swsr = CLOCK_WATCHDOG_SERVICE_SECOND;
	primary_trap_6();
}

/* Does nothing; primary_start calls it between gpt_init and system_init
 * (0x12bb2). */
void start_nothing(void)
{
}

/* Does nothing: the changed hook of most numeric and setting descriptors,
 * and the done hook of some (0x12af0). */
void scpi_param_hook_nothing(void)
{
}

/* The console's words ps, mem_rep and s_rep: the kernel's process, memory
 * and stack reports (0x12f86, 0x12f92, 0x12f9e). */
void ps_word(void)
{
	os_process_report(0);
}

void mem_rep_word(void)
{
	os_memory_report(0);
}

void s_rep_word(void)
{
	task_stack_report(0);
}

/* Prints a task's user and supervisor stacks' use and sizes
 * (0x132f4). */
void stack_usage_report(void *task)
{
	uint32_t usage[CLOCK_STACK_USAGE_LONGS];
	char message[CLOCK_STACK_MESSAGE_SIZE];

	if (stack_measure(task, usage) != 0)
		return;
	sprintf(message, "User: %5u of %5u b   Supv: %5u of %5u b\r\n", usage[0], usage[1], usage[2],
		usage[3]);
	task_debug_message(message);
}

/* Sets a task's priority, which the kernel changes by a step from the
 * present one, and returns the priority it had (0x1339e). */
uint8_t task_set_priority(uint32_t task, uint8_t priority)
{
	uint8_t old;

	psos_priority(task, 0, &old);
	psos_priority(task, (int16_t)(priority - old), &old);
	return old;
}

/* Spins 729 empty loops for each count, the count a word once it is not
 * 0 (0x13428). */
void busy_wait(uint32_t count)
{
	if (count == 0)
		return;
	uint16_t remaining = count;

	do {
		for (uint16_t spin = CLOCK_BUSY_WAIT_SPINS; spin != 0; spin--)
			__asm__ volatile("");
		remaining--;
	} while (remaining != 0);
}

/* Pauses a number of seconds, as kernel ticks, at least one; a negative
 * time pauses a tick, and one past 2^21 seconds that long; nothing calls
 * it (0x13448). */
void sleep_seconds(float seconds)
{
	if (float_compare(0.0f, seconds) > 0)
		seconds = 1.0f / CLOCK_TICKS_PER_SECOND;
	else if (float_compare(seconds, CLOCK_SLEEP_LONGEST) > 0)
		seconds = CLOCK_SLEEP_LONGEST;
	uint32_t ticks = float_to_unsigned(float_multiply(seconds, CLOCK_TICKS_PER_SECOND));

	psos_pause(ticks == 0 ? 1 : ticks);
}

/* Sets the SIM's PICR: the periodic interrupt at level 5, or at level 0,
 * off (0x12004). */
void periodic_interrupt_enable(char enable)
{
	if (enable)
		sim.picr = CLOCK_PERIODIC_LEVEL << PICR_PIRQL_SHIFT | CLOCK_PERIODIC_VECTOR;
	else
		sim.picr = CLOCK_PERIODIC_VECTOR;
}

/* Stops the interrupts and the serial devices: the periodic interrupt, the
 * GPT's interrupts but its prescaler, the QSPI, whose status is read
 * before it is cleared, the SCI's receiver and transmitter, and the DUART
 * (0x120aa). */
void shutdown(void)
{
	periodic_interrupt_enable(0);
	gpt.tmsk &= TMSK_CPROUT | TMSK_CPR_MASK;
	qsm.spcr1 = 0;
	qsm.spcr2 = 0;
	qsm.spcr3 = 0;
	(void)qsm.spsr;
	qsm.spsr = 0;
	qsm.sccr1 = 0;
	duart_stop();
}
