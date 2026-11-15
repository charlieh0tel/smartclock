/* From the 58503A 3704's rewritten/gps_requests.c, the Z3816A 4001's own
 * beside common/'s: gps_request_build, which runs the message table's encoders
 * and send filters, the image's own routines, with their arguments in the
 * slots they read, where the decompiler's reading passes them otherwise; the
 * 58503A's code but where this image's differs: its message codes, its Time
 * RAIM tail, its self-test's result cleared as @@Ca or @@Fa is sent, and a
 * height's type sent as GPS's unless gps.engine has it MSL; and
 * gps_request_post, whose decompiler's reading lays the request it sends over
 * separate locals, the 58503A's code but for its step, its arguments and its
 * third monitor (docs/firmware/z3816a-4001.md, "The function check").  Its
 * comments' addresses are the Z3816A's. Each function keeps its generated
 * prototype. With them, from the 58503A 3704's and the Z3815A 4010's
 * rewritten/gps_requests.c, of its functions, those the Z3816A's code carries
 * (identical or relocated by image_diff, or changed with the same
 * instructions), with the static helpers they use, each the same here but
 * where its comment says otherwise (docs/firmware/z3816a-4001.md, "The link
 * check").  Their comments' addresses are the Z3816A's. */

/* Runs an argument encoder of oncore_messages.  The table holds the
 * image's own routines, which return where the next byte goes in D0
 * alone, where gcc takes a pointer result from A0, so it is called as
 * returning a long; and which take the index in a 2-byte slot, at SP+8,
 * and out after it, at SP+10 (0x561a4 to 0x561b4), where gcc gives each
 * a 4-byte slot; so the two are passed as one structure, which gcc lays
 * from the slot's start. */
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
 * SP+6 and SP+10 (0x5613e to 0x5614a), where gcc gives each a 4-byte
 * slot; so the three are passed as one structure, which gcc lays from
 * the slot's start. */
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
 * send filter refuses (0x56112). */
int16_t gps_request_build(gps_request_code_word message, gps_argument_mode mode,
                          int32_t* arguments, uint8_t* out) {
  static const uint8_t ENCODERS = sizeof oncore_messages[0].encoders /
                                  sizeof oncore_messages[0].encoders[0];
  /* A query form's stand-in for a zero byte, which would end it. */
  static const uint8_t QUERY_ZERO = 0xee;
  /* @@Bn's and @@En's fields after those their encoders send, zeros
   * (0x56204 to 0x5620e), where the 58503A's sends 0, 0, 1 and zeros. */
  static const uint8_t TRAIM_UNUSED = 10;
  /* The self-test's result as @@Ca or @@Fa is sent: none yet. */
  static const uint16_t SELFTEST_PENDING = 0xffff;
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
      for (uint8_t i = 0; i < TRAIM_UNUSED; i++) {
        *next++ = 0;
      }
    } else if (message == GPS_REQUEST_SELFTEST ||
               message == GPS_REQUEST_SELFTEST_8) {
      gps.self_test_mask = SELFTEST_PENDING;
    }
    /* A height's last byte is its type, sent as GPS's unless the engine
     * is to give MSL heights (0x56226 to 0x56244). */
    if ((gps.engine & GPS_ENGINE_HEIGHT_MSL) == 0 &&
        (message == GPS_REQUEST_HEIGHT ||
         message == GPS_REQUEST_POSITION_HOLD_POSITION ||
         message == GPS_REQUEST_ALTITUDE_HOLD_HEIGHT)) {
      next[-1] = ONCORE_HEIGHT_GPS;
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
  *next = oncore_checksum(out + (uint8_t)libc_length("@@"), next);
  next++;
  *next++ = '\r';
  *next++ = '\n';
  return (int16_t)(next - out);
}

/* Sends a monitor's request to the task's exchange, marking that
 * monitor's result pending: for GPS_POST_STEP the code, mode and
 * arguments of the step *step points at (0x50946), else the code with
 * arguments as its arguments (0x5095e).  The 58503A 3704's takes the
 * step through its arguments and sends none with a code. */
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
  if (sender == GPS_SENDER_COMMAND_MONITOR) {
    command_monitor_result = GPS_RESULT_PENDING;
  } else if (sender == GPS_SENDER_STATUS_MONITOR) {
    status_monitor_result = GPS_RESULT_PENDING;
  } else if (sender == GPS_SENDER_POLL_MONITOR) {
    poll_monitor_result = GPS_RESULT_PENDING;
  }
  return (uint8_t)psos_send_x(gpsx_exchange, &request) == 0;
}

