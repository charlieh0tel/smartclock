/* The SCPI status system -- the status model's registers and
 * conditions, the event and enable words, and the error queue -- written by
 * hand in place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/structures.md, "The status system").  The exported C includes
 * this file after its own, so the types and prototypes it declares are in
 * scope; each function here keeps its generated prototype. */

/* ORs bits into a register's event word and propagates. */
void status_event_set(status_model* status, status_register* reg,
                      uint32_t bits) {
  status_model_event_set(status, reg, bits);
}

/* Sets condition bits; those rising through the positive transition
 * filter become events. */
void status_condition_set(status_model* status, status_register* reg,
                          int32_t bits) {
  status_model_condition_set(status, reg, bits);
}

/* Clears condition bits; those falling through the negative transition
 * filter become events. */
void status_condition_clear(status_model* status, status_register* reg,
                            int32_t bits) {
  status_model_condition_clear(status, reg, bits);
}

/* Sets or clears a register's summary bit in its parent. */
void status_propagate(status_model* status, status_register* reg) {
  status_model_propagate(status, reg);
}

/* Allocates the status model, its registers and its error queue, the
 * model into its slot; -56 for no memory. */
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

/* Frees the status model's queue and registers; called by nothing. */
void status_destroy(status_model* status) {
  error_queue_destroy(status->queue);
  for (int32_t index = 0; index < status->register_count; index++) {
    memory_free(status->registers[index]);
  }
  memory_free(status);
}

/* The entry of a condition: a negative one in status_negative_conditions,
 * the rest in the model's table (status_set_bit+0x12 to status_set_bit+0x34).
 */
static status_condition* status_condition_entry(status_model* status,
                                                int32_t condition) {
  if (condition < 0) {
    return &status_negative_conditions[-condition];
  }
  return &status->conditions[condition];
}

/* Applies a condition by its kind; 1, or error_queue_push's result for
 * an error. */
int16_t status_set_bit(status_model* status, int32_t condition, int32_t value) {
  status_condition* entry = status_condition_entry(status, condition);
  status_register* reg;

  switch (entry->kind) {
    case STATUS_KIND_EVENT_SET:
      reg = status->registers[entry->register_index];
      status_event_set(status, reg, cpu_long_bit(entry->bit));
      break;
    case STATUS_KIND_SET:
      reg = status->registers[entry->register_index];
      status_condition_set(status, reg, cpu_long_bit(entry->bit));
      break;
    case STATUS_KIND_CLEAR:
      reg = status->registers[entry->register_index];
      status_condition_clear(status, reg, cpu_long_bit(entry->bit));
      break;
    case STATUS_KIND_ERROR:
      return error_queue_push(status->queue, entry->register_index, value);
  }
  return 1;
}

/* The same, its result dropped. */
void status_apply_condition(status_model* status, int32_t condition,
                            int32_t value) {
  status_set_bit(status, condition, value);
}

/* status_set_bit with 0. */
int16_t status_clear_bit(status_model* status, int32_t condition) {
  return status_set_bit(status, condition, 0);
}

/* 1 for a negative condition that queues a command error, -100 to -199. */
uint32_t condition_is_command_error(int32_t condition) {
  return status_model_command_error(status_negative_conditions, condition);
}

/* A condition's bit in its register's condition word, inverted for one
 * that clears it.  The word is sign-extended to the long the
 * bit is tested in. */
void status_condition_bit_get(status_model* status, int32_t condition,
                              uint32_t* bit) {
  status_model_condition_bit_get(status, condition, bit);
}

/* Sets a register's enable word, as :ENABle and *ESE do; the top
 * register's is the port's service request enable, without RQS. */
void status_register_set_enable(status_model* status, int32_t index,
                                int32_t enable) {
  status_model_register_set_enable(status, index, enable);
}

/* A register's enable word, as :ENABle? and *ESE? read it. */
void status_register_get_enable(status_model* status, int32_t index,
                                uint16_t* enable) {
  status_model_register_get_enable(status, index, enable);
}

/* A register's event word, cleared as it is read. */
void status_read_event(status_model* status, int32_t index, uint16_t* event) {
  status_model_read_event(status, index, event);
}

/* A register's condition word, as :CONDition? reads it. */
void status_read_condition(status_model* status, int32_t index,
                           uint16_t* condition) {
  status_model_read_condition(status, index, condition);
}

/* Reads every register's event word but the top one's, clearing them,
 * for *CLS. */
uint32_t status_clear_events(status_model* status) {
  return status_model_clear_events(status);
}

/* Takes the oldest error from the queue, its value dropped, for *CLS. */
void error_queue_clear(status_model* status, int32_t* code) {
  uint32_t discarded;

  error_queue_pop(status->queue, code, &discarded);
}

/* Takes the oldest error and its value from the queue. */
void error_queue_read(status_model* status, int32_t* code, uint32_t* message) {
  error_queue_pop(status->queue, code, message);
}

