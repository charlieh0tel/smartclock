/* From the Z3816A 4001's rewritten/gps.h, the 58503B 1.01.04's own beside
 * common/'s: what the GPS engine's files and the status screen share, the
 * receiver's channels and an angle's text in degrees, minutes and seconds.
 * The exported C includes the hand-written headers after its own declarations
 * and before the hand-written .c files, so every one of them can use it. */

/* The most channels an engine has, 1 to 8, its own count being
 * gps.channel_count; the satellite lists' limit (satellite_list_limits). */
static const uint8_t GPS_CHANNELS = 8;

/* The bits of gps.field_609 the send filters and the instrument state
 * page test (0x5843c, 0x58450; 0x4efc6, 0x4f03e), which
 * oncore_receiver_id_message sets by the engine's software version
 * (0x58190, 0x581d2). */
static const uint8_t GPS_FIELD_609_BIT_6 = 0x40;
static const uint8_t GPS_FIELD_609_BIT_7 = 0x80;

/* sky_statistics' hours, and the nibbles of its map's bytes and of its
 * hourly ranges: the high one shifted down, the low one masked. */
static const uint8_t SKY_HOURS = 24;
static const uint8_t SKY_HIGH_SHIFT = 4;
static const uint8_t SKY_LOW_NIBBLE = 0x0f;

/* A six-channel engine's count, gps.channel_count until @@Ea has come
 * (gps_channel_count_update). */
static const uint8_t GPS_SIX_CHANNELS = 6;

/* The poll monitor's states, gps.field_5fd, which gps_init starts idle. */
static const uint8_t GPS_RECEIVER_POLL_IDLE = 0;
static const uint8_t GPS_RECEIVER_POLL_WAITING = 1;
static const uint8_t GPS_RECEIVER_POLL_POSTED = 2;

/* An angle in mas, a double, as its hemisphere, N or S for a latitude,
 * E or W, then degrees, minutes and seconds by format, which gives the
 * hemisphere and the three (format_dms_precise). */
static void gps_format_dms(char* text, double mas, char latitude,
                           const char* format) {
  uint8_t degrees;
  uint8_t minutes;
  double seconds;
  char hemisphere;

  if (double_compare(0.0, mas) < 0) {
    hemisphere = latitude == 0 ? 'W' : 'S';
    mas = -mas;
  } else {
    hemisphere = latitude == 0 ? 'E' : 'N';
  }
  degrees = double_to_unsigned(
      double_divide_reversed_pointer((double)GPS_MAS_PER_DEGREE, &mas));
  double_subtract_in_place(&mas,
                           integer_to_double(degrees * GPS_MAS_PER_DEGREE));
  minutes = double_to_unsigned(
      double_divide_reversed_pointer((double)GPS_MAS_PER_MINUTE, &mas));
  double_subtract_in_place(&mas,
                           integer_to_double(minutes * GPS_MAS_PER_MINUTE));
  seconds = double_divide_reversed_pointer(GPS_MAS_PER_SECOND, &mas);
  sprintf(text, format, hemisphere, degrees, minutes, seconds);
}

/* Runs a message's send filter of oncore_messages, as gps_request_build
 * and monitor_query_next do.  The table holds the image's own routines,
 * which take the message's index in a 2-byte slot, at SP+4, and its
 * first encoder and the arguments after it, at SP+6 and SP+10 (0x584ec
 * to 0x584f8, 0x5257c to 0x52586), where gcc gives each a 4-byte slot;
 * so the three are passed as one structure, which gcc lays from the
 * slot's start. */
static uint8_t oncore_filter_run(oncore_send_filter* filter, int16_t index,
                                 oncore_encoder* encoder, int32_t* arguments) {
  /* The index, encoder and arguments as the image's filters read them. */
  typedef struct __attribute__((packed)) filter_slots {
    int16_t index;           /* +0x00: the message's */
    oncore_encoder* encoder; /* +0x02: its first encoder */
    int32_t* arguments;      /* +0x06 */
  } filter_slots;
  typedef uint8_t filter_slotted(filter_slots slots);

  return ((filter_slotted*)filter)((filter_slots){
      .index = index, .encoder = encoder, .arguments = arguments});
}
