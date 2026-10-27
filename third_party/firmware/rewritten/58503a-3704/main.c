/* The system's start, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `applications/main`): root_task, the kernel's first task, which
 * creates and starts the others.  system_init, which the decompiler
 * reads as the image has it, stays its.  The exported C includes this
 * file after its own, so the types and prototypes it declares are in
 * scope; each function here keeps its generated prototype. */

/* The root task: sets the kernel and status system up, creates the
 * exchanges and tasks and starts them, then counts its idle loops
 * forever (0x1321e).  Unlike the Z3805A's, it erases the log when
 * nv_restore fell back to the default settings, and creates the front
 * panel's task, fpnl, where front_panel_check finds the panel, quietly;
 * it starts fpnl whether or not it created it. */
void root_task(void) {
  /* pllc_exchange's create_x arguments, which psos.md leaves unnamed. */
  static const uint32_t PLLC_EXCHANGE_A = 2;
  static const uint32_t PLLC_EXCHANGE_B = 8;
  /* Each task's stacks in words, supervisor and user (psos.md,
   * "spawn"). */
  static const uint32_t TASK_SUPERVISOR_STACK_WORDS = 0;
  static const uint32_t TASK_USER_STACK_WORDS = 0x400;
  static const uint32_t GPSM_PRIORITY = 100;
  static const uint32_t HMON_PRIORITY = 200;
  static const uint32_t PLLP_PRIORITY = 175;
  static const uint32_t CURV_PRIORITY = 25;
  static const uint32_t FPNL_PRIORITY = 95;
  static const uint32_t SPOO_PRIORITY = 75;
  static const uint32_t KLOK_PRIORITY = 150;
  /* front_panel_check's argument for no report (0x13350). */
  static const char FRONT_PANEL_QUIET = 0;

  psos_debug_output(psos_debug_enabled);
  idle_loops = 0;
  task_root = psos_my_pid();
  const uint16_t sr = cpu_enter_supervisor();
  periodic_interrupt_enable(1);
  cpu_sr_write(sr);
  kernel_running = 1;
  eeprom_init();
  status_init();
  log_init();
  if (nv_settings_defaulted != 0) {
    log_erase_locked(REQUEST_SOURCE_INTERNAL);
    log_clear();
  }

  psos_create_x("gpsx", 0, 0, &gpsx_exchange);
  psos_create_x("plla", 0, 0, &plla_exchange);
  psos_create_x("pllc", PLLC_EXCHANGE_A, PLLC_EXCHANGE_B, &pllc_exchange);
  psos_create("gpsm", 0, GPSM_PRIORITY, TASK_SUPERVISOR_STACK_WORDS,
              TASK_USER_STACK_WORDS, NULL, &task_gpsm);
  psos_create("hmon", 0, HMON_PRIORITY, TASK_SUPERVISOR_STACK_WORDS,
              TASK_USER_STACK_WORDS, NULL, &task_hmon);
  psos_create("pllp", 0, PLLP_PRIORITY, TASK_SUPERVISOR_STACK_WORDS,
              TASK_USER_STACK_WORDS, NULL, &task_pllp);
  psos_create("curv", 0, CURV_PRIORITY, TASK_SUPERVISOR_STACK_WORDS,
              TASK_USER_STACK_WORDS, NULL, &task_curv);
  if (front_panel_check(FRONT_PANEL_QUIET) != 0) {
    psos_create("fpnl", 0, FPNL_PRIORITY, TASK_SUPERVISOR_STACK_WORDS,
                TASK_USER_STACK_WORDS, NULL, &task_fpnl);
  }
  spoo_task_create(SPOO_PRIORITY);
  klok_task_create(KLOK_PRIORITY);
  psos_start(task_fpnl, front_panel_task);
  psos_start(task_gpsm, gpsm_task);
  psos_start(task_hmon, hmon_task);
  psos_start(task_pllp, pllp_task);
  psos_start(task_curv, curv_task);
  if (nv_settings.pforth_at_startup == 0) {
    scpi_task_start();
  } else {
    console_init();
  }

  /* klok_take_pending reads and clears the count from another task. */
  for (;;) {
    cpu_long_increment(&idle_loops);
  }
}
