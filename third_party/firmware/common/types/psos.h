/* The kernel's and pForth's records and the I/O table's types, as the images'
 * smartclock.h declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C". Where a comment cites code, it is the
 * Z3805A 3543B's, which the 58503A's copy of the comment came from. */

/* An entry of status_screen_mode_lines: a mode line of the status
 * screen and the mode it stands for, which status_screen marks ">>"
 * when it is the snapshot's.  A null text ends the table. */
typedef struct status_screen_mode_line {
  status_screen_line
      mode; /* +0x00: 3 Locked, 2 Recovery, 1 Holdover, 0 Power-up */
  uint8_t field_01;
  char* text; /* +0x02 */
} status_screen_mode_line;
/* One entry of the kernel's I/O table, a "jmp.l routine" instruction:
 * kernel_dispatch_io jumps into the table at (device * 6 + function) * 6
 * (docs/firmware/psos.md, "Calls"). */
typedef struct io_jump {
  uint16_t opcode; /* +0x00: 0x4ef9, jmp.l */
  void* routine;   /* +0x02 */
} io_jump;

/* A device's six I/O routines, functions 0 to 5 in the order of the
 * console's dev_init, dev_open, dev_close, dev_read, dev_write and
 * dev_ctrl.  io_table holds four, which psos_config_init
 * copies from ROM io_table_defaults: DUART channel B, the QSPI, the console's
 * string device and the SCI.  The installer's, at installer_io_table, holds
 * one. */
typedef struct io_device_jumps {
  io_jump function[6]; /* +0x00 */
} io_device_jumps;

/* An io_jump's slot filled with "rts; nop; nop" instead of a jmp.l. */
typedef struct io_stub {
  uint16_t opcode;  /* +0x00: 0x4e75, rts */
  uint16_t nops[2]; /* +0x02: 0x4e71, nop */
} io_stub;

/* A fifth device's slot after io_table_defaults, at io_device_unused_stubs,
 * each function an io_stub: psos_config_init copies only the four before it
 * (0x90 bytes, psos_config_init+0x16) and psos_config.last_device is 3, so
 * nothing reaches it. */
typedef struct io_device_stubs {
  io_stub function[6]; /* +0x00 */
} io_device_stubs;

/* The bits of a pForth word's flags, pforth_word_header.flags: what
 * forth_interpret does with the word it finds (forth_interpret+0x20 to
 * forth_interpret+0x40), forth_find's skip (forth_find+0xe), and the clears at
 * forth_semicolon+0x22 and forth_code_does+0x4. */
typedef enum __attribute__((packed)) pforth_word_flag {
  PFORTH_WORD_COMPILE_ONLY =
      0x01, /* interpreted, it is refused (forth_interpret+0x20) */
  PFORTH_WORD_HIDDEN =
      0x02, /* forth_find skips it; cleared at forth_semicolon+0x22 */
  PFORTH_WORD_IMMEDIATE =
      0x04,                  /* run even while compiling (forth_interpret+0x2c);
                                immediate sets it (forth_immediate+0x8) */
  PFORTH_WORD_INLINE = 0x08, /* compiled by copying inline_size bytes of its
                                code rather than a jsr (forth_interpret+0x32);
                                cleared at forth_code_does+0x4 */
} pforth_word_flag;

/* The pForth console's dictionary (docs/firmware/structures.md, "The
 * console's dictionary").  A word's header: the kernel's 154,
 * forth_store_iodev+0x1e to forth_user, are in ROM, linked from
 * forth_header_user, the latest, which pforth_dictionary_init stores in
 * pforth_latest_word; pforth_define_word builds a new one the same way in RAM.
 * The name follows the 12 bytes, ended by a NUL and padded to even with 0xff.
 */
typedef struct pforth_word_header {
  struct pforth_word_header* link; /* +0x00: the word before; NULL ends it */
  void* code;                      /* +0x04: the word's routine */
  uint16_t inline_size; /* +0x08: the bytes of code forth_interpret copies to
                           compile a PFORTH_WORD_INLINE word
                           (forth_interpret+0x40) */
  uint16_t flags; /* +0x0a: pforth_word_flag bits, all in the low byte; the
                     kernel reads and writes the word */
  char name[];    /* +0x0c */
} pforth_word_header;

