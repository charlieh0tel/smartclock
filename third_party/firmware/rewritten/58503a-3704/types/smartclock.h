/* The SmartClock firmware's structures, as the code that uses them shows
 * them, for SmartClockSetup.java's types= argument.  A field is named
 * only where docs/firmware/structures.md gives the code that shows what
 * it is; the rest are field_NN, by offset.  Packed, so that the layout
 * is the image's whatever the host compiler.  The 68000's own
 * alignment, 2 bytes, gives the same for all but the GPS task's
 * records, which its decoders lay out byte by byte; check-layout.c
 * checks them with an m68k compiler ("make check-types"). */

#pragma pack(1)

/* <stdint.h>'s, for Ghidra's C parser, which reads no includes.
 * SmartClockSetup.java makes uint8_t and int8_t Ghidra's byte and sbyte,
 * which the decompiler prints as numbers, not characters. */
typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned long uint32_t;
typedef long int32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;

/* The firmware's coded values.  Packed, so that gcc makes each the
 * smallest integer that holds its values, a byte for all of these;
 * SmartClockSetup.java sizes them so in Ghidra, whose C parser makes
 * every enum 4 bytes. */

#include "../../common/types/task_debug_flag.h"

/* preserved_region_fields.restore, a word: whether system_init restores
 * the region (0x12bb4).  pllp_task sets it each pass; nv_clear_and_reset
 * and system_init's defaults clear it. */
typedef enum __attribute__((packed)) preserved_region_restore {
  PRESERVED_REGION_DISCARD = 0,
  PRESERVED_REGION_RESTORE = 1,
} preserved_region_restore;

/* BEGIN PROPOSED ENUMS */
/* END PROPOSED ENUMS */

#include "../../common/types/gps_framing_state.h"

#include "../../common/types/gps_request_result.h"

/* gps_request_class's sorting of a request code (gps_request_code). */
typedef enum __attribute__((packed)) gps_code_class {
  GPS_CLASS_MESSAGE = 0, /* 0 to 0x37 with argument mode 0 or 1: one message
                            (the 58503A 3704's, 0x4c7ce) */
  GPS_CLASS_SCRIPT = 1,  /* 0x52 to 0x65: a script */
  GPS_CLASS_STEP = 2,    /* 0x38 to 0x51: gps_console_request_58503a's */
  GPS_CLASS_INVALID = 3,
} gps_code_class;

#include "../../common/types/oncore_field_code.h"

#include "../../common/types/oncore_receiver_status.h"

/* Which receiver gps_receiver_status_update updates: the Oncore's
 * status, or the alternate receiver's, which it ignores, returning at
 * once (0x4b168); gpsm_task and status_snapshot call it with each. */
typedef enum __attribute__((packed)) gps_receiver_number {
  GPS_RECEIVER_ONCORE = 0,
  GPS_RECEIVER_ALTERNATE = 1,
} gps_receiver_number;

/* The console's override of the GPS reference's validity,
 * reference_valid_override_request (0x100c68), which force_1pps
 * (diag_force_1pps, 0x1b68c) sets from -1, 0 or 1. */
typedef enum __attribute__((packed)) gps_reference_override {
  GPS_REFERENCE_OVERRIDE_NONE = 0x00,
  GPS_REFERENCE_OVERRIDE_VALID = 0x01,
  GPS_REFERENCE_OVERRIDE_INVALID = 0xff,
} gps_reference_override;

/* A reference's validity, gps_reference_record.valid, from its state:
 * :SOURce:GPSystem:REFerence:VALid? answers 1 for valid with its 1PPS
 * alive, or questionable (0x2d0a0); the loop takes a valid reference with
 * its 1PPS alive, and a questionable one in its PLL stages
 * (gps_reference_update, 0x44f48). */
typedef enum __attribute__((packed)) gps_reference_validity {
  GPS_REFERENCE_INVALID = 0,
  GPS_REFERENCE_VALID = 1,        /* GPS_SYNC_VALID, or forced valid */
  GPS_REFERENCE_QUESTIONABLE = 2, /* GPS_SYNC_QUESTIONABLE and _CLEARING */
} gps_reference_validity;

/* The EFC's mode, diag_efc_mode, :DIAGnostic:ROSCillator:EFControl:MODE
 * NORM, USER or TIM (0x2b5a8, 0x2b5f6): in either of the last two
 * efc_mode_done holds the loop over for HOLD_CAUSE_EFC_CONTROL, and in
 * the timed one efc_mode_expire ends it after 600 passes. */
typedef enum __attribute__((packed)) efc_mode {
  EFC_MODE_NORMAL = 0,
  EFC_MODE_USER = 1,
  EFC_MODE_TIMED = 2,
} efc_mode;

#include "../../common/types/counter_sync_kind.h"

#include "../../common/types/satellite_health_class.h"

/* A mode line of the status screen, status_screen_mode_line.mode, which
 * status_screen picks by pll_screen_mode (0x4a9be, its table at
 * 0x4a9d4) and marks ">>"; status_screen_mode_suffix takes power-up's
 * or recovery's. */
typedef enum __attribute__((packed)) status_screen_line {
  STATUS_SCREEN_LINE_POWERUP = 0,  /* "Power-up" */
  STATUS_SCREEN_LINE_HOLDOVER = 1, /* "Holdover" */
  STATUS_SCREEN_LINE_RECOVERY = 2, /* "Recovery" */
  STATUS_SCREEN_LINE_LOCKED = 3,   /* "Locked to " */
  STATUS_SCREEN_LINE_NONE = 4      /* no line; the table's end */
} status_screen_line;
/* A reference's state, which gps_receiver_status_update keeps in the
 * 58503A 3704 (0x4b8a2), gps_reference_record.state, and
 * status_snapshot_record's copies; the status line picker (0x4a1fc) by
 * its switch on it (docs/firmware/revisions.md, "Reference state"). */
typedef enum __attribute__((packed)) gps_sync_status {
  GPS_SYNC_RECEIVER_FAILED = 0,       /* "Invalid: GPS rcvr failed": no @@Ba or
                                         @@Ea for 36 output intervals */
  GPS_SYNC_NOT_TRACKING = 1,          /* "Invalid: not tracking" */
  GPS_SYNC_INACCURATE_POSITION = 2,   /* "Invalid: inacc position": neither
                                         holding nor surveying */
  GPS_SYNC_NO_UTC_DATA = 3,           /* "Invalid: No UTC data"; never stored */
  GPS_SYNC_RAIM_ERROR = 4,            /* "Invalid: Time RAIM error" */
  GPS_SYNC_ASSESSING = 5,             /* "Assessing stability", for
                                         gps_reference_record.validation_time */
  GPS_SYNC_QUESTIONABLE = 6,          /* "Questionable accuracy": a fault after
                                         state 8, held for the hysteresis */
  GPS_SYNC_QUESTIONABLE_CLEARING = 7, /* "Questionable accuracy": the fault
                                         gone, held for the margin */
  GPS_SYNC_VALID = 8,
} gps_sync_status;

/* raim_monitor's assessment of Time RAIM (0x4b01e), gps.raim_assessment:
 * gps_print_time_raim's "Assessment:" strings (0x48560's table). */
typedef enum __attribute__((packed)) gps_raim_assessment {
  GPS_RAIM_INACTIVE = 0,
  GPS_RAIM_OK = 1,
  GPS_RAIM_ALARM_DETECTED = 2, /* not OK since raim_suspect_time */
  GPS_RAIM_ALARM = 3,          /* not OK for 4 s */
} gps_raim_assessment;

/* The command monitor's state, gps.command_monitor_state: command_monitor's
 * switch (0x4b4b0) and gps_print_process_monitor's strings (0x4ceae's
 * table). */
typedef enum __attribute__((packed)) gps_command_monitor_state {
  GPS_COMMAND_MONITOR_DISABLED = 0,
  GPS_COMMAND_MONITOR_STANDBY = 1,
  GPS_COMMAND_MONITOR_QUERY = 2,     /* a command went unanswered; queries */
  GPS_COMMAND_MONITOR_QUERY_ACK = 3, /* waits for the query's result */
} gps_command_monitor_state;

/* The status monitor's state, gps.status_monitor_state: command_monitor's
 * switch (0x4b5ec) and gps_print_process_monitor's strings (0x4cf22's
 * table). */
typedef enum __attribute__((packed)) gps_status_monitor_state {
  GPS_STATUS_MONITOR_DISABLED = 0,
  GPS_STATUS_MONITOR_STANDBY = 1,  /* watches @@Ba come */
  GPS_STATUS_MONITOR_RECOVERY = 2, /* waits for the outputs' request */
} gps_status_monitor_state;

/* @@AB: the application type, gps.application_type:
 * gps_print_instrument_state's "APPL" strings (0x48a7c's table). */
typedef enum __attribute__((packed)) oncore_application_type {
  ONCORE_APPLICATION_AIR = 0,
  ONCORE_APPLICATION_HANDHELD = 1,
  ONCORE_APPLICATION_LAND = 2,
  ONCORE_APPLICATION_MARINE = 3,
  ONCORE_APPLICATION_STATIC = 4,
} oncore_application_type;

/* @@Ah: the satellite selection, gps.satellite_select:
 * gps_print_instrument_state's "SELECTION" strings (0x48cf8's table). */
typedef enum __attribute__((packed)) oncore_satellite_select {
  ONCORE_SELECT_AUTOMATIC = 0,
  ONCORE_SELECT_MANUAL = 1,
  ONCORE_SELECT_HIGHEST_IN_SKY = 2,
} oncore_satellite_select;

/* @@Ar: the fix algorithm, gps.fix_algorithm:
 * gps_print_instrument_state's "FIX ALG" strings. */
typedef enum __attribute__((packed)) oncore_fix_algorithm {
  ONCORE_FIX_BEST_FOUR = 0,   /* "Best 4 satellites" */
  ONCORE_FIX_ALL_IN_VIEW = 1, /* "N-in-view" */
} oncore_fix_algorithm;

/* The loop's stage, pll.stage: the console's "PLL state: " strings
 * (docs/firmware/loop.md, "The stages"). */
typedef enum __attribute__((packed)) pll_stage {
  PLL_STAGE_INVALID = 0,
  PLL_STAGE_POWERUP = 1,
  PLL_STAGE_POWERUP_RECOVERY = 2,
  PLL_STAGE_HOLDOVER = 3,
  PLL_STAGE_HOLDOVER_RECOVERY = 4,
  PLL_STAGE_STARTUP_PLL = 5,
  PLL_STAGE_NORMAL_PLL = 6,
  PLL_STAGE_DIAG = 7,
  PLL_STAGE_IDLE = 8,
  PLL_STAGE_FATAL_ERROR = 9
} pll_stage;

/* Why the loop holds over, pll.hold_causes, a set of these bits: the
 * console's pr_hold_cause prints each as "Holdover due to ..."
 * (docs/firmware/loop.md).  enter_holdover logs a code for each, and
 * holdover_waiting_reason answers :HOLDover:WAITing? with one.  None is
 * pr_hold_cause's "No holdover cause"; without it Ghidra prints a byte's
 * 0 as the complement of all eight bits, which C promotes to an int
 * that no byte equals.  In the 58503A 3704 a word, with bit 8
 * (revisions.md, "Holdover cause"). */
typedef enum __attribute__((packed)) pll_hold_cause {
  HOLD_CAUSE_NONE = 0x00,
  HOLD_CAUSE_MANUAL = 0x01,
  HOLD_CAUSE_GPS = 0x02,
  HOLD_CAUSE_TI_ERROR = 0x04,
  HOLD_CAUSE_TI_LIMIT_EXCEEDED = 0x08,
  HOLD_CAUSE_HARDWARE_FAILURE = 0x10,
  HOLD_CAUSE_TEMPORARY_MODE = 0x20,
  HOLD_CAUSE_1PPS_SELECTION = 0x40,
  HOLD_CAUSE_GPS_COMMAND_MONITOR =
      0x80, /* set in holdover recovery while the GPS command monitor has
               failed; logged as plain "Holdover started" */
  HOLD_CAUSE_EFC_CONTROL =
      0x100 /* the 58503A 3704's: set while :DIAG:ROSC:EFControl:MODE is not
               0 (0x45c1c), cleared with the mode after 600 passes in mode 2
               (0x45c46); pr_hold_cause does not test it */
} pll_hold_cause;

/* An ADC channel in the 58503A 3704, adc_read's argument (0x1f0ec) and
 * the index of hmon_channels and adc_monitors, named by its
 * hmon_channels entry (0x2238a).  adc_read selects the ADC's input as
 * the channel shifted right one, plus 4 for an odd channel, so that
 * channels 0 to 7 are the Z3805A 3543B's inputs 0, 4, 1, 5, 2, 6, 3 and
 * 7: the same inputs as the Z3805A's readings, numbered otherwise
 * (docs/firmware/58503a-3704.md, "The enums"). */
typedef enum __attribute__((packed)) adc_channel {
  ADC_TEMPERATURE = 0,  /* "Temperature"; :DIAG:TEMPerature? */
  ADC_5V = 1,           /* "5V"; the oven's and -15V's converts subtract it */
  ADC_CHANNEL_2 = 2,    /* no convert routine and an empty name; the Z3805A's
                           double oven input */
  ADC_OVEN = 3,         /* "Oven"; oven_warmed_up and double_oven_warmed_up
                           (0x2233c, 0x22364) */
  ADC_PLUS_15V = 4,     /* "15V" */
  ADC_MINUS_15V = 5,    /* "-15V" */
  ADC_OVEN_CURRENT = 6, /* "Primary oven current"; :DIAG:ROSC:CURRent?
                           (0x2b526), diag_cal (0x1bf32), efc_history_update
                           and the loop's stages */
  ADC_ANTENNA_CURRENT = 7, /* "Antenna current"; :DIAG:GPS:ACURrent? */
} adc_channel;

#include "../../common/types/satellite_kind.h"

#include "../../common/types/satellite_list.h"

#include "../../common/types/counter_error.h"

#include "../../common/types/scpi_param_type.h"

/* The VP Oncore reference's codes (third_party/motorola/VPCommands.pdf). */

/* @@Ba: a channel's tracking mode. */
typedef enum __attribute__((packed)) oncore_channel_mode {
  ONCORE_CODE_SEARCH = 0,
  ONCORE_CODE_ACQUIRE = 1,
  ONCORE_AGC_SET = 2,
  ONCORE_FREQUENCY_ACQUIRE = 3,
  ONCORE_BIT_SYNC_DETECT = 4,
  ONCORE_MESSAGE_SYNC_DETECT = 5,
  ONCORE_SATELLITE_TIME_AVAILABLE = 6,
  ONCORE_EPHEMERIS_ACQUIRE = 7,
  ONCORE_AVAILABLE_FOR_POSITION = 8
} oncore_channel_mode;

/* @@Ba: which DOP it gives. */
typedef enum __attribute__((packed)) oncore_dop_type {
  ONCORE_DOP_PDOP = 0, /* in 3D mode */
  ONCORE_DOP_HDOP = 1  /* in 2D mode */
} oncore_dop_type;

/* @@Aj and @@Bc: the xDOP type. */
typedef enum __attribute__((packed)) oncore_xdop_type {
  ONCORE_XDOP_GDOP = 0,
  ONCORE_XDOP_PDOP = 1,
  ONCORE_XDOP_HDOP = 2,
  ONCORE_XDOP_VDOP = 3,
  ONCORE_XDOP_TDOP = 4
} oncore_xdop_type;

#include "../../common/types/oncore_health.h"

/* @@Aw: the time the engine reports. */
typedef enum __attribute__((packed)) oncore_time_mode {
  ONCORE_TIME_GPS = 0,
  ONCORE_TIME_UTC = 1
} oncore_time_mode;

/* @@Cg: fix or idle. */
typedef enum __attribute__((packed)) oncore_fix_mode {
  ONCORE_IDLE = 0,
  ONCORE_POSITION_FIX = 1,
  ONCORE_FIX_MODE_QUERY = 2 /* sent only: "Send current receiver mode" */
} oncore_fix_mode;

/* @@Af and @@As: the height's type. */
typedef enum __attribute__((packed)) oncore_height_type {
  ONCORE_HEIGHT_GPS = 0, /* above the GPS ellipsoid */
  ONCORE_HEIGHT_MSL = 1
} oncore_height_type;

/* @@Bj: the leap second pending. */
typedef enum __attribute__((packed)) oncore_leap_pending {
  ONCORE_NO_LEAP_SECOND = 0,
  ONCORE_LEAP_SECOND_ADD = 1,
  ONCORE_LEAP_SECOND_SUBTRACT = 2
} oncore_leap_pending;

/* @@Bn: the 1PPS control mode. */
typedef enum __attribute__((packed)) oncore_pps_mode {
  ONCORE_PPS_OFF = 0,
  ONCORE_PPS_ON = 1,
  ONCORE_PPS_WHEN_TRACKING = 2, /* tracking at least one satellite */
  ONCORE_PPS_WHEN_RAIM_OK = 3   /* Time RAIM confirms the alarm limit */
} oncore_pps_mode;

/* @@Bn: what the pulse is referenced to; the reverse of @@Aw's codes. */
typedef enum __attribute__((packed)) oncore_pulse_sync {
  ONCORE_PULSE_UTC = 0,
  ONCORE_PULSE_GPS = 1
} oncore_pulse_sync;

/* @@Bn: the Time RAIM solution. */
typedef enum __attribute__((packed)) oncore_raim_solution {
  ONCORE_RAIM_OK = 0,
  ONCORE_RAIM_ALARM = 1,
  ONCORE_RAIM_UNKNOWN = 2
} oncore_raim_solution;

