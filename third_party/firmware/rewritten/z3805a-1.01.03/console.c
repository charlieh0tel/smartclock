/* From the 58503B 1.01.04's console.c, the Z3805A 1.01.03's own beside
 * common/'s: of its functions, those common/ does not hold, each identical
 * or relocated in image_diff's matches (docs/firmware/z3805a-1.01.03.md,
 * "The export and the checks"); their comments' addresses are the Z3805A
 * 1.01.03's.  With them, from the 58503B 1.01.04's and the Z3805A 3543B's
 * C, the functions this image's code carries identical or relocated
 * (docs/firmware/z3805a-1.01.03.md, "The link list"), their comments'
 * addresses this image's but where they name another image.  The 58503B's
 * note follows. */

/* The Z3817A 3752's console.c, copied for the 58503B 1.01.04, each of its
 * functions identical or relocated in image_diff's matches
 * (docs/firmware/58503b-1.01.04.md, "The export and the checks"); its
 * comments' addresses were the 58503B's.  The Z3817A's own: from
 * common/console.c, the Z3817A 3752's until it shares common/: of its
 * functions, those the export needs hand-written to compile, each the
 * same in both images, with the static helpers and code templates it uses
 * (docs/firmware/z3817a-3752.md, "The export and the checks"). */

/* The code runs forth_compile_code copies into a word, each ended by a
 * zero and the last by an empty run, from the image's console_task+0x82 to
 * console_task+0xa1. */

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

void diag_pr_efc(void) { printf("efc = %5f\r\n", float_to_double(pll.efc)); }

/* The words log_send and print_clk_list (0x1c1a4, 0x1c1b8). */
void diag_log_send(uint32_t number) {
  log_write("Log test message %d", number);
}

/* gps_leap_sequence's state, named (0x1bd5c). */
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

/* The word pr_hold_cause: each reason the loop holds over, or none
 * (0x1b566). */
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

/* The word adc_read: an ADC channel's raw byte, adc_read taking the
 * number for the channel, where the Z3805A's maps it (0x1b4ee). */
uint32_t diag_adc_read(uint32_t number) {
  return (uint8_t)adc_read((adc_channel)number);
}

/* The word pr_adc_avg: the monitored average of each health monitor
 * channel marked for it, by its name (0x1b504). */
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

/* gps_init_sequence's state, named, copied to the original's 80 bytes
 * and printed (0x1bbaa). */
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

/* The loop's stage, named, by the jump table at 0x1bec8; 6, which the
 * Z3815A has for its n3 lock pll, is "unknown" here (0x1c172). */
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

/* Appends powerup's state, named, by the jump table at 0x1bf14. */
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
 * 0x1c018; 2, which nothing sets, is unknown. */
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
 * 0x1c0d4; the two self-tests' states, this image's own, are unknown. */
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
 * leap check sequences' states (0x1be88). */
void diag_pr_pll(void) {
  /* -0x50(A6), the original's 80 bytes, so #define. */
  enum { CONSOLE_STATE_SIZE = 80 };
  char state[CONSOLE_STATE_SIZE];
  char* to = state;
  const char* from;

  console_write("PLL state: ");
  from = console_stage_name(pll.stage);
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

/* The word force_gps_1pps: the GPS reference's override (0x1ba14). */
void diag_force_gps_1pps(int32_t force) {
  console_force_reference(&gps.references[0], force);
}

/* The word force_ext_1pps: the external 1PPS reference's override
 * (0x1ba4c). */
void diag_force_ext_1pps(int32_t force) {
  console_force_reference(&gps.references[1], force);
}

/* The words efc_wr and pr_efc: the loop's EFC set by efc_set, with its
 * readback's correction, from the value as it is, where the 58503A's
 * takes a 16-bit DAC value and sets pll.efc 16 times it (0x1bb4a). */
void diag_efc_wr(uint32_t value) { efc_set(unsigned_to_float(value)); }

/* The word pll_rep: the EFC history's ends, the loop's largest time
 * constant and the figures of merit (0x1b6a6), the Z3817A 3752's; the
 * Z3815A's prints the last average and the drift too. */
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
 * then the last fit's three terms (0x1b72c), the Z3817A 3752's; the
 * Z3815A's prints each entry's kept fit. */
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

/* The word clr_satview: clears the Sat View page's statistics (0x1bae8). */
void diag_clr_satview(void) { sky_statistics_clear(); }

/* The console's words ps, mem_rep and s_rep: the kernel's process, memory
 * and stack reports (ps_word, mem_rep_word, s_rep_word).  The console passes
 * SCPI's source, 0, as the image does (clr.b -(sp)). */
void ps_word(void) { os_process_report(REQUEST_SOURCE_SCPI); }

void mem_rep_word(void) { os_memory_report(REQUEST_SOURCE_SCPI); }

void s_rep_word(void) { task_stack_report(REQUEST_SOURCE_SCPI); }

/* The hook halt runs: lowers the console task below the SCPI task's
 * creator, closes the console's port, channel B, and has the SCPI task,
 * "scpi", created in its place, then deletes the console's task
 * (0x130b2).  The Z3805A 3543B's leaves the port open; the seeding named
 * this one sci_scpi_task_start, the 58503B's for its SCI session. */
void console_halt_hook(void) {
  psos_priority(OS_TASK_SELF, CONSOLE_HALT_PRIORITY, NULL);
  scpi_port_close();
  spoo_run(scpi_task_create, CONSOLE_SCPI_CREATOR_PRIORITY, 0, 0);
  task_console = 0;
  psos_delete(OS_TASK_SELF);
}

/* Bypasses the oven's warmup (0x1bba2). */
void diag_bypass_oven_warmup(void) { oven_warmup_bypass(); }

/* duart_debug: prints the DUART self-test's counter reading and what
 * each channel's loopback test received, ended at its last byte
 * (0x1c3ac). */
void diag_duart_debug(void) {
  printf("DUART counter test count (high F8CC, low F532): %04X\r\n",
         duart_counter_reading);
  loopback_received_a[sizeof loopback_received_a - 1] = 0;
  printf("DUARTA in string: %s\r\n", loopback_received_a);
  loopback_received_b[sizeof loopback_received_b - 1] = 0;
  printf("DUARTB in string: %s\r\n", loopback_received_b);
}

/* Defines every diagnostic word, console_words' entries up to the one
 * with no routine (0x1c3f6). */
void diag_words_define(void) {
  for (console_word* word = console_words; word->code != NULL; word++) {
    pforth_define_word(word->name, (uint32_t)word->code, word->argument_count,
                       word->result_count);
  }
}

/* The word pr_satview: asks the GPS task for the Sat View page by the
 * console request the Z3805A 3543B's pr_1pps makes (0x1baf0). */
void diag_pr_satview(void) { gps_command.code = GPS_CONSOLE_PRINT_PPS; }
