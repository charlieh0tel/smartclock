/* The installer's copies of the kernel's calls, its configuration's
 * start and the exception hook (`platform/os`; os.c has the
 * primary's).  The installer, which the boot code unpacks into RAM
 * (docs/firmware/restart.md), has its own kernel and data, so each copy
 * calls the installer's routines and keeps the installer's globals.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Clears the exception hook and its per-task switch. */
void installer_exception_hook_reset(void) {
  installer_exception_hook_per_task = 0;
  installer_exception_hook = NULL;
}

/* The running task's exception hook while the per-task switch is set,
 * else, or when it has none, the shared one. */
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
 * defaults. */
void installer_psos_config_init(void) {
  installer_memcpy(&installer_psos_config, &installer_psos_config_defaults,
                   sizeof installer_psos_config);
  installer_memcpy(&installer_io_table, &installer_io_table_defaults,
                   sizeof installer_io_table);
}

/* The running task's id, ident's for no name. */
uint32_t installer_psos_my_pid(void) {
  return (uint32_t)os_installer_psos_call(PSOS_SERVICE_IDENT, (os_registers){0})
      .a0;
}

/* Deletes a task. */
uint32_t installer_psos_delete(uint32_t task) {
  return os_installer_psos_call(PSOS_SERVICE_DELETE,
                                (os_registers){.a0 = (void*)task})
      .error;
}

/* Pauses the running task for some ticks. */
uint32_t installer_psos_pause(uint32_t ticks) {
  return os_installer_psos_call(PSOS_SERVICE_PAUSE, (os_registers){.d0 = ticks})
      .error;
}

/* Creates an exchange of a name, with two longs; on success its id. */
uint32_t installer_psos_create_x(char* name, uint32_t a, uint32_t b,
                                 uint32_t* exchange) {
  const os_registers created = os_installer_psos_call(
      PSOS_SERVICE_CREATE_X,
      (os_registers){
          .d0 = OS_PACK_NAME("installer_psos_pack_name"), .d1 = a, .d2 = b});

  if (!created.failed && exchange != NULL) {
    *exchange = (uint32_t)created.a0;
  }
  return created.error;
}

/* Sends a message to an exchange. */
uint32_t installer_psos_send_x(uint32_t exchange, void* message) {
  return os_installer_psos_call(
             PSOS_SERVICE_SEND_X,
             (os_registers){.a0 = (void*)exchange, .a1 = message})
      .error;
}

/* Signals events to a variable's task. */
uint32_t installer_psos_signal_v(uint32_t variable, uint32_t value) {
  return os_installer_psos_call(
             PSOS_SERVICE_SIGNAL_V,
             (os_registers){.d1 = value, .a0 = (void*)variable})
      .error;
}

/* Waits on a variable with the longs in D1, D2 and D0; D1 back, even on
 * an error. */
uint32_t installer_psos_wait_v(uint32_t d1, uint32_t d2, uint32_t d0,
                               uint32_t* out) {
  const os_registers waited = os_installer_psos_call(
      PSOS_SERVICE_WAIT_V, (os_registers){.d0 = d0, .d1 = d1, .d2 = d2});

  if (out != NULL) {
    *out = waited.d1;
  }
  return waited.error;
}

/* Reads a variable with the longs in D1 and D2; D1 back, even on an
 * error. */
uint32_t installer_psos_get_v(uint32_t d1, uint32_t d2, uint32_t* out) {
  const os_registers read = os_installer_psos_call(
      PSOS_SERVICE_GET_V, (os_registers){.d1 = d1, .d2 = d2});

  if (out != NULL) {
    *out = read.d1;
  }
  return read.error;
}

/* Counts a tick. */
uint32_t installer_psos_announce_tick(void) {
  return os_installer_psos_call(PSOS_SERVICE_ANNOUNCE_TICK, (os_registers){0})
      .error;
}

/* Creates a task of a name and priority, its supervisor and user
 * stacks' sizes in words packed in D3, a word each; on success its id. */
