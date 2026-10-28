/* scpi_engine.c's functions that are this image's own: the rest are
 * common/scpi_engine.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Reports a query's failure, the condition code, for the source its
 * input gives, and clears its query word, so that scpi_interpret
 * formats no response (0x29ade). */
int32_t scpi_query_failed(status_condition_code code, scpi_call* call) {
  status_report_from(code, return_zero(call->input));
  call->query = 0;
  return 0;
}

/* Clears the SCPI input, opens the port and prompts; the console port
 * (0x29b42). */
scpi_port* scpi_session_init(void) {
  scpi_echo_length = 0;
  scpi_input_repeat = 0;
  scpi_input_end = scpi_input_line;
  scpi_input_next = scpi_input_line;
  scpi_input_line[0] = 0;
  scpi_port_open();
  scpi_prompt(0);
  status_update_rqs();
  return SCPI_CONSOLE_PORT;
}

/* The SCPI task: time sliced, takes a 2000-byte segment for the
 * interpreter's arena and runs the interpreter on the console port until
 * its session ends; then gives the console to pForth, at priority 0x33,
 * or enters the installer, as :SYSTem:LANGuage chose, and deletes
 * itself (0x2a04a). */
void scpi_task(void) {
  /* scpi_interpret's last argument, unread, is the long above its
   * others: the task's D2, as it saved it. */
  const uint32_t caller_d2 = CPU_CALLERS_REGISTER(uint32_t, "d2");
  static const uint32_t ARENA_SIZE = 2000;
  static const uint32_t CONSOLE_PRIORITY = 0x33;
  void* arena;

  psos_mode(PSOS_MODE_TIME_SLICE, PSOS_MODE_TIME_SLICE, NULL);
  const uint32_t error =
      psos_service_8(ARENA_SIZE, 0, 1, 0, (uint32_t*)&arena) & OS_ERROR_BITS;

  if (error != 0) {
    printf("scpi: alloc_seg error %#0x\n", error);
    return;
  }
  scpi_prompt_enabled = 1;
  scpi_port* port = scpi_session_init();

  if (port == NULL) {
    console_write("scpi: error opening stream\n");
    return;
  }
  scpi_interpret(port, port, arena, ARENA_SIZE, scpi_trees.main,
                 scpi_trees.common, status_object, 0, caller_d2);
  psos_free_segment(arena);
  port->methods->close(port);
  if (scpi_language == SCPI_LANGUAGE_PFORTH) {
    psos_priority(OS_TASK_SELF, CONSOLE_PRIORITY, NULL);
    spoo_run((spoo_function*)console_init, 0, 0, 0);
  } else if (scpi_language == SCPI_LANGUAGE_INSTALL) {
    enter_installer();
  }
  psos_delete(OS_TASK_SELF);
}

/* Creates and starts the SCPI task at a priority; -1 when it exists,
 * else the kernel's error (0x29fce). */
uint32_t scpi_task_create(uint32_t priority) {
  uint32_t existing;

  if ((uint8_t)psos_ident("scpi", &existing, 0) == 0) {
    return -1;
  }
  uint32_t error =
      psos_create("scpi", 0, priority, SCPI_TASK_SUPERVISOR_STACK_WORDS,
                  SCPI_TASK_USER_STACK_WORDS, 0, &task_scpi) &
      OS_ERROR_BITS;

  if (error != 0) {
    return error;
  }
  return psos_start(task_scpi, scpi_task) & OS_ERROR_BITS;
}
