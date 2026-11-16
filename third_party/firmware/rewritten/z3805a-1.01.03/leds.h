/* What the Z3805A 1.01.03's lamps share with its self-tests (leds.c,
 * selftest.c): the latch's lamp bits.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* The latch's lamps, the five bits lamps_compose composes. */
static const latch_bit LATCH_LAMPS = LATCH_ENABLED | LATCH_ACTIVE |
                                     LATCH_ALARM | LATCH_GPS_LOCK |
                                     LATCH_HOLDOVER;
