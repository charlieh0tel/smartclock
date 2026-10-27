/* The front panel, Option 001's display and keypad on QSPI device 0
 * (docs/firmware/58503a-3704.md, "The front panel"): its commands, the
 * lines its modes show, its key handler, its task and the start-up show,
 * demonstration and display test.  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"), which reads
 * the switches through their tables as cases of the tables' offsets,
 * and the angle's double, in the frame, as no argument.  The exported C
 * includes this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

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

/* The ticks between a command's latch change, its byte and its data
 * bytes (0x53756, 0x53780, 0x4815c). */
static const uint32_t FRONT_PANEL_BYTE_TICKS = 2;

/* The counts front_panel_command_end spins after a command
 * (0x537a8). */
static const uint8_t FRONT_PANEL_END_SPINS = 200;

/* The counts front_panel_selftest waits between its commands
 * (0x5380c). */
static const uint32_t FRONT_PANEL_SELFTEST_WAIT = 5;

/* What the panel answers front_panel_selftest's second command and the
 * poll after it with (0x53842, 0x53848). */
static const uint8_t FRONT_PANEL_SELFTEST_FIRST_ANSWER = 0x55;

static const uint8_t FRONT_PANEL_SELFTEST_SECOND_ANSWER = 0xaa;

/* The front panel task's event, which pps_interrupt signals through
 * front_panel_signal_pps each 1PPS (0x5273a), and how long the task
 * waits for it before polling a key instead (0x48068). */
static const uint32_t FRONT_PANEL_PPS_EVENT = 1;

static const uint32_t FRONT_PANEL_PPS_WAIT_TICKS = 50;

/* Every key lamp, and none. */
static const front_panel_lamp FRONT_PANEL_LAMPS_ALL =
    FRONT_PANEL_LAMP_TIME | FRONT_PANEL_LAMP_LONG | FRONT_PANEL_LAMP_LAT |
    FRONT_PANEL_LAMP_ALT | FRONT_PANEL_LAMP_SAT | FRONT_PANEL_LAMP_STATUS |
    FRONT_PANEL_LAMP_SERIAL_PORT | FRONT_PANEL_LAMP_SHIFT;

static const front_panel_lamp FRONT_PANEL_LAMPS_NONE = 0;

/* A command's start: the latch's reset line high and its frame line low,
 * 2 ticks, the command's byte exchanged on QSPI device 0, 2 ticks; the
 * byte that came back, a key (0x53736). */
front_panel_mode front_panel_command_begin(front_panel_command command) {
  uint16_t reply = 0;
  uint16_t byte = command;
  qspi_buffers buffers = {.transmit = &byte, .receive = &reply};

  latch_bits_write(LATCH_FRONT_PANEL_RESET, LATCH_FRONT_PANEL_FRAME);
  psos_pause(FRONT_PANEL_BYTE_TICKS);
  qspi_read(QSPI_FRONT_PANEL, &buffers, 1);
  psos_pause(FRONT_PANEL_BYTE_TICKS);
  return (front_panel_mode)reply;
}

/* A command's end: the frame line high, then 200 counts
 * (0x53796). */
void front_panel_command_end(void) {
  latch_bits_set(LATCH_FRONT_PANEL_FRAME, 1);
  for (uint8_t spin = 0; spin < FRONT_PANEL_END_SPINS; spin++) {
    /* a delay, which the empty asm keeps from being optimized away */
    __asm__ volatile("");
  }
}

/* Holds the panel in reset, the latch's reset line low, for a second,
 * then lets it go for a second (0x537b2). */
void front_panel_reset(void) {
  latch_bits_set(LATCH_FRONT_PANEL_RESET, 0);
  sleep_seconds(1.0f);
  latch_bits_set(LATCH_FRONT_PANEL_RESET, 1);
  sleep_seconds(1.0f);
}

/* A command with nothing after it; the key that came back. */
static front_panel_mode front_panel_command_alone(front_panel_command command) {
  const front_panel_mode key = front_panel_command_begin(command);

  front_panel_command_end();
  return key;
}

/* The panel's self-test: commands 0, 8, 0x10 and 0, 5 counts apart;
 * passes if 0x55 comes back with 0x10 and 0xaa with the last 0,
 * printing both when verbose (0x537ea). */
