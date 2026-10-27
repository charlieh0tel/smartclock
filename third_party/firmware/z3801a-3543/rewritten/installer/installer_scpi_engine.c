/* installer/installer_scpi_engine.c's functions that are this image's own: the
 * rest are common/installer/installer_scpi_engine.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* Creates and starts the SCPI task at a priority; -1 when it exists,
 * else the kernel's error (installer::0x107886). */
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

/* Clears the SCPI input, opens the port and prompts; the console port
 * (installer::0x10742a). */
scpi_port* installer_scpi_session_init(void) {
  installer_scpi_echo_length = 0;
  installer_scpi_input_repeat = 0;
  installer_scpi_input_end = installer_scpi_input_line;
  installer_scpi_input_next = installer_scpi_input_line;
  installer_scpi_input_line[0] = 0;
  installer_scpi_port_open();
  installer_scpi_prompt(0);
  installer_status_update_rqs();
  return &installer_console_port;
}

/* The installer's SCPI task: time sliced, takes a 1500-byte segment for
 * the interpreter's arena and runs the interpreter on the console port
 * until its session ends; then restarts the processor, whatever
 * :SYSTem:LANGuage chose, and deletes itself (installer::0x107902). */
void installer_scpi_task(void) {
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
    installer_printf("scpi: alloc_seg error %#0x\n", error);
    return;
  }
  scpi_port* port = installer_scpi_session_init();

  if (port == NULL) {
    installer_console_write("scpi: error opening stream\n");
    return;
  }
  installer_scpi_interpret(
      port, port, arena, ARENA_SIZE, installer_scpi_trees.main,
      installer_scpi_trees.common, installer_status_object, 0, caller_d2);
  installer_psos_free_segment(arena);
  port->methods->close(port);
  installer_restart();
  installer_psos_delete(OS_TASK_SELF);
}
