/* What the GPS engine's files and the status screen share
 * (gps_receiver.c, gps_requests.c, screen.c): the satellite IDs, GPS 1
 * to 32 and pseudolites 33 to 37 (satellite_kind), the receiver's
 * channels, an angle's milliarcseconds, and gps_request_post's code for
 * a step.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

static const uint8_t GPS_FIRST_SATELLITE = 1;

static const uint8_t GPS_LAST_SATELLITE = 0x20;

static const uint8_t GPS_FIRST_PSEUDOLITE = 0x21;

static const uint8_t GPS_LAST_PSEUDOLITE = 0x25;

/* The receiver's channels, 1 to 6. */
static const uint8_t GPS_CHANNELS = 6;

/* A survey's progress in percent, the least shown once it starts and
 * the most until it is complete (survey_progress_text, 0x4f4ca; the
 * status screen's, 0x4a066). */
static const float GPS_SURVEY_PERCENT = 100.0f;

static const float GPS_SURVEY_LEAST = 0.1f;

static const float GPS_SURVEY_MOST_UNFINISHED = 99.9f;

/* An angle's milliarcseconds per degree, per minute and per second. */
static const int32_t GPS_MAS_PER_DEGREE = 3600000;

static const int32_t GPS_MAS_PER_MINUTE = 60000;

static const double GPS_MAS_PER_SECOND = 1000.0;

/* gps_request_post's code for a request given as a script step, whose
 * address its arguments' first long holds, in place of @@Ae's (0x4bd2c);
 * the command monitor posts its queries so (0x4b536). */
static const int16_t GPS_POST_STEP = 4;
