/* What the status system shares with the installer's copy of it
 * (status.c, installer_status.c): the error classes, and the bodies of
 * the routines that read and change a status model's registers, which
 * both copies call.  The exported C includes the hand-written headers
 * after its own declarations and before the hand-written .c files, so
 * every one of them can use it.  docs/firmware/structures.md, "The
 * status system", has the records. */

/* An error code's class, by the greatest code of each, which
 * error_queue_push reports through the negative conditions -1 to -4
 * (0x2863c to 0x28674); a positive code is a device's.  The command
 * errors' bounds are here, the other classes' in
 * status_model_error_class. */
static const int16_t STATUS_COMMAND_ERROR_GREATEST = -100;

/* The error the error queue's last free entry takes, "Queue overflow" in
 * scpi_error_messages (error_queue_push, 0x28610). */
static const int16_t STATUS_ERROR_QUEUE_OVERFLOW = -350;

/* The error queue's room, which status_init gives status_create
 * (0x2f480). */
static const int32_t STATUS_ERROR_QUEUE_ROOM = 0x1e;

/* Each sets or clears its parent's summary bit through the other. */
static void status_model_propagate(status_model* status, status_register* reg);

/* status_condition_set's and its installer copy's body: sets condition bits;
 * those rising through the positive transition filter become events.  The top
 * register's go to the port's status byte. */
static void status_model_condition_set(status_model* status,
                                       status_register* reg, int32_t bits) {
  uint16_t set = bits;

  if (reg->parent_bit == 0) {
    status->output->methods->status_set_bits(status->output, (int16_t)set);
    return;
  }
  uint16_t rising = set & reg->positive_transition & ~reg->condition;
  if (rising != 0) {
    reg->event |= rising;
    status_model_propagate(status, reg);
  }
  reg->condition |= set;
}

/* status_condition_clear's and its installer copy's body: clears condition
 * bits; those falling through the negative transition filter become events. */
static void status_model_condition_clear(status_model* status,
                                         status_register* reg, int32_t bits) {
  uint16_t cleared = bits;

  if (reg->parent_bit == 0) {
    status->output->methods->status_clear_bits(status->output,
                                               (int16_t)cleared);
    return;
  }
  uint16_t falling = cleared & reg->negative_transition & reg->condition;
  if (falling != 0) {
    reg->event |= falling;
    status_model_propagate(status, reg);
  }
  reg->condition &= ~cleared;
}

/* status_propagate's and its installer copy's body: sets or clears a register's
 * summary bit in its parent while its event and enable words share a bit.
 * parent_bit is that bit's mask. */
static void status_model_propagate(status_model* status, status_register* reg) {
  if ((reg->event & reg->enable) == 0) {
    status_model_condition_clear(status, reg->parent, reg->parent_bit);
  } else {
    status_model_condition_set(status, reg->parent, reg->parent_bit);
  }
}

/* status_event_set's and its installer copy's body: ORs bits into a register's
 * event word and propagates. */
static void status_model_event_set(status_model* status, status_register* reg,
                                   uint32_t bits) {
  reg->event |= bits;
  status_model_propagate(status, reg);
}

/* status_read_event's and its installer copy's body: a register's event word,
 * cleared as it is read, as :EVENt?, *ESR? and *STB? read it; the top
 * register's is the port's status byte
 *. */
static void status_model_read_event(status_model* status, int32_t index,
                                    uint16_t* event) {
  status_register* reg = status->registers[index];

  if (reg->parent_bit == 0) {
    *event = status->output->methods->status_byte_get(status->output);
    return;
  }
  *event = reg->event;
  reg->event = 0;
  status_model_propagate(status, reg);
}

/* status_clear_events' and its installer copy's body: reads every
 * register's event word but the top one's, clearing them, for *CLS; 0. */
static uint32_t status_model_clear_events(status_model* status) {
  uint16_t discarded;

  for (int16_t index = status->register_count - 1; index > 0; index--) {
    status_model_read_event(status, index, &discarded);
  }
  return 0;
}

/* status_condition_bit_get's and its installer copy's body: a
 * condition's bit in its register's condition word, inverted for one
 * that clears it.  The word is sign-extended to the long the bit is
 * tested in. */
