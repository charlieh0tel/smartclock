/* From the 58503B 1.01.04's rewritten/scpi_engine.c and the Z3815A 4010's
 * rewritten/scpi_engine.c, the Z3805A 1.01.03's own beside common/'s: of
 * its functions, those the Z3805A 1.01.03's code carries identical or
 * relocated (docs/firmware/z3805a-1.01.03.md, "The link list").  Its
 * comments' addresses are the Z3805A 1.01.03's, but where they name
 * another image.  Each keeps its generated prototype. */

/* Closes the SCPI port; scpi_task calls it when scpi_interpret returns
 * (0x2a292). */
void scpi_session_close(void) { scpi_port_close(); }

/* The SCPI task's body for a port: time sliced, takes a 2000-byte
 * segment for the interpreter's arena, turns the port's prompt on and
 * runs the interpreter on the port's session over the trees given until
 * it ends; then gives the console to pForth, at priority 0x33, or enters
 * the installer, as :SYSTem:LANGuage chose, and deletes itself.  write
 * and print report its two failures, after which it returns (0x2a7f4). */
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
 * formats no response (0x2a19e). */
int32_t scpi_query_failed(status_condition_code code, scpi_call* call) {
  status_report_from(code, scpi_input_source(call->input));
  call->query = 0;
  return 0;
}

/* The SCPI task, "scpi": the SCPI port's, on scpi_status_model, writing
 * its failures to DUART channel B (0x2a8fe). */
void scpi_task(void) {
  scpi_task_run(SCPI_PORT_DUART_B, scpi_status_model, scpi_trees.main,
                scpi_trees.common, (scpi_task_write_function*)console_write,
                (scpi_task_print_function*)printf);
}

/* Creates and starts the SCPI task, "scpi", at a priority; -1 when it
 * exists, else the kernel's error (0x2a92a).  The seeding named it
 * sci_scpi_task_create, the 58503B's for its SCI session. */
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

/* The source a command's input port gives, REQUEST_SOURCE_SCPI whatever
 * the port (0x2a678): the one SCPI port is the console's, DUART B. */
int32_t scpi_input_source(scpi_port* input) {
  (void)input;
  return REQUEST_SOURCE_SCPI;
}