/* @@Bn: what Time RAIM can do. */
typedef enum __attribute__((packed)) oncore_raim_status {
  ONCORE_RAIM_DETECT_AND_ISOLATE = 0,
  ONCORE_RAIM_DETECT_ONLY = 1,
  ONCORE_RAIM_NEITHER = 2
} oncore_raim_status;

#include "../../common/types/scpi_validate_function.h"

#include "../../common/types/stdlib.h"

#include "../../common/types/holdover_steering_state.h"

/* startup_pll's and pll_normal's steering, at 0x10205e: a
 * proportional-integral loop on each 10-second block's mean time
 * interval, its gains from the loop's time constant. */
typedef struct loop_steering_state {
  float integral; /* +0x00: the integral term, in EFC counts */
  float ti_sum;   /* +0x04: the block's valid readings, then their mean */
  int32_t large_offset_blocks; /* +0x08: startup_pll's consecutive blocks whose
                                  mean passed 150 ns, to 15 */
  uint8_t seconds;             /* +0x0c: the block's seconds, to 10 */
  uint8_t readings;            /* +0x0d: the block's valid readings */
  uint8_t
      measurement_failed; /* +0x0e: a reading failed; a block with none valid
                             then holds for a TI error, not the limit */
  uint8_t field_0f;
  float
      proportional_gain; /* +0x10: efc_gains.reciprocal_ti over max_loop_time */
  float integral_gain;   /* +0x14: efc_gains.reciprocal_tau over max_loop_time
                            squared, over 4 */
  float filter_weight; /* +0x18: 29.75 over max_loop_time, pll.ti_filtered's */
} loop_steering_state;

#include "../../common/types/serial_ring.h"

/* A serial port's parity code, in its settings record (:PARity). */
typedef enum __attribute__((packed)) serial_parity {
  SERIAL_PARITY_NONE = 0,
  SERIAL_PARITY_EVEN = 1,
  SERIAL_PARITY_ODD = 2,
  SERIAL_PARITY_ONE = 3, /* the SCI's only: the top bit set, mark parity */
} serial_parity;

/* The bank of six LEDs on the board, the DUART's output port bits OP2 to
 * OP7, which duart_output_bits resets (lit, the pin high) and sets
 * (docs/firmware/outputs.md, "The rest of the bank").  The heartbeat is
 * hmon_task's alone, flipped about every 1.1 s; the reset code and the
 * self-tests light the others on a failure, which stay lit. */
typedef enum __attribute__((packed)) board_led {
  BOARD_LED_HEARTBEAT = 0x04, /* OP2, hmon_task's */
  BOARD_LED_INSTALLER = 0x08, /* OP3, the boot code entered the installer */
  BOARD_LED_CPU = 0x10,       /* OP4, the CPU test failed */
  BOARD_LED_RAM = 0x20,       /* OP5, the RAM test failed */
  BOARD_LED_DUART = 0x40,     /* OP6, a DUART test failed */
  BOARD_LED_SELFTEST = 0x80,  /* OP7, a power-up self-test failed */
} board_led;

/* The latch at 0x300000's bits, which selftest.latch shadows and
 * latch_bits_set and its kin write (docs/firmware/58503a-3704.md, "The
 * front panel"): the three lamps lamps_write writes through
 * latch_lamps_write, from :LED:GPSLock?'s, :LED:HOLDover?'s and
 * :LED:ALARm?'s flags (0x23b10), and the front panel's two lines, named by
 * their use and not traced: bit 6, cleared from a command's first byte
 * to its last (front_panel_command_begin and front_panel_command_end), and
 * bit 7, low for a second in front_panel_reset and high otherwise. */
typedef enum __attribute__((packed)) latch_bit {
  LATCH_GPS_LOCK = 0x01,
  LATCH_HOLDOVER = 0x02,
  LATCH_ALARM = 0x04,
  LATCH_FRONT_PANEL_FRAME = 0x40,
  LATCH_FRONT_PANEL_RESET = 0x80,
} latch_bit;

/* A front panel command, its first byte, which front_panel_command_begin
 * exchanges (docs/firmware/58503a-3704.md, "The front panel", the
 * commands' table): a key poll; a line, a lamp byte then text; a lamp
 * byte; text with the lamps as they are; and the two front_panel_selftest
 * sends between polls, after which the panel answers 0x55, then 0xaa. */
typedef enum __attribute__((packed)) front_panel_command {
  FRONT_PANEL_POLL = 0x00,
  FRONT_PANEL_LINE = 0x01,
  FRONT_PANEL_LAMPS = 0x02,
  FRONT_PANEL_TEXT = 0x04,
  FRONT_PANEL_SELFTEST_FIRST = 0x08,
  FRONT_PANEL_SELFTEST_SECOND = 0x10,
} front_panel_command;

/* A front panel key, the byte a command brings back, in the order of
 * front_panel_update's lines (0x533c6) and the panel's keys
 * (097-58503-08, 1-2): each key's mode, 1 to 8, and mode 9, the alarm's
 * text, which front_panel_key keeps while the alarm is on. */
typedef enum __attribute__((packed)) front_panel_mode {
  FRONT_PANEL_NO_KEY = 0,
  FRONT_PANEL_TIME = 1,
  FRONT_PANEL_LONG = 2,
  FRONT_PANEL_LAT = 3,
  FRONT_PANEL_ALT = 4,
  FRONT_PANEL_SAT = 5,
  FRONT_PANEL_STATUS = 6,
  FRONT_PANEL_SERIAL_PORT = 7,
  FRONT_PANEL_SHIFT = 8,
  FRONT_PANEL_ALARM = 9,
} front_panel_mode;

/* A front panel key's lamp, bit n - 1 of the lamp byte for key n, which
 * front_panel_update sends with each mode's line; the alarm's text lights
 * the Status key's (docs/firmware/58503a-3704.md, "The keys"). */
typedef enum __attribute__((packed)) front_panel_lamp {
  FRONT_PANEL_LAMP_TIME = 0x01,
  FRONT_PANEL_LAMP_LONG = 0x02,
  FRONT_PANEL_LAMP_LAT = 0x04,
  FRONT_PANEL_LAMP_ALT = 0x08,
  FRONT_PANEL_LAMP_SAT = 0x10,
  FRONT_PANEL_LAMP_STATUS = 0x20,
  FRONT_PANEL_LAMP_SERIAL_PORT = 0x40,
  FRONT_PANEL_LAMP_SHIFT = 0x80,
} front_panel_lamp;

#include "../../common/types/io_device.h"

#include "../../common/types/data_format.h"

#include "../../common/types/timecode_format.h"

/* tcode_send's routine, which timecode_schedule sends the time code
 * through (0x14128): drtb_send_async's shape, which tcode_request stores
 * (0x13d6a). */
typedef void tcode_send_function(char* block, uint8_t count);

#include "../../common/types/klok_event_class.h"

#include "../../common/types/duartb_settings_record.h"

/* The SCI's serial settings, SERial2's, at 0x101f50: one byte for
 * parity and data bits together and no stop-bit setting, as 097-59551-02
 * gives the 59551A's PORT 2.  sci_init and sci_reconfigure apply them.
 * parameter_record_read copies the defaults at 0x30a2c over all 6
 * bytes whatever the EEPROM holds; save_parameter_record writes +2 to +5
 * to the record's bytes 9 to 0xc, which nothing reads back. */
typedef struct sci_settings_record {
  uint8_t field_00[2]; /* +0x00: only the defaults' copy writes them, 0; nothing
                          reads them */
  serial_baud baud;    /* +0x02 */
  serial_parity parity; /* +0x03: NONE with eight data bits, the others with
                           seven */
  uint8_t xon_xoff;     /* +0x04: :PACE XON when nonzero, else NONE */
  uint8_t echo;         /* +0x05: :FDUPlex ON, each character received echoed */
} sci_settings_record;

#include "../../common/types/duartb_applied_settings.h"

/* A routine that writes a character to a port. */
typedef void putc_function(char c);

#include "../../common/types/char_slot.h"

/* A self-test, in the tables at 0x18458 and 0x17fe0: run is 1 to run the
 * test, as selftest_execute's second call and selftest_run_all_counted
 * pass it, and 0 otherwise. */
typedef void selftest_test(char_slot run);
/* An entry of the self-test tables. */
typedef selftest_test* selftest_entry;

#include "../../common/types/dac_measure.h"

/* The 16-byte message a driver sends its exchange once, at start-up, so
 * that a task takes the exchange as a token before using the port: its
 * status, the exchange's id, and the task that holds it, which the
 * driver's interrupts signal. */
typedef struct exchange_token {
  uint32_t status;   /* +0x00: zeroed before the send; nothing reads it */
  uint32_t exchange; /* +0x04 */
  uint32_t task;     /* +0x08: the task waiting on the port, 0 for none */
  uint32_t field_0c;
} exchange_token;

/* DUART channel B's driver state, duartb at 0x103412 and the
 * installer's at 0x10af0c: in the 58503A 3704 the Z3805A 3543B's fields
 * rearranged in the same 0x56 bytes, each where drtb_puts, drtb_gets,
 * drtb_send_async, duartb_isr, duartb_transmit_interrupt,
 * duartb_reconfigure and duart_init address it (0x1d636 to 0x1e900);
 * the receive errors are a byte of their own before duartb_ring
 * (duartb_receive_errors, 0x10330e, 0x1e6a0). */
typedef struct duartb_driver {
  char* transmit_next;    /* +0x00: drtb_puts's next character */
  int16_t transmit_count; /* +0x04: drtb_puts's characters left */
  uint8_t loopback_test;  /* +0x06: set while duart_loopback_test_b runs */
  uint8_t least_free;    /* +0x07: the receive buffer's least free space seen */
  uint8_t reconfiguring; /* +0x08: set while duartb_reconfigure has the port's
                            interrupts off */
  uint8_t flow; /* +0x09: 0x80 an XOFF received, holding transmission; 0x20 one
                   sent */
  uint8_t xon_xoff; /* +0x0a: flow control, from the serial settings */
  uint8_t in_transmit_interrupt; /* +0x0b */
  uint8_t
      read_errors; /* +0x0c: the receive errors as the last read left them */
  uint8_t parity;  /* +0x0d: from the serial settings */
  uint8_t async_count;       /* +0x0e */
  uint8_t async_pending;     /* +0x0f: when the asynchronous send ends, the SCPI
                                task is signaled 0x20 */
  char* async_next;          /* +0x10: drtb_send_async's */
  uint32_t read_exchange;    /* +0x14: drtR */
  uint32_t write_exchange;   /* +0x18: drtW */
  exchange_token read_token; /* +0x1c: its task is signaled 0x800 on a ctrl-C,
                                0x4000 when input comes */
  uint8_t field_2c[8];
  exchange_token write_token; /* +0x34: its task is signaled 0x1000 when
                                 drtb_puts's string is out */
  uint8_t field_44[8];
  uint8_t async_allowed; /* +0x4c: an asynchronous send may start; an XOFF does
                            not stop it */
  uint8_t field_4d;      /* drtb_release_for_timecode sets it, the transmit
                            interrupt tests and clears it */
  uint8_t field_4e;      /* duart_init clears it */
  uint8_t field_4f;
  putc_function* echo;     /* +0x50: drtb_putc, or duartb_echo_none, a bare rts,
                              from the serial settings */
  uint8_t last_terminator; /* +0x54: the CR or LF that ended the last line, so
                              its partner is skipped */
  uint8_t field_55;
} duartb_driver;

/* The QSM's SCI port's exchanges, sciR and sciW, and the token messages
 * sci_init sends them, at 0x103150. */
typedef struct sci_exchange_state {
  uint32_t read_exchange;
  exchange_token read_token; /* +0x04: its task is signaled on a ctrl-C and when
                                input comes */
  uint8_t field_14[8];
  uint32_t write_exchange;    /* +0x1c */
  exchange_token write_token; /* +0x20: its task is signaled 0x200 when
                                 sci_puts's string is out */
  uint8_t field_34[8];
} sci_exchange_state;

/* The SCI port's driver, at 0x10328a, as DUART channel B's: its receive
 * ring is just before it, at 0x103188. */
typedef struct sci_driver {
  char*
      transmit_next; /* +0x00: sci_puts's next character; it sends to the NUL */
  char* async_next;  /* +0x04: sci_send_async's */
  uint8_t async_count;   /* +0x08 */
  uint8_t async_pending; /* +0x09 */
  uint8_t flow; /* +0x0a: 0x80 an XOFF received, holding transmission; 0x20 one
                   sent */
  uint8_t receive_errors; /* +0x0b: SCSR's OR, FE and PF, overrun, framing and
                             parity (MC68331UM D.4.6) */
  uint8_t read_errors;    /* +0x0c: receive_errors as the last read left them */
  uint8_t xon_xoff;       /* +0x0d: flow control, from the serial settings */
  uint8_t mark_bit; /* +0x0e: each character goes out with its top bit set: mark
                       parity, which SCCR1's PE and PT cannot give */
  uint8_t
      seven_bits; /* +0x0f: seven data bits, the top bit received stripped */
  uint8_t reconfiguring; /* +0x10: set while sci_reconfigure changes the port */
  uint8_t field_11;      /* +0x11: sci_send_async needs it clear; only sci_init
                            writes it */
  uint8_t async_allowed; /* +0x12: an asynchronous send may start */
  uint8_t field_13;
  uint16_t baud_constant; /* +0x14: sci_baud_set's per-baud value, halving as
                             the baud doubles; nothing reads it */
  putc_function* echo; /* +0x16: from the serial settings; sci_echo_none, a bare
                          rts, with echo off */
  uint8_t last_terminator; /* +0x1a: the CR or LF that ended the last line */
  uint8_t field_1b;
} sci_driver;

#include "../../common/types/qspi_state.h"

/* A device on the QSPI, by the minor number of I/O device 1, which
 * qspi_queue_setup (0x1f396) takes and sets up a queue for, by its jump
 * table at 0x1f3e2, cases 0 to 0xc; 3 and 8 are unknown to it.  The
 * front panel (one byte), the ADC (one word), the hex DAC (three), the
 * EFC DAC (one), and the FPGA's configuration in blocks of up to 16
 * words and a tail of two, and its readback of 8 to 16 bits
 * (fpga_program 0x225de, fpga_verify_chip 0x23084).  Devices 0 to 7
 * drive their own number on PCS3-PCS0, the FPGA's all 6
 * (docs/firmware/structures.md, "The QSPI's devices"); 0, the front
 * panel's, is one 8-bit transfer (case 0, 0x1f3fc), which
 * front_panel_command_begin and its kin send (docs/firmware/58503a-3704.md,
 * "The front panel").  10 and 11 are one word and one byte to the
 * FPGA, which only the unreferenced qspi_fpga_word_write and
 * qspi_fpga_byte_write (0x1f2fa, 0x1f312) send. */
typedef enum __attribute__((packed)) qspi_device {
  QSPI_FRONT_PANEL = 0,
#include "../../common/types/qspi_device.h"
} qspi_device;

#include "../../common/types/qspi_buffers.h"

/* The EFC's gain and what efc_gain_set derives from it, at 0x102554:
 * pllp_task sets the gain, the fractional frequency one EFC count
 * moves, 6.25e-13. */
typedef struct efc_gain_values {
  float gain;            /* +0x00: fractional frequency a count */
  float reciprocal_fine; /* +0x04: counts per unit fractional frequency, as fine
                            and coarse use it */
  float reciprocal_ti;   /* +0x08: the same, as startup_pll and pll_normal scale
                            the time interval with it */
  float reciprocal_tau; /* +0x0c: the same, as they divide it by the loop's time
                           constant */
  float step_limit;     /* +0x10: 6.25e-10 in counts, the bound startup_pll and
                           pll_normal compare a step with */
  float aging_per_day; /* +0x14: 5e-9 a day in counts a second; nothing reads it
                        */
} efc_gain_values;

#include "../../common/types/efc_range_state.h"

#include "../../common/types/gps_command_mailbox.h"

/* One of the health monitor's eight ADC monitors, adc_monitors at
 * 0x101b72 to 0x101cf1, by hmon_check_supplies' step: temperature,
 * double oven, +15V, oscillator current, 5V, oven, -15V and antenna
 * current.  monitor_average_add keeps a 10-sample moving average in it,
 * monitor_filter_add (the oscillator current's) a 0.9/0.1 filter;
 * adc_average reads it, and monitors_reset clears +0x2c to +0x2f. */
typedef struct adc_monitor {
  float
      samples[10]; /* +0x00: the last ten readings; monitor_average_add only */
  float value;     /* +0x28: their mean, or the filtered value */
  uint8_t valid; /* +0x2c: set once ten readings are in, or the filter's first;
                    adc_average returns a default and monitor_out_of_range false
                    while clear */
  uint8_t sample_index;     /* +0x2d: where the next reading goes, 0 to 9 */
  uint8_t out_of_tolerance; /* +0x2e: hmon_check_supplies' "is out of tolerance"
                               state; double oven, +15V, 5V and -15V only */
  uint8_t out_of_range_count; /* +0x2f: monitor_out_of_range's run of readings
                                 outside the range, held at its limit */
} adc_monitor;

/* The 58503A 3704's status summary text tree, which alarm_text_collect
 * (0x295b0) walks from alarm_text_root (0x29b42) for alarm_text's
 * (0x29740): a node is a status register and its entries, one to the
 * register's bit, an entry a child node, a name ("QUES SUM", "OPER
 * SUM", ...) and the severity alarm_text_collect keeps the least of
 * (+8, 0x29624). */