uint32_t installer_psos_create(char* name, uint32_t a, uint32_t priority,
                               uint32_t stack_a, uint32_t stack_b, void* e,
                               uint32_t* task) {
  const os_registers created = os_installer_psos_call(
      PSOS_SERVICE_SPAWN,
      (os_registers){.d0 = OS_PACK_NAME("installer_psos_pack_name"),
                     .d1 = a,
                     .d2 = priority,
                     .d3 = os_stack_sizes(stack_a, stack_b),
                     .a0 = e});

  if (!created.failed && task != NULL) {
    *task = (uint32_t)created.a0;
  }
  return created.error;
}

/* Starts a created task at its entry. */
uint32_t installer_psos_start(uint32_t task, void* entry) {
  return os_installer_psos_call(PSOS_SERVICE_START,
                                (os_registers){.a0 = (void*)task, .a1 = entry})
      .error;
}

/* Looks a task up by name; on success its id and a byte. */
uint32_t installer_psos_ident(char* name, uint32_t* id, uint8_t* byte) {
  const os_registers found = os_installer_psos_call(
      PSOS_SERVICE_IDENT,
      (os_registers){.d0 = OS_PACK_NAME("installer_psos_pack_name")});

  if (!found.failed) {
    if (id != NULL) {
      *id = (uint32_t)found.a0;
    }
    if (byte != NULL) {
      *byte = found.d0;
    }
  }
  return found.error;
}

/* Allocates a segment of the size in D1 with the other three longs in
 * D2, D3 and D0; on success the segment. */
uint32_t installer_psos_service_8(uint32_t size, uint32_t d2, uint32_t d3,
                                  uint32_t d0, uint32_t* out) {
  const os_registers allocated = os_installer_psos_call(
      PSOS_SERVICE_ALLOCATE_SEGMENT,
      (os_registers){.d0 = d0, .d1 = size, .d2 = d2, .d3 = d3});

  if (!allocated.failed && out != NULL) {
    *out = (uint32_t)allocated.a0;
  }
  return allocated.error;
}

/* A segment of a size, from service 8; on an error whatever its local
 * held. */
void* installer_memory_allocate(uint32_t size) {
  uint32_t segment;

  installer_psos_service_8(size, 1, 1, 0, &segment);
  return (void*)segment;
}

/* Reads up to a length from a device; on success the length read. */
uint32_t installer_psos_io_read(uint32_t device, void* buffer,
                                uint32_t* length) {
  const os_registers read = os_installer_psos_io_call(
      PSOS_IO_READ,
      (os_registers){.d0 = device, .d2 = *length, .a0 = length, .a1 = buffer});

  if (!read.failed && length != NULL) {
    *length = read.d2;
  }
  return read.error;
}

/* Writes a length to a device; on success the length written. */
uint32_t installer_psos_io_write(uint32_t device, void* buffer,
                                 uint32_t* length) {
  const os_registers written = os_installer_psos_io_call(
      PSOS_IO_WRITE,
      (os_registers){.d0 = device, .d2 = *length, .a0 = buffer, .a1 = length});

  if (!written.failed && length != NULL) {
    *length = written.d2;
  }
  return written.error;
}

/* Sets the masked bits of the running task's mode; the old mode, even on
 * an error. */
uint32_t installer_psos_mode(uint32_t mode, uint32_t mask, uint8_t* old) {
  const os_registers changed = os_installer_psos_call(
      PSOS_SERVICE_MODE, (os_registers){.d0 = mode, .d1 = mask});

  if (old != NULL) {
    *old = changed.d0;
  }
  return changed.error;
}

/* Frees a segment. */
uint32_t installer_psos_free_segment(void* segment) {
  return os_installer_psos_call(PSOS_SERVICE_FREE_SEGMENT,
                                (os_registers){.a0 = segment})
      .error;
}

