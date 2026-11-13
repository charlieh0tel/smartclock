/* console.c's functions that are this image's own: the rest are
 * common/console.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  The words whose code differs from the Z3805A's, carried
 * from the Z3815A 4010's or the 58503A 3704's or this image's own, with
 * the static helpers and code templates they use: efc_wr, pll_rep,
 * efc_rep, efc_comp, efc_comp_debug, puck and duart_debug among them
 * (docs/firmware/z3817a-3752.md, "The export and the checks").  Its
 * comments' addresses are the Z3817A's, but where they name another
 * image. */

/* The words efc_wr and pr_efc: the loop's EFC set by efc_set, with its
 * readback's correction, from the value as it is, where the 58503A's
 * takes a 16-bit DAC value and sets pll.efc 16 times it (0x2b524,
 * 0x2b53a). */
void diag_efc_wr(uint32_t value) { efc_set(unsigned_to_float(value)); }

void diag_pr_efc(void) { printf("efc = %5f\r\n", float_to_double(pll.efc)); }

/* The loop's stage, named, by the jump table at 0x2b89a. */
static char* console_stage_name(pll_stage stage) {
  switch (stage) {
    case PLL_STAGE_INVALID:
      return "invalid";
    case PLL_STAGE_POWERUP:
      return "powerup";
    case PLL_STAGE_POWERUP_RECOVERY:
      return "powerup recovery";
    case PLL_STAGE_HOLDOVER:
      return "holdover";
    case PLL_STAGE_HOLDOVER_RECOVERY:
      return "holdover recovery";
    case PLL_STAGE_STARTUP_PLL:
      return "startup pll";
    case PLL_STAGE_NORMAL_PLL:
      return "normal pll";
    case PLL_STAGE_DIAG:
      return "diag";
    case PLL_STAGE_IDLE:
      return "idle";
    case PLL_STAGE_FATAL_ERROR:
      return "fatal error";
    default:
      return "unknown";
  }
}

/* Appends powerup's sub-state, named, by the jump table at 0x2b8e4. */
static void console_append_powerup_state(char* state) {
  switch (pll.powerup_state) {
    case PLL_POWERUP_START:
      strcat(state, ": start");
      break;
    case PLL_POWERUP_INNER_OVEN:
      strcat(state, ": internal oven warmup");
      break;
    case PLL_POWERUP_OUTER_OVEN:
      strcat(state, ": external oven warmup");
      break;
    case PLL_POWERUP_WAITING_FOR_GPS:
      strcat(state, ": warmed up, waiting for GPS");
      break;
    case PLL_POWERUP_COARSE:
      strcat(state, ": coarse");
      break;
    case PLL_POWERUP_FINE:
      strcat(state, ": fine");
      print_fine_state(pll.fine_state, state);
      break;
    case PLL_POWERUP_WAITING:
      strcat(state, ": waiting");
      break;
    case PLL_POWERUP_LEAP_SECONDS:
      strcat(state, ": calculating leapseconds");
      break;
    default:
      strcat(state, ": unknown");
      break;
  }
}

/* Appends holdover_recovery's state, named, by the jump table at
 * 0x2b9e8. */
static void console_append_recovery_state(char* state) {
  switch (pll.recovery_state) {
    case PLL_RECOVERY_WAITING:
      strcat(state, ": start");
      break;
    case PLL_RECOVERY_FINE:
      print_fine_state(pll.fine_state, state);
      break;
    case PLL_RECOVERY_CHECK_DELAY:
      strcat(state, ": waiting");
      break;
    case PLL_RECOVERY_LEAP_ACK:
      strcat(state, ": calculating leapseconds");
      break;
    case PLL_RECOVERY_CLOCK_CHECK:
      strcat(state, ": checking time");
      break;
    default:
      strcat(state, ": unknown");
      break;
  }
}

/* Appends gps_diag_sequence's state, named, by the jump table at
 * 0x2baa4. */
