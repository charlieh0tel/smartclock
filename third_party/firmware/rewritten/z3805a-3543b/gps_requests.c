/* The GPS task's requests: how it sorts and checks the ones it receives
 * at gpsx_exchange, the ring it keeps them in until it runs them, the
 * monitors' side of posting one, and the scripts a request names
 * (docs/firmware/gps.md, "Requests").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/gps/requests`).  The exported
 * C includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

/* The senders whose requests the task takes, ending in 0: the image's
 * table at 0x4cfc0. */
static const gps_request_sender gps_requests_senders[] = {
    GPS_SENDER_SEQUENCE, GPS_SENDER_SETTING, GPS_SENDER_COMMAND_MONITOR,
    GPS_SENDER_STATUS_MONITOR, 0};

/* 1 for a sender the task takes requests from (0x4bc58). */
uint32_t request_kind_valid(gps_request_sender sender) {
  const gps_request_sender* known = gps_requests_senders;

  while (*known != 0) {
    if (*known++ == sender) {
      return 1;
    }
  }
  return 0;
}

/* A request code's class, by its range and, for a message, its argument
 * mode, 0 to send it and 1 to query it (0x4bdba). */
gps_code_class gps_request_class(gps_request_code_word code,
                                 gps_argument_mode argument_mode) {
  if (code >= GPS_REQUEST_TIME_OF_DAY && code <= GPS_REQUEST_RECEIVER_ID &&
      (argument_mode == GPS_ARGUMENTS_SET ||
       argument_mode == GPS_ARGUMENTS_QUERY)) {
    return GPS_CLASS_MESSAGE;
  }
  if (code >= GPS_STEP_STATUS_SNAPSHOT && code < GPS_SCRIPT_INIT_DEFAULTS) {
    return GPS_CLASS_STEP;
  }
  if (code >= GPS_SCRIPT_INIT_DEFAULTS && code <= GPS_SCRIPT_SATELLITE_SELECT) {
    return GPS_CLASS_SCRIPT;
  }
  return GPS_CLASS_INVALID;
}

/* The ring's entry after one, back to the first after the last. */
static gps_request_entry* gps_requests_after(gps_request_entry* entry) {
  static const uint32_t COUNT =
      sizeof gps_requests.entries / sizeof gps_requests.entries[0];
  gps_request_entry* next = entry + 1;

  return next >= gps_requests.entries + COUNT ? gps_requests.entries : next;
}

/* Empties the ring of waiting requests (0x4baaa). */
void request_ring_clear(void) {
  gps_requests.write = gps_requests.entries;
  gps_requests.read = gps_requests.entries;
}

/* 1 if the ring holds no request (0x4bb9a). */
bool request_ring_empty(void) {
  return gps_requests.read == gps_requests.write;
}

/* 1 if the ring has no room for another (0x4bbb0). */
bool request_ring_full(void) {
  return gps_requests_after(gps_requests.write) == gps_requests.read;
}

/* Adds a request as the newest; 0 if the ring is full (0x4baba). */
bool request_ring_put(gps_request_entry* entry) {
  gps_request_entry* next = gps_requests_after(gps_requests.write);

  if (next == gps_requests.read) {
    return 0;
  }
  *gps_requests.write = *entry;
  gps_requests.write = next;
  return 1;
}

/* Takes the oldest request; 0 if the ring is empty (0x4bb02). */
int32_t request_ring_get(gps_request_entry* entry) {
  if (gps_requests.read == gps_requests.write) {
    return 0;
  }
  *entry = *gps_requests.read;
  gps_requests.read = gps_requests_after(gps_requests.read);
  return 1;
}

/* Takes back the newest request; 0 if the ring is empty (0x4bb4e). */
int32_t request_ring_unput(gps_request_entry* entry) {
  static const uint32_t LAST =
      sizeof gps_requests.entries / sizeof gps_requests.entries[0] - 1;

  if (gps_requests.read == gps_requests.write) {
    return 0;
  }
  if (gps_requests.entries < gps_requests.write) {
    gps_requests.write--;
  } else {
    gps_requests.write = &gps_requests.entries[LAST];
  }
  *entry = *gps_requests.write;
  return 1;
}

