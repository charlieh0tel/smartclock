/* The spool task, spoo, which runs functions for other tasks, and the
 * deferred messages it flushes, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `services/spool`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

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
  int32_t count = -1;

  /* with preemption off, empties the text once it has all been printed */
  psos_mode(PSOS_MODE_NO_PREEMPTION, PSOS_MODE_NO_PREEMPTION, 0);
  if (deferred_start >= deferred_end) {
    deferred_end = 0;
    deferred_start = 0;
  }
  psos_mode(~(uint32_t)PSOS_MODE_NO_PREEMPTION, PSOS_MODE_NO_PREEMPTION, 0);
  if (deferred_end < DEFERRED_LAST_START) {
    count = vsprintf(deferred_text + deferred_end, format, arguments);
    if (count >= 0) {
      deferred_end += count + 1;
    }
  }
  return count;
}

/* The spool task: runs each function spoo_run sends, and prints the
 * deferred messages when its request times out
 * (0x20e4e). */
void spoo_task(void) {
  /* The ticks it waits for a message before it prints the deferred
   * ones instead (structures.md, "Others"). */
  static const uint32_t WAIT_TICKS = 0x400;

  psos_supervisor();
  for (;;) {
    spoo_receive_error =
        (uint8_t)psos_request_x(spoo_exchange, &spoo_message, 0, WAIT_TICKS);
    watchdog_spool++;
    if (spoo_receive_error == 0) {
      spoo_message.function(spoo_message.arguments[0],
                            spoo_message.arguments[1],
                            spoo_message.arguments[2]);
    } else if (spoo_receive_error == PSOS_ERROR_TIMEOUT) {
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