uint8_t front_panel_selftest(char verbose) {
  front_panel_command_alone(FRONT_PANEL_POLL);
  busy_wait(FRONT_PANEL_SELFTEST_WAIT);
  front_panel_command_alone(FRONT_PANEL_SELFTEST_FIRST);
  busy_wait(FRONT_PANEL_SELFTEST_WAIT);
  const front_panel_mode first =
      front_panel_command_alone(FRONT_PANEL_SELFTEST_SECOND);
  busy_wait(FRONT_PANEL_SELFTEST_WAIT);
  const front_panel_mode second = front_panel_command_alone(FRONT_PANEL_POLL);
  const bool passed = first == FRONT_PANEL_SELFTEST_FIRST_ANSWER &&
                      second == FRONT_PANEL_SELFTEST_SECOND_ANSWER;

  if (verbose != 0) {
    debug_printf("\r\nTest 1 --- Expected : 0x55\r\n");
    debug_printf("           Received : 0x%x\r\n", (uint32_t)first);
    debug_printf("\r\nTest 2 --- Expected : 0xaa\r\n");
    debug_printf("           Received : 0x%x\r\n", (uint32_t)second);
    debug_printf("\r\nTest Result : %s\r\n", passed ? "Passed" : "Failed");
    debug_printf("\r\n0x%x\r\n0x%x\r\n%d\r\n", (uint32_t)first,
                 (uint32_t)second, (uint32_t)passed);
  }
  return passed;
}

/* The self-test, and on a failure the panel's reset and the test again;
 * nonzero if the panel answers (0x48102). */
uint8_t front_panel_check(char verbose) {
  if (front_panel_selftest(verbose)) {
    return 1;
  }
  front_panel_reset();
  return front_panel_selftest(verbose);
}

/* Command 2 and a lamp byte; the key that came back (0x538d0). */
front_panel_mode front_panel_send_lamps(front_panel_lamp lamps) {
  const front_panel_mode key = front_panel_command_begin(FRONT_PANEL_LAMPS);
  uint16_t byte = lamps;

  qspi_write(QSPI_FRONT_PANEL, &byte, 1);
  front_panel_command_end();
  return key;
}

/* Command 4 and the text to its NUL, 2 ticks before each byte, the lamps
 * as they are; the key that came back (0x5390a). */
front_panel_mode front_panel_send_text(char* text) {
  const front_panel_mode key = front_panel_command_begin(FRONT_PANEL_TEXT);

  for (; *text != '\0'; text++) {
    uint16_t byte = *text;

    psos_pause(FRONT_PANEL_BYTE_TICKS);
    qspi_write(QSPI_FRONT_PANEL, &byte, 1);
  }
  front_panel_command_end();
  return key;
}

/* Command 0 alone, a key poll: the key that came back (0x5395c). */
front_panel_mode front_panel_poll_key(void) {
  return front_panel_command_alone(FRONT_PANEL_POLL);
}

/* Command 1: the lamp byte, then the text to its NUL, 2 ticks after
 * each byte but the last; the key that came back (0x4812e). */
front_panel_mode front_panel_send_line(front_panel_lamp lamps, char* text) {
  const front_panel_mode key = front_panel_command_begin(FRONT_PANEL_LINE);
  uint16_t byte = lamps;

  for (;;) {
    qspi_write(QSPI_FRONT_PANEL, &byte, 1);
    if (*text == '\0') {
      break;
    }
    byte = *text++;
    psos_pause(FRONT_PANEL_BYTE_TICKS);
  }
  front_panel_command_end();
  return key;
}

/* Signals the front panel's task, fpnl, if it runs: pps_interrupt's
 * each 1PPS (0x52732). */
void front_panel_signal_pps(void) {
  if (task_fpnl != 0) {
    psos_signal_v(task_fpnl, FRONT_PANEL_PPS_EVENT);
  }
}

/* The Serial Port key's parity letter: N, E or O, else ? (0x5285a). */
char front_panel_parity_letter(serial_parity parity) {
  switch (parity) {
    case SERIAL_PARITY_NONE:
      return 'N';
    case SERIAL_PARITY_EVEN:
      return 'E';
    case SERIAL_PARITY_ODD:
      return 'O';
    default:
      return '?';
  }
}

/* The Serial Port key's rate in baud (0x52886).  For another setting
 * the image returns what its switch leaves in D0, the setting less 3. */