/* A diagnostic word, one entry of diag_words, which
 * diag_words_define walks, 0x26 bytes a step, to the entry whose code is
 * NULL, passing each to pforth_define_word (diag_words_define+0xa to
 * diag_words_define+0x22). */
typedef struct diag_word_entry {
  void* code;             /* +0x00: the word's routine */
  char name[26];          /* +0x04: NUL-padded */
  int32_t argument_count; /* +0x1e */
  uint32_t result_count;  /* +0x22 */
} diag_word_entry;

/* The pSOS kernel's records and codes (docs/firmware/psos.md, "Inside
 * the kernel"). */
/* A task record's status byte, +0x13, by task_status_name's text. */
typedef enum __attribute__((packed)) psos_task_status {
  PSOS_TASK_READY = 0x00,         /* " RDY": none of the rest */
  PSOS_TASK_SUSPENDED = 0x04,     /* "susp" */
  PSOS_TASK_PAUSED = 0x08,        /* "paus", as a nonzero delay is */
  PSOS_TASK_VARIABLE_WAIT = 0x10, /* "vblk": in wait_v */
  PSOS_TASK_EXCHANGE_WAIT = 0x20, /* "xblk": in request_x */
  PSOS_TASK_MEMORY_WAIT = 0x40,   /* "mblk": on a region's queue, presumably */
  PSOS_TASK_SPAWNING = 0x80,      /* "spwn": created, not started */
} psos_task_status;

/* The kernel's errors that a caller tells apart, the low byte of a
 * service's result (D7). */
typedef enum __attribute__((packed)) psos_error {
  PSOS_ERROR_NONE = 0,
  PSOS_ERROR_NO_MESSAGE =
      0x80, /* request_x not to wait, or from an
               interrupt, and none there (kernel_request_x+0x70) */
  PSOS_ERROR_TIMEOUT =
      0x81, /* a wait's ticks run out
               (kernel_process_ticks, kernel_process_ticks+0x52) */
} psos_error;

/* A task record's mode byte, +0x11 (psos.md, service 32). */
typedef enum __attribute__((packed)) psos_task_mode {
  PSOS_MODE_TIME_SLICE = 0x40,    /* the report's "R" */
  PSOS_MODE_NO_PREEMPTION = 0x80, /* the report's "*" */
} psos_task_mode;

/* task_table_entry.state, by which os_report_processes groups the
 * tasks; PSOS_TABLE_EMPTY marks an unused entry. */
typedef enum __attribute__((packed)) psos_table_state {
  PSOS_TABLE_READY = 0,
  PSOS_TABLE_PAUSED = 1,
  PSOS_TABLE_BLOCKED = 2,
  PSOS_TABLE_EMPTY = 0xff,
} psos_table_state;

/* The kernel's services, trap #3's D7 (psos.md, "Services"); 8 and 21
 * are named by the pSOS-68K brochure's list there. */
typedef enum __attribute__((packed)) psos_service {
  PSOS_SERVICE_VERSION = 0, /* ver: the kernel's version in D7 */
  PSOS_SERVICE_SPAWN = 1,   /* create a task */
  PSOS_SERVICE_START = 2,
  PSOS_SERVICE_IDENT = 3,
  PSOS_SERVICE_DELETE = 4,
  PSOS_SERVICE_SUSPEND = 5,
  PSOS_SERVICE_RESUME = 6,
  PSOS_SERVICE_PRIORITY = 7,
  PSOS_SERVICE_ALLOCATE_SEGMENT = 8,
  PSOS_SERVICE_FREE_SEGMENT = 9,
  PSOS_SERVICE_ASSIGN_SEGMENT = 10,
  PSOS_SERVICE_RECLAIM_SEGMENT = 16,
  PSOS_SERVICE_BROADCAST_X = 18,
  PSOS_SERVICE_JAM_X = 19,
  PSOS_SERVICE_CREATE_X = 20,
  PSOS_SERVICE_ATTACH_X = 21,
  PSOS_SERVICE_DELETE_X = 22,
  PSOS_SERVICE_SEND_X = 23,
  PSOS_SERVICE_REQUEST_X = 24,
  PSOS_SERVICE_SIGNAL_V = 25,
  PSOS_SERVICE_WAIT_V = 26,
  PSOS_SERVICE_GET_V = 27,
  PSOS_SERVICE_PAUSE = 28,
  PSOS_SERVICE_ANNOUNCE_TICK = 29,
  PSOS_SERVICE_GET_TIME = 30,
  PSOS_SERVICE_SET_TIME = 31,
  PSOS_SERVICE_MODE = 32,
  PSOS_SERVICE_CREATE_PARTITION = 35,
  PSOS_SERVICE_DELETE_PARTITION = 36,
  PSOS_SERVICE_GET_BLOCK = 37,
  PSOS_SERVICE_RETURN_BLOCK = 38,
} psos_service;

