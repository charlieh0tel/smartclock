/* The operating system, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `platform/os`).  The exported C includes this file after its own, so
 * the types and prototypes it declares are in scope; each function here
 * keeps its generated prototype.  It is the kernel's configuration and
 * its binding, a stub a service (docs/firmware/psos.md, "Calls" and
 * "Services"); the mutexes, lists and memory built on it; the exception
 * hook; the kernel's own records, as the reports walk them; the process,
 * exchange and region reports; and the stacks' use.  The task, exchange,
 * segment and data area records below are the kernel's
 * (docs/firmware/structures.md, "The kernel's configuration").
 * psos_config_init, changed in the 58503A, and task_name and
 * object_name, which only the Z3805A writes by hand, are the Z3805A's
 * own (its rewritten/os.c). */

#include <stddef.h>

/* A whole, as the reports' percentages take it. */
static const uint32_t OS_PERCENT = 100;

/* The binding's stubs (psos_create up).  Each loads the registers its
 * service reads and makes it through psos_call, in one asm statement
 * (os.h, os_psos_call), whose D7 is 0 or the service in the high byte
 * and its error in the low, and stores from the registers it returns,
 * on success, Z set, as the image's stubs branch on it.  A register a
 * service does not read is passed as 0.  psos_pack_name packs the name
 * the stub was given, its caller's first argument. */

/* The kernel's data area. */
static psos_kernel* os_kernel_data(void) { return psos_config.data_area; }

/* The kernel's version, which service 0 returns in D7 where the other
 * services return their error: 0x3341 (docs/firmware/psos.md); nothing
 * calls it. */
uint16_t psos_version(void) {
  return os_psos_call(PSOS_SERVICE_VERSION, (os_registers){0}).error;
}

/* Creates a task of a name and priority, its supervisor and user
 * stacks' sizes in words packed in D3, a word each; on success its id. */
uint32_t psos_create(char* name, uint32_t a, uint32_t priority,
                     uint32_t stack_a, uint32_t stack_b, void* e,
                     uint32_t* task) {
  const os_registers created = os_psos_call(
      PSOS_SERVICE_SPAWN, (os_registers){.d0 = OS_PACK_NAME("psos_pack_name"),
                                         .d1 = a,
                                         .d2 = priority,
                                         .d3 = os_stack_sizes(stack_a, stack_b),
                                         .a0 = e});

  if (!created.failed && task != NULL) {
    *task = (uint32_t)created.a0;
  }
  return created.error;
}

/* Starts a created task at its entry.  The image's stub
 * passes D0 to D3 on as its caller left them, but the kernel's start
 * reads only the task and the entry (kernel_start) and takes no mode: the
 * task starts in user mode (docs/firmware/psos.md, "Services"). */
uint32_t psos_start(uint32_t task, void* entry) {
  return os_psos_call(PSOS_SERVICE_START,
                      (os_registers){.a0 = (void*)task, .a1 = entry})
      .error;
}

