/* What pllc and the SCPI task share (pllc.c, scpi_requests.c): the
 * event pllc signals the SCPI task with when a request is done.  The
 * exported C includes the hand-written headers after its own
 * declarations and before the hand-written .c files, so every one of
 * them can use it. */

/* The event the SCPI task's request_and_wait (0x29e7e) waits for. */
static const uint32_t PLLC_DONE_EVENT = 0x20;

/* The ticks request_and_wait's send_x_retry pauses between tries
 * (0x29e8e, 0x29eb0). */
static const uint32_t PLLC_RETRY_TICKS = 5;
