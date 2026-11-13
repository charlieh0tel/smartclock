/* From the 58503A 3704's rewritten/gps_requests.c, the Z3817A 3752's own beside
 * common/'s: of the GPS task's requests, the one the function check finds the
 * decompiler's reading wrong for, the same in both images: the request's
 * builder, whose message table entry, encoders and send filter the decompiler
 * called through untyped words, with the two helpers it runs them through
 * (docs/firmware/z3817a-3752.md, "The function check"); the senders' check, the
 * request ring, the scripts' lookup, the channels' send filter, and the
 * request's class, queue and admission and the Time RAIM test; and, last, Time
 * RAIM's send filter and the monitors' poster; each written as this image's
 * where its code differs ("The export and the checks").  Its comments'
 * addresses are the Z3817A's.  Each keeps its generated prototype. */

/* Runs an argument encoder of oncore_messages.  The table holds the
 * image's own routines, which return where the next byte goes in D0
 * alone, where gcc takes a pointer result from A0, so it is called as
 * returning a long; and which take the index in a 2-byte slot, at SP+8,
 * and out after it, at SP+10, as gps_request_build pushes them
 * (0x570b6 to 0x570ba), where gcc gives each a 4-byte slot; so the two are
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
 * SP+6 and SP+10 (0x57050 to 0x5705c), where gcc gives each a 4-byte slot; so
 * the three are passed as one structure, which gcc lays from the slot's start.
 */
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
 * where the Z3805A's asks gps_message_supported (0x5702a). */
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

/* The senders whose requests the task takes, ending in GPS_SENDER_NONE:
 * the image's table at 0x53e9a, five, as the Z3815A's, where the
 * 58503A's and the Z3805A's have four. */
static const gps_request_sender gps_requests_senders[] = {
    GPS_SENDER_SEQUENCE,       GPS_SENDER_SETTING,
    GPS_SENDER_SETTING_OTHER,  GPS_SENDER_COMMAND_MONITOR,
    GPS_SENDER_STATUS_MONITOR, GPS_SENDER_NONE};

/* 1 for a sender the task takes requests from (0x5272a). */
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

/* 1 if the ring holds no request (0x52566). */
bool request_ring_empty(void) {
  return gps_requests.read == gps_requests.write;
}

/* Empties the ring of waiting requests (0x52476). */
void request_ring_clear(void) {
  gps_requests.write = gps_requests.entries;
  gps_requests.read = gps_requests.entries;
}

/* 1 if the ring has no room for another (0x5257c). */
bool request_ring_full(void) {
  return gps_requests_after(gps_requests.write) == gps_requests.read;
}

/* Adds a request as the newest; 0 if the ring is full (0x52486). */
bool request_ring_put(gps_request_entry* entry) {
  gps_request_entry* next = gps_requests_after(gps_requests.write);

  if (next == gps_requests.read) {
    return 0;
  }
  *gps_requests.write = *entry;
  gps_requests.write = next;
  return 1;
}

/* Takes the oldest request; 0 if the ring is empty (0x524ce). */
int32_t request_ring_get(gps_request_entry* entry) {
  if (gps_requests.read == gps_requests.write) {
    return 0;
  }
  *entry = *gps_requests.read;
  gps_requests.read = gps_requests_after(gps_requests.read);
  return 1;
}

/* Takes back the newest request; 0 if the ring is empty (0x5251a). */
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
 * is waiting in the ring (0x525a2). */
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

/* The oldest waiting request with the code; NULL with none (0x52624). */
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
 * every other request put back in its order (0x5265a).  The image takes
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

/* The scripts by their codes from GPS_SCRIPT_INIT_DEFAULTS on. */
static gps_script_step** const gps_requests_scripts[] = {
    gps_script_53, gps_script_54, gps_script_55, gps_script_56, gps_script_57,
    gps_script_58, gps_script_59, gps_script_5a, gps_script_5b, gps_script_5c,
    gps_script_5d, gps_script_5e, gps_script_5f, gps_script_60, gps_script_61,
    gps_script_62, gps_script_63, gps_script_64, gps_script_65, gps_script_66};

/* A script code's steps, the empty script for any other; the first
 * script also resets gps (0x52918). */
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

/* @@Ba's, @@Ea's and @@Ek's send filter (oncore_messages entries 30, 50
 * and 51), and through oncore_filter_traim @@Bn's and @@En's: the
 * six-channel messages, @@Ba and @@Bn, to a six-channel engine, or to
 * an eight-channel one when the first argument, the output rate, encodes
 * as 0, a response once; the eight-channel ones, @@Ea, @@Ek and @@En,
 * to an eight-channel engine alone (0x56f28; docs/firmware/gps.md, "Six
 * or eight channels").  It passes @@Bk as it does @@Ba, but @@Bk's entry,
 * 39, has no filter, so goes to every engine.  Written by hand: it runs
 * the message's first encoder, which takes its index in a 2-byte slot. */