/* Receives a message from an exchange into a buffer, waiting, unless
 * no_wait, up to ticks, 0 without end. */
uint32_t installer_psos_request_x(uint32_t exchange, void* buffer,
                                  uint32_t no_wait, uint32_t ticks) {
  return os_installer_psos_call(PSOS_SERVICE_REQUEST_X,
                                (os_registers){.d0 = ticks,
                                               .d1 = no_wait,
                                               .a0 = (void*)exchange,
                                               .a1 = buffer})
      .error;
}

/* Opens a device with a long. */
uint32_t installer_psos_io_open(uint32_t device, uint32_t a) {
  return os_installer_psos_io_call(PSOS_IO_OPEN,
                                   (os_registers){.d0 = device, .d1 = a})
      .error;
}

/* Restarts the processor through trap #12 (docs/firmware/restart.md),
 * where the primary's enter_installer has trap #11. */
void installer_restart(void) { __asm__ volatile("trap #12"); }

/* The kernel's version, which service 0 returns in D7 where the other
 * services return their error. */
uint16_t installer_psos_version(void) {
  return os_installer_psos_call(PSOS_SERVICE_VERSION, (os_registers){0}).error;
}

/* Suspends a task. */
uint32_t installer_psos_suspend(uint32_t task) {
  return os_installer_psos_call(PSOS_SERVICE_SUSPEND,
                                (os_registers){.a0 = (void*)task})
      .error;
}

/* Resumes a task. */
uint32_t installer_psos_resume(uint32_t task) {
  return os_installer_psos_call(PSOS_SERVICE_RESUME,
                                (os_registers){.a0 = (void*)task})
      .error;
}

/* Changes a task's priority by a step; its old priority, even on an
 * error. */
uint32_t installer_psos_priority(uint32_t task, uint32_t priority,
                                 uint8_t* old) {
  const os_registers changed = os_installer_psos_call(
      PSOS_SERVICE_PRIORITY, (os_registers){.d0 = priority, .a0 = (void*)task});

  if (old != NULL) {
    *old = changed.d0;
  }
  return changed.error;
}

/* Gives a segment of the running task's to another task. */
uint32_t installer_psos_assign_segment(void* segment, uint32_t task) {
  return os_installer_psos_call(
             PSOS_SERVICE_ASSIGN_SEGMENT,
             (os_registers){.a0 = segment, .a1 = (void*)task})
      .error;
}

/* Takes a segment back from a task. */
uint32_t installer_psos_reclaim_segment(void* segment, uint32_t task) {
  return os_installer_psos_call(
             PSOS_SERVICE_RECLAIM_SEGMENT,
             (os_registers){.a0 = segment, .a1 = (void*)task})
      .error;
}

/* Makes a partition of fixed blocks in an area; its id, even on an
 * error. */
uint32_t installer_psos_create_partition(void* area, uint32_t block_size,
                                         uint32_t count, void** partition) {
  const os_registers created = os_installer_psos_call(
      PSOS_SERVICE_CREATE_PARTITION,
      (os_registers){.d0 = block_size, .d1 = count, .a0 = area});

  if (partition != NULL) {
    *partition = created.a0;
  }
  return created.error;
}

/* Deletes a partition. */
uint32_t installer_psos_delete_partition(void* partition, uint32_t block_size) {
  return os_installer_psos_call(
             PSOS_SERVICE_DELETE_PARTITION,
             (os_registers){.d0 = block_size, .a0 = partition})
      .error;
}

/* Takes a block from a partition; on success the block. */
uint32_t installer_psos_get_block(void* partition, uint32_t block_size,
                                  void** block) {
  const os_registers taken =
      os_installer_psos_call(PSOS_SERVICE_GET_BLOCK,
                             (os_registers){.d0 = block_size, .a0 = partition});

  if (!taken.failed && block != NULL) {
    *block = taken.a1;
  }
  return taken.error;
}

