/* What the Z3815A 4010's health monitor and port E link share with its
 * loop's stages (monitors.c, pll_stages.c): the GPS reference record's
 * valid states, and the puck's compensation and debug flags' values for
 * on.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* gps_reference_record.valid's 1: its state valid, or forced so. */
static const uint8_t MONITORS_REFERENCE_VALID = 1;

/* gps_reference_record.valid's 2: its state questionable (7 or 8), which
 * the loop holds good in its PLL stages and with which gps_pps_check
 * takes a good short gate as the 1PPS back. */
static const uint8_t MONITORS_REFERENCE_QUESTIONABLE = 2;

/* efc_compensation's value for on as the out of range log tests it, and
 * efc_compensation_debug's for on. */
static const int32_t PUCK_COMPENSATION_ON = 1;

static const uint8_t PUCK_DEBUG_ON = 1;