struct alarm_text_entry;
typedef struct alarm_text_node {
  int8_t register_index; /* +0x00: status_peek_event's index and
                            nv_settings.status_enable's (0x295d8, 0x295ee);
                            less 2 the transition filters' (0x29632) */
  uint8_t field_01;
  struct alarm_text_entry* entries; /* +0x02: to the first with no name */
} alarm_text_node;
typedef struct alarm_text_entry {
  alarm_text_node* child; /* +0x00: null for a leaf */
  char* name;             /* +0x04 */
  uint8_t severity;       /* +0x08 */
  uint8_t field_09;
} alarm_text_entry;

/* The 58503A 3704's alarm text cursor, which alarm_text builds on its
 * stack (link -6, 0x29746) and alarm_text_collect fills: where the text
 * goes and the least severity seen, which alarm_text_collect lowers to
 * a matching entry's (0x29614, 0x2962c). */
typedef struct alarm_text_cursor {
  char* text;       /* +0x00 */
  uint8_t severity; /* +0x04: 4 at first (0x2975a) */
  uint8_t field_05;
} alarm_text_cursor;

/* How a GPS request or script step gives its message's arguments
 * (gps_request_entry.argument_mode, gps_script_step.argument_mode,
 * gps_running_command.query): gps_request_class takes 0 and 1 for a
 * message, gps_print_process_monitor prints them "change/execute" and
 * "query setting", and gpsm_task takes 2 as 0 with gps_request_step's
 * arguments from the index in the step's first (0x4c1f6 to 0x4c212). */
typedef enum __attribute__((packed)) gps_argument_mode {
  GPS_ARGUMENTS_SET = 0,          /* the message with its arguments */
  GPS_ARGUMENTS_QUERY = 1,        /* the message's query form */
  GPS_ARGUMENTS_FROM_REQUEST = 2, /* set, gps_request_step's arguments */
} gps_argument_mode;

/* gps_request_code_word: a gps_request_code in a word, gps_script_step's
 * code and its shorter forms', which gpsm_task reads with move.w and
 * compares signed (0x4c1c6 to 0x4c1d2) and fills from the request's
 * word (0x4c0d4); gps_request_entry's code, which gpsm_task gives
 * gps_request_class (0x4bfd4, 0x4c0a4), which compares it signed
 * (0x4bdc0 to 0x4bdf6), and gps_script_lookup (0x4c140), which
 * sign-extends it (movea.w, 0x4be04); gps_running_command's message,
 * the step's code (0x4c242); and the code gps_request_post,
 * gps_message_supported and gps_response_decode take, a word slot
 * (cmpi.w 0x4bd44, 0x4f8fc; ext.l 0x4faae), and gps_console_request,
 * gps_request_build, gps_response_match and gps_response_timed_out
 * (movea.w 0x4c578; ext.l 0x4f948, 0x4fba2, 0x4b9c0); gps_response_index
 * (0x101396), which gpsm_task passes as a word (0x4c06a, 0x4c386); and
 * gps_command_log_entry's code and message (0x4c686, 0x4c68c).  Its values are
 * gps_request_code's; its one name only
 * makes it a signed word, in gcc's packing and in SmartClockSetup.java's
 * sizing of enums alike, and no step holds it. */
typedef enum __attribute__((packed)) gps_request_code_word {
  GPS_REQUEST_CODE_WORD_MIN = -0x8000,
} gps_request_code_word;

/* A GPS script's step, as gpsm_task runs it.  The one at 0x101b56,
 * gps_request_step, is in RAM: the one-step script gps_script_message
 * (0x50cc2) points at it, and gpsm_task fills it from a request
 * gps_request_class puts in class 0, a message. */
typedef struct gps_script_step {
  gps_request_code_word code;      /* +0x00: gps_request_build's code */
  gps_argument_mode argument_mode; /* +0x02: GPS_ARGUMENTS_FROM_REQUEST from
                                      the index in arguments[0] */
  uint8_t field_03;
  int32_t arguments[4]; /* +0x04: gps_request_step's are also a class 1
                           request's, a script's, and raim_request_arguments' */
} gps_script_step;

/* The ROM's script steps, 0x50a46 to 0x50cc2, are packed end to end,
 * each only as long as the arguments gpsm_task reads from it: none for
 * a query (gps_request_build reads the message's query form), one for
 * GPS_ARGUMENTS_FROM_REQUEST's index, a step of the task's own or a
 * message with no encoder, and otherwise one for each of the message's
 * encoders, which gps_request_build runs on arguments[0] on.  So
 * gps_script_step's four would overlap the next step; these are its
 * shorter forms (docs/firmware/structures.md, "ROM tables"). */
typedef struct gps_script_query_step {
  gps_request_code_word code;      /* +0x00 */
  gps_argument_mode argument_mode; /* +0x02: GPS_ARGUMENTS_QUERY */
  uint8_t field_03;
} gps_script_query_step;

typedef struct gps_script_step_1_argument {
  gps_request_code_word code;      /* +0x00 */
  gps_argument_mode argument_mode; /* +0x02 */
  uint8_t field_03;
  int32_t arguments[1]; /* +0x04 */
} gps_script_step_1_argument;

typedef struct gps_script_step_2_arguments {
  gps_request_code_word code;      /* +0x00 */
  gps_argument_mode argument_mode; /* +0x02 */
  uint8_t field_03;
  int32_t arguments[2]; /* +0x04 */
} gps_script_step_2_arguments;

typedef struct gps_script_step_3_arguments {
  gps_request_code_word code;      /* +0x00 */
  gps_argument_mode argument_mode; /* +0x02 */
  uint8_t field_03;
  int32_t arguments[3]; /* +0x04 */
} gps_script_step_3_arguments;

/* A self-test's number, selftest_run's test and the index of the table
 * at 0x18458 that selftest_execute calls.  :DIAGnostic:TEST? maps its
 * choice to it, alphabetically from the words at 0x53b24 by the jump
 * table at 0x2bc4c; 097-59551-02, ":DIAGnostic:TEST?", lists the words
 * but IREFerence, IMRC and QMRC in this order.  0 is *TST?'s
 * (selftest_request at 0x46326).  IMRC and QMRC are the interpolator's
 * calibration and sample (selftest_interpolator_calibration,
 * selftest_interpolator_sample). */
typedef enum __attribute__((packed)) selftest_test_number {
  SELFTEST_TEST_ALL = 0,
  SELFTEST_TEST_DISPLAY = 1,
  SELFTEST_TEST_PROCESSOR = 2,
  SELFTEST_TEST_RAM = 3,
  SELFTEST_TEST_EEPROM = 4,
  SELFTEST_TEST_UART = 5,
  SELFTEST_TEST_QSPI = 6,
  SELFTEST_TEST_FPGA = 7,
  SELFTEST_TEST_INTERPOLATOR = 8,
  SELFTEST_TEST_INTERNAL_REFERENCE = 9,
  SELFTEST_TEST_GPS = 10,
  SELFTEST_TEST_POWER = 11,
  SELFTEST_TEST_INTERPOLATOR_CALIBRATION = 12,
  SELFTEST_TEST_INTERPOLATOR_SAMPLE = 13,
} selftest_test_number;

/* selftest_summary's failure flags (0x177fa), selftest_results.summary
 * and test 0's result: bit n - 1 for test n that failed.  The display
 * test, 1, has no result, so bit 0 is never set. */
typedef enum __attribute__((packed)) selftest_summary_bit {
  SELFTEST_SUMMARY_CPU = 0x002,
  SELFTEST_SUMMARY_RAM = 0x004,
  SELFTEST_SUMMARY_EEPROM = 0x008,
  SELFTEST_SUMMARY_UART = 0x010,
  SELFTEST_SUMMARY_QSPI = 0x020,
  SELFTEST_SUMMARY_FPGA = 0x040,
  SELFTEST_SUMMARY_INTERPOLATOR = 0x080,
  SELFTEST_SUMMARY_INTERNAL_REFERENCE = 0x100,
  SELFTEST_SUMMARY_GPS = 0x200,
  SELFTEST_SUMMARY_POWER = 0x400,
} selftest_summary_bit;

/* The result of a test with one failure: selftest_results' cpu
 * (cpu_register_test 0x1749e), eeprom (nv_slot_valid 0x32b5e), fpga
 * (counter_selftest 0x40f98: the counters read nonzero after a preset),
 * interpolator ("Interpolator: A, MRC mode" at 0x18015,
 * interpolator_selftest 0x406c8) and internal_reference
 * (internal_pps_test 0x184a4); each selftest_* entry reports its bit 0. */
typedef enum __attribute__((packed)) selftest_failure {
  SELFTEST_FAILED = 0x01,
} selftest_failure;

/* selftest_results.ram, the RAM test's failures, as selftest_ram
 * reports them (0x179ac): a pattern, its data bits at ram_data_bits,
 * or an address line, the lines at ram_address_lines. */
typedef enum __attribute__((packed)) selftest_ram_failure {
  SELFTEST_RAM_PATTERN = 0x01,
  SELFTEST_RAM_ADDRESS_LINE = 0x02,
} selftest_ram_failure;

#include "../../common/types/selftest_uart_failure.h"

/* selftest_results.qspi, the QSPI devices that failed, as
 * qspi_tests_run sets them (0x189f2) and selftest_qspi reports them. */
typedef enum __attribute__((packed)) selftest_qspi_failure {
  SELFTEST_QSPI_ADC = 0x02,
  SELFTEST_QSPI_HEX_DAC = 0x04,
  SELFTEST_QSPI_FPGA = 0x40,
  SELFTEST_QSPI_EFC = 0x80,
} selftest_qspi_failure;

/* selftest_results.gps, the receiver's @@Ca result (VPCommands.pdf,
 * "6 Channel Self-Test (@@Ca)": bit 0 channel 1 to bit 13), by
 * selftest_gps's texts (0x17d94), which name bits 7 to 10 ROM LSByte,
 * ROM MSByte, RAM LSByte and RAM MSByte where the reference's list is
 * garbled.  In the 58503A 3704 the result is a long and the firmware's
 * own two are bits 16 and 17, gps_selftest_run's (0x468d2 to 0x469ee):
 * the engine did not answer, "GPS: SW problem", or timed out, "GPS:
 * Engine timeout" (selftest_gps_58503a, 0x1801c, 0x18030).  An
 * eight-channel engine's @@Fa result, which selftest_gps_58503a reads
 * while gps.field_592 bit 5 is set (0x17e9e), has channels 1 to 8 in
 * bits 0 to 7 and the rest two bits higher, the RTC at 0x8000: the
 * names are @@Ca's. */
typedef enum __attribute__((packed)) selftest_gps_failure {
  SELFTEST_GPS_CHANNEL_1 = 0x0001,
  SELFTEST_GPS_CHANNEL_2 = 0x0002,
  SELFTEST_GPS_CHANNEL_3 = 0x0004,
  SELFTEST_GPS_CHANNEL_4 = 0x0008,
  SELFTEST_GPS_CHANNEL_5 = 0x0010,
  SELFTEST_GPS_CHANNEL_6 = 0x0020,
  SELFTEST_GPS_1_KHZ = 0x0040,
  SELFTEST_GPS_ROM_LOW_BYTE = 0x0080,
  SELFTEST_GPS_ROM_HIGH_BYTE = 0x0100,
  SELFTEST_GPS_RAM_LOW_BYTE = 0x0200,
  SELFTEST_GPS_RAM_HIGH_BYTE = 0x0400,
  SELFTEST_GPS_EEPROM = 0x0800,
  SELFTEST_GPS_DCXO = 0x1000,
  SELFTEST_GPS_RTC = 0x2000,
  SELFTEST_GPS_NO_ANSWER = 0x10000,
  SELFTEST_GPS_TIMED_OUT = 0x20000,
} selftest_gps_failure;

/* selftest_results.power, the supplies out of range, as supplies_test
 * sets them (0x18546). */
typedef enum __attribute__((packed)) selftest_power_failure {
  SELFTEST_POWER_5V = 0x01,
  SELFTEST_POWER_P15V = 0x02,
  SELFTEST_POWER_M15V = 0x04,
} selftest_power_failure;

/* The self-tests' results, at 0x100e02 in the 58503A 3704, 0x48 bytes:
 * each test's word nonzero if it failed, and what selftest_run reports
 * for test n.  The Z3805A 3543B's start-up clear begins at its +0x18,
 * 0x100c3c; the 58503A's is not traced. */
typedef struct selftest_results {
  int16_t cpu;  /* +0x00: the reset code's CPU test, selftest_failure; lights
                   OP4 */
  uint16_t ram; /* +0x02: the RAM test's, selftest_ram_failure's bits; lights
                   OP5 */
  uint16_t ram_data_bits; /* +0x04: the bits that failed a pattern, both halves
                             of a long ORed */
  uint32_t ram_address_lines; /* +0x06: the address lines that alias */
  int16_t uart;    /* +0x0a: duart_early_init's IVR and loopback checks,
                      selftest_uart_failure's bits; lights OP6 */
  latch_bit latch; /* +0x0c: the latch at 0x300000's bits, which latch_bits_set
                      and its kin keep and write (0x136c0 to 0x13778) */
  uint8_t lamp_mask; /* +0x0d: the DUART output bits the failures reset, which
                        primary_reset sets (0x14534) and duart_init writes back
                        (0x1d790) */
  uint16_t summary;  /* +0x0e: selftest_summary_bit's */
  int16_t eeprom;    /* +0x10: selftest_failure */
  int16_t
      fpga; /* +0x12: the FPGA's counters, counter_selftest; selftest_failure */
  uint32_t gps;         /* +0x14: the receiver's self-test, a long in the 58503A
                           3704: gps_selftest_run and gps_diag_sequence store 0x10000
                           or 0x20000 or the @@Ca result (0x468d2 to 0x469ee), which
                           selftest_gps_58503a tests by bit (0x17e28) */
  int16_t interpolator; /* +0x18: selftest_failure */
  uint8_t interpolator_calibrated; /* +0x1a: 1 once a calibration succeeded */
  uint8_t interpolator_tries; /* +0x1b: the calibrations it took, up to 20 */
  uint16_t interpolator_readings[2]; /* +0x1c: both ends' readings,
                                        interpolator_sample_test's */
  uint8_t abort;   /* +0x20: set to stop a test running, selftest_abort */
  uint8_t running; /* +0x21: set while selftest_execute runs a test */
  int16_t power;   /* +0x22: the 5 V, +15 V and -15 V supplies,
                      selftest_power_failure's bits */
  int16_t internal_reference; /* +0x24: the internal 1PPS's, selftest_failure */
  int16_t qspi; /* +0x26: the QSPI's devices, selftest_qspi_failure's bits */
  uint8_t field_28[14];
  uint8_t lamp_testing; /* +0x36: set while lamp_test runs (0x1866e, 0x186c8) */
  uint8_t field_37;
  uint8_t passed; /* +0x38: 1 unless a test selftest_print_failure ran failed */
  uint8_t field_39;
  uint32_t result;     /* +0x3a: selftest_run's test's result, what *TST? and
                          :DIAGnostic:TEST? report */
  uint32_t details[2]; /* +0x3e: the RAM test's bits and lines, or the
                          interpolator's tries and outcome or readings */
  selftest_test_number test; /* +0x46: the test selftest_run was asked for */
  uint8_t in_run; /* +0x47: set during selftest_run; nothing reads it */
} selftest_results;

#include "../../common/types/gps_request_sender.h"

/* A GPS request's code (gps_request_entry.code, a descriptor's
 * gps_request), which gps_request_class (0x4c7c4) sorts: in the 58503A
 * 3704 0 to 0x37 an Oncore message, the index of its entry in
 * oncore_messages (0x51178; the VP Oncore Command Reference names each,
 * third_party/motorola/VPCommands.pdf); 0x38 to 0x51 a step of the GPS
 * task's own, gps_console_request_58503a's (0x4d046, its jump table
 * 0x4d070); 0x52 to 0x65 a script, each named by the messages it sends
 * (docs/firmware/oncore/58503a-3704.txt).  The Z3805A 3543B's 0 to 0x30
 * are the same but for @@Bn to @@Cj, one higher; its steps from 0x31
 * are 7 higher to its average's, 10 higher after the three the 58503A
 * adds and 11 from its link reset's; its scripts from 0x47 are 11
 * higher to its hold position's, 12 after the one the 58503A adds
 * (docs/firmware/58503a-3704.md, "The enums").  The table's empty
 * slots, 0x1a, 0x22, 0x23, 0x25, 0x28, 0x2b and 0x2c, and the steps
 * gps_console_request_58503a does nothing for, 0x4b, 0x4c, 0x4f and
 * 0x51, have no name. */
