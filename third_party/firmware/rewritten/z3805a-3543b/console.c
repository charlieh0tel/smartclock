/* The pForth console's setup and words, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/console`; docs/firmware/
 * console.md).  The exported C includes this file after its own, so the
 * types and prototypes it declares are in scope; each function here
 * keeps its generated prototype.  It is the console's start, its
 * dictionary's C words, the halt hook, and the diagnostic words in their
 * table's order, the reports last. */

#include <stddef.h>

/* The console's dictionary, in bytes (pforth_dictionary_init's segment
 * size). */
static const uint32_t CONSOLE_DICTIONARY_SIZE = 0x1000;

/* The console task's priority while it runs, and while it hands the port
 * back to SCPI, below the SCPI task's creator (console.md, "Leaving
 * it"). */
static const uint32_t CONSOLE_PRIORITY = 50;

static const uint32_t CONSOLE_HALT_PRIORITY = 26;

static const uint32_t CONSOLE_SCPI_CREATOR_PRIORITY = 25;

/* Starts the console: its dictionary and words, the phrase it reads
 * first from the string device, then its task (0x12faa). */
void console_init(void) {
  /* console_task_create's segments and stack. */
  static const uint32_t BLOCK_A_SIZE = 0x200;
  static const uint32_t BLOCK_B_SIZE = 0x200;
  static const uint32_t STACK_SIZE = 0x180;

  pforth_dictionary_init(CONSOLE_DICTIONARY_SIZE);
  console_set_halt_hook(console_halt_hook);
  pforth_define_word("ps", (uint32_t)ps_word, 0, 0);
  pforth_define_word("mem_rep", (uint32_t)mem_rep_word, 0, 0);
  pforth_define_word("s_rep", (uint32_t)s_rep_word, 0, 0);
  diag_words_define();
  /* The string device keeps the phrase's address past the call, so it
   * is the image's. */
  psos_io_control(IO_DEVICE_CONSOLE_STRING, (uint32_t)console_first_phrase, 0);
  console_set_device(IO_DEVICE_CONSOLE_STRING);
  task_console = console_task_create(BLOCK_A_SIZE, BLOCK_B_SIZE, STACK_SIZE,
                                     CONSOLE_PRIORITY);
}

/* Sets the routine halt jumps through (0x1ae46). */
void console_set_halt_hook(void* hook) { console_halt_hook_cell = hook; }

/* Sets the device the console reads and writes (0x1ae50). */
void console_set_device(int32_t device) { console_device = device; }

/* A word's header as pforth_define_word builds it, after the link
 * pforth_link_word stores (docs/firmware/console.md, structures.md "The
 * console's dictionary"). */
typedef struct console_word_header {
  void* code;         /* +0x00: the word's code, after its name */
  uint16_t fields[2]; /* +0x04: two 16-bit fields, cleared */
  char name[];        /* +0x08: ended by a NUL, then padded to even */
} console_word_header;

/* A 68000 jsr to an absolute long address, which a word compiles to
 * call its C routine. */
static const uint16_t CONSOLE_JSR_ABSOLUTE = 0x4eb9;

/* The code runs forth_compile_code copies into a word, each ended by a
 * zero and the last by an empty run, from the image's 0x1af5e to
 * 0x1af7d. */

/* move.l (A6)+,-(SP): a Forth cell onto the C stack. */
static const uint16_t console_push_argument[] = {0x2f1e, 0, 0};

/* adda.w #n,SP, its n patched: drops the arguments. */
static const uint16_t console_drop_arguments[] = {0xdefc, 0x00ff, 0, 0};

/* move.l D0,-(A6) */
static const uint16_t console_push_result[] = {0x2d00, 0, 0};

/* move.l D1,-(A6) */
static const uint16_t console_push_second_result[] = {0x2d01, 0, 0};

/* rts */
static const uint16_t console_return[] = {0x4e75, 0, 0};

/* An address rounded up to even. */
static void* console_align(void* address) {
  return (void*)(((uint32_t)address + 1) & ~(uint32_t)1);
}

/* Copies a code template into the dictionary; forth_compile_code only
 * reads it. */
static void* console_compile(const uint16_t* code, void* dp) {
  return forth_compile_code((uint16_t*)code, dp);
}

