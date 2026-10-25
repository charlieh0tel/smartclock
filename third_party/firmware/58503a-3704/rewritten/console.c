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
 * first from the string device, then its task (0x1317e). */
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

/* Sets the routine halt jumps through (0x1afaa). */
void console_set_halt_hook(void* hook) { console_halt_hook_cell = hook; }

/* Sets the device the console reads and writes (0x1afb4). */
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
 * zero and the last by an empty run, from the image's 0x1b0c2 to
 * 0x1b0e1. */

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

/* Links the word at word to the latest in the dictionary and makes it
 * the latest; past the link.  pforth_link_word (0x1af9a) takes the word
 * in A4 and leaves A4 past the link it stores, the word in D0 and the
 * condition codes changed, in registers C has no names for. */
__attribute__((always_inline)) static inline void* console_link_word(
    void* word) {
  register void* console_cursor __asm__("a4") = word;

  __asm__ volatile("jsr pforth_link_word"
                   : "+r"(console_cursor)
                   :
                   : "d0", "cc", "memory");
  return console_cursor;
}

/* Copies a code template into the dictionary; forth_compile_code only
 * reads it. */
static void* console_compile(const uint16_t* code, void* dp) {
  return forth_compile_code((uint16_t*)code, dp);
}

/* Defines a word that pops its arguments from the Forth stack, the top
 * one first, onto the C stack, so that they reach the C routine left to
 * right, calls the routine and pushes D0, and D1, for a result count of
 * 1 or 2; returns 0, or -1 with no dictionary (0x1aeee). */