/* The I/O functions, trap #4's D7 (psos.md, "Calls" and its I/O
 * functions' table: close's stub psos_io_close, control's psos_io_control). */
typedef enum __attribute__((packed)) psos_io_function {
  PSOS_IO_INIT = 0,
  PSOS_IO_OPEN = 1,
  PSOS_IO_CLOSE = 2,
  PSOS_IO_READ = 3,
  PSOS_IO_WRITE = 4,
  PSOS_IO_CONTROL = 5,
} psos_io_function;

typedef struct psos_task psos_task;

/* A segment of a region, on a task's list or a region's free list:
 * os_report_region_line prints its first five fields. */
typedef struct psos_segment {
  struct psos_segment* next; /* +0x00 */
  uint32_t field_04;
  uint32_t field_08;
  uint32_t size;  /* +0x0c: bytes, from the segment's start */
  uint16_t flags; /* +0x10: the region, 1 or 2, in bits 1 to 0 */
} psos_segment;

/* One of a task's resources, on its list at +0x1c. */
typedef struct psos_resource {
  struct psos_resource* next; /* +0x00 */
  void* object;               /* +0x04: an exchange; object_name names it */
} psos_resource;

/* A task's record, 0xb2 bytes, which kernel_init carves from
 * the data area; its address is its id. */
struct psos_task {
  psos_task* next; /* +0x00: the next on the ready queue, or on the circular
                    queue of an exchange or region it waits at */
  uint32_t field_04;
  psos_task* timer_next; /* +0x08: the next on the delay list */
  uint32_t field_0c;
  uint8_t priority;        /* +0x10 */
  psos_task_mode mode;     /* +0x11: service 32's mode byte */
  uint8_t delay;           /* +0x12: nonzero while paused */
  psos_task_status status; /* +0x13: 0 when ready */
  uint8_t group;           /* +0x14 */
  uint8_t field_15;
  uint16_t events_waited;   /* +0x16 */
  uint16_t events_pending;  /* +0x18 */
  uint16_t slice_ticks;     /* +0x1a: the report's TIX for a task not paused */
  psos_resource* resources; /* +0x1c */
  psos_segment* segments;   /* +0x20: the segments it holds */
  char name[4];             /* +0x24 */
  uint32_t id_check;        /* +0x28: the id with its low word complemented */
  uint32_t field_2c;
  psos_task* list_next; /* +0x30: the next on the kernel's task list or its
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
typedef struct psos_exchange {
  psos_task* waiting; /* +0x00: the circular queue of tasks waiting, which
                       ends at the exchange itself */
  uint8_t field_04[0xc];
  uint8_t by_priority; /* +0x10: queues by priority, else first in first out */
  uint8_t group_only;  /* +0x11: only its group may use it, else any */
  uint8_t limited;     /* +0x12: queue_limit holds, else no limit */
  uint8_t field_13;
  char name[4];               /* +0x14 */
  uint32_t id_check;          /* +0x18: the id with its words swapped */
  struct psos_exchange* next; /* +0x1c: the next on the kernel's list */
  uint8_t queue_limit;        /* +0x20 */
  uint8_t queued;             /* +0x21: messages queued */
} psos_exchange;

/* A message buffer, 0x18 bytes, on the free list by its first long. */
typedef struct psos_message {
  struct psos_message* next; /* +0x00 */
} psos_message;

/* The kernel's data area, psos_config.data_area, which it keeps in A6;
 * the regions' arrays are region 1's and region 2's. */
