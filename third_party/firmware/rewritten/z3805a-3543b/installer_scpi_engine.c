/* The installer's copies of the SCPI task's start (`applications/scpi/
 * engine`; scpi_engine.c has the primary's), on the installer's task.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Has the installer's spool create its SCPI task, at priority 0x19, and
 * leaves the console to it (installer::0x100d20). */
void installer_scpi_task_start(void) {
  static const uint32_t SCPI_TASK_PRIORITY = 0x19;

  /* spoo_run calls it with three longs; it reads the first. */
  installer_spoo_run((spoo_function*)installer_scpi_task_create,
                     SCPI_TASK_PRIORITY, 0, 0);
  installer_task_console = 0;
}

/* Creates and starts the SCPI task at a priority; -1 when it exists,
 * else the kernel's error (installer::0x107886). */
uint32_t installer_scpi_task_create(uint32_t priority) {
  static const uint32_t ERROR_BITS = 0xff;
  /* The SCPI task's stacks in words, supervisor and user (psos.md,
   * "spawn"). */
  static const uint32_t TASK_SUPERVISOR_STACK_WORDS = 0x300;
  static const uint32_t TASK_USER_STACK_WORDS = 0x400;
  uint32_t existing;

  if ((uint8_t)installer_psos_ident("scpi", &existing, 0) == 0) {
    return -1;
  }
  uint32_t error =
      installer_psos_create("scpi", 0, priority, TASK_SUPERVISOR_STACK_WORDS,
                            TASK_USER_STACK_WORDS, 0, &installer_task_scpi) &
      ERROR_BITS;

  if (error != 0) {
    return error;
  }
  return installer_psos_start(installer_task_scpi, installer_scpi_task) &
         ERROR_BITS;
}
