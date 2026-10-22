/* The installer's copies of the kernel's calls, its configuration's
 * start and the exception hook (`platform/os`; os.c has the
 * primary's).  The installer, which the boot code unpacks into RAM
 * (docs/firmware/restart.md), has its own kernel and data, so each copy
 * calls the installer's routines and keeps the installer's globals.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Clears the exception hook and its per-task switch (installer::0x100538). */
void installer_exception_hook_reset(void) {
  installer_exception_hook_per_task = 0;
  installer_exception_hook = NULL;
}

/* The running task's exception hook while the per-task switch is set,
 * else, or when it has none, the shared one (installer::0x10058e). */
void* installer_exception_hook_get(void) {
  void* hook = NULL;

  if (installer_exception_hook_per_task != 0) {
    hook = ((psos_task*)installer_psos_my_pid())->exception_hook;
  }
  if (hook == NULL) {
    hook = installer_exception_hook;
  }
  return hook;
}

/* Copies the kernel's configuration and I/O table from their
 * defaults
 * (installer::0x1083b6). */
void installer_psos_config_init(void) {
  installer_memcpy(&installer_psos_config, &installer_psos_config_defaults,
                   sizeof installer_psos_config);
  installer_memcpy(&installer_io_table, &installer_io_table_defaults,
                   sizeof installer_io_table);
}

/* The running task's id, ident's for no name (installer::0x102b4c). */
uint32_t installer_psos_my_pid(void) {
  installer_psos_call(PSOS_SERVICE_IDENT, 0, 0, 0, 0, NULL, NULL);
  return bridge_result_a0;
}

/* Deletes a task (installer::0x102b5c). */
uint32_t installer_psos_delete(uint32_t task) {
  return installer_psos_call(PSOS_SERVICE_DELETE, 0, 0, 0, 0, (void*)task,
                             NULL);
}

/* Pauses the running task for some ticks (installer::0x102bca). */
uint32_t installer_psos_pause(uint32_t ticks) {
  return installer_psos_call(PSOS_SERVICE_PAUSE, ticks, 0, 0, 0, NULL, NULL);
}

/* Creates an exchange of a name, with two longs; on success its id
 * (installer::0x102d22). */
uint32_t installer_psos_create_x(char* name, uint32_t a, uint32_t b,
                                 uint32_t* exchange) {
  uint32_t error = installer_psos_call(
      PSOS_SERVICE_CREATE_X, installer_psos_pack_name(), a, b, 0, NULL, NULL);

  if (error == 0 && exchange != NULL) {
    *exchange = bridge_result_a0;
  }
  return error;
}

/* Sends a message to an exchange (installer::0x102da6). */
uint32_t installer_psos_send_x(uint32_t exchange, void* message) {
  return installer_psos_call(PSOS_SERVICE_SEND_X, 0, 0, 0, 0, (void*)exchange,
                             message);
}

/* Signals events to a variable's task (installer::0x102e3c). */
uint32_t installer_psos_signal_v(uint32_t variable, uint32_t value) {
  return installer_psos_call(PSOS_SERVICE_SIGNAL_V, 0, value, 0, 0,
                             (void*)variable, NULL);
}

/* Waits on a variable with the longs in D1, D2 and D0; D1 back, even on
 * an error (installer::0x102e58). */
uint32_t installer_psos_wait_v(uint32_t d1, uint32_t d2, uint32_t d0,
                               uint32_t* out) {
  uint32_t error =
      installer_psos_call(PSOS_SERVICE_WAIT_V, d0, d1, d2, 0, NULL, NULL);

  if (out != NULL) {
    *out = bridge_result_d1;
  }
  return error;
}

/* Reads a variable with the longs in D1 and D2; D1 back, even on an
 * error (installer::0x102e86). */
uint32_t installer_psos_get_v(uint32_t d1, uint32_t d2, uint32_t* out) {
  uint32_t error =
      installer_psos_call(PSOS_SERVICE_GET_V, 0, d1, d2, 0, NULL, NULL);

  if (out != NULL) {
    *out = bridge_result_d1;
  }
  return error;
}

/* Counts a tick (installer::0x102eb0). */
uint32_t installer_psos_announce_tick(void) {
  return installer_psos_call(PSOS_SERVICE_ANNOUNCE_TICK, 0, 0, 0, 0, NULL,
                             NULL);
}

/* Creates a task of a name and priority, its supervisor and user
 * stacks' sizes in words packed in D3, a word each; on success its id
 * (installer::0x102ac8). */
uint32_t installer_psos_create(char* name, uint32_t a, uint32_t priority,
                               uint32_t stack_a, uint32_t stack_b, void* e,
                               uint32_t* task) {
  uint32_t error =
      installer_psos_call(PSOS_SERVICE_SPAWN, installer_psos_pack_name(), a,
                          priority, os_stack_sizes(stack_a, stack_b), e, NULL);

  if (error == 0 && task != NULL) {
    *task = bridge_result_a0;
  }
  return error;
}

/* Starts a created task at its entry (installer::0x102b02). */
uint32_t installer_psos_start(uint32_t task, void* entry) {
  return installer_psos_call(PSOS_SERVICE_START, 0, 0, 0, 0, (void*)task,
                             entry);
}

/* Looks a task up by name; on success its id and a byte
 * (installer::0x102b1e). */
uint32_t installer_psos_ident(char* name, uint32_t* id, uint8_t* byte) {
  uint32_t error = installer_psos_call(
      PSOS_SERVICE_IDENT, installer_psos_pack_name(), 0, 0, 0, NULL, NULL);

  if (error == 0) {
    if (id != NULL) {
      *id = bridge_result_a0;
    }
    if (byte != NULL) {
      *byte = bridge_result_d0;
    }
  }
  return error;
}

/* Allocates a segment of the size in D1 with the other three longs in
 * D2, D3 and D0; on success the segment (installer::0x102c14). */
uint32_t installer_psos_service_8(uint32_t size, uint32_t d2, uint32_t d3,
                                  uint32_t d0, uint32_t* out) {
  uint32_t error = installer_psos_call(PSOS_SERVICE_ALLOCATE_SEGMENT, d0, size,
                                       d2, d3, NULL, NULL);

  if (error == 0 && out != NULL) {
    *out = bridge_result_a0;
  }
  return error;
}

/* A segment of a size, from service 8; on an error whatever its local
 * held (installer::0x108428). */
void* installer_memory_allocate(uint32_t size) {
  uint32_t segment;

  installer_psos_service_8(size, 1, 1, 0, &segment);
  return (void*)segment;
}

/* Reads up to a length from a device; on success the length read
 * (installer::0x102f6a). */
uint32_t installer_psos_io_read(uint32_t device, void* buffer,
                                uint32_t* length) {
  uint32_t error = installer_psos_io_call(PSOS_IO_READ, device, 0, *length, 0,
                                          length, buffer);

  if (error == 0 && length != NULL) {
    *length = bridge_result_d2;
  }
  return error;
}

/* Writes a length to a device; on success the length written
 * (installer::0x102f9c). */
uint32_t installer_psos_io_write(uint32_t device, void* buffer,
                                 uint32_t* length) {
  uint32_t error = installer_psos_io_call(PSOS_IO_WRITE, device, 0, *length, 0,
                                          buffer, length);

  if (error == 0 && length != NULL) {
    *length = bridge_result_d2;
  }
  return error;
}
