/* scpi_engine.c's functions that are this image's own: the rest are
 * common/scpi_engine.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Reports a query's failure, the condition code, for the source its
 * input gives, and clears its query word, so that scpi_interpret
 * formats no response (0x28d70). */
int32_t scpi_query_failed(status_condition_code code, scpi_call* call) {
  status_report_from(code, return_zero(call->input));
  call->query = 0;
  return 0;
}

/* Clears the SCPI input, sets the receive errors port_report_errors
 * reports, opens the port and prompts; the console port (0x28df0). */
scpi_port* scpi_session_init_58503a(void) {
  scpi_echo_length = 0;
  port_error_reports[0].mask = SCPI_CONSOLE_BREAK_OR_FRAMING;
  port_error_reports[1].mask = SCPI_CONSOLE_OVERRUN;
  port_error_reports[2].mask = SCPI_CONSOLE_PARITY;
  port_error_reports[3].mask = SCPI_CONSOLE_COMMUNICATION;
  port_error_reports[0].condition = STATUS_FRAMING_ERROR;
  port_error_reports[1].condition = STATUS_INPUT_OVERRUN;
  port_error_reports[2].condition = STATUS_PARITY_ERROR;
  port_error_reports[3].condition = STATUS_COMMUNICATION_ERROR;
  scpi_input_repeat = 0;
  scpi_input_end = scpi_input_line;
  scpi_input_next = scpi_input_line;
  scpi_input_line[0] = 0;
  scpi_port_open();
  scpi_prompt(0);
  status_update_rqs();
  return SCPI_CONSOLE_PORT;
}

/* The SCPI task's body for a port: time sliced, takes a 2000-byte
 * segment for the interpreter's arena, turns the port's prompt on and
 * runs the interpreter on the port's session over the trees given until
 * it ends; then gives the console to pForth, at priority 0x33, or enters
 * the installer, as :SYSTem:LANGuage chose, and deletes itself.  write
 * and print report its two failures, after which it returns (0x293c2). */
void scpi_task_run(char port, status_model* status, scpi_child_list* main,
                   scpi_child_list* common, scpi_task_write_function* write,
                   scpi_task_print_function* print) {
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
    print("scpi: alloc_seg error %#0x\n", error);
    return;
  }
  port_prompt_set(port, 1);
  scpi_port* input = port_session_init(port);

  if (input == NULL) {
    write("scpi: error opening stream\n");
    return;
  }
  scpi_interpret(input, input, arena, ARENA_SIZE, main, common, status, 0,
                 caller_d2);
  psos_free_segment(arena);
  input->methods->close(input);
  if (scpi_language == SCPI_LANGUAGE_PFORTH) {
    psos_priority(OS_TASK_SELF, CONSOLE_PRIORITY, NULL);
    spoo_run((spoo_function*)console_init, 0, 0, 0);
  } else if (scpi_language == SCPI_LANGUAGE_INSTALL) {
    enter_installer();
  }
  psos_delete(OS_TASK_SELF);
}

/* The SCPI task: the console port's, on status_object (0x294cc). */
void scpi_task(void) {
  scpi_task_run(SCPI_PORT_DUART_B, scpi_status_model, scpi_trees.main,
                scpi_trees.common, console_write, printf);
}

/* Creates and starts the SCPI task at a priority; -1 when it exists,
 * else the kernel's error (0x294f8). */
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
