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
 * (docs/firmware/structures.md, "The kernel's configuration"). */

#include <stddef.h>

typedef struct os_task os_task;

/* A segment of a region, on a task's list or a region's free list:
 * os_report_region_line prints its first five fields. */
typedef struct os_segment {
  struct os_segment* next; /* +0x00 */
  uint32_t field_04;
  uint32_t field_08;
  uint32_t size;  /* +0x0c: bytes, from the segment's start */
  uint16_t flags; /* +0x10: the region, 1 or 2, in bits 1 to 0 */
} os_segment;

/* One of a task's resources, on its list at +0x1c. */
typedef struct os_resource {
  struct os_resource* next; /* +0x00 */
  void* object;             /* +0x04: an exchange; object_name names it */
} os_resource;

/* A task's record, 0xb2 bytes, which kernel_init (0x15cd8) carves from
 * the data area; its address is its id. */
struct os_task {
  os_task* next; /* +0x00: the next on the ready queue, or on the circular
                    queue of an exchange or region it waits at */
  uint32_t field_04;
  os_task* timer_next; /* +0x08: the next on the delay list */
  uint32_t field_0c;
  uint8_t priority; /* +0x10 */
  uint8_t mode;     /* +0x11: os_task_mode's bits, service 32's mode byte */
  uint8_t delay;    /* +0x12: nonzero while paused */
  uint8_t status;   /* +0x13: os_task_status's bits, 0 when ready */
  uint8_t group;    /* +0x14 */
  uint8_t field_15;
  uint16_t events_waited;  /* +0x16 */
  uint16_t events_pending; /* +0x18 */
  uint16_t slice_ticks;    /* +0x1a: the report's TIX for a task not paused */
  os_resource* resources;  /* +0x1c */
  os_segment* segments;    /* +0x20: the segments it holds */
  char name[4];            /* +0x24 */
  uint32_t id_check;       /* +0x28: the id with its low word complemented */
  uint32_t field_2c;
  os_task* list_next; /* +0x30: the next on the kernel's task list or its
                         free list */
  uint16_t field_34;
  uint32_t supervisor_pointer; /* +0x36: the saved SSP */
  uint32_t user_pointer;       /* +0x3a: the saved USP */
  uint8_t field_3e[0xc];
  int32_t timer_ticks; /* +0x4a: its ticks past the delay list's one before */
  uint32_t field_4e;
  uint32_t region_request; /* +0x52: the bytes a region waiter asks */
  uint32_t region_wait;    /* +0x56: bits 3 to 0, its region less 1 */
  uint8_t field_5a[0x28];
  void* exception_hook; /* +0x82: exception_hook_get's, per task */
  uint32_t field_86;
  uint32_t user_top;       /* +0x8a: the user stack's top */
  uint32_t supervisor_top; /* +0x8e: the supervisor stack's top; the user
                              stack starts 4 above it */
  uint32_t stack_bottom;   /* +0x92: the supervisor stack's bottom */
  uint8_t field_96[0x18];
  uint32_t ticks; /* +0xae: ticks run since the last process report */
};

/* An exchange's record, 0x22 bytes; its address is its id. */
typedef struct os_exchange {
  os_task* waiting; /* +0x00: the circular queue of tasks waiting, which
                       ends at the exchange itself */
  uint8_t field_04[0xc];
  uint8_t by_priority; /* +0x10: queues by priority, else first in first out */
  uint8_t group_only;  /* +0x11: only its group may use it, else any */
  uint8_t limited;     /* +0x12: queue_limit holds, else no limit */
  uint8_t field_13;
  char name[4];             /* +0x14 */
  uint32_t id_check;        /* +0x18: the id with its words swapped */
  struct os_exchange* next; /* +0x1c: the next on the kernel's list */
  uint8_t queue_limit;      /* +0x20 */
  uint8_t queued;           /* +0x21: messages queued */
} os_exchange;

/* A message buffer, 0x18 bytes, on the free list by its first long. */
typedef struct os_message {
  struct os_message* next; /* +0x00 */
} os_message;

/* The kernel's data area, psos_config.data_area, which it keeps in A6;
 * the regions' arrays are region 1's and region 2's. */
typedef struct os_kernel {
  os_task* running;            /* +0x00 */
  os_task* ready;              /* +0x04: the ready queue, by priority */
  os_task* timers;             /* +0x08: the delay list, through timer_next */
  os_task* tasks;              /* +0x0c: every task, through list_next */
  os_task* free_tasks;         /* +0x10 */
  os_exchange* exchanges;      /* +0x14 */
  os_exchange* free_exchanges; /* +0x18 */
  os_message* free_messages;   /* +0x1c */
  uint8_t field_20[0x10];
  void* debug_hooks;          /* +0x30: psos_debug_output's table or 0 */
  uint32_t region_minimum[2]; /* +0x34: os_report_regions' "min" */
  uint32_t region_maximum[2]; /* +0x3c: "max" */
  uint32_t region_pending[2]; /* +0x44: "pend" */
  uint32_t region_end[2];     /* +0x4c */
  os_segment* region_free[2]; /* +0x54 */
  uint8_t field_5c[8];
  os_task* region_waiting; /* +0x64: the circular queue of tasks waiting for a
                              region, which ends at this field */
} os_kernel;

