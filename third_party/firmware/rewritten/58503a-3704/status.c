/* status.c's functions that are this image's own: the rest are
 * common/status.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The first register with transition filters, :STATus:OPERation; the
 * filters' arrays in nv_settings start with it. */
static const uint8_t STATUS_FIRST_FILTERED = 2;

/* Sets a register's enable from nv_settings, reporting -300 to a source
 * that may not (0x434a2). */
void status_enable_restore(int32_t index, int32_t source) {
  int8_t reg = index;

  if (status_preset_register(STATUS_MASK_ENABLE, source, reg,
                             nv_settings.status_enable[reg]) == 0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* Sets a filtered register's positive transition filter from
 * nv_settings, by its index less 2 (0x434e2). */
void status_ptr_restore(int32_t index, int32_t source) {
  int8_t filter = index;

  if (status_preset_register(STATUS_MASK_POSITIVE_TRANSITION, source, filter,
                             nv_settings.status_positive_transition[filter]) ==
      0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* The same for its negative transition filter (0x43524). */
void status_ntr_restore(int32_t index, int32_t source) {
  int8_t filter = index;

  if (status_preset_register(STATUS_MASK_NEGATIVE_TRANSITION, source, filter,
                             nv_settings.status_negative_transition[filter]) ==
      0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* The Standard Event register's positive transition filter, its defined
 * bits, 2 to 5 and 7 (0x2f14c).  The Z3805A's status_init reads the
 * register's filters through status_register_layouts' NULL pointers;
 * the 58503A's passes this and 0. */
static const uint32_t STATUS_STANDARD_EVENT_POSITIVE = 0xbc;

/* The console's port, whose object status_init gives the top register
 * (0x2f11e). */
static const char STATUS_CONSOLE_PORT = 2;

/* Builds the status model, keeps it as scpi_status_model too, sets its
 * registers up from status_register_layouts and the saved enables, and
 * marks the status system ready; status_create's error (0x2f0d8).
 * scpi_status_model is set even when status_create fails. */
uint32_t status_init(void) {
  static const uint8_t STANDARD_EVENT_REGISTER = 1;

  status_request_service = 0;
  const uint32_t error =
      status_create(&status_object, STATUS_REGISTERS, STATUS_ERROR_QUEUE_ROOM,
                    status_conditions, 0);

  scpi_status_model = status_object;
  if (error != 0) {
    return error;
  }
  status_register_setup((int32_t)status_object, 0, 0, 0, 0, 0,
                        (int32_t)port_object(STATUS_CONSOLE_PORT));
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
 * registers' filters, set and saved for the source (0x43566).  The
 * Hardware register's bit 5, "not used", is left out of its enables and
 * filter, where the Z3805A's has it. */
void status_preset_done(int32_t form, int32_t source) {
  /* The enables each form gives, by register (0x2f44c, 0x2f45a). */
  static const uint16_t SCPI_ENABLES[] = {0, 0, 0, 0, 7, 0xf, 0x1fdf};
  static const uint16_t ALARM_ENABLES[] = {0x88, 0, 0x24, 3, 7, 8, 0x1fdf};
  /* The filters, by filtered register (0x2f468, 0x2f472). */
  static const uint16_t POSITIVE_FILTERS[] = {0x7f, 2, 7, 0xf, 0x13df};
  static const uint16_t NEGATIVE_FILTERS[] = {0, 0, 0, 0, 0};

  switch ((status_preset_form)form) {
    case STATUS_PRESET_SCPI:
      for (uint8_t index = STATUS_FIRST_FILTERED; index < STATUS_REGISTERS;
           index++) {
        nv_settings.status_enable[index] = SCPI_ENABLES[index];
        status_enable_restore(index, REQUEST_SOURCE_SCPI);
      }
      break;
    case STATUS_PRESET_ALARM:
      for (uint8_t index = 0; index < STATUS_REGISTERS; index++) {
        nv_settings.status_enable[index] = ALARM_ENABLES[index];
        status_enable_restore(index, REQUEST_SOURCE_SCPI);
      }
      nv_settings.questionable_user = 0;
      questionable_user_done();
      break;
  }
  for (uint8_t filter = 0; filter < STATUS_REGISTERS - STATUS_FIRST_FILTERED;
       filter++) {
    nv_settings.status_positive_transition[filter] = POSITIVE_FILTERS[filter];
    status_ptr_restore(filter, REQUEST_SOURCE_SCPI);
    nv_settings.status_negative_transition[filter] = NEGATIVE_FILTERS[filter];
    status_ntr_restore(filter, REQUEST_SOURCE_SCPI);
  }
  nv_save(source);
}

/* :STATus:QUEStionable:USER's hook: its condition from nv_settings
 * (0x43486). */
void questionable_user_done(void) {
  status_happening(nv_settings.questionable_user != 0
                       ? STATUS_USER_REPORTED_SET
                       : STATUS_USER_REPORTED_CLEARED);
}

/* Where a code reported before the model is ready would be kept, an
 * early_status_events buffer; does nothing (0x2f1b8). */
void status_event_hook(uint32_t buffer, uint8_t code) {}

/* Puts a code into the status system once it is ready, with its
 * message: SCPI's for 0 and a negative code, status_report_messages'
 * from 1 to 0x16; 0 (0x2bc96). */
uint32_t status_report(int16_t code) {
  /* status_report_messages' entries. */
  static const int16_t REPORT_MESSAGES = 0x17;

  if (status_ready == 0) {
    status_event_hook((uint32_t)&early_status_reports, code);
    return 0;
  }
  char* message;

  if (code <= 0) {
    message = error_message_lookup(code);
  } else if (code < REPORT_MESSAGES) {
    message = status_report_messages[code];
  } else {
    message = 0;
  }
  /* The message is the error queue entry's value. */
  status_set_bit(scpi_status_model, code, (int32_t)message);
  return 0;
}

/* Raises a status condition once the status system is ready; entering
 * one of the lock and holdover states clears the other three, and one
 * of the empty conditions 0x36 and 0x37 besides (0x2f1fe).
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

/* Replays a buffer's happenings from first to last (0x2f370).  A
 * buffer of reports replays nothing: the 58503A's drops the Z3805A's
 * call of status_report (0x2f3a2). */
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

/* A register's condition word; nothing calls it (0x2f1ba). */
uint16_t status_condition_word(uint8_t index) {
  return status_ready_condition(index);
}

/* :STATus:OPERation:HARDware's condition word, 0 before the status
 * system is ready (0x2f30a). */
uint16_t status_hardware_condition(void) {
  /* The register's index (structures.md, "The status system"). */
  static const uint8_t HARDWARE_REGISTER = 6;

  return status_ready_condition(HARDWARE_REGISTER);
}

/* *CLS's clearing of status_object's events: the front panel's Shift
 * then Alt (0x2f1ee). */
void status_events_clear(void) { status_clear_events(status_object); }

/* The status model a port's requests report into: scpi_status_model for
 * ports 0 and 2 (0x2eff0).  For another the image returns what its
 * switch leaves in D0: for port 1 the port less 2, -1, with its low word
 * replaced by the jump table's entry for it, 0x000c (0x2effc), and past
 * 2 the port less 2.  A negative port jumps through the bytes before
 * the table, which the C does not follow. */
status_model* port_status_model(char port) {
  /* What port 1 gets: -1's high word and the table's entry, 0x000c. */
  static const uint32_t PORT_1_MODEL = 0xffff000c;

  if (port == 0 || port == STATUS_CONSOLE_PORT) {
    return scpi_status_model;
  }
  if (port == 1) {
    return (status_model*)PORT_1_MODEL;
  }
  return (status_model*)(int32_t)(port - STATUS_CONSOLE_PORT);
}

/* A register's event word, as status_read_event reads it, without
 * clearing it: the top register's the port's status byte (0x2775a). */
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
 * setting alone, else "NAME ?" (0x29640 to 0x296fa). */
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
 * if any was taken (0x295b0).  A child's walk replaces the result, so
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
 * most severity 4: the front panel's mode 9 and :ALARm?; true if any
 * (0x29740).  The image copies the text over itself at the end. */
uint8_t alarm_text(char* text) {
  /* The least severe an entry may be and still be taken. */
  static const uint8_t ALARM_SEVERITY_LEAST = 4;
  alarm_text_cursor cursor = {.text = text, .severity = ALARM_SEVERITY_LEAST};

  libc_copy(text, "");
  const uint8_t taken = alarm_text_collect(&alarm_text_root, &cursor);

  libc_copy(text, cursor.text);
  return taken;
}
