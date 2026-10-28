/* status.c's functions that are this image's own: the rest are
 * common/status.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The first register with transition filters, :STATus:OPERation; the
 * filters' arrays in nv_settings start with it. */
static const uint8_t STATUS_FIRST_FILTERED = 2;

/* Builds the status model, sets its registers up from status_register_layouts
 * and the saved enables, and marks the status system ready;
 * status_create's error (0x2f480).  The Standard Event register's
 * filter pointers are NULL, so its filters are ROM's first long's low
 * word, the reset stack pointer's, as the image reads them. */
uint32_t status_init(void) {
  status_request_service = 0;
  uint32_t error = status_create(&status_object, STATUS_REGISTERS,
                                 STATUS_ERROR_QUEUE_ROOM, status_conditions, 0);

  if (error != 0) {
    return error;
  }
  status_register_setup((int32_t)status_object, 0, 0, 0, 0, 0,
                        (int32_t)scpi_console_port());
  for (uint8_t index = 1; index < STATUS_REGISTERS; index++) {
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

/* Sets the filtered registers' transition filters from nv_settings;
 * called by nothing (0x2f40c). */
void status_transitions_init(void) {
  for (uint8_t index = STATUS_FIRST_FILTERED; index < STATUS_REGISTERS;
       index++) {
    status_register_set_ptr(status_object, index,
                            *status_register_layouts[index].positive);
    status_register_set_ntr(status_object, index,
                            *status_register_layouts[index].negative);
  }
}

/* Sets a register's enable or transition filter for a request from
 * SCPI, and 1; 0 for another source (0x2f34e). */
uint32_t status_preset_register(status_register_mask mask,
                                request_source source, int16_t index,
                                int16_t value) {
  if (source != REQUEST_SOURCE_SCPI) {
    return 0;
  }
  switch (mask) {
    case STATUS_MASK_ENABLE:
      status_register_set_enable(status_object, index, value);
      break;
    case STATUS_MASK_POSITIVE_TRANSITION:
      status_register_set_ptr(status_object, index + STATUS_FIRST_FILTERED,
                              value);
      break;
    case STATUS_MASK_NEGATIVE_TRANSITION:
      status_register_set_ntr(status_object, index + STATUS_FIRST_FILTERED,
                              value);
      break;
  }
  return 1;
}

/* Sets a register's enable from nv_settings, reporting -300 to a source
 * that may not (0x43482). */
void status_enable_restore(int32_t index, int32_t source) {
  int8_t reg = index;

  if (status_preset_register(STATUS_MASK_ENABLE, source, reg,
                             nv_settings.status_enable[reg]) == 0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* Sets a filtered register's positive transition filter from
 * nv_settings, by its index less 2 (0x434c2). */
void status_ptr_restore(int32_t index, int32_t source) {
  int8_t filter = index;

  if (status_preset_register(STATUS_MASK_POSITIVE_TRANSITION, source, filter,
                             nv_settings.status_positive_transition[filter]) ==
      0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* The same for its negative transition filter (0x43504). */
void status_ntr_restore(int32_t index, int32_t source) {
  int8_t filter = index;

  if (status_preset_register(STATUS_MASK_NEGATIVE_TRANSITION, source, filter,
                             nv_settings.status_negative_transition[filter]) ==
      0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* :STATus:PRESet's hook: the enables its form gives and the filtered
 * registers' filters, set and saved for the source (0x43546). */
void status_preset_done(int32_t form, int32_t source) {
  /* The enables each form gives, by register (0x2f76e, 0x2f77c). */
  static const uint16_t SCPI_ENABLES[] = {0, 0, 0, 0, 7, 0xf, 0x1fff};
  static const uint16_t ALARM_ENABLES[] = {0x88, 0, 0x24, 3, 7, 8, 0x1fff};
  /* The filters, by filtered register (0x2f78a, 0x2f794). */
  static const uint16_t POSITIVE_FILTERS[] = {0x7f, 2, 7, 0xf, 0x13ff};
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
 * (0x43466). */
void questionable_user_done(void) {
  status_happening(nv_settings.questionable_user != 0
                       ? STATUS_USER_REPORTED_SET
                       : STATUS_USER_REPORTED_CLEARED);
}

/* Puts a code into the status system once it is ready, with its
 * message: SCPI's for 0 and a negative code, status_report_messages'
 * from 1 to 0x16; 0 (0x2be9c). */
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
  status_set_bit(status_object, code, (int32_t)message);
  return 0;
}

/* Raises a status condition once the status system is ready; entering
 * one of the lock and holdover states clears the other three
 * (0x2f53c).  STATUS_INPUT_OVERRUN, which :DIAG:STAT:HAPP 0 raises,
 * does nothing. */
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
      break;
    case STATUS_HOLDING_SET:
      status_clear_bit(status_object, STATUS_LOCKED_CLEARED);
      status_clear_bit(status_object, STATUS_WAITING_CLEARED);
      status_clear_bit(status_object, STATUS_RECOVERING_CLEARED);
      break;
    case STATUS_WAITING_SET:
      status_clear_bit(status_object, STATUS_LOCKED_CLEARED);
      status_clear_bit(status_object, STATUS_HOLDING_CLEARED);
      status_clear_bit(status_object, STATUS_RECOVERING_CLEARED);
      break;
    case STATUS_RECOVERING_SET:
      status_clear_bit(status_object, STATUS_LOCKED_CLEARED);
      status_clear_bit(status_object, STATUS_HOLDING_CLEARED);
      status_clear_bit(status_object, STATUS_WAITING_CLEARED);
      break;
    default:
      break;
  }
}

/* Replays a buffer's codes from first to last (0x2f680). */
void happening_replay(char* events, status_replay_mode mode, char first,
                      char last) {
  for (int8_t index = first; index <= last; index++) {
    switch (mode) {
      case STATUS_REPLAY_REPORTS:
        status_report(events[index]);
        break;
      case STATUS_REPLAY_HAPPENINGS:
        status_happening(events[index]);
        break;
    }
  }
}

/* :STATus:OPERation:HARDware's condition word (0x2f626). */
uint16_t status_hardware_condition(void) {
  /* The register's index (structures.md, "The status system"). */
  static const int32_t HARDWARE_REGISTER = 6;
  uint16_t condition;

  status_read_condition(status_object, HARDWARE_REGISTER, &condition);
  return condition;
}

/* status_summary from the hardware condition's groups and the holdover
 * threshold, and the Alarm lamp from it (outputs.md, "The Z3805A's
 * lamps"; 0x46470). */
void status_summary_update(void) {
  /* The hardware condition's groups, by summary bit. */
  static const uint16_t HARDWARE_SELFTEST = 0x0001;
  static const uint16_t HARDWARE_SUPPLIES = 0x000e;
  static const uint16_t HARDWARE_EFC_AT_LIMIT = 0x0080;
  static const uint16_t HARDWARE_GPS = 0x0300;
  static const uint16_t HARDWARE_INTERNAL_REFERENCE = 0x1000;
  static const uint8_t SUMMARY_SELFTEST = 0x01;
  static const uint8_t SUMMARY_SUPPLIES = 0x02;
  static const uint8_t SUMMARY_EFC_AT_LIMIT = 0x04;
  static const uint8_t SUMMARY_GPS = 0x08;
  static const uint8_t SUMMARY_INTERNAL_REFERENCE = 0x10;
  static const uint8_t SUMMARY_HOLDOVER_EXCEEDED = 0x20;
  uint16_t hardware = status_hardware_condition();

  status_summary = (hardware & HARDWARE_SELFTEST) != 0 ? SUMMARY_SELFTEST : 0;
  if ((hardware & HARDWARE_SUPPLIES) != 0) {
    status_summary |= SUMMARY_SUPPLIES;
  }
  if ((hardware & HARDWARE_EFC_AT_LIMIT) != 0) {
    status_summary |= SUMMARY_EFC_AT_LIMIT;
  }
  if ((hardware & HARDWARE_GPS) != 0) {
    status_summary |= SUMMARY_GPS;
  }
  if ((hardware & HARDWARE_INTERNAL_REFERENCE) != 0) {
    status_summary |= SUMMARY_INTERNAL_REFERENCE;
  }
  if (holdover_duration_exceeded != 0) {
    status_summary |= SUMMARY_HOLDOVER_EXCEEDED;
  }
  led_alarm = status_summary != 0;
}