static void console_append_diag_state(char* state) {
  switch (pll.gps_diag_state) {
    case PLL_GPS_DIAG_RESET_SEND:
      strcat(state, ": reset send");
      break;
    case PLL_GPS_DIAG_RESET_ACK:
      strcat(state, ": gps reset ack");
      break;
    case PLL_GPS_DIAG_TEST_SEND:
      strcat(state, ": test send");
      break;
    case PLL_GPS_DIAG_TEST_ACK:
      strcat(state, ": gps test ack");
      break;
    case PLL_GPS_DIAG_SETTLE:
      strcat(state, ": stop meas");
      break;
    case PLL_GPS_DIAG_RUN:
      strcat(state, ": test");
      break;
    default:
      strcat(state, ": unknown");
      break;
  }
}

/* The words log_send and print_clk_list (0x2bb74, 0x2bb88). */
void diag_log_send(uint32_t number) {
  log_write("Log test message %d", number);
}

/* The word pr_pll: the loop's stage and sub-state, then the GPS init and
 * leap check sequences' states (0x2b85a). */
void diag_pr_pll(void) {
  /* -0x50(A6). */
  char state[80];
  char* to = state;
  const char* from = console_stage_name(pll.stage);

  console_write("PLL state: ");
  while ((*to++ = *from++) != '\0') {
  }
  switch (pll.stage) {
    case PLL_STAGE_POWERUP:
      console_append_powerup_state(state);
      break;
    case PLL_STAGE_HOLDOVER_RECOVERY:
      console_append_recovery_state(state);
      break;
    case PLL_STAGE_DIAG:
      console_append_diag_state(state);
      break;
    default:
      break;
  }
  printf("%s\r\n", state);
  print_gps_init_state();
  print_leap_check_state();
}

/* gps_leap_sequence's state, named (0x2b72e). */
void print_leap_check_state(void) {
  char* name;

  console_write("Leap chk state: ");
  switch (pll.leap_check_state) {
    case PLL_LEAP_CHECK_START:
      name = "leap start";
      break;
    case PLL_LEAP_CHECK_PENDING_SEND:
      name = "leap pending send";
      break;
    case PLL_LEAP_CHECK_PENDING_ACK:
      name = "leap pending ack";
      break;
    case PLL_LEAP_CHECK_WAIT:
      name = "leap wait";
      break;
    default:
      name = "unknown";
      break;
  }
  printf("%s\r\n", name);
}

/* The word adc_read: an ADC channel's raw byte, adc_read taking the
 * number for the channel, where the Z3805A's maps it (0x2ae80). */
uint32_t diag_adc_read(uint32_t number) {
  return (uint8_t)adc_read((adc_channel)number);
}

/* The word pr_adc_avg: the monitored average of each health monitor
 * channel marked for it, by its name (0x2ae96). */
void diag_pr_adc_avg(void) {
  static const uint32_t CHANNELS =
      sizeof(hmon_channels) / sizeof(hmon_channels[0]);

  for (uint32_t channel = 0; channel < CHANNELS; channel++) {
    if (hmon_channels[channel].field_28 != 0) {
      printf("%s: %.2f\r\n", hmon_channels[channel].name,
             float_to_double(adc_average(channel)));
    }
  }
}

/* A reference's override from the words force_gps_1pps and
 * force_ext_1pps: -1 forces it invalid, 1 valid, any other clears it. */
static void console_force_reference(gps_reference_record* reference,
                                    int32_t force) {
  switch (force) {
    case -1:
      reference->override = GPS_REFERENCE_OVERRIDE_INVALID;
      break;
    case 1:
      reference->override = GPS_REFERENCE_OVERRIDE_VALID;
      break;
    default:
      reference->override = GPS_REFERENCE_OVERRIDE_NONE;
      break;
  }
}

/* The word force_gps_1pps: the GPS reference's override (0x2b3f6). */
void diag_force_gps_1pps(int32_t force) {
  console_force_reference(&gps.references[0], force);
}

/* The word pr_hold_cause: each reason the loop holds over, or none
 * (0x2aef8). */
