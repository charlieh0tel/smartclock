/* The spool task and the debug messages, written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C"): spoo,
 * which runs functions for other tasks, the deferred messages it
 * flushes, the debug writes with and without a task's name, and the
 * fatal errors.  The exported C includes this file after its own, so
 * the types and prototypes it declares are in scope; each function here
 * keeps its generated prototype. */

/* A length-prefixed message and ", %08X" with a code, to fatal_error;
 * the two arguments before the code are unread (0x1eefc). */
void fatal_error_with_code(char* message, uint32_t unused_1, uint32_t unused_2,
                           uint32_t code) {
  char text[100];

  memcpy(text, message + 1, *message);
  sprintf(text + *message, ", %08X", code);
  fatal_error(text);
}

/* Writes a string by polling while debug output is on; returns its
 * length, 0 when off (0x1ef74). */
int32_t debug_write(char* text) {
  if (!debug_enabled) {
    return 0;
  }
  return console_write_polled(text);
}

/* The calling task's id, from ident with no name. */
static uint32_t spool_my_task(void) {
  uint32_t task;
  uint8_t error;

  if ((uint8_t)psos_ident(0, &task, &error) != 0) {
    fatal_error("dmessage");
  }
  return task;
}

/* Prints "task: message" with the calling task's name, while debug
 * output is on (0x1eff6). */
void task_debug_message(char* message) {
  if (debug_enabled) {
    uint32_t task = spool_my_task();

    debug_printf("%s: %s", task_name((void*)task), message);
  }
}

/* A task and its dmes_ flag, which task_dmessage looks the caller up
 * in. */
typedef struct spool_task_flag {
  uint32_t* task;
  uint32_t* enabled;
} spool_task_flag;

/* The tasks task_dmessage prints for, in the order it checks them. */
static const spool_task_flag spool_task_flags[] = {
    {&task_gpsm, &dmes_gpsm}, {&task_hmon, &dmes_hmon},
    {&task_klok, &dmes_klok}, {&task_pllp, &dmes_pllp},
    {&task_root, &dmes_root}, {&task_scpi, &dmes_scpi},
    {&task_spoo, &dmes_spoo}, {&task_curv, &dmes_curv},
};

/* Prints "task: message" with the calling task's name, while debug
 * output and the task's dmes_ flag are on (0x1f056). */
void task_dmessage(char* message) {
  uint32_t task;
  bool enabled = false;

  if (!debug_enabled) {
    return;
  }
  task = spool_my_task();
  for (uint32_t i = 0; i < sizeof spool_task_flags / sizeof spool_task_flags[0];
       i++) {
    if (task == *spool_task_flags[i].task &&
        *spool_task_flags[i].enabled != 0) {
      enabled = true;
      break;
    }
  }
  if (enabled) {
    debug_printf("%s: %s", task_name((void*)task), message);
  }
}

/* Keeps the message, and the task's name, at crash_message; prints
 * them and "RESET to recover." while debug output is on; then spins
 * until the watchdog restarts the processor (0x1f176). */
void fatal_error(char* message) {
  /* The length below which crash_message takes ", " and a task's name. */
  static const uint32_t CRASH_ROOM_FOR_NAME = 74;
  static const uint32_t FATAL_PAUSES = 10;
  uint32_t task;

  strncpy(crash_message, message, MONITORS_CRASH_MESSAGE_SIZE);
  crash_message[MONITORS_CRASH_MESSAGE_SIZE - 1] = 0;
  if (kernel_running && (uint8_t)psos_ident(0, &task, 0) == 0) {
    uint32_t length = 0;

    while (crash_message[length] != 0) {
      length++;
    }
    if (length < CRASH_ROOM_FOR_NAME) {
      strcat(crash_message, ", ");
      strcat(crash_message, task_name((void*)task));
    }
    if (debug_enabled) {
      printf_polled("%s: %s", task_name((void*)task), "FATAL ERROR: ");
      printf_polled("%s\r\nRESET to recover.\r\n", message);
      psos_pause(FATAL_PAUSES);
    }
    set_interrupt_mask(CPU_IPL_4);
  }
  for (;;) {
  }
}

/* Creates the spool task at a priority, unless one is running, with its
 * exchange, and starts it; returns 0, else the kernel's error, or 1 for
 * a spool task already there (0x20c96). */
