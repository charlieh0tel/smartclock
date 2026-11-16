/* The GPS task's requests: its poster, read from this image's code,
 * gps_request_build, the Z3815A 4010's, whose instructions differ from
 * this image's only in its request codes, and gps_count_response, the
 * Z3815A 4010's with this image's third sender, written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C").  With
 * them, from the 58503B 1.01.04's C, the functions this image's code
 * carries identical or relocated (docs/firmware/z3805a-1.01.03.md, "The
 * link list"), their comments' addresses this image's.  Each keeps its
 * generated prototype.  The exported C includes this file after its
 * own, so the types and prototypes it declares are in scope. */

/* Sends a request to the GPS task's exchange, marking its sender's
 * result pending: the code's, with the arguments given, or for
 * GPS_POST_STEP the code, mode and arguments of the step *step points
 * at (0x4da42).  Where the Z3815A 4010's takes the step from its
 * arguments' first long and passes none, this image's takes it from an
 * argument of its own and passes the arguments as given, and marks the
 * other settings' result, sender 3, beside the monitors'
 * (0x4da96 to 0x4dace). */
bool gps_request_post(gps_request_sender sender, gps_request_code_word code,
                      int32_t* arguments, gps_script_step** step,
                      uint8_t sent) {
  gps_request_entry request;

  request.field_00 = 0;
  request.field_04 = 0;
  request.sender = sender;
  if (code == GPS_POST_STEP) {
    request.code = (*step)->code;
    request.argument_mode = (*step)->argument_mode;
    request.arguments = (*step)->arguments;
  } else {
    request.code = code;
    request.argument_mode = GPS_ARGUMENTS_SET;
    request.arguments = arguments;
  }
  request.sent = sent;
  request.result = GPS_RESULT_REJECT;
  switch (sender) {
    case GPS_SENDER_SETTING_OTHER:
      setting_other_result = GPS_RESULT_PENDING;
      break;
    case GPS_SENDER_COMMAND_MONITOR:
      command_monitor_result = GPS_RESULT_PENDING;
      break;
    case GPS_SENDER_STATUS_MONITOR:
      status_monitor_result = GPS_RESULT_PENDING;
      break;
    default:
      break;
  }
  return (uint8_t)psos_send_x(gpsx_exchange, &request) == 0;
}

/* A descriptor's encoder, the image's routine, called with the image's
 * convention: the arguments at stack offset 4, the index a byte at 8 and
 * the text's pointer at 10, where its encoders read them.  gcc gives
 * each argument a four-byte slot, so the index and the pointer's high
 * word share the second, its low word the third's high half; the result
 * is in D0. */
typedef uint32_t gps_sentence_encoder_image(int32_t* arguments,
                                            uint32_t index_and_text_high,
                                            uint32_t text_low);

static char* gps_sentence_encode(gps_sentence_encoder* encoder,
                                 int32_t* arguments, uint8_t index,
                                 char* text) {
  static const uint8_t BYTE_HIGH = 24;
  static const uint8_t WORD = 16;
  const uint32_t at = (uint32_t)text;

  return (char*)((gps_sentence_encoder_image*)encoder)(
      arguments, (uint32_t)index << BYTE_HIGH | at >> WORD, at << WORD);
}

/* Builds a request's sentence into text: "$", "PFEC," before a
 * lowercase address, "GP", then to set it the address and each argument
 * through the descriptor's encoders, comma separated, or to query it
 * the descriptor's query, and CR LF; its length, 0 for a query of none
 * or a descriptor not built, -1 where its test refuses it.  Setting tps's
 * or rrm's interval keeps it as the status's or time RAIM's rate too
 * (0x54368).  The Z3815A 4010's, its codes this image's (0x54462 to
 * 0x5447e). */
