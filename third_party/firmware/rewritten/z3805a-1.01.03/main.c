/* The Z3805A 1.01.03's root task, the 58503A 3704's in its shape without
 * the front panel's task: the decompiler's reading drops the status
 * register it restores and the count its endless loop keeps
 * (docs/firmware/z3805a-1.01.03.md, "What fails").  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C");
 * it keeps its generated prototype. */

/* The root task: sets the kernel and status system up, creates the
 * exchanges and tasks and starts them, then counts its idle loops
 * forever (0x131b2).  It erases the log when nv_restore fell back to
 * the default settings (0x1321e), as the 58503A's does, and has no
 * front panel's task. */
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
  for (;;) {
    cpu_long_increment(&idle_loops);
  }
}

/* system_init's set-up of the state the other images restore from the
 * preserved region on a warm start: the clock, afresh when forced,
 * retained_block's defaults, gps, pll and the ADC monitors (0x12d26).
 * system_init calls it on every start, forced on a cold one. */
void system_defaults_init(int16_t force) {
  clock_init(force);
  memcpy(retained_block, retained_block_defaults,
         sizeof retained_block_defaults);
  gps_init();
  pll_init();
  monitors_reset();
}

/* Sets the system up before the kernel runs: the status system marked
 * not ready, the records and settings read, every task's debug messages
 * on, and the defaults by system_defaults_init, which this image runs
 * on every start: nothing is restored from the preserved region, whose
 * restore word only says what start this was.  A good region after a
 * reset neither external nor power-up, the settings not defaulted and
 * the word RESTORE: a warm start, the defaults unforced, the word
 * REBOOTED, the reset's cause logged and then the clock started afresh
 * all the same (0x12e94).  A good region
 * after the RESET instruction's reset alone with the word DISCARD: a
 * cold start, the word kept.  Else a cold start, the word RESTORE, and
 * the FPGA's PROGRAM pulsed (0x12d68). */
void system_init(void) {
  /* RSR's EXT and POW, bits 7 and 6: an external or a power-up reset;
   * SYS, bit 1, alone: the RESET instruction's (MC68331UM D.3.4;
   * restart.md). */
  static const uint8_t RESET_EXTERNAL_OR_POWER_UP = 0xc0;
  static const uint8_t RESET_SYSTEM_ONLY = 0x02;
  /* sample_values' defaults run on through the descriptors' values to
   * retained_block's (0x12da8). */
  static const size_t SAMPLE_DEFAULTS_SIZE = 0xbe;
  static const int16_t CLOCK_AFRESH = 1;
  bool warm_start;

  kernel_running = 0;
  status_ready = 0;
  happening_buffers_clear();
  eeprom_lock_init();
  calibration_block_read();
  parameter_record_read();
  identification_record_read();
  hmon_tick_countdown = 0;
  memcpy(sample_values, sample_values_defaults, SAMPLE_DEFAULTS_SIZE);
  nv_restore();
  debug_enabled = nv_settings.debug_output;
  psos_debug_enabled = debug_enabled;
  lifetime_record_read();

  for (int8_t i = 0; i < sizeof dmes / sizeof dmes[0]; i++) {
    dmes[i] = 1;
  }

  /* RSR is read again for the cold start's test, as the image does. */
  const uint16_t checksum =
      record_checksum(&preserved_region, sizeof preserved_region);
  if (checksum == preserved_region.checksum &&
      (sim.rsr & RESET_EXTERNAL_OR_POWER_UP) == 0 && !nv_settings_defaulted &&
      preserved_region.restore == PRESERVED_REGION_RESTORE) {
    system_defaults_init(0);
    preserved_region.restore = PRESERVED_REGION_REBOOTED;
    warm_start = true;
  } else if (checksum == preserved_region.checksum &&
             sim.rsr == RESET_SYSTEM_ONLY &&
             preserved_region.restore == PRESERVED_REGION_DISCARD) {
    system_defaults_init(CLOCK_AFRESH);
    preserved_region.restore = PRESERVED_REGION_DISCARD;
    warm_start = false;
  } else {
    system_defaults_init(CLOCK_AFRESH);
    preserved_region.restore = PRESERVED_REGION_RESTORE;
    warm_start = false;
    sim.porte0 &= ~FPGA_PROGRAM;
    sim.porte0 |= FPGA_PROGRAM;
  }

  if (warm_start) {
    log_reset_cause();
    clock_init(CLOCK_AFRESH);
  } else {
    exception_record_clear();
  }
  pps_monitors_reset();
  timecode_init();
  selftest_init();
}
