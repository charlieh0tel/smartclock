/* The pForth console's setup and words, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/console`; docs/firmware/
 * console.md).  The exported C includes this file after its own, so the
 * types and prototypes it declares are in scope; each function here
 * keeps its generated prototype.  It is the console's start, its
 * dictionary's C words, the halt hook, and the diagnostic words in their
 * table's order, the reports last. */

#include <stddef.h>

/* A diagnostic word: its routine, name and argument and result counts. */
typedef struct console_word {
  const void* code;
  char* name;
  int32_t argument_count;
  uint32_t result_count;
} console_word;

/* The diagnostic words, in the order diag_words_define defines them:
 * the image's table at 0x1c8ac, 0x26 bytes an entry, to its null code at
 * 0x1d54a. */
static const console_word console_words[] = {
    {diag_cal, "cal", 1, 0},
    {diag_adc_read, "adc_read", 1, 1},
    {diag_adc_temp, "adc_temp", 0, 0},
    {diag_adc_5v, "adc_5v", 0, 0},
    {diag_adc_p15v, "adc_p15v", 0, 0},
    {diag_adc_m15v, "adc_m15v", 0, 0},
    {diag_adc_oven, "adc_oven", 0, 0},
    {diag_adc_doven, "adc_doven", 0, 0},
    {diag_adc_ant_curr, "adc_ant_curr", 0, 0},
    {diag_pr_adc_avg, "pr_adc_avg", 0, 0},
    {diag_efc_write, "efc_write", 1, 0},
    {diag_hdac_write, "hdac_write", 2, 0},
    {diag_hdac_all, "hdac_all", 6, 0},
    {diag_doven, "doven", 1, 0},
    {diag_dmessage, "dmessage", 1, 0},
    {diag_dmes_gpsm, "dmes_gpsm", 1, 0},
    {diag_dmes_hmon, "dmes_hmon", 1, 0},
    {diag_dmes_klok, "dmes_klok", 1, 0},
    {diag_dmes_pllp, "dmes_pllp", 1, 0},
    {diag_dmes_root, "dmes_root", 1, 0},
    {diag_dmes_scpi, "dmes_scpi", 1, 0},
    {diag_dmes_spoo, "dmes_spoo", 1, 0},
    {diag_dmes_curv, "dmes_curv", 1, 0},
    {diag_dmes_all, "dmes_all", 1, 0},
    {diag_wr_eeprom, "wr_eeprom", 2, 1},
    {diag_clear_nv, "clear_nv", 0, 0},
    {diag_debug_psos, "debug_psos", 1, 0},
    {diag_clk_synch, "clk_synch", 2, 0},
    {diag_get_time, "get_time", 0, 0},
    {diag_set_time, "set_time", 6, 0},
    {diag_abr_stat, "abr_stat", 1, 0},
    {diag_print_stat, "print_stat", 0, 0},
    {diag_stat_rate, "stat_rate", 1, 0},
    {diag_vis_stat_rate, "vis_stat_rate", 1, 0},
    {diag_print_vis, "print_vis", 0, 0},
    {diag_ext_msg_rate, "ext_msg_rate", 1, 0},
    {diag_xdop_stat_rate, "xdop_stat_rate", 1, 0},
    {diag_print_ext_msg, "print_ext_msg", 0, 0},
    {diag_print_bc, "print_bc", 0, 0},
    {diag_gps_time, "gps_time", 3, 0},
    {diag_gps_date, "gps_date", 3, 0},
    {diag_gps_php, "gps_php", 4, 0},
    {diag_gps_ph, "gps_ph", 1, 0},
    {diag_gps_cable_dly, "gps_cable_dly", 1, 0},
    {diag_force_1pps, "force_1pps", 1, 0},
    {diag_efc_wr, "efc_wr", 1, 0},
    {diag_pr_efc, "pr_efc", 0, 0},
    {diag_pll_restart, "pll_restart", 0, 0},
    {diag_pll_debug, "pll_debug", 1, 0},
    {diag_lock, "lock", 0, 0},
    {diag_phase_off, "phase_off", 0, 0},
    {diag_pr_pll, "pr_pll", 0, 0},
    {diag_log_send, "log_send", 1, 0},
    {diag_print_clk_list, "print_clk_list", 0, 0},
    {diag_crash, "crash", 1, 0},
    {diag_leapsec, "leapsec", 2, 0},
    {diag_eman, "eman", 1, 0},
    {diag_q_eman, "q_eman", 0, 0},
    {diag_q_gps_php, "q_gps_php", 0, 0},
    {diag_q_gps_cable_dly, "q_gps_cable_dly", 0, 0},
    {diag_avg, "avg", 1, 0},
    {diag_clr_avg, "clr_avg", 0, 0},
    {diag_init_gps, "init_gps", 0, 0},
    {diag_gps_query_all, "gps_query_all", 0, 0},
    {diag_quiet_status, "quiet_status", 0, 0},
    {diag_master_reset, "master_reset", 0, 0},
    {diag_pr_gps_debug, "pr_gps_debug", 0, 0},
    {diag_clr_gps_debug, "clr_gps_debug", 0, 0},
    {diag_pr_gps_cmds, "pr_gps_cmds", 0, 0},
    {diag_pr_avg, "pr_avg", 0, 0},
    {diag_pr_gps_istate, "pr_gps_istate", 0, 0},
    {diag_pr_gps_id, "pr_gps_id", 0, 0},
    {diag_pr_1pps, "pr_1pps", 0, 0},
    {diag_pr_time_raim, "pr_time_raim", 0, 0},
    {diag_disable_gps_cmds, "disable_gps_cmds", 1, 0},
    {diag_pstat, "pstat", 1, 0},
    {diag_gps_change, "gps_change", 5, 0},
    {diag_gps_query, "gps_query", 1, 0},
    {diag_pr_hold_cause, "pr_hold_cause", 0, 0},
    {diag_loop_time, "loop_time", 1, 0},
    {diag_pll_rep, "pll_rep", 0, 0},
    {diag_efc_rep, "efc_rep", 0, 0},
    {diag_sync_imm, "sync_imm", 0, 0},
    {diag_es_ascii, "es_ascii", 1, 0},
    {diag_fp_u, "fp_u", 0, 0},
};