/* A request code's class, by its range and, for a message, its argument
 * mode, 0 to send it and 1 to query it (0x509d6). */
gps_code_class gps_request_class(gps_request_code_word code,
                                 gps_argument_mode argument_mode) {
  if (code >= GPS_REQUEST_TIME_OF_DAY &&
      code <= GPS_REQUEST_FIX_ALGORITHM_SECOND &&
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

/* The senders whose requests the task takes, ending in GPS_SENDER_NONE:
 * the image's table (0x5239a). */
static const gps_request_sender gps_requests_senders[] = {
    GPS_SENDER_SEQUENCE,
    GPS_SENDER_SETTING,
    GPS_SENDER_SETTING_OTHER,
    GPS_SENDER_COMMAND_MONITOR,
    GPS_SENDER_STATUS_MONITOR,
    GPS_SENDER_POLL_MONITOR,
    GPS_SENDER_NONE};

/* 1 for a sender the task takes requests from. */
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

/* 1 if the ring holds no request (0x5068e). */
bool request_ring_empty(void) {
  return gps_requests.read == gps_requests.write;
}

/* Empties the ring of waiting requests (0x5059e). */
void request_ring_clear(void) {
  gps_requests.write = gps_requests.entries;
  gps_requests.read = gps_requests.entries;
}

/* 1 if the ring has no room for another (0x506a4). */
bool request_ring_full(void) {
  return gps_requests_after(gps_requests.write) == gps_requests.read;
}

/* Adds a request as the newest; 0 if the ring is full (0x505ae). */
bool request_ring_put(gps_request_entry* entry) {
  gps_request_entry* next = gps_requests_after(gps_requests.write);

  if (next == gps_requests.read) {
    return 0;
  }
  *gps_requests.write = *entry;
  gps_requests.write = next;
  return 1;
}

/* Takes the oldest request; 0 if the ring is empty (0x505f6). */
int32_t request_ring_get(gps_request_entry* entry) {
  if (gps_requests.read == gps_requests.write) {
    return 0;
  }
  *entry = *gps_requests.read;
  gps_requests.read = gps_requests_after(gps_requests.read);
  return 1;
}

/* Takes back the newest request; 0 if the ring is empty (0x50642). */
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
 * is waiting in the ring (0x506ca). */
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

/* The oldest waiting request with the code; NULL with none (0x5074c). */
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
 * gps_console_request does a console line's, and counts it a success:
 * taken from the ring's oldest end, or by turning the ring once round,
 * every other request put back in its order (0x50782).  The image takes
 * it from the newest end when it is at gps_requests.write, which
 * request_ring_find never returns, the newest being the entry before. */
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
 * in its place (0x5086e). */
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

/* 1 if the newest waiting request is a monitor's not yet sent, which a
 * new request may replace. */
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

/* Whether the task takes a request from a sender: always.  The image
 * would look the code of a sequence's or a setting's request, senders 1
 * to 3, up in a list at 0x594da, refusing none found, but the list is
 * empty, its end mark alone, and the test the compiler made of it is
 * always true (0x50bba). */
uint8_t gps_request_admitted(gps_request_sender sender,
                             gps_request_code_word code) {
  return 1;
}

/* @@Ba's and @@Ea's send filter, and @@Ek's: encodes the first argument,
 * the rate; 1 for @@Ba, @@Bk or @@Bn on a VP of six channels, or of
 * eight while the rate is 0, and for @@Ea or @@En, or @@Ek on a VP, with
 * eight channels (0x55f68). */
uint8_t oncore_filter_channels(int16_t index, oncore_encoder* encoder,
                               int32_t* arguments) {
  const uint8_t channels = gps.channel_count;
  const bool vp = (gps.engine & GPS_ENGINE_VP) != 0;
  uint8_t rate;

  gps_requests_encode(encoder, arguments, 0, &rate);
  if ((index == GPS_REQUEST_POSITION_STATUS ||
       index == GPS_REQUEST_POSITION_EXTENSION || index == GPS_REQUEST_TRAIM) &&
      vp &&
      (channels == GPS_SIX_CHANNELS ||
       (channels == GPS_CHANNELS && rate == 0))) {
    return 1;
  }
  return (index == GPS_REQUEST_POSITION_STATUS_8 ||
          (index == GPS_REQUEST_POSITION_EXTENSION_8 && vp) ||
          index == GPS_REQUEST_TRAIM_8) &&
         channels == GPS_CHANNELS;
}

/* Whether a self-test's result has a test failed, of the 14 the generic
 * word gives. */
static bool gps_requests_selftest_failed(void) {
  static const uint16_t TESTS = 0x3fff;

  return (gps.self_test & TESTS) != 0;
}

/* The second @@Ca's send filter: an engine of six channels whose last
 * self-test failed, to run it again (0x55fee). */
uint8_t oncore_filter_selftest_again(int16_t index, oncore_encoder* encoder,
                                     int32_t* arguments) {
  return oncore_filter_six_channels() != 0 && gps_requests_selftest_failed();
}

/* The second @@Fa's send filter: an engine of eight channels whose last
 * self-test failed (0x56024). */
uint8_t oncore_filter_selftest_8_again(int16_t index, oncore_encoder* encoder,
                                       int32_t* arguments) {
  return oncore_filter_eight_channels() != 0 && gps_requests_selftest_failed();
}

/* @@Bn's and @@En's send filter: oncore_filter_channels while the engine
 * has Time RAIM (0x5605a). */
uint8_t oncore_filter_traim(int16_t index, oncore_encoder* encoder,
                            int32_t* arguments) {
  if ((gps.engine & GPS_ENGINE_TRAIM) == 0) {
    return 0;
  }
  return oncore_filter_channels(index, encoder, arguments);
}

/* Whether the engine's Time RAIM is known to decide a request: 1 for the
 * TRAIM script, @@Bn or @@En once the receiver's ID has come, with,
 * through supported, whether the engine is a UT Plus or has Time RAIM;
 * 0 for any other or before the ID, supported left as it was
 * (0x560b6). */
uint32_t gps_message_supported(int16_t code, uint8_t* supported) {
  if ((code != GPS_SCRIPT_TRAIM && code != GPS_REQUEST_TRAIM &&
       code != GPS_REQUEST_TRAIM_8) ||
      gps.receiver_id_received == 0) {
    return 0;
  }
  *supported = (gps.engine & (GPS_ENGINE_UT_PLUS | GPS_ENGINE_TRAIM)) != 0;
  return 1;
}

/* From a place in a null-ended list of steps, the first after it, from
 * the list's start again past its end, whose message's send filter
 * passes it; the place itself where none does, or where it holds the
 * null (0x50204). */
gps_script_step** gps_script_next_sendable(gps_script_step** place,
                                           gps_script_step** list) {
  gps_script_step** next = place;

  if (*place == NULL) {
    return place;
  }
  do {
    next++;
    if (*next == NULL) {
      next = list;
    }
    gps_script_step* step = *next;
    const oncore_message_entry* entry = oncore_message_entry_at(step->code);

    if (entry->filter == NULL ||
        gps_requests_filter(entry->filter, step->code, entry->encoders[0],
                            step->arguments) != 0) {
      break;
    }
  } while (next != place);
  return next;
}

/* The scripts by their codes from GPS_SCRIPT_INIT_DEFAULTS on, by
 * gps_script_lookup's jump table (0x50a3e). */
static gps_script_step** const gps_requests_scripts[] = {
    gps_script_5b, gps_script_5c, gps_script_5d, gps_script_5e, gps_script_5f,
    gps_script_60, gps_script_61, gps_script_62, gps_script_63, gps_script_64,
    gps_script_65, gps_script_66, gps_script_67, gps_script_68, gps_script_69,
    gps_script_6a, gps_script_6b, gps_script_6c, gps_script_6d, gps_script_6e};

/* A script code's steps, the empty script for any other; the first
 * script also resets gps (0x50a20). */
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