int32_t front_panel_baud(serial_baud baud) {
  switch (baud) {
    case SERIAL_BAUD_1200:
      return 1200;
    case SERIAL_BAUD_2400:
      return 2400;
    case SERIAL_BAUD_9600:
      return 9600;
    case SERIAL_BAUD_19200:
      return 19200;
    default:
      return (int8_t)baud - SERIAL_BAUD_19200;
  }
}

/* The most satellites front_panel_satellites gives; more is taken for a
 * count not yet settled (0x528f8). */
static const uint8_t FRONT_PANEL_SATELLITES_MOST = 100;

/* The satellites satellites_in_use counts, GPS and pseudolite, with
 * interrupts held off; 0 past 100 (0x528be). */
uint8_t front_panel_satellites(void) {
  uint8_t gps_count;
  uint8_t pseudolites;
  const uint16_t sr = set_interrupt_mask(CPU_IPL_1);

  satellites_in_use(&gps_count, &pseudolites, NULL, NULL);
  set_interrupt_mask(sr);
  const uint8_t count = pseudolites + gps_count;

  return count > FRONT_PANEL_SATELLITES_MOST ? 0 : count;
}

/* VFD DSP TEST with every key lamp lit, then "@;" stepped across the
 * display from the right, a second a step, then blank (0x52b1c). */
void front_panel_display_test(void) {
  static char* const STEPS[] = {
      "           @;", "          @; ", "         @;  ", "        @;   ",
      "       @;    ", "      @;     ", "     @;      ", "    @;       ",
      "   @;        ", "  @;         ", " @;          ", "@;           ",
  };

  front_panel_send_line(FRONT_PANEL_LAMPS_ALL, "VFD DSP TEST");
  sleep_seconds(2.0f);
  for (uint32_t step = 0; step < sizeof STEPS / sizeof STEPS[0]; step++) {
    front_panel_send_text(STEPS[step]);
    sleep_seconds(1.0f);
  }
  front_panel_send_text("");
}

/* A line's text in front_panel_update and front_panel_demo, the
 * original's 26 bytes (link -30, 0x53372), so #define. */
#define FRONT_PANEL_LINE_SIZE 26

/* :GPS:POSition's fields, latitude and longitude in mas and height in
 * cm. */
enum {
  FRONT_PANEL_LATITUDE = 0,
  FRONT_PANEL_LONGITUDE = 1,
  FRONT_PANEL_HEIGHT = 2,
};

/* The position the Long, Lat and Alt keys show: the one held while
 * :GPS:POSition holds (nv_settings.douglas_state), else the survey's
 * once valid, else none (0x5347a to 0x5348c). */
static const int32_t* front_panel_position(void) {
  if (nv_settings.douglas_state != 0) {
    return nv_settings.position;
  }
  if (survey_position_valid != 0) {
    return survey_position;
  }
  return NULL;
}

/* The Long or Lat key's line: the angle with its hemisphere, or the
 * placeholder without a position. */
static void front_panel_angle_text(char* text, uint8_t field, char latitude,
                                   const char* placeholder) {
  const int32_t* position = front_panel_position();

  if (position == NULL) {
    libc_copy(text, placeholder);
    return;
  }
  front_panel_format_dms(text, integer_to_double(position[field]), latitude);
}

/* The Alt key's line: the height in meters, "%+3.2fm", or the
 * placeholder without a position (0x53518). */
static void front_panel_height_text(char* text) {
  static const double CENTIMETERS_PER_METER = 100.0;
  const int32_t* position = front_panel_position();

  if (position == NULL) {
    libc_copy(text, "ALT: +**.**m");
    return;
  }
  sprintf(
      text, "%+3.2fm",
      double_divide_reversed(CENTIMETERS_PER_METER,
                             integer_to_double(position[FRONT_PANEL_HEIGHT])));
}

/* The Time key's line: its label, GPS or UTC with no time zone set,
 * GPS L or LOCAL with one, by @@Aw's mode, and the time (0x533f4). */
static void front_panel_time_text(char* text) {
  time_of_day time;
  const bool local =
      nv_settings.time_zone[0] != 0 || nv_settings.time_zone[1] != 0;
  const bool gps_time = gps.utc_time == ONCORE_TIME_GPS;
  const char* label;

  get_date_time(NULL, &time);
  if (local) {
    label = gps_time ? "GPS L" : "LOCAL";
  } else {
    label = gps_time ? "GPS" : "UTC";
  }
  sprintf(text, "%s %02d:%02d:%02d", label, time.hour, time.minute,
          time.second);
}

