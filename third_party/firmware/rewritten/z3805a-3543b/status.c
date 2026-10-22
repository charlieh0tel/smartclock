/* The SCPI status system -- the status model's registers and
 * conditions, the event and enable words, and the error queue -- written by
 * hand in place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/structures.md, "The status system").  The exported C includes
 * this file after its own, so the types and prototypes it declares are in
 * scope; each function here keeps its generated prototype. */

/* An error code's class, by the greatest code of each, which
 * error_queue_push reports through the negative conditions -1 to -4
 * (0x2863c to 0x28674); a positive code is a device's.  The command
 * errors' bounds are here, the other classes' in
 * status_error_class. */
static const int16_t STATUS_COMMAND_ERROR_GREATEST = -100;

/* A status condition's kind, status_condition.kind, by the jump table
 * at 0x2825c. */
enum {
  STATUS_KIND_EVENT_SET = 0, /* sets the bit in the event word */
  STATUS_KIND_SET = 1,       /* sets the bit in the condition word */
  STATUS_KIND_CLEAR = 2,     /* clears it */
  STATUS_KIND_ERROR = 3,     /* queues register_index as an error code */
  STATUS_KIND_NOTHING = 4,
};

/* A bit, 1 << bit, as lsl.l takes its count: modulo 64, and 0 from 32
 * (MC68331UM, LSL). */
static uint32_t status_bit(int16_t bit) {
  uint32_t count = bit & 0x3f;

  return count >= 32 ? 0 : (uint32_t)1 << count;
}

/* ORs bits into a register's event word and propagates (0x27fca). */
void status_event_set(status_model* status, status_register* reg,
                      uint32_t bits) {
  reg->event |= bits;
  status_propagate(status, reg);
}

/* Sets condition bits; those rising through the positive transition
 * filter become events.  The top register's go to the port's status
 * byte (0x27fe8). */
void status_condition_set(status_model* status, status_register* reg,
                          int32_t bits) {
  uint16_t set = bits;

  if (reg->parent_bit == 0) {
    status->output->methods->status_set_bits(status->output, (int16_t)set);
    return;
  }
  uint16_t rising = set & reg->positive_transition & ~reg->condition;
  if (rising != 0) {
    reg->event |= rising;
    status_propagate(status, reg);
  }
  reg->condition |= set;
}

/* Clears condition bits; those falling through the negative transition
 * filter become events (0x28044). */
void status_condition_clear(status_model* status, status_register* reg,
                            int32_t bits) {
  uint16_t cleared = bits;

  if (reg->parent_bit == 0) {
    status->output->methods->status_clear_bits(status->output,
                                               (int16_t)cleared);
    return;
  }
  uint16_t falling = cleared & reg->negative_transition & reg->condition;
  if (falling != 0) {
    reg->event |= falling;
    status_propagate(status, reg);
  }
  reg->condition &= ~cleared;
}

/* Sets or clears a register's summary bit in its parent while its event
 * and enable words share a bit (0x280a4).  parent_bit is that bit's
 * mask. */
void status_propagate(status_model* status, status_register* reg) {
  if ((reg->event & reg->enable) == 0) {
    status_condition_clear(status, reg->parent, reg->parent_bit);
  } else {
    status_condition_set(status, reg->parent, reg->parent_bit);
  }
}

/* Allocates the status model, its registers and its error queue, the
 * model into its slot (0x280ea); -56 for no memory. */
uint32_t status_create(status_model** slot, int32_t register_count,
                       int32_t queue_capacity, status_condition* conditions,
                       uint32_t reserved) {
  /* Room for one register pointer more than it keeps. */
  status_model* status = memory_allocate(
      register_count * sizeof(status_register*) +
      __builtin_offsetof(status_model, registers) + sizeof(status_register*));

  *slot = status;
  if (status == 0) {
    return STATUS_SCPI_NO_MEMORY;
  }
  status->register_count = register_count;
  status->field_0e = reserved;
  status->conditions = conditions;
  for (int16_t index = 0; index < register_count; index++) {
    status_register* reg = memory_allocate(sizeof(status_register));

    (*slot)->registers[index] = reg;
    if (reg == 0) {
      return STATUS_SCPI_NO_MEMORY;
    }
  }
  return error_queue_create(&(*slot)->queue, queue_capacity, *slot);
}

