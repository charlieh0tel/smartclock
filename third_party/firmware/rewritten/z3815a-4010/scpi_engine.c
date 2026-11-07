/* scpi_engine.c's functions that are this image's own: the rest are
 * common/scpi_engine.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The SCPI task's arena, the segment it takes for scpi_interpret, and
 * the priority it gives the console when it hands it to pForth
 * (0x3c9fe, 0x3ca9e). */
static const uint32_t SCPI_TASK_ARENA_SIZE = 2000;

static const uint32_t SCPI_TASK_CONSOLE_PRIORITY = 0x33;

/* Closes the SCPI port, device 3, DUART channel B, here (0x3c0e4). */
void scpi_session_close(void) { duartb_port_close(); }

/* The source a command's input port gives: REQUEST_SOURCE_SCPI for the
 * SCPI port's object, REQUEST_SOURCE_SCI for any other, the SCI
 * session's (0x3c84e). */
int32_t scpi_input_source(scpi_port* input) {
  return input == scpi_console_port() ? REQUEST_SOURCE_SCPI
                                      : REQUEST_SOURCE_SCI;
}

/* Reports a query's failure, the condition code, for the source its
 * input gives, and clears its query word, so that scpi_interpret
 * formats no response (0x3bff0). */
int32_t scpi_query_failed(status_condition_code code, scpi_call* call) {
  status_report_from(code, scpi_input_source(call->input));
  call->query = 0;
  return 0;
}

/* Clears the SCPI port's input, sets the receive errors
 * port_report_errors reports, opens DUART channel B and prompts; the
 * SCPI port's object (0x3c070). */
scpi_port* scpi_session_init(void) {
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
  duartb_port_open();
  scpi_prompt(0);
  status_update_rqs();
  return SCPI_CONSOLE_PORT;
}

/* Clears the SCI session's input, sets the receive errors
 * port_report_errors reports for port 1, the SCI's SCSR bits (the
 * fourth, IDLE, one the SCI's receive errors never hold: 0x30010 keeps
 * OR, FE and PF, 0x3010c sets OR), opens the console and prompts; the
 * SCI session's object (0x3c37a). */
scpi_port* sci_scpi_session_init(void) {
  sci_scpi_echo_length = 0;
  sci_scpi_error_reports[0].mask = SCSR_FE;
  sci_scpi_error_reports[1].mask = SCSR_OR;
  sci_scpi_error_reports[2].mask = SCSR_PF;
  sci_scpi_error_reports[3].mask = SCSR_IDLE;
  sci_scpi_error_reports[0].condition = STATUS_FRAMING_ERROR;
  sci_scpi_error_reports[1].condition = STATUS_INPUT_OVERRUN;
  sci_scpi_error_reports[2].condition = STATUS_PARITY_ERROR;
  sci_scpi_error_reports[3].condition = STATUS_COMMUNICATION_ERROR;
  sci_scpi_input_repeat = 0;
  sci_scpi_input_end = sci_scpi_input_line;
  sci_scpi_input_next = sci_scpi_input_line;
  sci_scpi_input_line[0] = 0;
  scpi_port_open();
  sci_scpi_prompt(0);
  sci_status_update_rqs();
  return SCI_SCPI_PORT;
}

/* The SCPI task's body for a port: time sliced, takes a segment for the
 * interpreter's arena, turns the port's prompt on and runs the
 * interpreter on the port's session over the trees given until it ends;
 * then gives the console to pForth or enters the installer, as
 * :SYSTem:LANGuage chose, and deletes itself.  write and print report
 * its two failures, after which it returns; the 58503A's (0x3c9da). */
void scpi_task_run(char port, status_model* status, scpi_child_list* main,
                   scpi_child_list* common, scpi_task_write_function* write,
                   scpi_task_print_function* print) {
  /* scpi_interpret's last argument, unread, is the long above its
   * others: the task's D2, as it saved it. */
  const uint32_t caller_d2 = CPU_CALLERS_REGISTER(uint32_t, "d2");
  void* arena;

  psos_mode(PSOS_MODE_TIME_SLICE, PSOS_MODE_TIME_SLICE, NULL);
  const uint32_t error =
      psos_service_8(SCPI_TASK_ARENA_SIZE, 0, 1, 0, (uint32_t*)&arena) &
      OS_ERROR_BITS;

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
  scpi_interpret(input, input, arena, SCPI_TASK_ARENA_SIZE, main, common,
                 status, 0, caller_d2);
  psos_free_segment(arena);
  input->methods->close(input);
  if (scpi_language == SCPI_LANGUAGE_PFORTH) {
    psos_priority(OS_TASK_SELF, SCPI_TASK_CONSOLE_PRIORITY, NULL);
    spoo_run((spoo_function*)console_init, 0, 0, 0);
  } else if (scpi_language == SCPI_LANGUAGE_INSTALL) {
    enter_installer();
  }
  psos_delete(OS_TASK_SELF);
}

/* The SCI's SCPI task, "sci": the SCI session's, on its status model,
 * writing its failures to the console (0x3cae4). */
void sci_scpi_task(void) {
  scpi_task_run(SCPI_PORT_SCI, sci_scpi_status_model, scpi_trees.main,
                scpi_trees.common, console_write, printf);
}

/* The SCPI task, "scpi": the SCPI port's, on scpi_status_model, writing
 * its failures to DUART channel B (0x3cb10). */
void scpi_task(void) {
  scpi_task_run(SCPI_PORT_DUART_B, scpi_status_model, scpi_trees.main,
                scpi_trees.common, (scpi_task_write_function*)duartb_port_write,
                (scpi_task_print_function*)duartb_printf);
}

/* Creates and starts the SCPI task, "scpi", at a priority; -1 when it
 * exists, else the kernel's error (0x3cbb6). */
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

/* Creates and starts the SCI session's task, "sci", at a priority, as
 * scpi_task_create does the SCPI port's; -1 when it exists, else the
 * kernel's error (0x3cb3c). */
uint32_t sci_scpi_task_create(uint32_t priority) {
  uint32_t existing;

  if ((uint8_t)psos_ident("sci", &existing, 0) == 0) {
    return -1;
  }
  uint32_t error =
      psos_create("sci", 0, priority, SCPI_TASK_SUPERVISOR_STACK_WORDS,
                  SCPI_TASK_USER_STACK_WORDS, 0, &task_sci) &
      OS_ERROR_BITS;

  if (error != 0) {
    return error;
  }
  return psos_start(task_sci, sci_scpi_task) & OS_ERROR_BITS;
}

/* Has spoo create the SCI session's task, at priority 0x19, and leaves
 * the console to it, as scpi_task_start does the SCPI port's
 * (0x230f6). */
void sci_scpi_task_start(void) {
  /* spoo_run calls it with three longs; it reads the first. */
  spoo_run((spoo_function*)sci_scpi_task_create, SCPI_TASK_PRIORITY, 0, 0);
  task_console = 0;
}