typedef enum __attribute__((packed)) gps_request_code {
  GPS_REQUEST_TIME_OF_DAY = 0x00,              /* @@Aa */
  GPS_REQUEST_GMT_CORRECTION = 0x01,           /* @@Ab */
  GPS_REQUEST_DATE = 0x02,                     /* @@Ac */
  GPS_REQUEST_LATITUDE = 0x03,                 /* @@Ad */
  GPS_REQUEST_LONGITUDE = 0x04,                /* @@Ae */
  GPS_REQUEST_HEIGHT = 0x05,                   /* @@Af */
  GPS_REQUEST_MASK_ANGLE = 0x06,               /* @@Ag, :GPS:EMANgle */
  GPS_REQUEST_SATELLITE_SELECT_OPTIONS = 0x07, /* @@Ah */
  GPS_REQUEST_MANUAL_SATELLITE_SELECT = 0x08,  /* @@Ai */
  GPS_REQUEST_DOP_TYPE = 0x09,                 /* @@Aj */
  GPS_REQUEST_DOP_HYSTERESIS = 0x0a,           /* @@Ak */
  GPS_REQUEST_DOP_THRESHOLD = 0x0b,            /* @@Al, 3D to 2D */
  GPS_REQUEST_IGNORE_LIST = 0x0c,              /* @@Am, :GPS:SAT:TRAC:IGNore */
  GPS_REQUEST_ALMANAC_UPDATE = 0x0d,           /* @@An */
  GPS_REQUEST_DATUM = 0x0e,                    /* @@Ao */
  GPS_REQUEST_USER_DATUM = 0x0f,               /* @@Ap */
  GPS_REQUEST_IONOSPHERIC_CORRECTION = 0x10,   /* @@Aq */
  GPS_REQUEST_FIX_ALGORITHM = 0x11,            /* @@Ar */
  GPS_REQUEST_POSITION_HOLD_POSITION = 0x12,   /* @@As */
  GPS_REQUEST_POSITION_HOLD = 0x13,            /* @@At */
  GPS_REQUEST_ALTITUDE_HOLD_HEIGHT = 0x14,     /* @@Au, the 58503A's */
  GPS_REQUEST_ALTITUDE_HOLD = 0x15,            /* @@Av */
  GPS_REQUEST_UTC_SELECT = 0x16,               /* @@Aw, UTC or GPS time */
  GPS_REQUEST_EPOCH_OFFSET = 0x17,             /* @@Ax */
  GPS_REQUEST_PPS_OFFSET = 0x18,               /* @@Ay */
  GPS_REQUEST_CABLE_DELAY = 0x19,              /* @@Az, :GPS:ADELay */
  GPS_REQUEST_APPLICATION_TYPE = 0x1b,         /* @@AB */
  GPS_REQUEST_HDOP_THRESHOLD = 0x1c,           /* @@AC, 2D to 0D */
  GPS_REQUEST_CORRECTION_THRESHOLDS = 0x1d,    /* @@AD */
  GPS_REQUEST_POSITION_STATUS = 0x1e,          /* @@Ba */
  GPS_REQUEST_VISIBILITY = 0x1f,               /* @@Bb */
  GPS_REQUEST_DOP_TABLE = 0x20,                /* @@Bc */
  GPS_REQUEST_ALMANAC_STATUS = 0x21,           /* @@Bd */
  GPS_REQUEST_CORRECTION_OUTPUT = 0x24,        /* @@Bh */
  GPS_REQUEST_LEAP_SECOND_PENDING = 0x26,      /* @@Bj */
  GPS_REQUEST_POSITION_EXTENSION = 0x27,       /* @@Bk */
  GPS_REQUEST_TRAIM = 0x29,                    /* @@Bn, Time RAIM */
  GPS_REQUEST_SELFTEST = 0x2a,                 /* @@Ca */
  GPS_REQUEST_CORRECTION_INPUT = 0x2d,         /* @@Ce */
  GPS_REQUEST_DEFAULTS = 0x2e,                 /* @@Cf, set to defaults */
  GPS_REQUEST_FIX_MODE = 0x2f,                 /* @@Cg, fix or idle */
  GPS_REQUEST_IO_FORMAT = 0x30,                /* @@Ci */
  GPS_REQUEST_RECEIVER_ID = 0x31,              /* @@Cj */
  GPS_REQUEST_POSITION_STATUS_8 = 0x32,        /* @@Ea, eight channels' @@Ba */
  GPS_REQUEST_POSITION_EXTENSION_8 = 0x33,     /* @@Ek, eight channels' @@Bk */
  GPS_REQUEST_TRAIM_8 = 0x34,                  /* @@En, eight channels' @@Bn */
  GPS_REQUEST_SELFTEST_8 = 0x35,               /* @@Fa, eight channels' @@Ca */
  GPS_REQUEST_POSITION_STATUS_8_SECOND = 0x36, /* @@Ea again, without a filter
                                                  or a destination
                                                  (docs/firmware/gps.md, "Six
                                                  or eight channels") */
  GPS_REQUEST_FIX_ALGORITHM_SECOND = 0x37,     /* @@Ar again, sent only to
                                                  software 8.4 (0x50b6c) */
  GPS_STEP_STATUS_SNAPSHOT = 0x38,             /* status_snapshot */
  GPS_STEP_PRINT_STATUS = 0x39,
  GPS_STEP_PRINT_VISIBILITY = 0x3a,
  GPS_STEP_PRINT_EXTENSION = 0x3b,
  GPS_STEP_PRINT_BEST_COMBINATIONS = 0x3c,
  GPS_STEP_PRINT_ID = 0x3d,
  GPS_STEP_PRINT_PPS = 0x3e,
  GPS_STEP_PRINT_INSTRUMENT_STATE = 0x3f,
  GPS_STEP_PRINT_TIME_RAIM = 0x40,
  GPS_STEP_PRINT_AVERAGE = 0x41,
  GPS_STEP_AVERAGE_RESET = 0x42, /* gps.average_count = 0 */
  GPS_STEP_AVERAGE = 0x43,       /* the console's avg; gps_console_request
                                    does nothing with it */
  GPS_STEP_GPS_HYSTERESIS =
      0x44, /* the GPS reference's hysteresis from its
               argument (0x4b858);
               :DIAG:REF:GPS:QUES:HYSTeresis's (0x34082) */
  GPS_STEP_EXTERNAL_HYSTERESIS = 0x45, /* the external 1PPS reference's
                                          (0x4b882); nv_settings.field_14's
                                          descriptor's (0x340c0) */
  GPS_STEP_TRACKING_LOG = 0x46,        /* gps.tracking_log, its argument, and
                                          tracking_log_active cleared with it 0;
                                          :DIAG:GPS:TRACking:LOG's (0x340ec) */
  GPS_STEP_SURVEY_START = 0x47,        /* gps.surveying = 1 */
  GPS_STEP_SURVEY_STOP = 0x48,         /* gps.surveying = 0 */
  GPS_STEP_PPS_CHECK = 0x49,      /* gps.pps_check_disabled, its argument */
  GPS_STEP_RAIM_ARGUMENTS = 0x4a, /* raim_request_arguments */
  GPS_STEP_PARSE_OPTIONS = 0x4d,  /* the @@Cj parse (0x50a16) */
  GPS_STEP_STATUS_MONITOR = 0x4e, /* gps.status_monitor_enabled, its argument */
  GPS_STEP_LINK_RESET = 0x50,     /* gps_link_reset */
  GPS_SCRIPT_INIT_DEFAULTS = 0x52, /* @@Cf, then every setting; gps_init too */
  GPS_SCRIPT_INIT = 0x53,          /* every setting, without @@Cf */
  GPS_SCRIPT_FIX_RESTART = 0x54,   /* idle and fix again, the PPS check off */
  GPS_SCRIPT_ENGINE_SELFTEST = 0x55, /* @@Ea off, @@Ca and @@Fa, idle */
  GPS_SCRIPT_OUTPUTS_ON = 0x56,      /* @@Ba, @@Ea and @@Bb each second */
  GPS_SCRIPT_OUTPUTS_OFF = 0x57,     /* @@Ba, @@Ea, @@Bb, @@Bc and @@Bk off */
  GPS_SCRIPT_QUERY_SETTINGS = 0x58,  /* @@Cj, then each setting's query */
  GPS_SCRIPT_UTC_SET = 0x59,         /* @@Aw from its arguments, @@Ba, @@Ea */
  GPS_SCRIPT_HOLD_AT =
      0x5a, /* @@As from its arguments, hold on, TRAIM, restart */
  GPS_SCRIPT_POSITION_SET = 0x5b,  /* @@Ad, @@Ae, @@Af, idle around them */
  GPS_SCRIPT_HOLD_RELEASE = 0x5c,  /* survey stop, average reset, hold off,
                                      TRAIM, restart */
  GPS_SCRIPT_SURVEY_FROM = 0x5d,   /* hold off, @@Ad to @@Af from its
                                      arguments, TRAIM, restart, survey start,
                                      average reset */
  GPS_SCRIPT_HOLD_OFF = 0x5e,      /* hold off, TRAIM, average reset */
  GPS_SCRIPT_HOLD_AT_QUIET = 0x5f, /* @@As, hold on, TRAIM, no restart */
  GPS_SCRIPT_HOLD_SELECT = 0x60,   /* @@At from its arguments, TRAIM, restart */
  GPS_SCRIPT_HOLD_POSITION = 0x61, /* @@As from its arguments, restart */
  GPS_SCRIPT_TRAIM = 0x62,         /* @@Bn and @@En from their arguments, the
                                      58503A's */
  GPS_SCRIPT_TIME_SET = 0x63,      /* @@Aa from its arguments, idle around it */
  GPS_SCRIPT_DATE_SET = 0x64,      /* @@Ac from its arguments, idle around it */
  GPS_SCRIPT_SATELLITE_SELECT = 0x65, /* @@Ah 1, each channel's @@Ai cleared,
                                         then from its arguments */
  GPS_REQUEST_NONE = 0xff,            /* a descriptor's, for none */
} gps_request_code;

#include "../../common/types/gps_request_entry.h"

/* gpsm_task's ring of waiting GPS requests: six entries in the 58503A
 * 3704, where the Z3805A 3543B has five (request_ring_put's bound,
 * +0x9c, at 0x4c3ba), the oldest at read, the next free at write;
 * empty when the two are equal, so it holds five. */
typedef struct gps_request_ring {
  gps_request_entry entries[6];
  gps_request_entry* read;  /* +0x9c */
  gps_request_entry* write; /* +0xa0 */
} gps_request_ring;

/* The GPS command gpsm_task is running, on its stack from -0xaa(A6)'s
 * frame offset (gpsm_task's command local): the request it took from
 * the ring, where it is in the request's script, and the message it
 * last sent, which gps_response_match matches each response against
 * and command_log_add logs.  gps_last_command points at it.  0x72
 * bytes. */
typedef struct gps_running_command {
  gps_request_entry request;     /* +0x00: copied from the ring's oldest */
  gps_script_step** script;      /* +0x1a: the step it runs; the next after each
                                    matched response, a null one ending the script */
  gps_request_code_word message; /* +0x1e: the step's code, an index of
                                    oncore_messages, which gps_request_build
                                    built */
  gps_argument_mode query;       /* +0x20: the step's, as gps_request_build was
                                    given it; the log prints q, else c */
  uint8_t length;                /* +0x21: the built message's bytes */
  uint8_t sent[80];              /* +0x22: the message as sent, copied from
                                    gps_command_message */
} gps_running_command;

#include "../../common/types/time_of_day.h"

#include "../../common/types/gps_command_log_entry.h"

#include "../../common/types/packed_datetime.h"

#include "../../common/types/lifetime_state.h"

/* The EEPROM's records ("EEPROM records" in structures.md), each
 * checksummed by record_checksum in its first two bytes, as their
 * writers build them on their stacks: erased to 0xff, then filled. */

#include "../../common/types/parameter_record.h"

#include "../../common/types/identification_record.h"

/* The calibration block at 0x400080: save_calibration_block's TC and
 * time offset. */
typedef struct calibration_block {
  uint16_t checksum;    /* +0x00 */
  float tc_coefficient; /* +0x02: scpi_value_DIAGnostic_ROSCillator_TCOefficient
                         */
  float time_offset;    /* +0x06: scpi_value_DIAGnostic_TOFFset */
  int16_t field_0a;     /* +0x0a: the 58503A 3704's: the long at 0x10201c,
                           which @@Az's decoder subtracts (0x5003e), cut to a
                           word (0x30b4a) */
  uint8_t unused[52];   /* +0x0c: left erased */
} calibration_block;

/* An NV register record, one of nv_records at 0x4000c0, as nv_save
 * builds it (structures.md, "The NV register"): in the 58503A 3704,
 * 0xa0 bytes (nv_save's frame, link -160 at 0x328bc, and the slot's
 * stride, 0x328e6), its checksum nv_checksum's over bytes 8 to 0x9f
 * (0x32876). */
typedef struct nv_record {
  char tag[2];         /* +0x00: "CQ" */
  uint16_t checksum;   /* +0x02 */
  int32_t sequence;    /* +0x04: one past the stored record's */
  char revision[8];    /* +0x08: "3704" */
  uint32_t packed[36]; /* +0x10: the settings packed into longs, then 0xff */
} nv_record;

#include "../../common/types/reset_record.h"

#include "../../common/types/sprintf.h"

#include "../../common/types/scpi_decimal.h"

#include "../../common/types/scpi_error_message.h"

#include "../../common/types/scpi.h"

/* The two command trees scpi_task gives scpi_task_run, a pair in ROM at
 * 0x61218 (0x294d8), and the installer's at installer::0x1089d4, which
 * installer_scpi_task gives installer_scpi_task_run. */
typedef struct scpi_tree_pair {
  scpi_child_list* common; /* +0x00: the common commands' list, *IDN? on */
  scpi_child_list* main;   /* +0x04: the main tree's root list */
} scpi_tree_pair;

/* scpi_task_run's routines for its two messages, which scpi_task and
 * installer_scpi_task pass: console_write's shape, for "scpi: error
 * opening stream\n" (0x2943c), and printf's, for "scpi: alloc_seg error
 * %#0x\n" (0x2940a). */
typedef unsigned long scpi_task_write_function(char* text);
typedef int scpi_task_print_function(char* format, ...);

/* :SYSTem:LANGuage's choice of what follows the SCPI session
 * (scpi_language), which scpi_task acts on (0x2a0fe to 0x2a136). */
typedef enum __attribute__((packed)) scpi_language_choice {
  SCPI_LANGUAGE_PFORTH = 0,  /* the pForth console */
  SCPI_LANGUAGE_INSTALL = 1, /* the installer */
} scpi_language_choice;

#include "../../common/types/scpi_call.h"

#include "../../common/types/scpi_staged_values.h"

#include "../../common/types/scpi_context.h"

#include "../../common/types/scpi_array.h"

#include "../../common/types/status_register.h"

/* One of port_error_reports' four entries, which scpi_session_init_58503a
 * fills (0x28dfc to 0x28e24) and port_report_errors reads: receive status
 * bits and the condition reported for them, 0xc0 0x15, 0x10 0x16, 0x20
 * 0x14 and 0x01 0x13; the installer's likewise. */
typedef struct port_error_report {
  uint8_t mask;      /* +0x00: the DUART's status bits */
  uint8_t field_01;  /* unread */
  int16_t condition; /* +0x02: a status_condition_code, port_status_report's */
} port_error_report;

/* The installer's status conditions, installer_status_conditions'
 * (installer::0x107aea) and installer_status_report_messages' indexes
 * (installer::0x107b08): none, then its four errors, -360 to -363,
 * which installer_console_port_read reports (installer::0x107554 to
 * installer::0x10758e) where the primary's are STATUS_COMMUNICATION_ERROR
 * to STATUS_INPUT_OVERRUN.  It should type installer_status_report's
 * code. */
typedef enum __attribute__((packed)) installer_status_condition {
  INSTALLER_STATUS_NONE = 0,
  INSTALLER_STATUS_COMMUNICATION_ERROR = 1, /* -360 */
  INSTALLER_STATUS_PARITY_ERROR = 2,        /* -361 */
  INSTALLER_STATUS_FRAMING_ERROR = 3,       /* -362 */
  INSTALLER_STATUS_INPUT_OVERRUN = 4,       /* -363 */
} installer_status_condition;

#include "../../common/types/status_condition_kind.h"

#include "../../common/types/status_condition.h"

/* A condition of the status system, the code status_happening,
 * status_report and the SCPI engine raise.  From 0, an index of the
 * 58503A 3704's status_conditions (0x2f516, 0x60 entries): 1 to 0x16
 * queue the SCPI error beside each (kind 3); from 0x19 each sets (_SET)
 * or clears (_CLEARED) a register's condition bit, or is an event only,
 * as the entry's kind says (1, 2 and 0).  Those names are the bits' in
 * the 58503A's status reporting system (097-59551-02, Figure 5-1 and
 * 5-36 to 5-39); bit 7 of the Standard Event register is IEEE 488.2's
 * power-on.  The Z3805A 3543B's numbering is the same to 0x33; from 0x34
 * the 58503A's table leaves four entries empty before the Hardware
 * register, four where its bit 5 and the Z3805A's do not line up, and
 * more before and after the Questionable register's
 * (docs/firmware/58503a-3704.md, "The enums").  :DIAG:STAT:HAPP n
 * raises n + 0x16, n up to 73 (0x2b74a).  Below 0, minus an index of
 * status_negative_conditions (0x279b6), what the SCPI engine's routines
 * return: -1 to -4 set Standard Event bits 5 to 2, the error classes
 * error_queue_push reports, and the rest queue their SCPI error.  0x17,
 * 0x18, 0x30 to 0x37, 0x42 to 0x45, 0x52 to 0x59, 0x5d to 0x5f, -29 and
 * -58 to -62 do nothing, as 0 does, and but for 0x35, which code raises,
 * have no name; nor have -25 and -26, -171, and -57, -224, which no code
 * shows the cause of.  The SCPI standard's, to 0x16, the same in every
 * image, are common/types/status_scpi.h's, included as the head of the
 * members. */