void diag_pr_hold_cause(void) {
  static const struct {
    pll_hold_cause cause;
    char* text;
  } CAUSES[] = {
      {HOLD_CAUSE_HARDWARE_FAILURE, "Holdover due to hardware failure\r\n"},
      {HOLD_CAUSE_MANUAL, "Holdover due to manual\r\n"},
      {HOLD_CAUSE_GPS, "Holdover due to GPS\r\n"},
      {HOLD_CAUSE_TI_ERROR, "Holdover due to TI error\r\n"},
      {HOLD_CAUSE_TI_LIMIT_EXCEEDED, "Holdover due to TI limit exceeded\r\n"},
      {HOLD_CAUSE_TEMPORARY_MODE, "Holdover due to temporary mode\r\n"},
      {HOLD_CAUSE_1PPS_SELECTION, "Holdover due to 1PPS selection\r\n"},
  };

  if (pll.hold_causes == HOLD_CAUSE_NONE) {
    printf("No holdover cause\r\n");
    return;
  }
  for (uint32_t i = 0; i < sizeof CAUSES / sizeof CAUSES[0]; i++) {
    if ((pll.hold_causes & CAUSES[i].cause) != 0) {
      printf(CAUSES[i].text);
    }
  }
}

/* The word force_ext_1pps: the external 1PPS reference's override
 * (0x2b42e). */
void diag_force_ext_1pps(int32_t force) {
  console_force_reference(&gps.references[1], force);
}

/* gps_init_sequence's state, named, copied to the original's 80 bytes
 * and printed (0x2b57c). */
void print_gps_init_state(void) {
  /* The name's buffer, the original's 80 bytes (link -80), so #define. */
  enum { CONSOLE_STATE_SIZE = 80 };
  char text[CONSOLE_STATE_SIZE];
  char* name;

  console_write("Gen GPS state: ");
  switch (pll.gps_init_state) {
    case PLL_GPS_INIT_START:
      name = "start";
      break;
    case PLL_GPS_INIT_ACK:
      name = "init ack";
      break;
    case PLL_GPS_INIT_UTC_SEND:
      name = "UTC corr send";
      break;
    case PLL_GPS_INIT_UTC_ACK:
      name = "UTC corr ack";
      break;
    case PLL_GPS_INIT_MASK_ANGLE_SEND:
      name = "mask angle send";
      break;
    case PLL_GPS_INIT_MASK_ANGLE_ACK:
      name = "mask angle ack";
      break;
    case PLL_GPS_INIT_ANTENNA_DELAY_SEND:
      name = "antenna delay send";
      break;
    case PLL_GPS_INIT_ANTENNA_DELAY_ACK:
      name = "antenna delay ack";
      break;
    case PLL_GPS_INIT_IGNORE_SEND:
      name = "sat ignore send";
      break;
    case PLL_GPS_INIT_IGNORE_ACK:
      name = "sat ignore ack";
      break;
    case PLL_GPS_INIT_POSITION_SEND:
      name = "pos location send";
      break;
    case PLL_GPS_INIT_POSITION_ACK:
      name = "pos location ack";
      break;
    case PLL_GPS_INIT_HOLD_OFF_SEND:
      name = "disable pos hold send";
      break;
    case PLL_GPS_INIT_HOLD_OFF_ACK:
      name = "disable pos hold ack";
      break;
    case PLL_GPS_INIT_SURVEY:
      name = "survey";
      break;
    case PLL_GPS_INIT_SURVEY_POSITION_SEND:
      name = "set_survey location send";
      break;
    case PLL_GPS_INIT_SURVEY_POSITION_ACK:
      name = "set_survey location ack";
      break;
    case PLL_GPS_INIT_DONE:
      name = "done";
      break;
    default:
      name = "unknown";
      break;
  }
  libc_copy(text, name);
  printf("%s\r\n", text);
}

/* The console's words ps, mem_rep and s_rep: the kernel's process, memory
 * and stack reports, passed REQUEST_SOURCE_SILENT_1 (0x2311c, 0x2312a,
 * 0x23138), where the 58503A's pass SCPI's source, 0. */
void ps_word(void) { os_process_report(REQUEST_SOURCE_SILENT_1); }

void mem_rep_word(void) { os_memory_report(REQUEST_SOURCE_SILENT_1); }

void s_rep_word(void) { task_stack_report(REQUEST_SOURCE_SILENT_1); }

