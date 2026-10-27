/* installer/installer_scpi_engine.c's functions that are this image's own: the
 * rest are common/installer/installer_scpi_engine.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* The source a command's input port gives: always 0
 * (installer::0x1077a0). */
int32_t installer_return_zero(scpi_port* input) { return 0; }

/* Clears the SCPI input, sets the receive errors
 * installer_port_report_errors reports, opens the port and prompts; the
 * console port (installer::0x1073d8). */
scpi_port* installer_scpi_session_init_58503a(void) {
  *installer_scpi_echo_length() = 0;
  installer_port_error_reports[0].mask = SCPI_CONSOLE_BREAK_OR_FRAMING;
  installer_port_error_reports[1].mask = SCPI_CONSOLE_OVERRUN;
  installer_port_error_reports[2].mask = SCPI_CONSOLE_PARITY;
  installer_port_error_reports[3].mask = SCPI_CONSOLE_COMMUNICATION;
  installer_port_error_reports[0].condition = INSTALLER_STATUS_FRAMING_ERROR;
  installer_port_error_reports[1].condition = INSTALLER_STATUS_INPUT_OVERRUN;
  installer_port_error_reports[2].condition = INSTALLER_STATUS_PARITY_ERROR;
  installer_port_error_reports[3].condition =
      INSTALLER_STATUS_COMMUNICATION_ERROR;
  installer_scpi_input_repeat = 0;
  installer_scpi_input_end = installer_scpi_input_line;
  installer_scpi_input_next = installer_scpi_input_line;
  installer_scpi_input_line[0] = 0;
  installer_scpi_port_open();
  installer_scpi_prompt(0);
  installer_status_update_rqs();
  return &installer_console_port;
}

/* The installer's SCPI task body for a port: time sliced, takes a
 * 1500-byte segment for the interpreter's arena, turns the port's
 * prompt on and runs the interpreter on the port's session over the
 * trees given until it ends; then restarts the processor, whatever
 * :SYSTem:LANGuage chose, and deletes itself.  write and print report
 * its two failures, after which it returns (installer::0x10791c). */
void installer_scpi_task_run(char port, status_model* status,
                             scpi_child_list* main, scpi_child_list* common,
                             scpi_task_write_function* write,
                             scpi_task_print_function* print) {
  /* installer_scpi_interpret's last argument, unread, is the long above
   * its others: the task's D2, as it saved it. */
  const uint32_t caller_d2 = CPU_CALLERS_REGISTER(uint32_t, "d2");
  static const uint32_t ARENA_SIZE = 1500;
  void* arena;

  installer_psos_mode(PSOS_MODE_TIME_SLICE, PSOS_MODE_TIME_SLICE, NULL);
  const uint32_t error =
      installer_psos_service_8(ARENA_SIZE, 0, 1, 0, (uint32_t*)&arena) &
      OS_ERROR_BITS;

  if (error != 0) {
    print("scpi: alloc_seg error %#0x\n", error);
    return;
  }
  installer_port_prompt_set(port, 1);
  scpi_port* input = installer_port_session_init(port);

  if (input == NULL) {
    write("scpi: error opening stream\n");
    return;
  }
  installer_scpi_interpret(input, input, arena, ARENA_SIZE, main, common,
                           status, 0, caller_d2);
  installer_psos_free_segment(arena);
  input->methods->close(input);
  installer_restart();
  installer_psos_delete(OS_TASK_SELF);
}

/* The installer's SCPI task: the console port's, on
 * installer_status_object (installer::0x1079ec). */
void installer_scpi_task(void) {
  installer_scpi_task_run(
      SCPI_PORT_DUART_B, installer_status_object, installer_scpi_trees.main,
      installer_scpi_trees.common, installer_console_write, installer_printf);
}

/* Creates and starts the SCPI task at a priority; -1 when it exists,
 * else the kernel's error (installer::0x107a18). */
uint32_t installer_scpi_task_create(uint32_t priority) {
  uint32_t existing;

  if ((uint8_t)installer_psos_ident("scpi", &existing, 0) == 0) {
    return -1;
  }
  uint32_t error = installer_psos_create(
                       "scpi", 0, priority, SCPI_TASK_SUPERVISOR_STACK_WORDS,
                       SCPI_TASK_USER_STACK_WORDS, 0, &installer_task_scpi) &
                   OS_ERROR_BITS;

  if (error != 0) {
    return error;
  }
  return installer_psos_start(installer_task_scpi, installer_scpi_task) &
         OS_ERROR_BITS;
}