uint8_t oncore_filter_channels(int16_t index, oncore_encoder* encoder,
                               int32_t* arguments) {
  const uint8_t channels = gps.channel_count;
  uint8_t rate;

  gps_requests_encode(encoder, arguments, 0, &rate);
  if ((index == GPS_REQUEST_POSITION_STATUS ||
       index == GPS_REQUEST_POSITION_EXTENSION || index == GPS_REQUEST_TRAIM) &&
      (channels == GPS_SIX_CHANNELS ||
       (channels == GPS_CHANNELS && rate == 0))) {
    return 1;
  }
  return (index == GPS_REQUEST_POSITION_STATUS_8 ||
          index == GPS_REQUEST_POSITION_EXTENSION_8 ||
          index == GPS_REQUEST_TRAIM_8) &&
         channels == GPS_CHANNELS;
}

/* A request code's class, by its range and, for a message, its argument
 * mode, 0 to send it and 1 to query it (0x528ce). */
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

/* Queues a request the task has received: a link reset aborts the
 * running request and every waiting one and goes in alone; another is
 * refused as busy while one from its sender runs or waits, or when the
 * ring is full, unless it can replace the newest, which is answered busy
 * in its place (0x52746). */
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

/* Whether the task takes a request: a sequence's or a setting's, senders
 * 1 to 3, only if its code is in gps_admitted_requests (0x53e84):
 * the status snapshot, the status, visibility, best combinations and
 * time RAIM printouts, the GPS hysteresis, the status monitor, the link
 * reset and step 0x52, which does nothing; any other sender's always.  The
 * image first compares the list with the word after its end mark (0x53e98),
 * which never holds, where the 58503A's and the Z3815A's compare their empty
 * list with itself and so admit every request (0x52abc). */
uint8_t gps_request_admitted(gps_request_sender sender,
                             gps_request_code_word code) {
  if (sender != GPS_SENDER_SEQUENCE && sender != GPS_SENDER_SETTING &&
      sender != GPS_SENDER_SETTING_OTHER) {
    return 1;
  }
  for (const gps_request_code_word* admitted = gps_admitted_requests;
       *admitted != GPS_REQUEST_NONE; admitted++) {
    if (*admitted == code) {
      return 1;
    }
  }
  return 0;
}

/* Whether the engine's Time RAIM option decides a request: 1 for the
 * TRAIM script, @@Bn or @@En, with, through supported, whether the engine
 * has the option, gps.field_592's MODEL_R or its Time RAIM; 0 for any
 * other, supported left as it was (0x56fcc).  Where the Z3815A's also
 * waits for the receiver's ID, the image tests a constant 1 (0x56fe4). */
uint32_t gps_message_supported(int16_t code, uint8_t* supported) {
  if (code != GPS_SCRIPT_TRAIM && code != GPS_REQUEST_TRAIM &&
      code != GPS_REQUEST_TRAIM_8) {
    return 0;
  }
  *supported =
      (gps.field_592 & (GPS_FIELD_592_MODEL_R | GPS_FIELD_592_TIME_RAIM)) != 0;
  return 1;
}

/* @@Bn's and @@En's send filter: oncore_filter_channels while the engine
 * has Time RAIM (0x56f98). */
uint8_t oncore_filter_traim(int16_t index, oncore_encoder* encoder,
                            int32_t* arguments) {
  if ((gps.field_592 & GPS_FIELD_592_TIME_RAIM) == 0) {
    return 0;
  }
  return oncore_filter_channels(index, encoder, arguments);
}

/* Sends a request to the task's exchange, marking the command or the
 * status monitor's result pending, as the sender is either: for
 * GPS_POST_STEP the code, mode and arguments of the step at the place
 * query gives, else the code with a copy of the four arguments given in
 * gps_post_arguments, or none (0x527fe).  The 58503A 3704's takes the
 * step's address in arguments[0] and sends a code's request without
 * arguments. */
bool gps_request_post(gps_request_sender sender, gps_request_code_word code,
                      int32_t* arguments, gps_script_step** query,
                      uint8_t sent) {
  static const uint8_t ARGUMENTS =
      sizeof gps_post_arguments / sizeof gps_post_arguments[0];
  gps_request_entry request;

  request.field_00 = 0;
  request.field_04 = 0;
  request.sender = sender;
  if (code == GPS_POST_STEP) {
    gps_script_step* step = *query;

    request.code = step->code;
    request.argument_mode = step->argument_mode;
    request.arguments = step->arguments;
  } else {
    request.code = code;
    request.argument_mode = GPS_ARGUMENTS_SET;
    if (arguments == NULL) {
      request.arguments = NULL;
    } else {
      for (uint8_t i = 0; i < ARGUMENTS; i++) {
        gps_post_arguments[i] = arguments[i];
      }
      request.arguments = gps_post_arguments;
    }
  }
  request.sent = sent;
  request.result = GPS_RESULT_REJECT;
  switch (sender) {
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
