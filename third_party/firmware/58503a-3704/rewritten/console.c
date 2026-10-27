/* The pForth console's setup and words, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/console`; docs/firmware/
 * console.md).  The exported C includes this file after its own, so the
 * types and prototypes it declares are in scope; each function here
 * keeps its generated prototype.  It is the console's start, its
 * dictionary's C words, the halt hook, and the diagnostic words in their
 * table's order, the reports last. */

#include <stddef.h>

/* The words efc_wr and pr_efc: the loop's EFC set from a 16-bit DAC
 * value, and printed (0x1b79c, 0x1b7ba). */
void diag_efc_wr(uint32_t value) {
  /* pll.efc runs to 1048560, 16 times the DAC's 65535. */
  static const float EFC_PER_DAC_STEP = 16.0f;

  pll.efc = float_multiply(unsigned_to_float(value), EFC_PER_DAC_STEP);
}

void diag_pr_efc(void) { printf("efc = %5f\r\n", float_to_double(pll.efc)); }

/* The words log_send and print_clk_list (0x1be06, 0x1be1a). */
void diag_log_send(uint32_t number) {
  log_write("Log test message %d", number);
}

/* gps_leap_sequence's state, named (0x1b9c0). */
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

/* The loop's stage, named, by the jump table at 0x1bb2c. */
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

/* Appends powerup's sub-state, named, by the jump table at 0x1bb76. */
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
 * 0x1bc7a. */
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
 * 0x1bd36. */
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
 * leap check sequences' states (0x1baec). */
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