int16_t gps_request_build(gps_request_code_word code,
                          gps_argument_mode argument_mode, int32_t* arguments,
                          char* text) {
  static const int16_t REFUSED = -1;
  static const uint8_t ENCODERS =
      sizeof((gps_sentence_descriptor*)0)->encoders /
      sizeof((gps_sentence_descriptor*)0)->encoders[0];
  const gps_sentence_descriptor* descriptor = gps_request_descriptor(code);
  char* end = text;

  if (descriptor->test != NULL &&
      descriptor->test(code, descriptor->encoders[0], arguments) == 0) {
    return REFUSED;
  }
  text[0] = '\0';
  if (descriptor->kind != GPS_SENTENCE_BUILT &&
      descriptor->kind != GPS_SENTENCE_BUILT_TOO) {
    return 0;
  }
  text[0] = '$';
  text[1] = '\0';
  if ((ctype_table[(uint8_t)descriptor->address[0] + 1] & LIBC_CTYPE_LOWER) !=
      0) {
    strcat(text, "PFEC");
    strcat(text, ",");
  }
  strcat(text, "GP");
  if (argument_mode == GPS_ARGUMENTS_SET) {
    strcat(text, descriptor->address);
    end += libc_length(text);
    for (uint8_t index = 0;
         descriptor->encoders[index] != NULL && index < ENCODERS; index++) {
      if (index != 0) {
        *end++ = ',';
      }
      end = gps_sentence_encode(descriptor->encoders[index], arguments, index,
                                end);
    }
    if (code == GPS_NMEA_INTERVAL_TPS || code == GPS_NMEA_INTERVAL_RRM) {
      uint8_t rate;

      long_array_get_byte(arguments, 0, &rate);
      if (code == GPS_NMEA_INTERVAL_TPS) {
        gps.status_rate = rate;
      } else {
        gps.raim.rate = rate;
      }
    }
  } else {
    if (descriptor->query == NULL) {
      text[0] = '\0';
      return 0;
    }
    strcat(text, descriptor->query);
    end += libc_length(text);
  }
  *end++ = '\r';
  *end++ = '\n';
  return end - text;
}

/* 1 if the ring holds no request (0x4d7aa). */
bool request_ring_empty(void) {
  return gps_requests.read == gps_requests.write;
}

/* Takes back the newest request; 0 if the ring is empty (0x4d75e). */
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

/* Runs the oldest waiting request with the code at once, out of turn, as
 * gps_console_request does a console line's, and counts it a
 * success: taken from the ring's oldest end, or by turning the ring once
 * round, every other request put back in its order (0x4d89e).  The
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

/* Whether the task takes a request from a sender: always.  The image
 * would look the code of a sequence's or a setting's request, senders 1
 * and 2, up in a list at 0x4f814, refusing none found, but the list is
 * empty, its end mark alone, and the test the compiler made of it is
 * always true (0x4dcd2). */
uint8_t gps_request_admitted(gps_request_sender sender,
                             gps_request_code_word code) {
  return 1;
}

/* 1 if the newest waiting request is a monitor's not yet sent, which a
 * new request may replace (0x4d82c). */
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

/* A request code's class, by its range and, for a sentence or an Oncore
 * message, its argument mode, 0 to send it and 1 to query it (0x4daf2). */
