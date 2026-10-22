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

/* Sets the masked bits of the running task's mode; the old mode, even on
 * an error (installer::0x102be2). */
uint32_t installer_psos_mode(uint32_t mode, uint32_t mask, uint8_t* old) {
  uint32_t error =
      installer_psos_call(PSOS_SERVICE_MODE, mode, mask, 0, 0, NULL, NULL);

  if (old != NULL) {
    *old = bridge_result_d0;
  }
  return error;
}

/* Frees a segment (installer::0x102c48). */
uint32_t installer_psos_free_segment(void* segment) {
  return installer_psos_call(PSOS_SERVICE_FREE_SEGMENT, 0, 0, 0, 0, segment,
                             NULL);
}

/* Receives a message from an exchange into a buffer, waiting, unless
 * no_wait, up to ticks, 0 without end (installer::0x102e18). */
uint32_t installer_psos_request_x(uint32_t exchange, void* buffer,
                                  uint32_t no_wait, uint32_t ticks) {
  return installer_psos_call(PSOS_SERVICE_REQUEST_X, ticks, no_wait, 0, 0,
                             (void*)exchange, buffer);
}

/* Opens a device with a long (installer::0x102f36). */
uint32_t installer_psos_io_open(uint32_t device, uint32_t a) {
  return installer_psos_io_call(PSOS_IO_OPEN, device, a, 0, 0, NULL, NULL);
}

/* Restarts the processor through trap #12 (docs/firmware/restart.md),
 * where the primary's enter_installer has trap #11
 * (installer::0x100d1c). */
void installer_restart(void) { __asm__ volatile("trap #12"); }

/* The kernel's version, which service 0 returns in D7 where the other
 * services return their error (installer::0x102abc). */
uint16_t installer_psos_version(void) {
  return installer_psos_call(PSOS_SERVICE_VERSION, 0, 0, 0, 0, NULL, NULL);
}

/* Suspends a task (installer::0x102b74). */
uint32_t installer_psos_suspend(uint32_t task) {
  return installer_psos_call(PSOS_SERVICE_SUSPEND, 0, 0, 0, 0, (void*)task,
                             NULL);
}

/* Resumes a task (installer::0x102b8c). */
uint32_t installer_psos_resume(uint32_t task) {
  return installer_psos_call(PSOS_SERVICE_RESUME, 0, 0, 0, 0, (void*)task,
                             NULL);
}

/* Changes a task's priority by a step; its old priority, even on an
 * error (installer::0x102ba4). */
uint32_t installer_psos_priority(uint32_t task, uint32_t priority,
                                 uint8_t* old) {
  uint32_t error = installer_psos_call(PSOS_SERVICE_PRIORITY, priority, 0, 0, 0,
                                       (void*)task, NULL);

  if (old != NULL) {
    *old = bridge_result_d0;
  }
  return error;
}

/* Gives a segment of the running task's to another task
 * (installer::0x102c60). */
uint32_t installer_psos_assign_segment(void* segment, uint32_t task) {
  return installer_psos_call(PSOS_SERVICE_ASSIGN_SEGMENT, 0, 0, 0, 0, segment,
                             (void*)task);
}

/* Takes a segment back from a task (installer::0x102c7c). */
uint32_t installer_psos_reclaim_segment(void* segment, uint32_t task) {
  return installer_psos_call(PSOS_SERVICE_RECLAIM_SEGMENT, 0, 0, 0, 0, segment,
                             (void*)task);
}

/* Makes a partition of fixed blocks in an area; its id, even on an
 * error (installer::0x102c98). */
uint32_t installer_psos_create_partition(void* area, uint32_t block_size,
                                         uint32_t count, void** partition) {
  uint32_t error = installer_psos_call(PSOS_SERVICE_CREATE_PARTITION,
                                       block_size, count, 0, 0, area, NULL);

  if (partition != NULL) {
    *partition = (void*)bridge_result_a0;
  }
  return error;
}

/* Deletes a partition (installer::0x102cc2). */
uint32_t installer_psos_delete_partition(void* partition, uint32_t block_size) {
  return installer_psos_call(PSOS_SERVICE_DELETE_PARTITION, block_size, 0, 0, 0,
                             partition, NULL);
}

/* Takes a block from a partition; on success the block
 * (installer::0x102cde). */
