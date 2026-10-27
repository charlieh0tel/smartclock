/* The GPS task's requests: how it sorts and checks the ones it receives
 * at gpsx_exchange, the ring it keeps them in until it runs them, the
 * monitors' side of posting one, and the scripts a request names
 * (docs/firmware/gps.md, "Requests").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/gps/requests`).  The exported
 * C includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

/* The ring's entry after one, back to the first after the last. */
static gps_request_entry* gps_requests_after(gps_request_entry* entry) {
  static const uint32_t COUNT =
      sizeof gps_requests.entries / sizeof gps_requests.entries[0];
  gps_request_entry* next = entry + 1;

  return next >= gps_requests.entries + COUNT ? gps_requests.entries : next;
}

/* 1 if the ring holds no request (0x4c48a). */
bool request_ring_empty(void) {
  return gps_requests.read == gps_requests.write;
}

/* Runs an argument encoder of oncore_messages.  The table holds the
 * image's own routines, which return where the next byte goes in D0
 * alone, where gcc takes a pointer result from A0, so it is called as
 * returning a long; and which take the index in a 2-byte slot, at SP+8,
 * and out after it, at SP+10 (0x4fe5a, as gps_request_build pushes them
 * at 0x50c54), where gcc gives each a 4-byte slot; so the two are
 * passed as one structure, which gcc lays from the slot's start. */
static uint8_t* gps_requests_encode(oncore_encoder* encoder, int32_t* arguments,
                                    uint8_t index, uint8_t* out) {
  /* The index and out as the image's encoders read them. */
  typedef struct __attribute__((packed)) encoder_slots {
    uint8_t index;    /* +0x00: the argument's index */
    uint8_t field_01; /* unread: the rest of the index's slot */
    uint8_t* out;     /* +0x02: where its bytes go */
  } encoder_slots;
  typedef uint32_t encode_in_d0(int32_t* arguments, encoder_slots slots);

  return (uint8_t*)((encode_in_d0*)encoder)(
      arguments, (encoder_slots){.index = index, .out = out});
}

/* Runs a message's send filter of oncore_messages.  The table holds the
 * image's own routines, which take the message's index in a 2-byte
 * slot, at SP+4, and its first encoder and the arguments after it, at
 * SP+6 and SP+10 (0x50bee), where gcc gives each a 4-byte slot; so the
 * three are passed as one structure, which gcc lays from the slot's
 * start. */
static uint8_t gps_requests_filter(oncore_send_filter* filter, int16_t index,
                                   oncore_encoder* encoder,
                                   int32_t* arguments) {
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

/* Builds a request's message at out: "@@" and its ID, then its
 * arguments through the message's encoders or its query form, then the
 * checksum and CR LF.  Its length; 0, out empty, for a message the task
 * does not send or a query of one without a query form; -1 for one its
 * send filter refuses, the 58503A's test of what the engine takes,
 * where the Z3805A's asks gps_message_supported (0x50bc8). */
int16_t gps_request_build(gps_request_code_word message, gps_argument_mode mode,
                          int32_t* arguments, uint8_t* out) {
  static const uint8_t ENCODERS = sizeof oncore_messages[0].encoders /
                                  sizeof oncore_messages[0].encoders[0];
  /* A query form's stand-in for a zero byte, which would end it. */
  static const uint8_t QUERY_ZERO = 0xee;
  /* @@Bn's and @@En's fields after those their encoders send: nnn and
   * mdyyhms, not used (VPCommands.pdf, Time RAIM Setup and Status), as
   * the image sends them. */
  static const uint8_t TRAIM_UNUSED[] = {0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
  const oncore_message_entry* entry = oncore_message_entry_at(message);
  /* The message starts as text, its "@@" and ID. */
  char* text = (char*)out;
  uint8_t* next;

  if (entry->filter != NULL &&
      gps_requests_filter(entry->filter, message, entry->encoders[0],
                          arguments) == 0) {
    return -1;
  }
  *out = 0;
  if (entry->kind != ONCORE_KIND_SENT && entry->kind != ONCORE_KIND_EXCHANGED) {
    return 0;
  }
  strcat(text, "@@");
  strcat(text, entry->id);
  next = out + libc_length(text);
  if (mode == GPS_ARGUMENTS_SET) {
    for (uint8_t i = 0; i < ENCODERS && entry->encoders[i] != NULL; i++) {
      next = gps_requests_encode(entry->encoders[i], arguments, i, next);
    }
    if (message == GPS_REQUEST_POSITION_STATUS ||
        message == GPS_REQUEST_POSITION_STATUS_8) {
      /* The rate asked for, @@Ba's or @@Ea's first argument, as it is
       * sent. */
      uint8_t rate;

      gps_requests_encode(entry->encoders[0], arguments, 0, &rate);
      gps.status_rate = rate;
    } else if (message == GPS_REQUEST_TRAIM || message == GPS_REQUEST_TRAIM_8) {
      for (uint32_t i = 0; i < sizeof TRAIM_UNUSED; i++) {
        *next++ = TRAIM_UNUSED[i];
      }
    }
  } else {
    const uint8_t* form = entry->query;

    if (form == NULL) {
      *out = 0;
      return 0;
    }
    if (message == GPS_REQUEST_MANUAL_SATELLITE_SELECT) {
      /* @@Ai's query names its channel. */
      next = long_array_get_byte(arguments, 0, next);
    }
    for (; *form != 0; form++) {
      *next++ = *form == QUERY_ZERO ? 0 : *form;
    }
  }
  /* The checksum covers the ID on. */
  *next = oncore_checksum(out + libc_length("@@"), next);
  next++;
  *next++ = '\r';
  *next++ = '\n';
  return (int16_t)(next - out);
}

/* @@Ba's, @@Ea's and @@Ek's send filter (oncore_messages entries 30, 50
 * and 51): the six-channel messages, @@Ba, @@Bk and @@Bn, to a
 * six-channel engine, or to an eight-channel one when the first argument,
 * the output rate, encodes as 0, a response once; the eight-channel ones,
 * @@Ea, @@Ek and @@En, to an eight-channel engine alone (0x50ad8;
 * docs/firmware/gps.md, "Six or eight channels").  Written by hand: it
 * runs the message's first encoder, which takes its index in a 2-byte
 * slot. */
uint8_t oncore_filter_channels(int16_t index, oncore_encoder* encoder,
                               int32_t* arguments) {
  static const uint8_t SIX_CHANNELS = 6;
  const uint8_t channels = gps.channel_count;
  uint8_t rate;

  gps_requests_encode(encoder, arguments, 0, &rate);
  if ((index == GPS_REQUEST_POSITION_STATUS ||
       index == GPS_REQUEST_POSITION_EXTENSION || index == GPS_REQUEST_TRAIM) &&
      (channels == SIX_CHANNELS || (channels == GPS_CHANNELS && rate == 0))) {
    return 1;
  }
  return (index == GPS_REQUEST_POSITION_STATUS_8 ||
          index == GPS_REQUEST_POSITION_EXTENSION_8 ||
          index == GPS_REQUEST_TRAIM_8) &&
         channels == GPS_CHANNELS;
}

/* @@Bn's and @@En's send filter (entries 41 and 52):
 * oncore_filter_channels while the engine has the Time RAIM option
 * (0x50b48). */
uint8_t oncore_filter_traim(int16_t index, oncore_encoder* encoder,
                            int32_t* arguments) {
  if ((gps.field_592 & GPS_FIELD_592_OPTION_I) == 0) {
    return 0;
  }
  return oncore_filter_channels(index, encoder, arguments);
}
