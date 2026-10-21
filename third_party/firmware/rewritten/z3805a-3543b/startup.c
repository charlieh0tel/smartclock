/* The start-up's path, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"): primary_start, its no-op
 * hooks, the periodic interrupt's enable and the shutdown.  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

enum {
	/* The periodic interrupt, primary_user_66, 1024 a second: its
	 * vector, 66, and its level (docs/firmware/structures.md). */
	STARTUP_PERIODIC_VECTOR = 0x42,
	STARTUP_PERIODIC_LEVEL = 5,
};

/* The reset's path: the exception hook cleared, the watchdog serviced, the
 * QSM and GPT set up, system_init, and the kernel started with trap #6
 * (0x12d6e). */
void primary_start(uint32_t a, uint32_t b, uint32_t c)
{
	exception_hook_reset();
	sim.swsr = CPU_SWSR_FIRST;
	sim.swsr = CPU_SWSR_SECOND;
	qsm_init();
	gpt_init();
	start_nothing();
	system_init();
	psos_config_init();
	sim.swsr = CPU_SWSR_FIRST;
	sim.swsr = CPU_SWSR_SECOND;
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

/* Sets the SIM's PICR: the periodic interrupt at level 5, or at level 0,
 * off (0x12004). */
void periodic_interrupt_enable(char enable)
{
	if (enable)
		sim.picr = STARTUP_PERIODIC_LEVEL << PICR_PIRQL_SHIFT | STARTUP_PERIODIC_VECTOR;
	else
		sim.picr = STARTUP_PERIODIC_VECTOR;
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
