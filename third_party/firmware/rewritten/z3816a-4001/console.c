/* console.c's functions that are this image's own: the rest are
 * common/console.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the 58503A 3704's, the Z3815A 4010's and the Z3805A
 * 3543B's rewritten/console.c, of its functions, those the Z3816A's code
 * carries (identical or relocated by image_diff, or changed with the same
 * instructions), with the static helpers they use, each the same here but
 * where its comment says otherwise (docs/firmware/z3816a-4001.md, "The link
 * check").  Its comments' addresses are the Z3816A's.  Each keeps its
 * generated prototype. */

/* The words efc_wr and pr_efc: the loop's EFC set from a 16-bit DAC
 * value, and printed (0x2b81c, 0x2b83a). */
void diag_efc_wr(uint32_t value) {
  /* pll.efc runs to 1048560, 16 times the DAC's 65535. */
  static const float EFC_PER_DAC_STEP = 16.0f;

  pll.efc = float_multiply(unsigned_to_float(value), EFC_PER_DAC_STEP);
}

/* The pForth word clr_satview: clears the satellite view (0x2b7ba). */
void diag_clr_satview(void) { satellite_view_clear(); }

void diag_pr_efc(void) { printf("efc = %5f\r\n", float_to_double(pll.efc)); }

/* The words log_send and print_clk_list (0x2be82, 0x2be96). */
void diag_log_send(uint32_t number) {
  log_write("Log test message %d", number);
}

/* gps_leap_sequence's state, named (0x2ba3c). */
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

/* The loop's stage, named, by diag_pr_pll's jump table at 0x2bba8. */
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

/* Appends powerup's sub-state, named, by the jump table at 0x2bbf2. */
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
 * 0x2bcf6. */
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
 * 0x2bdb2. */
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

/* The word pr_pll: the loop's stage and sub-state, then the GPS init and
 * leap check sequences' states (0x2bb68). */
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

/* The word cal: samples the oven current and the EFC every 10 seconds
 * for the given seconds and prints the least-squares slope of the EFC on
 * the current, scaled (0x2bf78). */
void diag_cal(uint32_t seconds) {
  static const uint16_t STEP_SECONDS = 10;
  /* About 10 seconds at 1024 ticks a second. */
  static const uint32_t STEP_TICKS = 10000;
  /* 1 / 1.02, the image's 0x3f7afafb. */
  static const float SLOPE_SCALE = 0.98039216f;
  double sum_current = 0.0;
  double sum_efc = 0.0;
  double sum_product = 0.0;
  double sum_current_squared = 0.0;
  float count = 0.0f;

  for (uint16_t elapsed = 0; elapsed < seconds; elapsed += STEP_SECONDS) {
    const uint16_t current = (uint8_t)adc_read(ADC_OVEN_CURRENT);
    const float efc = pll.efc;

    double_add_in_place(&sum_current, integer_to_double(current));
    double_add_in_place(&sum_efc, float_to_double(efc));
    double_add_in_place(&sum_product, float_to_double(float_multiply(
                                          unsigned_to_float(current), efc)));
    double_add_in_place(&sum_current_squared,
                        integer_to_double(current * current));
    count = float_add(count, 1.0f);
    printf("curr= %d efc= %.1f, sec remaining= %d\r\n", (uint32_t)current,
           float_to_double(efc), seconds - elapsed);
    psos_pause(STEP_TICKS);
  }
  printf("now do a least square line fit to data...\r\n");
  const double numerator = double_subtract_reversed(
      double_multiply_pointer(float_to_double(count), &sum_product),
      double_multiply_pointers(&sum_efc, &sum_current));
  const double denominator = double_subtract_reversed(
      double_multiply_pointer(float_to_double(count), &sum_current_squared),
      double_multiply_pointers(&sum_current, &sum_current));
  const float slope =
      double_to_float(double_divide_reversed(denominator, numerator));
  printf("tempco = %f\r\n",
         float_to_double(float_multiply(slope, SLOPE_SCALE)));
}