typedef enum __attribute__((packed)) status_condition_code {
#include "../../common/types/status_scpi.h"
  STATUS_POWER_ON = 0x19,   /* *ESR? bit 7 */
  STATUS_LOCKED_SET = 0x1a, /* :OPERation bit 1, locked to GPS */
  STATUS_LOCKED_CLEARED = 0x1b,
  STATUS_POSITION_HOLD_SET =
      0x1c, /* :OPERation bit 3, position hold, not survey */
  STATUS_POSITION_HOLD_CLEARED = 0x1d,
  STATUS_SATELLITES_SUFFICIENT_SET =
      0x1e, /* :OPERation bit 4, enough satellites to lock */
  STATUS_SATELLITES_SUFFICIENT_CLEARED = 0x1f,
  STATUS_LOG_ALMOST_FULL_SET = 0x20, /* :OPERation bit 6 */
  STATUS_LOG_ALMOST_FULL_CLEARED = 0x21,
  STATUS_FIRST_SATELLITE_SET =
      0x22, /* :POWERup bit 0, first satellite tracked */
  STATUS_FIRST_SATELLITE_CLEARED = 0x23,
  STATUS_OVEN_WARMED_SET = 0x24, /* :POWERup bit 1, oscillator oven warmed up */
  STATUS_OVEN_WARMED_CLEARED = 0x25,
  STATUS_DATE_TIME_VALID_SET = 0x26, /* :POWERup bit 2 */
  STATUS_DATE_TIME_VALID_CLEARED = 0x27,
  STATUS_HOLDING_SET = 0x28, /* :HOLDover bit 0 */
  STATUS_HOLDING_CLEARED = 0x29,
  STATUS_WAITING_SET = 0x2a, /* :HOLDover bit 1, waiting to recover */
  STATUS_WAITING_CLEARED = 0x2b,
  STATUS_RECOVERING_SET = 0x2c, /* :HOLDover bit 2 */
  STATUS_RECOVERING_CLEARED = 0x2d,
  STATUS_HOLDOVER_THRESHOLD_SET =
      0x2e, /* :HOLDover bit 3, longer than :ROSC:HOLD:DUR:THReshold */
  STATUS_HOLDOVER_THRESHOLD_CLEARED = 0x2f,
  STATUS_CONDITION_35 = 0x35, /* does nothing; pll_restart raises it after
                                 STATUS_HOLDOVER_THRESHOLD_CLEARED (0x47668),
                                 the Z3805A 3543B's number for
                                 STATUS_SELFTEST_FAILURE_CLEARED */
  STATUS_CONDITION_36 = 0x36, /* does nothing; status_happening clears it
                                 entering holdover, waiting or recovery
                                 (0x2f29a) */
  STATUS_CONDITION_37 = 0x37, /* does nothing; status_happening clears it
                                 entering lock (0x2f272) */
  STATUS_SELFTEST_FAILURE_SET = 0x38, /* :HARDware bit 0, selftest_summary's
                                         (0x178de) */
  STATUS_SELFTEST_FAILURE_CLEARED = 0x39,
  STATUS_PLUS_15V_SET = 0x3a, /* :HARDware bit 1, +15V supply exceeds
                                 tolerance; hmon_channels' 15V */
  STATUS_PLUS_15V_CLEARED = 0x3b,
  STATUS_MINUS_15V_SET = 0x3c, /* :HARDware bit 2, -15V; hmon_channels' */
  STATUS_MINUS_15V_CLEARED = 0x3d,
  STATUS_PLUS_5V_SET = 0x3e, /* :HARDware bit 3, +5V; hmon_channels' */
  STATUS_PLUS_5V_CLEARED = 0x3f,
  STATUS_OVEN_SUPPLY_SET = 0x40, /* :HARDware bit 4, oven supply exceeds
                                    tolerance; hmon_channels' Primary oven
                                    current */
  STATUS_OVEN_SUPPLY_CLEARED = 0x41,
  STATUS_EFC_NEAR_LIMIT_SET = 0x46, /* :HARDware bit 6, EFC voltage near
                                       full-scale; hmon_check_efc's (0x2366c) */
  STATUS_EFC_NEAR_LIMIT_CLEARED = 0x47,
  STATUS_EFC_AT_LIMIT_SET = 0x48, /* :HARDware bit 7, efc_clamp's (0x235b6) */
  STATUS_EFC_AT_LIMIT_CLEARED = 0x49,
  STATUS_GPS_PPS_FAILURE_SET = 0x4a, /* :HARDware bit 8, GPS 1 PPS failure;
                                        hmon_task's (0x23ca6) */
  STATUS_GPS_PPS_FAILURE_CLEARED = 0x4b,
  STATUS_GPS_FAILURE_SET = 0x4c,        /* :HARDware bit 9 */
  STATUS_GPS_FAILURE_CLEARED = 0x4d,    /* command_monitor_init's (0x4bcba) */
  STATUS_TIME_INTERVAL_ERROR = 0x4e,    /* :HARDware bit 10, an event only
                                           (0x45166) */
  STATUS_EEPROM_WRITE_FAILURE = 0x4f,   /* :HARDware bit 11, an event only
                                           (eeprom_write, 0x3116c) */
  STATUS_INTERNAL_REFERENCE_SET = 0x50, /* :HARDware bit 12, internal reference
                                           failure; hmon_task's (0x23cf4) */
  STATUS_INTERNAL_REFERENCE_CLEARED = 0x51,
  STATUS_TIME_SET = 0x5a, /* :QUEStionable bit 0, time reset, an event only
                             (clock_check_against_gps, 0x43f98) */
  STATUS_USER_REPORTED_SET = 0x5b, /* :QUEStionable bit 1
                                      (questionable_user_done, 0x4348e) */
  STATUS_USER_REPORTED_CLEARED = 0x5c,
} status_condition_code;

/* The 58503A 3704's health monitor's channel table, hmon_channels
 * (0x2238a), by ADC channel, 0 to 7: what reads, averages and checks
 * each.  0x21ca2 converts adc_read's reading through convert;
 * 0x2220a, the monitor's step, calls add, and while check is null or
 * says so, report; adc_average (0x222a6) returns nominal while the
 * channel's adc_monitor is not valid. */
typedef float hmon_convert_function(uint8_t reading);
typedef void hmon_channel_function(adc_channel channel);
typedef uint8_t hmon_channel_test(adc_channel channel);
typedef struct hmon_channel_entry {
  hmon_convert_function* convert; /* +0x00: null for an unused channel */
  hmon_channel_function* add;     /* +0x04: monitor_average_add, or the oven
                                     current's filter, monitor_filter_add_58503a */
  hmon_channel_test* check;      /* +0x08: whether to report; null for always */
  int32_t field_0c;              /* compared at 0x221f8; 0 in every entry */
  hmon_channel_function* report; /* +0x10: the range check and its report,
                                    0x22194 or 0x221aa */
  float minimum;                 /* +0x14: the tolerance's (0x22134) */
  float maximum;                 /* +0x18: (0x2215a) */
  float nominal;                 /* +0x1c: adc_average's default */
  char* name;                    /* +0x20: "Temperature", "5V", ... */
  status_condition_code events[2]; /* +0x24: the happenings out of tolerance
                                      and back within it, 0x16 for none */
  uint16_t bit;                    /* +0x26: 1, 2 or 4 for the supplies */
  uint8_t field_28;
  uint8_t field_29;
} hmon_channel_entry;

#include "../../common/types/status_byte_bit.h"

#include "../../common/types/status_error_entry.h"

#include "../../common/types/status_register_layout.h"

/* One register's entry in the installer's status_register_layouts
 * (installer::0x107ad6), two of them: status_register_layout's first ten
 * bytes, without the nv_settings words, which the installer has none of.
 * installer_status_init sets register 1 up from its entry, its positive
 * filter its defined bits (installer::0x107a52 to 0x107a74). */
typedef struct installer_status_register_layout {
  uint8_t parent_index;  /* +0x00 */
  uint8_t unused;        /* +0x01 */
  uint16_t parent_bit;   /* +0x02 */
  uint16_t maximum;      /* +0x04 */
  uint16_t minimum;      /* +0x06 */
  uint16_t defined_bits; /* +0x08 */
} installer_status_register_layout;

#include "../../common/types/status_register_mask.h"

#include "../../common/types/status_preset_form.h"

#include "../../common/types/status_model.h"

/* The GPS engine's data, as its message decoders store it: each field
 * is a field of the message, in the message's order, as the VP Oncore
 * reference (third_party/motorola/VPCommands.pdf) gives it. */

/* @@Aa, time of day. */
typedef struct oncore_time_fields {
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
} oncore_time_fields;

/* @@Ab, GMT offset. */
typedef struct oncore_gmt_offset_fields {
  uint8_t sign; /* 0 positive, 0xff negative */
  uint8_t hours;
  int8_t minutes; /* the printer also takes a negative one as negative */
} oncore_gmt_offset_fields;

#include "../../common/types/oncore_date_fields.h"

/* @@Ad, @@Ae and @@Af, the current position: angles in milliarcseconds,
 * heights in cm. */
typedef struct oncore_position_fields {
  int32_t latitude;
  int32_t longitude;
  int32_t height_gps; /* above the ellipsoid */
  int32_t height_msl; /* above mean sea level */
} oncore_position_fields;

/* @@As, the position-hold position, as above. */
typedef struct oncore_hold_position_fields {
  int32_t latitude;
  int32_t longitude;
  int32_t height;
} oncore_hold_position_fields;

/* @@Bk, the position/status/data extension.  DOPs, angles, velocities
 * and the matrix in tenths. */
typedef struct oncore_extension_fields {
  uint16_t gdop;
  uint16_t pdop;
  uint16_t hdop;
  uint16_t vdop;
  uint16_t tdop;
  int16_t magnetic_variation; /* degrees, east positive */
  int16_t velocity_north;     /* m/s */
  int16_t velocity_east;
  int16_t velocity_up;
  uint16_t correction_age; /* s, of differential corrections */
  int32_t ecef_x;          /* cm */
  int32_t ecef_y;
  int32_t ecef_z;
  uint8_t ura[8]; /* per channel: @@Ek's eight; @@Bk's decoder (0x525e8)
                     zeroes the last two */
  int16_t least_squares_inverse[10]; /* A-1, upper triangle by rows */
  int16_t clock_bias;                /* m */
  uint16_t oscillator_offset;        /* m/s */
} oncore_extension_fields;

/* The EFC history: a ring of 64 averages of the EFC, one each 2700
 * samples, which the holdover prediction draws on.  Its routine's debug
 * message names its arrays: "e_avg= time= weight= log= tfom=".
 * Cleared as a whole; in the 58503A 3704 0x36a bytes, a long before
 * efc_sum moving the rest 4 bytes on. */
typedef struct efc_ring {
  uint8_t start_ptr;     /* +0x000: the oldest entry */
  uint8_t stop_ptr;      /* +0x001: the next to fill */
  uint8_t loop_settled;  /* +0x002: startup_pll sets it on settling without a
                            fit; aging_state then goes to 3 */
  uint8_t field_003;     /* nothing refers to it */
  uint16_t sample_count; /* +0x004: samples toward this entry, to 2700 */
  int16_t field_006;     /* nothing refers to it */
  uint16_t summed_count; /* +0x008: of those, summed */
  float averages[64];    /* +0x00a: e_avg, efc_sum / summed_count */
  uint8_t weights[64];   /* +0x10a: weight, 1 to 3 */
  float times[64];       /* +0x14a: time */
  float logs[64];        /* +0x24a: log */
  uint32_t field_34a;    /* efc_history_update adds 2700 to it and compares
                            (0x41518, pll + 0x396) */
  float efc_sum; /* +0x34e: the EFC, less its temperature correction, summed */
  int32_t field_352; /* nothing refers to it */
  float drift;       /* +0x356: pll_rep's current drift, times 5.4e-8 per day */
  int32_t field_35a; /* nothing refers to it */
  float fit_a;       /* +0x35e: the curv task's aging fit a + b y + c ln y, y =
                        samples / 2700 */
  float fit_b;       /* +0x362 */
  float fit_c;       /* +0x366 */
} efc_ring;

/* The fit aging_fit made, aging_curve_state.mode ("mode= %d"), which
 * fit_quality's constants go by. */
typedef enum __attribute__((packed)) aging_fit_mode {
  AGING_FIT_CONSTANT = 0, /* fewer than 3 entries */
  AGING_FIT_FEW = 1,      /* a line through fewer than 10 */
  AGING_FIT_LOG = 2,      /* a log curve */
  AGING_FIT_LINE = 3,     /* a line */
  AGING_FIT_HALVES = 4,   /* a line from a full history's halves */
} aging_fit_mode;

#include "../../common/types/aging_curve_state.h"

/* @@Ba, the position/status/data message: one channel's status. */
typedef struct oncore_channel_status {
  uint8_t satellite;
  oncore_channel_mode mode;
  uint8_t signal_strength;
  uint8_t status; /* bit 7 used for the fix */
} oncore_channel_status;

/* @@Ba, as its decoder's table (0x525cc) lays it out: a pad byte
 * before the hour (time_of_day's own), and another before the DOP type, put the
 * longs and words on even addresses.  In the 58503A 3704 eight channels,
 * @@Ea's (0x525d6); @@Ba's decoder zeroes the last two. */
typedef struct oncore_status_fields {
  oncore_date_fields date;
  time_of_day time;  /* its reserved byte the pad */
  uint32_t fraction; /* ns */
  int32_t latitude;  /* mas */
  int32_t longitude;
  int32_t height_gps; /* cm */
  int32_t height_msl;
  uint16_t velocity; /* cm/s */
  uint16_t heading;  /* tenths of a degree */
  uint16_t dop;      /* tenths */
  uint8_t pad_22;
  oncore_dop_type dop_type;
  uint8_t visible_count;
  uint8_t tracked_count;
  oncore_channel_status channels[8];
  oncore_receiver_status receiver_status;
} oncore_status_fields;

#include "../../common/types/oncore_visible_satellite.h"

/* @@Bc: one combination of satellites and its xDOP. */
typedef struct oncore_xdop_combination {
  uint16_t xdop; /* tenths */
  uint8_t satellites[6];
} oncore_xdop_combination;

/* @@Bc, the xDOP table, copied as it arrives (0x50fe2). */
typedef struct oncore_xdop_table_fields {
  oncore_xdop_type type;
  uint8_t count;
  uint8_t mode; /* best 4, or 6 or 8 in view */
  oncore_xdop_combination combinations[9];
} oncore_xdop_table_fields;

/* @@Bd: one almanac's status. */
typedef struct oncore_almanac {
  uint8_t valid;
  uint8_t week;
  uint8_t time;
  uint8_t satellite_count;
  uint32_t satellites; /* one bit a satellite, bit 0 SV 1 */
} oncore_almanac;

/* @@Bd, the almanac status message. */
typedef struct oncore_almanac_status_fields {
  oncore_almanac ram;
  oncore_almanac eeprom;
} oncore_almanac_status_fields;

/* @@Ce: one channel's pseudorange correction, as the decoder's table
 * (0x50ff6) widens it. */
typedef struct oncore_correction_channel {
  uint8_t satellite;
  uint8_t pad_1;
  int32_t range_correction; /* cm */
  int16_t rate_correction;  /* mm/s */
  uint8_t issue_of_data;
  uint8_t pad_9;
} oncore_correction_channel;

/* @@Ce, the pseudorange correction message. */
typedef struct oncore_corrections_fields {
  int32_t time; /* tenths of a second of the week */
  oncore_correction_channel channels[6];
} oncore_corrections_fields;

/* @@Bn: one channel's time estimate. */
typedef struct oncore_raim_channel {
  uint8_t satellite;
  uint8_t pad_1;
  uint32_t time_estimate; /* ns */
} oncore_raim_channel;

/* @@Bn, the Time RAIM setup and status message, as the decoder's table
 * (0x525ef) lays it out; in the 58503A 3704 eight channels, @@En's
 * (0x52602), @@Bn's decoder zeroing the last two. */
typedef struct oncore_raim_fields {
  uint8_t rate;
  uint8_t raim_on;
  uint8_t pad_02;
  uint16_t alarm_limit; /* 100 ns */
  oncore_pps_mode pps_mode;
  uint8_t pad_06;
  int32_t pps_rate;
  uint8_t next_month; /* the time to the next pulse */
  uint8_t next_day;
  uint16_t next_year;
  uint8_t pad_0f;
  uint8_t next_hour;
  uint8_t next_minute;
  uint8_t next_second;
  uint8_t pulse_status;
  oncore_pulse_sync pulse_sync;
  oncore_raim_solution solution_status;
  oncore_raim_status raim_status;
  uint16_t sigma;  /* ns */
  int8_t sawtooth; /* ns, of the next pulse */
  uint8_t pad_1a;
  oncore_raim_channel channels[8];
} oncore_raim_fields;

/* @@Cj, the receiver ID: ten lines, each line's CR made a NUL and its
 * LF dropped by the decoder's table (0x51004), named by gps_print_id. */
typedef struct oncore_receiver_id {
  char copyright[34];
  char software_part_number[27];
  char software_version[27];
  char software_revision[27];
  char software_date[27];
  char model_number[27];
  char hardware_part_number[27];
  char serial_number[27];
  char manufacture_date[27];
  char options[27];
} oncore_receiver_id;

/* In the 58503A 3704, a line of the receiver ID by the index
 * oncore_receiver_id_lines (0x5262c) gives it, in @@Cj's order, named
 * by the label each begins with; 9 ends the table, and 0x50a16 reads
 * the options', the software version's and its revision's. */