/* The Serial Port key's line: channel B's rate, parity, data bits and
 * stop bits (0x5357a). */
static void front_panel_serial_text(char* text) {
  const int32_t stop_bits = (int8_t)duartb_settings.two_stop_bits + 1;
  const int32_t data_bits = (int8_t)duartb_settings.eight_bits + 7;
  const char parity = front_panel_parity_letter(duartb_settings.parity);

  sprintf(text, "%d,%c,%d,%d", front_panel_baud(duartb_settings.baud),
          (int32_t)parity, data_bits, stop_bits);
}

/* With refresh set, the mode's line and its key's lamp; else a key
 * poll; the key that came back, or none for another mode
 * (0x53372). */
front_panel_mode front_panel_update(char refresh, front_panel_mode mode) {
  char text[FRONT_PANEL_LINE_SIZE];

  if (refresh == 0) {
    return front_panel_poll_key();
  }
  switch (mode) {
    case FRONT_PANEL_TIME:
      front_panel_time_text(text);
      return front_panel_send_line(FRONT_PANEL_LAMP_TIME, text);
    case FRONT_PANEL_LONG:
      front_panel_angle_text(text, FRONT_PANEL_LONGITUDE, 0,
                             "LONG: **:**:**.*");
      return front_panel_send_line(FRONT_PANEL_LAMP_LONG, text);
    case FRONT_PANEL_LAT:
      front_panel_angle_text(text, FRONT_PANEL_LATITUDE, 1, "LAT: ***:**:**.*");
      return front_panel_send_line(FRONT_PANEL_LAMP_LAT, text);
    case FRONT_PANEL_ALT:
      front_panel_height_text(text);
      return front_panel_send_line(FRONT_PANEL_LAMP_ALT, text);
    case FRONT_PANEL_SAT:
      sprintf(text, "%d TRACKED", (int32_t)front_panel_satellites());
      return front_panel_send_line(FRONT_PANEL_LAMP_SAT, text);
    case FRONT_PANEL_STATUS:
      front_panel_status_text(text);
      return front_panel_send_line(FRONT_PANEL_LAMP_STATUS, text);
    case FRONT_PANEL_SERIAL_PORT:
      front_panel_serial_text(text);
      return front_panel_send_line(FRONT_PANEL_LAMP_SERIAL_PORT, text);
    case FRONT_PANEL_SHIFT:
      return front_panel_send_line(FRONT_PANEL_LAMP_SHIFT, "SHIFT");
    case FRONT_PANEL_ALARM:
      alarm_text(text);
      return front_panel_send_line(FRONT_PANEL_LAMP_STATUS, text);
    default:
      return FRONT_PANEL_NO_KEY;
  }
}

/* The passes Shift stays armed for, a key or a 1PPS each (0x536b2). */
static const int32_t FRONT_PANEL_SHIFT_PASSES = 60;

/* Shows a message and holds it 2 s: the alarm's clear. */
static void front_panel_message(char* text) {
  front_panel_send_text(text);
  sleep_seconds(2.0f);
}

/* The key handler: a key 1 to 8 the mode; Shift arms for 60 passes, and
 * Alt while armed clears the alarm, "ALRM CLEARED", or says "NO ALARM";
 * mode 9 while the alarm is on, and Status when it goes off; 1 if the
 * line is to be rewritten (0x5362e). */
uint8_t front_panel_key(front_panel_mode key, front_panel_mode* mode,
                        front_panel_mode* previous, uint8_t* armed) {
  if (*mode == FRONT_PANEL_ALARM && led_alarm == 0) {
    *armed = 0;
    *mode = FRONT_PANEL_STATUS;
    *previous = FRONT_PANEL_ALARM;
    front_panel_message("ALRM CLEARED");
  } else if (key == FRONT_PANEL_SHIFT && *mode != FRONT_PANEL_SHIFT) {
    *armed = 1;
    front_panel_shift_passes = 0;
    *previous = *mode;
    *mode = FRONT_PANEL_SHIFT;
  } else if (*mode == FRONT_PANEL_SHIFT) {
    front_panel_shift_passes++;
    if ((int32_t)front_panel_shift_passes >= FRONT_PANEL_SHIFT_PASSES ||
        key == FRONT_PANEL_SHIFT) {
      *armed = 0;
      *mode = *previous;
      *previous = FRONT_PANEL_SHIFT;
    } else if (key == FRONT_PANEL_ALT) {
      *armed = 0;
      *mode = FRONT_PANEL_STATUS;
      *previous = FRONT_PANEL_SHIFT;
      if (led_alarm != 0) {
        status_events_clear();
        front_panel_message("ALRM CLEARED");
      } else {
        front_panel_message("NO ALARM");
      }
    } else {
      return 0;
    }
  } else if (led_alarm != 0) {
    *mode = FRONT_PANEL_ALARM;
    *previous = FRONT_PANEL_ALARM;
    *armed = 0;
  } else if (key >= FRONT_PANEL_TIME && key <= FRONT_PANEL_SHIFT) {
    *mode = key;
  } else {
    return 0;
  }
  return 1;
}

