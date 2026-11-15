/* From common/status.c, the 58503B 1.01.04's own beside common/'s, as the
 * Z3817A 3752's: of the status system's routines, those the function check
 * finds the decompiler's reading wrong for, each the same in both images
 * (docs/firmware/58503b-1.01.04.md, "The function check").  Each keeps its
 * generated prototype.  With them, from other images' rewritten C, of its
 * functions, those the 58503B's code carries identical or relocated, with the
 * static helpers they use (docs/firmware/58503b-1.01.04.md, "The link list");
 * their comments' addresses are the 58503B's, but where they name another
 * image. */

/* Sets a register's enable from nv_settings, reporting -300 to a source
 * that may not (0x48376). */
void status_enable_restore(int32_t index, int32_t source) {
  int8_t reg = index;

  if (status_preset_register(STATUS_MASK_ENABLE, source, reg,
                             nv_settings.status_enable[reg]) == 0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* Sets a filtered register's positive transition filter from
 * nv_settings, by its index less 2 (0x483b6). */
void status_ptr_restore(int32_t index, int32_t source) {
  int8_t filter = index;

  if (status_preset_register(STATUS_MASK_POSITIVE_TRANSITION, source, filter,
                             nv_settings.status_positive_transition[filter]) ==
      0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* The same for its negative transition filter (0x483f8). */
void status_ntr_restore(int32_t index, int32_t source) {
  int8_t filter = index;

  if (status_preset_register(STATUS_MASK_NEGATIVE_TRANSITION, source, filter,
                             nv_settings.status_negative_transition[filter]) ==
      0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* :STATus:QUEStionable:USER's hook: its condition from nv_settings
 * (0x4835a). */
void questionable_user_done(void) {
  status_happening(nv_settings.questionable_user != 0
                       ? STATUS_USER_REPORTED_SET
                       : STATUS_USER_REPORTED_CLEARED);
}

/* Raises a status condition once the status system is ready; entering
 * one of the lock and holdover states clears the other three, and one
 * of the empty conditions 0x36 and 0x37 besides (0x3f34e).
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

/* Replays a buffer's happenings from first to last (0x3f4c0).  A
 * buffer of reports replays nothing: the 58503A's drops the Z3805A's
 * call of status_report (0x3f4f2). */
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

/* A register's condition word; nothing calls it (0x3f30a). */
uint16_t status_condition_word(uint8_t index) {
  return status_ready_condition(index);
}

/* :STATus:OPERation:HARDware's condition word, 0 before the status
 * system is ready (0x3f45a). */
uint16_t status_hardware_condition(void) {
  /* The register's index (structures.md, "The status system"). */
  static const uint8_t HARDWARE_REGISTER = 6;

  return status_ready_condition(HARDWARE_REGISTER);
}

/* A register's event word, as status_read_event reads it, without
 * clearing it: the top register's the port's status byte (0x3706a). */
void status_peek_event(status_model* status, int32_t index, uint16_t* event) {
  const status_register* reg = status->registers[index];

  if (reg->parent_bit == 0) {
    *event = status->output->methods->status_byte_get(status->output);
    return;
  }
  *event = reg->event;
}

/* *CLS's clearing of status_object's events: the front panel's Shift
 * then Alt (0x3f33e). */
void status_events_clear(void) { status_clear_events(status_object); }

/* The alarm's text, the most severe of alarm_text_root's entries, at
 * most severity 4: the front panel's mode 9 and :ALARm?; true if any
 * (0x39062).  The image copies the text over itself at the end. */
uint8_t alarm_text(char* text) {
  /* The least severe an entry may be and still be taken. */
  static const uint8_t ALARM_SEVERITY_LEAST = 4;
  alarm_text_cursor cursor = {.text = text, .severity = ALARM_SEVERITY_LEAST};

  libc_copy(text, "");
  const uint8_t taken = alarm_text_collect(&alarm_text_root, &cursor);

  libc_copy(text, cursor.text);
  return taken;
}

/* The first register with transition filters, :STATus:OPERation; the
 * filters' arrays in nv_settings start with it. */
static const uint8_t STATUS_FIRST_FILTERED = 2;

/* Puts a code into the SCI session's status model once it is made, with
 * its message, as status_report does the SCPI port's, but with no early
 * buffer before: SCPI's for 0 and a negative code, status_report_
 * messages' from 1 to 0x16; 0 (0x3bd9e). */
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
  status_set_bit(sci_scpi_status_model, code, (int32_t)message);
  return 0;
}

/* Sets a register's enable or transition filter in the status model of
 * the request's port, and 1; 0 for a port with none (0x3f164).  The
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

/* An alarm text entry's name and how its register's transition filters
 * report its bit: "-NAME" for a clearing reported alone, "NAME" for a
 * setting alone, else "NAME ?" (0x38f62 to 0x3901c). */
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
 * if any was taken (0x38ed2).  A child's walk replaces the result, so
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

/* Reports a code to the SCI session's status model for the SCI's source
 * and startup; nothing for the rest, a SCPI command's among them, which
 * no port here gives (0x3bdee, its jump table at 0x3be06). */
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

/* The status model a port's requests report into: the SCI session's for
 * ports 1 and 2; for another, what the switch leaves in D0, the port less
 * 2 (0x3f140, its jump table at 0x3f158). */
status_model* port_status_model(char port) {
  /* The port the switch's index counts from. */
  static const int8_t PORT_SECOND = 2;

  if (port == SCPI_PORT_SCI || port == PORT_SECOND) {
    return sci_scpi_status_model;
  }
  return (status_model*)(int32_t)(port - PORT_SECOND);
}

/* The Standard Event register's positive filter, the bits status_init
 * sets it up with (0x3f29c). */
static const uint32_t STATUS_STANDARD_EVENT_POSITIVE = 0xbc;

/* Builds the status model, keeps it as the SCI session's too, sets its
 * registers up from status_register_layouts and the saved enables, and
 * marks the status system ready; status_create's error (0x3f228).  The
 * 58503A 3704's, the session's model set even when status_create fails,
 * the port object the SCI's. */
uint32_t status_init(void) {
  static const uint8_t STANDARD_EVENT_REGISTER = 1;

  status_request_service = 0;
  const uint32_t error =
      status_create(&status_object, STATUS_REGISTERS, STATUS_ERROR_QUEUE_ROOM,
                    status_conditions, 0);

  sci_scpi_status_model = status_object;
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

/* :STATus:PRESet's hook: the enables its form gives and the filtered
 * registers' filters, set for source 2, which port_status_model takes to
 * the SCI session's status model, then saved for the source (0x4843a, its
 * jump table at 0x48454).  The 58503A 3704's tables, whose Hardware
 * register leaves bit 5 out of its enables and filter. */
void status_preset_done(int32_t form, int32_t source) {
  /* The enables each form gives, by register (0x3f59c, 0x3f5aa). */
  static const uint16_t SCPI_ENABLES[] = {0, 0, 0, 0, 7, 0xf, 0x1fdf};
  static const uint16_t ALARM_ENABLES[] = {0x88, 0, 0x24, 3, 7, 8, 0x1fdf};
  /* The filters, by filtered register (0x3f5b8, 0x3f5c2). */
  static const uint16_t POSITIVE_FILTERS[] = {0x7f, 2, 7, 0xf, 0x13df};
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