/* The word adc_read: an ADC channel's raw byte, adc_read taking the
 * number for the channel, where the Z3805A's maps it (0x2b1b2). */
uint32_t diag_adc_read(uint32_t number) {
  return (uint8_t)adc_read((adc_channel)number);
}

/* The word pr_adc_avg: the monitored average of each health monitor
 * channel marked for it, by its name (0x2b1c8). */
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

/* The word force_gps_1pps: the GPS reference's override (0x2b6e6). */
void diag_force_gps_1pps(int32_t force) {
  console_force_reference(&gps.references[0], force);
}

/* The word force_ext_1pps: the external 1PPS reference's override
 * (0x2b71e). */
void diag_force_ext_1pps(int32_t force) {
  console_force_reference(&gps.references[1], force);
}

/* The word pll_rep: the EFC history's ends, the loop time, the figures of
 * merit, the last average and the drift (0x2b36a). */
void diag_pll_rep(void) {
  /* The drift as a fraction a day. */
  static const float DRIFT_PER_DAY = 5.4e-08f;
  frequency_figure_of_merit ffom;

  printf("start ptr = %d    stop_ptr = %d\r\n",
         (uint32_t)pll.efc_history.start_ptr,
         (uint32_t)pll.efc_history.stop_ptr);
  printf("max loop time = %d\r\n", float_to_integer(max_loop_time));
  get_ffom(&ffom);
  printf("ffom = %1d\r\n", (uint32_t)ffom);
  printf("tfom = %.1e secs\r\n", float_to_double(pll.tfom));
  printf("last efc average = %.1f\r\n",
         float_to_double(pll.efc_history.averages[pll.efc_history.stop_ptr]));
  printf("current drift = %.1e / day\r\n",
         float_to_double(float_multiply(pll.efc_history.drift, DRIFT_PER_DAY)));
}
/* The hook halt runs: lowers the console task below the SCPI task's
 * creator, has it run, and deletes the console's task. */
void console_halt_hook(void) {
  psos_priority(OS_TASK_SELF, CONSOLE_HALT_PRIORITY, NULL);
  spoo_run(scpi_task_create, CONSOLE_SCPI_CREATOR_PRIORITY, 0, 0);
  task_console = 0;
  psos_delete(OS_TASK_SELF);
}

void mem_rep_word(void) { os_memory_report(REQUEST_SOURCE_SILENT_1); }

/* The word pr_hold_cause: each reason the loop holds over, or none
 * (0x2b22a). */
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

/* The word efc_rep: every EFC history entry's time, average and weight
 * (0x2b438). */
void diag_efc_rep(void) {
  for (int32_t i = 0; i < PLL_AGING_ENTRIES; i++) {
    printf("%f %.1f %d\r\n", float_to_double(pll.efc_history.times[i]),
           float_to_double(pll.efc_history.averages[i]),
           (uint32_t)pll.efc_history.weights[i]);
  }
}

/* gps_init_sequence's state, named (0x2b88e). */
void print_gps_init_state(void) {
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
  printf("%s\r\n", name);
}

/* The console's words ps and s_rep: the kernel's process and stack
 * reports, passed REQUEST_SOURCE_SILENT_1 (0x23090, 0x230ac), as the
 * Z3815A 4010's, where the 58503A's pass SCPI's source, 0. */
void ps_word(void) { os_process_report(REQUEST_SOURCE_SILENT_1); }

void s_rep_word(void) { task_stack_report(REQUEST_SOURCE_SILENT_1); }

/* Defines every diagnostic word in the image's table, diag_words, to its
 * NULL (0x2c22e).  The other images' define theirs from their C's
 * tables. */
void diag_words_define(void) {
  for (const diag_word_entry* word = diag_words; word->code != NULL; word++) {
    pforth_define_word((char*)word->name, (uint32_t)word->code,
                       word->argument_count, word->result_count);
  }
}

/* The pForth word pr_satview: has the GPS task print by code 0x14, the
 * code the other images' pr_1pps gives it (0x2b7c2). */
void diag_pr_satview(void) { gps_command.code = GPS_CONSOLE_PRINT_PPS; }