/* Allocates the error queue, with room for capacity entries, into its
 * slot, reporting into a status model; -56 for no memory. */
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

/* Adds an error to the queue and reports its class; its last free entry
 * takes -350, Queue overflow, in its place.  1 when the queue is full or
 * overflows, else 0. */
int16_t error_queue_push(status_error_queue* queue, int32_t code,
                         uint32_t value) {
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
      error = STATUS_ERROR_QUEUE_OVERFLOW;
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
  int32_t condition = status_model_error_class(error);

  if (condition != 0) {
    status_apply_condition(status, condition, 0);
  }
  status_prompt_refresh(status, error);
  return full;
}

/* Takes the oldest error and its value, 0 and 0 when the queue is
 * empty, and prompts again once it empties. */
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

/* Frees the error queue. */
void error_queue_destroy(status_error_queue* queue) { memory_free(queue); }

/* Sets a register's positive transition filter, for :PTRansition. */
void status_register_set_ptr(status_model* status, int32_t index,
                             uint32_t filter) {
  status->registers[index]->positive_transition = filter;
}

/* Sets a register's negative transition filter, for :NTRansition. */
void status_register_set_ntr(status_model* status, int32_t index,
                             uint32_t filter) {
  status->registers[index]->negative_transition = filter;
}

/* A register's positive transition filter, for :PTRansition?. */
void status_register_get_ptr(status_model* status, int32_t index,
                             uint16_t* filter) {
  *filter = status->registers[index]->positive_transition;
}

/* A register's negative transition filter, for :NTRansition?. */
void status_register_get_ntr(status_model* status, int32_t index,
                             uint16_t* filter) {
  *filter = status->registers[index]->negative_transition;
}

/* An error code's message, NULL for none. */
char* error_message_lookup(int32_t code) {
  return status_model_error_message(code);
}

/* The registers status_create makes (structures.md, "The status
 * system"). */
static const uint8_t STATUS_REGISTERS = 7;

/* Sets a register up: its words cleared, its filters and its parent;
 * the Status Byte, with no parent, the port's. */
void status_register_setup(int32_t model, int32_t index, uint32_t positive,
                           uint32_t negative, int32_t parent_index,
                           uint32_t parent_bit, int32_t port) {
  status_model_register_setup((status_model*)model, index, positive, negative,
                              parent_index, parent_bit, (scpi_port*)port);
}

/* Sets every register's enable from nv_settings. */
void status_enables_init(void) {
  for (uint8_t index = 0; index < STATUS_REGISTERS; index++) {
    status_register_set_enable(status_object, index,
                               *status_register_layouts[index].enable);
  }
}

/* status_report for a source that reports, a SCPI command or startup;
 * the others, and any past REQUEST_SOURCE_INTERNAL, do not. */
void status_report_from(int16_t code, request_source source) {
  /* Whether each source reports, by the jump table at status_report_from+0x16.
   */
  static const bool SOURCE_REPORTS[REQUEST_SOURCE_INTERNAL + 1] = {
      [REQUEST_SOURCE_SCPI] = true,
      [REQUEST_SOURCE_STARTUP] = true,
  };

  if (source < sizeof SOURCE_REPORTS && SOURCE_REPORTS[source]) {
    status_report(code);
  }
}

/* Empties an early_status_events buffer. */
void happening_buffer_clear(uint8_t* bytes) {
  early_status_events* buffer = (early_status_events*)bytes;

  buffer->count = 0;
  buffer->next = 0;
  for (uint8_t index = 0; index < sizeof buffer->events; index++) {
    buffer->events[index] = 0;
  }
}

/* Empties both early buffers. */
void happening_buffers_clear(void) {
  happening_buffer_clear((uint8_t*)&early_status_reports);
  happening_buffer_clear((uint8_t*)&early_status_happenings);
}

/* Replays one of the two early buffers, a ring of five: from the oldest
 * once it has wrapped, then from its start, and empties both.  For a
 * buffer neither, the mode is D2's low byte as the caller left it. */
void happening_flush(char* bytes) {
  /* The register as the caller left it, which the image reads. */
  status_replay_mode mode =
      (status_replay_mode)CPU_CALLERS_REGISTER(int32_t, "d2");
  static const int8_t RING_SIZE = sizeof early_status_reports.events;
  early_status_events* buffer = (early_status_events*)bytes;

  if (buffer == &early_status_reports) {
    mode = STATUS_REPLAY_REPORTS;
  } else if (buffer == &early_status_happenings) {
    mode = STATUS_REPLAY_HAPPENINGS;
  }
  int8_t last = (buffer->count > RING_SIZE ? RING_SIZE : buffer->count) - 1;

  if (buffer->count > RING_SIZE && buffer->next != 0) {
    happening_replay((char*)buffer->events, mode, buffer->next, last);
    last = buffer->next - 1;
  }
  happening_replay((char*)buffer->events, mode, 0, last);
  happening_buffers_clear();
}
