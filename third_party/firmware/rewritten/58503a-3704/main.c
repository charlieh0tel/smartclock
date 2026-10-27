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
 * on, and gps, pll, the ADC monitors and the retained block restored
 * from the preserved region on a warm start or set to their defaults on
 * a cold one (0x12cfa).  Unlike the Z3805A's, it reads the calibration
 * block first, starts the clock by nv_settings_defaulted, takes no warm
 * start when the settings were defaulted, sets the PWMA output from
 * holdover_duration_exceeded on a warm one, and keeps the region's
 * restore flag clear only after a RESET instruction alone. */
void system_init(void) {
  /* RSR's EXT and POW, bits 7 and 6: an external or a power-up reset;
   * SYS, bit 1, alone: the RESET instruction's (MC68331UM D.3.4;
   * restart.md). */
  static const uint8_t RESET_EXTERNAL_OR_POWER_UP = 0xc0;
  static const uint8_t RESET_SYSTEM_ONLY = 0x02;
  /* sample_values' defaults run on through the descriptors' values,
   * scpi_value_SAMPle_BOOLean on (0x307a4); the export gives the label
   * no size. */
  static const size_t SAMPLE_DEFAULTS_SIZE = 0x9b;

  kernel_running = 0;
  status_ready = 0;
  happening_buffers_clear();
  eeprom_lock_init();
  calibration_block_read();
  parameter_record_read_58503a();
  identification_record_read();
  hmon_tick_countdown = 0;
  memcpy(sample_values, &sample_values_defaults, SAMPLE_DEFAULTS_SIZE);
  nv_restore();
  debug_enabled = nv_settings.debug_output;
  psos_debug_enabled = nv_settings.debug_output;
  lifetime_record_read();
  clock_init(nv_settings_defaulted);

  for (int8_t i = 0; i < sizeof dmes / sizeof dmes[0]; i++) {
    dmes[i] = 1;
  }

  /* RSR is read again for the cold start's test, as the image does. */
  const bool checksum_good =
      record_checksum(&preserved_region, sizeof preserved_region) ==
      preserved_region.checksum;
  if (checksum_good && (sim.rsr & RESET_EXTERNAL_OR_POWER_UP) == 0 &&
      !nv_settings_defaulted &&
      preserved_region.restore == PRESERVED_REGION_RESTORE) {
    memcpy(retained_block, preserved_region.values,
           sizeof preserved_region.values);
    gps = preserved_region.gps;
    pll = preserved_region.pll;
    memcpy(adc_monitors, preserved_region.adc_monitors, sizeof adc_monitors);
    preserved_region.restore = PRESERVED_REGION_RESTORE;
    warm_start = true;
    pwma_force_level(holdover_duration_exceeded != 0);
  } else {
    const bool system_reset =
        checksum_good && sim.rsr == RESET_SYSTEM_ONLY &&
        preserved_region.restore == PRESERVED_REGION_DISCARD;

    memcpy(retained_block, retained_block_defaults,
           sizeof retained_block_defaults);
    gps_init_58503a();
    pll_init();
    monitors_reset();
    if (system_reset) {
      preserved_region.restore = PRESERVED_REGION_DISCARD;
      warm_start = false;
    } else {
      preserved_region.restore = PRESERVED_REGION_RESTORE;
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
  pps_monitors_reset_58503a();
  timecode_init();
  selftest_init();
}

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
