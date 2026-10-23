/* The installer's copies of the spool task's start and its runner
 * (`services/spool`; spool.c has the primary's), on the installer's
 * spool task, exchange and flag.  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps
 * its generated prototype. */

/* Creates the spool task at a priority, unless one is running, with its
 * exchange, and starts it; returns 0, else the kernel's error, or 1 for
 * a spool task already there (installer::0x103efe). */
uint32_t installer_spoo_task_create(uint32_t priority) {
  /* The spool task's name. */
  static const char NAME[] = "spoo";
  uint32_t error = (uint8_t)installer_psos_ident((char*)NAME, 0, 0) == 0;
  char text[80];

  if (error == 0) {
    error = (uint8_t)installer_psos_create_x((char*)NAME, SPOOL_EXCHANGE_A,
                                             SPOOL_EXCHANGE_B,
                                             &installer_spoo_exchange);
  }
  if (error == 0) {
    error = (uint8_t)installer_psos_create(
        (char*)NAME, 0, priority, SPOOL_SUPERVISOR_STACK, SPOOL_USER_STACK, 0,
        &installer_task_spoo);
  }
  if (error == 0) {
    error =
        (uint8_t)installer_psos_start(installer_task_spoo, installer_spoo_task);
  }
  if (error == 0) {
    installer_spoo_started = 1;
  } else {
    installer_sprintf(text, "Spool_go error %#04x", error);
    installer_fatal_error(text);
  }
  return error;
}

/* Runs a function with three arguments: itself before the spool task
 * starts, returning 1, and after by sending it to the spool task,
 * returning send_x's error; 0 for no function (installer::0x103fce). */
uint32_t installer_spoo_run(spoo_function* function, uint32_t a, uint32_t b,
                            uint32_t c) {
  spoo_request request;

  if (function == 0) {
    return 0;
  }
  if (!installer_spoo_started) {
    function(a, b, c);
    return 1;
  }
  request.field_04 = 0;
  request.function = function;
  request.arguments[0] = a;
  request.arguments[1] = b;
  request.arguments[2] = c;
  return (uint8_t)installer_psos_send_x(installer_spoo_exchange, &request);
}

/* The installer's spool task: runs each function installer_spoo_run
 * sends.  Unlike spoo_task it services no watchdog and prints no
 * deferred messages, and keeps the request's error as a long
 * (installer::0x10403a). */
void installer_spoo_task(void) {
  installer_psos_supervisor();
  for (;;) {
    installer_spoo_receive_error = (uint8_t)installer_psos_request_x(
        installer_spoo_exchange, &installer_spoo_message, 0, SPOOL_WAIT_TICKS);
    if (installer_spoo_receive_error == 0) {
      installer_spoo_message.function(installer_spoo_message.arguments[0],
                                      installer_spoo_message.arguments[1],
                                      installer_spoo_message.arguments[2]);
    }
  }
}
