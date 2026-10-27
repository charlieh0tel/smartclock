/* What the GPS engine's files and the status screen share
 * (gps_receiver.c, gps_requests.c, screen.c): the satellite IDs, GPS 1
 * to 32 and pseudolites 33 to 37 (satellite_kind), the receiver's
 * channels, an angle's milliarcseconds, and gps_request_post's code for
 * a step.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* The receiver's channels, 1 to 6. */
static const uint8_t GPS_CHANNELS = 6;