/* The front panel's task, fpnl: blanks the display, waits 2 s and for
 * lamp_test, runs the start-up show, then rewrites the mode's line at
 * each 1PPS signal and after a handled key, else polls a key, taking a
 * key, Shift armed or a change of the alarm to the key handler; runs
 * the panel's self-test verbosely when front_panel_verbose is set, which
 * nothing sets (0x48018). */
void front_panel_task(void) {
  front_panel_mode mode = FRONT_PANEL_TIME;
  front_panel_mode previous = FRONT_PANEL_TIME;
  uint8_t armed = 0;
  bool rewrite = true;
  uint8_t alarm = 0;

  front_panel_send_line(FRONT_PANEL_LAMPS_NONE, "");
  sleep_seconds(2.0f);
  while (*(volatile uint8_t*)&selftest.lamp_testing != 0) {
  }
  front_panel_startup();
  for (;;) {
    const bool signaled =
        (uint8_t)psos_wait_v(FRONT_PANEL_PPS_EVENT, 0,
                             FRONT_PANEL_PPS_WAIT_TICKS, NULL) == 0;
    const front_panel_mode key = front_panel_update(signaled || rewrite, mode);

    rewrite = false;
    if (front_panel_verbose != 0) {
      front_panel_check(1);
      front_panel_verbose = 0;
    }
    if (key == FRONT_PANEL_NO_KEY && armed == 0 && alarm == (int8_t)led_alarm) {
      continue;
    }
    alarm = led_alarm;
    rewrite = front_panel_key(key, &mode, &previous, &armed);
  }
}

/* DEMO MODE: each key shows a fixed line with its lamp, the Time key's
 * a twelve-hour clock from noon counted in 1PPS signals, until Alt
 * follows Shift's line; then EXITING... (0x52c16). */
void front_panel_demo(void) {
  /* The demonstration clock's start, noon. */
  static const uint32_t DEMO_FIRST_HOUR = 12;
  static const uint32_t HOURS = 12;
  front_panel_mode key = FRONT_PANEL_NO_KEY;
  front_panel_lamp lamps = FRONT_PANEL_LAMPS_NONE;
  uint32_t seconds = 0;
  uint32_t hours = 0;
  uint32_t minutes = 0;
  uint32_t second = 0;
  char text[FRONT_PANEL_LINE_SIZE];

  libc_copy(text, "DEMO MODE");
  while (key != FRONT_PANEL_ALT || lamps != FRONT_PANEL_LAMP_SHIFT) {
    switch (key) {
      case FRONT_PANEL_TIME:
        lamps = FRONT_PANEL_LAMP_TIME;
        sprintf(text, "UTC %02d:%02d:%02d", hours + DEMO_FIRST_HOUR, minutes,
                second);
        break;
      case FRONT_PANEL_LONG:
        lamps = FRONT_PANEL_LAMP_LONG;
        libc_copy(text, "E 139:01:23.456");
        break;
      case FRONT_PANEL_LAT:
        lamps = FRONT_PANEL_LAMP_LAT;
        libc_copy(text, "N 35:19:80.123");
        break;
      case FRONT_PANEL_ALT:
        lamps = FRONT_PANEL_LAMP_ALT;
        libc_copy(text, "+57.35m");
        break;
      case FRONT_PANEL_SAT:
        lamps = FRONT_PANEL_LAMP_SAT;
        libc_copy(text, "5 SAT AQUIRD");
        break;
      case FRONT_PANEL_STATUS:
        lamps = FRONT_PANEL_LAMP_STATUS;
        libc_copy(text, "10MHZ STABLE");
        break;
      case FRONT_PANEL_SERIAL_PORT:
        lamps = FRONT_PANEL_LAMP_SERIAL_PORT;
        front_panel_serial_text(text);
        break;
      case FRONT_PANEL_SHIFT:
        lamps = FRONT_PANEL_LAMP_SHIFT;
        libc_copy(text, "SHIFT KEY");
        break;
      default:
        break;
    }
    if ((uint8_t)psos_wait_v(FRONT_PANEL_PPS_EVENT, 0,
                             FRONT_PANEL_PPS_WAIT_TICKS, NULL) == 0) {
      seconds++;
      hours = seconds / CLOCK_SECONDS_PER_HOUR % HOURS;
      minutes = seconds % CLOCK_SECONDS_PER_HOUR / CLOCK_SECONDS_PER_MINUTE;
      second = seconds % CLOCK_SECONDS_PER_MINUTE;
    }
    const front_panel_mode pressed = front_panel_send_line(lamps, text);

    if (pressed != FRONT_PANEL_NO_KEY) {
      key = pressed;
    }
  }
  front_panel_send_line(FRONT_PANEL_LAMPS_NONE, "EXITING...");
}