typedef enum __attribute__((packed)) oncore_receiver_id_field {
  ONCORE_ID_MODEL = 0,             /* "MODEL #" */
  ONCORE_ID_SERIAL = 1,            /* "SERIAL #" */
  ONCORE_ID_MANUFACTURE_DATE = 2,  /* "MANUFACTUR DATE " */
  ONCORE_ID_OPTIONS = 3,           /* "OPTIONS LIST " */
  ONCORE_ID_HARDWARE_PART = 4,     /* "HDWR P/N #" */
  ONCORE_ID_SOFTWARE_PART = 5,     /* "SFTW P/N #" */
  ONCORE_ID_SOFTWARE_VERSION = 6,  /* "SOFTWARE VER #" */
  ONCORE_ID_SOFTWARE_REVISION = 7, /* "SOFTWARE REV #" */
  ONCORE_ID_SOFTWARE_DATE = 8,     /* "SOFTWARE DATE " */
  ONCORE_ID_END = 9,               /* the table's last entry */
} oncore_receiver_id_field;

/* In the 58503A 3704, a line of the receiver ID as 0x509b2 finds it by
 * its index: its text in gps.receiver_id, the label it begins with and
 * the label's length; 0x509b2 returns the text after the label, or 0
 * for a line that does not begin with it, and 0x50a16 reads the
 * options' (3), the software version's (6) and revision's (7).  The
 * table, oncore_receiver_id_lines (0x5262c), is in @@Cj's order, from
 * the model number, and ends at an index of 9. */
typedef struct oncore_receiver_id_line {
  oncore_receiver_id_field index; /* +0x00 */
  uint8_t field_01;               /* unread */
  char* text;                     /* +0x02: gps.receiver_id's line */
  char* label;                    /* +0x06: "MODEL #", ... */
  uint8_t label_length;           /* +0x0a */
  uint8_t field_0b;               /* unread */
} oncore_receiver_id_line;

/* gps.status_received in the 58503A 3704: whether @@Ba has come, and
 * lately, as the status monitor keeps it (command_monitor, 0x4bf70 and
 * 0x4bf9a); channel_position_satellite takes a channel's satellite only
 * while it is fresh (0x50190).  The Z3805A's is 0 or 1. */
typedef enum __attribute__((packed)) gps_status_freshness {
  GPS_STATUS_NONE = 0,  /* none since the monitor last failed */
  GPS_STATUS_STALE = 1, /* none for three of its periods */
  GPS_STATUS_FRESH = 2,
} gps_status_freshness;

/* One reference's validation, the GPS's or the external 1PPS's: in
 * the 58503A 3704, gps_receiver_status_update's (0x4b8a2) state for
 * each, gps.references[index]; 0x4b82a sets valid from override and
 * state to 1, 0x4b858 and 0x4b882 the GPS's and the external's
 * hysteresis.  22 bytes (0x101394 and 0x1013aa, 0x4b830). */
typedef struct gps_reference_record {
  gps_reference_validity valid;    /* 1 at state 8, 2 at 6 and 7, else 0; while
                                      override is set, 1 for 1 and 0 for any
                                      other (0x4bc2a) */
  gps_sync_status state;           /* its jump tables' (0x4ba16, 0x4bb70) */
  gps_reference_override override; /* 1 forces valid, 0xff invalid; gps_init
                                      clears it, diag_force_gps_1pps and
                                      diag_force_ext_1pps set it */
  uint8_t pps_dead; /* gps_pps_dead, the GPS's, set each pass (0x4b9d2) */
  uint32_t elapsed; /* 0x4b708 counts it from validation_start */
  uint32_t validation_start;  /* uptime as state reached 5 or 6 (0x4ba38) */
  uint32_t field_0c;          /* elapsed as state reached 7 (0x4ba60) */
  uint16_t validation_time;   /* 35 for the GPS, 1 for the external */
  uint16_t hysteresis;        /* nv_settings' (0x4b858, 0x4b882) */
  uint16_t hysteresis_margin; /* hysteresis less 1, or 0, 1 (0x4b86a) */
} gps_reference_record;

/* gps.field_592's bits, by what sets them (gps.md, "Six or eight
 * channels"): an @@Ea came (oncore_position_status_8_message and
 * oncore_eight_channels_seen, 0x506e2, 0x5098a), which makes
 * gps.channel_count 8 (gps_channel_count_update) and passes @@Fa's
 * filter; an I in @@Cj's options, which oncore_filter_traim,
 * gps_message_supported and gps_print_time_raim test; and @@Cj's
 * software version 8.4, which passes the second @@Ar's filter
 * (gps_receiver_abilities_update, 0x50a16). */
typedef enum __attribute__((packed)) gps_field_592_bit {
  GPS_FIELD_592_EA_SEEN = 0x20,
  GPS_FIELD_592_OPTION_I = 0x40,
  GPS_FIELD_592_SOFTWARE_8_4 = 0x80,
} gps_field_592_bit;

/* The GPS task's state: what its decoders store of each message, in
 * the order of the message table, and its own state after.  Cleared as
 * one, in the 58503A 3704 memset(&gps, 0, 0x5a8) (gps_init, 0x4c914),
 * and addressed through a base register.  Members are named by their
 * message, field_NNN by offset where none fills them.  The 58503A's
 * message table (0x51178) gives each record's offset; its records are
 * the Z3805A 3543B's but for the eight-channel ones, its own messages
 * (@@Au) and its own state after the receiver ID. */
typedef struct gps_state {
  uint8_t field_000;
  oncore_time_fields time;             /* @@Aa */
  oncore_gmt_offset_fields gmt_offset; /* @@Ab */
  uint8_t field_007;
  oncore_date_fields date;                  /* @@Ac */
  oncore_position_fields position;          /* @@Ad, @@Ae, @@Af */
  uint8_t mask_angle;                       /* @@Ag */
  oncore_satellite_select satellite_select; /* @@Ah */
  uint8_t manual_satellites[8];             /* @@Ai, by channel */
  oncore_xdop_type xdop_type;               /* @@Aj */
  uint8_t field_027;
  uint16_t xdop_hysteresis;    /* @@Ak */
  uint16_t xdop_threshold;     /* @@Al, 3D to 2D */
  uint16_t hdop_threshold;     /* @@AC, 2D to 0D */
  uint32_t ignored_satellites; /* @@Am */
  uint8_t field_032[0x90 - 0x32];
  uint8_t almanac_update; /* @@An */
  uint8_t field_091[0xa6 - 0x91];
  uint8_t ionospheric_correction;            /* @@Aq */
  oncore_fix_algorithm fix_algorithm;        /* @@Ar */
  oncore_hold_position_fields hold_position; /* @@As */
  oncore_height_type hold_height_type;       /* @@As */
  uint8_t field_0b5;
  uint8_t position_hold; /* @@At */
  uint8_t field_0b7;
  int32_t altitude_hold_height;                 /* @@Au */
  oncore_height_type altitude_hold_height_type; /* @@Au */
  uint8_t altitude_hold;                        /* @@Av */
  oncore_time_mode utc_time;                    /* @@Aw */
  uint8_t field_0bf;
  int16_t epoch_offset;          /* @@Ax, ms */
  int32_t pps_offset;            /* @@Ay */
  int32_t pps_cable_delay;       /* @@Az, less the long at 0x10201c (0x5003e) */
  int32_t pps_cable_delay_given; /* @@Az as the engine gives it */
  uint8_t field_0ce;
  oncore_application_type application_type; /* @@AB */
  uint8_t position_fix_threshold;           /* @@AD, s */
  uint8_t differential_output_threshold;    /* @@AD, s */
  gps_status_freshness status_received;     /* the status monitor sets it */
  uint8_t status_rate;         /* s between @@Ba, as last requested */
  oncore_status_fields status; /* @@Ba, @@Ea */
  uint8_t field_11b;
  uint8_t visibility_received; /* set by @@Bb's decoder */
  uint8_t field_11d;
  oncore_visibility_fields visibility; /* @@Bb */
  uint8_t field_198;
  oncore_xdop_table_fields xdop_table; /* @@Bc */
  uint8_t field_1e4[2];
  oncore_almanac_status_fields almanac; /* @@Bd */
  uint8_t field_1f6[0x2b4 - 0x1f6];
  oncore_corrections_fields corrections; /* @@Ce */
  double correction_average;             /* m, printed AVG */
  double correction_deviation;           /* m, printed STD DEV */
  uint8_t field_304[2];
  uint8_t raim_received;   /* @@Bn has come; the status monitor sets it */
  oncore_raim_fields raim; /* @@Bn, @@En */
  oncore_leap_pending leap_second_pending; /* @@Bj */
  uint8_t field_353;
  uint16_t self_test; /* @@Ca, @@Fa, failed tests */
  uint8_t field_356[0x35c - 0x356];
  oncore_extension_fields extension; /* @@Bk, @@Ek */
  uint8_t field_39c[0x406 - 0x39c];
  oncore_fix_mode fix_mode; /* @@Cg */
  uint8_t field_407;
  uint8_t receiver_id_received;   /* set by @@Cj's decoder */
  oncore_receiver_id receiver_id; /* @@Cj */
  uint8_t field_51e[2];           /* gps_init clears the first */
  uint32_t average_count;         /* positions averaged */
  double average_latitude;        /* of @@Ba's, while surveying */
  double average_longitude;
  double average_height_gps;
  double average_height_msl;
  gps_reference_record references[2]; /* the GPS's, then the external
                                         1PPS's */
  uint8_t surveying;                  /* averaging @@Ba's position */
  uint8_t field_571; /* gps_init sets it; gps_receiver_status_update reads it */
  gps_raim_assessment
      raim_assessment;        /* 0 inactive, 1 OK, 2 alarm detected, 3 alarm */
  uint8_t pps_check_disabled; /* gps_pps_check skips while set */
  uint8_t receiver_health_failed; /* "GPS Receiver Health" */
  uint8_t field_575;
  uint8_t command_monitor_enabled;
  gps_command_monitor_state command_monitor_state;
  gps_script_step** command_monitor_query; /* its next query, a place in
                                              command_monitor_queries */
  uint32_t command_monitor_time;           /* uptime of its last state change */
  uint8_t command_monitor_error; /* set by gpsm_task when a command goes
                                    unanswered */
  uint8_t command_monitor_failed;
  uint8_t status_monitor_enabled;
  gps_status_monitor_state
      status_monitor_state;     /* 0 disabled, 1 standby, 2 recovery */
  uint32_t status_monitor_time; /* uptime of the last fresh @@Ba */
  uint32_t status_last_time;   /* the last @@Ba's time bytes, compared as one */
  uint8_t status_repeat_count; /* @@Ba with the same time in a row */
  uint8_t status_monitor_timed_out;
  uint8_t status_monitor_failed;
  uint8_t field_58f;
  uint8_t channel_count; /* the engine's channels, 6 or 8 (0x509fe); the
                            channel loops' bound */
  uint8_t field_591;
  gps_field_592_bit field_592; /* set by @@Ea's decoders and from @@Cj
                                  (gps_field_592_bit) */
  uint8_t field_593;
  uint8_t tracking_log; /* nv_settings.tracking_log, copied by gps_init */
  uint8_t tracking_log_active; /* tracking_log, as the receiver state
                                  update takes it (0x4ba8a) */
  /* The tracking log's channels, packed by gps_track_words_pack
     (0x4da70) at each update that leaves the reference valid, assessing
     or clearing: 10 bits a channel from the top of [0], 5 of satellite
     (32 as 10), 3 of mode (0; 1 for 1 and 2; else mode - 1) and 2 of
     signal strength (gps_receiver_strength_bits), 80 bits in all.  A
     track lost logs [0] and [1], the last good update's; a track resumed
     ORs channel_word_lowest into [2]'s free low half and logs it
     (0x4bb5c to 0x4bc16). */
  uint32_t track_words[3];
  /* Each channel's 2-bit mode (gps_receiver_mode_bits) and 2-bit signal
     strength from the top, packed when a track is lost
     (gps_channel_word_pack, 0x4db52); logged when it resumes. */
  uint32_t channel_word;
  /* Each channel's lowest 2-bit mode since the track was lost, 2 bits a
     channel from the top: 0xffff at the loss, lowered at each update
     while it is out (gps_channel_word_lowest, 0x4dbe8). */
  uint16_t channel_word_lowest;
} gps_state;

/* An entry of fpga_bit_table (0x413ac in the 58503A 3704, seven
 * entries), which fpga_set_bit and fpga_set_field take: a register of
 * fconf_regs and its mask.  The names are from how ti.c's routines use
 * them; only the address and sync are in structures.md.  The 58503A
 * adds entry 6, which fpga_fields_to_registers (0x40c24) and
 * counter_configuration_init (0x40f84) bound by. */
typedef enum __attribute__((packed)) fpga_bit_index {
#include "../../common/types/fpga_bit_index.h"
  FPGA_BIT_SYNC = 5,  /* register 3, 0x03: counter_set_sync */
  FPGA_BIT_ALARM = 6, /* register 3, 0x04, a flag: the alarm, which 0x40828
                         sets with PORTF0 bit 5 (docs/firmware/58503a-3704.md,
                         "The alarm outputs") */
} fpga_bit_index;

#include "../../common/types/fpga_mode.h"

#include "../../common/types/interval_arm_mode.h"

/* An FPGA configuration, a bitstream fpga_program loads and
 * fpga_verify checks, by its index in fpga_configurations.  The table
 * holds one, the first bitstream, the time-interval counter's design
 * (outputs.md, "The FPGA bitstreams"). */
typedef enum __attribute__((packed)) fpga_configuration_index {
  FPGA_CONFIGURATION_COUNTER = 0,
  FPGA_CONFIGURATION_LAST = 1, /* fpga_configurations.last (0x21e2c), past
                                  the one configuration; pll_init's
                                  immediate (0x473d2) */
} fpga_configuration_index;

#include "../../common/types/fpga_chip.h"

/* What a stage of the loop gives pllp_task each pass: go on in it, it is
 * done (pllp_task moves to the next stage), or it failed. */
typedef enum __attribute__((packed)) pll_stage_result {
  PLL_STAGE_RESULT_RUNNING = 0,
  PLL_STAGE_RESULT_DONE = 1,
  PLL_STAGE_RESULT_FAILED = 2,
} pll_stage_result;

/* powerup's sub-states, pll.powerup_state: the status screen's ladder
 * (docs/screen-format-strings.md, "Mode suffixes"). */
typedef enum __attribute__((packed)) pll_powerup_state {
  PLL_POWERUP_START = 0,
  PLL_POWERUP_INNER_OVEN = 1,
  PLL_POWERUP_OUTER_OVEN = 2,
  PLL_POWERUP_WAITING_FOR_GPS = 3,
  PLL_POWERUP_COARSE = 4,
  PLL_POWERUP_FINE = 5,
  PLL_POWERUP_WAITING = 6,
  PLL_POWERUP_LEAP_SECONDS = 7,
} pll_powerup_state;

/* pll_check_reference's states, pll.reference_check_state. */
typedef enum __attribute__((packed)) pll_reference_check_state {
  PLL_REFERENCE_START = 0,
  PLL_REFERENCE_FIRST = 1,
  PLL_REFERENCE_AGAIN = 2,
} pll_reference_check_state;

/* startup_pll's states, pll.startup_state; any other starts it again. */
typedef enum __attribute__((packed)) pll_startup_state {
  PLL_STARTUP_SETTLING = 1,
  PLL_STARTUP_WAITING_FOR_HISTORY = 2,
  PLL_STARTUP_LENGTHENING = 3,
} pll_startup_state;

/* pll_normal's states, pll.normal_state. */
typedef enum __attribute__((packed)) pll_normal_state {
  PLL_NORMAL_RUNNING = 1,
} pll_normal_state;

/* holdover's states, pll.holdover_state. */
typedef enum __attribute__((packed)) pll_holdover_state {
  PLL_HOLDOVER_START = 0,
  PLL_HOLDOVER_HOLDING = 1,
} pll_holdover_state;

/* holdover_recovery's states, pll.recovery_state; nothing sets 2, and any
 * state past PLL_RECOVERY_CLOCK_CHECK starts it again. */
typedef enum __attribute__((packed)) pll_recovery_state {
  PLL_RECOVERY_WAITING = 0,
  PLL_RECOVERY_FINE = 1,
  PLL_RECOVERY_CHECK_DELAY = 3,
  PLL_RECOVERY_LEAP_ACK = 4,
  PLL_RECOVERY_CLOCK_CHECK = 5,
} pll_recovery_state;

/* coarse's states, pll.coarse_state. */
typedef enum __attribute__((packed)) pll_coarse_state {
  PLL_COARSE_SYNC = 0,
  PLL_COARSE_SETTLE = 1,
  PLL_COARSE_CALIBRATE = 2,
  PLL_COARSE_FIRST = 3,
  PLL_COARSE_SECOND = 4,
} pll_coarse_state;

/* efc_history_update's states, pll.aging_state: the aging fit holds
 * from PLL_AGING_COLLECTING. */
typedef enum __attribute__((packed)) pll_aging_state {
  PLL_AGING_START = 0,
  PLL_AGING_SETTLING = 1,
  PLL_AGING_IDLE = 2,
  PLL_AGING_COLLECTING = 3,
} pll_aging_state;