/* Frees the status model's queue and registers; called by nothing
 * (0x28160). */
void status_destroy(status_model* status) {
  error_queue_destroy(status->queue);
  for (int32_t index = 0; index < status->register_count; index++) {
    memory_free(status->registers[index]);
  }
  memory_free(status);
}

/* The entry of a condition: a negative one in status_negative_conditions,
 * the rest in the model's table (0x2821c to 0x2823e). */
static status_condition* status_condition_entry(status_model* status,
                                                int32_t condition) {
  if (condition < 0) {
    return &status_negative_conditions[-condition];
  }
  return &status->conditions[condition];
}

/* Applies a condition by its kind; 1, or error_queue_push's result for
 * an error (0x2820a). */
int16_t status_set_bit(status_model* status, int32_t condition, int32_t value) {
  status_condition* entry = status_condition_entry(status, condition);
  status_register* reg;

  switch (entry->kind) {
    case STATUS_KIND_EVENT_SET:
      reg = status->registers[entry->register_index];
      status_event_set(status, reg, status_bit(entry->bit));
      break;
    case STATUS_KIND_SET:
      reg = status->registers[entry->register_index];
      status_condition_set(status, reg, status_bit(entry->bit));
      break;
    case STATUS_KIND_CLEAR:
      reg = status->registers[entry->register_index];
      status_condition_clear(status, reg, status_bit(entry->bit));
      break;
    case STATUS_KIND_ERROR:
      return error_queue_push(status->queue, entry->register_index, value);
  }
  return 1;
}

/* The same, its result dropped (0x282ce). */
void status_apply_condition(status_model* status, int32_t condition,
                            int32_t value) {
  status_set_bit(status, condition, value);
}

/* status_set_bit with 0 (0x28388). */
int16_t status_clear_bit(status_model* status, int32_t condition) {
  return status_set_bit(status, condition, 0);
}

/* 1 for a negative condition that queues a command error, -100 to -199
 * (0x2839c). */
uint32_t condition_is_command_error(int32_t condition) {
  static const int16_t ERROR_COMMAND_LEAST = -199;

  if (condition >= 0) {
    return 0;
  }
  status_condition* entry = &status_negative_conditions[-condition];

  return entry->kind == STATUS_KIND_ERROR &&
         entry->register_index >= ERROR_COMMAND_LEAST &&
         entry->register_index <= STATUS_COMMAND_ERROR_GREATEST;
}

/* A condition's bit in its register's condition word, inverted for one
 * that clears it (0x283d2).  The word is sign-extended to the long the
 * bit is tested in. */
void status_condition_bit_get(status_model* status, int32_t condition,
                              uint32_t* bit) {
  status_condition* entry = &status->conditions[condition];
  int32_t word = (int16_t)status->registers[entry->register_index]->condition;

  *bit = (status_bit(entry->bit) & word) != 0;
  if (entry->kind == STATUS_KIND_CLEAR) {
    *bit = *bit == 0;
  }
}

/* Sets a register's enable word, as :ENABle and *ESE do; the top
 * register's is the port's service request enable, without RQS
 * (0x2842c). */
void status_register_set_enable(status_model* status, int32_t index,
                                int32_t enable) {
  static const uint8_t STATUS_BYTE_BITS = 0xff;
  status_register* reg = status->registers[index];

  if (reg->parent_bit == 0) {
    status->output->methods->sre_set(
        status->output,
        (uint16_t)enable & (STATUS_BYTE_BITS & ~STATUS_BYTE_REQUEST_SERVICE));
    return;
  }
  reg->enable = enable;
  status_propagate(status, reg);
}

/* A register's enable word, as :ENABle? and *ESE? read it (0x28474). */
void status_register_get_enable(status_model* status, int32_t index,
                                uint16_t* enable) {
  if (status->registers[index]->parent_bit == 0) {
    *enable = status->output->methods->sre_get(status->output);
  } else {
    *enable = status->registers[index]->enable;
  }
}