/* One frame of the start-up show: its key lamps and text. */
typedef struct front_panel_frame {
  front_panel_lamp lamps; /* +0x00 */
  char* text;             /* +0x04 */
} front_panel_frame;

/* The show's key lamps, sweeping out from the middle pair, Alt and
 * Shift, to all eight. */
enum {
  FRONT_PANEL_SWEEP_0 = 0,
  FRONT_PANEL_SWEEP_1 = FRONT_PANEL_LAMP_ALT | FRONT_PANEL_LAMP_SHIFT,
  FRONT_PANEL_SWEEP_2 =
      FRONT_PANEL_SWEEP_1 | FRONT_PANEL_LAMP_LAT | FRONT_PANEL_LAMP_SERIAL_PORT,
  FRONT_PANEL_SWEEP_3 =
      FRONT_PANEL_SWEEP_2 | FRONT_PANEL_LAMP_LONG | FRONT_PANEL_LAMP_STATUS,
  FRONT_PANEL_SWEEP_4 =
      FRONT_PANEL_SWEEP_3 | FRONT_PANEL_LAMP_TIME | FRONT_PANEL_LAMP_SAT,
};

/* The start-up show (0x52e14 to 0x52f68): HP scrolling in from the
 * right, the key lamps sweeping out, then 58503A typed after it, the
 * lamps sweeping back; 100 ticks a frame. */
static const front_panel_frame FRONT_PANEL_SHOW[] = {
    {FRONT_PANEL_SWEEP_0, "           H"},
    {FRONT_PANEL_SWEEP_0, "          HP"},
    {FRONT_PANEL_SWEEP_1, "         HP "},
    {FRONT_PANEL_SWEEP_1, "        HP  "},
    {FRONT_PANEL_SWEEP_2, "       HP   "},
    {FRONT_PANEL_SWEEP_2, "      HP    "},
    {FRONT_PANEL_SWEEP_2, "     HP     "},
    {FRONT_PANEL_SWEEP_3, "    HP      "},
    {FRONT_PANEL_SWEEP_3, "   HP       "},
    {FRONT_PANEL_SWEEP_3, "  HP        "},
    {FRONT_PANEL_SWEEP_4, " HP         "},
    {FRONT_PANEL_SWEEP_4, "HP          "},
    {FRONT_PANEL_SWEEP_3, "HP 5"},
    {FRONT_PANEL_SWEEP_3, "HP 58"},
    {FRONT_PANEL_SWEEP_2, "HP 585"},
    {FRONT_PANEL_SWEEP_2, "HP 5850"},
    {FRONT_PANEL_SWEEP_1, "HP 58503"},
    {FRONT_PANEL_SWEEP_0, "HP 58503A"},
};

/* The show's frames whose keys are taken, HP's scroll (0x52e1e to
 * 0x52ef8). */
static const uint32_t FRONT_PANEL_SHOW_KEYED = 12;

static const uint32_t FRONT_PANEL_SHOW_TICKS = 100;

/* How many passes a warm-up message a key showed is held, counted from
 * 1, and the count the Alt and Shift keys' clears set, which ends it at
 * the next pass (0x5317e, 0x5307e). */
static const uint8_t FRONT_PANEL_HOLD_PASSES = 35;

/* Passes without a refresh before the Status lamp goes out
 * (0x5327c). */
