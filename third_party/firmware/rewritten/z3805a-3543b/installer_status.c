/* The installer's copies of the status system's routines
 * (`services/status`; status.c has the primary's), on the installer's
 * status model, through status.h's bodies where they read nothing of the
 * installer's own.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* ORs bits into a register's event word and propagates (installer::0x105c78).
 */
void installer_status_event_set(status_model* status, status_register* reg,
                                uint32_t bits) {
  status_model_event_set(status, reg, bits);
}

/* Sets condition bits; those rising through the positive transition
 * filter become events (installer::0x105c96). */
void installer_status_condition_set(status_model* status, status_register* reg,
                                    int32_t bits) {
  status_model_condition_set(status, reg, bits);
}

/* Clears condition bits; those falling through the negative transition
 * filter become events (installer::0x105cf2). */
void installer_status_condition_clear(status_model* status,
                                      status_register* reg, int32_t bits) {
  status_model_condition_clear(status, reg, bits);
}

/* Sets or clears a register's summary bit in its parent (installer::0x105d52).
 */
void installer_status_propagate(status_model* status, status_register* reg) {
  status_model_propagate(status, reg);
}

/* The entry of a condition: a negative one in the installer's
 * status_negative_conditions, the rest in the model's table. */
static status_condition* installer_status_condition_entry(status_model* status,
                                                          int32_t condition) {
  if (condition < 0) {
    return &installer_status_negative_conditions[-condition];
  }
  return &status->conditions[condition];
}

/* Applies a condition by its kind; 1, or installer_error_queue_push's
 * result for an error (installer::0x105eb8). */
int16_t installer_status_set_bit(status_model* status, int32_t condition,
                                 int32_t value) {
  status_condition* entry = installer_status_condition_entry(status, condition);
  status_register* reg;

  switch (entry->kind) {
    case STATUS_KIND_EVENT_SET:
      reg = status->registers[entry->register_index];
      installer_status_event_set(status, reg, cpu_long_bit(entry->bit));
      break;
    case STATUS_KIND_SET:
      reg = status->registers[entry->register_index];
      installer_status_condition_set(status, reg, cpu_long_bit(entry->bit));
      break;
    case STATUS_KIND_CLEAR:
      reg = status->registers[entry->register_index];
      installer_status_condition_clear(status, reg, cpu_long_bit(entry->bit));
      break;
    case STATUS_KIND_ERROR:
      return installer_error_queue_push(status->queue, entry->register_index,
                                        value);
  }
  return 1;
}

/* The same, its result dropped (installer::0x105f7c). */
void installer_status_apply_condition(status_model* status, int32_t condition,
                                      int32_t value) {
  installer_status_set_bit(status, condition, value);
}

/* A register's event word, cleared as it is read (installer::0x106164). */
void installer_status_read_event(status_model* status, int32_t index,
                                 uint16_t* event) {
  status_model_read_event(status, index, event);
}

/* Adds an error to the queue and reports its class; its last free entry
 * takes -350, Queue overflow, in its place.  1 when the queue is full or
 * overflows, else 0 (installer::0x106282). */
int16_t installer_error_queue_push(status_error_queue* queue, int32_t code,
                                   uint32_t value) {
  /* The error the queue's last free entry takes, "Queue overflow" in
   * scpi_error_messages. */
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
  int32_t condition = status_model_error_class(error);

  if (condition != 0) {
    installer_status_apply_condition(status, condition, 0);
  }
  installer_status_prompt_refresh(status, error);
  return full;
}

/* An error code's message, NULL for none (installer::0x106548). */
char* installer_error_message_lookup(int32_t code) {
  return status_model_error_message(code);
}

/* Builds the installer's status model, two registers and its four
 * errors' conditions, marks it ready, and sets the registers up: the
 * Status Byte the console port's, register 1 from its layout, its
 * positive filter its defined bits (installer::0x107a00). */
uint32_t installer_status_init(void) {
  static const int32_t REGISTERS = 2;
  static const int32_t ERROR_QUEUE_ROOM = 0x1e;
  const installer_status_register_layout* layout =
      &installer_status_register_layouts[1];
  const uint32_t error =
      installer_status_create(&installer_status_object, REGISTERS,
                              ERROR_QUEUE_ROOM, installer_status_conditions, 0);

  if (error != 0) {
    return error;
  }
  installer_status_ready = 1;
  installer_status_register_setup((int32_t)installer_status_object, 0, 0, 0, 0,
                                  0, (int32_t)installer_scpi_console_port());
  installer_status_register_setup((int32_t)installer_status_object, 1,
                                  layout->defined_bits, 0, layout->parent_index,
                                  layout->parent_bit, 0);
  return 0;
}

/* Allocates the status model, its registers and its error queue, the
 * model into its slot; -56 for no memory (installer::0x105d98). */
uint32_t installer_status_create(status_model** slot, int32_t register_count,
                                 int32_t queue_capacity,
                                 status_condition* conditions,
                                 uint32_t reserved) {
  /* Room for one register pointer more than it keeps. */
  status_model* status = installer_memory_allocate(
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
    status_register* reg = installer_memory_allocate(sizeof(status_register));

    (*slot)->registers[index] = reg;
    if (reg == 0) {
      return STATUS_SCPI_NO_MEMORY;
    }
  }
  return installer_error_queue_create(&(*slot)->queue, queue_capacity, *slot);
}

/* Allocates the error queue, with room for capacity entries, into its
 * slot, reporting into a status model; -56 for no memory
 * (installer::0x106236). */
uint32_t installer_error_queue_create(status_error_queue** slot,
                                      int32_t capacity, status_model* status) {
  /* A spare entry's room beyond the ring's. */
  status_error_queue* ring = installer_memory_allocate(
      capacity * sizeof(status_error_entry) + sizeof(status_error_queue) +
      sizeof(status_error_entry));

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

/* Sets a register up: its words cleared, its filters and its parent;
 * the Status Byte, with no parent, the port's (installer::0x105e4a). */
void installer_status_register_setup(int32_t model, int32_t index,
                                     uint32_t positive, uint32_t negative,
                                     int32_t parent_index, uint32_t parent_bit,
                                     int32_t port) {
  status_model_register_setup((status_model*)model, index, positive, negative,
                              parent_index, parent_bit, (scpi_port*)port);
}

/* Puts a code into the status system once it is ready, with its
 * message: SCPI's for 0 and a negative code, its own four errors' from
 * 1 to 4; 0 (installer::0x107a86).  Unlike status_report it keeps
 * nothing reported before. */
uint32_t installer_status_report(int16_t code) {
  static const int16_t REPORT_MESSAGES =
      sizeof installer_status_report_messages /
      sizeof installer_status_report_messages[0];
  char* message;

  if (installer_status_ready == 0) {
    return 0;
  }
  if (code <= 0) {
    message = installer_error_message_lookup(code);
  } else if (code < REPORT_MESSAGES) {
    message = installer_status_report_messages[code];
  } else {
    message = 0;
  }
  /* The message is the error queue entry's value. */
  installer_status_set_bit(installer_status_object, code, (int32_t)message);
  return 0;
}