_Static_assert(offsetof(os_task, slice_ticks) == 0x1a, "os_task");
_Static_assert(offsetof(os_task, list_next) == 0x30, "os_task");
_Static_assert(offsetof(os_task, timer_ticks) == 0x4a, "os_task");
_Static_assert(offsetof(os_task, exception_hook) == 0x82, "os_task");
_Static_assert(offsetof(os_task, ticks) == 0xae, "os_task");
_Static_assert(sizeof(os_task) == 0xb2, "os_task");
_Static_assert(sizeof(os_exchange) == 0x22, "os_exchange");
_Static_assert(offsetof(os_kernel, region_waiting) == 0x64, "os_kernel");

/* A task record's status byte, +0x13, by task_status_name's text. */
enum os_task_status {
  OS_TASK_SUSPENDED = 0x04,     /* "susp" */
  OS_TASK_PAUSED = 0x08,        /* "paus", as a nonzero delay is */
  OS_TASK_VARIABLE_WAIT = 0x10, /* "vblk": in wait_v */
  OS_TASK_EXCHANGE_WAIT = 0x20, /* "xblk": in request_x */
  OS_TASK_MEMORY_WAIT = 0x40,   /* "mblk": on a region's queue, presumably */
  OS_TASK_SPAWNING = 0x80,      /* "spwn": created, not started */
};

/* A task record's mode byte, +0x11 (psos.md, service 32). */
enum os_task_mode {
  OS_MODE_TIME_SLICE = 0x40,    /* the report's "R" */
  OS_MODE_NO_PREEMPTION = 0x80, /* the report's "*" */
};

/* task_table_entry.state, by which os_report_processes groups the
 * tasks; OS_TABLE_EMPTY marks an unused entry. */
enum os_table_state {
  OS_TABLE_READY = 0,
  OS_TABLE_PAUSED = 1,
  OS_TABLE_BLOCKED = 2,
  OS_TABLE_EMPTY = 0xff,
};

/* The kernel's services, trap #3's D7 (psos.md, "Services"); 8 and 21
 * are named by the pSOS-68K brochure's list there. */
enum os_service {
  OS_SERVICE_START = 2,
  OS_SERVICE_IDENT = 3,
  OS_SERVICE_DELETE = 4,
  OS_SERVICE_SUSPEND = 5,
  OS_SERVICE_RESUME = 6,
  OS_SERVICE_PRIORITY = 7,
  OS_SERVICE_ALLOCATE_SEGMENT = 8,
  OS_SERVICE_FREE_SEGMENT = 9,
  OS_SERVICE_ASSIGN_SEGMENT = 10,
  OS_SERVICE_RECLAIM_SEGMENT = 16,
  OS_SERVICE_BROADCAST_X = 18,
  OS_SERVICE_JAM_X = 19,
  OS_SERVICE_CREATE_X = 20,
  OS_SERVICE_ATTACH_X = 21,
  OS_SERVICE_DELETE_X = 22,
  OS_SERVICE_SEND_X = 23,
  OS_SERVICE_REQUEST_X = 24,
  OS_SERVICE_SIGNAL_V = 25,
  OS_SERVICE_WAIT_V = 26,
  OS_SERVICE_GET_V = 27,
  OS_SERVICE_PAUSE = 28,
  OS_SERVICE_ANNOUNCE_TICK = 29,
  OS_SERVICE_GET_TIME = 30,
  OS_SERVICE_SET_TIME = 31,
  OS_SERVICE_MODE = 32,
  OS_SERVICE_CREATE_PARTITION = 35,
  OS_SERVICE_DELETE_PARTITION = 36,
  OS_SERVICE_GET_BLOCK = 37,
  OS_SERVICE_RETURN_BLOCK = 38,
};

/* The I/O functions, trap #4's D7 (psos.md, "Calls"). */
enum os_io_function {
  OS_IO_INIT = 0,
  OS_IO_OPEN = 1,
  OS_IO_READ = 3,
  OS_IO_WRITE = 4,
};

/* The kernel's data area. */
static os_kernel* os_kernel_data(void) { return psos_config.data_area; }

/* Copies the kernel's configuration and I/O table from ROM
 * (0x51038). */
void psos_config_init(void) {
  memcpy(&psos_config, &psos_config_defaults, sizeof psos_config);
  memcpy(io_table, io_table_defaults, sizeof io_table);
}

/* The binding's stubs (0x16f74 up).  Each loads the registers its
 * service reads, makes it through psos_call, whose result is 0 or the
 * service in the high byte and its error in the low, and stores the
 * registers it returns, which psos_call's bridge keeps in
 * __ghidra_result_d0 to __ghidra_result_a1.  A register a service does
 * not read is passed as 0.  psos_pack_name packs the name the stub was
 * given, its caller's first argument. */

/* Starts a created task at its entry (0x16fae). */
uint32_t psos_start(uint32_t task, void* entry) {
  return psos_call(OS_SERVICE_START, 0, 0, 0, 0, (void*)task, entry);
}