static const uint8_t FRONT_PANEL_LAMP_IDLE_PASSES = 5;

/* Polls, 100 counts apart, the alarm's Shift waits for Alt
 * (0x5330a). */
static const uint8_t FRONT_PANEL_ALARM_POLLS = 30;

static const uint32_t FRONT_PANEL_ALARM_POLL_WAIT = 100;

/* The warm-up's most satellites; more shows 0 (0x531f8). */
static const uint8_t FRONT_PANEL_WARM_UP_SATELLITES_MOST = 8;

/* The warm-up's message for a pll_powerup_suffix_index result, with
 * the lamps as they are; a key poll for another (0x531b0). */
static front_panel_mode front_panel_warm_up_text(pll_powerup_suffix suffix,
                                                 char* text) {
  switch (suffix) {
    case PLL_POWERUP_SUFFIX_NONE:
      return front_panel_send_text("HP 58503A");
    case PLL_POWERUP_SUFFIX_WARM_UP:
      return front_panel_send_text("OCXO WARM UP");
    case PLL_POWERUP_SUFFIX_GPS_ACQUISITION: {
      uint8_t satellites = front_panel_satellites();

      if (satellites > FRONT_PANEL_WARM_UP_SATELLITES_MOST) {
        satellites = 0;
      }
      sprintf(text, "%01d SAT AQUIRD", (int32_t)satellites);
      return front_panel_send_text(text);
    }
    case PLL_POWERUP_SUFFIX_COARSE:
      return front_panel_send_text("COARSE F ADJ");
    case PLL_POWERUP_SUFFIX_FINE_FREQUENCY:
      return front_panel_send_text("FINE F ADJ");
    case PLL_POWERUP_SUFFIX_PHASE_ALIGNMENT:
      return front_panel_send_text("PHASE ALIGN");
    case PLL_POWERUP_SUFFIX_LEAP_SECOND:
      return front_panel_send_text("LEAPSEC CALC");
    default:
      return front_panel_poll_key();
  }
}

/* While the alarm is on, its text; Shift then shows SHIFT and waits up
 * to 30 polls for Alt, which clears the alarm, ALRM CLEARED, or Shift
 * again.  The last key back is the caller's; true if the alarm was shown
 * (0x53294 to 0x53312). */
static bool front_panel_warm_up_alarm(char* text, front_panel_mode* last) {
  bool shown = false;
  front_panel_mode key = *last;

  while (led_alarm != 0) {
    shown = true;
    alarm_text(text);
    key = front_panel_send_text(text);
    if (key != FRONT_PANEL_SHIFT) {
      continue;
    }
    key = front_panel_send_line(FRONT_PANEL_LAMP_SHIFT, "SHIFT");
    bool done = false;

    for (uint8_t poll = 0; poll < FRONT_PANEL_ALARM_POLLS && !done; poll++) {
      if (key == FRONT_PANEL_ALT) {
        status_events_clear();
        front_panel_send_line(FRONT_PANEL_LAMPS_NONE, "ALRM CLEARED");
        done = true;
      } else if (key == FRONT_PANEL_SHIFT) {
        done = true;
      }
      busy_wait(FRONT_PANEL_ALARM_POLL_WAIT);
      key = front_panel_poll_key();
    }
  }
  *last = key;
  return shown;
}

/* The start-up show; the demonstration if Shift came during HP's scroll,
 * the display test and the demonstration if Sat (or keys ORing to 5);
 * then, until the loop locks, the warm-up's messages at each 1PPS or
 * change, the keys' placeholder lines held a while, and the alarm's text
 * while it is on; OUTPUT VALID for 3 s at the end (0x52dea). */