static void status_model_condition_bit_get(status_model* status,
                                           int32_t condition, uint32_t* bit) {
  status_condition* entry = &status->conditions[condition];
  int32_t word = (int16_t)status->registers[entry->register_index]->condition;

  *bit = (cpu_long_bit(entry->bit) & word) != 0;
  if (entry->kind == STATUS_KIND_CLEAR) {
    *bit = *bit == 0;
  }
}

/* status_register_set_enable's and its installer copy's body: sets a
 * register's enable word, as :ENABle and *ESE do; the top register's is
 * the port's service request enable, without RQS. */
static void status_model_register_set_enable(status_model* status,
                                             int32_t index, int32_t enable) {
  static const uint8_t STATUS_BYTE_BITS = 0xff;
  status_register* reg = status->registers[index];

  if (reg->parent_bit == 0) {
    status->output->methods->sre_set(
        status->output,
        (uint16_t)enable & (STATUS_BYTE_BITS & ~STATUS_BYTE_REQUEST_SERVICE));
    return;
  }
  reg->enable = enable;
  status_model_propagate(status, reg);
}

/* status_register_get_enable's and its installer copy's body: a
 * register's enable word, as :ENABle? and *ESE? read it. */
static void status_model_register_get_enable(status_model* status,
                                             int32_t index, uint16_t* enable) {
  if (status->registers[index]->parent_bit == 0) {
    *enable = status->output->methods->sre_get(status->output);
  } else {
    *enable = status->registers[index]->enable;
  }
}

/* status_read_condition's and its installer copy's body: a register's
 * condition word, as :CONDition? reads it. */
static void status_model_read_condition(status_model* status, int32_t index,
                                        uint16_t* condition) {
  *condition = status->registers[index]->condition;
}

/* The negative condition for an error code's class; 0 for none, a
 * code from -1 to -99 (0x2863c to 0x28670). */
static int32_t status_model_error_class(int16_t code) {
  /* The greatest code of each other class (0x2863c to 0x28674). */
  static const int16_t ERROR_QUERY_GREATEST = -400;
  static const int16_t ERROR_DEVICE_GREATEST = -300;
  static const int16_t ERROR_EXECUTION_GREATEST = -200;

  if (code <= ERROR_QUERY_GREATEST) {
    return STATUS_ESR_QUERY_ERROR;
  }
  if (code <= ERROR_DEVICE_GREATEST || code > 0) {
    return STATUS_ESR_DEVICE_ERROR;
  }
  if (code <= ERROR_EXECUTION_GREATEST) {
    return STATUS_ESR_EXECUTION_ERROR;
  }
  if (code <= STATUS_COMMAND_ERROR_GREATEST) {
    return STATUS_ESR_COMMAND_ERROR;
  }
  return 0;
}

/* error_message_lookup's and its installer copy's body: an error code's
 * message, from scpi_error_messages, NULL for none (the installer's at
 * installer::0x10656a holds the same). */