/* Looks a task up by name; on success its id and a byte (0x16fca). */
uint32_t psos_ident(char* name, uint32_t* id, uint8_t* byte) {
  uint32_t error =
      psos_call(OS_SERVICE_IDENT, psos_pack_name(), 0, 0, 0, NULL, NULL);

  if (error == 0) {
    if (id != NULL) {
      *id = __ghidra_result_a0;
    }
    if (byte != NULL) {
      *byte = __ghidra_result_d0;
    }
  }
  return error;
}

/* The running task's id, ident's for no name (0x16ff8). */
uint32_t psos_my_pid(void) {
  psos_call(OS_SERVICE_IDENT, 0, 0, 0, 0, NULL, NULL);
  return __ghidra_result_a0;
}

/* Deletes a task (0x17008). */
uint32_t psos_delete(uint32_t task) {
  return psos_call(OS_SERVICE_DELETE, 0, 0, 0, 0, (void*)task, NULL);
}

/* Suspends a task (0x17020). */
uint32_t psos_suspend(uint32_t task) {
  return psos_call(OS_SERVICE_SUSPEND, 0, 0, 0, 0, (void*)task, NULL);
}

/* Resumes a task (0x17038). */
uint32_t psos_resume(uint32_t task) {
  return psos_call(OS_SERVICE_RESUME, 0, 0, 0, 0, (void*)task, NULL);
}

/* Changes a task's priority by a step; its old priority, even on an
 * error (0x17050). */
uint32_t psos_priority(uint32_t task, uint32_t priority, uint8_t* old) {
  uint32_t error =
      psos_call(OS_SERVICE_PRIORITY, priority, 0, 0, 0, (void*)task, NULL);

  if (old != NULL) {
    *old = __ghidra_result_d0;
  }
  return error;
}

/* Pauses the running task for some ticks (0x17076). */
uint32_t psos_pause(uint32_t ticks) {
  return psos_call(OS_SERVICE_PAUSE, ticks, 0, 0, 0, NULL, NULL);
}

/* Sets the masked bits of the running task's mode; the old mode, even on
 * an error (0x1708e). */
uint32_t psos_mode(uint32_t mode, uint32_t mask, uint8_t* old) {
  uint32_t error = psos_call(OS_SERVICE_MODE, mode, mask, 0, 0, NULL, NULL);

  if (old != NULL) {
    *old = __ghidra_result_d0;
  }
  return error;
}

/* Allocates a segment of the size in D1 with the other three longs in
 * D2, D3 and D0; on success the segment (0x170c0). */
uint32_t psos_service_8(uint32_t size, uint32_t d2, uint32_t d3, uint32_t d0,
                        uint32_t* out) {
  uint32_t error =
      psos_call(OS_SERVICE_ALLOCATE_SEGMENT, d0, size, d2, d3, NULL, NULL);

  if (error == 0 && out != NULL) {
    *out = __ghidra_result_a0;
  }
  return error;
}

/* Frees a segment (0x170f4). */
uint32_t psos_free_segment(void* segment) {
  return psos_call(OS_SERVICE_FREE_SEGMENT, 0, 0, 0, 0, segment, NULL);
}

/* Gives a segment of the running task's to another task (0x1710c). */
uint32_t psos_assign_segment(void* segment, uint32_t task) {
  return psos_call(OS_SERVICE_ASSIGN_SEGMENT, 0, 0, 0, 0, segment, (void*)task);
}

/* Takes a segment back from a task (0x17128). */
uint32_t psos_reclaim_segment(void* segment, uint32_t task) {
  return psos_call(OS_SERVICE_RECLAIM_SEGMENT, 0, 0, 0, 0, segment,
                   (void*)task);
}

/* Makes a partition of fixed blocks in an area; its id, even on an
 * error (0x17144). */
uint32_t psos_create_partition(void* area, uint32_t block_size, uint32_t count,
                               void** partition) {
  uint32_t error = psos_call(OS_SERVICE_CREATE_PARTITION, block_size, count, 0,
                             0, area, NULL);

  if (partition != NULL) {
    *partition = (void*)__ghidra_result_a0;
  }
  return error;
}

/* Deletes a partition (0x1716e). */
uint32_t psos_delete_partition(void* partition, uint32_t block_size) {
  return psos_call(OS_SERVICE_DELETE_PARTITION, block_size, 0, 0, 0, partition,
                   NULL);
}

/* Takes a block from a partition; on success the block (0x1718a). */
uint32_t psos_get_block(void* partition, uint32_t block_size, void** block) {
  uint32_t error =
      psos_call(OS_SERVICE_GET_BLOCK, block_size, 0, 0, 0, partition, NULL);

  if (error == 0 && block != NULL) {
    *block = (void*)__ghidra_result_a1;
  }
  return error;
}

/* Returns a block to its partition (0x171b2). */
uint32_t psos_return_block(void* partition, void* block) {
  return psos_call(OS_SERVICE_RETURN_BLOCK, 0, 0, 0, 0, partition, block);
}

/* Creates an exchange of a name, with two longs; on success its id
 * (0x171ce). */