void front_panel_startup(void) {
  static const uint32_t FRAMES =
      sizeof FRONT_PANEL_SHOW / sizeof FRONT_PANEL_SHOW[0];
  /* A line's text, the original's 25 bytes (0x52ffc), so #define. */
  enum { STARTUP_TEXT_SIZE = 25 };
  char text[STARTUP_TEXT_SIZE];
  front_panel_mode keys = FRONT_PANEL_NO_KEY;

  for (uint32_t frame = 0; frame < FRAMES; frame++) {
    const front_panel_mode key = front_panel_send_line(
        FRONT_PANEL_SHOW[frame].lamps, FRONT_PANEL_SHOW[frame].text);

    if (frame < FRONT_PANEL_SHOW_KEYED) {
      keys |= key;
    }
    psos_pause(FRONT_PANEL_SHOW_TICKS);
  }
  if (keys == FRONT_PANEL_SHIFT) {
    front_panel_demo();
  } else if (keys == FRONT_PANEL_SAT) {
    front_panel_display_test();
    front_panel_demo();
  }

  front_panel_mode key = FRONT_PANEL_NO_KEY;
  uint8_t idle = 0;
  uint8_t hold = 0;
  bool armed = false;
  /* The image compares the first pass's suffix with a byte of its frame
   * nothing set (0x52fae); the C takes it for a change. */
  bool first = true;
  pll_powerup_suffix previous = PLL_POWERUP_SUFFIX_NONE;

  while (pll_mode() != PLL_MODE_LOCKED) {
    const pll_powerup_suffix suffix = pll_powerup_suffix_index();
    bool refresh =
        (uint8_t)psos_wait_v(FRONT_PANEL_PPS_EVENT, 0,
                             FRONT_PANEL_PPS_WAIT_TICKS, NULL) == 0 ||
        first || suffix != previous;

    first = false;
    previous = suffix;
    if (armed && key != FRONT_PANEL_ALT && key != FRONT_PANEL_SHIFT) {
      key = FRONT_PANEL_NO_KEY;
    }
    bool held = true;

    switch (key) {
      case FRONT_PANEL_TIME:
        libc_copy(text, "TIME: **:**:**");
        front_panel_send_line(FRONT_PANEL_LAMP_TIME, text);
        break;
      case FRONT_PANEL_LONG:
        libc_copy(text, "LONG: **:**:**.*");
        front_panel_send_line(FRONT_PANEL_LAMP_LONG, text);
        break;
      case FRONT_PANEL_LAT:
        libc_copy(text, "LAT: ***:**:**.*");
        front_panel_send_line(FRONT_PANEL_LAMP_LAT, text);
        break;
      case FRONT_PANEL_ALT:
        if (armed) {
          armed = false;
          libc_copy(text, "NO ALARM");
          front_panel_send_text(text);
          sleep_seconds(3.0f);
          hold = FRONT_PANEL_HOLD_PASSES;
          held = false;
          break;
        }
        libc_copy(text, "ALT: +**.**m");
        front_panel_send_line(FRONT_PANEL_LAMP_ALT, text);
        break;
      case FRONT_PANEL_SAT:
        front_panel_satellites();
        sprintf(text, "%01d TRACKED", (int32_t)front_panel_satellites());
        front_panel_send_line(FRONT_PANEL_LAMP_SAT, text);
        break;
      case FRONT_PANEL_STATUS:
        if (hold != 0) {
          armed = false;
          hold = 0;
          refresh = true;
        }
        held = false;
        break;
      case FRONT_PANEL_SERIAL_PORT:
        front_panel_serial_text(text);
        front_panel_send_line(FRONT_PANEL_LAMP_SERIAL_PORT, text);
        break;
      case FRONT_PANEL_SHIFT:
        if (armed) {
          armed = false;
          hold = FRONT_PANEL_HOLD_PASSES;
          held = false;
          break;
        }
        libc_copy(text, "SHIFT");
        front_panel_send_line(FRONT_PANEL_LAMP_SHIFT, text);
        armed = true;
        break;
      default:
        held = false;
        break;
    }
    if (held) {
      hold = 1;
    }
    key = front_panel_poll_key();
    if (hold != 0 && hold++ >= FRONT_PANEL_HOLD_PASSES) {
      armed = false;
      hold = 0;
      refresh = true;
    }
    if (hold == 0) {
      if (refresh) {
        idle = 0;
        key |= front_panel_send_lamps(FRONT_PANEL_LAMP_STATUS);
        key |= front_panel_warm_up_text(suffix, text);
      } else if (++idle >= FRONT_PANEL_LAMP_IDLE_PASSES) {
        key |= front_panel_send_lamps(FRONT_PANEL_LAMPS_NONE);
      }
    }
    if (front_panel_warm_up_alarm(text, &key)) {
      front_panel_send_line(FRONT_PANEL_LAMPS_NONE, "ALRM CLEARED");
      sleep_seconds(2.0f);
    }
  }
  front_panel_send_line(FRONT_PANEL_LAMPS_NONE, "OUTPUT VALID");
  sleep_seconds(3.0f);
}