/* gps_diag_sequence's states, pll.gps_diag_state. */
typedef enum __attribute__((packed)) pll_gps_diag_state {
  PLL_GPS_DIAG_RESET_SEND = 0,
  PLL_GPS_DIAG_RESET_ACK = 1,
  PLL_GPS_DIAG_TEST_SEND = 2,
  PLL_GPS_DIAG_TEST_ACK = 3,
  PLL_GPS_DIAG_SETTLE = 4,
  PLL_GPS_DIAG_RUN = 5,
} pll_gps_diag_state;

/* gps_init_sequence's states, pll.gps_init_state, as
 * print_gps_init_state names them: each setting sent, then its
 * acknowledgment awaited.  In the 58503A 3704 the Z3805A 3543B's from 2
 * on are 2 higher (gps_init_sequence's stores, 0x46aba to 0x46f50, and
 * both switches, 0x1b828 and 0x46a44, to 19); 2 and 3 are stored by
 * nothing, print_gps_init_state prints them "unknown" and
 * gps_init_sequence takes them as 0. */
typedef enum __attribute__((packed)) pll_gps_init_state {
  PLL_GPS_INIT_START = 0x00,
  PLL_GPS_INIT_ACK = 0x01,
  PLL_GPS_INIT_UTC_SEND = 0x04,
  PLL_GPS_INIT_UTC_ACK = 0x05,
  PLL_GPS_INIT_MASK_ANGLE_SEND = 0x06,
  PLL_GPS_INIT_MASK_ANGLE_ACK = 0x07,
  PLL_GPS_INIT_ANTENNA_DELAY_SEND = 0x08,
  PLL_GPS_INIT_ANTENNA_DELAY_ACK = 0x09,
  PLL_GPS_INIT_IGNORE_SEND = 0x0a,
  PLL_GPS_INIT_IGNORE_ACK = 0x0b,
  PLL_GPS_INIT_POSITION_SEND = 0x0c,
  PLL_GPS_INIT_POSITION_ACK = 0x0d,
  PLL_GPS_INIT_HOLD_OFF_SEND = 0x0e,
  PLL_GPS_INIT_HOLD_OFF_ACK = 0x0f,
  PLL_GPS_INIT_SURVEY = 0x10,
  PLL_GPS_INIT_SURVEY_POSITION_SEND = 0x11,
  PLL_GPS_INIT_SURVEY_POSITION_ACK = 0x12,
  PLL_GPS_INIT_DONE = 0x13,
} pll_gps_init_state;

/* gps_leap_sequence's states, pll.leap_check_state, as
 * print_leap_check_state names them. */
typedef enum __attribute__((packed)) pll_leap_check_state {
  PLL_LEAP_CHECK_START = 0,
  PLL_LEAP_CHECK_PENDING_SEND = 1,
  PLL_LEAP_CHECK_PENDING_ACK = 2,
  PLL_LEAP_CHECK_WAIT = 3,
} pll_leap_check_state;

/* gps_leap_ack's states, pll.leap_ack_state: the engine's time mode
 * switched with @@Aw to measure the GPS-UTC offset, then switched back. */
typedef enum __attribute__((packed)) pll_leap_ack_state {
  PLL_LEAP_ACK_IDLE = 0,
  PLL_LEAP_ACK_CHECK = 1,
  PLL_LEAP_ACK_MODE_SEND = 2,
  PLL_LEAP_ACK_MODE_ACK = 3,
  PLL_LEAP_ACK_SWITCH_SEND = 4,
  PLL_LEAP_ACK_SWITCH_ACK = 5,
  PLL_LEAP_ACK_BACK_SEND = 6,
  PLL_LEAP_ACK_BACK_ACK = 7,
} pll_leap_ack_state;

#include "../../common/types/pll_screen_mode.h"

/* The power-up mode's suffix on the status screen, pll_powerup_suffix_index's
 * result by powerup's sub-state (docs/screen-format-strings.md, "Mode
 * suffixes"). */
typedef enum __attribute__((packed)) pll_powerup_suffix {
  PLL_POWERUP_SUFFIX_NONE = 0,
  PLL_POWERUP_SUFFIX_WARM_UP = 1, /* OCXO warm-up */
  PLL_POWERUP_SUFFIX_GPS_ACQUISITION = 2,
  PLL_POWERUP_SUFFIX_COARSE = 3,
  PLL_POWERUP_SUFFIX_FINE_FREQUENCY = 4,
  PLL_POWERUP_SUFFIX_PHASE_ALIGNMENT = 5,
  PLL_POWERUP_SUFFIX_LEAP_SECOND = 6, /* leap second determination */
} pll_powerup_suffix;

/* The recovery mode's suffix, recovery_suffix_index's result while
 * holdover_recovery sets the frequency finely. */
typedef enum __attribute__((packed)) pll_recovery_suffix {
  PLL_RECOVERY_SUFFIX_NONE = 0,
  PLL_RECOVERY_SUFFIX_FINE_FREQUENCY = 1,
  PLL_RECOVERY_SUFFIX_PHASE_ALIGNMENT = 2,
} pll_recovery_suffix;

/* The Frequency Figure of Merit, get_ffom's by the loop's stage
 * (097-z3801-01, 3-14, "FFOM Value"), :PTIMe:FFOMerit? and the time
 * code's. */
typedef enum __attribute__((packed)) frequency_figure_of_merit {
  FFOM_STABILIZED = 0,  /* PLL stabilized, 10 MHz within specification */
  FFOM_STABILIZING = 1, /* PLL stabilizing */
  FFOM_HOLDOVER = 2,    /* PLL unlocked, in holdover */
  FFOM_UNLOCKED = 3,    /* PLL unlocked, not in holdover: do not use */
} frequency_figure_of_merit;

#include "../../common/types/gps_ack_awaited_kind.h"

/* The disciplining loop's state block: the stage the status screen
 * shows (docs/screen-format-strings.md, "Mode suffixes") and the PLL's
 * values.  Cleared as one, in the 58503A 3704 memset(&pll, 0, 1000)
 * (pll_init, 0x475ac): its hold causes a word, which moves the fields
 * after them on by two, the EFC history 4 bytes longer, and fields of
 * its own near the end. */
typedef struct pll_state {
  pll_stage stage;
  pll_stage previous_stage;  /* +0x01: pllp_task's copy of stage */
  uint8_t powerup_countdown; /* +0x02: readings left before powerup checks the
                                clock against GPS, 25 then 2 */
  pll_powerup_state powerup_state; /* +0x03: powerup's sub-state,
                            screen-format-strings.md's ladder */
  uint8_t powerup_reference_lost;  /* +0x04: GPS or its PPS went, or temporary
                                      mode; powerup goes back to state 3 */
  uint8_t field_005;
  pll_reference_check_state
      reference_check_state; /* +0x06: pll_check_reference's */
  uint8_t field_007[5];
  pll_startup_state startup_state; /* +0x0c: startup_pll's */
  uint8_t field_00d;
  pll_normal_state normal_state; /* +0x0e: pll_normal's */
  uint8_t field_00f;
  pll_hold_cause hold_causes; /* +0x10: a set of pll_hold_cause bits, a word
                                 (diag_pr_hold_cause, 0x1b1c2) */
  pll_holdover_state holdover_state; /* +0x12: holdover's */
  uint8_t manual;                    /* +0x13 */
  uint8_t ti_limit_override; /* +0x14: holdover checks the TI limit while 0;
                                holdover_recovery_limit_ignore sets it */
  uint8_t field_015;
  uint8_t
      recovery_countdown; /* +0x16: holdover_recovery's, as powerup_countdown */
  pll_recovery_state recovery_state; /* +0x17 */
  pll_coarse_state coarse_state;     /* +0x18: coarse's */
  pll_fine_state fine_state;         /* +0x19: fine's; PLL_FINE_SLEW to
                                 PLL_FINE_FINE_SLEW slew the phase */
  uint8_t frequency_adjusted; /* +0x1a: fine applied a frequency correction */
  uint8_t fine_index;         /* fine acquisition's "i= %d" */
  uint16_t fine_retries; /* +0x1c: fine's failed attempts, given up past 9 */
  float fine_d;          /* fine acquisition's "d = %f" */
  pll_aging_state
      aging_state; /* +0x22: efc_history_update's; 3 once the aging fit holds */
  uint8_t field_023[3];
  pll_gps_diag_state gps_diag_state; /* +0x26: gps_diag_sequence's */
  uint8_t gps_diag_substate;         /* +0x27 */
  pll_gps_init_state
      gps_init_state; /* +0x28: gps_init_sequence's, print_gps_init_state's */
  uint8_t field_029;
  uint16_t leap_check_timer;    /* +0x2a: gps_leap_sequence checks again when it
                                   wraps */
  uint8_t leap_check_requested; /* +0x2c: pll_stage_status sets it */
  pll_leap_check_state leap_check_state; /* +0x2d: gps_leap_sequence's,
                               print_leap_check_state's */
  pll_leap_ack_state leap_ack_state;     /* +0x2e: gps_leap_ack's, measuring the
                                 GPS-UTC offset     by switching @@Aw */
  uint8_t leap_ack_requested;            /* +0x2f */
  uint8_t leap_ack_done;                 /* +0x30: the offset is set */
  uint8_t field_031;
  uint8_t
      leap_ack_again; /* +0x32: holdover_recovery measures again after fine */
  uint8_t field_033;
  float efc;                   /* pr_efc's efc */
  float tfom;                  /* pll_rep's tfom, s */
  float predicted_uncertainty; /* +0x3c: the curv fit's, :KENneth:PREDicted? */
  uint8_t field_040[8];
  float frequency_tolerance; /* +0x48: 1e-8 at first; fine's bound, holdover's
                                limit per second */
  efc_ring efc_history;
  float ti_filtered; /* +0x3b6: the TI, filtered with weight 29.75 / tau */
  float efc_base;    /* +0x3ba: the EFC less the temperature term, TC times the
                        current */
  float loop_time_constant; /* +0x3be: 150 s at first, at most max_loop_time */
  uint32_t holdover_start_time; /* +0x3c2: get_date_time's as holdover starts */
  uint32_t holdover_start_date; /* +0x3c6 */
  uint8_t led_gps_lock;         /* +0x3ca: :LED:GPSLock? */
  uint8_t led_holdover;         /* +0x3cb: :LED:HOLDover? */
  uint32_t holdover_duration;   /* +0x3cc: :ROSCillator:HOLDover:DURation? */
  uint32_t field_3d0;           /* nothing refers to it */
  uint32_t reference_uptime;    /* +0x3d4: the uptime clock_set_from_reference
                                   counts from */
  time_of_day reference_time;   /* +0x3d8: gps.status.time as pllp_task last
                                   took it (0x47c60), which the
                                   clock's check against GPS compares */
  uint32_t efc_control_passes;  /* +0x3dc: 0x45c46's count toward clearing
                                   HOLD_CAUSE_EFC_CONTROL at 600 */
  uint8_t reference_seconds;    /* +0x3e0: the FPGA's seconds count with it */
  fpga_configuration_index
      fpga_configuration; /* +0x3e1: the configuration fpga_setup loaded and
                             fpga_load_test loads again; pll_init starts it at
                             fpga_configurations.last */
  uint8_t fpga_loaded;    /* +0x3e2: the PPS checks wait for it */
  uint8_t loop_running;   /* +0x3e3: 0 in diagnostic mode; nothing reads it */
  uint8_t sync_requested; /* +0x3e4: sync_perform syncs whatever the TI, then
                             clears it */
  uint8_t field_3e5;      /* pll_init clears it, pll_stage_status tests it and
                             status_snapshot copies it */
  uint8_t field_3e6;      /* pll_init sets 9 (0x47612), 0x45eb4 writes it */
  uint8_t field_3e7;
} pll_state;

/* The GPS link's counters, which the "GPS Process Monitor" page
 * (gps_print_process_monitor) prints.  The 58503A's differs from the
 * Z3805A's and the Z3801A's: its count of responses
 * read is a long, which the page prints "Resp" %10d (0x4d626), so that
 * the counters after it are a word on, and the record is 0x38 bytes,
 * as clear_gps_debug clears it (0x4d40a).  All words but that one. */
typedef struct gps_statistics {
  uint16_t received;                 /* +0x00: Msgs Rcvd */
  uint16_t sent;                     /* +0x02: Msgs Sent */
  uint16_t jammed;                   /* +0x04: Msgs Jam */
  uint16_t sent_errors;              /* +0x06 */
  uint16_t received_errors;          /* +0x08 */
  uint16_t jam_errors;               /* +0x0a */
  uint16_t successes;                /* +0x0c: Success */
  uint16_t field_0e;                 /* +0x0e: NOP; gpsm_task counts a request
                                        gps_request_admitted refuses, which is none */
  uint16_t rejects;                  /* +0x10: Reject */
  uint16_t timeouts;                 /* +0x12: Timeout */
  uint16_t busy;                     /* +0x14: Busy */
  uint16_t aborts;                   /* +0x16: Abort */
  uint16_t no_command;               /* +0x18: No Cmd */
  uint16_t no_query;                 /* +0x1a: No Qry */
  uint16_t unknown_command;          /* +0x1c: ? Cmd */
  uint16_t unknown_sender;           /* +0x1e: ? Sndr */
  uint16_t commands_sent;            /* +0x20: Cmds Sent */
  uint32_t responses_read;           /* +0x22: Resp */
  uint16_t e_retries;                /* +0x26: E Retry */
  uint16_t t_retries;                /* +0x28: T Retry */
  uint16_t raim_detected;            /* +0x2a: Time RAIM page: (%d detected) */
  uint16_t raim_alarms;              /* +0x2c: # Alarms */
  uint16_t unknown_response;         /* +0x2e: ? Resp */
  uint16_t frame_errors;             /* +0x30: Frame */
  uint16_t checksum_errors;          /* +0x32: Cksum */
  uint16_t command_monitor_failures; /* +0x34 */
  uint16_t status_monitor_failures;  /* +0x36 */
} gps_statistics;

#include "../../common/types/exception_record.h"

#include "../../common/types/os.h"

/* A fixed event log message, by the code log_event_58503a (0x3189a) takes
 * and its jump table's order (0x318bc, 28 cases); the names are the
 * messages'.  enter_holdover (0x4396e) picks a holdover's by its cause.
 * The 58503A 3704 adds "EXT reference valid" as 0x15, moving the
 * Z3805A 3543B's 0x15 to 0x1a one on. */
typedef enum __attribute__((packed)) log_event_code {
  LOG_CLEARED = 0x00,                  /* "Log cleared" */
  LOG_POWER_ON = 0x01,                 /* "Power on" */
  LOG_REBOOT = 0x02,                   /* "Re-boot" */
  LOG_SURVEY_STARTED = 0x03,           /* "Survey mode started" */
  LOG_POSITION_HOLD_STARTED = 0x04,    /* "Position hold mode started" */
  LOG_EXT_LOCK_STARTED = 0x05,         /* "EXT lock started" */
  LOG_GPS_LOCK_STARTED = 0x06,         /* "GPS lock started" */
  LOG_HOLDOVER_MANUAL = 0x07,          /* "Holdover started, manual" */
  LOG_HOLDOVER_EXT = 0x08,             /* "..., EXT" */
  LOG_HOLDOVER_INVALID_EXT_PPS = 0x09, /* "..., invalid EXT 1PPS" */
  LOG_HOLDOVER_GPS = 0x0a,             /* "..., GPS" */
  LOG_HOLDOVER_INVALID_GPS_PPS = 0x0b, /* "..., invalid GPS 1PPS" */
  LOG_HOLDOVER_NOT_TRACKING = 0x0c,    /* "..., not tracking GPS" */
  LOG_HOLDOVER_RAIM_ALARM = 0x0d,      /* "..., GPS RAIM alarm" */
  LOG_HOLDOVER_GPS_ALARM = 0x0e,       /* "..., GPS alarm" */
  LOG_HOLDOVER_HW_ERROR = 0x0f,        /* "..., HW error" */
  LOG_HOLDOVER_TI_ERROR = 0x10,        /* "..., TI error" */
  LOG_HOLDOVER_TI_LIMIT = 0x11,        /* "..., TI limit exceeded" */
  LOG_HOLDOVER_TEMPORARY = 0x12,       /* "..., temporary" */
  LOG_HOLDOVER_SELECTING_PPS = 0x13,   /* "..., selecting 1PPS" */
  LOG_HOLDOVER_STARTED = 0x14,         /* "Holdover started" */
  LOG_EXT_REFERENCE_VALID = 0x15,      /* "EXT reference valid" */
  LOG_SPECIFICATIONS_EXCEEDED = 0x16,  /* "Output specifications exceeded" */
  LOG_HARDWARE_FAILURE = 0x17,         /* "Hardware failure" */
  LOG_SELFTEST_FAILED = 0x18,          /* "Self-test failed" */
  LOG_EEPROM_SAVE_FAILED = 0x19,       /* "EEPROM save failed" */
  LOG_OUT_OF_EVENT_BUFFERS = 0x1a,     /* "Clk_do: out of event buffers" */
  LOG_SYSTEM_PRESET = 0x1b,            /* "System preset" */
} log_event_code;

#include "../../common/types/log_entry.h"

#include "../../common/types/fpga_bit.h"

#include "../../common/types/interpolator_calibration.h"

#include "../../common/types/scpi_setting_param.h"

/* The words the numeric and setting descriptors' group_cell points at,
 * one for each command group (scpi_group_cells, 0x1022a4, 10 bytes, up
 * to interval_reading).  No code reads or writes them, only
 * primary_reset's clear, so each is a word by their spacing alone.  The
 * Z3805A's fourth, :SYSTem:COMMunicate:SERial2's, is not here: the
 * 58503A has no SERial2 descriptor. */
