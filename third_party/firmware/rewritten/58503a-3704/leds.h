/* What the 58503A's lamps share with its self-tests (leds.c,
 * selftest.c): the latch's lamp bits.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* The latch's lamps, which latch_lamps_write writes. */
static const latch_bit LATCH_LAMPS =
    LATCH_ALARM | LATCH_HOLDOVER | LATCH_GPS_LOCK;