uint32_t psos_create_x(char* name, uint32_t a, uint32_t b, uint32_t* exchange) {
  uint32_t error =
      psos_call(OS_SERVICE_CREATE_X, psos_pack_name(), a, b, 0, NULL, NULL);

  if (error == 0 && exchange != NULL) {
    *exchange = __ghidra_result_a0;
  }
  return error;
}

/* Attaches to an exchange by name; on success three longs, A0's, D1's
 * and D2's (0x171fe). */
uint32_t psos_service_21(char* name, uint32_t* out1, uint32_t* out2,
                         uint32_t* out3) {
  uint32_t error =
      psos_call(OS_SERVICE_ATTACH_X, psos_pack_name(), 0, 0, 0, NULL, NULL);

  if (error == 0) {
    if (out1 != NULL) {
      *out1 = __ghidra_result_a0;
    }
    if (out2 != NULL) {
      *out2 = __ghidra_result_d1;
    }
    if (out3 != NULL) {
      *out3 = __ghidra_result_d2;
    }
  }
  return error;
}

/* Deletes an exchange (0x1723a). */
uint32_t psos_delete_x(uint32_t exchange) {
  return psos_call(OS_SERVICE_DELETE_X, 0, 0, 0, 0, (void*)exchange, NULL);
}

/* Sends a message to an exchange (0x17252). */
uint32_t psos_send_x(uint32_t exchange, void* message) {
  return psos_call(OS_SERVICE_SEND_X, 0, 0, 0, 0, (void*)exchange, message);
}

/* Sends a message to the front of an exchange's queue (0x1726e). */
uint32_t psos_jam_x(uint32_t exchange, void* message) {
  return psos_call(OS_SERVICE_JAM_X, 0, 0, 0, 0, (void*)exchange, message);
}

/* Gives a message to every task waiting at an exchange and queues
 * copies up to a count; on success the larger of the count and the
 * tasks given it, and those tasks (0x1728a). */
uint32_t psos_broadcast_x(uint32_t exchange, void* message, uint32_t count,
                          uint8_t* count_out, uint8_t* woken) {
  uint32_t error = psos_call(OS_SERVICE_BROADCAST_X, count, 0, 0, 0,
                             (void*)exchange, message);

  if (error == 0) {
    if (count_out != NULL) {
      *count_out = __ghidra_result_d0;
    }
    if (woken != NULL) {
      *woken = __ghidra_result_d1;
    }
  }
  return error;
}

/* Receives a message from an exchange into a buffer, with two longs in
 * D1 and D0 (0x172c4). */
uint32_t psos_request_x(uint32_t exchange, void* buffer, uint32_t d1,
                        uint32_t d0) {
  return psos_call(OS_SERVICE_REQUEST_X, d0, d1, 0, 0, (void*)exchange, buffer);
}

/* Signals events to a variable's task (0x172e8). */
uint32_t psos_signal_v(uint32_t variable, uint32_t value) {
  return psos_call(OS_SERVICE_SIGNAL_V, 0, value, 0, 0, (void*)variable, NULL);
}

/* Waits on a variable with the longs in D1, D2 and D0; D1 back, even on
 * an error (0x17304). */
uint32_t psos_wait_v(uint32_t d1, uint32_t d2, uint32_t d0, uint32_t* out) {
  uint32_t error = psos_call(OS_SERVICE_WAIT_V, d0, d1, d2, 0, NULL, NULL);

  if (out != NULL) {
    *out = __ghidra_result_d1;
  }
  return error;
}

/* Reads a variable with the longs in D1 and D2; D1 back, even on an
 * error (0x17332). */
uint32_t psos_get_v(uint32_t d1, uint32_t d2, uint32_t* out) {
  uint32_t error = psos_call(OS_SERVICE_GET_V, 0, d1, d2, 0, NULL, NULL);

  if (out != NULL) {
    *out = __ghidra_result_d1;
  }
  return error;
}

/* Counts a tick (0x1735c). */
uint32_t psos_announce_tick(void) {
  return psos_call(OS_SERVICE_ANNOUNCE_TICK, 0, 0, 0, 0, NULL, NULL);
}

/* The time of day: its date, time and ticks into the second, even on an
 * error (0x17370). */
uint32_t psos_get_time(uint32_t* date, uint32_t* time, uint16_t* ticks) {
  uint32_t error = psos_call(OS_SERVICE_GET_TIME, 0, 0, 0, 0, NULL, NULL);

  if (date != NULL) {
    *date = __ghidra_result_d0;
  }
  if (time != NULL) {
    *time = __ghidra_result_d1;
  }
  if (ticks != NULL) {
    *ticks = __ghidra_result_d2;
  }
  return error;
}

/* Sets the time of day (0x173a6). */
uint32_t psos_set_time(uint32_t date, uint32_t time, uint32_t ticks) {
  return psos_call(OS_SERVICE_SET_TIME, date, time, ticks, 0, NULL, NULL);
}

/* Initializes a device (0x173ca). */
uint32_t psos_io_init(uint32_t device) {
  return psos_io_call(OS_IO_INIT, device, 0, 0, 0, NULL, NULL);
}