typedef struct scpi_group_cells_fields {
  uint16_t serial_1; /* +0x00: :SYSTem:COMMunicate:SERial1's and :FDUPlex's
                        (0x33336 on) */
  uint16_t sample;   /* +0x02: :GPS:INITial's, :SYSTem:PRINt's,
                        scpi_param_DOUGlas_STATe_request's and
                        :DIAGnostic:ROSCillator:EFControl's (0x3349e on) */
  uint16_t pulse;    /* +0x04: :OUTPut:STATe's and :PULSe's (0x336a2 on) */
  uint16_t status;   /* +0x06: :STATus's (0x337d8 on) */
  uint16_t rest;     /* +0x08: every other descriptor's, 0x33946 to 0x340ec */
} scpi_group_cells_fields;

#include "../../common/types/scpi_action_param.h"

#include "../../common/types/request_source.h"

#include "../../common/types/pllc_request.h"

/* An Oncore argument encoder: puts a request's argument by its index
 * at out, and returns where the next byte goes (long_array_get_byte and
 * its kin). */
typedef uint8_t* oncore_encoder(int32_t* arguments, uint8_t index,
                                uint8_t* out);

/* An Oncore response decoder: unpacks the message at cursor into
 * record, and returns the cursor past what it read
 * (oncore_decode_fields and the oncore_*_message routines). */
typedef uint8_t* oncore_decode_function(uint8_t* cursor, uint8_t* record);

/* A decoder of an Oncore message and the record in gps it fills. */
typedef struct oncore_decoder {
  oncore_decode_function* decode; /* gps_response_decode calls it with the
                                     cursor and record, and takes the cursor
                                     back */
  uint8_t* record;
} oncore_decoder;

/* How gps_response_match takes a response to a message
 * (oncore_message_entry.match_kind; its jump table at 0x4fbce).  The
 * kinds that compare take the response as the command's echo, its bytes
 * after the ID the same (VPCommands.pdf for each message). */
typedef enum __attribute__((packed)) oncore_match_kind {
  ONCORE_MATCH_ANY = 0,        /* answered at once; gpsm_task waits for none */
  ONCORE_MATCH_ECHO = 1,       /* the same message, its bytes the same */
  ONCORE_MATCH_GMT_OFFSET = 2, /* @@Ab: an echo, but a negative offset under
                                  an hour, sign 0xff and hours 0, compared as
                                  sign 0 and its minutes negated */
  ONCORE_MATCH_HEIGHT = 3,     /* @@Af: the height alone, the response's MSL
                                  one for type 1 */
  ONCORE_MATCH_SECOND_AR = 4,  /* the 58503A 3704's second @@Ar, entry 55:
                                  an echo of entry 55 (0x50e6c) */
  ONCORE_MATCH_HOLD_POSITION = 5, /* @@As: latitude and longitude alone when
                                     the type is the same and the height in
                                     range and within 2 cm */
  ONCORE_MATCH_FIX_MODE = 6,      /* @@Cg: an echo, but mode 2, a query of the
                                     mode, matched at once */
  ONCORE_MATCH_SAME = 7,          /* the same message, its bytes not compared */
  ONCORE_MATCH_USER_DATUM = 8,    /* @@Ao: answered by @@Ap */
  ONCORE_MATCH_NEVER = 9,         /* never answered; no message has it */
  ONCORE_MATCH_CORRECTIONS = 10,  /* @@Bh: answered by @@Ce */
  ONCORE_MATCH_EA = 11,           /* the second @@Ea, entry 54: answered by
                                     the first, entry 50 (0x51048) */
} oncore_match_kind;

/* gps_response_match's verdict on a response. */
typedef enum __attribute__((packed)) gps_match_result {
  GPS_MATCH_OTHER = 0,    /* not the command's answer */
  GPS_MATCH_DIFFERS = 1,  /* its answer, its bytes not the command's */
  GPS_MATCH_ANSWERED = 2, /* its answer */
} gps_match_result;

/* An oncore_messages entry's kind: bit 0 that the task sends the
 * message, bit 1 that the engine does.  gps_request_build composes those
 * with bit 0; @@Ci, which switches the engine's format, has no answer,
 * and @@Ap and @@Ce only the engine sends. */
typedef enum __attribute__((packed)) oncore_message_kind {
  ONCORE_KIND_EMPTY = 0,     /* a slot without a message */
  ONCORE_KIND_SENT = 1,      /* @@Ci */
  ONCORE_KIND_RECEIVED = 2,  /* @@Ap, @@Ce */
  ONCORE_KIND_EXCHANGED = 3, /* every other */
} oncore_message_kind;

/* A send filter of oncore_message_entry: given the message's index,
 * its first argument encoder and its arguments, 0 to refuse it
 * (gps_request_build, 0x50bf6); oncore_filter_channels encodes the
 * first argument to test it (0x50af8). */
typedef uint8_t oncore_send_filter(int16_t index, oncore_encoder* encoder,
                                   int32_t* arguments);

/* An entry of the GPS task's Oncore message table, oncore_messages: in
 * the 58503A 3704 60 bytes, the Z3805A 3543B's 56 and a send filter
 * (docs/firmware/oncore/README.md, "The message table"; 0x50bb2). */
typedef struct oncore_message_entry {
  char* id; /* +0x00: the two-letter ID; zero for an empty slot */
  oncore_message_kind kind; /* +0x04 */
  uint8_t field_05;
  oncore_encoder* encoders[4]; /* +0x06: to the first zero */
  uint8_t* query; /* +0x16: the query form, the bytes after the ID to a NUL,
                     0xee standing for a zero byte (0x4fa4e); zero if none */
  uint8_t
      timeout; /* +0x1a: gps_response_timed_out's limit on the uptime elapsed */
  uint8_t field_1b;
  uint16_t length; /* +0x1c: the response's, @@ to line end; gps_response_lookup
                      returns it */
  oncore_match_kind match_kind; /* +0x1e */
  uint8_t field_1f;
  oncore_decoder
      decoders[3]; /* +0x20: gps_response_decode runs them to the first zero */
  oncore_send_filter* filter; /* +0x38: zero for none */
} oncore_message_entry;

#include "../../common/types/holdover_limit_setting.h"

/* The settings nv_save keeps in the NV register and nv_restore loads:
 * each field the value of the descriptor in nv_save's tables that points
 * at it, named by the SCPI command whose setter is given that descriptor.
 * In the 58503A 3704, 0xcb bytes, six more than the Z3805A 3543B's: its
 * tables (nv_numeric_params, 0x32d62, 23 descriptors; nv_setting_params,
 * 0x32dbe, 18) add three values and a setting and drop two settings
 * no code read (the Z3805A's field_1e and field_bc). */
typedef struct nv_settings_fields {
  uint8_t field_00[2];               /* no code refers to them */
  int32_t elevation_mask;            /* :GPS:EMANgle */
  int32_t antenna_delay;             /* :GPS:ADELay */
  int32_t douglas_duration;          /* :DOUGlas:DURation */
  uint8_t douglas_duration_infinite; /* :DOUGlas:DURation INFinity */
  uint8_t tracking_log;              /* :DIAG:GPS:TRACking:LOG (0x340ec) */
  int32_t questionable_hysteresis;   /* :DIAG:REF:GPS:QUES:HYSTeresis (0x34082),
                                        10 bits; gps_init passes it to 0x4b858 */
  int32_t field_14; /* 4 bits (0x340c0); gps_init passes it to 0x4b882; no
                       handler names its descriptor */
  int32_t holdover_status_threshold;           /* :ROSC:HOLD:DUR:STAT:THR */
  float limit_threshold;                       /* :ROSC:LIMit:THReshold */
  float holdover_limit_threshold;              /* :ROSC:HOLD:LIMit:THReshold */
  holdover_limit_setting holdover_limit_state; /* :ROSC:HOLD:LIMit:STATe */
  uint8_t douglas_state;                       /* :DOUGlas:STATe */
  int32_t position[3];                         /* :GPS:POSition, mas, mas, cm */
  uint32_t ignored_satellites;                 /* :GPS:SAT:TRAC:IGNore */
  int32_t time_zone[2];        /* :PTIMe:TZONe, hours, minutes */
  int32_t prediction_duration; /* :KENneth:PREDicted:DURation */
  uint32_t leap_date;          /* a packed date */
  int32_t leap_minute_length;  /* 59 for a second removed */
  uint8_t leap_pending;
  uint8_t raim_state[2]; /* :DIAG:GPS:POS:HOLD:RAIM:STATe */
  uint8_t pad_4d;
  int32_t raim_threshold[2];              /* :DIAG:GPS:POS:HOLD:RAIM:THR */
  int32_t holdover_measurement_threshold; /* :ROSC:HOLD:DUR:MEAS:THR */
  uint32_t status_enable[7];              /* *ESE, *SRE, :STATus ENABle */
  uint32_t status_positive_transition[5]; /* :STATus PTRansition */
  uint32_t status_negative_transition[5]; /* :STATus NTRansition */
  uint8_t questionable_user;              /* :STAT:QUES:...:USER */
  uint8_t serial_restricted;              /* :DIAG:SER1:RESTricted */
  uint8_t pulse_state;                    /* :PULSe:CONTinuous:STATe */
  uint8_t pad_a1;
  int32_t pulse_period;          /* :PULSe:CONTinuous:PERiod */
  int32_t pulse_start_time[3];   /* :PULSe:STARt:TIME */
  int32_t pulse_start_date[3];   /* :PULSe:STARt:DATE */
  int32_t field_be;              /* 16 bits (0x34044); only its descriptor */
  uint8_t output_state;          /* :OUTPut:STATe */
  data_format format_data;       /* :FORMat:DATA */
  uint8_t memory_save;           /* :ROBin:MEMory:SAVE */
  uint8_t antenna_current_alarm; /* :DIAG:GPS:ACURrent:ALARm (0x33f6c) */
  uint8_t douglas_state_powerup; /* :DOUGlas:STATe:POWerup */
  timecode_format tcode_format;  /* :PTIMe:TCODe:FORMat */
  uint8_t gps_utc;               /* :DIAG:GPS:UTC */
  uint8_t pforth_at_startup;     /* :DIAG:SYSTem:PSTartup */
  uint8_t debug_output;          /* :DIAG:SYSTem:DOUTput */
} nv_settings_fields;

#include "../../common/types/interval_reading_fields.h"

#include "../../common/types/counter_work_state.h"

/* The battery-backed RAM a warm restart restores, preserved_region at
 * 0x100000: pllp_task copies gps, pll and the RAM blocks below into it
 * each pass and checksums it, and system_init copies them back when the
 * checksum is good, RSR shows neither an external nor a power-up reset,
 * and restore is 1.  In the 58503A 3704, 0xb2e bytes, record_checksum's
 * length (system_init, 0x12cfa): its gps and pll are larger than the
 * Z3805A 3543B's, and it keeps 0x180 bytes from 0x101e78 beside the 0x19
 * of retained_block. */
typedef struct preserved_region_fields {
  uint16_t
      checksum; /* +0x000: record_checksum's sum and complement of the rest */
  uint16_t restore;            /* +0x002: a preserved_region_restore */
  gps_state gps;               /* +0x004 */
  pll_state pll;               /* +0x5ac */
  adc_monitor adc_monitors[8]; /* +0x994: adc_monitors', 0x101e78 */
  uint8_t values[0x19];        /* +0xb14: retained_block's */
  uint8_t field_b2d;           /* in the checksum; nothing else refers to it */
} preserved_region_fields;

#include "../../common/types/clock_record.h"

#include "../../common/types/pps_exchange_message.h"

#include "../../common/types/gps_process_state.h"

#include "../../common/types/satellite_sets.h"

/* A satellite_status_list entry, 8 bytes: status_snapshot keeps twelve
 * not tracked and six tracked
 * (0x4a81a, 0x4a83a); satellite_status_blank_entry (0x4e016) is an
 * empty one. */
typedef struct satellite_list_entry {
  uint8_t satellite;   /* 0 for none */
  uint8_t attempting;  /* not tracked: on a channel but not used (0x49964) */
  int16_t level;       /* tracked: its channel's signal strength; -1 none */
  satellite_cell cell; /* +0x04 */
} satellite_list_entry;

/* A status snapshot, status_snapshot's record, which status_screen
 * prints: in the 58503A 3704 0x16a bytes, status_snapshot_latest
 * (0x10181e) to status_snapshot_flagged (0x101988), each field where
 * status_snapshot (0x4ac56) stores it: the receiver's channel count
 * before the satellite sets, sixteen not tracked and eight tracked, and
 * both reference records' codes where the Z3805A 3543B keeps the GPS's
 * flat fields. */
typedef struct status_snapshot_record {
  pll_stage stage;         /* +0x000: pll.stage */
  uint8_t mode;            /* +0x001: pll_mode's */
  uint8_t powerup_suffix;  /* +0x002: pll_powerup_suffix_index's */
  uint8_t recovery_suffix; /* +0x003: recovery_suffix_index's */
  float tfom;              /* +0x004: pll.tfom */
  uint8_t ffom;            /* +0x008: get_ffom's */
  uint8_t ffom_valid;      /* +0x009: get_ffom's result */
  double interval;         /* +0x00a: interval_reading.value */
  uint8_t
      interval_valid;            /* +0x012: interval_reading.valid;
                                    status_screen_mode_suffix prints the TI while set */
  counter_error interval_errors; /* +0x013: interval_reading.errors */
  uint16_t interval_tag;       /* +0x014: interval_reading.tag; its low byte is
                                  format_si's resolution for the TI */
  float interval_mean;         /* +0x016 */
  uint16_t interval_mean_flag; /* +0x01a: the word at interval_mean_flag */
  uint8_t holdover_waiting_reason;             /* +0x01c */
  holdover_limit_setting holdover_limit_state; /* +0x01d: nv_settings' */
  float holdover_limit_threshold;              /* +0x01e: nv_settings' */
  uint32_t holdover_duration;                  /* +0x022: pll's */
  float kenneth_present;               /* +0x026: kenneth_present_get's */
  uint8_t kenneth_present_valid;       /* +0x02a: its result */
  int8_t kenneth_present_resolution;   /* its resolution, a power of ten */
  float kenneth_predicted;             /* +0x02c: kenneth_predicted_get's */
  uint8_t kenneth_predicted_valid;     /* +0x030: its result */
  int8_t kenneth_predicted_resolution; /* its resolution, a power of ten */
  uint8_t channel_count;               /* +0x032: gps' (0x4ae46) */
  uint8_t field_033;
  satellite_sets satellites;            /* +0x034: satellite_sets_build's */
  satellite_list_entry not_tracked[16]; /* +0x05e: satellite_status_list's
                                           list 0, the visible satellites no
                                           channel uses for the fix */
  satellite_list_entry tracked[8];      /* +0x0de: its list 1, those the
                                           channels use, with their signal */
  uint8_t status_received;              /* +0x11e: gps' */
  uint8_t not_tracked_count;   /* +0x11f: satellite_status_list's count of
                                  not_tracked */
  uint8_t tracked_count;       /* +0x120: its count of tracked */
  uint8_t pll_field_3d5;       /* +0x121: pll.field_3e5 */
  uint8_t reference_valid;     /* +0x122: gps.references[0].valid */
  uint8_t pps_dead;            /* +0x123: gps.references[0]' */
  gps_sync_status status_code; /* +0x124: gps.references[0].state */
  uint8_t field_125;
  uint32_t reference_elapsed;           /* +0x126: gps.references[0].elapsed */
  uint8_t external_valid;               /* +0x12a: gps.references[1].valid */
  uint8_t external_pps_dead;            /* +0x12b: gps.references[1].pps_dead */
  gps_sync_status external_status_code; /* +0x12c: gps.references[1].state */
  uint8_t field_12d;
  uint32_t external_elapsed; /* +0x12e: gps.references[1].elapsed */
  uint8_t fix_mode;          /* +0x132: gps' */
  oncore_receiver_status receiver_status; /* +0x133: gps.status' */
  time_of_day time;     /* +0x134: gps.status' time made local;
                           get_date_time's while from_clock */
  uint32_t date;        /* +0x138: the same's date */
  int32_t time_zone[2]; /* +0x13c: nv_settings' */
  uint8_t from_clock;   /* +0x144: 1 when the clock is set, the time its; 0 for
                          a clock not set since power-up (get_date_time's 1),
                          the time @@Ba's, which the screen marks "[?]" */
  uint8_t utc_time;     /* +0x145: gps' */
  oncore_leap_pending
      leap_second_pending; /* +0x146: from nv_settings.leap_minute_length,
                              59 a second subtracted, 61 added (0x4adf6) */
  uint8_t field_147;
  int32_t pps_cable_delay;           /* +0x148: gps' */
  uint8_t mask_angle;                /* +0x14c: gps' */
  uint8_t position_hold;             /* +0x14d: gps' */
  uint8_t douglas_duration_infinite; /* +0x14e: nv_settings' */
  uint8_t field_14f;
  char* position_source;       /* +0x150: "INIT ", "AVG " or "" */
  int32_t position[3];         /* +0x154: latitude, longitude, height; as
                                  position_source says */
  uint32_t average_count;      /* +0x160: gps' */
  int32_t douglas_duration;    /* +0x164: nv_settings' */
  uint16_t hardware_condition; /* +0x168: status_hardware_condition's */
} status_snapshot_record;

#include "../../common/types/psos.h"

#include "../../common/types/spoo_request.h"

#include "../../common/types/early_status_events.h"

#include "../../common/types/installer_test_choice.h"
