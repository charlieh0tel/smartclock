/* The pForth console's setup and words, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/console`; docs/firmware/
 * console.md).  The exported C includes this file after its own, so the
 * types and prototypes it declares are in scope; each function here
 * keeps its generated prototype.  It is the console's start, its
 * dictionary's C words, the halt hook, and the diagnostic words in their
 * table's order, the reports last. */

#include <stddef.h>

/* The words efc_wr and pr_efc: the loop's EFC set by efc_set, with its
 * readback's correction, from the value as it is, where the 58503A's
 * takes a 16-bit DAC value and sets pll.efc 16 times it (0x2be50,
 * 0x2be66). */
void diag_efc_wr(uint32_t value) { efc_set(unsigned_to_float(value)); }

void diag_pr_efc(void) { printf("efc = %5f\r\n", float_to_double(pll.efc)); }

/* The loop's stage, named, for pr_pll. */
static const char* console_stage_name(pll_stage stage) {
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
    case PLL_STAGE_N3_LOCK_PLL:
      return "n3 lock pll";
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

/* Appends powerup's sub-state, named, by the jump table at 0x2c224. */
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
 * 0x2c328. */
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

/* Appends n3 lock pll's state, named, by the jump table at 0x2c3d6:
 * nothing for state 11 or past exit2. */
static void console_append_n3_lock_state(char* state) {
  switch (pll.n3_lock_state) {
    case PLL_N3_LOCK_WAIT1:
      strcat(state, ": wait1");
      break;
    case PLL_N3_LOCK_WAIT2:
      strcat(state, ": wait2");
      break;
    case PLL_N3_LOCK_SYNC1:
      strcat(state, ": sync1");
      break;
    case PLL_N3_LOCK_SYNC2:
      strcat(state, ": sync2");
      break;
    case PLL_N3_LOCK_LEAPSEC:
      strcat(state, ": leapsec");
      break;
    case PLL_N3_LOCK_CHECK_TIME:
      strcat(state, ": check_time");
      break;
    case PLL_N3_LOCK_MEASURE1:
      strcat(state, ": measure1");
      break;
    case PLL_N3_LOCK_MEASURE2:
      strcat(state, ": measure2");
      break;
    case PLL_N3_LOCK_MEASURE3:
      strcat(state, ": measure3");
      break;
    case PLL_N3_LOCK_CALC_FREQ:
      strcat(state, ": calc_freq");
      break;
    case PLL_N3_LOCK_SLEW:
      strcat(state, ": slew");
      break;
    case PLL_N3_LOCK_PLL:
      strcat(state, ": pll");
      break;
    case PLL_N3_LOCK_EXIT1:
      strcat(state, ": exit1");
      break;
    case PLL_N3_LOCK_EXIT2:
      strcat(state, ": exit2");
      break;
    default:
      break;
  }
}

/* Appends gps_diag_sequence's state, named, by the jump table at
 * 0x2c526. */
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
 * leap check sequences' states; unlike the 58503A's, n3 lock pll's state
 * too (0x2c198). */
void diag_pr_pll(void) {
  /* -0x50(A6), the original's 80 bytes, so #define. */
  enum { CONSOLE_STATE_SIZE = 80 };
  char state[CONSOLE_STATE_SIZE];

  console_write("PLL state: ");
  libc_copy(state, console_stage_name(pll.stage));
  switch (pll.stage) {
    case PLL_STAGE_POWERUP:
      console_append_powerup_state(state);
      break;
    case PLL_STAGE_HOLDOVER_RECOVERY:
      console_append_recovery_state(state);
      break;
    case PLL_STAGE_N3_LOCK_PLL:
      console_append_n3_lock_state(state);
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

/* The words log_send and print_clk_list (0x2c5f6, 0x2c60a). */
void diag_log_send(uint32_t number) {
  log_write("Log test message %d", number);
}

/* gps_leap_sequence's state, named (0x2c06c). */
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

/* The word efc_comp_debug: efc_compensation_debug, which has
 * puck_efc_correct print its figures while it is 1 (0x2c742). */
void diag_efc_comp_debug(uint32_t debug) { efc_compensation_debug = debug; }

/* The word puck: puck_report (0x2c74e). */
void diag_puck(void) { puck_report(); }

/* The word adc_read: an ADC channel's raw byte, adc_read taking the
 * number for the channel, where the Z3805A's maps it (0x2b774). */
uint32_t diag_adc_read(uint32_t number) {
  return (uint8_t)adc_read((adc_channel)number);
}

/* The word pr_adc_avg: the monitored average of each health monitor
 * channel marked for it, by its name (0x2b78a). */
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

/* The word force_gps_1pps: the GPS reference's override (0x2bd22). */
void diag_force_gps_1pps(int32_t force) {
  console_force_reference(&gps.references[0], force);
}

/* The word force_ext_1pps: the external 1PPS reference's override
 * (0x2bd5a). */
void diag_force_ext_1pps(int32_t force) {
  console_force_reference(&gps.references[1], force);
}

/* gps_init_sequence's state, named, copied to the original's 80 bytes
 * and printed (0x2beba). */
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
 * and stack reports, passed REQUEST_SOURCE_SILENT_1 (0x23152, 0x23160,
 * 0x2316e), where the 58503A's pass SCPI's source, 0. */
void ps_word(void) { os_process_report(REQUEST_SOURCE_SILENT_1); }
void mem_rep_word(void) { os_memory_report(REQUEST_SOURCE_SILENT_1); }
void s_rep_word(void) { task_stack_report(REQUEST_SOURCE_SILENT_1); }

/* The hook halt runs: lowers the console task below the SCPI task's
 * creator, closes the console's port, the SCI, and has the SCI session's
 * SCPI task, "sci", created in its place, then deletes the console's
 * task.  The 58503A's leaves the port open and creates "scpi"
 * (0x23116). */
void console_halt_hook(void) {
  psos_priority(OS_TASK_SELF, CONSOLE_HALT_PRIORITY, NULL);
  scpi_port_close();
  spoo_run(sci_scpi_task_create, CONSOLE_SCPI_CREATOR_PRIORITY, 0, 0);
  task_console = 0;
  psos_delete(OS_TASK_SELF);
}

/* A diagnostic word: its routine, name and argument and result counts,
 * 0x26 bytes in the image, its name 26 bytes in place. */
typedef struct console_word {
  const void* code;
  char* name;
  int32_t argument_count;
  uint32_t result_count;
} console_word;

/* The diagnostic words, in the order diag_words_define defines them:
 * the image's table at 0x2cef0, 0x26 bytes an entry, to its null code at
 * 0x2dc26.  Unlike the 58503A's it has no cal, and adds efc_comp_debug
 * and puck first and the redundancy's words (set_mux to pr_bad_act)
 * last. */
static const console_word console_words[] = {
    {diag_efc_comp_debug, "efc_comp_debug", 1, 0},
    {diag_puck, "puck", 0, 0},
    {diag_adc_read, "adc_read", 1, 1},
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
    {diag_force_gps_1pps, "force_gps_1pps", 1, 0},
    {diag_force_ext_1pps, "force_ext_1pps", 1, 0},
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
    {diag_set_mux, "set_mux", 1, 0},
    {diag_n3_phase, "n3_phase", 2, 0},
    {diag_n3_duart_res, "n3_duart_res", 0, 0},
    {diag_efc_comp, "efc_comp", 1, 0},
    {diag_s3_out_reg, "s3_out_reg", 1, 0},
    {diag_s3_testmode, "s3_testmode", 1, 0},
    {diag_s3_hw_fail, "s3_hw_fail", 1, 0},
    {diag_s3_gps_avail, "s3_gps_avail", 1, 0},
    {diag_pr_bad_act, "pr_bad_act", 0, 0},
};

/* Defines every diagnostic word (0x2c868). */
void diag_words_define(void) {
  for (size_t i = 0; i < sizeof console_words / sizeof console_words[0]; i++) {
    const console_word* word = &console_words[i];

    pforth_define_word(word->name, (uint32_t)word->code, word->argument_count,
                       word->result_count);
  }
}

/* The word set_mux: mux_set, a bare rts here, with its argument
 * (0x2c696). */
void diag_set_mux(uint32_t mux) { mux_set(mux); }

/* The word n3_phase: n3_lock.phase, negated for a nonzero second
 * argument, then printed (0x2c6a6). */
void diag_n3_phase(int32_t phase, int32_t negate) {
  n3_lock.phase = negate != 0 ? -phase : phase;
  printf("%d\r\n", n3_lock.phase);
}

/* The word n3_duart_res: selftest.uart, the DUART tests' failures
 * (0x2c67c). */
void diag_n3_duart_res(void) {
  printf("fail_UART = %x\r\n", (uint32_t)(uint16_t)selftest.uart);
}

/* The word efc_comp: efc_compensation, which has efc_set correct the EFC
 * by its readback while set (0x2ba7c). */
void diag_efc_comp(int32_t compensation) { efc_compensation = compensation; }

/* The word s3_out_reg: the word output_status_update writes in the s3
 * test mode (0x2ba86). */
void diag_s3_out_reg(uint32_t value) { s3_output_set(value); }

/* The word s3_testmode: the s3 test mode on or off (0x2ba96). */
void diag_s3_testmode(uint32_t on) { s3_test_mode_set(on); }

/* The word s3_hw_fail: OP5 in the s3 test mode (0x2baa6). */
void diag_s3_hw_fail(uint32_t on) { s3_hardware_fail_set(on); }

/* The word s3_gps_avail: OP2 in the s3 test mode (0x2bab6). */
void diag_s3_gps_avail(uint32_t on) { s3_gps_available_set(on); }

/* The word pr_hold_cause: each reason the loop holds over, or none
 * (0x2b7ec). */
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

/* The word pll_rep: the EFC history's ends, the loop time, the figures of
 * merit, the last average and the drift (0x2b92c). */
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

/* The word efc_rep: every EFC history entry's time, average and weight,
 * and, which the 58503A's lacks, its fit's three terms (0x2b9fa). */
void diag_efc_rep(void) {
  for (int32_t i = 0; i < PLL_AGING_ENTRIES; i++) {
    printf("%f %.1f %d %e %e %e\r\n", float_to_double(pll.efc_history.times[i]),
           float_to_double(pll.efc_history.averages[i]),
           (uint32_t)pll.efc_history.weights[i],
           float_to_double(pll.efc_history.fit_a_history[i]),
           float_to_double(pll.efc_history.fit_b_history[i]),
           float_to_double(pll.efc_history.fit_c_history[i]));
  }
}

void diag_hdac_write(uint32_t value, uint32_t channel) {
  hdac_write(value, channel);
}

void diag_hdac_all(uint32_t value_1, uint32_t value_2, uint32_t value_3,
                   uint32_t value_4, uint32_t value_5, uint32_t value_6) {
  hdac_write_all(value_1, value_2, value_3, value_4, value_5, value_6);
}

/* The word doven: the outer oven on or off. */
void diag_doven(int32_t on) {
  if (on == 0) {
    gpt.portgp &= ~PLL_STAGES_OUTER_OVEN_ENABLE;
  } else {
    gpt.portgp |= PLL_STAGES_OUTER_OVEN_ENABLE;
  }
}

void diag_phase_off(int32_t setpoint) {
  ti_setpoint = integer_to_float(setpoint);
}