/* Defines every diagnostic word (0x1c18e). */
void diag_words_define(void) {
  for (size_t i = 0; i < sizeof console_words / sizeof console_words[0]; i++) {
    const console_word* word = &console_words[i];

    pforth_define_word(word->name, (uint32_t)word->code, word->argument_count,
                       word->result_count);
  }
}

/* The word cal: samples the oven current and the EFC every 10 seconds
 * for the given seconds and prints the least-squares slope of the EFC on
 * the current, scaled (0x1bed8). */
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
    const uint16_t current = (uint8_t)adc_read(ADC_CURRENT);
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

/* The word adc_read: an ADC channel's raw byte; the number's low bit is
 * the channel's bit 2, its others the channel's low bits (0x1afd6). */
uint32_t diag_adc_read(uint8_t number) {
  static const uint8_t LOW_BIT_SHIFT = 2;
  const adc_channel channel = number >> 1 | (number & 1) << LOW_BIT_SHIFT;

  return (uint8_t)adc_read(channel);
}

/* The words adc_temp to adc_ant_curr: a reading, scaled (0x1b008 to
 * 0x1b0d4). */
void diag_adc_temp(void) {
  printf("Temperature: %f\r\n", float_to_double(read_adc_temp()));
}

void diag_adc_5v(void) {
  printf("5V reading: %f\r\n", float_to_double(read_adc_5v()));
}

void diag_adc_p15v(void) {
  printf("+15V reading: %f\r\n", float_to_double(read_adc_p15v()));
}

void diag_adc_m15v(void) {
  printf("-15V reading: %f\r\n", float_to_double(read_adc_m15v()));
}

void diag_adc_oven(void) {
  printf("Oven: %f\r\n", float_to_double(read_adc_oven()));
}

void diag_adc_doven(void) {
  printf("Double oven: %f\r\n", float_to_double(read_adc_doven()));
}

void diag_adc_ant_curr(void) {
  printf("Antenna current: %f\r\n", float_to_double(read_adc_ant_curr()));
}

/* Prints a channel's monitored average by a format. */
static void console_print_average(char* format, adc_channel channel) {
  printf(format, float_to_double(adc_average(channel)));
}

/* The word pr_adc_avg: every channel's monitored average (0x1b0f6). */
void diag_pr_adc_avg(void) {
  console_print_average("Temperature: %.2f\r\n", ADC_TEMPERATURE);
  console_print_average("5V: %.2f\r\n", ADC_5V);
  console_print_average("+15V: %.2f\r\n", ADC_PLUS_15V);
  console_print_average("-15V: %.2f\r\n", ADC_MINUS_15V);
  console_print_average("Oven: %.2f\r\n", ADC_OVEN);
  console_print_average("Double oven: %.2f\r\n", ADC_DOUBLE_OVEN);
  console_print_average("Oven current: %.2f\r\n", ADC_CURRENT);
}

/* The word pr_hold_cause: each reason the loop holds over, or none
 * (0x1b1d0). */
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

/* The word force_1pps: -1 forces the GPS reference invalid, 1 valid, any
 * other clears the override (0x1b68c). */
void diag_force_1pps(int32_t force) {
  switch (force) {
    case -1:
      reference_valid_override_request = GPS_REFERENCE_OVERRIDE_INVALID;
      break;
    case 1:
      reference_valid_override_request = GPS_REFERENCE_OVERRIDE_VALID;
      break;
    default:
      reference_valid_override_request = GPS_REFERENCE_OVERRIDE_NONE;
      break;
  }
}

/* The words efc_wr and pr_efc: the loop's EFC set from a 16-bit DAC
 * value, and printed (0x1b77c, 0x1b79a). */
void diag_efc_wr(uint32_t value) {
  /* pll.efc runs to 1048560, 16 times the DAC's 65535. */
  static const float EFC_PER_DAC_STEP = 16.0f;

  pll.efc = float_multiply(unsigned_to_float(value), EFC_PER_DAC_STEP);
}

void diag_pr_efc(void) { printf("efc = %5f\r\n", float_to_double(pll.efc)); }

/* The words log_send and print_clk_list (0x1bde2, 0x1bdf6). */
void diag_log_send(uint32_t number) {
  log_write(log_text_log_test_message, number);
}

/* The word pll_rep: the EFC history's ends, the loop time, the figures of
 * merit, the last average and the drift (0x1b310). */
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

/* The word efc_rep: every EFC history entry's time, average and weight
 * (0x1b3de). */
void diag_efc_rep(void) {
  for (int32_t i = 0; i < PLL_AGING_ENTRIES; i++) {
    printf("%f %.1f %d\r\n", float_to_double(pll.efc_history.times[i]),
           float_to_double(pll.efc_history.averages[i]),
           (uint32_t)pll.efc_history.weights[i]);
  }
}

/* gps_init_sequence's state, named (0x1b7ee). */
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

/* gps_leap_sequence's state, named (0x1b99c). */
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

/* The loop's stage, named, by the jump table at 0x1bb08. */
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

/* Appends powerup's sub-state, named, by the jump table at 0x1bb52. */
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
 * 0x1bc56. */
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
 * 0x1bd12. */
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
 * leap check sequences' states (0x1bac8). */
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
