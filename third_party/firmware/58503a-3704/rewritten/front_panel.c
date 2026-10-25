/* The front panel, Option 001's display and keypad on QSPI device 0
 * (docs/firmware/58503a-3704.md, "The front panel"): the lines its
 * modes show, front_panel_update's (0x53372).  Written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C"),
 * which reads the status line's switches through their tables as cases
 * of the tables' offsets, and the angle's double, in the frame, as no
 * argument.  The exported C includes this file after its own, so the
 * types and prototypes it declares are in scope; each function here
 * keeps its generated prototype. */

/* The most whole hours of holdover the status line gives as hours and
 * minutes, a byte of them; past it, "MAX " (0x5295e). */
static const uint8_t FRONT_PANEL_MOST_HOURS = 99;

/* survey_progress_text's text, "%+.1f" of 0.1 to 100.0 and its NUL
 * (0x52ac2), so #define. */
#define FRONT_PANEL_PROGRESS_SIZE 7

/* The progress's characters the status line takes after its sign
 * (0x52ae8). */
static const uint32_t FRONT_PANEL_PROGRESS_DIGITS = 4;

/* An angle in mas, a double, as its hemisphere, N or S for a latitude,
 * E or W, then "%4d:%02d:%06.3f" degrees, minutes and seconds: the
 * Long and Lat keys' lines (0x5274e).  format_dms_precise's to the
 * millisecond. */
void front_panel_format_dms(char* text, double mas, char latitude) {
  gps_format_dms(text, mas, latitude, "%c%4d:%02d:%06.3f");
}

/* The status line in holdover: its hours and minutes, "MAX " past
 * FRONT_PANEL_MOST_HOURS, and why, "HLD USR" for a manual holdover,
 * "HLD GPS" or "HLD LIM" by the reason the loop waits, else "HOLDOVR"
 * (0x5293e). */
static void front_panel_holdover_text(char* text) {
  uint8_t hours = pll.holdover_duration / CLOCK_SECONDS_PER_HOUR;
  uint8_t minutes =
      pll.holdover_duration % CLOCK_SECONDS_PER_HOUR / CLOCK_SECONDS_PER_MINUTE;

  if (hours > FRONT_PANEL_MOST_HOURS) {
    libc_copy(text, "MAX ");
  } else {
    sprintf(text, "%02d:%02d ", hours, minutes);
  }
  if (pll_mode() == PLL_MODE_HOLDOVER_MANUAL) {
    strcat(text, "HLD USR");
    return;
  }
  /* The image tests the result's low byte (0x529a4). */
  switch ((uint8_t)holdover_waiting_reason_58503a()) {
    case HOLD_CAUSE_MANUAL:
      strcat(text, "HLD USR");
      break;
    case HOLD_CAUSE_GPS:
      strcat(text, "HLD GPS");
      break;
    case HOLD_CAUSE_TI_LIMIT_EXCEEDED:
      strcat(text, "HLD LIM");
      break;
    default:
      strcat(text, "HOLDOVR");
      break;
  }
}

/* The status line in holdover recovery: its step by
 * recovery_suffix_index (0x52a0c). */
static void front_panel_recovery_text(char* text) {
  switch (recovery_suffix_index()) {
    case PLL_RECOVERY_SUFFIX_FINE_FREQUENCY:
      libc_copy(text, "FINE F ADJ");
      break;
    case PLL_RECOVERY_SUFFIX_PHASE_ALIGNMENT:
      libc_copy(text, "PHASE ALIGN");
      break;
    default:
      libc_copy(text, "HLD RECVR");
      break;
  }
}

/* The status line otherwise, by the position: held, the FFOM's
 * "STABILIZING" or "10MHZ STABLE", nothing for another FFOM; navigating,
 * "NAV MODE"; else the survey's progress, "SURV 12.3 PCT", or "SURVEY
 * HALT" when none runs (0x52a5a). */
static void front_panel_position_text(char* text) {
  frequency_figure_of_merit ffom;
  char progress[FRONT_PANEL_PROGRESS_SIZE];

  if (gps.position_hold == 1) {
    get_ffom(&ffom);
    if (ffom == FFOM_STABILIZING) {
      libc_copy(text, "STABILIZING");
    } else if (ffom == FFOM_STABILIZED) {
      libc_copy(text, "10MHZ STABLE");
    }
  } else if (nv_settings.douglas_duration_infinite == 1) {
    libc_copy(text, "NAV MODE");
  } else {
    uint16_t sr = set_interrupt_mask(CPU_IPL_1);
    /* The image tests the result's low byte (0x52ad8). */
    uint8_t running = survey_progress_text(progress);

    set_interrupt_mask(sr);
    if (running != 0) {
      libc_copy(text, "SURV ");
      strncat(text, progress + 1, FRONT_PANEL_PROGRESS_DIGITS);
      strcat(text, " PCT");
    } else {
      libc_copy(text, "SURVEY HALT");
    }
  }
}

/* The Status key's line, mode 6, by pll.stage: holdover's, holdover
 * recovery's, else the position's (0x5290c). */
void front_panel_status_text(char* text) {
  switch (pll.stage) {
    case PLL_STAGE_HOLDOVER:
      front_panel_holdover_text(text);
      break;
    case PLL_STAGE_HOLDOVER_RECOVERY:
      front_panel_recovery_text(text);
      break;
    default:
      front_panel_position_text(text);
      break;
  }
}