/* 1 if a request from the sender is running, the task being busy, or
 * is waiting in the ring (0x4bbd6). */
uint32_t request_ring_contains(char busy, char sender, char running_sender) {
  if (busy != 0 && sender == running_sender) {
    return 1;
  }
  for (gps_request_entry* entry = gps_requests.read;
       entry != gps_requests.write; entry = gps_requests_after(entry)) {
    if (entry->sender == sender) {
      return 1;
    }
  }
  return 0;
}

/* 1 if the newest waiting request is a monitor's not yet sent, which a
 * new request may replace (0x4bc1c). */
uint32_t request_ring_newest_replaceable(void) {
  gps_request_entry newest;

  if (request_ring_unput(&newest) == 0) {
    return 0;
  }
  request_ring_put(&newest);
  return (newest.sender == GPS_SENDER_COMMAND_MONITOR ||
          newest.sender == GPS_SENDER_STATUS_MONITOR) &&
         newest.sent == 0;
}

/* Queues a request the task has received: a link reset aborts the
 * running request and every waiting one and goes in alone; another is
 * refused as busy while one from its sender runs or waits, or when the
 * ring is full, unless it can replace the newest, which is answered busy
 * in its place (0x4bc74). */
void gps_request_queue(uint8_t state, gps_request_entry* request,
                       gps_request_entry* running) {
  gps_request_entry waiting;

  if (request->code == GPS_STEP_LINK_RESET) {
    if (state != GPS_PROCESS_TASK_IDLE) {
      gps_count_response(GPS_RESULT_ABORT, running);
    }
    while (request_ring_get(&waiting) != 0) {
      gps_count_response(GPS_RESULT_ABORT, &waiting);
    }
    request_ring_put(request);
    gps_task_state = GPS_PROCESS_TASK_IDLE;
    return;
  }
  if (request_ring_contains(state, request->sender, running->sender) != 0) {
    gps_count_response(GPS_RESULT_BUSY, request);
    return;
  }
  if (request_ring_empty() == 0) {
    if (request_ring_newest_replaceable() != 0) {
      request_ring_unput(&waiting);
      gps_count_response(GPS_RESULT_BUSY, &waiting);
    } else if (request_ring_full() != 0) {
      gps_count_response(GPS_RESULT_BUSY, request);
      return;
    }
  }
  request_ring_put(request);
}

/* Sends a monitor's request to the task's exchange, marking its result
 * pending: the code's, without arguments, or for GPS_POST_STEP the code, mode
 * and arguments of the step arguments[0] points at (0x4bd2c). */
bool gps_request_post(gps_request_sender sender, gps_request_code_word code,
                      int32_t* arguments, uint8_t sent) {
  gps_request_entry request;

  request.field_00 = 0;
  request.field_04 = 0;
  request.sender = sender;
  if (code == GPS_POST_STEP) {
    /* The argument long holds the step's address. */
    gps_script_step* step = (gps_script_step*)arguments[0];

    request.code = step->code;
    request.argument_mode = step->argument_mode;
    request.arguments = step->arguments;
  } else {
    request.code = code;
    request.argument_mode = GPS_ARGUMENTS_SET;
    request.arguments = 0;
  }
  request.sent = sent;
  request.result = GPS_RESULT_REJECT;
  if (sender == GPS_SENDER_COMMAND_MONITOR) {
    command_monitor_result = GPS_RESULT_PENDING;
  } else {
    status_monitor_result = GPS_RESULT_PENDING;
  }
  return (uint8_t)psos_send_x(gpsx_exchange, &request) == 0;
}

