/* status.c's functions that are this image's own: the rest are
 * common/status.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Of the status system's routines, those the function check
 * finds the decompiler's reading wrong for, from the Z3815A 4010's and the
 * 58503A 3704's, and status_report_from, port_status_model and
 * status_init, this image's own (docs/firmware/z3817a-3752.md, "The
 * function check").  Its comments' addresses are the Z3817A's, but where
 * they name another image.  Each keeps its generated prototype. */

/* The first register with transition filters, :STATus:OPERation; the
 * filters' arrays in nv_settings start with it. */
static const uint8_t STATUS_FIRST_FILTERED = 2;

/* Sets a register's enable from nv_settings, reporting -300 to a source
 * that may not (0x4861e). */
void status_enable_restore(int32_t index, int32_t source) {
  int8_t reg = index;

  if (status_preset_register(STATUS_MASK_ENABLE, source, reg,
                             nv_settings.status_enable[reg]) == 0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* Sets a filtered register's positive transition filter from
 * nv_settings, by its index less 2 (0x4865e). */
void status_ptr_restore(int32_t index, int32_t source) {
  int8_t filter = index;

  if (status_preset_register(STATUS_MASK_POSITIVE_TRANSITION, source, filter,
                             nv_settings.status_positive_transition[filter]) ==
      0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* The same for its negative transition filter (0x486a0). */
void status_ntr_restore(int32_t index, int32_t source) {
  int8_t filter = index;

  if (status_preset_register(STATUS_MASK_NEGATIVE_TRANSITION, source, filter,
                             nv_settings.status_negative_transition[filter]) ==
      0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* :STATus:QUEStionable:USER's hook: its condition from nv_settings
 * (0x48602). */
void questionable_user_done(void) {
  status_happening(nv_settings.questionable_user != 0
                       ? STATUS_USER_REPORTED_SET
                       : STATUS_USER_REPORTED_CLEARED);
}

/* Raises a status condition once the status system is ready; entering
 * one of the lock and holdover states clears the other three, and one
 * of the empty conditions 0x36 and 0x37 besides (0x3fff6).
 * STATUS_INPUT_OVERRUN, which :DIAG:STAT:HAPP 0 raises, does nothing. */
void status_happening(status_condition_code condition) {
  if (status_ready == 0) {
    status_event_hook((uint32_t)&early_status_happenings, condition);
    return;
  }
  if (condition == STATUS_INPUT_OVERRUN) {
    return;
  }
  status_clear_bit(status_object, condition);
  switch (condition) {
    case STATUS_LOCKED_SET:
      status_clear_bit(status_object, STATUS_HOLDING_CLEARED);
      status_clear_bit(status_object, STATUS_WAITING_CLEARED);
      status_clear_bit(status_object, STATUS_RECOVERING_CLEARED);
      status_clear_bit(status_object, STATUS_CONDITION_37);
      break;
    case STATUS_HOLDING_SET:
      status_clear_bit(status_object, STATUS_LOCKED_CLEARED);
      status_clear_bit(status_object, STATUS_WAITING_CLEARED);
      status_clear_bit(status_object, STATUS_RECOVERING_CLEARED);
      status_clear_bit(status_object, STATUS_CONDITION_36);
      break;
    case STATUS_WAITING_SET:
      status_clear_bit(status_object, STATUS_LOCKED_CLEARED);
      status_clear_bit(status_object, STATUS_HOLDING_CLEARED);
      status_clear_bit(status_object, STATUS_RECOVERING_CLEARED);
      status_clear_bit(status_object, STATUS_CONDITION_36);
      break;
    case STATUS_RECOVERING_SET:
      status_clear_bit(status_object, STATUS_LOCKED_CLEARED);
      status_clear_bit(status_object, STATUS_HOLDING_CLEARED);
      status_clear_bit(status_object, STATUS_WAITING_CLEARED);
      status_clear_bit(status_object, STATUS_CONDITION_36);
      break;
    default:
      break;
  }
}

/* Replays a buffer's happenings from first to last (0x40168).  A
 * buffer of reports replays nothing: the 58503A's drops the Z3805A's
 * call of status_report (0x4019a). */
void happening_replay(char* events, status_replay_mode mode, char first,
                      char last) {
  for (int8_t index = first; index <= last; index++) {
    switch (mode) {
      case STATUS_REPLAY_REPORTS:
        break;
      case STATUS_REPLAY_HAPPENINGS:
        status_happening(events[index]);
        break;
    }
  }
}

/* A register's condition word once the status system is ready, else 0. */
static uint16_t status_ready_condition(uint8_t index) {
  uint16_t condition;

  if (status_ready == 0) {
    return 0;
  }
  status_read_condition(status_object, index, &condition);
  return condition;
}

/* A register's condition word; nothing calls it (0x3ffb2). */
uint16_t status_condition_word(uint8_t index) {
  return status_ready_condition(index);
}

/* :STATus:OPERation:HARDware's condition word, 0 before the status
 * system is ready (0x40102). */
uint16_t status_hardware_condition(void) {
  /* The register's index (structures.md, "The status system"). */
  static const uint8_t HARDWARE_REGISTER = 6;

  return status_ready_condition(HARDWARE_REGISTER);
}

/* Clears status_object's events; nothing calls it here, where the
 * 58503A's front panel does for Shift then Alt (0x3ffe6). */
void status_events_clear(void) { status_clear_events(status_object); }

/* A register's event word, as status_read_event reads it, without
 * clearing it: the top register's the port's status byte (0x37fe4). */
void status_peek_event(status_model* status, int32_t index, uint16_t* event) {
  const status_register* reg = status->registers[index];

  if (reg->parent_bit == 0) {
    *event = status->output->methods->status_byte_get(status->output);
    return;
  }
  *event = reg->event;
}

/* An alarm text entry's name and how its register's transition filters
 * report its bit: "-NAME" for a clearing reported alone, "NAME" for a
 * setting alone, else "NAME ?" (0x39eb8 to 0x39f72). */
static void alarm_text_name(char* text, const char* name, bool positive,
                            bool negative) {
  if (negative && !positive) {
    libc_copy(text, "-");
    strcat(text, name);
    return;
  }
  libc_copy(text, name);
  if (!positive || negative) {
    strcat(text, " ?");
  }
}

/* Walks an alarm text node: for each entry whose bit its register both
 * enables in nv_settings and holds as an event, the most severe so far
 * replaces the cursor's text with its name, and a child is walked; true
 * if any was taken (0x39e28).  A child's walk replaces the result, so
 * that one taken before it is forgotten if the child takes none. */
uint8_t alarm_text_collect(alarm_text_node* node, alarm_text_cursor* cursor) {
  uint16_t event = 0;
  uint8_t taken = 0;

  for (uint32_t bit = 0; node->entries[bit].name != NULL; bit++) {
    const alarm_text_entry* entry = &node->entries[bit];
    const uint32_t mask = 1 << bit;

    status_peek_event(status_object, node->register_index, &event);
    if ((nv_settings.status_enable[node->register_index] & mask) == 0 ||
        ((int16_t)event & mask) == 0) {
      continue;
    }
    if (entry->severity < cursor->severity) {
      const uint8_t filter = node->register_index - STATUS_FIRST_FILTERED;

      cursor->severity = entry->severity;
      alarm_text_name(
          cursor->text, entry->name,
          (nv_settings.status_positive_transition[filter] & mask) != 0,
          (nv_settings.status_negative_transition[filter] & mask) != 0);
      taken = 1;
    }
    if (entry->child != NULL) {
      taken = alarm_text_collect(entry->child, cursor);
    }
  }
  return taken;
}

/* The alarm's text, the most severe of alarm_text_root's entries, at
 * most severity 4, :ALARm?'s (0x3aebe); true if any (0x39fb8).  The image
 * copies the text over itself at the end. */
uint8_t alarm_text(char* text) {
  /* The least severe an entry may be and still be taken. */
  static const uint8_t ALARM_SEVERITY_LEAST = 4;
  alarm_text_cursor cursor = {.text = text, .severity = ALARM_SEVERITY_LEAST};

  libc_copy(text, "");
  const uint8_t taken = alarm_text_collect(&alarm_text_root, &cursor);

  libc_copy(text, cursor.text);
  return taken;
}

/* Sets a register's enable or transition filter in the status model of
 * the request's port, and 1; 0 for a port with none (0x3fe0c).  The
 * Z3805A's takes status_object for SCPI's requests alone. */
uint32_t status_preset_register(status_register_mask mask,
                                request_source source, int16_t index,
                                int16_t value) {
  status_model* status = port_status_model(source);

  if (status == NULL) {
    return 0;
  }
  switch (mask) {
    case STATUS_MASK_ENABLE:
      status_register_set_enable(status, index, value);
      break;
    case STATUS_MASK_POSITIVE_TRANSITION:
      status_register_set_ptr(status, index + STATUS_FIRST_FILTERED, value);
      break;
    case STATUS_MASK_NEGATIVE_TRANSITION:
      status_register_set_ntr(status, index + STATUS_FIRST_FILTERED, value);
      break;
  }
  return 1;
}

/* Puts a code into the session's status model once it is made, with its
 * message, as status_report does, but with no early buffer before:
 * SCPI's for 0 and a negative code, status_report_messages' from 1 to
 * 0x16; 0 (0x3cbf6).  The Z3815A's SCI session's, on this image's one
 * session's model and status_ready. */
uint32_t sci_scpi_status_report(int16_t code) {
  if (status_ready == 0) {
    return 0;
  }
  char* message;

  if (code <= 0) {
    message = error_message_lookup(code);
  } else if (code < (int16_t)(sizeof status_report_messages /
                              sizeof *status_report_messages)) {
    message = status_report_messages[code];
  } else {
    message = 0;
  }
  /* The message is the error queue entry's value. */
  status_set_bit(scpi_status_model, code, (int32_t)message);
  return 0;
}

/* :STATus:PRESet's hook: the enables its form gives and the filtered
 * registers' filters, set and saved for the source (0x486e2).  The
 * 58503A 3704's code with this image's tables, whose Hardware register
 * takes bits 0 to 14 but 5 enabled and those but 10 and 11 filtered,
 * where the 58503A's stops at bit 12 and the Z3815A's takes bit 5; each
 * restore reports for REQUEST_SOURCE_STARTUP (0x48720), which
 * status_report_from reports to the SCI's session here, where the
 * Z3815A's reports it to its SCPI port's. */
void status_preset_done(int32_t form, int32_t source) {
  /* The enables each form gives, by register (0x40244, 0x40252). */
  static const uint16_t SCPI_ENABLES[] = {0, 0, 0, 0, 7, 0xf, 0x7fdf};
  static const uint16_t ALARM_ENABLES[] = {0x88, 0, 0x24, 3, 7, 8, 0x7fdf};
  /* The filters, by filtered register (0x40260, 0x4026a). */
  static const uint16_t POSITIVE_FILTERS[] = {0x7f, 2, 7, 0xf, 0x73df};
  static const uint16_t NEGATIVE_FILTERS[] = {0, 0, 0, 0, 0};

  switch ((status_preset_form)form) {
    case STATUS_PRESET_SCPI:
      for (uint8_t index = STATUS_FIRST_FILTERED; index < STATUS_REGISTERS;
           index++) {
        nv_settings.status_enable[index] = SCPI_ENABLES[index];
        status_enable_restore(index, REQUEST_SOURCE_STARTUP);
      }
      break;
    case STATUS_PRESET_ALARM:
      for (uint8_t index = 0; index < STATUS_REGISTERS; index++) {
        nv_settings.status_enable[index] = ALARM_ENABLES[index];
        status_enable_restore(index, REQUEST_SOURCE_STARTUP);
      }
      nv_settings.questionable_user = 0;
      questionable_user_done();
      break;
  }
  for (uint8_t filter = 0; filter < STATUS_REGISTERS - STATUS_FIRST_FILTERED;
       filter++) {
    nv_settings.status_positive_transition[filter] = POSITIVE_FILTERS[filter];
    status_ptr_restore(filter, REQUEST_SOURCE_STARTUP);
    nv_settings.status_negative_transition[filter] = NEGATIVE_FILTERS[filter];
    status_ntr_restore(filter, REQUEST_SOURCE_STARTUP);
  }
  nv_save(source);
}

/* sci_scpi_status_report(code), the session's, for REQUEST_SOURCE_SCI
 * and REQUEST_SOURCE_STARTUP, by its jump table (0x3cc5e); nothing for
 * the rest, REQUEST_SOURCE_SCPI among them (0x3cc46). */
void status_report_from(int16_t code, request_source source) {
  switch (source) {
    case REQUEST_SOURCE_SCI:
    case REQUEST_SOURCE_STARTUP:
      sci_scpi_status_report(code);
      break;
    default:
      break;
  }
}

/* The Z3815A's port number for DUART channel B, which port_status_model
 * still answers for. */
static const char STATUS_DUART_B_PORT = 2;

/* The status model of the SCI, port 1, and of port 2, the Z3815A's
 * DUART channel B, both scpi_status_model; for another port the port
 * less 2, what the switch leaves in D0 (0x3fde8). */
status_model* port_status_model(char port) {
  if (port == SCPI_PORT_SCI || port == STATUS_DUART_B_PORT) {
    return scpi_status_model;
  }
  return (status_model*)(int32_t)(port - STATUS_DUART_B_PORT);
}

/* The Standard Event register's positive filter (0x3ff44). */
static const uint32_t STATUS_STANDARD_EVENT_POSITIVE = 0xbc;

/* Builds the status model, keeps it as scpi_status_model too, sets its
 * registers up from status_register_layouts and the saved enables, and
 * marks the status system ready; status_create's error (0x3fed0).  The
 * 58503A's, the session's port object the SCI's.  scpi_status_model is
 * set even when status_create fails. */
uint32_t status_init(void) {
  static const uint8_t STANDARD_EVENT_REGISTER = 1;

  status_request_service = 0;
  const uint32_t error =
      status_create(&status_object, STATUS_REGISTERS, STATUS_ERROR_QUEUE_ROOM,
                    sci_status_conditions, 0);

  scpi_status_model = status_object;
  if (error != 0) {
    return error;
  }
  status_register_setup((int32_t)status_object, 0, 0, 0, 0, 0,
                        (int32_t)port_object(SCPI_PORT_SCI));
  const status_register_layout* standard_event =
      &status_register_layouts[STANDARD_EVENT_REGISTER];

  status_register_setup((int32_t)status_object, STANDARD_EVENT_REGISTER,
                        STATUS_STANDARD_EVENT_POSITIVE, 0,
                        standard_event->parent_index,
                        standard_event->parent_bit, 0);
  for (uint8_t index = STATUS_FIRST_FILTERED; index < STATUS_REGISTERS;
       index++) {
    const status_register_layout* layout = &status_register_layouts[index];

    status_register_setup((int32_t)status_object, index, *layout->positive,
                          *layout->negative, layout->parent_index,
                          layout->parent_bit, 0);
  }
  status_enables_init();
  questionable_user_done();
  status_ready = 1;
  return 0;
}
