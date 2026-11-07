/* The GPS task's requests: how it sorts and checks the ones it receives
 * at gpsx_exchange, the ring it keeps them in until it runs them, and
 * the monitors' side of posting one (docs/firmware/gps.md, "Requests"). Written
 * by hand in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"; docs/firmware/modules.md, `applications/gps/requests`).  The exported C
 * includes this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

/* A request code's class, by its range and, for a sentence or an Oncore
 * message, its argument mode, 0 to send it and 1 to query it (0x57ae0). */
gps_code_class gps_request_class(gps_request_code_word code,
                                 gps_argument_mode argument_mode) {
  static const int16_t STEP_LAST = GPS_SCRIPT_INIT_DEFAULTS - 1;
  static const int16_t SCRIPT_LAST = 0x83;

  if (code >= GPS_REQUEST_TIME_OF_DAY && code <= GPS_NMEA_SELFTEST &&
      (argument_mode == GPS_ARGUMENTS_SET ||
       argument_mode == GPS_ARGUMENTS_QUERY)) {
    return GPS_CLASS_MESSAGE;
  }
  if (code >= GPS_STEP_STATUS_SNAPSHOT && code <= STEP_LAST) {
    return GPS_CLASS_STEP;
  }
  if (code >= GPS_SCRIPT_INIT_DEFAULTS && code <= SCRIPT_LAST) {
    return GPS_CLASS_SCRIPT;
  }
  return GPS_CLASS_INVALID;
}

/* The senders whose requests the task takes, ending in GPS_SENDER_NONE:
 * the image's table at 0x594dc, five where the others' have four. */
static const gps_request_sender gps_requests_senders[] = {
    GPS_SENDER_SEQUENCE,       GPS_SENDER_SETTING,
    GPS_SENDER_SETTING_OTHER,  GPS_SENDER_COMMAND_MONITOR,
    GPS_SENDER_STATUS_MONITOR, GPS_SENDER_NONE};

/* 1 for a sender the task takes requests from (0x5796a). */
uint32_t request_kind_valid(gps_request_sender sender) {
  const gps_request_sender* known = gps_requests_senders;

  while (*known != GPS_SENDER_NONE) {
    if (*known++ == sender) {
      return 1;
    }
  }
  return 0;
}

/* The ring's entry after one, back to the first after the last. */
static gps_request_entry* gps_requests_after(gps_request_entry* entry) {
  static const uint32_t COUNT =
      sizeof gps_requests.entries / sizeof gps_requests.entries[0];
  gps_request_entry* next = entry + 1;

  return next >= gps_requests.entries + COUNT ? gps_requests.entries : next;
}

/* 1 if the ring holds no request (0x577a6). */
bool request_ring_empty(void) {
  return gps_requests.read == gps_requests.write;
}

/* Empties the ring of waiting requests (0x576b6). */
void request_ring_clear(void) {
  gps_requests.write = gps_requests.entries;
  gps_requests.read = gps_requests.entries;
}

/* 1 if the ring has no room for another (0x577bc). */
bool request_ring_full(void) {
  return gps_requests_after(gps_requests.write) == gps_requests.read;
}

/* Adds a request as the newest; 0 if the ring is full (0x576c6). */
bool request_ring_put(gps_request_entry* entry) {
  gps_request_entry* next = gps_requests_after(gps_requests.write);

  if (next == gps_requests.read) {
    return 0;
  }
  *gps_requests.write = *entry;
  gps_requests.write = next;
  return 1;
}

/* Takes the oldest request; 0 if the ring is empty (0x5770e). */
int32_t request_ring_get(gps_request_entry* entry) {
  if (gps_requests.read == gps_requests.write) {
    return 0;
  }
  *entry = *gps_requests.read;
  gps_requests.read = gps_requests_after(gps_requests.read);
  return 1;
}

/* Takes back the newest request; 0 if the ring is empty (0x5775a). */
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

/* Whether a request from the sender is running, the task not idle, or
 * is waiting in the ring (0x577e2). */