static char* status_model_error_message(int32_t code) {
  /* The code that ends the table. */
  enum { END = -1 };
  /* The image's table at 0x28910, scpi_error_messages. */
  static const scpi_error_message MESSAGES[] = {
      {0, "No error"},
      {-100, "Command error"},
      {-101, "Invalid character"},
      {-102, "Syntax error"},
      {-103, "Invalid separator"},
      {-104, "Data type error"},
      {-105, "GET not allowed"},
      {-108, "Parameter not allowed"},
      {-109, "Missing parameter"},
      {-110, "Command header error"},
      {-111, "Header separator error"},
      {-112, "Program mnemonic too long"},
      {-113, "Undefined header"},
      {-114, "Header suffix out of range"},
      {-120, "Numeric data error"},
      {-121, "Invalid character in number"},
      {-123, "Exponent too large"},
      {-124, "Too many digits"},
      {-128, "Numeric data not allowed"},
      {-130, "Suffix error"},
      {-131, "Invalid suffix"},
      {-134, "Suffix too long"},
      {-138, "Suffix not allowed"},
      {-140, "Character data error"},
      {-141, "Invalid character data"},
      {-144, "Character data too long"},
      {-148, "Character data not allowed"},
      {-150, "String data error"},
      {-151, "Invalid string data"},
      {-158, "String data not allowed"},
      {-160, "Block data error"},
      {-161, "Invalid block data"},
      {-168, "Block data not allowed"},
      {-170, "Expression error"},
      {-171, "Invalid expression"},
      {-178, "Expression data not allowed"},
      {-180, "Macro error"},
      {-181, "Invalid outside macro definition"},
      {-183, "Invalid inside macro definition"},
      {-184, "Macro parameter error"},
      {-200, "Execution error"},
      {-201, "Invalid while in local"},
      {-202, "Settings lost due to rtl"},
      {-210, "Trigger error"},
      {-211, "Trigger ignored"},
      {-212, "Arm ignored"},
      {-213, "Init ignored"},
      {-214, "Trigger deadlock"},
      {-215, "Arm deadlock"},
      {-220, "Parameter error"},
      {-221, "Settings conflict"},
      {-222, "Data out of range"},
      {-223, "Too much data"},
      {-224, "Illegal parameter value"},
      {-230, "Data corrupt or stale"},
      {-231, "Data questionable"},
      {-240, "Hardware error"},
      {-241, "Hardware missing"},
      {-250, "Mass storage error"},
      {-251, "Missing mass storage"},
      {-252, "Missing media"},
      {-253, "Corrupt media"},
      {-254, "Media full"},
      {-255, "Directory full"},
      {-256, "File name not found"},
      {-257, "File name error"},
      {-258, "Media protected"},
      {-260, "Expression error"},
      {-261, "Math error in expression"},
      {-270, "Macro error"},
      {-271, "Macro syntax error"},
      {-272, "Macro execution error"},
      {-273, "Illegal macro label"},
      {-274, "Macro parameter error"},
      {-275, "Macro definition too long"},
      {-276, "Macro recursion error"},
      {-277, "Macro redefinition not allowed"},
      {-278, "Macro header not found"},
      {-280, "Program error"},
      {-281, "Cannot create program"},
      {-282, "Illegal program name"},
      {-283, "Illegal variable name"},
      {-284, "Program currently running"},
      {-285, "Program syntax error"},
      {-286, "Program runtime error"},
      {-300, "Device-specific error"},
      {-310, "System error"},
      {-311, "Memory error"},
      {-312, "PUD memory error"},
      {-313, "Calibration memory lost"},
      {-314, "Save/recall memory lost"},
      {-315, "Configuration memory lost"},
      {-321, "Out of memory"},
      {-330, "Self-test failed"},
      {-350, "Queue overflow"},
      {-400, "Query error"},
      {-410, "Query INTERRUPTED"},
      {-420, "Query UNTERMINATED"},
      {-430, "Query DEADLOCKED"},
      {-440, "Query UNTERMINATED after indefinite response"},
      {END, 0},
  };

  for (const scpi_error_message* entry = MESSAGES; entry->code != END;
       entry++) {
    if (entry->code == (int16_t)code) {
      return entry->text;
    }
  }
  return 0;
}

/* status_register_setup's and its installer copy's body: sets a
 * register up, its words cleared, its filters and its parent.  The
 * Status Byte, with no parent, is the port's: its service request
 * enable and every bit but RQS cleared, and the port kept as the
 * model's output. */
static void status_model_register_setup(status_model* status, int32_t index,
                                        uint32_t positive, uint32_t negative,
                                        int32_t parent_index,
                                        uint32_t parent_bit,
                                        scpi_port* output) {
  status_register* reg = status->registers[index];

  reg->condition = 0;
  reg->event = 0;
  reg->enable = 0;
  reg->positive_transition = positive;
  reg->negative_transition = negative;
  reg->parent_bit = parent_bit;
  if (reg->parent_bit == 0) {
    output->methods->sre_set(output, 0);
    output->methods->status_clear_bits(output,
                                       (uint8_t)~STATUS_BYTE_REQUEST_SERVICE);
    status->output = output;
  } else {
    reg->parent = status->registers[parent_index];
  }
}

/* condition_is_command_error's and its installer copy's body: 1 for a
 * negative condition that queues a command error, -100 to -199, by its
 * image's negative conditions. */
static uint32_t status_model_command_error(const status_condition* negatives,
                                           int32_t condition) {
  static const int16_t ERROR_COMMAND_LEAST = -199;

  if (condition >= 0) {
    return 0;
  }
  const status_condition* entry = &negatives[-condition];

  return entry->kind == STATUS_KIND_ERROR &&
         entry->register_index >= ERROR_COMMAND_LEAST &&
         entry->register_index <= STATUS_COMMAND_ERROR_GREATEST;
}
