/* The SCPI status system -- the status model's registers and
 * conditions, the event and enable words, and the error queue -- written by
 * hand in place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/structures.md, "The status system").  The exported C includes
 * this file after its own, so the types and prototypes it declares are in
 * scope; each function here keeps its generated prototype.  It uses scpi.c's
 * SCPI_STATUS_NO_MEMORY and SCPI_STATUS_REQUEST_SERVICE, which the export
 * includes before it. */

/* An error code's class, by the greatest code of each, which
 * error_queue_push reports through the negative conditions -1 to -4
 * (0x2863c to 0x28674); a positive code is a device's.  The command
 * errors' bounds are here, the other classes' in
 * status_error_class. */
static const int16_t STATUS_COMMAND_ERROR_GREATEST = -100;

/* The negative conditions for the error classes. */
enum {
  STATUS_CLASS_COMMAND_ERROR = -1,
  STATUS_CLASS_EXECUTION_ERROR = -2,
  STATUS_CLASS_DEVICE_ERROR = -3,
  STATUS_CLASS_QUERY_ERROR = -4,
};

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
    return SCPI_STATUS_NO_MEMORY;
  }
  status->register_count = register_count;
  status->field_0e = reserved;
  status->conditions = conditions;
  for (int16_t index = 0; index < register_count; index++) {
    status_register* reg = memory_allocate(sizeof(status_register));

    (*slot)->registers[index] = reg;
    if (reg == 0) {
      return SCPI_STATUS_NO_MEMORY;
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
        (uint16_t)enable & (STATUS_BYTE_BITS & ~SCPI_STATUS_REQUEST_SERVICE));
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
uint32_t error_queue_create(scpi_engine_error_queue** slot, int32_t capacity,
                            status_model* status) {
  /* A spare entry's room beyond the ring's. */
  scpi_engine_error_queue* ring = memory_allocate(
      capacity * sizeof(scpi_engine_error_entry) +
      sizeof(scpi_engine_error_queue) + sizeof(scpi_engine_error_entry));

  *slot = ring;
  if (ring == 0) {
    return SCPI_STATUS_NO_MEMORY;
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
    return STATUS_CLASS_QUERY_ERROR;
  }
  if (code <= ERROR_DEVICE_GREATEST || code > 0) {
    return STATUS_CLASS_DEVICE_ERROR;
  }
  if (code <= ERROR_EXECUTION_GREATEST) {
    return STATUS_CLASS_EXECUTION_ERROR;
  }
  if (code <= STATUS_COMMAND_ERROR_GREATEST) {
    return STATUS_CLASS_COMMAND_ERROR;
  }
  return 0;
}

/* Adds an error to the queue and reports its class; its last free entry
 * takes -350, Queue overflow, in its place.  1 when the queue is full or
 * overflows, else 0 (0x285d4). */
int16_t error_queue_push(scpi_engine_error_queue* queue, int32_t code,
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
void error_queue_pop(scpi_engine_error_queue* queue, int32_t* code,
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
void error_queue_destroy(scpi_engine_error_queue* queue) { memory_free(queue); }

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
  static const int16_t END = -1;

  for (scpi_error_message* entry = scpi_error_messages; entry->code != END;
       entry++) {
    if (entry->code == (int16_t)code) {
      return entry->text;
    }
  }
  return 0;
}
