/* The system's start, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `applications/main`): system_init, which primary_start runs before
 * the kernel, restores the preserved region on a warm start and reads
 * the records, and root_task, the kernel's first task, which creates
 * and starts the others.  The exported C includes this file after its
 * own, so the types and prototypes it declares are in scope; each
 * function here keeps its generated prototype.  The preserved region and
 * the warm start are docs/firmware/restart.md's and structures.md's. */

/* Sets the system up before the kernel runs: the status system marked
 * not ready, the records and settings read, every task's debug messages
 * on, and gps, pll and the retained block restored from the preserved
 * region on a warm start or set to their defaults on a cold one
 * (0x12bb4). */
void system_init(void) {
  /* RSR's EXT and POW, bits 7 and 6: an external or a power-up reset
   * (MC68331UM D.3.4; restart.md). */
  static const uint8_t RESET_EXTERNAL_OR_POWER_UP = 0xc0;
  /* preserved_region.restore's value for a region to restore. */
  static const uint16_t RESTORE = 1;

  kernel_running = 0;
  status_ready = 0;
  happening_buffers_clear();
  eeprom_lock_init();
  parameter_record_read();
  identification_record_read();
  hmon_tick_countdown = 0;
  /* The block runs on from sample_values through the descriptors'
   * values, scpi_value_SAMPle_BOOLean on, as long as its defaults. */
  memcpy(sample_values, sample_values_defaults, sizeof sample_values_defaults);
  nv_restore();
  debug_enabled = nv_settings.debug_output;
  psos_debug_enabled = debug_enabled;
  lifetime_record_read();
  clock_init(selftest.eeprom);

  for (int8_t i = 0; i < sizeof dmes / sizeof dmes[0]; i++) {
    dmes[i] = 1;
  }

  const bool region_good =
      record_checksum(&preserved_region, sizeof preserved_region) ==
          preserved_region.checksum &&
      (sim.rsr & RESET_EXTERNAL_OR_POWER_UP) == 0;
  if (region_good && preserved_region.restore == RESTORE) {
    memcpy(&retained_block, preserved_region.values,
           sizeof preserved_region.values);
    gps = preserved_region.gps;
    pll = preserved_region.pll;
    preserved_region.restore = RESTORE;
    warm_start = true;
  } else {
    memcpy(&retained_block, retained_block_defaults,
           sizeof retained_block_defaults);
    gps_init();
    pll_init();
    if (region_good) {
      preserved_region.restore = 0;
      warm_start = false;
    } else {
      preserved_region.restore = RESTORE;
      warm_start = false;
      sim.porte0 &= ~FPGA_PROGRAM;
      sim.porte0 |= FPGA_PROGRAM;
    }
  }

  if (warm_start) {
    log_reset_cause();
  } else {
    exception_record_clear();
  }
  pps_monitors_reset();
  timecode_init();
  selftest_init();
  calibration_block_read();
}

/* The root task: sets the kernel and status system up, creates the
 * exchanges and tasks and starts them, then counts its idle loops
 * forever (0x1304a). */
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
  static const uint32_t SPOO_PRIORITY = 75;
  static const uint32_t KLOK_PRIORITY = 150;

  psos_debug_output(psos_debug_enabled);
  idle_loops = 0;
  task_root = psos_my_pid();
  const uint16_t sr = enter_supervisor();
  periodic_interrupt_enable(1);
  cpu_sr_write(sr);
  kernel_running = 1;
  eeprom_init();
  status_init();
  log_init();
  if (selftest.eeprom != 0) {
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
  spoo_task_create(SPOO_PRIORITY);
  klok_task_create(KLOK_PRIORITY);
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
  volatile uint32_t* const loops = &idle_loops;
  for (;;) {
    (*loops)++;
  }
}