/* A register's event word, cleared as it is read, as :EVENt?, *ESR? and
 * *STB? read it; the top register's is the port's status byte
 * (0x284b6). */
void status_read_event(status_model* status, int32_t index, uint16_t* event) {
  status_register* reg = status->registers[index];

  if (reg->parent_bit == 0) {
    *event = status->output->methods->status_byte_get(status->output);
    return;
  }
  *event = reg->event;
  reg->event = 0;
  status_propagate(status, reg);
}

/* A register's condition word, as :CONDition? reads it (0x28506). */
void status_read_condition(status_model* status, int32_t index,
                           uint16_t* condition) {
  *condition = status->registers[index]->condition;
}

/* Reads every register's event word but the top one's, clearing them,
 * for *CLS (0x2851a). */
uint32_t status_clear_events(status_model* status) {
  uint16_t discarded;

  for (int16_t index = status->register_count - 1; index > 0; index--) {
    status_read_event(status, index, &discarded);
  }
  return 0;
}

/* Takes the oldest error from the queue, its value dropped, for *CLS
 * (0x2854a). */
void error_queue_clear(status_model* status, int32_t* code) {
  uint32_t discarded;

  error_queue_pop(status->queue, code, &discarded);
}

/* Takes the oldest error and its value from the queue (0x2856c). */
void error_queue_read(status_model* status, int32_t* code, uint32_t* message) {
  error_queue_pop(status->queue, code, message);
}

/* Allocates the error queue, with room for capacity entries, into its
 * slot, reporting into a status model; -56 for no memory (0x28588). */
uint32_t error_queue_create(status_error_queue** slot, int32_t capacity,
                            status_model* status) {
  /* A spare entry's room beyond the ring's. */
  status_error_queue* ring =
      memory_allocate(capacity * sizeof(status_error_entry) +
                      sizeof(status_error_queue) + sizeof(status_error_entry));

  *slot = ring;
  if (ring == 0) {
    return STATUS_SCPI_NO_MEMORY;
  }
  ring->status = status;
  ring->head = capacity;
  ring->tail = capacity;
  ring->size = capacity + 1;
  return 0;
}

/* The negative condition for an error code's class; 0 for none, a
 * code from -1 to -99 (0x2863c to 0x28670). */