gps_code_class gps_request_class(gps_request_code_word code,
                                 gps_argument_mode argument_mode) {
  static const int16_t STEP_LAST = GPS_SCRIPT_INIT_DEFAULTS - 1;
  static const int16_t SCRIPT_LAST = 0x8e;

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

/* The ring's entry after one, back to the first after the last. */
static gps_request_entry* gps_requests_after(gps_request_entry* entry) {
  static const uint32_t COUNT =
      sizeof gps_requests.entries / sizeof gps_requests.entries[0];
  gps_request_entry* next = entry + 1;

  return next >= gps_requests.entries + COUNT ? gps_requests.entries : next;
}

/* The oldest waiting request with the code; NULL with none (0x4d868). */
gps_request_entry* request_ring_find(gps_request_code_word code) {
  for (gps_request_entry* entry = gps_requests.read;
       entry != gps_requests.write; entry = gps_requests_after(entry)) {
    if (entry->code == code) {
      return entry;
    }
  }
  return NULL;
}

/* Whether a request from the sender is running, the task not idle, or
 * is waiting in the ring (0x4d7e6). */
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

/* Adds a request as the newest; 0 if the ring is full (0x4d6ca). */
bool request_ring_put(gps_request_entry* entry) {
  gps_request_entry* next = gps_requests_after(gps_requests.write);

  if (next == gps_requests.read) {
    return 0;
  }
  *gps_requests.write = *entry;
  gps_requests.write = next;
  return 1;
}

/* Takes the oldest request; 0 if the ring is empty (0x4d712). */
int32_t request_ring_get(gps_request_entry* entry) {
  if (gps_requests.read == gps_requests.write) {
    return 0;
  }
  *entry = *gps_requests.read;
  gps_requests.read = gps_requests_after(gps_requests.read);
  return 1;
}

/* 1 if the ring has no room for another (0x4d7c0). */
bool request_ring_full(void) {
  return gps_requests_after(gps_requests.write) == gps_requests.read;
}

/* Queues a request the task has received: a link reset aborts the
 * running request and every waiting one and goes in alone; another is
 * refused as busy while one from its sender runs or waits, or when the
 * ring is full, unless it can replace the newest, which is answered busy
 * in its place (0x4d98a). */
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

/* The senders whose requests the task takes, ending in GPS_SENDER_NONE:
 * the image's table at 0x4f816, five. */
static const gps_request_sender gps_requests_senders[] = {
    GPS_SENDER_SEQUENCE,        GPS_SENDER_SETTING,
    GPS_SENDER_COMMAND_MONITOR, GPS_SENDER_STATUS_MONITOR,
    GPS_SENDER_SETTING_OTHER,   GPS_SENDER_NONE};

/* 1 for a sender the task takes requests from (0x4d96e). */
uint32_t request_kind_valid(gps_request_sender sender) {
  const gps_request_sender* known = gps_requests_senders;

  while (*known != GPS_SENDER_NONE) {
    if (*known++ == sender) {
      return 1;
    }
  }
  return 0;
}

/* Counts a request's outcome in gps_counts, puts back the code and
 * argument mode it came with, and gives it to its sender: a monitor's
 * or the other setting sender's in its result, another's sent back to
 * plla_exchange; 1 if it got there (0x4d5d2).  The Z3815A 4010's with
 * the third sender, GPS_SENDER_SETTING_OTHER (0x4d67c). */
int32_t gps_count_response(gps_request_result kind,
                           gps_request_entry* request) {
  uint8_t error;

  switch (kind) {
    case GPS_RESULT_SUCCESS:
      gps_counts.successes++;
      break;
    case GPS_RESULT_REJECT:
      gps_counts.rejects++;
      break;
    case GPS_RESULT_ABORT:
      gps_counts.aborts++;
      break;
    case GPS_RESULT_UNKNOWN_SENDER:
      gps_counts.unknown_sender++;
      break;
    case GPS_RESULT_UNKNOWN_COMMAND:
      gps_counts.unknown_command++;
      break;
    case GPS_RESULT_BUSY:
      gps_counts.busy++;
      break;
    case GPS_RESULT_NO_COMMAND:
      gps_counts.no_command++;
      break;
    case GPS_RESULT_NO_QUERY:
      gps_counts.no_query++;
      break;
    case GPS_RESULT_TIMEOUT:
      gps_counts.timeouts++;
      break;
    default:
      break;
  }
  request->code = request->untranslated_code;
  request->argument_mode = request->untranslated_argument_mode;
  if (request->sender == GPS_SENDER_SETTING_OTHER) {
    setting_other_result = kind;
    error = 0;
  } else if (request->sender == GPS_SENDER_COMMAND_MONITOR) {
    command_monitor_result = kind;
    error = 0;
  } else if (request->sender == GPS_SENDER_STATUS_MONITOR) {
    status_monitor_result = kind;
    error = 0;
  } else {
    request->result = kind;
    error = (uint8_t)psos_send_x(plla_exchange, request);
  }
  if (error == 0) {
    gps_counts.sent++;
  } else {
    gps_counts.sent_errors++;
  }
  return error == 0;
}

/* Empties the ring of waiting requests (0x4d6ba). */
void request_ring_clear(void) {
  gps_requests.write = gps_requests.entries;
  gps_requests.read = gps_requests.entries;
}

/* The scripts by their codes from GPS_SCRIPT_INIT_DEFAULTS on, by
 * gps_script_lookup's jump table (0x4db5a): the empty script for the
 * codes with none, 0x79, 0x7b, 0x7c, 0x7e, 0x80 and 0x82. */
static gps_script_step** const gps_requests_scripts[] = {
    gps_script_76,   gps_script_77,   gps_script_78,   gps_script_none,
    gps_script_7a,   gps_script_none, gps_script_none, gps_script_7d,
    gps_script_none, gps_script_7f,   gps_script_none, gps_script_81,
    gps_script_none, gps_script_83,   gps_script_84,   gps_script_85,
    gps_script_86,   gps_script_87,   gps_script_88,   gps_script_89,
    gps_script_8a,   gps_script_8b};

/* A script code's steps, the empty script for any other; the first
 * script also resets gps (0x4db3c). */
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

/* The query of the setting in gps_periodic.query_code, built into the
 * caller's buffer; the code is then none.  A query gps_request_build
 * refuses, -1, is a length of 255 (0x4dd66 to 0x4dd86). */
static gps_fixed_sentence gps_requests_periodic_query(char* buffer) {
  gps_fixed_sentence sentence = {.text = buffer};

  sentence.length = (uint8_t)gps_request_build(
      gps_periodic.query_code, GPS_ARGUMENTS_QUERY, NULL, buffer);
  gps_periodic.query_code = GPS_REQUEST_NONE;
  return sentence;
}

/* Sends the engine one sentence, by gps_periodic's stage: the query of
 * the setting gpsm_task left in its query_code, built in the frame, up
 * to GPS_PERIODIC_QUERIES in a row, else int,GSV00, and int,anc00,acc00
 * once it has sent GPS_PERIODIC_SENDS; 0.  It reads neither of the two
 * arguments tps's commit passes (0x4dd04). */
uint32_t gps_periodic_send(void* unused, void* unused_2) {
  static const uint8_t GPS_PERIODIC_QUERIES = 4;
  static const uint16_t GPS_PERIODIC_SENDS = 59;
  const bool query = gps_periodic.query_code != GPS_REQUEST_NONE;
  char built[14];
  gps_fixed_sentence sentence;

  switch (gps_periodic.stage) {
    case GPS_PERIODIC_GSV:
      if (gps_periodic.sends >= GPS_PERIODIC_SENDS) {
        sentence = gps_periodic_anc;
        gps_periodic.stage = GPS_PERIODIC_ANC;
      } else if (query) {
        sentence = gps_requests_periodic_query(built);
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
        sentence = gps_requests_periodic_query(built);
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
        sentence = gps_requests_periodic_query(built);
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

/* gps.channel_count from the engine's model, gps.field_592's top two
 * bits: the GT-74's six, the GT-77's eight, and twelve for the GT-80's
 * or both (0x5279a, the table at 0x527bc). */
void gps_channel_count_from_model(void) {
  static const uint8_t GT74_CHANNELS = 6;
  static const uint8_t GT77_CHANNELS = 8;
  static const uint8_t GT80_CHANNELS = 12;

  switch (gps.field_592 &
          (GPS_FIELD_592_MODEL_GT77 | GPS_FIELD_592_MODEL_GT80)) {
    case 0:
      gps.channel_count = GT74_CHANNELS;
      break;
    case GPS_FIELD_592_MODEL_GT77:
      gps.channel_count = GT77_CHANNELS;
      break;
    default:
      gps.channel_count = GT80_CHANNELS;
      break;
  }
}

/* Whether the engine's Time RAIM option is known to decide a request:
 * 1 for the TRAIM script, @@Bn or @@En once the receiver's ID has come,
 * with, through supported, whether the engine has the option, field_592's
 * bit 1 or its Time RAIM bit; 0 for any other or before the ID, supported
 * left as it was (0x52824). */
uint32_t gps_message_supported(int16_t code, uint8_t* supported) {
  if ((code != GPS_SCRIPT_TRAIM && code != GPS_REQUEST_TRAIM &&
       code != GPS_REQUEST_TRAIM_8) ||
      gps.receiver_id_received == 0) {
    return 0;
  }
  *supported =
      (gps.field_592 & (GPS_FIELD_592_BIT_1 | GPS_FIELD_592_TIME_RAIM)) != 0;
  return 1;
}