/* The scripts by their codes from GPS_SCRIPT_INIT_DEFAULTS on. */
static gps_script_step** const gps_requests_scripts[] = {
    gps_script_47, gps_script_48, gps_script_49, gps_script_4a, gps_script_4b,
    gps_script_4c, gps_script_4d, gps_script_4e, gps_script_4f, gps_script_50,
    gps_script_51, gps_script_52, gps_script_53, gps_script_54, gps_script_55,
    gps_script_56, gps_script_57, gps_script_58, gps_script_59};

/* A script code's steps, the empty script for any other; the first
 * script also resets gps (0x4be04). */
gps_script_step** gps_script_lookup(gps_request_code_word code) {
  static const uint32_t COUNT =
      sizeof gps_requests_scripts / sizeof gps_requests_scripts[0];
  uint32_t index = (uint32_t)(code - GPS_SCRIPT_INIT_DEFAULTS);

  if (index >= COUNT) {
    return gps_script_none;
  }
  if (code == GPS_SCRIPT_INIT_DEFAULTS) {
    gps_init();
  }
  return gps_requests_scripts[index];
}

/* 1 unless the message is @@Bn without the receiver's ID yet; and
 * through supported, whether the receiver takes it: @@Bn only with its
 * Time RAIM option (0x4f8fc). */
uint32_t gps_message_supported(gps_request_code_word code, uint8_t* supported) {
  if (code == GPS_REQUEST_TRAIM) {
    if (gps.receiver_id_received == 0) {
      return 0;
    }
    *supported = gps.raim_option != 0;
  } else {
    *supported = 1;
  }
  return 1;
}

/* Sets gps.raim_option when the receiver ID's options list has an I,
 * the Time RAIM option (0x4f8b6). */
void gps_parse_options(void) {
  static const char LABEL[] = "OPTIONS LIST ";
  static const uint32_t LABEL_LENGTH = sizeof LABEL - 1;

  gps.raim_option =
      strncmp(gps.receiver_id.options, (char*)LABEL, LABEL_LENGTH) == 0 &&
      strchr(gps.receiver_id.options + LABEL_LENGTH, 'I') != 0;
}

/* Runs an argument encoder of oncore_messages.  The table holds the
 * image's own routines, which return where the next byte goes in D0
 * alone, where gcc takes a pointer result from A0; so it is called as
 * returning a long. */
static uint8_t* gps_requests_encode(oncore_encoder* encoder, int32_t* arguments,
                                    uint8_t index, uint8_t* out) {
  typedef uint32_t encode_in_d0(int32_t* arguments, uint8_t index,
                                uint8_t* out);

  return (uint8_t*)((encode_in_d0*)encoder)(arguments, index, out);
}

/* Builds a request's message at out: "@@" and its ID, then its
 * arguments through the message's encoders or its query form, then the
 * checksum and CR LF.  Its length; 0, out empty, for a message the task
 * does not send or a query of one without a query form; -1 for one the
 * receiver does not take (0x4f932). */
int16_t gps_request_build(gps_request_code_word message, gps_argument_mode mode,
                          int32_t* arguments, uint8_t* out) {
  static const uint8_t ENCODERS = sizeof oncore_messages[0].encoders /
                                  sizeof oncore_messages[0].encoders[0];
  /* A query form's stand-in for a zero byte, which would end it. */
  static const uint8_t QUERY_ZERO = 0xee;
  /* @@Bn's fields after those its encoders send: nnn and mdyyhms, not
   * used (VPCommands.pdf, Time RAIM Setup and Status), as the image
   * sends them. */
  static const uint8_t TRAIM_UNUSED[] = {0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
  const oncore_message_entry* entry = &oncore_messages[message];
  /* The message starts as text, its "@@" and ID. */
  char* text = (char*)out;
  uint8_t supported = 0;
  uint8_t* next;

  if (gps_message_supported(message, &supported) == 0 || supported == 0) {
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
    if (message == GPS_REQUEST_POSITION_STATUS) {
      /* The rate asked for, @@Ba's first argument, as it is sent. */
      uint8_t rate;

      gps_requests_encode(entry->encoders[0], arguments, 0, &rate);
      gps.status_rate = rate;
    } else if (message == GPS_REQUEST_TRAIM) {
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