bool request_ring_contains(gps_process_state state, gps_request_sender sender,
                           gps_request_sender running_sender) {
  if (state != GPS_PROCESS_TASK_IDLE && sender == running_sender) {
    return true;
  }
  for (gps_request_entry* entry = gps_requests.read;
       entry != gps_requests.write; entry = gps_requests_after(entry)) {
    if (entry->sender == sender) {
      return true;
    }
  }
  return false;
}

/* 1 if the newest waiting request is a monitor's not yet sent, which a
 * new request may replace (0x57828). */
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

/* The oldest waiting request with the code; NULL with none (0x57864). */
gps_request_entry* request_ring_find(gps_request_code_word code) {
  for (gps_request_entry* entry = gps_requests.read;
       entry != gps_requests.write; entry = gps_requests_after(entry)) {
    if (entry->code == code) {
      return entry;
    }
  }
  return NULL;
}

/* Runs the oldest waiting request with the code at once, out of turn, as
 * gps_console_request does a console line's, and counts it a
 * success: taken from the ring's oldest end, or by turning the ring once
 * round, every other request put back in its order (0x5789a).  The
 * image takes it from the newest end when it is at gps_requests.write,
 * which request_ring_find never returns, the newest being the entry
 * before. */
void gps_request_run_queued(gps_request_code_word code) {
  gps_request_entry* found = request_ring_find(code);
  gps_request_entry* end = gps_requests.write;
  gps_request_entry taken;
  gps_request_entry entry;
  bool done = false;

  if (found == NULL) {
    return;
  }
  if (found == gps_requests.read) {
    request_ring_get(&taken);
  } else if (found == gps_requests.write) {
    request_ring_unput(&taken);
  } else {
    while (end != gps_requests.read) {
      request_ring_get(&entry);
      if (done || entry.code != code) {
        request_ring_put(&entry);
      } else {
        taken = entry;
        done = true;
      }
    }
  }
  gps_console_request(taken.code, taken.arguments, taken.sender);
  gps_count_response(GPS_RESULT_SUCCESS, &taken);
}

/* Queues a request the task has received: a link reset aborts the
 * running request and every waiting one and goes in alone; another is
 * refused as busy while one from its sender runs or waits, or when the
 * ring is full, unless it can replace the newest, which is answered busy
 * in its place (0x57986). */
void gps_request_queue(gps_process_state state, gps_request_entry* request,
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
  if (request_ring_contains(state, request->sender, running->sender)) {
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
 * pending: the code's, without arguments, or for GPS_POST_STEP the code,
 * mode and arguments of the step arguments[0] points at (0x57a3e).  Only
 * the monitors' senders mark a result, where the others' mark the status
 * monitor's for any but the command monitor. */
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
  } else if (sender == GPS_SENDER_STATUS_MONITOR) {
    status_monitor_result = GPS_RESULT_PENDING;
  }
  return (uint8_t)psos_send_x(gpsx_exchange, &request) == 0;
}

/* Whether the task takes a request from a sender: always.  The image
 * would look the code of a sequence's or a setting's request, senders 1
 * to 3, up in a list at 0x594da, refusing none found, but the list is
 * empty, its end mark alone, and the test the compiler made of it is
 * always true (0x57ca0). */
uint8_t gps_request_admitted(gps_request_sender sender,
                             gps_request_code_word code) {
  return 1;
}

/* The query of the setting in gps_periodic.query_code, built into
 * gps_command_message, which it then clears.  A query
 * gps_request_build refuses, -1, is a length of 255: a firmware
 * fault, which gpsm_task does not have, sending only a positive length
 * (0x581a2; docs/firmware/quirks.md). */
static gps_fixed_sentence gps_periodic_query(void) {
  gps_fixed_sentence sentence = {.text = gps_command_message};

  sentence.length = (uint8_t)gps_request_build(
      gps_periodic.query_code, GPS_ARGUMENTS_QUERY, NULL, sentence.text);
  gps_periodic.query_code = GPS_REQUEST_NONE;
  return sentence;
}

/* Sends the engine one sentence, by gps_periodic's stage: the query of
 * the setting gpsm_task left in its query_code, up to
 * GPS_PERIODIC_QUERIES in a row, else int,GSV00, and int,anc00 once it
 * has sent GPS_PERIODIC_SENDS; 0 (0x57cd8).  Its caller pushes two zeros
 * it does not read (0x5d114). */