/* Defines a word that pops its arguments from the Forth stack, the top
 * one first, onto the C stack, so that they reach the C routine left to
 * right, calls the routine and pushes D0, and D1, for a result count of
 * 1 or 2; returns 0, or -1 with no dictionary (0x1ad8a). */
uint32_t pforth_define_word(char* name, uint32_t code, int32_t argument_count,
                            uint32_t result_count) {
  static const uint32_t NO_DICTIONARY = 0xffffffff;
  static const int32_t ARGUMENT_BYTES = 4;

  if (pforth_dictionary_next == NULL) {
    return NO_DICTIONARY;
  }
  console_word_header* header =
      pforth_link_word(console_align(pforth_dictionary_next));
  header->code = NULL;
  header->fields[0] = 0;
  header->fields[1] = 0;
  char* end = header->name;
  while ((*end++ = *name++) != '\0') {
  }
  void* dp = console_align(end);
  header->code = dp;
  /* A dbf loop on the count's low word. */
  for (uint16_t left = argument_count; left != 0; left--) {
    dp = console_compile(console_push_argument, dp);
  }
  uint16_t* call = dp;
  *call++ = CONSOLE_JSR_ABSOLUTE;
  *(uint32_t*)call = code;
  dp = call + 2;
  if (argument_count != 0) {
    dp = console_compile(console_drop_arguments, dp);
    ((uint16_t*)dp)[-1] = argument_count * ARGUMENT_BYTES;
  }
  const int16_t results = result_count;
  if (results > 0) {
    dp = console_compile(console_push_result, dp);
    if (results > 1) {
      dp = console_compile(console_push_second_result, dp);
    }
  }
  pforth_dictionary_next = console_compile(console_return, dp);
  return 0;
}

/* The console's words ps, mem_rep and s_rep: the kernel's process, memory
 * and stack reports (0x12f86, 0x12f92, 0x12f9e). */
void ps_word(void) { os_process_report(REQUEST_SOURCE_SCPI); }

void mem_rep_word(void) { os_memory_report(REQUEST_SOURCE_SCPI); }

void s_rep_word(void) { task_stack_report(REQUEST_SOURCE_SCPI); }

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

/* The hook halt runs: lowers the console task below the SCPI task's
 * creator, has it run, and deletes the console's task (0x12f50). */
