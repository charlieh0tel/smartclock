/* What the health monitor shares with the debug output and the log
 * (debug.c, log.c): the size of crash_message, which fatal_error fills.
 * The exported C includes the hand-written headers after its own
 * declarations and before the hand-written .c files, so every one of
 * them can use it. */

/* crash_message's size, which fatal_error fills and log_reset_cause
 * reads: an array size, so #define. */
#define MONITORS_CRASH_MESSAGE_SIZE 80