typedef struct psos_kernel {
  psos_task* running;            /* +0x00 */
  psos_task* ready;              /* +0x04: the ready queue, by priority */
  psos_task* timers;             /* +0x08: the delay list, through timer_next */
  psos_task* tasks;              /* +0x0c: every task, through list_next */
  psos_task* free_tasks;         /* +0x10 */
  psos_exchange* exchanges;      /* +0x14 */
  psos_exchange* free_exchanges; /* +0x18 */
  psos_message* free_messages;   /* +0x1c */
  uint8_t field_20[0x10];
  void* debug_hooks;            /* +0x30: psos_debug_output's table or 0 */
  uint32_t region_minimum[2];   /* +0x34: os_report_regions' "min" */
  uint32_t region_maximum[2];   /* +0x3c: "max" */
  uint32_t region_pending[2];   /* +0x44: "pend" */
  uint32_t region_end[2];       /* +0x4c */
  psos_segment* region_free[2]; /* +0x54 */
  uint8_t field_5c[8];
  psos_task* region_waiting; /* +0x64: the circular queue of tasks waiting for a
                              region, which ends at this field */
} psos_kernel;

/* The kernel's configuration table, psos_config, which
 * psos_config_init copies from ROM psos_config_defaults; the installer's at
 * installer_psos_config. The kernel reaches it through the pointer at
 * primary_unpack_records+0xce, which the long at primary_trap_5+0x8 holds, and
 * keeps data_area in A6.  kernel_init carves the data area into the supervisor
 * stack and the records, and gives region 1 the rest up to data_end. */
typedef struct psos_configuration {
  struct psos_kernel* data_area; /* +0x00: the kernel's state, A6; the
                                    supervisor stack starts
                      200 bytes in */
  uint32_t data_end; /* +0x04: region 1's end; kernel_init fails unless its
                        records fit below it; task_id_valid and
                        exchange_id_valid take ids from data_area up to it */
  uint32_t region_1_minimum; /* +0x08: at least 18; os_report_regions' "min" */
  uint32_t region_2_start;   /* +0x0c: 0 */
  uint32_t region_2_end; /* +0x10: 0; under 72 bytes, kernel_init leaves region
                            2 empty */
  uint32_t region_2_minimum; /* +0x14 */
  uint16_t task_records;     /* +0x18: kernel_init makes one more 0xb2-byte task
                                record than this */
  uint16_t exchange_records; /* +0x1a: 0x22 bytes each */
  uint16_t
      message_buffers; /* +0x1c: 0x18 bytes each; os_report_exchange_summary's
                          "Msg buffer(s)" total */
  uint32_t field_1e; /* +0x1e: kernel_init copies it to the data area's +0x86 */
  uint16_t last_device; /* +0x22: kernel_init runs function 0 of each device up
                           to it; kernel_dispatch_io refuses a higher one */
  io_device_jumps* io_table;      /* +0x24 */
  uint32_t supervisor_stack_size; /* +0x28: ">> pSOS system supervisor stack:"
                                     divides by it */
  void* root_entry; /* +0x2c: where kernel_init starts ROOT, root_task */
  uint16_t
      root_stack_sizes[2]; /* +0x30: ROOT's two sizes, kernel_create's D3 */
  uint16_t root_priority; /* +0x34: ROOT's priority, kernel_create's D2; IDLE is
                             given 1 */
  void* start_hook;  /* +0x36: kernel_start calls it with the started task's
                        record in A0: stack_fill */
  void* delete_hook; /* +0x3a: kernel_delete calls it while nonzero; 0 */
  uint32_t field_3e; /* +0x3e: kernel_init copies it to the data area's +0xb4 */
  void** debug_hooks; /* +0x42: a cell holding a hook table's address;
                         psos_debug_output puts the table at the data area's
                         +0x30, whose +4 kernel_dispatch_service and
                         kernel_dispatch_io call */
  void** task_hooks; /* +0x46: a cell holding a hook table, whose +4 kernel_init
                        calls and +8 kernel_delete; 0 */
  uint8_t field_4a[24];
} psos_configuration;

/* One of the 17 entries of task_table, which
 * task_table_build fills from the running task and the kernel's task
 * list, the running task first. */
typedef struct task_table_entry {
  psos_table_state state; /* +0x00: ready when the record's status is clear;
                             paused when its delay is set or it has
                             PSOS_TASK_PAUSED, as timer_ticks_until counts;
                             blocked otherwise.  os_report_processes lists
                             by it */
  uint8_t field_01;
  struct psos_task* task; /* +0x02: the task's record */
} task_table_entry;