void console_halt_hook(void) {
  /* Task 0, the caller. */
  static const uint32_t SELF = 0;

  psos_priority(SELF, CONSOLE_HALT_PRIORITY, NULL);
  spoo_run(scpi_task_create, CONSOLE_SCPI_CREATOR_PRIORITY, 0, 0);
  task_console = 0;
  psos_delete(SELF);
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

/* The words efc_write, hdac_write and hdac_all: the DACs written
 * (0x1b282, 0x1b292, 0x1b2a8). */
void diag_efc_write(uint32_t value) { efc_write(value); }

void diag_hdac_write(uint32_t channel, uint32_t value) {
  hdac_write(channel, value);
}

void diag_hdac_all(uint32_t value_1, uint32_t value_2, uint32_t value_3,
                   uint32_t value_4, uint32_t value_5, uint32_t value_6) {
  hdac_write_all(value_1, value_2, value_3, value_4, value_5, value_6);
}

/* The word doven: the outer oven on or off (0x1bebe). */
void diag_doven(int32_t on) {
  if (on == 0) {
    gpt.portgp &= ~PLL_STAGES_OUTER_OVEN_ENABLE;
  } else {
    gpt.portgp |= PLL_STAGES_OUTER_OVEN_ENABLE;
  }
}

/* The word dmessage: a debug message, then a new line (0x1bdfe). */
void diag_dmessage(char* message) {
  task_debug_message(message);
  console_write("\r\n");
}

/* The words dmes_gpsm to dmes_curv: a task's debug messages on or off
 * (0x1be18 to 0x1be5e). */
void diag_dmes_gpsm(uint32_t on) { dmes[DMES_GPSM] = on; }

void diag_dmes_hmon(uint32_t on) { dmes[DMES_HMON] = on; }

void diag_dmes_klok(uint32_t on) { dmes[DMES_KLOK] = on; }

void diag_dmes_pllp(uint32_t on) { dmes[DMES_PLLP] = on; }

void diag_dmes_root(uint32_t on) { dmes[DMES_ROOT] = on; }

void diag_dmes_scpi(uint32_t on) { dmes[DMES_SCPI] = on; }

void diag_dmes_spoo(uint32_t on) { dmes[DMES_SPOO] = on; }

void diag_dmes_curv(uint32_t on) { dmes[DMES_CURV] = on; }

/* The word dmes_all: every task's debug messages on or off, the nine
 * flags of dmes (0x1be68). */
void diag_dmes_all(uint32_t on) {
  for (uint8_t i = 0; i < sizeof dmes / sizeof dmes[0]; i++) {
    dmes[i] = on;
  }
}

/* The word wr_eeprom: writes an EEPROM byte; whether it reads back
 * (0x1be84). */
uint8_t diag_wr_eeprom(char* address, uint32_t value) {
  return eeprom_write_byte(address, value);
}

/* The word clear_nv: the settings records invalidated (0x1be9e). */
void diag_clear_nv(void) { clear_nv(); }

/* The word debug_psos: the kernel's debug output on or off (0x1bea6). */
void diag_debug_psos(int32_t on) { psos_debug_output(on != 0); }

/* The words clk_synch and sync_imm: a counter step or sync (0x1b2d8,
 * 0x1b2ec). */
void diag_clk_synch(counter_sync_kind kind, int32_t count) {
  sync_request(kind, count);
}

void diag_sync_imm(void) { sync_request(COUNTER_SYNC_FULL, 1); }

/* The word get_time: the clock's date and time, whether it is not set
 * and whether it is valid (0x1b430). */
void diag_get_time(void) {
  uint32_t date;
  time_of_day time;
  const bool not_set = get_date_time(&date, &time);
  const uint8_t clock_set = time_valid();

  printf("%02d/%02d/%04d\t%02d:%02d:%02d, %1d, valid: %1d\r\n",
         (uint32_t)clock_middle(date), (uint32_t)clock_low(date),
         (uint32_t)clock_year(date), (uint32_t)time.hour, (uint32_t)time.minute,
         (uint32_t)time.second, (uint32_t)not_set, (uint32_t)clock_set);
}

/* The word set_time: sets the clock's date and time, printing
 * set_timedate's error (0x1b4a0). */
void diag_set_time(uint32_t year, uint32_t month, uint32_t day, uint32_t hour,
                   uint32_t minute, uint32_t second) {
  /* The image leaves the time's reserved byte as its frame holds it. */
  time_of_day time;

  time.hour = hour;
  time.minute = minute;
  time.second = second;
  const uint8_t error =
      set_timedate(clock_pack(year, month, day), clock_time_bits(time));
  if (error != 0) {
    printf("set_timedate error: %2X\r\n", (uint32_t)error);
  }
}

/* The words abr_stat to gps_query post a GPS command for pllp_task,
 * gps_command, with its arguments (0x1b504 to 0x1b706). */
void diag_abr_stat(int32_t interval) {
  gps_command.arguments[0] = interval;
  gps_command.code = GPS_CONSOLE_STATUS_INTERVAL;
}

void diag_print_stat(void) { gps_command.code = GPS_CONSOLE_PRINT_STATUS; }

void diag_stat_rate(int32_t rate) {
  gps_command.arguments[0] = rate;
  gps_command.code = GPS_CONSOLE_STATUS_RATE;
}

void diag_vis_stat_rate(int32_t on) {
  gps_command.arguments[0] = on != 0;
  gps_command.code = GPS_CONSOLE_VISIBILITY_RATE;
}

void diag_print_vis(void) { gps_command.code = GPS_CONSOLE_PRINT_VISIBILITY; }

void diag_ext_msg_rate(int32_t rate) {
  gps_command.arguments[0] = rate;
  gps_command.code = GPS_CONSOLE_EXTENSION_RATE;
}

void diag_xdop_stat_rate(int32_t rate) {
  gps_command.arguments[0] = rate;
  gps_command.code = GPS_CONSOLE_DOP_RATE;
}

void diag_print_ext_msg(void) {
  gps_command.code = GPS_CONSOLE_PRINT_EXTENSION;
}

void diag_print_bc(void) {
  gps_command.code = GPS_CONSOLE_PRINT_BEST_COMBINATIONS;
}

void diag_gps_time(int32_t hour, int32_t minute, int32_t second) {
  gps_command.arguments[0] = hour;
  gps_command.arguments[1] = minute;
  gps_command.arguments[2] = second;
  gps_command.code = GPS_CONSOLE_TIME;
}

void diag_gps_date(int32_t month, int32_t day, int32_t year) {
  gps_command.arguments[0] = month;
  gps_command.arguments[1] = day;
  gps_command.arguments[2] = year;
  gps_command.code = GPS_CONSOLE_DATE;
}

void diag_gps_php(int32_t argument_1, int32_t argument_2, int32_t argument_3,
                  int32_t argument_4) {
  gps_command.arguments[0] = argument_1;
  gps_command.arguments[1] = argument_2;
  gps_command.arguments[2] = argument_3;
  gps_command.arguments[3] = argument_4;
  gps_command.code = GPS_CONSOLE_HOLD_POSITION;
}

void diag_gps_ph(int32_t mode) {
  gps_command.arguments[0] = mode;
  gps_command.code = GPS_CONSOLE_POSITION_HOLD;
}

void diag_gps_cable_dly(int32_t delay) {
  gps_command.code = GPS_CONSOLE_CABLE_DELAY;
  gps_command.arguments[0] = delay;
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

/* The words pll_restart, pll_debug, lock and phase_off: the loop's
 * requests and settings (0x1b7c6, 0x1b7d0, 0x1b7bc, 0x1b7dc). */
void diag_pll_restart(void) { pll_restart_requested = 1; }

void diag_pll_debug(uint32_t on) { pll_debug_enabled = on; }

void diag_lock(void) { pll_lock = 1; }

void diag_phase_off(int32_t setpoint) {
  ti_setpoint = integer_to_float(setpoint);
}

/* The words log_send and print_clk_list (0x1bde2, 0x1bdf6). */
void diag_log_send(uint32_t number) {
  log_write("Log test message %d", number);
}

void diag_print_clk_list(void) { klok_print_entries(); }

/* The word crash: raises the fault its argument names (0x1c07c). */
void diag_crash(uint32_t kind) {
  /* Above every chip select: the bus monitor ends the write. */
  static volatile uint32_t* const UNMAPPED = (volatile uint32_t*)0x700000;

  switch (kind) {
    case CONSOLE_CRASH_BUS_ERROR:
      *UNMAPPED = 1;
      break;
    case CONSOLE_CRASH_ADDRESS_ERROR:
      *(volatile uint32_t*)((uint8_t*)&eeprom_parameter_record + 1) = 1;
      break;
    case CONSOLE_CRASH_ZERO_DIVIDE: {
      volatile uint32_t zero = kind - kind;
      volatile uint32_t quotient = kind / zero;
      break;
    }
    case CONSOLE_CRASH_FLOAT_DIVIDE: {
      double value = unsigned_to_double(kind);

      double_divide_in_place(&value,
                             double_subtract_reversed_pointers(&value, &value));
      break;
    }
    case CONSOLE_CRASH_FATAL_ERROR:
      fatal_error("Force crash from pforth\r\n");
      break;
  }
}

/* The word leapsec: its arguments kept and the leap check run again
 * (0x1c108). */
void diag_leapsec(uint32_t argument_1, uint32_t argument_2) {
  diag_leapsec_argument_1 = argument_1;
  diag_leapsec_argument_2 = argument_2;
  leap_recheck_requested = 1;
}

void diag_eman(int32_t angle) {
  gps_command.arguments[0] = angle;
  gps_command.code = GPS_CONSOLE_MASK_ANGLE;
}

void diag_q_eman(void) { gps_command.code = GPS_CONSOLE_MASK_ANGLE_QUERY; }

void diag_q_gps_php(void) {
  gps_command.code = GPS_CONSOLE_HOLD_POSITION_QUERY;
}

void diag_q_gps_cable_dly(void) {
  gps_command.code = GPS_CONSOLE_CABLE_DELAY_QUERY;
}

void diag_avg(int32_t on) {
  gps_command.arguments[0] = on;
  gps_command.code = GPS_CONSOLE_AVERAGE;
}

void diag_clr_avg(void) { gps_command.code = GPS_CONSOLE_CLEAR_AVERAGE; }

void diag_init_gps(void) { gps_command.code = GPS_CONSOLE_INIT; }

void diag_gps_query_all(void) { gps_command.code = GPS_CONSOLE_QUERY_ALL; }

void diag_quiet_status(void) { gps_command.code = GPS_CONSOLE_QUIET_STATUS; }

void diag_master_reset(void) { gps_command.code = GPS_CONSOLE_MASTER_RESET; }

/* The words pr_gps_debug, clr_gps_debug and pr_gps_cmds (0x1b736,
 * 0x1b73e, 0x1b746). */
void diag_pr_gps_debug(void) { gps_print_process_monitor(); }

void diag_clr_gps_debug(void) { clear_gps_debug(); }

void diag_pr_gps_cmds(void) { gps_print_commands(); }

void diag_pr_avg(void) { gps_command.code = GPS_CONSOLE_PRINT_AVERAGE; }

void diag_pr_gps_istate(void) {
  gps_command.code = GPS_CONSOLE_PRINT_INSTRUMENT_STATE;
}

void diag_pr_gps_id(void) { gps_command.code = GPS_CONSOLE_PRINT_ID; }

void diag_pr_1pps(void) { gps_command.code = GPS_CONSOLE_PRINT_PPS; }

void diag_pr_time_raim(void) { gps_command.code = GPS_CONSOLE_PRINT_TIME_RAIM; }

/* The word disable_gps_cmds: blocks gpsm_task's class 0 and 1 requests
 * (0x1b74e; structures.md, "The GPS task's link"). */
void diag_disable_gps_cmds(int32_t blocked) {
  gps_commands_blocked = blocked != 0;
}

/* The word pstat: the status screen printed every so many seconds, made
 * negative so that gpsm_task prints it at once (0x1b766). */
void diag_pstat(uint32_t seconds) {
  int8_t interval = seconds;

  if (interval > 0) {
    interval = -interval;
  }
  status_screen_print_interval = interval;
}

void diag_gps_change(int32_t request, int32_t argument_1, int32_t argument_2,
                     int32_t argument_3, int32_t argument_4) {
  gps_command.code = GPS_CONSOLE_CHANGE;
  gps_command.request = request;
  gps_command.arguments[0] = argument_1;
  gps_command.arguments[1] = argument_2;
  gps_command.arguments[2] = argument_3;
  gps_command.arguments[3] = argument_4;
}

void diag_gps_query(int32_t request) {
  gps_command.code = GPS_CONSOLE_QUERY;
  gps_command.request = request;
}

/* The word loop_time: the loop's maximum time (0x1b2fe). */
void diag_loop_time(int32_t seconds) {
  max_loop_time = integer_to_float(seconds);
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

/* The word es_ascii: the event stamps' digits as ASCII or not
 * (0x1c124). */
void diag_es_ascii(int32_t on) { es_ascii = on != 0; }

/* The word fp_u: a float product that underflows, printed (0x1c13c). */
void diag_fp_u(void) {
  static const float A = 1e-20f;
  static const float B = 2e-20f;

  printf("%.1f * %.1f = %.1f\r\n", float_to_double(A), float_to_double(B),
         float_to_double(float_multiply(A, B)));
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

/* Appends fine's state, named, to a PLL state (0x1ba32). */
void print_fine_state(pll_fine_state fine_state, char* state) {
  char* name;

  switch (fine_state) {
    case PLL_FINE_START:
      name = ": fine start";
      break;
    case PLL_FINE_SYNC_MEASURE:
      name = ": fine sync meas";
      break;
    case PLL_FINE_SYNC_1:
      name = ": fine sync 1";
      break;
    case PLL_FINE_SYNC_2:
      name = ": fine sync 2";
      break;
    case PLL_FINE_MEASURE_1:
      name = ": fine meas 1";
      break;
    case PLL_FINE_MEASURE_DELAY:
      name = ": fine meas delay";
      break;
    case PLL_FINE_MEASURE_2:
      name = ": fine meas 2";
      break;
    case PLL_FINE_SLEW:
      name = ": fine slew";
      break;
    case PLL_FINE_SLEW_MEASURE:
      name = ": fine slew meas";
      break;
    case PLL_FINE_FINE_SLEW:
      name = ": fine fine slew";
      break;
    default:
      name = ": fine unknown";
      break;
  }
  strcat(state, name);
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