/* Defines every diagnostic word, from the image's table, diag_words
 * (0x2c42c), to its null code (0x2bda8): each name is passed in place,
 * in the table.  Unlike the Z3815A's the table has puck, efc_comp and
 * efc_comp_debug first, none of the redundancy's words, and duart_debug
 * last. */
void diag_words_define(void) {
  for (const diag_word_entry* word = diag_words; word->code != NULL; word++) {
    pforth_define_word(word->name, (uint32_t)word->code, word->argument_count,
                       word->result_count);
  }
}

/* The hook halt runs: lowers the console task below the SCPI task's
 * creator, scpi_task_create, has it run, and deletes the console's
 * task (0x230e6), the 58503A's console_halt_hook. */
void console_halt_hook(void) {
  psos_priority(OS_TASK_SELF, CONSOLE_HALT_PRIORITY, NULL);
  spoo_run(scpi_task_create, CONSOLE_SCPI_CREATOR_PRIORITY, 0, 0);
  task_console = 0;
  psos_delete(OS_TASK_SELF);
}

/* The word efc_comp, the Z3815A's s3_testmode: the s3 test mode on or
 * off (0x2b104). */
void diag_efc_comp(uint32_t on) { s3_test_mode_set(on); }

/* The word efc_comp_debug: efc_adc_debug, its argument's low byte
 * (0x2b114). */
void diag_efc_comp_debug(uint32_t debug) { efc_adc_debug = debug; }

/* The word pll_rep: the EFC history's ends, the loop's largest time
 * constant and the figures of merit; the Z3815A's without the last
 * average and the drift (0x2aff2). */
void diag_pll_rep(void) {
  frequency_figure_of_merit ffom;

  printf("start ptr = %d    stop_ptr = %d\r\n",
         (uint32_t)pll.efc_history.start_ptr,
         (uint32_t)pll.efc_history.stop_ptr);
  printf("max loop time = %d\r\n", float_to_integer(max_loop_time));
  get_ffom(&ffom);
  printf("ffom = %1d\r\n", (uint32_t)ffom);
  printf("tfom = %.1e secs\r\n", float_to_double(pll.tfom));
}

/* The word efc_rep: every EFC history entry's time, average and weight,
 * then the curv task's last fit's three terms, where the Z3815A's prints
 * each entry's kept fit (0x2b078). */
void diag_efc_rep(void) {
  for (int32_t i = 0; i < PLL_AGING_ENTRIES; i++) {
    printf("%f %.1f %d\r\n", float_to_double(pll.efc_history.times[i]),
           float_to_double(pll.efc_history.averages[i]),
           (uint32_t)pll.efc_history.weights[i]);
  }
  printf("a= %e b= %e c= %e\r\n", float_to_double(pll.efc_history.fit_a),
         float_to_double(pll.efc_history.fit_b),
         float_to_double(pll.efc_history.fit_c));
}

/* The word puck: the EFC calibration, whether the oscillator's link is
 * alive and whether it is ready (0x2b120).  The Z3815A 4010's word also
 * prints its ADC error flag and status bytes while the link is alive. */
void diag_puck(void) {
  printf("a= %e b= %e a/d= %e\r\n",
         float_to_double(scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe),
         float_to_double(scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe),
         float_to_double(scpi_value_DIAGnostic_ROSCillator_EFControl_DADC));
  printf(puck_alive != 0 ? "puck communication is alive\r\n"
                         : "puck communication is not alive\r\n");
  printf("puck warm= %d\r\n", (uint32_t)puck_warm());
}

/* The word duart_debug: the DUART counter test's count and both
 * loopback strings, each ended at its last byte (0x2bd5e). */
void diag_duart_debug(void) {
  printf("DUART counter test count (high F8CC, low F532): %04X\r\n",
         (uint32_t)duart_counter_count);
  duarta_loopback_received[sizeof(duarta_loopback_received) - 1] = 0;
  printf("DUARTA in string: %s\r\n", duarta_loopback_received);
  duartb_loopback_received[sizeof(duartb_loopback_received) - 1] = 0;
  printf("DUARTB in string: %s\r\n", duartb_loopback_received);
}
