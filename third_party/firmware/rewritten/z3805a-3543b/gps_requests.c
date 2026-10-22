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
gps_code_class gps_request_class(int16_t code, char argument_mode) {
  if (code >= GPS_REQUEST_TIME_OF_DAY && code <= GPS_REQUEST_RECEIVER_ID &&
      (argument_mode == 0 || argument_mode == 1)) {
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
void gps_request_queue(char state, gps_request_entry* request,
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
 * pending: the code's, without arguments, or for code 4 the code, mode
 * and arguments of the step arguments[0] points at (0x4bd2c). */
bool gps_request_post(gps_request_sender sender, int16_t code,
                      int32_t* arguments, uint8_t sent) {
  /* The code that passes a step, rather than naming the request. */
  static const int16_t STEP_GIVEN = 4;
  gps_request_entry request;

  request.field_00 = 0;
  request.field_04 = 0;
  request.sender = sender;
  if (code == STEP_GIVEN) {
    /* The argument long holds the step's address. */
    gps_script_step* step = (gps_script_step*)arguments[0];

    request.code = step->code;
    request.argument_mode = step->argument_mode;
    request.arguments = step->arguments;
  } else {
    request.code = code;
    request.argument_mode = 0;
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
gps_script_step** gps_script_lookup(int16_t code) {
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
uint32_t gps_message_supported(int16_t code, uint8_t* supported) {
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
