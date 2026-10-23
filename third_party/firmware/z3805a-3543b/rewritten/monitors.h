/* What the health monitor shares with the debug output, the log and the
 * self-tests (debug.c, log.c, selftest.c): the size of crash_message,
 * which fatal_error fills, the supplies' tolerances and the 1PPS
 * counts.
 * The exported C includes the hand-written headers after its own
 * declarations and before the hand-written .c files, so every one of
 * them can use it. */

/* crash_message's size, which fatal_error fills and log_reset_cause
 * reads: an array size, so #define. */
#define MONITORS_CRASH_MESSAGE_SIZE 80

/* The supplies' tolerances, which hmon_check_supplies and supplies_test
 * apply: 10 percent of 5 V, 1 V of 15. */
static const float MONITORS_PLUS_5V_LEAST = 4.5f;

static const float MONITORS_PLUS_5V_MOST = 5.5f;

static const float MONITORS_PLUS_15V_LEAST = 14.0f;

static const float MONITORS_PLUS_15V_MOST = 16.0f;

static const float MONITORS_MINUS_15V_LEAST = -16.0f;

static const float MONITORS_MINUS_15V_MOST = -14.0f;

/* A 1PPS's count since the last look that is alive, 1 to 3:
 * internal_pps_check's and gps_pps_check's each check (0x2471a,
 * 0x245a6), and internal_pps_test's over its 1024 ticks (0x184a4). */
static const int8_t MONITORS_PPS_LEAST = 1;

static const int8_t MONITORS_PPS_MOST = 3;