/* Opens a device with a long (0x173e2). */
uint32_t psos_io_open(uint32_t device, uint32_t a) {
  return psos_io_call(OS_IO_OPEN, device, a, 0, 0, NULL, NULL);
}

/* Reads up to a length from a device; on success the length read
 * (0x17416). */
uint32_t psos_io_read(uint32_t device, void* buffer, uint32_t* length) {
  uint32_t error =
      psos_io_call(OS_IO_READ, device, 0, *length, 0, length, buffer);

  if (error == 0 && length != NULL) {
    *length = __ghidra_result_d2;
  }
  return error;
}

/* Writes a length to a device; on success the length written
 * (0x17448). */
uint32_t psos_io_write(uint32_t device, void* buffer, uint32_t* length) {
  uint32_t error =
      psos_io_call(OS_IO_WRITE, device, 0, *length, 0, buffer, length);

  if (error == 0 && length != NULL) {
    *length = __ghidra_result_d2;
  }
  return error;
}

/* Packs the name and creates the mutex as an exchange of that name;
 * psos_pack_name reads the name as this function's caller's first
 * argument (0x104e6). */
uint32_t mutex_create(char* name, mutex* lock) {
  return mutex_init(psos_pack_name(), lock);
}

/* mutex_acquire on its argument (0x10502). */
uint32_t mutex_lock(mutex* lock) { return mutex_acquire(lock); }

/* mutex_release on its argument (0x10518). */
uint32_t mutex_unlock(mutex* lock) { return mutex_release(lock); }

/* The task holding a mutex (0x1052e). */
uint32_t mutex_owner(void* lock) { return ((mutex*)lock)->owner; }

/* Pushes an entry on the front of a list (0x10c68). */
void list_push(klok_entry** list, klok_entry* entry) {
  entry->next = *list;
  *list = entry;
}

/* Unlinks an entry from a list; -2 when it is not on it (0x10c10). */
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
 * held (0x510aa). */
void* memory_allocate(uint32_t size) {
  uint32_t segment;

  psos_service_8(size, 1, 1, 0, &segment);
  return (void*)segment;
}

/* Frees a segment memory_allocate gave (0x510d2). */
void memory_free(void* segment) { psos_free_segment(segment); }

/* Clears the exception hook and its per-task switch (0x118a8). */
void exception_hook_reset(void) {
  exception_hook_per_task = 0;
  exception_hook = NULL;
}

/* Sets the running task's own exception hook (0x118c2). */
int32_t exception_hook_set_task(void* hook) {
  ((os_task*)psos_my_pid())->exception_hook = hook;
  return 0;
}

/* The running task's exception hook while the per-task switch is set,
 * else, or when it has none, the shared one (0x118fe). */
void* exception_hook_get(void) {
  void* hook = NULL;

  if (exception_hook_per_task != 0) {
    hook = ((os_task*)psos_my_pid())->exception_hook;
  }
  if (hook == NULL) {
    hook = exception_hook;
  }
  return hook;
}

/* The count of idle loops in the last second, which os_report_cpu
 * prints (0x10538). */
uint32_t idle_count(void) { return idle_loops_last_second; }

/* Counts a tick into the running task's record (0x16ee6). */
void task_count_tick(void) { os_kernel_data()->running->ticks++; }

/* Whether an id is an exchange's: within the data area, compared signed,
 * and its record's check its words swapped (0x169fa). */
uint32_t exchange_id_valid(uint32_t id) {
  if ((int32_t)id < (int32_t)psos_config.data_area ||
      (int32_t)psos_config.data_end < (int32_t)id) {
    return 0;
  }
  uint32_t check = ((os_exchange*)id)->id_check;

  return (check << 16 | check >> 16) == id;
}

/* Whether an id is a task's: within the data area and its record's
 * check its low word complemented (0x16a20). */
uint32_t task_id_valid(uint32_t id) {
  static const uint32_t ID_CHECK_MASK = 0xffff;

  if (id < (uint32_t)psos_config.data_area || psos_config.data_end < id) {
    return 0;
  }
  return (((os_task*)id)->id_check ^ ID_CHECK_MASK) == id;
}

/* Turns the kernel's debug output on, with the configuration's hook
 * table, or off, with interrupts masked (0x16e5a). */
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
static bool os_task_paused(os_task* task) {
  return task->delay != 0 || (task->status & OS_TASK_PAUSED) != 0;
}

/* Fills task_table, the running task first and then the kernel's list,
 * with each one's state, and sums their ticks into task_ticks_total;
 * the number of tasks (0x16540). */
