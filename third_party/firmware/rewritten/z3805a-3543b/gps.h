/* What the GPS engine's files and the status screen share
 * (gps_receiver.c, gps_requests.c, screen.c): the satellite IDs, GPS 1
 * to 32 and pseudolites 33 to 37 (satellite_kind), and
 * gps_request_post's code for a step.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

static const uint8_t GPS_FIRST_SATELLITE = 1;

static const uint8_t GPS_LAST_SATELLITE = 0x20;

static const uint8_t GPS_FIRST_PSEUDOLITE = 0x21;

static const uint8_t GPS_LAST_PSEUDOLITE = 0x25;

/* gps_request_post's code for a request given as a script step, whose
 * address its arguments' first long holds, in place of @@Ae's (0x4bd2c);
 * the command monitor posts its queries so (0x4b536). */
static const int16_t GPS_POST_STEP = 4;