static int32_t status_error_class(int16_t code) {
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

/* Adds an error to the queue and reports its class; its last free entry
 * takes -350, Queue overflow, in its place.  1 when the queue is full or
 * overflows, else 0 (0x285d4). */
int16_t error_queue_push(status_error_queue* queue, int32_t code,
                         uint32_t value) {
  /* The error the queue's last free entry takes, "Queue overflow" in
   * scpi_error_messages (0x28610). */
  static const int16_t ERROR_QUEUE_OVERFLOW = -350;
  status_model* status = queue->status;
  int16_t error = code;
  int16_t full = 0;
  int16_t room = queue->tail - queue->head;

  if (room <= 0) {
    room += queue->size;
  }
  if (room == 1) {
    full = 1;
  } else {
    if (room == 2) {
      error = ERROR_QUEUE_OVERFLOW;
      value = 0;
      full = 1;
    }
    queue->head++;
    if (queue->head == queue->size) {
      queue->head = 0;
    }
    queue->entries[queue->head].code = error;
    queue->entries[queue->head].value = value;
  }
  int32_t condition = status_error_class(error);

  if (condition != 0) {
    status_apply_condition(status, condition, 0);
  }
  status_prompt_refresh(status, error);
  return full;
}

/* Takes the oldest error and its value, 0 and 0 when the queue is
 * empty, and prompts again once it empties (0x2869e). */
void error_queue_pop(status_error_queue* queue, int32_t* code,
                     uint32_t* value) {
  if (queue->head == queue->tail) {
    *code = 0;
    *value = 0;
    return;
  }
  queue->tail++;
  if (queue->tail == queue->size) {
    queue->tail = 0;
  }
  *code = queue->entries[queue->tail].code;
  *value = queue->entries[queue->tail].value;
  if (queue->head == queue->tail) {
    status_prompt_refresh(queue->status, 0);
  }
}

/* Frees the error queue (0x28712). */
void error_queue_destroy(status_error_queue* queue) { memory_free(queue); }

/* Sets a register's positive transition filter, for :PTRansition
 * (0x2889a). */
void status_register_set_ptr(status_model* status, int32_t index,
                             uint32_t filter) {
  status->registers[index]->positive_transition = filter;
}

/* Sets a register's negative transition filter, for :NTRansition
 * (0x288ae). */
void status_register_set_ntr(status_model* status, int32_t index,
                             uint32_t filter) {
  status->registers[index]->negative_transition = filter;
}

/* A register's positive transition filter, for :PTRansition?
 * (0x288c2). */
void status_register_get_ptr(status_model* status, int32_t index,
                             uint16_t* filter) {
  *filter = status->registers[index]->positive_transition;
}

/* A register's negative transition filter, for :NTRansition?
 * (0x288d8). */
void status_register_get_ntr(status_model* status, int32_t index,
                             uint16_t* filter) {
  *filter = status->registers[index]->negative_transition;
}

/* An error code's message, from scpi_error_messages, NULL for none
 * (0x288ee). */
char* error_message_lookup(int32_t code) {
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

/* The registers status_create makes (structures.md, "The status
 * system"). */
static const uint8_t STATUS_REGISTERS = 7;

/* The first register with transition filters, :STATus:OPERation; the
 * filters' arrays in nv_settings start with it. */
static const uint8_t STATUS_FIRST_FILTERED = 2;

/* Sets a register up: its words cleared, its filters and its parent.
 * The Status Byte, with no parent, is the port's: its service request
 * enable and every bit but RQS cleared, and the port kept as the
 * model's output (0x2819c). */
void status_register_setup(int32_t model, int32_t index, uint32_t positive,
                           uint32_t negative, int32_t parent_index,
                           uint32_t parent_bit, int32_t port) {
  status_model* status = (status_model*)model;
  scpi_port* output = (scpi_port*)port;
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

/* Builds the status model, sets its registers up from status_register_layouts
 * and the saved enables, and marks the status system ready;
 * status_create's error (0x2f480).  The Standard Event register's
 * filter pointers are NULL, so its filters are ROM's first long's low
 * word, the reset stack pointer's, as the image reads them. */
uint32_t status_init(void) {
  static const int32_t ERROR_QUEUE_ROOM = 0x1e;

  status_request_service = 0;
  uint32_t error = status_create(&status_object, STATUS_REGISTERS,
                                 ERROR_QUEUE_ROOM, status_conditions, 0);

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

/* Sets every register's enable from nv_settings (0x2f3d0). */
void status_enables_init(void) {
  for (uint8_t index = 0; index < STATUS_REGISTERS; index++) {
    status_register_set_enable(status_object, index,
                               *status_register_layouts[index].enable);
  }
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

/* What status_preset_register sets, by the jump table at 0x2f386. */
enum {
  STATUS_PRESET_ENABLE = 0,
  STATUS_PRESET_POSITIVE = 1, /* a filtered register's, by its index less 2 */
  STATUS_PRESET_NEGATIVE = 2,
};

/* Sets a register's enable or transition filter for a request from
 * source 0, and 1; 0 for another source (0x2f34e). */
uint32_t status_preset_register(char setting, char source, int16_t index,
                                int16_t value) {
  if (source != 0) {
    return 0;
  }
  switch (setting) {
    case STATUS_PRESET_ENABLE:
      status_register_set_enable(status_object, index, value);
      break;
    case STATUS_PRESET_POSITIVE:
      status_register_set_ptr(status_object, index + STATUS_FIRST_FILTERED,
                              value);
      break;
    case STATUS_PRESET_NEGATIVE:
      status_register_set_ntr(status_object, index + STATUS_FIRST_FILTERED,
                              value);
      break;
  }
  return 1;
}

/* Sets a register's enable from nv_settings, reporting -300 to a source
 * that may not (0x43482). */
void status_enable_restore(uint32_t index, uint32_t source) {
  int8_t reg = index;

  if (status_preset_register(STATUS_PRESET_ENABLE, source, reg,
                             nv_settings.status_enable[reg]) == 0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* Sets a filtered register's positive transition filter from
 * nv_settings, by its index less 2 (0x434c2). */
void status_ptr_restore(uint32_t index, uint32_t source) {
  int8_t filter = index;

  if (status_preset_register(STATUS_PRESET_POSITIVE, source, filter,
                             nv_settings.status_positive_transition[filter]) ==
      0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* The same for its negative transition filter (0x43504). */
void status_ntr_restore(uint32_t index, uint32_t source) {
  int8_t filter = index;

  if (status_preset_register(STATUS_PRESET_NEGATIVE, source, filter,
                             nv_settings.status_negative_transition[filter]) ==
      0) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, source);
  }
}

/* :STATus:PRESet's two forms, status_preset_done's first argument, by
 * the jump table at 0x43560. */
enum {
  STATUS_PRESET_SCPI = 0,  /* the filtered registers' enables */
  STATUS_PRESET_ALARM = 1, /* every enable, and :QUEStionable:USER off */
};

/* :STATus:PRESet's hook: the enables its form gives and the filtered
 * registers' filters, set and saved for the source (0x43546). */
void status_preset_done(uint32_t form, uint32_t source) {
  /* The enables each form gives, by register (0x2f76e, 0x2f77c). */
  static const uint16_t SCPI_ENABLES[] = {0, 0, 0, 0, 7, 0xf, 0x1fff};
  static const uint16_t ALARM_ENABLES[] = {0x88, 0, 0x24, 3, 7, 8, 0x1fff};
  /* The filters, by filtered register (0x2f78a, 0x2f794). */
  static const uint16_t POSITIVE_FILTERS[] = {0x7f, 2, 7, 0xf, 0x13ff};
  static const uint16_t NEGATIVE_FILTERS[] = {0, 0, 0, 0, 0};

  switch ((char)form) {
    case STATUS_PRESET_SCPI:
      for (uint8_t index = STATUS_FIRST_FILTERED; index < STATUS_REGISTERS;
           index++) {
        nv_settings.status_enable[index] = SCPI_ENABLES[index];
        status_enable_restore(index, 0);
      }
      break;
    case STATUS_PRESET_ALARM:
      for (uint8_t index = 0; index < STATUS_REGISTERS; index++) {
        nv_settings.status_enable[index] = ALARM_ENABLES[index];
        status_enable_restore(index, 0);
      }
      nv_settings.questionable_user = 0;
      questionable_user_done();
      break;
  }
  for (uint8_t filter = 0; filter < STATUS_REGISTERS - STATUS_FIRST_FILTERED;
       filter++) {
    nv_settings.status_positive_transition[filter] = POSITIVE_FILTERS[filter];
    status_ptr_restore(filter, 0);
    nv_settings.status_negative_transition[filter] = NEGATIVE_FILTERS[filter];
    status_ntr_restore(filter, 0);
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

/* Where a code reported before the model is ready would be kept, an
 * early_status_events buffer; does nothing (0x2f53a). */
void status_event_hook(uint32_t buffer, uint8_t code) {}

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

/* Empties an early_status_events buffer (0x2f64a). */
void happening_buffer_clear(uint8_t* bytes) {
  early_status_events* buffer = (early_status_events*)bytes;

  buffer->count = 0;
  buffer->next = 0;
  for (uint8_t index = 0; index < sizeof buffer->events; index++) {
    buffer->events[index] = 0;
  }
}

/* Empties both early buffers (0x2f668). */
void happening_buffers_clear(void) {
  happening_buffer_clear((uint8_t*)&early_status_reports);
  happening_buffer_clear((uint8_t*)&early_status_happenings);
}

/* What happening_replay replays a buffer's codes as, by the jump table
 * at 0x2f6b2. */
enum {
  STATUS_REPLAY_REPORTS = 0,    /* by status_report */
  STATUS_REPLAY_HAPPENINGS = 1, /* by status_happening */
};

/* Replays a buffer's codes from first to last (0x2f680). */
void happening_replay(char* events, char mode, char first, char last) {
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
