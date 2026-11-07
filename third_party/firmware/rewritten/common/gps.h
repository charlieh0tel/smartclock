/* What of gps.h the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"), and what the Oncore's and the
 * reports' files share with each image's own (gps_oncore.c,
 * gps_reports.c): a message's ID length, the store of a received byte
 * and the scale of thousandths; each image's own
 * rewritten/gps.h holds the rest. */

static const uint8_t GPS_FIRST_SATELLITE = 1;

static const uint8_t GPS_LAST_SATELLITE = 0x20;

static const uint8_t GPS_FIRST_PSEUDOLITE = 0x21;

static const uint8_t GPS_LAST_PSEUDOLITE = 0x25;

/* A survey's progress in percent, the least shown once it starts and
 * the most until it is complete (survey_progress_text's; the status
 * screen's, status_screen_position's). */
static const float GPS_SURVEY_PERCENT = 100.0f;

static const float GPS_SURVEY_LEAST = 0.1f;

static const float GPS_SURVEY_MOST_UNFINISHED = 99.9f;

/* An angle's milliarcseconds per degree, per minute and per second. */
static const int32_t GPS_MAS_PER_DEGREE = 3600000;

static const int32_t GPS_MAS_PER_MINUTE = 60000;

static const double GPS_MAS_PER_SECOND = 1000.0;

/* gps_request_post's code for a request given as a script step, whose
 * address its arguments' first long holds, in place of @@Ae's;
 * the command monitor posts its queries so (command_monitor). */
static const int16_t GPS_POST_STEP = 4;

/* A message's two-character ID, after its "@@". */
static const uint32_t GPS_ONCORE_ID_LENGTH = 2;

/* Stores a received byte at gps_response_cursor. */
static void gps_oncore_store(uint8_t c) { *gps_response_cursor++ = c; }

/* The scale of a value kept in thousandths, as an angle's seconds are
 * in milliarcseconds (format_angle). */
static const float GPS_REPORTS_THOUSANDTHS = 1000.0f;
