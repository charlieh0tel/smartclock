/* From the Z3815A 4010's rewritten/scpi_engine.c, the Z3817A 3752's own beside
 * common/'s: of its functions, those the Z3817A's code carries identical or
 * relocated, and scpi_input_source, this image's own
 * (docs/firmware/z3817a-3752.md, "The function check").  Its comments'
 * addresses are the Z3817A's, but where they name another image.  Each keeps
 * its generated prototype. */

/* The SCPI task's arena, the segment it takes for scpi_interpret, and
 * the priority it gives the console when it hands it to pForth
 * (0x39c60, 0x39d00). */
static const uint32_t SCPI_TASK_ARENA_SIZE = 2000;

static const uint32_t SCPI_TASK_CONSOLE_PRIORITY = 0x33;

/* The SCPI task's body for a port: time sliced, takes a segment for the
 * interpreter's arena, turns the port's prompt on and runs the
 * interpreter on the port's session over the trees given until it ends;
 * then gives the console to pForth or enters the installer, as
 * :SYSTem:LANGuage chose, and deletes itself.  write and print report
 * its two failures, after which it returns; the 58503A's (0x39c3c). */
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

/* Creates and starts the SCPI task, the SCI session's, "sci", at a
 * priority, as the Z3815A's sci_scpi_task_create does its SCI session's;
 * -1 when it exists, else the kernel's error (0x39d72). */
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

/* Closes the session's port, the SCI, device 0, here (0x396ee), the
 * Z3815A's DUART channel B. */
void scpi_session_close(void) { scpi_port_close(); }

/* Reports a query's failure, the condition code, for the source its
 * input gives, and clears its query word, so that scpi_interpret
 * formats no response (0x395fa). */
int32_t scpi_query_failed(status_condition_code code, scpi_call* call) {
  status_report_from(code, scpi_input_source(call->input));
  call->query = 0;
  return 0;
}

/* Clears the session's input, sets the receive errors
 * port_report_errors reports for port 1, the SCI's SCSR bits, as the
 * Z3815A's SCI session's (the fourth, IDLE, one the SCI's receive errors
 * never hold), opens the port and prompts; the session's object
 * (0x3967a). */
scpi_port* sci_scpi_session_init(void) {
  scpi_echo_length = 0;
  port_error_reports[0].mask = SCSR_FE;
  port_error_reports[1].mask = SCSR_OR;
  port_error_reports[2].mask = SCSR_PF;
  port_error_reports[3].mask = SCSR_IDLE;
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

/* The SCPI task, "sci": the session's, on its status model, writing its
 * failures to the console (0x39d46). */
void sci_scpi_task(void) {
  scpi_task_run(SCPI_PORT_SCI, scpi_status_model, scpi_trees.main,
                scpi_trees.common, console_write, printf);
}

/* The source a command's input port gives: REQUEST_SOURCE_SCI whatever
 * the port, the image's one session the SCI's (0x39ac0), where the
 * Z3815A's gives REQUEST_SOURCE_SCPI for its SCPI port's object. */
int32_t scpi_input_source(scpi_port* input) { return REQUEST_SOURCE_SCI; }
