/* What the GPS engine's files and the status screen share
 * (gps_receiver.c, screen.c): the satellite IDs, GPS 1 to 32 and
 * pseudolites 33 to 37 (satellite_kind).  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

static const uint8_t GPS_FIRST_SATELLITE = 1;

static const uint8_t GPS_LAST_SATELLITE = 0x20;

static const uint8_t GPS_FIRST_PSEUDOLITE = 0x21;

static const uint8_t GPS_LAST_PSEUDOLITE = 0x25;