/* Returns a block to its partition. */
uint32_t installer_psos_return_block(void* partition, void* block) {
  return os_installer_psos_call(PSOS_SERVICE_RETURN_BLOCK,
                                (os_registers){.a0 = partition, .a1 = block})
      .error;
}

/* Attaches to an exchange by name; on success three longs, A0's, D1's
 * and D2's. */
uint32_t installer_psos_service_21(char* name, uint32_t* out1, uint32_t* out2,
                                   uint32_t* out3) {
  const os_registers attached = os_installer_psos_call(
      PSOS_SERVICE_ATTACH_X,
      (os_registers){.d0 = OS_PACK_NAME("installer_psos_pack_name")});

  if (!attached.failed) {
    if (out1 != NULL) {
      *out1 = (uint32_t)attached.a0;
    }
    if (out2 != NULL) {
      *out2 = attached.d1;
    }
    if (out3 != NULL) {
      *out3 = attached.d2;
    }
  }
  return attached.error;
}

/* Deletes an exchange. */
uint32_t installer_psos_delete_x(uint32_t exchange) {
  return os_installer_psos_call(PSOS_SERVICE_DELETE_X,
                                (os_registers){.a0 = (void*)exchange})
      .error;
}

/* Sends a message to the front of an exchange's queue. */
uint32_t installer_psos_jam_x(uint32_t exchange, void* message) {
  return os_installer_psos_call(
             PSOS_SERVICE_JAM_X,
             (os_registers){.a0 = (void*)exchange, .a1 = message})
      .error;
}

/* Gives a message to every task waiting at an exchange and queues
 * copies up to a count; on success the larger of the count and the
 * tasks given it, and those tasks. */
uint32_t installer_psos_broadcast_x(uint32_t exchange, void* message,
                                    uint32_t count, uint8_t* count_out,
                                    uint8_t* woken) {
  const os_registers sent = os_installer_psos_call(
      PSOS_SERVICE_BROADCAST_X,
      (os_registers){.d0 = count, .a0 = (void*)exchange, .a1 = message});

  if (!sent.failed) {
    if (count_out != NULL) {
      *count_out = sent.d0;
    }
    if (woken != NULL) {
      *woken = sent.d1;
    }
  }
  return sent.error;
}

/* The time of day: its date, time and ticks into the second, even on an
 * error. */
uint32_t installer_psos_get_time(uint32_t* date, uint32_t* time,
                                 uint16_t* ticks) {
  const os_registers now =
      os_installer_psos_call(PSOS_SERVICE_GET_TIME, (os_registers){0});

  if (date != NULL) {
    *date = now.d0;
  }
  if (time != NULL) {
    *time = now.d1;
  }
  if (ticks != NULL) {
    *ticks = now.d2;
  }
  return now.error;
}

/* Sets the time of day. */
uint32_t installer_psos_set_time(uint32_t date, uint32_t time, uint32_t ticks) {
  return os_installer_psos_call(
             PSOS_SERVICE_SET_TIME,
             (os_registers){.d0 = date, .d1 = time, .d2 = ticks})
      .error;
}

/* Initializes a device. */
uint32_t installer_psos_io_init(uint32_t device) {
  return os_installer_psos_io_call(PSOS_IO_INIT, (os_registers){.d0 = device})
      .error;
}

/* Closes a device. */
uint32_t installer_psos_io_close(uint32_t device) {
  return os_installer_psos_io_call(PSOS_IO_CLOSE, (os_registers){.d0 = device})
      .error;
}

/* Controls a device with two longs. */
uint32_t installer_psos_io_control(uint32_t device, uint32_t a, uint32_t b) {
  return os_installer_psos_io_call(
             PSOS_IO_CONTROL, (os_registers){.d0 = device, .d2 = a, .d3 = b})
      .error;
}

/* Nothing: a bare rts where the primary has cpu_register_test, next after
 * installer_psos_io_control as there; nothing calls it. */
void installer_cpu_register_test(void) {}