uint32_t installer_psos_get_block(void* partition, uint32_t block_size,
                                  void** block) {
  uint32_t error = installer_psos_call(PSOS_SERVICE_GET_BLOCK, block_size, 0, 0,
                                       0, partition, NULL);

  if (error == 0 && block != NULL) {
    *block = (void*)bridge_result_a1;
  }
  return error;
}

/* Returns a block to its partition (installer::0x102d06). */
uint32_t installer_psos_return_block(void* partition, void* block) {
  return installer_psos_call(PSOS_SERVICE_RETURN_BLOCK, 0, 0, 0, 0, partition,
                             block);
}

/* Attaches to an exchange by name; on success three longs, A0's, D1's
 * and D2's (installer::0x102d52). */
uint32_t installer_psos_service_21(char* name, uint32_t* out1, uint32_t* out2,
                                   uint32_t* out3) {
  uint32_t error = installer_psos_call(
      PSOS_SERVICE_ATTACH_X, installer_psos_pack_name(), 0, 0, 0, NULL, NULL);

  if (error == 0) {
    if (out1 != NULL) {
      *out1 = bridge_result_a0;
    }
    if (out2 != NULL) {
      *out2 = bridge_result_d1;
    }
    if (out3 != NULL) {
      *out3 = bridge_result_d2;
    }
  }
  return error;
}

/* Deletes an exchange (installer::0x102d8e). */
uint32_t installer_psos_delete_x(uint32_t exchange) {
  return installer_psos_call(PSOS_SERVICE_DELETE_X, 0, 0, 0, 0, (void*)exchange,
                             NULL);
}

/* Sends a message to the front of an exchange's queue
 * (installer::0x102dc2). */
uint32_t installer_psos_jam_x(uint32_t exchange, void* message) {
  return installer_psos_call(PSOS_SERVICE_JAM_X, 0, 0, 0, 0, (void*)exchange,
                             message);
}

/* Gives a message to every task waiting at an exchange and queues
 * copies up to a count; on success the larger of the count and the
 * tasks given it, and those tasks (installer::0x102dde). */
uint32_t installer_psos_broadcast_x(uint32_t exchange, void* message,
                                    uint32_t count, uint8_t* count_out,
                                    uint8_t* woken) {
  uint32_t error = installer_psos_call(PSOS_SERVICE_BROADCAST_X, count, 0, 0, 0,
                                       (void*)exchange, message);

  if (error == 0) {
    if (count_out != NULL) {
      *count_out = bridge_result_d0;
    }
    if (woken != NULL) {
      *woken = bridge_result_d1;
    }
  }
  return error;
}

/* The time of day: its date, time and ticks into the second, even on an
 * error (installer::0x102ec4). */
uint32_t installer_psos_get_time(uint32_t* date, uint32_t* time,
                                 uint16_t* ticks) {
  uint32_t error =
      installer_psos_call(PSOS_SERVICE_GET_TIME, 0, 0, 0, 0, NULL, NULL);

  if (date != NULL) {
    *date = bridge_result_d0;
  }
  if (time != NULL) {
    *time = bridge_result_d1;
  }
  if (ticks != NULL) {
    *ticks = bridge_result_d2;
  }
  return error;
}

/* Sets the time of day (installer::0x102efa). */
uint32_t installer_psos_set_time(uint32_t date, uint32_t time, uint32_t ticks) {
  return installer_psos_call(PSOS_SERVICE_SET_TIME, date, time, ticks, 0, NULL,
                             NULL);
}

/* Initializes a device (installer::0x102f1e). */
uint32_t installer_psos_io_init(uint32_t device) {
  return installer_psos_io_call(PSOS_IO_INIT, device, 0, 0, 0, NULL, NULL);
}

/* Closes a device (installer::0x102f52). */
uint32_t installer_psos_io_close(uint32_t device) {
  return installer_psos_io_call(PSOS_IO_CLOSE, device, 0, 0, 0, NULL, NULL);
}

/* Controls a device with two longs (installer::0x102fce). */
uint32_t installer_psos_io_control(uint32_t device, uint32_t a, uint32_t b) {
  return installer_psos_io_call(PSOS_IO_CONTROL, device, 0, a, b, NULL, NULL);
}

/* Packs the name and creates the mutex as an exchange of that name;
 * installer_psos_pack_name reads the name as this function's caller's
 * first argument (installer::0x1004e6). */
