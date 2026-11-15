/* From the 58503A 3704's rewritten/main.c, the Z3816A 4001's own beside
 * common/'s: root_task, the kernel's first task, which creates and starts the
 * others, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"), whose reading drops the idle
 * loop's count.  Its comments' addresses are the Z3816A's.  The exported C
 * includes this file after its own, so the types and prototypes it declares
 * are in scope; each function here keeps its generated prototype. */

/* The root task: sets the kernel and status system up, creates the
 * exchanges and tasks and starts them, then counts its idle loops
 * forever (0x2315a).  The 58503A's but that this image has no front
 * panel and no fpnl task. */
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

/* Sets the system up before the kernel runs: the status system marked
 * not ready, the records and settings read, every task's debug
 * messages on, and gps, pll, the ADC monitors and the retained block
 * restored from the preserved region on a warm start or set to their
 * defaults on a cold one, then the calibration block read (0x22c7c).
 * The Z3815A 4010's but that it has no SCI session status to mark,
 * copies 0x95 bytes of sample_values' defaults and reads the
 * calibration block last (0x22e66). */
void system_init(void) {
  /* RSR's EXT and POW, bits 7 and 6: an external or a power-up reset;
   * SYS, bit 1, alone: the RESET instruction's (MC68331UM D.3.4;
   * restart.md). */
  static const uint8_t RESET_EXTERNAL_OR_POWER_UP = 0xc0;
  static const uint8_t RESET_SYSTEM_ONLY = 0x02;
  /* sample_values' defaults run on through the descriptors' values;
   * the export gives the label no size. */
  static const size_t SAMPLE_DEFAULTS_SIZE = 0x95;

  kernel_running = 0;
  status_ready = 0;
  happening_buffers_clear();
  eeprom_lock_init();
  parameter_record_read();
  identification_record_read();
  hmon_tick_countdown = 0;
  memcpy(sample_values, &sample_values_defaults, SAMPLE_DEFAULTS_SIZE);
  nv_restore();
  debug_enabled = nv_settings.debug_output;
  psos_debug_enabled = debug_enabled;
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
    warm_start = 1;
  } else {
    const bool system_reset =
        checksum_good && sim.rsr == RESET_SYSTEM_ONLY &&
        preserved_region.restore == PRESERVED_REGION_DISCARD;

    memcpy(retained_block, retained_block_defaults,
           sizeof retained_block_defaults);
    gps_init();
    pll_init();
    monitors_reset();
    if (system_reset) {
      preserved_region.restore = PRESERVED_REGION_DISCARD;
      warm_start = 0;
    } else {
      preserved_region.restore = PRESERVED_REGION_RESTORE;
      warm_start = 0;
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