int32_t task_table_build(void) {
  enum { TABLE_ENTRIES = sizeof task_table / sizeof task_table[0] };
  os_kernel* kernel = os_kernel_data();

  for (int32_t entry = 0; entry < TABLE_ENTRIES; entry++) {
    task_table[entry].task = NULL;
    task_table[entry].state = OS_TABLE_EMPTY;
  }
  os_task* running = kernel->running;

  task_table[0].state = OS_TABLE_READY;
  task_table[0].task = running;
  int32_t count = 1;

  for (os_task* task = kernel->tasks; task != NULL; task = task->list_next) {
    if (task == running) {
      continue;
    }
    task_table[count].task = task;
    if (task->status == 0) {
      task_table[count].state = OS_TABLE_READY;
    } else if (!os_task_paused(task)) {
      task_table[count].state = OS_TABLE_BLOCKED;
    } else {
      task_table[count].state = OS_TABLE_PAUSED;
    }
    count++;
  }
  task_ticks_total = 0;
  for (int32_t entry = 0; entry < count; entry++) {
    task_ticks_total += ((os_task*)task_table[entry].task)->ticks;
  }
  return count;
}

/* A task's name in task_name_text, or null for a record whose name's
 * address, task + 0x24, is 0; the copy's NUL lands on object_name_text
 * (0x160a0). */
char* task_name(void* task) {
  if ((uint32_t)task + offsetof(os_task, name) == 0) {
    return NULL;
  }
  strncpy(task_name_text, ((os_task*)task)->name, sizeof task_name_text);
  object_name_text[0] = '\0';
  return task_name_text;
}

/* An exchange's name in object_name_text, or null for a record whose
 * name's address, object + 0x14, is 0; the copy's NUL lands on
 * task_table[0].state (0x160de). */
char* object_name(void* object) {
  if ((uint32_t)object + offsetof(os_exchange, name) == 0) {
    return NULL;
  }
  strncpy(object_name_text, ((os_exchange*)object)->name,
          sizeof object_name_text);
  task_table[0].state = 0;
  return object_name_text;
}

/* A task's status as the process report's four characters (0x161f6). */
char* task_status_name(void* record) {
  os_task* task = record;

  if (task->status & OS_TASK_SPAWNING) {
    return "spwn";
  }
  if (task->status & OS_TASK_MEMORY_WAIT) {
    return "mblk";
  }
  if (task->status & OS_TASK_EXCHANGE_WAIT) {
    return "xblk";
  }
  if (task->status & OS_TASK_VARIABLE_WAIT) {
    return "vblk";
  }
  if (os_task_paused(task)) {
    return "paus";
  }
  if (task->status & OS_TASK_SUSPENDED) {
    return "susp";
  }
  if (task->status == 0) {
    return " RDY";
  }
  return " ?? ";
}

/* The segment of a task's that holds an address, from its start to its
 * size past it, or null (0x162b2). */
void* region_segment_containing(void* task, uint32_t address) {
  os_segment* segment = ((os_task*)task)->segments;

  for (; segment != NULL; segment = segment->next) {
    int32_t offset = address - (uint32_t)segment;

    if (offset >= 0 && (uint32_t)offset <= segment->size) {
      break;
    }
  }
  return segment;
}

/* A task's place on the ready queue from 0, or -1 when it is not on it
 * (0x16730). */