uint32_t spoo_task_create(uint32_t priority) {
  /* The spool task: its name, its stack sizes in words (supervisor
   * and user), and its exchange's create_x arguments, which psos.md
   * leaves unnamed (structures.md, "Others"). */
  static const char NAME[] = "spoo";
  static const uint32_t SUPERVISOR_STACK = 0x600;
  static const uint32_t USER_STACK = 0x10;
  static const uint32_t EXCHANGE_A = 2;
  static const uint32_t EXCHANGE_B = 5;
  uint32_t error = (uint8_t)psos_ident((char*)NAME, 0, 0) == 0;
  char text[80];

  if (error == 0) {
    error = (uint8_t)psos_create_x((char*)NAME, EXCHANGE_A, EXCHANGE_B,
                                   &spoo_exchange);
  }
  if (error == 0) {
    error = (uint8_t)psos_create((char*)NAME, 0, priority, SUPERVISOR_STACK,
                                 USER_STACK, 0, &task_spoo);
  }
  if (error == 0) {
    error = (uint8_t)psos_start(task_spoo, spoo_task);
  }
  if (error == 0) {
    spoo_started = 1;
  } else {
    sprintf(text, "Spool_go error %#04x", error);
    fatal_error(text);
  }
  return error;
}

/* Runs a function with three arguments: itself before the spool task
 * starts, returning 1, and after by sending it to the spool task,
 * returning send_x's error; 0 for no function (0x20d66). */
uint32_t spoo_run(spoo_function* function, uint32_t a, uint32_t b, uint32_t c) {
  spoo_request request;

  if (function == 0) {
    return 0;
  }
  if (!spoo_started) {
    function(a, b, c);
    return 1;
  }
  request.field_04 = 0;
  request.function = function;
  request.arguments[0] = a;
  request.arguments[1] = b;
  request.arguments[2] = c;
  return (uint8_t)psos_send_x(spoo_exchange, &request);
}

/* Formats a message into deferred_text, which the spool task prints;
 * returns vsprintf's count, -1 when there is no room (0x20dd2). */
int32_t deferred_vprintf(char* format, void* arguments) {
  /* deferred_text's room for a message: one starts only below this. */
  static const int16_t DEFERRED_LAST_START = 944;
  static const uint32_t MODE_NO_PREEMPT =
      0x80; /* mode's bit (psos.md, service 32) */
  int32_t count = -1;

  /* with preemption off, empties the text once it has all been printed */
  psos_mode(MODE_NO_PREEMPT, MODE_NO_PREEMPT, 0);
  if (deferred_start >= deferred_end) {
    deferred_end = 0;
    deferred_start = 0;
  }
  psos_mode(~MODE_NO_PREEMPT, MODE_NO_PREEMPT, 0);
  if (deferred_end < DEFERRED_LAST_START) {
    count = vsprintf(deferred_text + deferred_end, format, arguments);
    if (count >= 0) {
      deferred_end += count + 1;
    }
  }
  return count;
}

/* The spool task: runs each function spoo_run sends, and prints the
 * deferred messages when its request ends with FLUSH_ERROR
 * (0x20e4e). */
void spoo_task(void) {
  /* Its request_x's argument, which psos.md leaves unnamed, and the
   * request error on which it prints the deferred messages instead
   * (structures.md, "Others"). */
  static const uint32_t REQUEST_B = 0x400;
  static const uint8_t FLUSH_ERROR = 0x81;

  psos_supervisor();
  for (;;) {
    spoo_receive_error =
        (uint8_t)psos_request_x(spoo_exchange, &spoo_message, 0, REQUEST_B);
    watchdog_spool++;
    if (spoo_receive_error == 0) {
      spoo_message.function(spoo_message.arguments[0],
                            spoo_message.arguments[1],
                            spoo_message.arguments[2]);
    } else if (spoo_receive_error == FLUSH_ERROR) {
      deferred_flush();
    }
  }
}

/* Prints deferred_text's messages from deferred_start, each with CR LF
 * (0x20ebe). */
void deferred_flush(void) {
  while (deferred_start < deferred_end) {
    int32_t count = printf("%s\r\n", deferred_text + deferred_start);

    if (count < 0) {
      break;
    }
    /* past the message and its NUL, CR LF not stored */
    deferred_start += count - 1;
  }
}

/* Prints a message through task_debug_message, with the task's name,
 * or else through debug_write by polling (0x22d4a). */
void debug_message(char* message, char with_task_name) {
  if (with_task_name) {
    task_debug_message(message);
  } else {
    debug_write(message);
  }
}