uint32_t gps_periodic_send(void) {
  static const uint8_t GPS_PERIODIC_QUERIES = 4;
  static const uint16_t GPS_PERIODIC_SENDS = 59;
  const bool query = gps_periodic.query_code != GPS_REQUEST_NONE;
  gps_fixed_sentence sentence;

  switch (gps_periodic.stage) {
    case GPS_PERIODIC_GSV:
      if (gps_periodic.sends >= GPS_PERIODIC_SENDS) {
        sentence = gps_periodic_anc;
        gps_periodic.stage = GPS_PERIODIC_ANC;
      } else if (query) {
        sentence = gps_periodic_query();
        gps_periodic.queries = 1;
        gps_periodic.sends++;
        gps_periodic.stage = GPS_PERIODIC_QUERY;
      } else {
        sentence = gps_periodic_gsv;
        gps_periodic.sends++;
      }
      break;
    case GPS_PERIODIC_ANC:
      if (query) {
        sentence = gps_periodic_query();
        gps_periodic.queries = 1;
        gps_periodic.sends = 0;
        gps_periodic.stage = GPS_PERIODIC_QUERY;
      } else {
        sentence = gps_periodic_gsv;
        gps_periodic.sends = 1;
        gps_periodic.stage = GPS_PERIODIC_GSV;
      }
      break;
    case GPS_PERIODIC_QUERY:
      if (query && gps_periodic.queries < GPS_PERIODIC_QUERIES) {
        sentence = gps_periodic_query();
        gps_periodic.queries++;
      } else {
        sentence = gps_periodic_gsv;
        gps_periodic.stage = GPS_PERIODIC_GSV;
      }
      gps_periodic.sends++;
      break;
    default:
      sentence.length = 0;
      break;
  }
  if (sentence.length != 0) {
    drta_send_message((uint8_t*)sentence.text, sentence.length);
  }
  return 0;
}

/* Whether the engine's Time RAIM option is known to decide a request:
 * 1 for the TRAIM script, @@Bn or @@En once the receiver's ID has come,
 * with, through supported, whether the engine has the option, field_592's
 * bit 3 or its Time RAIM bit; 0 for any other or before the ID, supported
 * left as it was (0x5c0b0). */
uint32_t gps_message_supported(int16_t code, uint8_t* supported) {
  if ((code != GPS_SCRIPT_TRAIM && code != GPS_REQUEST_TRAIM &&
       code != GPS_REQUEST_TRAIM_8) ||
      gps.receiver_id_received == 0) {
    return 0;
  }
  *supported =
      (gps.field_592 & (GPS_FIELD_592_BIT_3 | GPS_FIELD_592_TIME_RAIM)) != 0;
  return 1;
}

/* The scripts by their codes from GPS_SCRIPT_INIT_DEFAULTS on, by
 * gps_script_lookup's jump table (0x57b48): the empty script for the
 * codes with none, 0x6e, 0x70, 0x71, 0x73, 0x75 and 0x77. */
static gps_script_step** const gps_requests_scripts[] = {
    gps_script_6b,   gps_script_6c,   gps_script_6d,   gps_script_none,
    gps_script_6f,   gps_script_none, gps_script_none, gps_script_72,
    gps_script_none, gps_script_74,   gps_script_none, gps_script_76,
    gps_script_none, gps_script_78,   gps_script_79,   gps_script_7a,
    gps_script_7b,   gps_script_7c,   gps_script_7d,   gps_script_7e,
    gps_script_7f,   gps_script_80};

/* A script code's steps, the empty script for any other; the first
 * script also resets gps (0x57b2a), the 58503A 3704's over this image's
 * codes. */
gps_script_step** gps_script_lookup(gps_request_code_word code) {
  static const uint32_t COUNT =
      sizeof gps_requests_scripts / sizeof gps_requests_scripts[0];
  const uint32_t index = (uint32_t)(code - GPS_SCRIPT_INIT_DEFAULTS);

  if (index >= COUNT) {
    return gps_script_none;
  }
  if (code == GPS_SCRIPT_INIT_DEFAULTS) {
    gps_init();
  }
  return gps_requests_scripts[index];
}
