/* status.c's functions that are this image's own: the rest are
 * common/status.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the 58503A 3704's rewritten/status.c, of its functions,
 * those the Z3816A's code carries (identical or relocated by image_diff, or
 * changed with the same instructions), with the static helpers they use, each
 * the same here but where its comment says otherwise
 * (docs/firmware/z3816a-4001.md, "The link check").  Its comments' addresses
 * are the Z3816A's.  Each keeps its generated prototype. */

/* The first register with transition filters, :STATus:OPERation; the
 * filters' arrays in nv_settings start with it. */
static const uint8_t STATUS_FIRST_FILTERED = 2;

/* Sets a register's enable from nv_settings, reporting -300 to a source
 * that may not (0x46dca). */
void status_enable_restore(int32_t index, int32_t source) {
  int8_t reg = index;

  if (status_preset_register(STATUS_MASK_ENABLE, source, reg,
                             nv_settings.status_enable[reg]) == 0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* Sets a filtered register's positive transition filter from
 * nv_settings, by its index less 2 (0x46e0a). */
void status_ptr_restore(int32_t index, int32_t source) {
  int8_t filter = index;

  if (status_preset_register(STATUS_MASK_POSITIVE_TRANSITION, source, filter,
                             nv_settings.status_positive_transition[filter]) ==
      0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* The same for its negative transition filter (0x46e4c). */
void status_ntr_restore(int32_t index, int32_t source) {
  int8_t filter = index;

  if (status_preset_register(STATUS_MASK_NEGATIVE_TRANSITION, source, filter,
                             nv_settings.status_negative_transition[filter]) ==
      0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* :STATus:QUEStionable:USER's hook: its condition from nv_settings
 * (0x46dae). */
void questionable_user_done(void) {
  status_happening(nv_settings.questionable_user != 0
                       ? STATUS_USER_REPORTED_SET
                       : STATUS_USER_REPORTED_CLEARED);
}

/* Raises a status condition once the status system is ready; entering
 * one of the lock and holdover states clears the other three, and one
 * of the empty conditions 0x36 and 0x37 besides (0x3ec76).
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

/* Replays a buffer's happenings from first to last (0x3ede8).  A
 * buffer of reports replays nothing: the 58503A's drops the Z3805A's
 * call of status_report (0x3ee1a). */
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

/* A register's condition word; nothing calls it (0x3ec42). */
uint16_t status_condition_word(uint8_t index) {
  return status_ready_condition(index);
}

/* :STATus:OPERation:HARDware's condition word, 0 before the status
 * system is ready (0x3ed82). */
uint16_t status_hardware_condition(void) {
  /* The register's index (structures.md, "The status system"). */
  static const uint8_t HARDWARE_REGISTER = 6;

  return status_ready_condition(HARDWARE_REGISTER);
}

/* A register's event word, as status_read_event reads it, without
 * clearing it: the top register's the port's status byte (0x379a4). */
void status_peek_event(status_model* status, int32_t index, uint16_t* event) {
  const status_register* reg = status->registers[index];

  if (reg->parent_bit == 0) {
    *event = status->output->methods->status_byte_get(status->output);
    return;
  }
  *event = reg->event;
}

/* Sets a register's enable or transition filter in the status model of
 * the request's port, and 1; 0 for a port with none (0x3ea9c).  The
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

/* Puts a code into the status system once it is ready, with its
 * message: SCPI's for 0 and a negative code, status_report_messages'
 * from 1 to 0x16; 0 (0x3b788).  The 58503A 3704's but with no early
 * buffer before it is ready, as the Z3815A 4010's sci_scpi_status_report. */
uint32_t status_report(int16_t code) {
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

/* status_report for a source that reports, the SCI's or startup; the
 * others do not (0x3b7d8, its jump table at 0x3b7f0).  The 58503A
 * 3704's reports for SCPI's source, 0, where this image's SCPI is the
 * SCI's, 1. */
void status_report_from(int16_t code, request_source source) {
  if (source == REQUEST_SOURCE_SCI || source == REQUEST_SOURCE_STARTUP) {
    status_report(code);
  }
}

/* The Standard Event register's positive transition filter, its defined
 * bits, 2 to 5 and 7 (0x3ebd4), as the 58503A 3704's. */
static const uint32_t STATUS_STANDARD_EVENT_POSITIVE = 0xbc;

/* The port port_status_model takes besides the SCI's, the 58503A 3704's
 * console port (0x3ea94). */
static const char STATUS_CONSOLE_PORT = 2;

/* The status model a port's requests report into: scpi_status_model for
 * ports 1, the SCI's, and 2 (0x3ea78).  For another the image returns
 * what its switch leaves in D0, the port less 2.  The 58503A 3704's
 * takes ports 0 and 2. */
status_model* port_status_model(char port) {
  if (port == SCPI_PORT_SCI || port == STATUS_CONSOLE_PORT) {
    return scpi_status_model;
  }
  return (status_model*)(int32_t)(port - STATUS_CONSOLE_PORT);
}

/* Builds the status model, keeps it as scpi_status_model too, sets its
 * registers up from status_register_layouts and the saved enables, and
 * marks the status system ready; status_create's error (0x3eb60).
 * scpi_status_model is set even when status_create fails.  The 58503A
 * 3704's but that the model's first register reports to the SCI's port
 * object (0x3eba6). */
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

/* status_summary from the hardware condition's groups, the holdover
 * threshold, the antenna's current and the hardware alarm, the hardware
 * alarm from the DUART's inputs (alarm_hardware_inputs), and the Alarm
 * lamp from the summary (0x49f76).  The Z3805A 3543B's but for the
 * power supplies' group, the Z3815A 4010's, and the antenna and the
 * hardware alarm.  The image keeps the condition in D3, which
 * alarm_hardware_inputs reads unset while the loop is stopped. */
void status_summary_update(void) {
  /* The hardware condition's groups, by summary bit. */
  static const uint16_t HARDWARE_SELFTEST = 0x0001;
  static const uint16_t HARDWARE_SUPPLIES = 0x602e;
  static const uint16_t HARDWARE_EFC_AT_LIMIT = 0x0080;
  static const uint16_t HARDWARE_GPS = 0x0300;
  static const uint16_t HARDWARE_INTERNAL_REFERENCE = 0x1000;
  static const uint8_t SUMMARY_SELFTEST = 0x01;
  static const uint8_t SUMMARY_SUPPLIES = 0x02;
  static const uint8_t SUMMARY_EFC_AT_LIMIT = 0x04;
  static const uint8_t SUMMARY_GPS = 0x08;
  static const uint8_t SUMMARY_INTERNAL_REFERENCE = 0x10;
  static const uint8_t SUMMARY_HOLDOVER_EXCEEDED = 0x20;
  static const uint8_t SUMMARY_ANTENNA = 0x40;
  static const uint8_t SUMMARY_HARDWARE_ALARM = 0x80;
  /* alarm_hardware's bit for an antenna current above its range. */
  static const uint8_t ALARM_HARDWARE_ANTENNA_HIGH = 0x10;
  register uint16_t hardware __asm__("d3") = status_hardware_condition();
  uint8_t summary = (hardware & HARDWARE_SELFTEST) != 0 ? SUMMARY_SELFTEST : 0;

  if ((hardware & HARDWARE_SUPPLIES) != 0) {
    summary |= SUMMARY_SUPPLIES;
  }
  if ((hardware & HARDWARE_EFC_AT_LIMIT) != 0) {
    summary |= SUMMARY_EFC_AT_LIMIT;
  }
  if ((hardware & HARDWARE_GPS) != 0) {
    summary |= SUMMARY_GPS;
  }
  if ((hardware & HARDWARE_INTERNAL_REFERENCE) != 0) {
    summary |= SUMMARY_INTERNAL_REFERENCE;
  }
  if (holdover_duration_exceeded != 0) {
    summary |= SUMMARY_HOLDOVER_EXCEEDED;
  }
  int8_t antenna = hmon_check_antenna_current();

  if (antenna != 0) {
    summary |= SUMMARY_ANTENNA;
  }
  uint8_t alarm = alarm_hardware_inputs();

  if (alarm != 0) {
    summary |= SUMMARY_HARDWARE_ALARM;
  }
  alarm_hardware = antenna > 0 ? alarm | ALARM_HARDWARE_ANTENNA_HIGH : alarm;
  status_summary = summary;
  led_alarm = summary != 0;
}

/* :STATus:PRESet's hook: the enables its form gives and the filtered
 * registers' filters, set and saved for the source (0x46e8e).  The
 * 58503A 3704's but that the Hardware register's enables and positive
 * filter have bits 13 and 14 too, and the registers are set as from the
 * start-up's source, 2, where the 58503A's sets them as from SCPI's. */
void status_preset_done(int32_t form, int32_t source) {
  /* The enables each form gives, by register (0x3eec4, 0x3eed2). */
  static const uint16_t SCPI_ENABLES[] = {0, 0, 0, 0, 7, 0xf, 0x7fdf};
  static const uint16_t ALARM_ENABLES[] = {0x88, 0, 0x24, 3, 7, 8, 0x7fdf};
  /* The filters, by filtered register (0x3eee0, 0x3eeea). */
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