uint32_t pforth_define_word(char* name, uint32_t code, int32_t argument_count,
                            uint32_t result_count) {
  static const uint32_t NO_DICTIONARY = 0xffffffff;
  static const int32_t ARGUMENT_BYTES = 4;

  if (pforth_dictionary_next == NULL) {
    return NO_DICTIONARY;
  }
  console_word_header* header =
      console_link_word(console_align(pforth_dictionary_next));
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
 * and stack reports (0x1315a, 0x13166, 0x13172).  The console passes
 * SCPI's source, 0, as the image does (clr.b -(sp)). */
void ps_word(void) { os_process_report(REQUEST_SOURCE_SCPI); }

void mem_rep_word(void) { os_memory_report(REQUEST_SOURCE_SCPI); }

void s_rep_word(void) { task_stack_report(REQUEST_SOURCE_SCPI); }

/* The hook halt runs: lowers the console task below the SCPI task's
 * creator, has it run, and deletes the console's task (0x13124). */
void console_halt_hook(void) {
  psos_priority(OS_TASK_SELF, CONSOLE_HALT_PRIORITY, NULL);
  spoo_run(scpi_task_create, CONSOLE_SCPI_CREATOR_PRIORITY, 0, 0);
  task_console = 0;
  psos_delete(OS_TASK_SELF);
}

/* The words efc_write, hdac_write and hdac_all: the DACs written
 * (0x1b264, 0x1b274, 0x1b2a8). */
void diag_efc_write(uint32_t value) { efc_write(value); }

void diag_hdac_write(uint32_t value, uint32_t channel) {
  hdac_write(value, channel);
}

void diag_hdac_all(uint32_t value_1, uint32_t value_2, uint32_t value_3,
                   uint32_t value_4, uint32_t value_5, uint32_t value_6) {
  hdac_write_all(value_1, value_2, value_3, value_4, value_5, value_6);
}

/* The word doven: the outer oven on or off (0x1bee2). */
void diag_doven(int32_t on) {
  if (on == 0) {
    gpt.portgp &= ~PLL_STAGES_OUTER_OVEN_ENABLE;
  } else {
    gpt.portgp |= PLL_STAGES_OUTER_OVEN_ENABLE;
  }
}

/* The word dmessage: a debug message, then a new line (0x1be22). */
void diag_dmessage(char* message) {
  task_debug_message(message);
  console_write("\r\n");
}

/* The words dmes_gpsm to dmes_curv: a task's debug messages on or off
 * (0x1be3c to 0x1be82). */
void diag_dmes_gpsm(uint32_t on) { dmes[DMES_GPSM] = on; }

void diag_dmes_hmon(uint32_t on) { dmes[DMES_HMON] = on; }

void diag_dmes_klok(uint32_t on) { dmes[DMES_KLOK] = on; }

void diag_dmes_pllp(uint32_t on) { dmes[DMES_PLLP] = on; }

void diag_dmes_root(uint32_t on) { dmes[DMES_ROOT] = on; }

void diag_dmes_scpi(uint32_t on) { dmes[DMES_SCPI] = on; }

void diag_dmes_spoo(uint32_t on) { dmes[DMES_SPOO] = on; }

void diag_dmes_curv(uint32_t on) { dmes[DMES_CURV] = on; }

/* The word dmes_all: every task's debug messages on or off, the nine
 * flags of dmes (0x1be8c). */
void diag_dmes_all(uint32_t on) {
  for (uint8_t i = 0; i < sizeof dmes / sizeof dmes[0]; i++) {
    dmes[i] = on;
  }
}

/* The word wr_eeprom: writes an EEPROM byte; whether it reads back
 * (0x1bea8). */
uint8_t diag_wr_eeprom(char* address, uint32_t value) {
  return eeprom_write_byte(address, value);
}

/* The word clear_nv: the settings records invalidated (0x1bec2). */
void diag_clear_nv(void) { clear_nv(); }

/* The word debug_psos: the kernel's debug output on or off (0x1beca). */
void diag_debug_psos(int32_t on) { psos_debug_output(on != 0); }

/* The words clk_synch and sync_imm: a counter step or sync (0x1b2ba,
 * 0x1b2ce). */
void diag_clk_synch(counter_sync_kind kind, int32_t count) {
  sync_request(kind, count);
}

void diag_sync_imm(void) { sync_request(COUNTER_SYNC_FULL, 1); }

/* The word get_time: the clock's date and time, whether it is not set
 * and whether it is valid (0x1b412). */
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
 * set_timedate's error (0x1b482). */
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
 * gps_command, with its arguments (0x1b4e6 to 0x1b726). */
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

/* The words efc_wr and pr_efc: the loop's EFC set from a 16-bit DAC
 * value, and printed (0x1b79c, 0x1b7ba). */
void diag_efc_wr(uint32_t value) {
  /* pll.efc runs to 1048560, 16 times the DAC's 65535. */
  static const float EFC_PER_DAC_STEP = 16.0f;

  pll.efc = float_multiply(unsigned_to_float(value), EFC_PER_DAC_STEP);
}

void diag_pr_efc(void) { printf("efc = %5f\r\n", float_to_double(pll.efc)); }

/* The words pll_restart, pll_debug, lock and phase_off: the loop's
 * requests and settings (0x1b7e6, 0x1b7f0, 0x1b7fc, 0x1b7fc). */
void diag_pll_restart(void) { pll_restart_requested = 1; }

void diag_pll_debug(uint32_t on) { pll_debug_enabled = on; }

void diag_lock(void) { pll_lock = 1; }

void diag_phase_off(int32_t setpoint) {
  ti_setpoint = integer_to_float(setpoint);
}

/* The words log_send and print_clk_list (0x1be06, 0x1be1a). */
void diag_log_send(uint32_t number) {
  log_write("Log test message %d", number);
}

void diag_print_clk_list(void) { klok_print_entries(); }

/* The word crash: raises the fault its argument names (0x1c0a0). */
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
 * (0x1c12c). */
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

/* The words pr_gps_debug, clr_gps_debug and pr_gps_cmds (0x1b756,
 * 0x1b75e, 0x1b766). */
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
 * (0x1b76e; structures.md, "The GPS task's link"). */
void diag_disable_gps_cmds(int32_t blocked) {
  gps_commands_blocked = blocked != 0;
}

/* The word pstat: the status screen printed every so many seconds, made
 * negative so that gpsm_task prints it at once (0x1b786). */
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

/* The word loop_time: the loop's maximum time (0x1b2e0). */
void diag_loop_time(int32_t seconds) {
  max_loop_time = integer_to_float(seconds);
}

/* The word es_ascii: the event stamps' digits as ASCII or not
 * (0x1c148). */
void diag_es_ascii(int32_t on) { es_ascii = on != 0; }

/* The word fp_u: a float product that underflows, printed (0x1c160). */
void diag_fp_u(void) {
  static const float A = 1e-20f;
  static const float B = 2e-20f;

  printf("%.1f * %.1f = %.1f\r\n", float_to_double(A), float_to_double(B),
         float_to_double(float_multiply(A, B)));
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

/* Appends fine's state, named, to a PLL state (0x1ba56). */
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