int32_t ready_queue_position(uint32_t* record) {
  os_task* task = os_kernel_data()->ready;
  int32_t position = 0;

  while (task != NULL && task != (os_task*)record) {
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
 * reads the list's end, null, as a record too (0x164f2). */
int32_t timer_ticks_until(void* task) {
  os_task* timer = os_kernel_data()->timers;
  int32_t ticks = 0;

  while (true) {
    if (os_task_paused(task)) {
      /* A copy the compiler cannot tie to timer, so that reading through
       * it, null or not, does not let it drop the test of timer below. */
      os_task* record = timer;

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

/* Copies a string, as the image's reports do in line. */
static void os_copy_text(char* to, const char* from) {
  while ((*to++ = *from++) != '\0') {
  }
}

/* Rounds bytes to the nearest kilobyte, as the reports print them. */
static int32_t os_kilobytes(int32_t bytes) {
  static const int32_t KILOBYTE = 0x400;

  return (bytes + KILOBYTE / 2) / KILOBYTE;
}

/* Prints the idle count and the CPU it leaves free, 172 millionths of
 * the CPU a loop, rounded (0x1606c). */
void os_report_cpu(printf_function* print) {
  static const uint32_t MILLIONTHS_PER_LOOP = 172;
  static const uint32_t MILLION = 1000000;
  uint32_t idle = idle_count();

  print("Free CPU cycles: count 0x%08X, approx %2d%%\r\n", idle,
        (MILLION / 2 + idle * MILLIONTHS_PER_LOOP) / MILLION);
}

/* Prints the tasks, those free and the ticks they ran (0x1660a). */
void os_report_process_summary(printf_function* print) {
  char format[] = "\t%d Process(s) (%d avail); Total time: %-d ticks.\r\n";
  int32_t free = 0;

  for (os_task* task = os_kernel_data()->free_tasks; task != NULL;
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
 * what nothing set") (0x162d4). */
void os_report_process(void* record, char* status, int32_t position,
                       uint32_t ticks, printf_function* print) {
  /* The registers as the caller left them, which the image reads. */
  register uint32_t caller_d5 __asm__("d5");
  register int32_t caller_d6 __asm__("d6");
  __asm__("" : "=r"(caller_d5));
  __asm__("" : "=r"(caller_d6));
  static const int32_t PERCENT = 100;
  os_task* task = record;
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

  for (os_segment* segment = task->segments; segment != NULL;
       segment = segment->next) {
    memory += segment->size;
  }
  uint32_t stack_share = caller_d5;
  uint32_t pointer = task->user_pointer;

  if (region_segment_containing(task, task->supervisor_pointer) != NULL) {
    uint32_t top = task->user_top;
    uint32_t size = task->user_top - task->supervisor_top - 4;

    if (size == 0) {
      size = task->supervisor_top - task->stack_bottom;
      pointer = task->supervisor_pointer;
      top = task->supervisor_top;
    }
    stack_share = size == 0 ? 0 : (top - pointer) * PERCENT / size;
  }
  uint32_t task_ticks = task->ticks;
  uint32_t cpu_share = 0;

  task->ticks = 0;
  if (task_ticks_total != 0) {
    cpu_share =
        (task_ticks_total / 2 + task_ticks * PERCENT) / task_ticks_total;
  }
  os_copy_text(state, status);
  if (task->mode & (OS_MODE_TIME_SLICE | OS_MODE_NO_PREEMPTION)) {
    strcat(state, "/");
  }
  if (task->mode & OS_MODE_TIME_SLICE) {
    strcat(state, "R");
  }
  if (task->mode & OS_MODE_NO_PREEMPTION) {
    strcat(state, "*");
  }
  print("$%08X %4s  %6s  %3d %2d  %2s %5d %4dkB %2d%% %2d%%\r\n", task, name,
        state, task->priority, task->group, place, ticks, os_kilobytes(memory),
        stack_share, cpu_share);
  os_resource* resource = task->resources;

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
 * last test leaves it, 1 (0x1675a). */
int32_t os_report_processes(printf_function* print) {
  os_task* running = task_table[0].task;
  int32_t state = OS_TABLE_READY;

  print("%s", "    pid   PNAME  STAT/M PRI GID POS  TIX MEMORY STK CPU\r\n");
  os_report_process(running, " RUN", 0, running->slice_ticks, print);
  print("%s", "\r\n");
  for (; state <= OS_TABLE_BLOCKED; state++) {
    bool listed = false;

    for (int32_t entry = 1; entry < task_count; entry++) {
      if ((int8_t)task_table[entry].state != state) {
        continue;
      }
      os_task* task = task_table[entry].task;
      int32_t position = ready_queue_position((uint32_t*)task);
      uint32_t ticks = state == OS_TABLE_PAUSED ? timer_ticks_until(task)
                                                : task->slice_ticks;

      os_report_process(task, task_status_name(task), position, ticks, print);
      listed = true;
    }
    if (listed) {
      print("%s", "\r\n");
    }
  }
  return state - OS_TABLE_BLOCKED;
}

/* Prints an exchange's line of the exchange report: its id, name,
 * queueing, access, limit and queue, and the tasks waiting (0x16118). */
void os_report_exchange(uint32_t* id, printf_function* print) {
  os_exchange* exchange = (os_exchange*)id;
  char* queueing = exchange->by_priority == 0 ? "fifo" : "prio";
  char* access = exchange->group_only == 0 ? "any" : "grp";
  char limit[40];

  if (exchange->limited == 0) {
    os_copy_text(limit, "inf");
  } else {
    sprintf(limit, "%d", exchange->queue_limit);
  }
  print("0x%07x  %4s  %4s %4s  %3s    %d    ", exchange, object_name(exchange),
        queueing, access, limit, exchange->queued);
  /* The queue ends at the exchange, whose first field is its head. */
  os_task* end = (os_task*)exchange;

  for (os_task* task = exchange->waiting; task != end;) {
    print("%s", task_name(task));
    task = task->next;
    if (task != end) {
      print(",");
    }
  }
  print("%s", "\r\n");
}

/* Prints the exchange report, an exchange a line (0x1665e). */
void os_report_exchanges(printf_function* print) {
  char header[] = "    xid   XNAME  TYPE  ACC  maxQ  Qlen BLOCKED\r\n";

  print("%s", header);
  for (os_exchange* exchange = os_kernel_data()->exchanges; exchange != NULL;
       exchange = exchange->next) {
    os_report_exchange((uint32_t*)exchange, print);
  }
}

/* Prints the exchanges and message buffers, used and free (0x166b0). */
void os_report_exchange_summary(printf_function* print) {
  os_kernel* kernel = os_kernel_data();
  int32_t exchanges = 0;
  int32_t free_exchanges = 0;
  int32_t free_messages = 0;

  for (os_exchange* exchange = kernel->exchanges; exchange != NULL;
       exchange = exchange->next) {
    exchanges++;
  }
  for (os_exchange* exchange = kernel->free_exchanges; exchange != NULL;
       exchange = exchange->next) {
    free_exchanges++;
  }
  for (os_message* message = kernel->free_messages; message != NULL;
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
 * segment's region indexes the sums unchecked, as in the image
 * (0x16828). */
void os_report_regions(printf_function* print) {
  /* A waiter's region is its record's bits 3 to 0, less 1. */
  static const uint32_t REGION_WAIT_MASK = 0xf;
  static const uint16_t SEGMENT_REGION_MASK = 3;
  os_kernel* kernel = os_kernel_data();
  int32_t held_count[OS_REGIONS] = {0, 0};
  int32_t free_bytes[OS_REGIONS];
  int32_t held_bytes[OS_REGIONS] = {0, 0};
  void* start[OS_REGIONS] = {psos_config.data_area,
                             (void*)psos_config.region_2_start};

  print(
      "%s",
      "Region strt       end  min     max    pend   USED  cnt  AVAIL cnt\r\n");
  for (int32_t entry = 0; entry < task_count; entry++) {
    for (os_segment* segment = ((os_task*)task_table[entry].task)->segments;
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
    for (os_segment* segment = kernel->region_free[region]; segment != NULL;
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
  os_task* end = (os_task*)&kernel->region_waiting;
  os_task* task = kernel->region_waiting;

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
 * longs, and its flags (0x16280). */
void os_report_region_line(char* name, uint32_t* record,
                           printf_function* print) {
  os_segment* segment = (os_segment*)record;

  print("%4s 0x%08x %7d   0x%08x  0x%08x  0x%08x 0x%04x\r\n", name, segment,
        segment->size, segment->next, segment->field_04, segment->field_08,
        segment->flags);
}

/* stack_measure's user stack's use and size, then its supervisor
 * stack's: array sizes, so #define. */
#define OS_STACK_USAGE_LONGS 4

#define OS_STACK_MESSAGE_SIZE 80

/* A task's user stack's use and size, then its supervisor stack's, as
 * stack_usage measures them, its used bytes in D0's half of the result
 * and the size in D1's (0x1321a). */
int32_t stack_measure(void* record, uint32_t* usage) {
  static const uint32_t HALF_BITS = 32;
  static const uint32_t USER_STACK_GAP = 4;
  os_task* task = record;
  uint64_t supervisor =
      stack_usage((uint16_t*)task->stack_bottom, (char*)task->supervisor_top);

  usage[2] = supervisor >> HALF_BITS;
  usage[3] = supervisor;
  uint64_t user =
      stack_usage((uint16_t*)(task->supervisor_top + USER_STACK_GAP),
                  (char*)task->user_top);

  usage[0] = user >> HALF_BITS;
  usage[1] = user;
  return 0;
}

/* Prints a task's user and supervisor stacks' use and sizes
 * (0x132f4). */
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
 * (docs/firmware/quirks.md, "Reads of what nothing set") (0x16bb6). */
void task_stack_info(void* record, uint32_t* user_top, uint32_t* user_pointer,
                     uint32_t* user_size, uint32_t* supervisor_top,
                     uint32_t* supervisor_pointer, uint32_t* supervisor_size) {
  /* The image's user stack is 4 bytes short of the gap, and a segment's
   * use 0x12 short of its size. */
  static const uint32_t USER_STACK_GAP = 4;
  static const uint32_t SEGMENT_OVERHEAD = 0x12;
  os_task* task = record;
  volatile uint32_t unset_top;
  os_segment* volatile unset_segment;

  *user_pointer = task->user_pointer;
  *supervisor_pointer = task->supervisor_pointer;
  if (task->supervisor_top != task->stack_bottom) {
    *supervisor_top = task->supervisor_top;
    *supervisor_size = task->supervisor_top - task->stack_bottom;
    *user_top = task->user_top;
    *user_size = task->user_top - task->supervisor_top - USER_STACK_GAP;
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

/* Sets a task's priority, which the kernel changes by a step from the
 * present one, and returns the priority it had (0x1339e). */
uint8_t task_set_priority(uint32_t task, uint8_t priority) {
  uint8_t old;

  psos_priority(task, 0, &old);
  psos_priority(task, priority - old, &old);
  return old;
}

/* Spins 729 empty loops for each count, the count a word once it is not
 * 0 (0x13428). */
void busy_wait(uint32_t count) {
  /* busy_wait's empty loops for each count (0x13432). */
  static const uint16_t BUSY_WAIT_SPINS = 0x2d9;

  if (count == 0) {
    return;
  }
  uint16_t remaining = count;

  do {
    for (uint16_t spin = BUSY_WAIT_SPINS; spin != 0; spin--) {
      __asm__ volatile("");
    }
    remaining--;
  } while (remaining != 0);
}

/* Pauses a number of seconds, as kernel ticks, at least one; a negative
 * time pauses a tick, and one past 2^21 seconds that long; nothing calls
 * it (0x13448). */
void sleep_seconds(float seconds) {
  /* The kernel's ticks a second, and the shortest and longest pauses
   * it takes, a tick and 2^21 seconds. */
  static const float TICKS_PER_SECOND = 1024;
  static const float SLEEP_LONGEST = 1 << 21;

  if (float_compare(0.0f, seconds) > 0) {
    seconds = 1.0f / TICKS_PER_SECOND;
  } else if (float_compare(seconds, SLEEP_LONGEST) > 0) {
    seconds = SLEEP_LONGEST;
  }
  uint32_t ticks = float_to_unsigned(float_multiply(seconds, TICKS_PER_SECOND));

  psos_pause(ticks == 0 ? 1 : ticks);
}