uint32_t installer_mutex_create(char* name, mutex* lock) {
  return installer_mutex_init(installer_psos_pack_name(), lock);
}

/* installer_mutex_acquire on its argument (installer::0x100502). */
uint32_t installer_mutex_lock(mutex* lock) {
  return installer_mutex_acquire(lock);
}

/* installer_mutex_release on its argument (installer::0x100518). */
uint32_t installer_mutex_unlock(mutex* lock) {
  return installer_mutex_release(lock);
}

/* The task holding a mutex (installer::0x10052e). */
uint32_t installer_mutex_owner(void* lock) { return os_mutex_owner(lock); }

/* Frees a segment installer_memory_allocate gave
 * (installer::0x108450). */
void installer_memory_free(void* segment) {
  installer_psos_free_segment(segment);
}

/* The running task's id; nothing calls it (installer::0x10845e). */
uint32_t installer_current_task_id(void) { return installer_psos_my_pid(); }

/* Sets the per-task switch, so that installer_exception_hook_get takes
 * the running task's hook first; 0 (installer::0x100546). */
int32_t installer_exception_hook_enable_per_task(void) {
  installer_exception_hook_per_task = OS_SWITCH_ON;
  return 0;
}

/* Sets the running task's own exception hook (installer::0x100552). */
int32_t installer_exception_hook_set_task(void* hook) {
  ((psos_task*)installer_psos_my_pid())->exception_hook = hook;
  return 0;
}

/* Sets the shared exception hook; OS_SWITCH_ON when one was set
 * before, else 0 (installer::0x100568). */
int32_t installer_exception_hook_set(void* hook) {
  int32_t was_set = installer_exception_hook != NULL ? OS_SWITCH_ON : 0;

  installer_exception_hook = hook;
  return was_set;
}

/* Clears the running task's own exception hook
 * (installer::0x100584). */
int32_t installer_exception_hook_clear_task(void) {
  return installer_exception_hook_set_task(NULL);
}

/* A task's user stack's use and size, then its supervisor stack's, as
 * installer_stack_usage measures them, its used bytes in D0's half of
 * the result and the size in D1's (installer::0x100da4). */
int32_t installer_stack_measure(void* record, uint32_t* usage) {
  static const uint32_t HALF_BITS = 32;
  static const uint32_t USER_STACK_GAP = 4;
  psos_task* task = record;
  uint64_t supervisor = installer_stack_usage((uint16_t*)task->stack_bottom,
                                              (char*)task->supervisor_top);

  usage[2] = supervisor >> HALF_BITS;
  usage[3] = supervisor;
  uint64_t user =
      installer_stack_usage((uint16_t*)(task->supervisor_top + USER_STACK_GAP),
                            (char*)task->user_top);

  usage[0] = user >> HALF_BITS;
  usage[1] = user;
  return 0;
}

/* A bare rts where the primary has stack_usage_report; nothing calls
 * it (installer::0x100e7e). */
void installer_stack_usage_report(void) {}

/* Sets a task's priority, which the kernel changes by a step from the
 * present one, and returns the priority it had
 * (installer::0x100eb6). */
uint8_t installer_task_set_priority(uint32_t task, uint8_t priority) {
  uint8_t old;

  installer_psos_priority(task, 0, &old);
  installer_psos_priority(task, priority - old, &old);
  return old;
}

/* Spins 729 empty loops for each count, the count a word once it is not
 * 0 (installer::0x100f40). */
void installer_busy_wait(uint32_t count) { os_busy_wait(count); }

/* Pauses a number of seconds, as kernel ticks, at least one; a negative
 * time pauses a tick, and one past 2^21 seconds that long
 * (installer::0x100f60). */
void installer_sleep_seconds(float seconds) {
  /* The kernel's ticks a second, and the shortest and longest pauses
   * it takes, a tick and 2^21 seconds. */
  static const float TICKS_PER_SECOND = 1024;
  static const float SLEEP_LONGEST = 1 << 21;

  if (installer_float_compare(0.0f, seconds) > 0) {
    seconds = 1.0f / TICKS_PER_SECOND;
  } else if (installer_float_compare(seconds, SLEEP_LONGEST) > 0) {
    seconds = SLEEP_LONGEST;
  }
  uint32_t ticks = installer_float_to_unsigned(
      installer_float_multiply(seconds, TICKS_PER_SECOND));

  installer_psos_pause(ticks == 0 ? 1 : ticks);
}
