/* From the 58503A 3704's rewritten/scpi_engine.c and the Z3816A 4001's
 * rewritten/scpi_engine.c, the 58503B 1.01.04's own beside common/'s: of its
 * functions, those the 58503B's code carries identical or relocated
 * (docs/firmware/58503b-1.01.04.md, "The link list").  Its comments'
 * addresses are the 58503B's, but where they name another image.  Each keeps
 * its generated prototype. */

/* Closes the SCPI port; sci_scpi_task calls it when scpi_interpret returns
 * (0x38774). */
void scpi_session_close(void) { scpi_port_close(); }

/* The source a command's input port gives: always the SCI's, where the
 * 58503A's gives 0 (0x38b6a). */
int32_t scpi_input_source(scpi_port* input) { return REQUEST_SOURCE_SCI; }

/* The SCPI task's body for a port: time sliced, takes a 2000-byte
 * segment for the interpreter's arena, turns the port's prompt on and
 * runs the interpreter on the port's session over the trees given until
 * it ends; then gives the console to pForth, at priority 0x33, or enters
 * the installer, as :SYSTem:LANGuage chose, and deletes itself.  write
 * and print report its two failures, after which it returns (0x38ce6). */
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

/* Reports a query's failure, the condition code, for the source its
 * input gives, and clears its query word, so that scpi_interpret
 * formats no response (0x38680). */
int32_t scpi_query_failed(status_condition_code code, scpi_call* call) {
  status_report_from(code, scpi_input_source(call->input));
  call->query = 0;
  return 0;
}

/* Creates and starts the SCPI task, "sci", at a priority; -1 when it exists,
 * else the kernel's error (0x38e1c). */
uint32_t scpi_task_create(uint32_t priority) {
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

/* Clears the SCI session's input, sets the receive errors
 * port_report_errors reports for port 1, the SCI's SCSR bits (the
 * fourth, IDLE, one the SCI's receive errors never hold: 0x30010 keeps
 * OR, FE and PF, 0x3010c sets OR), opens the console and prompts; the
 * SCI session's object (0x38700). */
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
  scpi_input_repeat = 0;
  sci_scpi_input_end = scpi_input_line;
  sci_scpi_input_next = scpi_input_line;
  scpi_input_line[0] = 0;
  scpi_port_open();
  scpi_prompt(0);
  status_update_rqs();
  return SCPI_CONSOLE_PORT;
}

/* The SCI's SCPI task, "sci": the SCI session's, on its status model,
 * writing its failures to the console (0x38df0). */
void sci_scpi_task(void) {
  scpi_task_run(SCPI_PORT_SCI, sci_scpi_status_model, scpi_trees.main,
                scpi_trees.common, console_write, printf);
}