/* Looks a task up by name; on success its id and a byte. */
uint32_t psos_ident(char* name, uint32_t* id, uint8_t* byte) {
  const os_registers found = os_psos_call(
      PSOS_SERVICE_IDENT, (os_registers){.d0 = OS_PACK_NAME("psos_pack_name")});

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

/* The running task's id, ident's for no name. */
uint32_t psos_my_pid(void) {
  return (uint32_t)os_psos_call(PSOS_SERVICE_IDENT, (os_registers){0}).a0;
}

/* Deletes a task. */
uint32_t psos_delete(uint32_t task) {
  return os_psos_call(PSOS_SERVICE_DELETE, (os_registers){.a0 = (void*)task})
      .error;
}

/* Suspends a task. */
uint32_t psos_suspend(uint32_t task) {
  return os_psos_call(PSOS_SERVICE_SUSPEND, (os_registers){.a0 = (void*)task})
      .error;
}

/* Resumes a task. */
uint32_t psos_resume(uint32_t task) {
  return os_psos_call(PSOS_SERVICE_RESUME, (os_registers){.a0 = (void*)task})
      .error;
}

/* Changes a task's priority by a step; its old priority, even on an
 * error. */
uint32_t psos_priority(uint32_t task, uint32_t priority, uint8_t* old) {
  const os_registers changed = os_psos_call(
      PSOS_SERVICE_PRIORITY, (os_registers){.d0 = priority, .a0 = (void*)task});

  if (old != NULL) {
    *old = changed.d0;
  }
  return changed.error;
}

/* Pauses the running task for some ticks. */
uint32_t psos_pause(uint32_t ticks) {
  return os_psos_call(PSOS_SERVICE_PAUSE, (os_registers){.d0 = ticks}).error;
}

/* Sets the masked bits of the running task's mode; the old mode, even on
 * an error. */
uint32_t psos_mode(uint32_t mode, uint32_t mask, uint8_t* old) {
  const os_registers changed =
      os_psos_call(PSOS_SERVICE_MODE, (os_registers){.d0 = mode, .d1 = mask});

  if (old != NULL) {
    *old = changed.d0;
  }
  return changed.error;
}

/* Allocates a segment of the size in D1 with the other three longs in
 * D2, D3 and D0; on success the segment. */
uint32_t psos_service_8(uint32_t size, uint32_t d2, uint32_t d3, uint32_t d0,
                        uint32_t* out) {
  const os_registers allocated =
      os_psos_call(PSOS_SERVICE_ALLOCATE_SEGMENT,
                   (os_registers){.d0 = d0, .d1 = size, .d2 = d2, .d3 = d3});

  if (!allocated.failed && out != NULL) {
    *out = (uint32_t)allocated.a0;
  }
  return allocated.error;
}

/* Frees a segment. */
uint32_t psos_free_segment(void* segment) {
  return os_psos_call(PSOS_SERVICE_FREE_SEGMENT, (os_registers){.a0 = segment})
      .error;
}

/* Gives a segment of the running task's to another task. */
uint32_t psos_assign_segment(void* segment, uint32_t task) {
  return os_psos_call(PSOS_SERVICE_ASSIGN_SEGMENT,
                      (os_registers){.a0 = segment, .a1 = (void*)task})
      .error;
}

/* Takes a segment back from a task. */
uint32_t psos_reclaim_segment(void* segment, uint32_t task) {
  return os_psos_call(PSOS_SERVICE_RECLAIM_SEGMENT,
                      (os_registers){.a0 = segment, .a1 = (void*)task})
      .error;
}

/* Makes a partition of fixed blocks in an area; its id, even on an
 * error. */
uint32_t psos_create_partition(void* area, uint32_t block_size, uint32_t count,
                               void** partition) {
  const os_registers created =
      os_psos_call(PSOS_SERVICE_CREATE_PARTITION,
                   (os_registers){.d0 = block_size, .d1 = count, .a0 = area});

  if (partition != NULL) {
    *partition = created.a0;
  }
  return created.error;
}

/* Deletes a partition. */
uint32_t psos_delete_partition(void* partition, uint32_t block_size) {
  return os_psos_call(PSOS_SERVICE_DELETE_PARTITION,
                      (os_registers){.d0 = block_size, .a0 = partition})
      .error;
}

/* Takes a block from a partition; on success the block. */
uint32_t psos_get_block(void* partition, uint32_t block_size, void** block) {
  const os_registers taken =
      os_psos_call(PSOS_SERVICE_GET_BLOCK,
                   (os_registers){.d0 = block_size, .a0 = partition});

  if (!taken.failed && block != NULL) {
    *block = taken.a1;
  }
  return taken.error;
}

/* Returns a block to its partition. */
uint32_t psos_return_block(void* partition, void* block) {
  return os_psos_call(PSOS_SERVICE_RETURN_BLOCK,
                      (os_registers){.a0 = partition, .a1 = block})
      .error;
}

/* Creates an exchange of a name, with two longs; on success its id. */
uint32_t psos_create_x(char* name, uint32_t a, uint32_t b, uint32_t* exchange) {
  const os_registers created = os_psos_call(
      PSOS_SERVICE_CREATE_X,
      (os_registers){.d0 = OS_PACK_NAME("psos_pack_name"), .d1 = a, .d2 = b});

  if (!created.failed && exchange != NULL) {
    *exchange = (uint32_t)created.a0;
  }
  return created.error;
}

/* Attaches to an exchange by name; on success three longs, A0's, D1's
 * and D2's. */
uint32_t psos_service_21(char* name, uint32_t* out1, uint32_t* out2,
                         uint32_t* out3) {
  const os_registers attached =
      os_psos_call(PSOS_SERVICE_ATTACH_X,
                   (os_registers){.d0 = OS_PACK_NAME("psos_pack_name")});

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
uint32_t psos_delete_x(uint32_t exchange) {
  return os_psos_call(PSOS_SERVICE_DELETE_X,
                      (os_registers){.a0 = (void*)exchange})
      .error;
}

/* Sends a message to an exchange. */
uint32_t psos_send_x(uint32_t exchange, void* message) {
  return os_psos_call(PSOS_SERVICE_SEND_X,
                      (os_registers){.a0 = (void*)exchange, .a1 = message})
      .error;
}

/* Sends a message to the front of an exchange's queue. */
uint32_t psos_jam_x(uint32_t exchange, void* message) {
  return os_psos_call(PSOS_SERVICE_JAM_X,
                      (os_registers){.a0 = (void*)exchange, .a1 = message})
      .error;
}

/* Gives a message to every task waiting at an exchange and queues
 * copies up to a count; on success the larger of the count and the
 * tasks given it, and those tasks. */
uint32_t psos_broadcast_x(uint32_t exchange, void* message, uint32_t count,
                          uint8_t* count_out, uint8_t* woken) {
  const os_registers sent = os_psos_call(
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

/* Receives a message from an exchange into a buffer, waiting, unless
 * no_wait, up to ticks, 0 without end. */
uint32_t psos_request_x(uint32_t exchange, void* buffer, uint32_t no_wait,
                        uint32_t ticks) {
  return os_psos_call(PSOS_SERVICE_REQUEST_X,
                      (os_registers){.d0 = ticks,
                                     .d1 = no_wait,
                                     .a0 = (void*)exchange,
                                     .a1 = buffer})
      .error;
}

/* Signals events to a variable's task. */
uint32_t psos_signal_v(uint32_t variable, uint32_t value) {
  return os_psos_call(PSOS_SERVICE_SIGNAL_V,
                      (os_registers){.d1 = value, .a0 = (void*)variable})
      .error;
}

/* Waits on a variable with the longs in D1, D2 and D0; D1 back, even on
 * an error. */
uint32_t psos_wait_v(uint32_t d1, uint32_t d2, uint32_t d0, uint32_t* out) {
  const os_registers waited = os_psos_call(
      PSOS_SERVICE_WAIT_V, (os_registers){.d0 = d0, .d1 = d1, .d2 = d2});

  if (out != NULL) {
    *out = waited.d1;
  }
  return waited.error;
}

/* Reads a variable with the longs in D1 and D2; D1 back, even on an
 * error. */
uint32_t psos_get_v(uint32_t d1, uint32_t d2, uint32_t* out) {
  const os_registers read =
      os_psos_call(PSOS_SERVICE_GET_V, (os_registers){.d1 = d1, .d2 = d2});

  if (out != NULL) {
    *out = read.d1;
  }
  return read.error;
}

/* Counts a tick. */
uint32_t psos_announce_tick(void) {
  return os_psos_call(PSOS_SERVICE_ANNOUNCE_TICK, (os_registers){0}).error;
}

/* The time of day: its date, time and ticks into the second, even on an
 * error. */
uint32_t psos_get_time(uint32_t* date, uint32_t* time, uint16_t* ticks) {
  const os_registers now =
      os_psos_call(PSOS_SERVICE_GET_TIME, (os_registers){0});

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
uint32_t psos_set_time(uint32_t date, uint32_t time, uint32_t ticks) {
  return os_psos_call(PSOS_SERVICE_SET_TIME,
                      (os_registers){.d0 = date, .d1 = time, .d2 = ticks})
      .error;
}

/* Initializes a device. */
uint32_t psos_io_init(uint32_t device) {
  return os_psos_io_call(PSOS_IO_INIT, (os_registers){.d0 = device}).error;
}

/* Opens a device with a long. */
uint32_t psos_io_open(uint32_t device, uint32_t a) {
  return os_psos_io_call(PSOS_IO_OPEN, (os_registers){.d0 = device, .d1 = a})
      .error;
}

/* Closes a device. */
uint32_t psos_io_close(uint32_t device) {
  return os_psos_io_call(PSOS_IO_CLOSE, (os_registers){.d0 = device}).error;
}

/* Reads up to a length from a device; on success the length read. */
uint32_t psos_io_read(uint32_t device, void* buffer, uint32_t* length) {
  const os_registers read = os_psos_io_call(
      PSOS_IO_READ,
      (os_registers){.d0 = device, .d2 = *length, .a0 = length, .a1 = buffer});

  if (!read.failed && length != NULL) {
    *length = read.d2;
  }
  return read.error;
}

/* Writes a length to a device; on success the length written. */
uint32_t psos_io_write(uint32_t device, void* buffer, uint32_t* length) {
  const os_registers written = os_psos_io_call(
      PSOS_IO_WRITE,
      (os_registers){.d0 = device, .d2 = *length, .a0 = buffer, .a1 = length});

  if (!written.failed && length != NULL) {
    *length = written.d2;
  }
  return written.error;
}

/* Controls a device with two longs. */
uint32_t psos_io_control(uint32_t device, uint32_t a, uint32_t b) {
  return os_psos_io_call(PSOS_IO_CONTROL,
                         (os_registers){.d0 = device, .d2 = a, .d3 = b})
      .error;
}

/* Packs the name and creates the mutex as an exchange of that name;
 * psos_pack_name reads the name as this function's caller's first
 * argument. */
uint32_t mutex_create(char* name, mutex* lock) {
  return mutex_init(OS_PACK_NAME("psos_pack_name"), lock);
}

/* mutex_acquire on its argument. */
uint32_t mutex_lock(mutex* lock) { return mutex_acquire(lock); }

/* mutex_release on its argument. */
uint32_t mutex_unlock(mutex* lock) { return mutex_release(lock); }

/* The task holding a mutex. */
uint32_t mutex_owner(void* lock) { return os_mutex_owner(lock); }

/* Pushes an entry on the front of a list. */
void list_push(klok_entry** list, klok_entry* entry) {
  entry->next = *list;
  *list = entry;
}

/* Unlinks an entry from a list; -2 when it is not on it. */
int32_t list_unlink(klok_entry** list, klok_entry* entry) {
  static const int32_t NOT_LISTED = -2;
  klok_entry** link = list;

  while (true) {
    if (*link == NULL) {
      return NOT_LISTED;
    }
    if (*link == entry) {
      break;
    }
    link = &(*link)->next;
  }
  *link = entry->next;
  return 0;
}

/* A segment of a size, from service 8; on an error whatever its local
 * held. */
void* memory_allocate(uint32_t size) {
  uint32_t segment;

  psos_service_8(size, 1, 1, 0, &segment);
  return (void*)segment;
}

/* Frees a segment memory_allocate gave. */
void memory_free(void* segment) { psos_free_segment(segment); }

/* The running task's id; nothing calls it. */
uint32_t current_task_id(void) { return psos_my_pid(); }

/* Clears the exception hook and its per-task switch. */
void exception_hook_reset(void) {
  exception_hook_per_task = 0;
  exception_hook = NULL;
}

/* Sets the per-task switch, so that exception_hook_get takes the
 * running task's hook first; 0.  Nothing calls it. */
int32_t exception_hook_enable_per_task(void) {
  exception_hook_per_task = OS_SWITCH_ON;
  return 0;
}

/* Sets the running task's own exception hook. */
int32_t exception_hook_set_task(void* hook) {
  ((psos_task*)psos_my_pid())->exception_hook = hook;
  return 0;
}

/* Sets the shared exception hook; OS_SWITCH_ON when one was set
 * before, else 0.  Nothing calls it. */
int32_t exception_hook_set(void* hook) {
  int32_t was_set = exception_hook != NULL ? OS_SWITCH_ON : 0;

  exception_hook = hook;
  return was_set;
}

/* Clears the running task's own exception hook; nothing calls it. */
int32_t exception_hook_clear_task(void) {
  return exception_hook_set_task(NULL);
}

/* The running task's exception hook while the per-task switch is set,
 * else, or when it has none, the shared one. */
void* exception_hook_get(void) {
  void* hook = NULL;

  if (exception_hook_per_task != 0) {
    hook = ((psos_task*)psos_my_pid())->exception_hook;
  }
  if (hook == NULL) {
    hook = exception_hook;
  }
  return hook;
}

/* The count of idle loops in the last second, which os_report_cpu
 * prints. */
uint32_t idle_count(void) { return idle_loops_last_second; }

/* Counts a tick into the running task's record. */
void task_count_tick(void) { os_kernel_data()->running->ticks++; }

/* Whether an id is an exchange's: within the data area, compared signed,
 * and its record's check its words swapped. */
uint32_t exchange_id_valid(uint32_t id) {
  if ((int32_t)id < (int32_t)psos_config.data_area ||
      (int32_t)psos_config.data_end < (int32_t)id) {
    return 0;
  }
  uint32_t check = ((psos_exchange*)id)->id_check;

  return (check << 16 | check >> 16) == id;
}

/* Whether an id is a task's: within the data area and its record's
 * check its low word complemented. */
uint32_t task_id_valid(uint32_t id) {
  static const uint32_t ID_CHECK_MASK = 0xffff;

  if (id < (uint32_t)psos_config.data_area || psos_config.data_end < id) {
    return 0;
  }
  return (((psos_task*)id)->id_check ^ ID_CHECK_MASK) == id;
}

/* Turns the kernel's debug output on, with the configuration's hook
 * table, or off, with interrupts masked. */
void psos_debug_output(char enable) {
  uint16_t sr = set_interrupt_mask(CPU_IPL_7);

  if (enable == 0) {
    psos_debug_enabled = 0;
    os_kernel_data()->debug_hooks = NULL;
  } else {
    void* hooks = NULL;

    psos_debug_enabled = 1;
    if (psos_config.debug_hooks != NULL) {
      hooks = *psos_config.debug_hooks;
    }
    os_kernel_data()->debug_hooks = hooks;
  }
  set_interrupt_mask(sr);
}

/* Whether a task is paused, by its delay or its status. */
static bool os_task_paused(psos_task* task) {
  return task->delay != 0 || (task->status & PSOS_TASK_PAUSED) != 0;
}

/* Fills task_table, the running task first and then the kernel's list,
 * with each one's state, and sums their ticks into task_ticks_total;
 * the number of tasks. */
int32_t task_table_build(void) {
  static const int32_t TABLE_ENTRIES = sizeof task_table / sizeof task_table[0];
  psos_kernel* kernel = os_kernel_data();

  for (int32_t entry = 0; entry < TABLE_ENTRIES; entry++) {
    task_table[entry].task = NULL;
    task_table[entry].state = PSOS_TABLE_EMPTY;
  }
  psos_task* running = kernel->running;

  task_table[0].state = PSOS_TABLE_READY;
  task_table[0].task = running;
  int32_t count = 1;

  for (psos_task* task = kernel->tasks; task != NULL; task = task->list_next) {
    if (task == running) {
      continue;
    }
    task_table[count].task = task;
    if (task->status == PSOS_TASK_READY) {
      task_table[count].state = PSOS_TABLE_READY;
    } else if (!os_task_paused(task)) {
      task_table[count].state = PSOS_TABLE_BLOCKED;
    } else {
      task_table[count].state = PSOS_TABLE_PAUSED;
    }
    count++;
  }
  task_ticks_total = 0;
  for (int32_t entry = 0; entry < count; entry++) {
    task_ticks_total += ((psos_task*)task_table[entry].task)->ticks;
  }
  return count;
}

/* A task's status as the process report's four characters. */
char* task_status_name(void* record) {
  psos_task* task = record;

  if (task->status & PSOS_TASK_SPAWNING) {
    return "spwn";
  }
  if (task->status & PSOS_TASK_MEMORY_WAIT) {
    return "mblk";
  }
  if (task->status & PSOS_TASK_EXCHANGE_WAIT) {
    return "xblk";
  }
  if (task->status & PSOS_TASK_VARIABLE_WAIT) {
    return "vblk";
  }
  if (os_task_paused(task)) {
    return "paus";
  }
  if (task->status & PSOS_TASK_SUSPENDED) {
    return "susp";
  }
  if (task->status == PSOS_TASK_READY) {
    return " RDY";
  }
  return " ?? ";
}

/* The segment of a task's that holds an address, from its start to its
 * size past it, or null. */
void* region_segment_containing(void* task, uint32_t address) {
  psos_segment* segment = ((psos_task*)task)->segments;

  for (; segment != NULL; segment = segment->next) {
    int32_t offset = address - (uint32_t)segment;

    if (offset >= 0 && (uint32_t)offset <= segment->size) {
      break;
    }
  }
  return segment;
}

/* A task's place on the ready queue from 0, or -1 when it is not on it. */
int32_t ready_queue_position(uint32_t* record) {
  psos_task* task = os_kernel_data()->ready;
  int32_t position = 0;

  while (task != NULL && task != (psos_task*)record) {
    position++;
    task = task->next;
  }
  if (task == NULL) {
    return -1;
  }
  return position;
}

/* The ticks until a paused task's delay ends, the delay list's ticks up
 * to and including it; 0 for a task not paused.  Like the image, it
 * reads the list's end, null, as a record too. */
int32_t timer_ticks_until(void* task) {
  psos_task* timer = os_kernel_data()->timers;
  int32_t ticks = 0;

  while (true) {
    if (os_task_paused(task)) {
      /* A copy the compiler cannot tie to timer, so that reading through
       * it, null or not, does not let it drop the test of timer below. */
      psos_task* record = timer;

      __asm__("" : "+r"(record));
      ticks += record->timer_ticks;
    }
    if (timer == NULL || timer == task) {
      break;
    }
    timer = timer->timer_next;
  }
  return ticks;
}

/* Rounds bytes to the nearest kilobyte, as the reports print them. */
static int32_t os_kilobytes(int32_t bytes) {
  static const int32_t KILOBYTE = 0x400;

  return (bytes + KILOBYTE / 2) / KILOBYTE;
}

/* Prints the idle count and the CPU it leaves free, 172 millionths of
 * the CPU a loop, rounded. */
void os_report_cpu(printf_function* print) {
  static const uint32_t MILLIONTHS_PER_LOOP = 172;
  static const uint32_t MILLION = 1000000;
  uint32_t idle = idle_count();

  print("Free CPU cycles: count 0x%08X, approx %2d%%\r\n", idle,
        (MILLION / 2 + idle * MILLIONTHS_PER_LOOP) / MILLION);
}

/* Prints the tasks, those free and the ticks they ran. */
void os_report_process_summary(printf_function* print) {
  /* Even, since gcc fills it by longs, misaligned at an odd address
   * (MC68331UM, 4.4.4). */
  char __attribute__((aligned(2))) format[] =
      "\t%d Process(s) (%d avail); Total time: %-d ticks.\r\n";
  int32_t free = 0;

  for (psos_task* task = os_kernel_data()->free_tasks; task != NULL;
       task = task->list_next) {
    free++;
  }
  print(format, task_count, free, task_ticks_total);
}

/* Prints a task's line of the process report, its resources and its
 * events; clears its ticks.  Its stack share and memory start from the
 * caller's D5 and D6, which it never sets on some paths: the stack share
 * is D5 when no segment holds the supervisor stack pointer, and the
 * memory adds the segments to D6 (docs/firmware/quirks.md, "Reads of
 * what nothing set"). */
void os_report_process(void* record, char* status, int32_t position,
                       uint32_t ticks, printf_function* print) {
  /* The registers as the caller left them, which the image reads. */
  uint32_t caller_d5 = CPU_CALLERS_REGISTER(uint32_t, "d5");
  int32_t caller_d6 = CPU_CALLERS_REGISTER(int32_t, "d6");
  psos_task* task = record;
  char* name = task_name(task);
  char place[16];
  char state[20];

  place[1] = '\0';
  if (position > 0) {
    sprintf(place, "%2d", position);
  } else if (position == 0) {
    place[0] = '*';
  } else {
    place[0] = '.';
  }
  int32_t memory = caller_d6;

  for (psos_segment* segment = task->segments; segment != NULL;
       segment = segment->next) {
    memory += segment->size;
  }
  uint32_t stack_share = caller_d5;
  uint32_t pointer = task->user_pointer;

  if (region_segment_containing(task, task->supervisor_pointer) != NULL) {
    uint32_t top = task->user_top;
    uint32_t size = task->user_top - task->supervisor_top - OS_USER_STACK_GAP;

    if (size == 0) {
      size = task->supervisor_top - task->stack_bottom;
      pointer = task->supervisor_pointer;
      top = task->supervisor_top;
    }
    stack_share = size == 0 ? 0 : (top - pointer) * OS_PERCENT / size;
  }
  uint32_t task_ticks = task->ticks;
  uint32_t cpu_share = 0;

  task->ticks = 0;
  if (task_ticks_total != 0) {
    cpu_share =
        (task_ticks_total / 2 + task_ticks * OS_PERCENT) / task_ticks_total;
  }
  libc_copy(state, status);
  if (task->mode & (PSOS_MODE_TIME_SLICE | PSOS_MODE_NO_PREEMPTION)) {
    strcat(state, "/");
  }
  if (task->mode & PSOS_MODE_TIME_SLICE) {
    strcat(state, "R");
  }
  if (task->mode & PSOS_MODE_NO_PREEMPTION) {
    strcat(state, "*");
  }
  print("$%08X %4s  %6s  %3d %2d  %2s %5d %4dkB %2d%% %2d%%\r\n", task, name,
        state, task->priority, task->group, place, ticks, os_kilobytes(memory),
        stack_share, cpu_share);
  psos_resource* resource = task->resources;

  if (resource != NULL) {
    print("\t   >> RESOURCES: ");
    while (resource != NULL) {
      print("%s", object_name(resource->object));
      resource = resource->next;
      if (resource != NULL) {
        print(",");
      }
    }
    print("%s", "\r\n");
  }
  if (task->events_waited != 0 || task->events_pending != 0) {
    print("\t   >> EVENTS: W(0x%04x) S(0x%04x)\r\n", task->events_waited,
          task->events_pending);
  }
}

/* Prints the process report: the running task, then the others ready,
 * paused and blocked, each group after a blank line; D0 as the loop's
 * last test leaves it, 1. */
int32_t os_report_processes(printf_function* print) {
  psos_task* running = task_table[0].task;
  int32_t state = PSOS_TABLE_READY;

  print("%s", "    pid   PNAME  STAT/M PRI GID POS  TIX MEMORY STK CPU\r\n");
  os_report_process(running, " RUN", 0, running->slice_ticks, print);
  print("%s", "\r\n");
  for (; state <= PSOS_TABLE_BLOCKED; state++) {
    bool listed = false;

    for (int32_t entry = 1; entry < task_count; entry++) {
      if ((int8_t)task_table[entry].state != state) {
        continue;
      }
      psos_task* task = task_table[entry].task;
      int32_t position = ready_queue_position((uint32_t*)task);
      uint32_t ticks = state == PSOS_TABLE_PAUSED ? timer_ticks_until(task)
                                                  : task->slice_ticks;

      os_report_process(task, task_status_name(task), position, ticks, print);
      listed = true;
    }
    if (listed) {
      print("%s", "\r\n");
    }
  }
  return state - PSOS_TABLE_BLOCKED;
}

/* Prints an exchange's line of the exchange report: its id, name,
 * queueing, access, limit and queue, and the tasks waiting. */
void os_report_exchange(uint32_t* id, printf_function* print) {
  psos_exchange* exchange = (psos_exchange*)id;
  char* queueing = exchange->by_priority == 0 ? "fifo" : "prio";
  char* access = exchange->group_only == 0 ? "any" : "grp";
  char limit[40];

  if (exchange->limited == 0) {
    libc_copy(limit, "inf");
  } else {
    sprintf(limit, "%d", exchange->queue_limit);
  }
  print("0x%07x  %4s  %4s %4s  %3s    %d    ", exchange, object_name(exchange),
        queueing, access, limit, exchange->queued);
  /* The queue ends at the exchange, whose first field is its head. */
  psos_task* end = (psos_task*)exchange;

  for (psos_task* task = exchange->waiting; task != end;) {
    print("%s", task_name(task));
    task = task->next;
    if (task != end) {
      print(",");
    }
  }
  print("%s", "\r\n");
}

/* Prints the exchange report, an exchange a line. */
void os_report_exchanges(printf_function* print) {
  /* Even, as os_report_process_summary's format. */
  char __attribute__((aligned(2))) header[] =
      "    xid   XNAME  TYPE  ACC  maxQ  Qlen BLOCKED\r\n";

  print("%s", header);
  for (psos_exchange* exchange = os_kernel_data()->exchanges; exchange != NULL;
       exchange = exchange->next) {
    os_report_exchange((uint32_t*)exchange, print);
  }
}

/* Prints the exchanges and message buffers, used and free. */
void os_report_exchange_summary(printf_function* print) {
  psos_kernel* kernel = os_kernel_data();
  int32_t exchanges = 0;
  int32_t free_exchanges = 0;
  int32_t free_messages = 0;

  for (psos_exchange* exchange = kernel->exchanges; exchange != NULL;
       exchange = exchange->next) {
    exchanges++;
  }
  for (psos_exchange* exchange = kernel->free_exchanges; exchange != NULL;
       exchange = exchange->next) {
    free_exchanges++;
  }
  for (psos_message* message = kernel->free_messages; message != NULL;
       message = message->next) {
    free_messages++;
  }
  int32_t messages = (int16_t)psos_config.message_buffers;

  print("\t%d Exchange(s) (%d avail).\r\n", exchanges, free_exchanges);
  print("\t%d Msg buffer(s) (%d avail).\r\n", messages - free_messages,
        free_messages);
}

/* The two regions os_report_regions prints. */
#define OS_REGIONS 2

/* Prints the region report: each region's bounds and limits, the memory
 * the tasks hold and that is free, and the tasks waiting for memory.  A
 * segment's region indexes the sums unchecked, as in the image. */
void os_report_regions(printf_function* print) {
  /* A waiter's region is its record's bits 3 to 0, less 1. */
  static const uint32_t REGION_WAIT_MASK = 0xf;
  static const uint16_t SEGMENT_REGION_MASK = 3;
  psos_kernel* kernel = os_kernel_data();
  int32_t held_count[OS_REGIONS] = {0, 0};
  int32_t free_bytes[OS_REGIONS];
  int32_t held_bytes[OS_REGIONS] = {0, 0};
  void* start[OS_REGIONS] = {psos_config.data_area,
                             (void*)psos_config.region_2_start};

  print(
      "%s",
      "Region strt       end  min     max    pend   USED  cnt  AVAIL cnt\r\n");
  for (int32_t entry = 0; entry < task_count; entry++) {
    for (psos_segment* segment = ((psos_task*)task_table[entry].task)->segments;
         segment != NULL; segment = segment->next) {
      int32_t region = (segment->flags & SEGMENT_REGION_MASK) - 1;

      held_bytes[region] += segment->size;
      held_count[region]++;
    }
  }
  for (int32_t region = 0; region < OS_REGIONS; region++) {
    int32_t free_count = 0;
    uint32_t end = 0;
    uint32_t minimum = 0;
    uint32_t maximum = 0;
    uint32_t pending = 0;

    free_bytes[region] = 0;
    for (psos_segment* segment = kernel->region_free[region]; segment != NULL;
         segment = segment->next) {
      free_bytes[region] += segment->size;
      free_count++;
    }
    if (start[region] != NULL) {
      end = kernel->region_end[region];
      minimum = kernel->region_minimum[region];
      maximum = kernel->region_maximum[region];
      pending = kernel->region_pending[region];
    }
    print("%1d 0x%08x 0x%08x %4d %8d %8d %4dkB %4d %4dkB %3d\r\n", region + 1,
          start[region], end, minimum, maximum, pending,
          os_kilobytes(held_bytes[region]), held_count[region],
          os_kilobytes(free_bytes[region]), free_count);
  }
  /* The queue ends at its head's field. */
  psos_task* end = (psos_task*)&kernel->region_waiting;
  psos_task* task = kernel->region_waiting;

  if (task != end) {
    print("   >> BLOCKED: ");
    while (task != end) {
      int8_t region = (task->region_wait & REGION_WAIT_MASK) + 1;

      print("%s %d(%d)", task_name(task), task->region_request, region);
      task = task->next;
      if (task != end) {
        print(",");
      }
    }
    print("%s", "\r\n");
  }
  print("%s", "\r\n");
}

/* Prints a segment's line: a name, its address, size and first three
 * longs, and its flags. */
void os_report_region_line(char* name, uint32_t* record,
                           printf_function* print) {
  psos_segment* segment = (psos_segment*)record;

  print("%4s 0x%08x %7d   0x%08x  0x%08x  0x%08x 0x%04x\r\n", name, segment,
        segment->size, segment->next, segment->field_04, segment->field_08,
        segment->flags);
}

/* The rule that opens and closes the reports. */
static char* const os_report_rule =
    "=================================================================\r\n";

/* :DIAG:OS:PROC?'s report: the CPU, the tasks, the exchanges and the
 * regions; the source is unused. */
void os_process_report(request_source source) {
  printf("%s", "\r\n");
  printf("%s", os_report_rule);
  printf("%s", "\r\n");
  os_report_cpu((printf_function*)printf);
  task_count = task_table_build();
  printf("%s", "\r\n");
  os_report_processes((printf_function*)printf);
  os_report_process_summary((printf_function*)printf);
  printf("%s", "\r\n");
  os_report_exchanges((printf_function*)printf);
  os_report_exchange_summary((printf_function*)printf);
  printf("%s", "\r\n");
  os_report_regions((printf_function*)printf);
  printf("%s", os_report_rule);
}

/* :DIAG:OS:MEM?'s report: each task's segments, each region's free
 * segments, then the regions; the source is unused. */
void os_memory_report(request_source source) {
  printf("%s", "\r\n");
  printf("%s", os_report_rule);
  task_count = task_table_build();
  printf("%s", "\r\n");
  printf("%s",
         "Process  Memseg size(bytes)     next        last    neighbor   "
         "key\r\n");
  for (int32_t entry = 0; entry < task_count; entry++) {
    psos_task* task = task_table[entry].task;

    for (psos_segment* segment = task->segments; segment != NULL;
         segment = segment->next) {
      os_report_region_line(task_name(task), (uint32_t*)segment,
                            (printf_function*)printf);
    }
  }
  for (int32_t region = 0; region < OS_REGIONS; region++) {
    psos_segment* segment = os_kernel_data()->region_free[region];

    if (segment != NULL) {
      printf(">> Free memory - region: %d\r\n", region);
    }
    for (; segment != NULL; segment = segment->next) {
      os_report_region_line("****", (uint32_t*)segment,
                            (printf_function*)printf);
    }
  }
  printf("%s", "\r\n");
  os_report_regions((printf_function*)printf);
  printf("%s", os_report_rule);
}

/* stack_measure's user stack's use and size, then its supervisor
 * stack's: array sizes, so #define. */
#define OS_STACK_USAGE_LONGS 4

#define OS_STACK_MESSAGE_SIZE 80

/* A task's user stack's use and size, then its supervisor stack's, as
 * stack_usage measures them, its used bytes in D0's half of the result
 * and the size in D1's. */
int32_t stack_measure(void* record, uint32_t* usage) {
  psos_task* task = record;
  uint64_t supervisor =
      stack_usage((uint16_t*)task->stack_bottom, (char*)task->supervisor_top);

  usage[2] = supervisor >> CPU_LONG_BITS;
  usage[3] = supervisor;
  uint64_t user =
      stack_usage((uint16_t*)(task->supervisor_top + OS_USER_STACK_GAP),
                  (char*)task->user_top);

  usage[0] = user >> CPU_LONG_BITS;
  usage[1] = user;
  return 0;
}

/* Prints a task's user and supervisor stacks' use and sizes. */
void stack_usage_report(void* task) {
  uint32_t usage[OS_STACK_USAGE_LONGS];
  char message[OS_STACK_MESSAGE_SIZE];

  if (stack_measure(task, usage) != 0) {
    return;
  }
  sprintf(message, "User: %5u of %5u b   Supv: %5u of %5u b\r\n", usage[0],
          usage[1], usage[2], usage[3]);
  task_debug_message(message);
}

/* A task's stacks for task_stack_report: each one's top, saved pointer
 * and size.  With no supervisor stack it takes the user stack's top and
 * a segment's size from two locals it never sets, which are volatile so
 * that they are read from the frame as the image reads them
 * (docs/firmware/quirks.md, "Reads of what nothing set"). */
void task_stack_info(void* record, uint32_t* user_top, uint32_t* user_pointer,
                     uint32_t* user_size, uint32_t* supervisor_top,
                     uint32_t* supervisor_pointer, uint32_t* supervisor_size) {
  /* The image's user stack is 4 bytes short of the gap, and a segment's
   * use 0x12 short of its size. */
  static const uint32_t SEGMENT_OVERHEAD = 0x12;
  psos_task* task = record;
  volatile uint32_t unset_top;
  psos_segment* volatile unset_segment;

  *user_pointer = task->user_pointer;
  *supervisor_pointer = task->supervisor_pointer;
  if (task->supervisor_top != task->stack_bottom) {
    *supervisor_top = task->supervisor_top;
    *supervisor_size = task->supervisor_top - task->stack_bottom;
    *user_top = task->user_top;
    *user_size = task->user_top - task->supervisor_top - OS_USER_STACK_GAP;
    return;
  }
  task_debug_message("no system stack found\r\n");
  *supervisor_pointer = 0;
  *supervisor_top = 0;
  *supervisor_size = 0;
  *user_pointer = task->supervisor_pointer;
  *user_top = unset_top;
  *user_size = unset_segment->size - SEGMENT_OVERHEAD;
}

/* A percentage as the stack report computes it: of a stack's size, the
 * bytes from a word to its top. */
static uint16_t os_stack_percent(uint32_t top, uint32_t at, uint32_t size) {
  return (top - at) * OS_PERCENT / size;
}

/* A task stack's use for the stack report, of a size that is not 0: up
 * from its bottom, top - size, while below its saved pointer, the first
 * word after one that is not the 0xabcd fill, as a percentage of its
 * size from there to the top; 0 if it finds none (task_stack_report+0x9c to
 * task_stack_report+0xd0). The word at the bottom is never tested. */
static uint16_t os_stack_use(uint32_t top, uint32_t size, uint32_t pointer) {
  static const uint16_t UNUSED = 0xabcd;
  const uint16_t* word = (const uint16_t*)(top - size);

  while ((uint32_t)word < pointer) {
    word++;
    if (*word != UNUSED) {
      return os_stack_percent(top, (uint32_t)word, size);
    }
  }
  return 0;
}

/* :DIAG:OS:STAC?'s report: each task's user and supervisor stacks, their
 * saved pointers, sizes, use and tops, then the kernel's own supervisor
 * stack; the source is unused.  The kernel's stack's use is
 * the last printed task's supervisor use when its words are all zero,
 * and, when no task was printed either, D2 as the caller left it
 * (task_stack_report+0x1c2; docs/firmware/quirks.md, "Reads of what nothing
 * set"). */
void task_stack_report(request_source source) {
  uint16_t supervisor_use = CPU_CALLERS_REGISTER(uint32_t, "d2");
  /* The kernel's supervisor stack starts this far into its data area
   * (structures.md, "The tasks' stacks"). */
  static const uint32_t SYSTEM_STACK_OFFSET = 200;
  static char* const ROW = "$%08X %4s $%08X $%-5X %3u%% $%08X $%-5X %3u%%\r\n";
  static char* const TOPS = "               $%08X (TOS)       $%08X (TOS)\r\n";

  printf("%s", "\r\n");
  printf("%s", os_report_rule);
  printf("%s", "\r\n");
  task_count = task_table_build();
  printf("%s", "    pid   name     USP   size            SSP   size\r\n");
  for (int16_t index = 0; index < task_count; index++) {
    psos_task* task = task_table[index].task;
    uint32_t user_top;
    uint32_t user_pointer;
    uint32_t user_size;
    uint32_t supervisor_top;
    uint32_t supervisor_pointer;
    uint32_t supervisor_size;

    task_stack_info(task, &user_top, &user_pointer, &user_size, &supervisor_top,
                    &supervisor_pointer, &supervisor_size);
    if (user_size == 0 && supervisor_size == 0) {
      continue;
    }
    const uint16_t user_use =
        user_size != 0 ? os_stack_use(user_top, user_size, user_pointer) : 0;
    supervisor_use =
        supervisor_size != 0
            ? os_stack_use(supervisor_top, supervisor_size, supervisor_pointer)
            : 0;
    /* task_stack_info's user size leaves out the gap, which the report
     * puts back. */
    printf(ROW, (uint32_t)task, task_name(task), user_pointer,
           user_size + OS_USER_STACK_GAP, (uint32_t)user_use,
           supervisor_pointer, supervisor_size, (uint32_t)supervisor_use);
    printf(TOPS, user_top, supervisor_top);
  }

  const uint32_t system_size = psos_config.supervisor_stack_size;

  if (system_size != 0) {
    const uint32_t bottom =
        (uint32_t)psos_config.data_area + SYSTEM_STACK_OFFSET;
    const uint32_t top = bottom + system_size;

    /* Up from the bottom to the first word in use, its use the rest. */
    for (const uint16_t* word = (const uint16_t*)bottom; (uint32_t)word < top;
         word++) {
      if (*word != 0) {
        supervisor_use =
            OS_PERCENT - os_stack_percent((uint32_t)word, bottom, system_size);
        break;
      }
    }
    printf("\r\n>> pSOS system supervisor stack:\r\n");
    printf(ROW, 0, "pSOS", 0, 0, 0, bottom, system_size,
           (uint32_t)supervisor_use);
    printf(TOPS, 0, top);
  }
  printf("%s", "\r\n");
  printf("%s", "\r\n");
  printf("%s", os_report_rule);
}

/* Sets a task's priority, which the kernel changes by a step from the
 * present one, and returns the priority it had. */
uint8_t task_set_priority(uint32_t task, uint8_t priority) {
  uint8_t old;

  psos_priority(task, 0, &old);
  psos_priority(task, priority - old, &old);
  return old;
}

/* Spins 729 empty loops for each count, the count a word once it is not
 * 0. */
void busy_wait(uint32_t count) { os_busy_wait(count); }

/* Pauses a number of seconds, as kernel ticks, at least one; a negative
 * time pauses a tick, and one past 2^21 seconds that long; nothing calls
 * it. */
void sleep_seconds(float seconds) {
  if (float_compare(0.0f, seconds) > 0) {
    seconds = 1.0f / OS_TICKS_PER_SECOND;
  } else if (float_compare(seconds, OS_SLEEP_LONGEST) > 0) {
    seconds = OS_SLEEP_LONGEST;
  }
  uint32_t ticks =
      float_to_unsigned(float_multiply(seconds, OS_TICKS_PER_SECOND));

  psos_pause(ticks == 0 ? 1 : ticks);
}