/* Packs the name and creates the mutex as an exchange of that name;
 * installer_psos_pack_name reads the name as this function's caller's
 * first argument. */
uint32_t installer_mutex_create(char* name, mutex* lock) {
  return installer_mutex_init(OS_PACK_NAME("installer_psos_pack_name"), lock);
}

/* installer_mutex_acquire on its argument. */
uint32_t installer_mutex_lock(mutex* lock) {
  return installer_mutex_acquire(lock);
}

/* installer_mutex_release on its argument. */
uint32_t installer_mutex_unlock(mutex* lock) {
  return installer_mutex_release(lock);
}

/* The task holding a mutex. */
uint32_t installer_mutex_owner(void* lock) { return os_mutex_owner(lock); }

/* Frees a segment installer_memory_allocate gave. */
void installer_memory_free(void* segment) {
  installer_psos_free_segment(segment);
}

/* The running task's id; nothing calls it. */
uint32_t installer_current_task_id(void) { return installer_psos_my_pid(); }

/* Sets the per-task switch, so that installer_exception_hook_get takes
 * the running task's hook first; 0. */
int32_t installer_exception_hook_enable_per_task(void) {
  installer_exception_hook_per_task = OS_SWITCH_ON;
  return 0;
}

/* Sets the running task's own exception hook. */
int32_t installer_exception_hook_set_task(void* hook) {
  ((psos_task*)installer_psos_my_pid())->exception_hook = hook;
  return 0;
}

/* Sets the shared exception hook; OS_SWITCH_ON when one was set
 * before, else 0. */
int32_t installer_exception_hook_set(void* hook) {
  int32_t was_set = installer_exception_hook != NULL ? OS_SWITCH_ON : 0;

  installer_exception_hook = hook;
  return was_set;
}

/* Clears the running task's own exception hook. */
int32_t installer_exception_hook_clear_task(void) {
  return installer_exception_hook_set_task(NULL);
}

/* A task's user stack's use and size, then its supervisor stack's, as
 * installer_stack_usage measures them, its used bytes in D0's half of
 * the result and the size in D1's. */
int32_t installer_stack_measure(void* record, uint32_t* usage) {
  psos_task* task = record;
  uint64_t supervisor = installer_stack_usage((uint16_t*)task->stack_bottom,
                                              (char*)task->supervisor_top);

  usage[2] = supervisor >> CPU_LONG_BITS;
  usage[3] = supervisor;
  uint64_t user = installer_stack_usage(
      (uint16_t*)(task->supervisor_top + OS_USER_STACK_GAP),
      (char*)task->user_top);

  usage[0] = user >> CPU_LONG_BITS;
  usage[1] = user;
  return 0;
}

/* A bare rts where the primary has stack_usage_report; nothing calls
 * it. */
void installer_stack_usage_report(void) {}

/* Sets a task's priority, which the kernel changes by a step from the
 * present one, and returns the priority it had. */
uint8_t installer_task_set_priority(uint32_t task, uint8_t priority) {
  uint8_t old;

  installer_psos_priority(task, 0, &old);
  installer_psos_priority(task, priority - old, &old);
  return old;
}

/* Spins 729 empty loops for each count, the count a word once it is not
 * 0. */
void installer_busy_wait(uint32_t count) { os_busy_wait(count); }

/* Pauses a number of seconds, as kernel ticks, at least one; a negative
 * time pauses a tick, and one past 2^21 seconds that long. */
void installer_sleep_seconds(float seconds) {
  if (installer_float_compare(0.0f, seconds) > 0) {
    seconds = 1.0f / OS_TICKS_PER_SECOND;
  } else if (installer_float_compare(seconds, OS_SLEEP_LONGEST) > 0) {
    seconds = OS_SLEEP_LONGEST;
  }
  uint32_t ticks = installer_float_to_unsigned(
      installer_float_multiply(seconds, OS_TICKS_PER_SECOND));

  installer_psos_pause(ticks == 0 ? 1 : ticks);
}
