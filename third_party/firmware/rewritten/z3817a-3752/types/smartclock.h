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
 * the region (0x22cf6).  pllp_task sets it each pass; nv_clear_and_reset
 * and system_init's defaults clear it. */
typedef enum __attribute__((packed)) preserved_region_restore {
  PRESERVED_REGION_DISCARD = 0,
  PRESERVED_REGION_RESTORE = 1,
} preserved_region_restore;

/* BEGIN PROPOSED ENUMS */
/* END PROPOSED ENUMS */

/* gps_receive_state (0x101632): where gps_receive is in framing an
 * Oncore message into gps_response_buffer, its jump table at 0x52244, as
 * the 58503A's, where the Z3815A's frames an NMEA sentence.  A state
 * above GPS_FRAMING_COMPLETE drops each byte (0x52236). */
typedef enum __attribute__((packed)) gps_framing_state {
  GPS_FRAMING_FIRST_AT = 0,  /* expects the first @ */
  GPS_FRAMING_SECOND_AT = 1, /* expects the second @ */
  GPS_FRAMING_ID_FIRST = 2,  /* an uppercase letter */
  GPS_FRAMING_ID_SECOND = 3, /* a letter; the ID's length from the table */
  GPS_FRAMING_BODY = 4,      /* the rest, the checksum last */
  GPS_FRAMING_CR = 5,
  GPS_FRAMING_LF = 6,
  GPS_FRAMING_COMPLETE = 7, /* gps_receive reads no more */
} gps_framing_state;

#include "../../common/types/gps_request_result.h"

/* gps_request_class's sorting of a request code (gps_request_code). */
typedef enum __attribute__((packed)) gps_code_class {
  GPS_CLASS_MESSAGE = 0, /* 0 to 0x38 with argument mode 0 or 1: one message
                            (0x528d8) */
  GPS_CLASS_SCRIPT = 1,  /* 0x53 to 0x66: a script */
  GPS_CLASS_STEP = 2,    /* 0x39 to 0x52: gps_console_request's */
  GPS_CLASS_INVALID = 3,
} gps_code_class;

#include "../../common/types/oncore_field_code.h"

#include "../../common/types/oncore_receiver_status.h"

/* Which reference gps_receiver_status_update qualifies:
 * the GPS's, from the receiver's status, or the external's, from the
 * alternate receiver's, gps_alternate_status (0x51958, 0x51a4c), which
 * the 58503A 3704's ignores; gpsm_task and status_snapshot call it with
 * each. */
typedef enum __attribute__((packed)) gps_receiver_number {
  GPS_RECEIVER_ONCORE = 0,
  GPS_RECEIVER_ALTERNATE = 1,
} gps_receiver_number;

/* The console's override of a reference's validity,
 * gps_reference_record.override, which diag_force_gps_1pps and
 * diag_force_ext_1pps set from -1, 0 or 1 (0x2b3f6, 0x2b42e). */
typedef enum __attribute__((packed)) gps_reference_override {
  GPS_REFERENCE_OVERRIDE_NONE = 0x00,
  GPS_REFERENCE_OVERRIDE_VALID = 0x01,
  GPS_REFERENCE_OVERRIDE_INVALID = 0xff,
} gps_reference_override;

/* The EFC's mode, diag_efc_mode, as the 58503A 3704's, whose row the
 * seeding carries: :DIAGnostic:ROSCillator:EFControl:MODE NORM, USER or
 * TIM (the 58503A's 0x2b5a8, 0x2b5f6). */
typedef enum __attribute__((packed)) efc_mode {
  EFC_MODE_NORMAL = 0,
  EFC_MODE_USER = 1,
  EFC_MODE_TIMED = 2,
} efc_mode;

#include "../../common/types/counter_sync_kind.h"

/* A satellite's cell on the status screen, the code at +4 of a
 * satellite_status_list entry: satellite_elevation_azimuth sets 1 or 2,
 * satellite_status the rest (0x4fa40), and format_satellite_cell prints
 * each (its table at 0x4fe72): the Z3805A 3543B's, the acquiring bands 5
 * to 7 (0x4fae2, 0x4fb0e), with no "Isolat", which the Z3815A 4010's 5
 * is. */
typedef enum __attribute__((packed)) satellite_cell_status {
#include "../../common/types/satellite_cell_status.h"
  SATELLITE_CELL_ACQUIRING = 5,    /* "Acq   " */
  SATELLITE_CELL_SYNCHRONIZED = 6, /* "Acq . " */
  SATELLITE_CELL_EPHEMERIS = 7     /* "Acq .." */
} satellite_cell_status;

/* satellite_track_class's class of a visible satellite, by its health
 * (its table at 0x4f9f2): satellite_status shows a removed one as
 * "Ignore" and an unhealthy one as "Not OK".  The shared classes alone:
 * the long satellite_status passes it, which the Z3815A 4010's takes as
 * Time RAIM's set for a fourth class, it ignores. */
typedef enum __attribute__((packed)) satellite_health_class {
#include "../../common/types/satellite_health_class.h"
} satellite_health_class;

/* A mode line of the status screen, status_screen_mode_line.mode, which
 * status_screen picks by pll_screen_mode (0x50fe4, its table at
 * 0x50ffa) and marks ">>"; status_screen_mode_suffix takes power-up's
 * or recovery's. */
typedef enum __attribute__((packed)) status_screen_line {
  STATUS_SCREEN_LINE_POWERUP = 0,  /* "Power-up" */
  STATUS_SCREEN_LINE_HOLDOVER = 1, /* "Holdover" */
  STATUS_SCREEN_LINE_RECOVERY = 2, /* "Recovery" */
  STATUS_SCREEN_LINE_LOCKED = 3,   /* "Locked to " */
  STATUS_SCREEN_LINE_NONE = 4      /* no line; the table's end */
} status_screen_line;
/* A reference's state, gps_reference_record.state, and
 * status_snapshot_record's copies.  One more than the 58503A 3704's:
 * gps_receiver_status_update stores 0 to 5 by the faults
 * it checks in turn, 5 when it finds none (0x51bf6 to 0x51c20), then 6
 * to 9 (0x51b02 to 0x51b40), and the status line names each (0x552ba;
 * docs/firmware/revisions.md, "Reference state"). */
typedef enum __attribute__((packed)) gps_sync_status {
  GPS_SYNC_RECEIVER_FAILED = 0,       /* "Invalid: GPS rcvr failed" */
  GPS_SYNC_NOT_TRACKING = 1,          /* "Invalid: not tracking" */
  GPS_SYNC_INACCURATE_POSITION = 2,   /* "Invalid: inacc position" */
  GPS_SYNC_READING_NAVIGATION = 3,    /* "Invalid: reading NAV msg" */
  GPS_SYNC_RAIM_ERROR = 4,            /* "Invalid: Time RAIM error" */
  GPS_SYNC_INVALID = 5,               /* "Invalid" */
  GPS_SYNC_ASSESSING = 6,             /* "Assessing stability", for
                                         gps_reference_record.validation_time */
  GPS_SYNC_QUESTIONABLE = 7,          /* "Questionable accuracy", for the
                                         hysteresis */
  GPS_SYNC_QUESTIONABLE_CLEARING = 8, /* "Questionable accuracy" */
  GPS_SYNC_VALID = 9,                 /* "Synchronized to UTC" */
} gps_sync_status;

/* raim_monitor's assessment of Time RAIM (0x517d0), gps.raim_assessment:
 * gps_print_time_raim's "Assessment:" strings (0x4e87a's table). */
typedef enum __attribute__((packed)) gps_raim_assessment {
  GPS_RAIM_INACTIVE = 0,
  GPS_RAIM_OK = 1,
  GPS_RAIM_ALARM_DETECTED = 2, /* not OK since raim_suspect_time */
  GPS_RAIM_ALARM = 3,          /* not OK for 4 s */
} gps_raim_assessment;

/* The command monitor's state, gps.command_monitor_state: command_monitor's
 * switch (0x51de8) and gps_print_process_monitor's strings (0x53ab0's
 * table). */
typedef enum __attribute__((packed)) gps_command_monitor_state {
  GPS_COMMAND_MONITOR_DISABLED = 0,
  GPS_COMMAND_MONITOR_STANDBY = 1,
  GPS_COMMAND_MONITOR_QUERY = 2,     /* a command went unanswered; queries */
  GPS_COMMAND_MONITOR_QUERY_ACK = 3, /* waits for the query's result */
} gps_command_monitor_state;

/* The status monitor's state, gps.status_monitor_state: command_monitor's
 * switch (0x51f70) and gps_print_process_monitor's strings (0x53b24's
 * table), "disabled", "monitoring", "recovering" and "wait-recover". */
typedef enum __attribute__((packed)) gps_status_monitor_state {
  GPS_STATUS_MONITOR_DISABLED = 0,
  GPS_STATUS_MONITOR_STANDBY = 1,  /* watches the status and Time RAIM come */
  GPS_STATUS_MONITOR_RECOVERY = 2, /* waits for the status monitor's request
                                      (0x52096); nothing sets it */
  GPS_STATUS_MONITOR_MISSED = 3,   /* watches as in standby after one miss;
                                      a second fails the monitor (0x5201c) */
} gps_status_monitor_state;

/* @@AB: the application type, gps.application_type:
 * gps_print_instrument_state's "APPL" strings (0x4eda0's table). */
typedef enum __attribute__((packed)) oncore_application_type {
  ONCORE_APPLICATION_AIR = 0,
  ONCORE_APPLICATION_HANDHELD = 1,
  ONCORE_APPLICATION_LAND = 2,
  ONCORE_APPLICATION_MARINE = 3,
  ONCORE_APPLICATION_STATIC = 4,
} oncore_application_type;

/* @@Ah: the satellite selection, gps.satellite_select:
 * gps_print_instrument_state's "SELECTION" strings (0x4f070's table). */
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
 * (0x2c220) and pllp_task's table of stages (0x4cd52); the 58503A
 * 3704's, with no "n3 lock pll", the Z3815A 4010's 6
 * (docs/firmware/z3817a-3752.md, "The lamps"). */
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

/* An ADC channel, adc_read's argument and the index of hmon_channels
 * and adc_monitors, named by its hmon_channels entry (0x32498, eight
 * entries of 0x2a bytes): five channels, entries 1 to 5, where the
 * 58503A 3704's has seven
 * (docs/firmware/z3817a-3752.md, "The health monitor's channels"). */
typedef enum __attribute__((packed)) adc_channel {
  ADC_CHANNEL_0 = 0, /* no convert routine and an empty name; the 58503A's
                        temperature */
  ADC_5V = 1,        /* "5V"; the -15V and -12B converts subtract it */
  ADC_PLUS_15V = 2,  /* "15V" */
  ADC_MINUS_15V = 3, /* "-15V" */
  ADC_PLUS_12B = 4,  /* "12B"; hardware bit 13 */
  ADC_MINUS_12B = 5, /* "-12B"; hardware bit 14 */
  ADC_CHANNEL_6 = 6, /* no convert routine and an empty name */
  ADC_CHANNEL_7 = 7, /* no convert routine and an empty name */
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

/* startup_pll's and pll_normal's steering, at loop_steering: a
 * proportional-integral loop on each 10-second block's mean time
 * interval, its gains from the loop's time constant. */
typedef struct loop_steering_state {
  float integral; /* +0x00: the integral term, in EFC counts */
  float ti_sum;   /* +0x04: the block's valid readings, then their mean */
  int32_t settled_blocks;     /* +0x08: startup_pll's consecutive blocks whose
                                 |mean| is below 150 ns, to 15; one at or above
                                 it clears it */
  uint8_t seconds;            /* +0x0c: the block's seconds, to 10 */
  uint8_t readings;           /* +0x0d: the block's valid readings */
  uint8_t measurement_failed; /* +0x0e: a reading came with the GPS reference
                                 questionable (0x494b8, 0x495e0); a block with
                                 none summed then sets no hold cause
                                 (0x49650) */
  uint8_t field_0f; /* +0x0f: a reading failed with both 1PPS alive and none
                       replaced (0x4960e); a block with none summed then holds
                       for a TI error, not the limit (0x4965a) */
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

/* The bank of LEDs on the board, the DUART's output port bits, which
 * duart_output_bits resets (lit, the pin high) and sets
 * (docs/firmware/outputs.md, "The rest of the bank"): the other images'
 * six, OP2 to OP7, named by their use there.  The primary here drives
 * OP6 and OP7 alone (duart_output_bits, 0x2d160): OP6 hmon_task's
 * heartbeat, flipped every 112 passes, about every 1.1 s (0x343b6 to
 * 0x343d8), and OP7 lit on a failure of the reset code's CPU, RAM or
 * DUART test (0x247be, 0x248f4, 0x24912) or a power-up self-test, which
 * stays lit. */
typedef enum __attribute__((packed)) board_led {
  BOARD_LED_HEARTBEAT = 0x04, /* OP2, the other images' heartbeat; the
                                 primary here does not drive it */
  BOARD_LED_INSTALLER = 0x08, /* OP3, the boot code entered the installer */
  BOARD_LED_CPU = 0x10,       /* OP4, the other images' failed CPU test; the
                                 primary here does not drive it */
  BOARD_LED_RAM = 0x20,       /* OP5, the other images' failed RAM test; the
                                 primary here does not drive it */
  BOARD_LED_DUART = 0x40,     /* OP6, the other images' failed DUART test;
                                 here hmon_task's heartbeat */
  BOARD_LED_SELFTEST = 0x80,  /* OP7, a power-up self-test failed, and here
                                 the reset code's tests too */
} board_led;

/* The latch at 0x300000's bits, which selftest.latch shadows and
 * latch_bits_set and its kin write (docs/firmware/z3817a-3752.md, "The
 * lamps"): the two lamps lamps_write writes through latch_lamps_write
 * (0x236c8), from lamps_compose's lamp_bits; bit 0, which lamp_test writes
 * and which lights nothing; and bits 6 and 7, named for the 58503A's front
 * panel lines, of which the flash test sets and clears bit 6 (0x3c714,
 * 0x3c738) and nothing here names bit 7. */
typedef enum __attribute__((packed)) latch_bit {
  LATCH_BIT_0 = 0x01,
  LATCH_LAMP_1 = 0x02, /* lit while locked, blinking while acquiring */
  LATCH_LAMP_2 = 0x04, /* lit on led_alarm */
  LATCH_FRONT_PANEL_FRAME = 0x40,
  LATCH_FRONT_PANEL_RESET = 0x80,
} latch_bit;

#include "../../common/types/io_device.h"

#include "../../common/types/data_format.h"

/* The time code's format, :PTIMe:TCODe:FORMat, which tcode_request
 * takes, T1 to FL3 (0x23cc8): T1, the GPS seconds; T2 with a test prefix;
 * T2; and FL1, FL2 and FL3, the query's keywords' third to fifth
 * (scpi_choice_keywords_query_SOURce_PTIMe_TCODe_1), which
 * :PTIMe:TCODe:FORMat? answers for 3 to 5 (0x3ee3c) but the setter does
 * not store (0x3edc6), and which timecode_update sends as a line end
 * alone (0x23f84). */
typedef enum __attribute__((packed)) timecode_format {
#include "../../common/types/timecode_format.h"
  TIMECODE_FL1 = 3,
  TIMECODE_FL2 = 4,
  TIMECODE_FL3 = 5,
} timecode_format;

/* tcode_send's routine, which timecode_schedule and output compare 3's
 * interrupt send the time code through (0x240cc, 0x2410c):
 * sci_send_async's shape, which tcode_request stores (0x23cfc). */
typedef void tcode_send_function(char* block, uint8_t count);

#include "../../common/types/klok_event_class.h"

#include "../../common/types/duartb_settings_record.h"

/* The SCI's serial settings, sci_settings at 0x10249e, and the
 * installer's at 0x10acb0: one byte for parity and data bits together and
 * no stop-bit setting.  sci_init and sci_reconfigure apply them.
 * parameter_record_read reads +2 to +5 from the parameter record's
 * bytes 9 to 0xc (0x41b20 to 0x41b38), or copies the defaults at 0x4153a
 * over all 6 bytes when its checksum fails; installer_parameter_record_read
 * reads +2 to +5 from the record's bytes 9 to 0xc, or copies its defaults at
 * installer::0x10836a. */
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

#include "../../common/types/selftest_test.h"

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

/* DUART channel B's driver state, duartb at 0x1037f8: in the Z3817A
 * 3752 its first ten bytes alone, the Z3815A 4010's but for the byte
 * the receive interrupt keeps the least free space in, each where
 * duartb_transmit_interrupt, the receive interrupt (0x2d8f6), the
 * DUART's init (0x2ddda), its send (0x2dfb6) and duart_loopback_test_b
 * address it; the loopback test's pointers follow it
 * (loopback_receive_next, 0x103802). */
typedef struct duartb_driver {
  char* transmit_next;    /* +0x00: the send's next character */
  int16_t transmit_count; /* +0x04: the send's characters left */
  uint8_t loopback_test;  /* +0x06: set while duart_loopback_test_b runs */
  uint8_t field_07;       /* the DUART's init clears it */
  uint8_t least_free;     /* +0x08: the receive ring's least free space seen */
  uint8_t held;           /* +0x09: duart_loopback_test_b sets it; while it
                             is set drtb_send_message leaves the transmit
                             interrupt off, as duarta.held */
} duartb_driver;

/* The QSM's SCI port's exchanges, sciR and sciW, and the token messages
 * sci_init sends them, sci_exchanges at 0x10380e, as the Z3805A's. */
typedef struct sci_exchange_state {
  uint32_t read_exchange;
  exchange_token read_token; /* +0x04: its task is signaled on a ctrl-C and when
                                input comes */
  uint8_t field_14[8];
  uint32_t write_exchange;    /* +0x1c */
  exchange_token write_token; /* +0x20: its task is signaled 0x80 when
                                 sci_puts's characters are out */
  uint8_t field_34[8];
} sci_exchange_state;

/* The SCI port's driver, sci at 0x103948, after its receive ring at
 * 0x103846, qspi after it at 0x103968: the installer's layout
 * (installer_sci_driver) rather than the Z3815A 4010's: a byte of its
 * own at +0x0c, the following ones a byte on to async_allowed, and no
 * field_17; but that the primary's driver, the Z3805A 3543B's SCI
 * driver's code, keeps no transmitting state and its last terminator
 * where the installer's keeps transmit_busy.  Each field where the
 * driver's code addresses it: flow at 0x103955 (sci_transmit_allowed,
 * 0x2f410), async_count at 0x103952, the echo at 0x103962, the last
 * terminator at 0x103966 (check-layout.c). */
typedef struct sci_driver {
  char* transmit_next;     /* +0x00: sci_puts's next character */
  uint16_t transmit_count; /* +0x04: sci_puts's characters left */
  char* async_next;        /* +0x06: sci_send_async's */
  uint8_t async_count;     /* +0x0a */
  uint8_t async_pending;   /* +0x0b */
  uint8_t puts_pending;    /* +0x0c: set at a character of sci_puts's write,
                              cleared once they are out */
  uint8_t flow; /* +0x0d: 0x80 an XOFF received, holding transmission; 0x20 one
                   sent */
  uint8_t receive_errors; /* +0x0e: SCSR's OR, FE and PF, overrun, framing and
                             parity (MC68331UM D.4.6) */
  uint8_t read_errors;    /* +0x0f: receive_errors as the last read left them */
  uint8_t xon_xoff;       /* +0x10: flow control, from the serial settings */
  uint8_t mark_bit; /* +0x11: each character goes out with its top bit set: mark
                       parity, which SCCR1's PE and PT cannot give */
  uint8_t
      seven_bits; /* +0x12: seven data bits, the top bit received stripped */
  uint8_t reconfiguring; /* +0x13: set while sci_reconfigure changes the port */
  uint8_t in_transmit_interrupt; /* +0x14: sci_send_async needs it clear */
  uint8_t async_allowed;         /* +0x15: an asynchronous send may start */
  uint8_t field_16;              /* +0x16: sci_release_for_timecode sets it */
  uint8_t field_17;              /* the installer's transmitting; nothing in the
                                    primary refers to it */
  uint16_t baud_constant; /* +0x18: a character's time in timer counts at the
                             baud rate, sci_baud_set's (0x2e700) */
  putc_function* echo;    /* +0x1a: from the serial settings; sci_echo_none with
                             echo off */
  uint8_t last_terminator; /* +0x1e: the CR or LF that ended the last line
                              (sci_gets, 0x2e8c0), where the installer keeps
                              its transmit_busy */
  uint8_t field_1f;        /* nothing in the primary refers to it */
} sci_driver;

/* The installer's SCI driver, installer_sci at 0x10b006, after its
 * receive ring at 0x10af04: sci_driver's fields with a byte of its own
 * at +0x0c, the following ones a byte on to async_allowed, and no
 * field_17, each where the installer's driver code addresses it
 * (check-layout.c); its routines keep the 58503A's installer's drtb
 * names, the SCI's here. */
typedef struct installer_sci_driver {
  char* transmit_next;     /* +0x00: installer_sci_puts's next
                              character */
  uint16_t transmit_count; /* +0x04: its characters left */
  char* async_next;        /* +0x06: installer_sci_send_async's */
  uint8_t async_count;     /* +0x0a */
  uint8_t async_pending;   /* +0x0b */
  uint8_t puts_pending;    /* +0x0c: set at a character of
                              installer_sci_puts's write, cleared,
                              its task signaled 0x80, once they are out */
  uint8_t flow; /* +0x0d: 0x80 an XOFF received, holding transmission; 0x20 one
                   sent */
  uint8_t receive_errors; /* +0x0e: SCSR's OR, FE and PF */
  uint8_t read_errors;    /* +0x0f: receive_errors as the last read left them */
  uint8_t xon_xoff;       /* +0x10: flow control, from the serial settings */
  uint8_t mark_bit;       /* +0x11: each character goes out with its top bit
                             set: mark parity */
  uint8_t seven_bits;     /* +0x12: seven data bits, the top bit received
                             stripped */
  uint8_t reconfiguring;  /* +0x13: set while installer_sci_reconfigure
                             changes the port */
  uint8_t in_transmit_interrupt; /* +0x14: set while the transmit interrupt
                                    runs; installer_sci_send_async needs it
                                    clear */
  uint8_t async_allowed;         /* +0x15: an asynchronous send may start */
  uint8_t field_16;        /* +0x16: the driver's initialization clears it */
  uint8_t transmitting;    /* +0x17: from a character's write until a
                              character time after the last one */
  uint16_t baud_constant;  /* +0x18: a character's time in timer counts */
  putc_function* echo;     /* +0x1a: from the serial settings */
  uint8_t transmit_busy;   /* +0x1e: set at each character's write, cleared
                              when its end is scheduled */
  uint8_t last_terminator; /* +0x1f: the CR or LF that ended the last line */
} installer_sci_driver;

#include "../../common/types/qspi_state.h"

/* A device on the QSPI, by the minor number of I/O device 1, which
 * qspi_queue_setup (0x2f6d8) takes and sets up a queue for, by its jump
 * table on the minor less 5 (0x2f726): this image's own numbers, none
 * the other images', whose devices from 1
 * it does not share.  No other minor is set up. */
typedef enum __attribute__((packed)) qspi_device {
  QSPI_EFC = 5,             /* one word (0x2f8b2); efc_write's (0x2e684) */
  QSPI_ADC = 0xa,           /* a byte, then a word (0x2f744); adc_read's */
  QSPI_FPGA_BLOCK = 0xc,    /* up to 16 words (0x2f7a2) */
  QSPI_TWO_WORDS = 0xf,     /* two words (0x2f76a); no caller names it */
  QSPI_FPGA_TAIL = 0x10,    /* two words; qspi_fpga_tail_write's (0x2e6be) */
  QSPI_FPGA_WORD = 0x11,    /* one word; qspi_fpga_word_write's (0x2e6d6) */
  QSPI_FPGA_BYTE = 0x12,    /* one byte; qspi_fpga_byte_write's (0x2e6ee) */
  QSPI_FPGA_READBACK = 0x13 /* 8 to 16 bits; qspi_transfer copies one word
                               back for it (0x2fcd6) */
} qspi_device;

#include "../../common/types/qspi_buffers.h"

/* The EFC's gain and what efc_gain_set derives from it, efc_gains at
 * 0x102a9e: pllp_task sets the gain, the fractional frequency one EFC
 * count moves.  0x14 bytes, gps_command after them (0x102ab2):
 * efc_gain_set (0x4cae2) stores through step_limit and, unlike the
 * Z3815A 4010's, no aging_per_day. */
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
} efc_gain_values;

#include "../../common/types/efc_range_state.h"

#include "../../common/types/gps_command_mailbox.h"

/* One of the health monitor's eight ADC monitors, adc_monitors at
 * 0x10204e to 0x1021cd, by adc_channel: 1 to 5 the supplies, hmon_channels'
 * five (0x32498), 0, 6 and 7 unused.  monitor_average_add, each channel's
 * add, keeps a 10-sample moving average in it; monitor_filter_add, a
 * 0.9/0.1 filter, is no channel's here.  adc_average reads it, and
 * monitors_reset clears +0x2c to +0x2f. */
typedef struct adc_monitor {
  float
      samples[10]; /* +0x00: the last ten readings; monitor_average_add only */
  float value;     /* +0x28: their mean, or the filtered value */
  uint8_t valid;   /* +0x2c: set once ten readings are in;
                      adc_average returns a default and monitor_out_of_range false
                      while clear */
  uint8_t sample_index;       /* +0x2d: where the next reading goes, 0 to 9 */
  uint8_t out_of_tolerance;   /* +0x2e: hmon_report_tolerance's "is out of
                                 tolerance" state (0x32128) */
  uint8_t out_of_range_count; /* +0x2f: monitor_out_of_range's run of readings
                                 outside the range, held at its limit */
} adc_monitor;

/* The status summary text tree, the 58503A 3704's, which
 * alarm_text_collect (0x39e28) walks from alarm_text_root (0x3a3ba) for
 * alarm_text's (0x39fb8): a node is a status register and its entries,
 * one to the register's bit, an entry a child node, a name ("QUES SUM",
 * "OPER SUM", ...) and the severity alarm_text_collect keeps the least of
 * (+8, 0x39e9c). */
struct alarm_text_entry;
typedef struct alarm_text_node {
  int8_t register_index; /* +0x00: status_peek_event's index and
                            nv_settings.status_enable's (0x39e50, 0x39e66);
                            less 2 the transition filters' (0x39eaa) */
  uint8_t field_01;
  struct alarm_text_entry* entries; /* +0x02: to the first with no name */
} alarm_text_node;
typedef struct alarm_text_entry {
  alarm_text_node* child; /* +0x00: null for a leaf */
  char* name;             /* +0x04 */
  uint8_t severity;       /* +0x08 */
  uint8_t field_09;
} alarm_text_entry;

/* The alarm text cursor, the 58503A 3704's, which alarm_text builds on
 * its stack (link -6, 0x39fbe) and alarm_text_collect fills: where the
 * text goes and the least severity seen, which alarm_text_collect lowers
 * to a matching entry's (0x39e8c, 0x39ea4). */
typedef struct alarm_text_cursor {
  char* text;       /* +0x00 */
  uint8_t severity; /* +0x04: 4 at first (0x39fd2) */
  uint8_t field_05;
} alarm_text_cursor;

/* How a GPS request or script step gives its message's arguments
 * (gps_request_entry.argument_mode, gps_script_step.argument_mode,
 * gps_running_command.query): gps_request_class takes 0 and 1 for a
 * message, gps_print_process_monitor prints them "change/execute" and
 * "query setting", and gpsm_task takes 2 as 0 with gps_request_step's
 * arguments from the index in the step's first (0x52dda to 0x52df2). */
typedef enum __attribute__((packed)) gps_argument_mode {
  GPS_ARGUMENTS_SET = 0,          /* the message with its arguments */
  GPS_ARGUMENTS_QUERY = 1,        /* the message's query form */
  GPS_ARGUMENTS_FROM_REQUEST = 2, /* set, gps_request_step's arguments */
} gps_argument_mode;

/* gps_request_code_word: a gps_request_code in a word, gps_script_step's
 * code and its shorter forms', which gpsm_task reads with move.w
 * (0x52da0) and gives gps_request_class, which compares it signed
 * (0x528d4 to 0x5290a), and fills from the request's word (0x52cbe);
 * gps_request_entry's code, which gpsm_task gives gps_request_class
 * (0x52b80, 0x52c8e) and gps_script_lookup (0x52d22), which sign-extends
 * it (movea.w, 0x52918); gps_running_command's message, the step's code
 * (0x52e22); and the code gps_request_post and gps_message_supported
 * take, a word slot (cmpi.w 0x52816, 0x56fd2), and gps_console_request
 * (movea.w 0x53134) and gps_request_build, gps_response_match and
 * gps_response_timed_out, which pass it to oncore_message_entry_at
 * (0x57040, 0x57298, 0x52382; its ext.l, 0x57018); gps_response_index
 * (0x101634), which gpsm_task passes as a word (0x52f90); and
 * gps_command_log_entry's code and message (0x53276, 0x5327c).  Its values are
 * gps_request_code's; its one name only
 * makes it a signed word, in gcc's packing and in SmartClockSetup.java's
 * sizing of enums alike, and no step holds it. */
typedef enum __attribute__((packed)) gps_request_code_word {
  GPS_REQUEST_CODE_WORD_MIN = -0x8000,
} gps_request_code_word;

/* A GPS script's step, as gpsm_task runs it.  The one at 0x10201c,
 * gps_request_step, is in RAM: the one-step script gps_script_message
 * (0x575cc) points at it, and gpsm_task fills it from a request
 * gps_request_class puts in class 0, a message. */
typedef struct gps_script_step {
  gps_request_code_word code;      /* +0x00: gps_request_build's code */
  gps_argument_mode argument_mode; /* +0x02: GPS_ARGUMENTS_FROM_REQUEST from
                                      the index in arguments[0] */
  uint8_t field_03;
  int32_t arguments[4]; /* +0x04: gps_request_step's are also a class 1
                           request's, a script's, and raim_request_arguments' */
} gps_script_step;

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

/* A self-test's number, selftest_run's test (0x289b4), the Z3815A
 * 4010's numbers: selftest_table (0x284c0), whose entry selftest_execute
 * calls (0x28366), has 11 here, no interpolator's, so that selftest_run
 * runs entry n for tests 0 to 7, entry n - 1 for 9 to 11 and nothing for
 * 8, 12 and 13 (its jump table at 0x289c8).  :DIAGnostic:TEST? maps its
 * choice to it, alphabetically from the words at 0x5b7b6 by the jump
 * table at 0x3c5c6; 097-59551-02, ":DIAGnostic:TEST?", lists the words
 * but IREFerence, IMRC and QMRC in this order.  0 is *TST?'s
 * (selftest_request at 0x4b88c).  INT, IMRC and QMRC, the Z3815A's
 * interpolator's test, calibration and sample, run nothing here. */
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
  /* Past the table, this image's own: :DIAGnostic:CALibration:ROSCillator
   * requests it (0x3c7a0), and gps_diag_sequence runs it by stepping
   * efc_calibration_measure (0x4c7da); what it calibrates is not
   * established past that. */
  SELFTEST_TEST_15 = 15,
} selftest_test_number;

/* selftest_summary's failure flags (0x277f2), selftest_results.summary
 * and test 0's result: bit n - 1 for test n that failed.  The display
 * test, 1, has no result, so bit 0 is never set, nor, with no
 * interpolator, bit 7. */
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
 * (cpu_register_test 0x27496), eeprom (nv_slot_valid 0x439d6), fpga
 * (counter_selftest 0x45a0c: the counters read nonzero after a preset)
 * and internal_reference (internal_pps_test 0x28500); each selftest_*
 * entry reports its bit 0. */
typedef enum __attribute__((packed)) selftest_failure {
  SELFTEST_FAILED = 0x01,
} selftest_failure;

/* selftest_results.ram, the RAM test's failures, as selftest_ram
 * reports them (0x27932): a pattern, its data bits at ram_data_bits,
 * or an address line, the lines at ram_address_lines. */
typedef enum __attribute__((packed)) selftest_ram_failure {
  SELFTEST_RAM_PATTERN = 0x01,
  SELFTEST_RAM_ADDRESS_LINE = 0x02,
} selftest_ram_failure;

#include "../../common/types/selftest_uart_failure.h"

/* selftest_results.qspi, the QSPI devices that failed, as
 * qspi_tests_run sets them and selftest_qspi reports them: the ADC, the
 * FPGA and the EFC (0x2893c, 0x28956, 0x28970); with no hex DAC, nothing
 * here sets SELFTEST_QSPI_HEX_DAC. */
typedef enum __attribute__((packed)) selftest_qspi_failure {
  SELFTEST_QSPI_ADC = 0x02,
  SELFTEST_QSPI_HEX_DAC = 0x04,
  SELFTEST_QSPI_FPGA = 0x40,
  SELFTEST_QSPI_EFC = 0x80,
} selftest_qspi_failure;

/* selftest_results.gps, the receiver's @@Ca result (VPCommands.pdf,
 * "6 Channel Self-Test (@@Ca)": bit 0 channel 1 to bit 13), by
 * selftest_gps's texts (0x27d1e), which name bits 7 to 10 ROM LSByte,
 * ROM MSByte, RAM LSByte and RAM MSByte where the reference's list is
 * garbled.  As in the 58503A 3704, the result is a long and the
 * firmware's own two are bits 16 and 17, gps_selftest_run's (0x4bc38 to
 * 0x4bd54): the engine did not answer, "GPS: SW problem", or timed out,
 * "GPS: Engine timeout" (selftest_gps, 0x27f4c, 0x27f60).  An
 * eight-channel engine's @@Fa result, which selftest_gps reads
 * while gps.field_592 bit 7 is set (0x27dce), has channels 1 to 8 in
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

/* The self-tests' results, selftest at 0x100e26, 0x40 bytes, up to the
 * GPS record (0x100e66): each test's word nonzero if it failed, and what
 * selftest_run reports for test n.  The 58503A 3704's less its
 * interpolator's eight bytes, each field where the image's code
 * addresses it (make record-access IMAGE=z3817a-3752 RECORD='0x100e26
 * 0x40 --fit selftest_results'). */
typedef struct selftest_results {
  int16_t cpu;  /* +0x00: the reset code's CPU test, selftest_failure; lights
                   OP7 (0x247be) */
  uint16_t ram; /* +0x02: the RAM test's, selftest_ram_failure's bits; lights
                   OP7 (0x248f4) */
  uint16_t ram_data_bits; /* +0x04: the bits that failed a pattern, both halves
                             of a long ORed */
  uint32_t ram_address_lines; /* +0x06: the address lines that alias */
  int16_t uart;    /* +0x0a: duart_early_init's IVR and loopback checks,
                      selftest_uart_failure's bits; lights OP7 (0x24912) */
  latch_bit latch; /* +0x0c: the latch at 0x300000's bits, which latch_bits_set
                      and its kin keep and write */
  uint8_t lamp_mask; /* +0x0d: the DUART output bits the failures reset, which
                        primary_reset sets and duart_channels_init writes
                        back (0x2debe) */
  uint16_t summary;  /* +0x0e: selftest_summary_bit's */
  int16_t eeprom;    /* +0x10: selftest_failure: nv_restore sets it when
                        neither NV slot has the tag and a good checksum,
                        whatever its revision (nv_records_valid), and
                        selftest_eeprom reads it (0x27ad2) */
  int16_t
      fpga; /* +0x12: the FPGA's counters, counter_selftest; selftest_failure */
  uint32_t gps;    /* +0x14: the receiver's self-test, a long: gps_selftest_run
                      and gps_diag_sequence store it, selftest_gps tests it by
                      bit */
  uint8_t abort;   /* +0x18: set to stop a test running, selftest_abort */
  uint8_t running; /* +0x19: set while selftest_execute runs a test */
  int16_t power;   /* +0x1a: the supplies, selftest_power_failure's bits */
  int16_t internal_reference; /* +0x1c: the internal 1PPS's, selftest_failure */
  int16_t qspi; /* +0x1e: the QSPI's devices, selftest_qspi_failure's bits */
  uint8_t field_20[14];
  uint8_t lamp_testing; /* +0x2e: set while lamp_test runs */
  uint8_t field_2f;
  uint8_t passed; /* +0x30: 1 unless a test selftest_print_failure ran failed */
  uint8_t field_31;
  uint32_t result;     /* +0x32: selftest_run's test's result, what *TST? and
                          :DIAGnostic:TEST? report */
  uint32_t details[2]; /* +0x36: the RAM test's bits and lines */
  selftest_test_number test; /* +0x3e: the test selftest_run was asked for */
  uint8_t in_run; /* +0x3f: set during selftest_run; nothing reads it */
} selftest_results;

/* Who sends a GPS request (gps_request_entry.sender), so that its result
 * comes back to it: gpsx_send for the loop's GPS sequences (pllp_task
 * takes the result as gps_ack_code), pll_scpi_send_to_loop for a
 * setting (pll_scpi_complete), and gps_request_post for the command
 * and status monitors, whose results gps_count_response keeps in
 * command_monitor_result and status_monitor_result (0x5287e, 0x52412).
 * request_kind_valid takes 1 to 5, from its table at 0x594dc, which
 * GPS_SENDER_NONE ends.  Its own, where the others' monitors are 3 and
 * 4: pll_scpi_send_to_loop's caller, pll_scpi_command, passes 3 for a
 * message whose byte +5 is neither 0 nor 5, 2 for the rest (0x484c8 to
 * 0x484e4), and for 3 the status snapshot step takes its snapshot at
 * 0x101852, not 0x1016e2 (0x53184 to 0x53194); gps_request_post is
 * called with 4 alone, command_monitor's (0x51e7c). */
typedef enum __attribute__((packed)) gps_request_sender {
#include "../../common/types/gps_request_sender.h"
  GPS_SENDER_SETTING_OTHER = 3,
  GPS_SENDER_COMMAND_MONITOR = 4,
  GPS_SENDER_STATUS_MONITOR = 5,
} gps_request_sender;

/* A GPS request's code (gps_request_entry.code, a descriptor's
 * gps_request), which gps_request_class (0x528ce) sorts: 0 to 0x38 an
 * Oncore message, the index of its entry in oncore_messages (0x575d8;
 * the VP Oncore Command Reference names each,
 * third_party/motorola/VPCommands.pdf, and the UT Plus engineering
 * notes those it lacks, third_party/motorola/ut-engg-notes.pdf); 0x39
 * to 0x52 a step of the GPS task's own, gps_console_request's
 * (0x53128, its jump table at 0x53152); 0x53 to 0x66 a script,
 * gps_script_lookup's (0x52918), each named by the messages it sends
 * (docs/firmware/oncore/z3817a-3752.txt).  The 58503A 3704's codes, one
 * higher from 0x36: its messages, then @@Sz at 0x36, so that its second
 * @@Ea and @@Ar are one higher; its steps, but that the survey's stop
 * clears field_577 too and a step of its own sets it (0x4a), so that
 * the PPS check, RAIM's arguments and the steps after them are two
 * higher, and the @@Cj parse has none (0x4e); and its scripts, entry for
 * entry.  The table's empty slots, 0x1a, 0x22, 0x23, 0x25, 0x28, 0x2b
 * and 0x2c, and the steps gps_console_request does nothing for, 0x44,
 * 0x4d, 0x4e, 0x50 and 0x52, have no name. */
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
  GPS_REQUEST_POWER_ON_FAILURE = 0x36,         /* @@Sz */
  GPS_REQUEST_POSITION_STATUS_8_SECOND = 0x37, /* @@Ea again, without a decoder
                                                  or a filter
                                                  (docs/firmware/gps.md, "Six
                                                  or eight channels") */
  GPS_REQUEST_FIX_ALGORITHM_SECOND = 0x38,     /* @@Ar again, its filter
                                                  0x56fbc */
  GPS_STEP_STATUS_SNAPSHOT = 0x39,             /* status_snapshot */
  GPS_STEP_PRINT_STATUS = 0x3a,
  GPS_STEP_PRINT_VISIBILITY = 0x3b,
  GPS_STEP_PRINT_EXTENSION = 0x3c,
  GPS_STEP_PRINT_BEST_COMBINATIONS = 0x3d,
  GPS_STEP_PRINT_ID = 0x3e,
  GPS_STEP_PRINT_PPS = 0x3f,
  GPS_STEP_PRINT_INSTRUMENT_STATE = 0x40,
  GPS_STEP_PRINT_TIME_RAIM = 0x41,
  GPS_STEP_PRINT_AVERAGE = 0x42,
  GPS_STEP_AVERAGE_RESET = 0x43,       /* gps.average_count = 0 */
  GPS_STEP_AVERAGE = 0x44,             /* the console's avg; gps_console_request
                                          does nothing with it */
  GPS_STEP_GPS_HYSTERESIS = 0x45,      /* the GPS reference's hysteresis from
                                          its argument */
  GPS_STEP_EXTERNAL_HYSTERESIS = 0x46, /* the external 1PPS reference's */
  GPS_STEP_TRACKING_LOG = 0x47,        /* gps.tracking_log, its argument, and
                                          tracking_log_active cleared with it 0 */
  GPS_STEP_SURVEY_START = 0x48,        /* gps.surveying = 1 */
  GPS_STEP_SURVEY_STOP = 0x49,         /* gps.surveying and field_577 = 0 */
  GPS_STEP_FIELD_577_SET = 0x4a,       /* gps.field_577 = 1 */
  GPS_STEP_PPS_CHECK = 0x4b,      /* gps.pps_check_disabled, its argument */
  GPS_STEP_RAIM_ARGUMENTS = 0x4c, /* raim_request_arguments */
  GPS_STEP_STATUS_MONITOR = 0x4f, /* gps.status_monitor_enabled, its argument */
  GPS_STEP_LINK_RESET = 0x51,     /* gps_link_reset */
  /* one console word's alone: its lookup runs gps_init, then it
   * sends @@Cf and the defaults */
  GPS_SCRIPT_INIT_DEFAULTS = 0x53,
  /* power-up's and init_gps's: every setting, without @@Cf or a position */
  GPS_SCRIPT_INIT = 0x54,
  GPS_SCRIPT_FIX_RESTART = 0x55,     /* idle and fix again, the PPS check off */
  GPS_SCRIPT_ENGINE_SELFTEST = 0x56, /* @@Ea off, @@Ca and @@Fa, idle */
  GPS_SCRIPT_OUTPUTS_ON = 0x57,      /* @@Ba, @@Ea and @@Bb each second */
  GPS_SCRIPT_OUTPUTS_OFF = 0x58,     /* @@Ba, @@Ea, @@Bb, @@Bc and @@Bk off */
  GPS_SCRIPT_QUERY_SETTINGS = 0x59,  /* each setting's query, without the
                                        58503A's @@Cj and its parse */
  GPS_SCRIPT_UTC_SET = 0x5a,         /* @@Aw from its arguments, @@Ba, @@Ea */
  GPS_SCRIPT_HOLD_AT =
      0x5b, /* @@As from its arguments, hold on, TRAIM, restart */
  GPS_SCRIPT_POSITION_SET = 0x5c,  /* @@Ad, @@Ae, @@Af, idle around them */
  GPS_SCRIPT_HOLD_RELEASE = 0x5d,  /* survey stop, average reset, hold off,
                                      TRAIM, restart */
  GPS_SCRIPT_SURVEY_FROM = 0x5e,   /* hold off, @@Ad to @@Af from its
                                      arguments, TRAIM, restart, survey start,
                                      average reset */
  GPS_SCRIPT_HOLD_OFF = 0x5f,      /* hold off, TRAIM, average reset */
  GPS_SCRIPT_HOLD_AT_QUIET = 0x60, /* @@As, hold on, TRAIM, no restart */
  GPS_SCRIPT_HOLD_SELECT = 0x61,   /* @@At from its arguments, TRAIM, restart */
  GPS_SCRIPT_HOLD_POSITION = 0x62, /* @@As from its arguments, restart */
  GPS_SCRIPT_TRAIM = 0x63,         /* @@Bn and @@En from their arguments */
  GPS_SCRIPT_TIME_SET = 0x64,      /* @@Aa from its arguments, idle around it */
  GPS_SCRIPT_DATE_SET = 0x65,      /* @@Ac from its arguments, idle around it */
  GPS_SCRIPT_SATELLITE_SELECT = 0x66, /* @@Ah 1, each channel's @@Ai cleared,
                                         then from its arguments */
  GPS_REQUEST_NONE = 0xff,            /* a descriptor's, for none */
} gps_request_code;

/* A GPS request: the message pll_scpi_send_to_loop, gpsx_send and
 * gps_request_post build in 26 bytes of their frames and send
 * gpsx_exchange, which gpsm_task receives, keeps in its ring, and
 * gps_count_response sends back to plla_exchange with the result, for
 * pllp_task and gps_selftest_run.  The 58503A 3704's, whose GPS code
 * this image's is: gpsx_send clears +0x0 and +0x4 of its 26-byte frame
 * (0x4bb7a, 0x4bb7e), and request_ring_put copies 26 bytes into the
 * ring (0x524b6 to 0x524c2), where the Z3815A 4010's is 28 bytes. */
typedef struct gps_request_entry {
  uint32_t
      field_00; /* +0x00: the senders clear it; the kernel does not copy it */
  uint32_t field_04; /* +0x04: the senders clear it; kernel_send_for_task
                        refuses a nonzero one other than the exchange's id,
                        error 4 */
  gps_request_sender
      sender; /* +0x08: one with the same sender waiting is not queued again
                 (request_ring_contains); the monitors' may be replaced
                 (request_ring_newest_replaceable) */
  gps_request_result result;  /* +0x09: 1 as sent; gps_count_response's kind as
                                 it comes back, which pllp_task takes as
                                 gps_ack_code or pll_scpi_complete's success */
  gps_request_code_word code; /* +0x0a: the request, gps_request_class's index;
                                 code 0x51 empties the ring before it is queued
                                 (gps_request_queue, 0x5275e) */
  gps_argument_mode argument_mode; /* +0x0c: gps_request_class's second
                                      argument, copied to gps_request_step's */
  uint8_t field_0d;
  int32_t* arguments; /* +0x0e: four longs, which gpsm_task copies into
                         gps_request_step's */
  uint8_t sent; /* +0x12: gps_request_post's last argument, 1 for the status
                   monitor's GPS_SCRIPT_OUTPUTS_ON; a
                   newest waiting request with it set is not replaced */
  uint8_t field_13[7];
} gps_request_entry;

/* gpsm_task's ring of waiting GPS requests: six entries, as the 58503A
 * 3704's (request_ring_put's bound, +0x9c, at 0x52496), the oldest at
 * read, the next free at write; empty when the two are equal, so it
 * holds five. */
typedef struct gps_request_ring {
  gps_request_entry entries[6];
  gps_request_entry* read;  /* +0x9c */
  gps_request_entry* write; /* +0xa0 */
} gps_request_ring;

/* The GPS command gpsm_task is running, on its stack (gpsm_task's
 * command local), the 58503A 3704's: the request it took from
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

/* A GPS Receiver Command Log entry: what command_log_add takes of a
 * request gpsm_task has sent, 0x14 bytes, its stride (0x532f6), and
 * gps_print_command prints.  The 58503A 3704's but for the sender and
 * the code, which change places: command_log_add stores the sender at
 * +0x00 and the code at +0x02 (0x53270, 0x53276), and gps_print_command
 * switches on the byte at +0x00 (0x533a6) and prints the word at +0x02
 * under "ID" (0x53444), where the 58503A's code is at +0x00 and its
 * sender at +0x02 (0x4d178, 0x4d17e). */
typedef struct gps_command_log_entry {
  gps_request_sender sender;  /* +0x00: the request's; printed PLL, SCPI1,
                                 GPSc and GPSs, else "?" */
  uint8_t field_01;           /* never written */
  gps_request_code_word code; /* +0x02: the request's +0xa, printed under
                                 "ID" */
  gps_request_code_word
      message;             /* +0x04: the request's +0x1e (0x5327c), its
                              index in the message table at
                              oncore_messages, whose ID prints (0x53432) */
  char argument_text[3];   /* +0x06: characters 4 and 5 of
                              gps_message_to_text's text, the first byte
                              after the ID in hex; NUL */
  gps_argument_mode query; /* +0x09: the command's; printed q, else c */
  uint8_t from_clock; /* +0x0a: 1 when the clock is set, the date and time its;
                         0 for a clock not set since power-up (get_date_time's
                         1), the date and time @@Ba's.  Printed blank, else e */
  uint8_t field_0b;   /* never written */
  uint32_t date; /* +0x0c: the clock's while from_clock, else @@Ba's, local */
  time_of_day
      time; /* +0x10: likewise; gps_print_command passes it to format_time */
} gps_command_log_entry;

/* The GPS Receiver Command Log, the 58503A 3704's, read and write at
 * +0x1a4 and +0x1a8 (command_log_add, 0x532f8 to 0x53336): a ring at
 * command_log, and its copy at
 * command_log_snapshot, which status_snapshot_update takes with the snapshot at
 * status_snapshot_flagged: 21 entries, the oldest at read, the next free at
 * write; empty when the two are equal, and full at 20, when command_log_add
 * drops the oldest.  gps_print_command takes the oldest. */
typedef struct gps_command_log {
  gps_command_log_entry entries[21];
  gps_command_log_entry* read;  /* +0x1a4 */
  gps_command_log_entry* write; /* +0x1a8 */
} gps_command_log;

#include "../../common/types/packed_datetime.h"

#include "../../common/types/lifetime_state.h"

/* The EEPROM's records ("EEPROM records" in structures.md), each
 * checksummed by record_checksum in its first two bytes, as their
 * writers build them on their stacks: erased to 0xff, then filled. */

#include "../../common/types/parameter_record.h"

#include "../../common/types/identification_record.h"

/* What adc_averages_print prints through: :DIAGnostic:ADC? passes
 * printf whatever the source (0x3bb18), where the Z3815A 4010's passes
 * duartb_printf for source 0. */
typedef void adc_average_printer(char* format, ...);

/* The oscillator module's link, puck at 0x1021de: the Z3815A 4010's
 * puck_link, the module's frames read from DUART channel B's receive
 * ring by puck_frame_receive (0x3374c), each a 0x55 and five bytes, the
 * last the sum of the five before it, and the EFC ADC's correction
 * efc_adc_correct (0x3355e) makes from a good one.  Whether a frame came
 * lately is puck_alive and puck_alive_until, outside the record. */
typedef struct puck_link {
  int32_t efc_adc_expected;   /* +0x00: the reading the ADC should give,
                                 efc_set's (0x33846) */
  float correction;           /* +0x04: added to pll.efc while corrections
                                 are on, "corr= %f" */
  float efc_written;          /* +0x08: pll.efc, plus correction while
                                 corrections are on */
  uint8_t correction_restart; /* +0x0c: corrections are on */
  uint8_t field_0d;           /* +0x0d: pps_monitors_reset sets it */
  uint32_t frames_since_efc;  /* +0x0e: frames since the EFC was set; the
                                 correction waits for 5 */
  float error_sum;            /* +0x12: the readings' differences summed; the
                                 correction is a thousandth of it */
  uint8_t frame[6];           /* +0x16: a frame as read, 0x55 first, the
                                 ADC's 24 bits high first from +1, a status
                                 byte at +4, bit 0 puck_warm's, and the sum */
  uint8_t framing;            /* +0x1c: a 0x55 has come, the frame's bytes are
                                 stored */
  uint8_t field_1d;           /* nothing refers to it */
  uint8_t* cursor;            /* +0x1e: where the frame's next byte goes */
} puck_link;

/* The calibration block at 0x400080: save_calibration_block's
 * TC, time offset and cable delay, the 58503A 3704's, and this image's
 * EFC calibration, second TC coefficient and oscillator's kind after them
 * (0x41756 to 0x417a4; calibration_block_read, 0x41938 to 0x4196a). */
typedef struct calibration_block {
  uint16_t checksum;    /* +0x00 */
  float tc_coefficient; /* +0x02: scpi_value_DIAGnostic_ROSCillator_TCOefficient
                         */
  float time_offset;    /* +0x06: scpi_value_DIAGnostic_TOFFset */
  int16_t field_0a;     /* +0x0a: pps_cable_delay_offset, cut to a word */
  float efc_dac_per_adc; /* +0x0c:
                          * scpi_value_DIAGnostic_ROSCillator_EFControl_DADC
                          */
  float efc_a; /* +0x10: scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe, 0
                  while efc_calibration_given is clear */
  float efc_b; /* +0x14: scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe */
  float tc_coefficient_1; /* +0x18: :DIAGnostic:ROSCillator:TCO's second,
                             tc_coefficient_1 (0x41766), 0 while erased
                             (0x4194a) */
  uint8_t oscillator;     /* +0x1c: the oscillator byte (0x10222e), which picks
                             the loop's time constant and gain; 0x418ec loads it
                             back (docs/firmware/z3817a-3752.md) */
  uint8_t unused[35];     /* +0x1d: left erased */
} calibration_block;

/* An NV register record, one of nv_records' two at 0x4000c0 and
 * 0x400160, as nv_save (0x43734) builds it (structures.md, "The Z3817A
 * 3752's record"): 0xa0 bytes, nv_save's frame (link -160) and the
 * slots' stride (nv_slot_valid, 0x43976 to 0x43980), its checksum
 * nv_checksum's (0x436da) over bytes 8 to 0x9f. */
typedef struct nv_record {
  char tag[2];         /* +0x00: "M1" (nv_tag_text, 0x43e7d) */
  uint16_t checksum;   /* +0x02 */
  int32_t sequence;    /* +0x04: one past the stored record's */
  char revision[8];    /* +0x08: revision_text's, "3752" and NULs */
  uint32_t packed[36]; /* +0x10: the settings packed into longs, then 0xff */
} nv_record;

#include "../../common/types/reset_record.h"

#include "../../common/types/sprintf.h"

/* What doprnt writes its output through, as fwrite is called: the
 * text, the size of an element, the count, and the output; sprintf's
 * is sprintf_put (0x68a5c).  The count written comes back. */
typedef int32_t sprintf_put_function(char* text, int32_t size, int32_t count,
                                     sprintf_output* output);

#include "../../common/types/scpi_decimal.h"

#include "../../common/types/scpi_error_message.h"

#include "../../common/types/scpi.h"

/* The two command trees sci_scpi_task gives scpi_task_run, a pair in ROM
 * at 0x651ac (0x39d52), and the installer's at installer::0x1089d4, which
 * installer_scpi_task gives installer_scpi_task_run. */
typedef struct scpi_tree_pair {
  scpi_child_list* common; /* +0x00: the common commands' list, *IDN? on */
  scpi_child_list* main;   /* +0x04: the main tree's root list */
} scpi_tree_pair;

/* scpi_task_run's routines for its two messages, which sci_scpi_task and
 * installer_scpi_task pass: console_write's shape, for "scpi: error
 * opening stream\n" (0x39cb6), and printf's, for "scpi: alloc_seg error
 * %#0x\n" (0x39c84). */
typedef unsigned long scpi_task_write_function(char* text);
typedef int scpi_task_print_function(char* format, ...);

/* :SYSTem:LANGuage's choice of what follows the SCPI session
 * (scpi_language), which scpi_task_run acts on (0x39cf6 to 0x39d24). */
typedef enum __attribute__((packed)) scpi_language_choice {
  SCPI_LANGUAGE_PFORTH = 0,  /* the pForth console */
  SCPI_LANGUAGE_INSTALL = 1, /* the installer */
} scpi_language_choice;

#include "../../common/types/scpi_call.h"

#include "../../common/types/scpi_staged_values.h"

#include "../../common/types/scpi_context.h"

#include "../../common/types/scpi_array.h"

#include "../../common/types/status_register.h"

/* One of port_error_reports' four entries, which sci_scpi_session_init
 * fills (0x39686 to 0x396ae) and port_report_errors reads: receive status
 * bits, the SCI's SCSR's, and the condition reported for them, FE 0x15,
 * OR 0x16, PF 0x14 and IDLE 0x13, where the DUART sessions of the other
 * images take 0xc0, 0x10, 0x20 and 0x01; the installer's likewise. */
typedef struct port_error_report {
  uint8_t mask;      /* +0x00: SCSR's status bits */
  uint8_t field_01;  /* unread */
  int16_t condition; /* +0x02: a status_condition_code, port_status_report's */
} port_error_report;

/* The installer's status conditions, installer_status_conditions'
 * (installer::0x107a26) and installer_status_report_messages' indexes
 * (installer::0x107a44): none, then its four errors, -360 to -363,
 * which installer_port_report_errors (installer::0x107502) reports for
 * installer_console_port_read, where the primary's are
 * STATUS_COMMUNICATION_ERROR to STATUS_INPUT_OVERRUN.  It should type
 * installer_status_report's code. */
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
 * status_report and the SCPI engine raise.  From 0, an index of
 * status_conditions, the 58503A 3704's but for 0x56 to 0x59 (0x4030e,
 * 0x60 entries): 1 to 0x16
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
 * raises n + 0x16, n up to 73 (0x3c490).  Below 0, minus an index of
 * status_negative_conditions (0x38240), what the SCPI engine's routines
 * return: -1 to -4 set Standard Event bits 5 to 2, the error classes
 * error_queue_push reports, and the rest queue their SCPI error.  0x17,
 * 0x18, 0x30 to 0x37, 0x42 to 0x45, 0x52 to 0x55, 0x5d to 0x5f, -29 and
 * -58 to -62 do nothing, as 0 does, and but for 0x35, which code raises,
 * have no name; nor have -25 and -26, -171, and -57, -224, which no code
 * shows the cause of, nor 0x56 to 0x59, which set and clear :HARDware
 * bits 13 and 14 here, hmon_channels' 12B and -12B (adc_channel). */
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
                                 STATUS_HOLDOVER_THRESHOLD_CLEARED (0x4ca8a),
                                 the Z3805A 3543B's number for
                                 STATUS_SELFTEST_FAILURE_CLEARED */
  STATUS_CONDITION_36 = 0x36, /* does nothing; status_happening clears it
                                 entering holdover, waiting or recovery
                                 (0x40092) */
  STATUS_CONDITION_37 = 0x37, /* does nothing; status_happening clears it
                                 entering lock (0x4006a) */
  STATUS_SELFTEST_FAILURE_SET = 0x38, /* :HARDware bit 0, selftest_summary's
                                         (0x27870) */
  STATUS_SELFTEST_FAILURE_CLEARED = 0x39,
  STATUS_PLUS_15V_SET = 0x3a, /* :HARDware bit 1, +15V supply exceeds
                                 tolerance; hmon_channels' 15V */
  STATUS_PLUS_15V_CLEARED = 0x3b,
  STATUS_MINUS_15V_SET = 0x3c, /* :HARDware bit 2, -15V; hmon_channels' */
  STATUS_MINUS_15V_CLEARED = 0x3d,
  STATUS_PLUS_5V_SET = 0x3e, /* :HARDware bit 3, +5V; hmon_channels' */
  STATUS_PLUS_5V_CLEARED = 0x3f,
  STATUS_OVEN_SUPPLY_SET = 0x40, /* :HARDware bit 4, oven supply exceeds
                                    tolerance; nothing here raises it, no
                                    hmon_channels entry being the oven's */
  STATUS_OVEN_SUPPLY_CLEARED = 0x41,
  STATUS_EFC_NEAR_LIMIT_SET = 0x46, /* :HARDware bit 6, EFC voltage near
                                       full-scale; hmon_check_efc's (0x339d0) */
  STATUS_EFC_NEAR_LIMIT_CLEARED = 0x47,
  STATUS_EFC_AT_LIMIT_SET = 0x48, /* :HARDware bit 7, efc_clamp's (0x338e0) */
  STATUS_EFC_AT_LIMIT_CLEARED = 0x49,
  STATUS_GPS_PPS_FAILURE_SET = 0x4a, /* :HARDware bit 8, GPS 1 PPS failure;
                                        hmon_task's (0x34436) */
  STATUS_GPS_PPS_FAILURE_CLEARED = 0x4b,
  STATUS_GPS_FAILURE_SET = 0x4c,        /* :HARDware bit 9 */
  STATUS_GPS_FAILURE_CLEARED = 0x4d,    /* command_monitor_init's (0x51d8e) */
  STATUS_TIME_INTERVAL_ERROR = 0x4e,    /* :HARDware bit 10, an event only
                                           (0x4a3a6) */
  STATUS_EEPROM_WRITE_FAILURE = 0x4f,   /* :HARDware bit 11, an event only
                                           (eeprom_write, 0x41f3c) */
  STATUS_INTERNAL_REFERENCE_SET = 0x50, /* :HARDware bit 12, internal reference
                                           failure; hmon_task's (0x34484) */
  STATUS_INTERNAL_REFERENCE_CLEARED = 0x51,
  STATUS_TIME_SET = 0x5a, /* :QUEStionable bit 0, time reset, an event only
                             (clock_check_against_gps, 0x490fe) */
  STATUS_USER_REPORTED_SET = 0x5b, /* :QUEStionable bit 1
                                      (questionable_user_done, 0x4860a) */
  STATUS_USER_REPORTED_CLEARED = 0x5c,
} status_condition_code;

/* The health monitor's channel table, the 58503A 3704's layout,
 * hmon_channels (0x32498), by ADC channel, 0 to 7: what reads, averages
 * and checks each.  hmon_channel_read converts adc_read's reading
 * through convert (0x31f90); hmon_channel_step (0x3239a), the monitor's
 * step, calls add, and while check is null or says so, report;
 * adc_average (0x32436) returns nominal while the channel's adc_monitor
 * is not valid. */
typedef float hmon_convert_function(uint8_t reading);
typedef void hmon_channel_function(adc_channel channel);
typedef uint8_t hmon_channel_test(adc_channel channel);
typedef struct hmon_channel_entry {
  hmon_convert_function* convert; /* +0x00: null for an unused channel */
  hmon_channel_function* add;     /* +0x04: monitor_average_add in each entry
                                     here */
  hmon_channel_test* check;      /* +0x08: whether to report; null for always */
  int32_t field_0c;              /* 0 in every entry; hmon_channel_step does
                                    not read it */
  hmon_channel_function* report; /* +0x10: the range check and its report,
                                    hmon_report_range (0x32326) in each entry
                                    here */
  float minimum;                 /* +0x14: the tolerance's
                                    (hmon_channel_out_of_range, 0x322c6) */
  float maximum;                 /* +0x18: (0x322ec) */
  float nominal;                 /* +0x1c: adc_average's default */
  char* name;                    /* +0x20: "5V", "15V", ... */
  status_condition_code events[2]; /* +0x24: the happenings out of tolerance
                                      and back within it, 0x16 for none */
  uint16_t bit;                    /* +0x26: supplies_test's, 1, 2, 4, 0x20
                                      and 0x40 (0x28616) */
  uint8_t field_28;
  uint8_t field_29;
} hmon_channel_entry;

#include "../../common/types/status_byte_bit.h"

#include "../../common/types/status_error_entry.h"

#include "../../common/types/status_register_layout.h"

/* One register's entry in the installer's status_register_layouts
 * (installer::0x107a12), two of them: status_register_layout's first ten
 * bytes, without the nv_settings words, which the installer has none of.
 * installer_status_init sets register 1 up from its entry, its positive
 * filter its defined bits (installer::0x10798e to installer::0x1079b0). */
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
  uint16_t correction_age; /* 0.1 s, of differential corrections */
  int32_t ecef_x;          /* cm */
  int32_t ecef_y;
  int32_t ecef_z;
  uint8_t ura[8]; /* per channel: @@Ek's eight; @@Bk's decoder zeroes the
                     last two (oncore_extension_field_codes, 0x58a68) */
  int16_t least_squares_inverse[10]; /* A-1, upper triangle by rows */
  int16_t clock_bias;                /* 0.1 m */
  uint16_t oscillator_offset;        /* 0.1 m/s */
} oncore_extension_fields;

/* The EFC history: a ring of 32 averages of the EFC, one each 2700
 * samples, which the holdover prediction draws on.  Its routine's debug
 * message names its arrays: "e_avg= time= weight= log= tfom=".  This
 * image's own, 0x34e bytes, each field where efc_history_update
 * (0x46422) and the fits reach it (make record-access): the ring is 32
 * entries, where the 58503A 3704's is 64; the entry a new one replaces
 * is kept after each array but the logs' (0x46544 to 0x46560), and each
 * entry's fit is kept in three arrays more after the logs
 * (measured_uncertainty_update, 0x4603c to 0x46054).  No sample count:
 * the uptime times the entries (next_entry_uptime). */
typedef struct efc_ring {
  uint8_t start_ptr;       /* +0x000: the oldest entry */
  uint8_t stop_ptr;        /* +0x001: the next to fill */
  uint8_t loop_settled;    /* +0x002: startup_pll sets it on settling without a
                              fit; aging_state then goes to 3 */
  uint8_t field_003[3];    /* nothing refers to it */
  uint16_t summed_count;   /* +0x006: samples summed toward this entry */
  float averages[32];      /* +0x008: e_avg, efc_sum / summed_count plus
                              efc_origin */
  float replaced_average;  /* +0x088: averages[stop_ptr] before the entry is
                              filled again; nothing reads it */
  uint8_t weights[32];     /* +0x08c: weight, 1 to 3 */
  uint8_t replaced_weight; /* +0x0ac: weights[stop_ptr], likewise */
  uint8_t field_0ad;       /* nothing refers to it */
  float times[32];         /* +0x0ae: time */
  float replaced_time;     /* +0x12e: times[stop_ptr], likewise */
  float logs[32];          /* +0x132: log */
  float fit_a_history[32]; /* +0x1b2: aging_curve.a at aging_curve.newest_index,
                              by measured_uncertainty_update */
  float fit_b_history[32]; /* +0x232: aging_curve.b */
  float fit_c_history[32]; /* +0x2b2: aging_curve.c */
  uint32_t next_entry_uptime; /* +0x332: the uptime the next entry is due at,
                                 2700 on each (0x46484, 0x4652c) */
  float efc_sum;     /* +0x336: the EFC, less its temperature correction and
                        efc_origin, summed */
  float efc_origin;  /* +0x33a: the entry's first sample, the EFC less its
                        temperature correction, which each sample is summed
                        less (0x464c4, 0x464de); holdover reads it with the sum
                        (0x48be0) */
  int32_t field_33e; /* nothing refers to it */
  float fit_a;       /* +0x342: the curv task's aging fit a + b y + c ln y, y =
                        samples / 2700 */
  float fit_b;       /* +0x346 */
  float fit_c;       /* +0x34a */
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

/* @@Ba's and @@Ea's record, the 58503A 3704's, a pad byte before the
 * hour (time_of_day's own) putting the longs and words on even addresses,
 * and another before the DOP type, pad_22, which the decoders' field
 * tables skip (oncore_status_field_codes, 0x58a4c).  The DOP type is the
 * byte after the message's two-byte DOP, bit 0 set for HDOP and clear
 * for PDOP, bits 7 to 5 the antenna's under- and overcurrent and the
 * survey (TRM0003, Oncore User's Guide revision 3.2, pp. 6.19-6.20). */
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

/* @@Bn and @@En, the Time RAIM setup and status messages, as their
 * decoders' tables lay them out (0x58a6f, 0x58a83): the 58503A 3704's
 * fields, but that the alarm limit is a long, two zero bytes before the
 * message's word, which the decoders multiply by 10 (0x56c2c), so that
 * from pps_mode on each field is a byte further in; the 1PPS rate a
 * long after a pad, its first byte zero; @@Bn's decoder zeroing the
 * last two channels, @@En's filling all eight. */
typedef struct oncore_raim_fields {
  uint8_t rate;             /* +0x00: s between messages */
  uint8_t raim_on;          /* +0x01 */
  int32_t alarm_limit;      /* +0x02: 10 ns once decoded */
  oncore_pps_mode pps_mode; /* +0x06 */
  uint8_t pad_07;
  int32_t pps_rate;   /* +0x08 */
  uint8_t next_month; /* +0x0c: the time to the next pulse */
  uint8_t next_day;
  uint16_t next_year;
  uint8_t pad_10;
  uint8_t next_hour; /* +0x11 */
  uint8_t next_minute;
  uint8_t next_second;
  uint8_t pulse_status;                 /* +0x14 */
  oncore_pulse_sync pulse_sync;         /* +0x15 */
  oncore_raim_solution solution_status; /* +0x16 */
  oncore_raim_status raim_status;       /* +0x17 */
  uint16_t sigma;                       /* +0x18: ns */
  int8_t sawtooth;                      /* +0x1a: ns, of the next pulse */
  uint8_t pad_1b;
  oncore_raim_channel channels[8]; /* +0x1c */
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

/* A line of the receiver ID by the index oncore_receiver_id_lines
 * (0x58aae) gives it, in @@Cj's order, named by the label each begins
 * with; 9 ends the table, and gps_receiver_abilities_update (0x56e2a)
 * reads the options', the software version's and its revision's. */
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

/* A line of the receiver ID as oncore_receiver_id_value (0x56dc6) finds
 * it by its index: its text in gps.receiver_id, the label it begins with
 * and the label's length; oncore_receiver_id_value returns the text after
 * the label, or 0 for a line that does not begin with it, and
 * gps_receiver_abilities_update (0x56e2a) reads the options' (3), the
 * software version's (6) and revision's (7).  The table,
 * oncore_receiver_id_lines (0x58aae), is in @@Cj's order, from the model
 * number, and ends at an index of 9. */
typedef struct oncore_receiver_id_line {
  oncore_receiver_id_field index; /* +0x00 */
  uint8_t field_01;               /* unread */
  char* text;                     /* +0x02: gps.receiver_id's line */
  char* label;                    /* +0x06: "MODEL #", ... */
  uint8_t label_length;           /* +0x0a */
  uint8_t field_0b;               /* unread */
} oncore_receiver_id_line;

/* A reference's validity, gps_reference_record.valid, from its state:
 * :SOURce:GPSystem:REFerence:VALid? answers 1 for valid with its 1PPS
 * alive, or questionable (0x3dea8). */
typedef enum __attribute__((packed)) gps_reference_validity {
  GPS_REFERENCE_INVALID = 0,
  GPS_REFERENCE_VALID = 1,        /* GPS_SYNC_VALID, or forced valid */
  GPS_REFERENCE_QUESTIONABLE = 2, /* GPS_SYNC_QUESTIONABLE and _CLEARING */
} gps_reference_validity;

/* One reference's validation, the GPS's or the external 1PPS's, as the
 * 58503A 3704's: gps_receiver_status_update's (0x51946) state for each,
 * gps.references[index]; gps_reference_reset (0x518ce) sets valid from
 * override and state to 1, gps_reference_hysteresis_set and
 * ext_reference_hysteresis_set (0x518fc, 0x51926) the GPS's and the
 * external's hysteresis.  22 bytes (0x1013b0 and 0x1013c6, 0x518d4). */
typedef struct gps_reference_record {
  gps_reference_validity valid;    /* 1 at state 9, 2 at 7 and 8, else 0; while
                                      override is set, 1 for 1 and 0 for any
                                      other (0x51cf6) */
  gps_sync_status state;           /* its jump tables' (0x51adc, 0x51c42) */
  gps_reference_override override; /* 1 forces valid, 0xff invalid; gps_init
                                      clears it, diag_force_gps_1pps and
                                      diag_force_ext_1pps set it */
  uint8_t pps_dead;           /* set each pass: in this image the GPS's from the
                                 hardware condition's bit 8 while fixing (0x51a60),
                                 the external's gps_pps_dead (0x51a82) */
  uint32_t elapsed;           /* uptime_elapsed (0x51766) counts it from
                                 validation_start */
  uint32_t validation_start;  /* uptime as state reached 6 or 7 (0x51afa,
                                 0x51bba) */
  uint32_t field_0c;          /* elapsed as state reached 8 (0x51b22) */
  uint16_t validation_time;   /* 35 for the GPS, 1 for the external */
  uint16_t hysteresis;        /* nv_settings' (0x518fc, 0x51926) */
  uint16_t hysteresis_margin; /* hysteresis less 1, or 0, 1 (0x5190e) */
} gps_reference_record;

/* gps.field_592's bits in this image, by what sets and tests them:
 * gps_receiver_abilities_update (0x56e2a), from the receiver ID, sets
 * the time RAIM's and 0x08 for an engine whose model number starts with
 * an R, and for any other 0x40, the time RAIM's for an I in its options
 * and the 8.4's for its software 8.4, the 58503A's tests; without the ID
 * it clears all four, and it sets the continuous 1PPS's each time.  @@Bn
 * and @@En set the continuous 1PPS's for a 1PPS mode of 1, on, and clear
 * it for any other (0x56c52, 0x56cc4).  An @@Ea sets the eight channels'
 * (oncore_position_status_8_message, 0x569de), the 58503A's 0x20, and
 * an @@Ba clears it.  The time RAIM's is the 58503A's I, which
 * oncore_filter_traim (0x56f98), gps_message_supported and
 * gps_print_time_raim test; the 8.4's passes the second @@Ar's filter
 * (0x56fbe); 0x40 makes a height the MSL's (0x4f2a0, 0x5667e). */
typedef enum __attribute__((packed)) gps_field_592_bit {
  GPS_FIELD_592_SOFTWARE_8_4 = 0x04,
  GPS_FIELD_592_MODEL_R = 0x08, /* the model number's first letter an R */
  /* While set, the reference pulse's monitor judges the pulse by its
   * count alone: a GPS sync state other than valid, questionable
   * clearing or assessing does not mark it dead (0x33cbc, 0x33df8), and
   * a good short gate while the GPS record is questionable does not bring
   * it back before the long gate (0x33ef6, 0x33f76).  Inferred: an engine
   * whose 1PPS runs continuously, whatever its sync state; why the
   * abilities update sets it for any engine is not established. */
  GPS_FIELD_592_PPS_CONTINUOUS = 0x10,
  GPS_FIELD_592_TIME_RAIM = 0x20,
  GPS_FIELD_592_BIT_6 = 0x40,
  GPS_FIELD_592_EIGHT_CHANNELS = 0x80,
} gps_field_592_bit;

/* The Z3815A 4010's GT-74 sentences, each as its decoders' table fills
 * it in its gps, field by field: each field's decoder writes the place
 * its entry names, and the commit at the table's end, where one is,
 * copies them into the 58503A's records (its gps_nmea.c).  Named by what
 * the commits make of them; the field's number in the sentence, counting
 * its name as 0, after each.  This image frames Oncore binary, and
 * nothing here uses these types. */

/* gps.status_received: whether the receiver's status, @@Ba or @@Ea, has
 * come, and lately, as the status monitor keeps it (command_monitor,
 * 0x52070 and 0x5208a), the 58503A 3704's; channel_position_satellite
 * takes a channel's satellite only while it is fresh (0x56392). */
typedef enum __attribute__((packed)) gps_status_freshness {
  GPS_STATUS_NONE = 0,  /* none since the monitor last failed */
  GPS_STATUS_STALE = 1, /* none for three of its periods */
  GPS_STATUS_FRESH = 2,
} gps_status_freshness;

/* A setting message the GPS task notes hearing, gps.echoes' bit and
 * gps.echo_ages' index, by gps_echo_entries (0x58b9a): gps_echo_seen
 * answers whether it was heard since gps_echoes_reset, which marks the
 * position hold heard.  The status screen shows the leap second and the
 * mask angle only once heard (0x513a6, 0x515ea), and the GPS record
 * wants the position hold (0x519da). */
typedef enum __attribute__((packed)) gps_echo {
  GPS_ECHO_FIX_MODE = 0,            /* @@Cg */
  GPS_ECHO_POSITION_HOLD = 1,       /* @@At */
  GPS_ECHO_UTC_SELECT = 2,          /* @@Aw */
  GPS_ECHO_LEAP_SECOND_PENDING = 3, /* @@Bj */
  GPS_ECHO_CABLE_DELAY = 4,         /* @@Az */
  GPS_ECHO_MASK_ANGLE = 5,          /* @@Ag */
} gps_echo;

/* An entry of gps_echo_entries (0x58b9a, six and an end), which
 * gps_echo_mark walks for a message heard: the message's request code,
 * GPS_REQUEST_NONE's 0xff ending the table, and its bit. */
typedef struct gps_echo_entry {
  gps_request_code_word message;
  gps_echo echo;
  uint8_t field_3;
} gps_echo_entry;

/* The GPS task's state: what its decoders store of each Oncore message,
 * in the order of the message table (0x575d8), and its own state after.
 * Copied to and from preserved_region as 0x170 longs (system_init,
 * 0x22df2), 0x5c0 bytes.  Its records lie where the message table points
 * the decoders (docs/firmware/oncore/z3817a-3752.txt) and each field
 * where the image's code reaches it (make record-access
 * IMAGE=z3817a-3752 RECORD='0x100e66 0x5c0 --fit gps_state'): the 58503A
 * 3704's through the visibility, then 4 bytes further in to @@Bn's,
 * whose fields are its own, and 6 bytes further in after them, with
 * fields of its own among the monitors' (docs/firmware/z3817a-3752.md,
 * "Types"). */
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
  int32_t pps_cable_delay;       /* @@Az, less pps_cable_delay_offset
                                    (0x10221e; oncore_cable_delay_decode,
                                    0x5612c) */
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
  uint8_t field_198[5];
  oncore_xdop_table_fields xdop_table; /* @@Bc, at +0x19d, which
                                          gps_print_best_combinations reads */
  uint8_t field_1e8[2];
  oncore_almanac_status_fields almanac; /* @@Bd */
  uint8_t field_1fa[0x2b8 - 0x1fa];
  oncore_corrections_fields corrections; /* @@Ce */
  double correction_average;             /* m, printed AVG */
  double correction_deviation;           /* m, printed STD DEV */
  uint8_t field_308[2];
  oncore_raim_fields raim; /* @@Bn, @@En (0x56c04, 0x56c76) */
  uint8_t raim_received;   /* set by @@Bn's and @@En's decoders; raim_monitor
                              and command_monitor read it */
  uint8_t field_357;
  oncore_leap_pending leap_second_pending; /* @@Bj */
  uint8_t field_359;
  uint16_t self_test; /* @@Ca, @@Fa, failed tests */
  uint8_t field_35c[0x362 - 0x35c];
  oncore_extension_fields extension; /* @@Bk, @@Ek */
  uint8_t field_3a2[0x40c - 0x3a2];
  oncore_fix_mode fix_mode; /* @@Cg */
  uint8_t field_40d;
  uint8_t receiver_id_received;   /* set by @@Cj's decoder */
  oncore_receiver_id receiver_id; /* @@Cj */
  uint8_t power_on_failure;       /* @@Sz's decoder stores 1 (0x56ce8);
                                     nothing reads it */
  uint8_t field_525;              /* gps_init clears it */
  uint32_t average_count;         /* positions averaged */
  double average_latitude;        /* of @@Ba's, while surveying */
  double average_longitude;
  double average_height_gps;
  double average_height_msl;
  gps_reference_record references[2]; /* the GPS's, then the external
                                         1PPS's */
  uint8_t surveying;                  /* averaging @@Ba's position */
  uint8_t field_577; /* gps_receiver_status_update and status_snapshot */
  uint8_t field_578; /* gps_init sets it; gps_receiver_status_update reads it */
  gps_raim_assessment
      raim_assessment;        /* 0 inactive, 1 OK, 2 alarm detected, 3 alarm */
  uint8_t pps_check_disabled; /* gps_pps_check skips while set */
  uint8_t receiver_health_failed; /* "GPS Receiver Health" */
  uint8_t command_monitor_enabled;
  gps_command_monitor_state command_monitor_state;
  gps_script_step** command_monitor_query; /* its next query, a place in
                                              command_monitor_queries */
  uint32_t command_monitor_time;           /* uptime of its last state change */
  uint8_t command_monitor_error; /* set by gpsm_task when a command goes
                                    unanswered */
  uint8_t command_monitor_failed;
  uint8_t status_monitor_enabled;
  gps_status_monitor_state status_monitor_state; /* gps_status_monitor_state */
  uint32_t status_monitor_time;  /* uptime of the last @@Ba or @@Ea, which
                                    oncore_position_message stamps (0x56984);
                                    0 for none since the uptime went back */
  uint32_t raim_time;            /* uptime of the last @@Bn or @@En, which their
                                    decoders store (0x56c62, 0x56cd4); command_monitor's
                                    status monitor wants one within 36 s while the time
                                    RAIM is expected (0x51ffc) */
  uint32_t status_monitor_start; /* uptime command_monitor's status monitor
                                    last started or restarted from */
  uint32_t status_last_time;     /* +0x596: the last @@Ba's time bytes, compared
                                    as one with the next's (0x5693a), as the
                                    58503A 3704's */
  uint8_t status_repeat_count;   /* @@Ba with the same time in a row */
  uint8_t status_monitor_failed;
  uint32_t echoes_aged;  /* uptime gps_echoes_age last counted from */
  uint16_t echoes;       /* the setting messages heard since gps_echoes_reset,
                            a bit 1 << gps_echo for each (gps_echo_mark) */
  uint8_t echo_ages[6];  /* by gps_echo: s since each was heard, to 0xfe,
                            0xff for not since the reset; nothing reads them */
  uint8_t channel_count; /* the engine's channels, 6 or 8; the channel loops'
                            bound */
  uint8_t field_5a9;
  gps_field_592_bit field_592; /* set by @@Ea's decoders and from @@Cj
                                  (gps_field_592_bit) */
  uint8_t field_663;
  uint8_t tracking_log; /* nv_settings.tracking_log, copied by gps_init */
  uint8_t tracking_log_active;  /* tracking_log, as the receiver state
                                   update takes it */
  uint32_t track_words[3];      /* the tracking log's channels, packed by
                                   gps_track_words_pack */
  uint32_t channel_word;        /* each channel's mode and signal strength,
                                   packed when a track is lost
                                   (gps_channel_word_pack) */
  uint16_t channel_word_lowest; /* each channel's lowest mode since its
                                   track was lost (gps_channel_word_lowest) */
} gps_state;

/* An entry of fpga_bit_table (0x45d7a, five entries), which fpga_set_bit
 * and fpga_set_field take: a register of fconf_regs and its mask.  This
 * image's own, read from the table, none of the other images' shared
 * prefix: its FPGA has no mode field and
 * keeps no start or reference bit, and has a fifth register, 4, the
 * start's select (docs/firmware/z3817a-trace.md, "Register port";
 * z3817a-3752.md, "P84, register 4 and the status bits"). */
typedef enum __attribute__((packed)) fpga_bit_index {
  /* register 0, 0x40, a flag: the preset, which resets the count, its
   * start and its stop; counter_selftest reads a count of 0 under it */
  FPGA_BIT_PRESET = 0,
  /* register 1, 0xf8, a field: the read address fpga_read writes
   * (0x451ec) */
  FPGA_BIT_ADDRESS = 1,
  /* register 3, 0x03, a field: counter_set_sync's sync kind (0x4558c) */
  FPGA_BIT_SYNC = 2,
  /* register 4, 0x01, a flag: the start's select, P84 set and P82 clear,
   * which only 0x4538c sets, to its argument, its callers' 0 (0x4cacc,
   * 0x4ccd8) */
  FPGA_BIT_START_SELECT = 3,
  /* register 1, 0x01, a flag: the others' start bit, which this FPGA does
   * not keep; nothing sets it, and 0x4539e reads its register byte as a
   * read address (0x45d8a) */
  FPGA_BIT_START = 4,
} fpga_bit_index;

#include "../../common/types/fpga_mode.h"

#include "../../common/types/interval_arm_mode.h"

/* An FPGA configuration, a bitstream fpga_program loads and
 * fpga_verify checks, by its index in fpga_configurations.  The table
 * holds one, the first bitstream, the time-interval counter's design
 * (outputs.md, "The FPGA bitstreams"). */
typedef enum __attribute__((packed)) fpga_configuration_index {
  FPGA_CONFIGURATION_COUNTER = 0,
  FPGA_CONFIGURATION_LAST = 1, /* fpga_configurations.last (0x30ef8), past
                                  the one configuration, which pll_init
                                  copies to pll.fpga_configuration
                                  (0x4ca04) */
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

/* pll_normal's states, pll.normal_state; any other starts it again. */
typedef enum __attribute__((packed)) pll_normal_state {
  PLL_NORMAL_RUNNING = 1,
} pll_normal_state;

/* The OCXO calibration's steps, efc_calibration.step, as
 * efc_calibration_measure's switch takes them (0x4daaa); any other starts
 * it again. */
typedef enum __attribute__((packed)) efc_calibration_step {
  EFC_CALIBRATION_START = 0,
  EFC_CALIBRATION_WAIT_REFERENCE = 1,
  EFC_CALIBRATION_SETTLE = 2,
  EFC_CALIBRATION_FIRST_READING = 3,
  EFC_CALIBRATION_SECOND_READING = 4,
  EFC_CALIBRATION_POINT = 5,
  EFC_CALIBRATION_ADC_LOW = 6,
  EFC_CALIBRATION_ADC_HIGH = 7,
  EFC_CALIBRATION_CHECK = 8,
} efc_calibration_step;

/* The OCXO calibration's measurement's state, efc_calibration at
 * 0x102b02, up to the kernel's I/O table (make record-access;
 * efc_calibration_measure's and efc_calibration_fit's, which index
 * frequency_offset by 4 from 0x102b06, 0x4da6c, 0x4d7ba). */
typedef struct efc_calibration_fields {
  efc_calibration_step step;  /* +0x00 */
  uint8_t point;              /* +0x01: the EFC's point, a tenth of its range
                                 each, "point= %d"; the ADC's readings' count */
  uint8_t field_02;           /* +0x02: cleared, never read */
  uint8_t field_03;           /* nothing refers to it */
  float frequency_offset[10]; /* +0x04: each point's, "delta_f" */
  int32_t adc_low;            /* +0x2c: the ADC at a tenth of the range,
                                 "low= %d" */
  int32_t adc_high;           /* +0x30: at nine tenths, "high= %d" */
  uint8_t settle;             /* +0x34: readings let settle */
  uint8_t field_35;           /* nothing refers to it */
} efc_calibration_fields;

/* The :DIAGnostic:ROSCillator:PTESt time uncertainty test's states, each
 * named by the choice :PTESt:STATe? gives it (its jump table, 0x3c338;
 * the choices ARM, FAIL, OFF, PASS and RUN): off, armed by :PTESt:MODE
 * until the uncertainty is valid, running, and passed or failed. */
typedef enum __attribute__((packed)) ptest_state {
  PTEST_STATE_OFF = 0,
  PTEST_STATE_ARMED = 1,
  PTEST_STATE_RUNNING = 2,
  PTEST_STATE_PASSED = 3,
  PTEST_STATE_FAILED = 4,
} ptest_state;

/* Which time uncertainty the test takes, :PTESt:TYPE's value: its setter
 * stores 0 for PREDicted and 1 for MEASured (0x3c18a), pll's
 * predicted_uncertainty or measured_uncertainty. */
typedef enum __attribute__((packed)) ptest_uncertainty_type {
  PTEST_TYPE_PREDICTED = 0,
  PTEST_TYPE_MEASURED = 1,
} ptest_uncertainty_type;

/* The time uncertainty test's run, ptest at 0x10253c (make
 * record-access; ptest_reset clears it whole, 0x46192), each field named
 * by what the test does with it. */
typedef struct ptest_run {
  ptest_state state;
  uint8_t passes;        /* +0x01: EFC history entries in a row whose
                            uncertainty was within the second
                            :PTESt:PTHR, against :PTESt:COUNt */
  uint32_t start_uptime; /* +0x02: get_uptime's as the test starts running */
  uint32_t end_uptime;   /* +0x06: get_uptime's as it passed or failed */
} ptest_run;

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

/* gps_diag_sequence's states, pll.gps_diag_state (0x1026ca): the
 * 58503A's, and self-test 15's, this image's own (0x4c7da). */
typedef enum __attribute__((packed)) pll_gps_diag_state {
  PLL_GPS_DIAG_RESET_SEND = 0,
  PLL_GPS_DIAG_RESET_ACK = 1,
  PLL_GPS_DIAG_TEST_SEND = 2,
  PLL_GPS_DIAG_TEST_ACK = 3,
  PLL_GPS_DIAG_SETTLE = 4,
  PLL_GPS_DIAG_RUN = 5,
  PLL_GPS_DIAG_SELFTEST_15 =
      6, /* steps efc_calibration_measure until it gives 1 */
} pll_gps_diag_state;

/* gps_init_sequence's states, pll.gps_init_state, as
 * print_gps_init_state names them: each setting sent, then its
 * acknowledgment awaited.  The 58503A 3704's, both switches to 19
 * (0x2b596, 0x4bdaa), but that 2 and 3, which the 58503A's stores
 * nothing in, here send the status monitor's step and await it
 * (0x4be20, 0x4be50), and print_gps_init_state prints them "unknown"
 * (its jump table at 0x2b5aa). */
typedef enum __attribute__((packed)) pll_gps_init_state {
  PLL_GPS_INIT_START = 0x00,
  PLL_GPS_INIT_ACK = 0x01,
  PLL_GPS_INIT_STATUS_MONITOR_SEND = 0x02, /* step GPS_STEP_STATUS_MONITOR,
                                              1 */
  PLL_GPS_INIT_STATUS_MONITOR_ACK = 0x03,
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

/* gps_leap_ack's states, pll.leap_ack_state, the 58503A 3704's, its
 * switch's eight (0x4c44a): the engine's time mode switched with @@Aw to
 * measure the GPS-UTC offset, then switched back. */
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

/* The reference pulse's mode, pll.pulse_mode: unit_and_reference_check
 * (0x4d4b0) switches it with no good reference for 60 s, logging
 * "Switched to PP2S mode" or "Switched to 1PPS mode"
 * (docs/firmware/z3817a-3752.md, "The pulse"). */
typedef enum __attribute__((packed)) reference_pulse_mode {
  REFERENCE_PULSE_1PPS = 0,
  REFERENCE_PULSE_PP2S = 1, /* a pulse each 2 s, a reading each pass */
} reference_pulse_mode;

/* The disciplining loop's state block: the stage the status screen
 * shows (docs/screen-format-strings.md, "Mode suffixes") and the PLL's
 * values.  This image's own, 0x3ee bytes (0xfb longs and a word,
 * system_init, 0x22e06 to 0x22e1a), cleared as one, memset(&pll, 0,
 * 0x3ee) (pll_init, 0x4c9ae), each field where this image's code
 * reaches it, through pll's address or a base register loaded with it
 * (make record-access), and named as the 58503A 3704's field the same
 * routine reaches: the 58503A's fields through the hold causes and
 * holdover's, then two floats of holdover's own, which move the
 * recovery and fine states 8 bytes on; a byte of fine's, 2 more; the
 * bytes of unit_and_reference_check's own, which move the EFC, TFOM
 * and uncertainties 26 bytes on from the 58503A's; the measured
 * uncertainty after the predicted, as the Z3815A 4010's; the EFC
 * history this image's own (efc_ring); the aging term and the loop's
 * EFC step after it, as the Z3815A's; the holdover start's uptime
 * where the 58503A's has an unused long; and the pulse's mode and the
 * block's length near the end.  A field no code reaches is named by its
 * offset. */
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
  pll_startup_state startup_state; /* +0x0c: startup_pll's, which
                                      efc_history_update tests in stage 5 */
  uint8_t field_00d;
  pll_normal_state normal_state; /* +0x0e: pll_normal's (stage 7), through a
                                    pointer in its frame */
  uint8_t field_00f;
  pll_hold_cause hold_causes; /* +0x10: a set of pll_hold_cause bits, a word
                                 (diag_pr_hold_cause) */
  pll_holdover_state holdover_state; /* +0x12: holdover's */
  uint8_t manual;                    /* +0x13 */
  uint8_t ti_limit_override; /* +0x14: holdover checks the TI limit while 0;
                                holdover_recovery_limit_ignore sets it */
  uint8_t field_015;
  float holdover_offset; /* +0x16: holdover's, set as it starts: the EFC, less
                            its temperature correction, less the aging fit's
                            value (0x48cea); added to the fit's prediction and
                            stepped toward 0 each block (0x48f08) */
  float holdover_offset_step; /* +0x1a: holdover_offset / 15 (0x48cfa) */
  uint8_t
      recovery_countdown; /* +0x1e: holdover_recovery's, as powerup_countdown */
  pll_recovery_state recovery_state; /* +0x1f */
  pll_coarse_state coarse_state;     /* +0x20: coarse's */
  pll_fine_state fine_state;         /* +0x21: fine's; PLL_FINE_SLEW to
                                 PLL_FINE_FINE_SLEW slew the phase */
  uint8_t frequency_adjusted; /* +0x22: fine applied a frequency correction */
  uint8_t fine_index;         /* +0x23: fine acquisition's "i= %d" */
  uint8_t fine_readings; /* +0x24: the readings fine's fit takes, 50, or 25 in
                            PP2S (0x4a81e to 0x4a82e), which fine_index counts
                            to (0x4a87a, 0x4ab72) */
  uint8_t field_025;
  uint16_t fine_retries; /* +0x26: fine's failed attempts, given up past 9 */
  float fine_d;          /* +0x28: fine acquisition's "d = %f" */
  pll_aging_state
      aging_state; /* +0x2c: efc_history_update's; 3 once the aging fit holds */
  uint8_t field_02d[3];
  pll_gps_diag_state gps_diag_state; /* +0x30: gps_diag_sequence's */
  uint8_t gps_diag_substate;         /* +0x31 */
  pll_gps_init_state
      gps_init_state; /* +0x32: gps_init_sequence's, print_gps_init_state's */
  uint8_t field_033;
  uint16_t leap_check_timer;    /* +0x34: gps_leap_sequence checks again when it
                                   wraps */
  uint8_t leap_check_requested; /* +0x36: pll_stage_status sets it */
  pll_leap_check_state leap_check_state; /* +0x37: gps_leap_sequence's,
                               print_leap_check_state's */
  pll_leap_ack_state leap_ack_state;     /* +0x38: gps_leap_ack's */
  uint8_t leap_ack_requested;            /* +0x39 */
  uint8_t leap_ack_done;                 /* +0x3a: the offset is set */
  uint8_t field_03b;
  uint8_t
      leap_ack_again; /* +0x3c: holdover_recovery measures again after fine */
  uint8_t field_03d;
  uint8_t cable_delay_changed; /* +0x3e: unit_and_reference_check's: set, with
                                  hold cause HOLD_CAUSE_TEMPORARY_MODE and
                                  ti_limit_override, when the GPS's cable delay
                                  changes (0x4d5ec); the hold cleared with it
                                  the next pass (0x4d5c4) */
  uint8_t cable_delay_known;   /* +0x3f: cable_delay holds one (0x4d600) */
  int32_t cable_delay; /* +0x40: the last 0x56616 gave, gps.pps_cable_delay
                          while the engine has sent it, -1000000 otherwise */
  uint16_t hardware_fault_seconds; /* +0x44: unit_and_reference_check's count
                                      while the hardware condition has a bit of
                                      0x70cf; the unit fails at 300 */
  uint16_t reference_hunt_seconds; /* +0x46: its count toward switching TIMEREF,
                                      past 300, to at most 43200 */
  uint16_t not_ready_seconds;      /* +0x48: its count while the oscillator is
                                      not ready (0x4d4e0 to 0x4d51a) */
  uint8_t oscillator_was_ready;    /* +0x4a: the oscillator has been ready */
  uint8_t timeref_select;          /* +0x4b: the TIMEREF line's selection, 0
                                      TIMEREF1; timeref_switch toggles it,
                                      timeref_select_apply drives PE7 by it */
  uint8_t unit_failed; /* +0x4c: unit_and_reference_check's latch: the unit
                          failed, and its first part no longer runs */
  uint8_t timeref_switch_requested; /* +0x4d: timeref_switch_request sets it
                                       (:DIAGnostic:REFerence:CPORt) */
  float efc;                        /* +0x4e: pr_efc's efc */
  float tfom;                       /* +0x52: pll_rep's tfom, s */
  float predicted_uncertainty; /* +0x56: the curv fit's, :KENneth:PREDicted? */
  float measured_uncertainty;  /* +0x5a: measured_uncertainty_update's, which
                                  :KENneth:MEASured? reads */
  uint8_t field_05e[8];
  float frequency_tolerance; /* +0x66: 1e-8 at first; fine's bound, holdover's
                                limit per second */
  efc_ring efc_history;      /* +0x6a */
  float aging_term;    /* +0x3b8: aging_fit_collect's, the fit's slope at the
                          present uptime */
  float loop_efc_step; /* +0x3bc: startup_pll's EFC step, filtered and clamped
                          to plus or minus efc_gains.step_limit */
  float ti_filtered;   /* +0x3c0: the TI, filtered with weight 29.75 / tau */
  float efc_base; /* +0x3c4: the EFC less the temperature term, TC times the
                     current */
  float loop_time_constant;       /* +0x3c8: the ramped time constant, from the
                                     default block's +0x14 (pll_init) */
  uint8_t field_3cc;              /* nothing refers to it */
  uint8_t led_gps_lock;           /* +0x3cd: :LED:GPSLock? */
  uint8_t led_holdover;           /* +0x3ce: :LED:HOLDover? */
  uint8_t field_3cf;              /* nothing refers to it */
  uint32_t holdover_duration;     /* +0x3d0: :ROSCillator:HOLDover:DURation? */
  uint32_t holdover_start_uptime; /* +0x3d4: get_uptime's as enter_holdover
                                     starts the count */
  uint32_t reference_uptime;      /* +0x3d8: the uptime clock_set_from_reference
                                     counts from */
  time_of_day reference_time;     /* +0x3dc: gps.status.time as pllp_task last
                                     took it, which the clock's check against GPS
                                     compares */
  uint32_t efc_control_passes;    /* +0x3e0: efc_mode_expire's count toward
                                     clearing HOLD_CAUSE_EFC_CONTROL at 600 */
  uint8_t reference_seconds;      /* +0x3e4: the FPGA's seconds count with it */
  fpga_configuration_index
      fpga_configuration; /* +0x3e5: the configuration fpga_setup loaded and
                             fpga_load_test loads again */
  uint8_t fpga_loaded;    /* +0x3e6: the PPS checks wait for it */
  uint8_t loop_running;   /* +0x3e7: pll_restart sets it */
  uint8_t sync_requested; /* +0x3e8: sync_perform syncs whatever the TI, then
                             clears it */
  uint8_t field_3e9;      /* startup_pll, pll_normal and pll_stage_status test
                             it, pll_init clears it */
  reference_pulse_mode pulse_mode; /* +0x3ea: unit_and_reference_check's
                                      (0x4d680, 0x4d6ac); status_snapshot
                                      copies it */
  uint8_t field_3eb;    /* pll_init sets 9 (0x4ca1a), pll_tfom_digit_store
                           writes it */
  uint8_t block_length; /* +0x3ec: the readings to a block, 10 from pll_init
                           (0x4ca20) and in 1PPS, 5 in PP2S (0x4d686) */
  uint8_t measured_uncertainty_valid; /* +0x3ed: measured_uncertainty_update's
                                         (0x46034), :KENneth:MEASured?'s */
} pll_state;

/* The GPS link's counters, gps_counts (0x101552), which the "GPS Process
 * Monitor" page (gps_print_process_monitor, 0x53666) prints through A2.
 * All words but responses_read, a long (0x5372e); the 58503A's through
 * raim_alarms, then four words nothing refers to, and the 58503A's last
 * five, eight bytes on: gps_receive counts its unknown IDs, frame errors
 * and checksum errors at +0x36, +0x38 and +0x3a (0x522e0, 0x5227c,
 * 0x52330). */
typedef struct gps_statistics {
  uint16_t received; /* Msgs Rcvd */
  uint16_t sent;     /* Msgs Sent */
  uint16_t jammed;   /* Msgs Jam */
  uint16_t sent_errors;
  uint16_t received_errors;
  uint16_t jam_errors;
  uint16_t successes;        /* Success */
  uint16_t field_0e;         /* NOP */
  uint16_t rejects;          /* Reject */
  uint16_t timeouts;         /* Timeout */
  uint16_t busy;             /* Busy */
  uint16_t aborts;           /* Abort */
  uint16_t no_command;       /* No Cmd */
  uint16_t no_query;         /* No Qry */
  uint16_t unknown_command;  /* ? Cmd */
  uint16_t unknown_sender;   /* ? Sndr */
  uint16_t commands_sent;    /* Cmds Sent */
  uint32_t responses_read;   /* Resp */
  uint16_t e_retries;        /* E Retry */
  uint16_t t_retries;        /* T Retry */
  uint16_t raim_detected;    /* Time RAIM page: (%d detected) (0x53c88) */
  uint16_t raim_alarms;      /* # Alarms (0x53c92) */
  uint16_t field_2e[4];      /* nothing refers to them */
  uint16_t unknown_response; /* ? Resp (0x537e8) */
  uint16_t frame_errors;     /* Frame (0x5381c) */
  uint16_t checksum_errors;  /* Cksum (0x53850) */
  uint16_t command_monitor_failures; /* command_monitor's (0x51e20) */
  uint16_t status_monitor_failures;  /* its status monitor's (0x5202c) */
} gps_statistics;

#include "../../common/types/exception_record.h"

#include "../../common/types/os.h"

/* A fixed event log message, by the code log_event (0x4271c) takes
 * and its jump table's order (0x42740, 28 cases); the names are the
 * messages'.  enter_holdover (0x48a10) picks a holdover's by its cause.
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
 * (oncore_message_entry.match_kind; its jump table at 0x572c2), this
 * image's own: the 58503A 3704's kinds one higher from 1, with a kind of
 * its own at 1, which no entry has, and @@Au's at 7, read from the
 * message table (0x575d8).  The kinds that compare take the response as
 * the command's echo, its bytes after the ID the same (VPCommands.pdf for
 * each message). */
typedef enum __attribute__((packed)) oncore_match_kind {
  ONCORE_MATCH_ANY = 0,        /* answered at once; gpsm_task waits for none */
  ONCORE_MATCH_PASS = 1,       /* gps_response_match's GPS_MATCH_IGNORED for
                                  any response, and gpsm_task passes the step
                                  over at its first timeout (0x52f1a) */
  ONCORE_MATCH_ECHO = 2,       /* the same message, its bytes the same */
  ONCORE_MATCH_GMT_OFFSET = 3, /* @@Ab: an echo, but a negative offset under
                                  an hour, sign 0xff and hours 0, compared as
                                  sign 0 and its minutes negated */
  ONCORE_MATCH_HEIGHT = 4,     /* @@Af: the height alone, the response's MSL
                                  one for type 1, within 2 cm of the sent
                                  while that is in range (0x573e2) */
  ONCORE_MATCH_SECOND_AR = 5,  /* the second @@Ar, entry 0x38: an echo of
                                  entry 0x38 (0x572e6) */
  ONCORE_MATCH_HOLD_POSITION = 6, /* @@As: latitude and longitude alone when
                                     the type is the same and the height in
                                     range and within 2 cm */
  ONCORE_MATCH_ALTITUDE_HOLD_HEIGHT = 7, /* @@Au: the height alone, within 2 cm
                                            of the sent while that is in range
                                            (0x573e2) */
  ONCORE_MATCH_FIX_MODE = 8,     /* @@Cg: an echo, but mode 2, a query of the
                                    mode, matched at once */
  ONCORE_MATCH_SAME = 9,         /* the same message, its bytes not compared */
  ONCORE_MATCH_USER_DATUM = 10,  /* @@Ao: answered by @@Ap */
  ONCORE_MATCH_NEVER = 11,       /* never answered; no message has it */
  ONCORE_MATCH_CORRECTIONS = 12, /* @@Bh: answered by @@Ce */
  ONCORE_MATCH_EA = 13,          /* the second @@Ea, entry 0x37: answered by
                                    the first, entry 0x32 (0x574a4) */
} oncore_match_kind;

/* gps_response_match's verdict on a response (0x57280): gpsm_task takes
 * the next step on 3, and on 2 sends again, up to three tries, and waits
 * on for 0 and 1 (0x52fae to 0x52ff2). */
typedef enum __attribute__((packed)) gps_match_result {
  GPS_MATCH_OTHER = 0,    /* not the message that answers the command */
  GPS_MATCH_IGNORED = 1,  /* ONCORE_MATCH_PASS's, for any response */
  GPS_MATCH_DIFFERS = 2,  /* its answer, its values not the command's */
  GPS_MATCH_ANSWERED = 3, /* its answer */
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
 * (gps_request_build, 0x57058); oncore_filter_channels encodes the
 * first argument to test it (0x56f48). */
typedef uint8_t oncore_send_filter(int16_t index, oncore_encoder* encoder,
                                   int32_t* arguments);

/* An entry of the GPS task's Oncore message table, oncore_messages: 60
 * bytes, as the 58503A 3704's, the Z3805A 3543B's 56 and a send filter
 * (docs/firmware/oncore/README.md, "The message table";
 * oncore_message_entry_at, 0x57014). */
typedef struct oncore_message_entry {
  char* id; /* +0x00: the two-letter ID; zero for an empty slot */
  oncore_message_kind kind; /* +0x04 */
  uint8_t field_05;
  oncore_encoder* encoders[4]; /* +0x06: to the first zero */
  uint8_t* query; /* +0x16: the query form, the bytes after the ID to a NUL,
                     0xee standing for a zero byte (0x5714c); zero if none */
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

/* The settings nv_save keeps in the NV register and nv_restore loads,
 * nv_settings (0x1023d2): each field the value of the descriptor in
 * nv_save's tables that points at it, named by the SCPI command whose
 * setter is given that descriptor (docs/firmware/structures.md, "The
 * Z3817A 3752's record").  0xcb bytes, nv_restore's copy of its
 * defaults, nv_settings_defaults (0x4146e), to debug_output at 0x10249c
 * (0x4366e): the 58503A 3704's, with :DIAGnostic:LOG:WRAP's setting at
 * +0x9e, where the 58503A's has questionable_user, which with
 * serial_restricted and pulse_state lies a byte further in, over the
 * 58503A's pad; its tables are nv_numeric_params (0x43bda, 23
 * descriptors, nv_save's first loop's bound) and nv_setting_params
 * (0x43c36, 19).  The Z3815A 4010's less active_holdover_threshold. */
typedef struct nv_settings_fields {
  uint8_t field_00[2];               /* no code refers to them */
  int32_t elevation_mask;            /* :GPS:EMANgle */
  int32_t antenna_delay;             /* :GPS:ADELay */
  int32_t douglas_duration;          /* :DOUGlas:DURation */
  uint8_t douglas_duration_infinite; /* :DOUGlas:DURation INFinity */
  uint8_t tracking_log;              /* :DIAG:GPS:TRACking:LOG (0x45174) */
  int32_t questionable_hysteresis;   /* :DIAG:REF:GPS:QUES:HYSTeresis
                                        (0x4510a), 10 bits */
  int32_t
      external_questionable_hysteresis; /* no command: only nv_numeric_params
                                           refers to its descriptor (0x45148);
                                           gps_init passes it to
                                           ext_reference_hysteresis_set, 4 bits
                                         */
  int32_t holdover_status_threshold;    /* :ROSC:HOLD:DUR:STAT:THR */
  float limit_threshold;                /* :ROSC:LIMit:THReshold */
  float holdover_limit_threshold;       /* :ROSC:HOLD:LIMit:THReshold */
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
  uint8_t log_wrap;                       /* :DIAG:LOG:WRAP (0x45190) */
  uint8_t questionable_user;              /* :STAT:QUES:...:USER */
  uint8_t serial_restricted;              /* :DIAG:SER1:RESTricted */
  uint8_t pulse_state;                    /* :PULSe:CONTinuous:STATe */
  int32_t pulse_period;                   /* :PULSe:CONTinuous:PERiod */
  int32_t pulse_start_time[3];            /* :PULSe:STARt:TIME */
  int32_t pulse_start_date[3];            /* :PULSe:STARt:DATE */
  int32_t field_be;              /* 16 bits (0x450cc); only its descriptor */
  uint8_t output_state;          /* :OUTPut:STATe */
  data_format format_data;       /* :FORMat:DATA */
  uint8_t memory_save;           /* :ROBin:MEMory:SAVE */
  uint8_t antenna_current_alarm; /* :DIAG:GPS:ACURrent:ALARm (0x44ff4) */
  uint8_t douglas_state_powerup; /* :DOUGlas:STATe:POWerup */
  timecode_format tcode_format;  /* :PTIMe:TCODe:FORMat */
  uint8_t gps_utc;               /* :DIAG:GPS:UTC */
  uint8_t pforth_at_startup;     /* :DIAG:SYSTem:PSTartup */
  uint8_t debug_output;          /* :DIAG:SYSTem:DOUTput */
} nv_settings_fields;

#include "../../common/types/interval_reading_fields.h"

#include "../../common/types/counter_work_state.h"

/* The time-interval counter's configuration, counter_configuration at
 * 0x1024fc, which counter_arm and counter_read are given.  A mode and a
 * state go in pairs: the mode says when the work is done, the state asks
 * for it or says it is done.  The 58503A's (its smartclock.h) less the
 * interpolator's pair and its offset and scale, there being no
 * interpolator: counter_arm loads the registers at +0xc and +0xd and
 * syncs at +0xe and +0xf, +0x10's kind, +0x12's count (0x45786 to
 * 0x457fa), and counter_read subtracts the calibration (0x45896).
 * 0x16 bytes, aging_curve after them (0x102512). */
typedef struct counter_configuration_fields {
  uint8_t fpga_registers[4]; /* counter_arm copies them into fconf_regs */
  double calibration;        /* s, which counter_read subtracts */
  counter_registers_mode registers_mode; /* when fpga_registers load */
  counter_work_state registers_state;
  counter_sync_mode sync_mode;   /* when counter_arm syncs */
  counter_work_state sync_state; /* sync_request sets both */
  counter_sync_kind sync_kind;   /* sync_request's first */
  uint8_t pad_11;
  int32_t sync_count; /* sync_request's second; counted down */
} counter_configuration_fields;

/* The battery-backed RAM a warm restart restores, preserved_region at
 * 0x100000: pllp_task copies gps, pll and the RAM blocks below into it
 * each pass and checksums it, and system_init copies them back when the
 * checksum is good, RSR shows neither an external nor a power-up reset,
 * and restore is 1.  0xb50 bytes, record_checksum's length (system_init,
 * 0x22da8), each block where system_init's copies take it (0x22df2 to
 * 0x22e2a). */
typedef struct preserved_region_fields {
  uint16_t
      checksum; /* +0x000: record_checksum's sum and complement of the rest */
  uint16_t restore;            /* +0x002: a preserved_region_restore */
  gps_state gps;               /* +0x004 */
  pll_state pll;               /* +0x5c4: pll's, 0xfb longs and a word */
  adc_monitor adc_monitors[8]; /* +0x9b2: adc_monitors' */
  uint8_t values[0x1d];        /* +0xb32: retained_block's (0x1023b4) */
  uint8_t field_b4f;           /* in the checksum; nothing else refers to it */
} preserved_region_fields;

#include "../../common/types/clock_record.h"

#include "../../common/types/pps_exchange_message.h"

#include "../../common/types/gps_process_state.h"

#include "../../common/types/satellite_sets.h"

/* A satellite_status_list entry, 8 bytes: status_snapshot keeps twice
 * gps.channel_count not tracked and channel_count tracked, up to 16 and
 * 8 (0x50e7a, 0x50e9a); satellite_status_blank_entry (0x54ebe) is an
 * empty one. */
typedef struct satellite_list_entry {
  uint8_t satellite;   /* 0 for none */
  uint8_t attempting;  /* not tracked: on a channel but not used (0x4fd20) */
  int16_t level;       /* tracked: its channel's signal strength; -1 none */
  satellite_cell cell; /* +0x04 */
} satellite_list_entry;

/* In the Z3815A 4010, the output's state, its output_state_get's, from
 * output_status' bits in this order: bit 6, then bit 0, then bit 1, then
 * bit 3 without bit 2, else unlocked.  Its :OUTPut:STATe? reports each by
 * a keyword (output_state_choice), its status screen's heading by its
 * card line (status_screen_card_lines: Active, Standby, Unlocked, Blocked,
 * ALARM), and its es_build sets es_status' bits 5 and 3 for active and
 * standby and es_error's bits 6 and 1 for blocked and alarm. Nothing in this
 * image uses the type. */
typedef enum __attribute__((packed)) output_state {
  OUTPUT_ACTIVE = 0,   /* output_status bit 0 */
  OUTPUT_STANDBY = 1,  /* bit 1 */
  OUTPUT_UNLOCKED = 2, /* none of the others */
  OUTPUT_BLOCKED = 3,  /* bit 3, bit 2 clear */
  OUTPUT_ALARM = 4,    /* bit 6, ahead of the others */
} output_state;

/* A status snapshot, status_snapshot's record (0x50c3c), which the
 * status screen prints: 0x170 bytes, status_snapshot_latest (0x1019c8)
 * to status_snapshot_flagged (0x101b38), each field where
 * status_snapshot stores it (make record-access).  The 58503A 3704's
 * fields through the satellite counts; then the Z3815A 4010's, without
 * its two bytes first, the external reference's padding, the TPS time
 * mode, the hold height and the alarms after the hardware condition:
 * the loop's TIMEREF selection before the external reference, the two
 * GPS records' codes a byte nearer, and two bytes of the GPS record's
 * after the Douglas flag. */
typedef struct status_snapshot_record {
  pll_stage stage;         /* +0x000: pll.stage */
  uint8_t mode;            /* +0x001: pll_mode's */
  uint8_t powerup_suffix;  /* +0x002: pll_powerup_suffix_index's */
  uint8_t recovery_suffix; /* +0x003: recovery_suffix_index's */
  float tfom;              /* +0x004: pll's tfom (0x50ca0) */
  uint8_t ffom;            /* +0x008: get_ffom's */
  uint8_t ffom_valid;      /* +0x009: get_ffom's result */
  double interval;         /* +0x00a: interval_reading.value, copied with the
                              next three as three longs (0x50c8c to 0x50c90) */
  uint8_t interval_valid;  /* +0x012: interval_reading.valid */
  counter_error interval_errors;   /* +0x013: interval_reading.errors */
  uint16_t interval_tag;           /* +0x014: interval_reading.tag */
  float interval_mean;             /* +0x016 */
  uint16_t interval_mean_flag;     /* +0x01a: the word at interval_mean_flag */
  uint8_t holdover_waiting_reason; /* +0x01c */
  holdover_limit_setting holdover_limit_state; /* +0x01d: nv_settings' */
  float holdover_limit_threshold;              /* +0x01e: nv_settings' */
  uint32_t holdover_duration;          /* +0x022: pll.holdover_duration */
  float kenneth_present;               /* +0x026: kenneth_present_get's */
  uint8_t kenneth_present_valid;       /* +0x02a: its result */
  int8_t kenneth_present_resolution;   /* its resolution, a power of ten */
  float kenneth_predicted;             /* +0x02c: kenneth_predicted_get's */
  uint8_t kenneth_predicted_valid;     /* +0x030: its result */
  int8_t kenneth_predicted_resolution; /* its resolution, a power of ten */
  uint8_t channel_count;               /* +0x032: gps' (0x50e44) */
  uint8_t field_033;
  satellite_sets satellites;            /* +0x034: satellite_sets_build's */
  satellite_list_entry not_tracked[16]; /* +0x05e: satellite_status_list's
                                           list 0, twice the channels */
  satellite_list_entry tracked[8];      /* +0x0de: its list 1 */
  uint8_t status_received;              /* +0x11e: gps' */
  uint8_t not_tracked_count;       /* +0x11f: satellite_status_list's count of
                                      not_tracked */
  uint8_t tracked_count;           /* +0x120: its count of tracked */
  uint8_t timeref_select;          /* +0x121: pll + 0x4b, the TIMEREF line's
                                      selection (0x50d66; timeref_select_apply) */
  reference_pulse_mode pulse_mode; /* +0x122: pll.pulse_mode (0x50d6c) */
  uint8_t reference_valid;         /* +0x123: gps.references[0].valid */
  uint8_t pps_dead;                /* +0x124: gps.references[0].pps_dead */
  gps_sync_status status_code;     /* +0x125: gps.references[0].state */
  uint32_t reference_elapsed;      /* +0x126: gps.references[0].elapsed */
  uint8_t external_valid;          /* +0x12a: gps.references[1].valid */
  uint8_t external_pps_dead;       /* +0x12b: gps.references[1].pps_dead */
  gps_sync_status external_status_code; /* +0x12c: gps.references[1].state */
  uint8_t field_12d;
  uint32_t external_elapsed;   /* +0x12e: gps.references[1].elapsed */
  gps_field_592_bit field_592; /* +0x132: gps', copied with gps_field_663 as
                                  a word (0x50dae) */
  uint8_t gps_field_663;       /* +0x133 */
  uint8_t fix_mode;            /* +0x134: gps' (0x50db4) */
  oncore_receiver_status receiver_status; /* +0x135: gps.status' */
  time_of_day time;     /* +0x136: gps.status' time made local;
                           get_date_time's while from_clock */
  uint32_t date;        /* +0x13a: the same's date */
  int32_t time_zone[2]; /* +0x13e: nv_settings' */
  uint8_t from_clock;   /* +0x146: 1 when the clock is set, the time its */
  uint8_t utc_time;     /* +0x147: gps' */
  oncore_leap_pending
      leap_second_pending; /* +0x148: from nv_settings.leap_minute_length,
                              59 a second subtracted, 61 added (0x50df4) */
  uint8_t field_149;
  int32_t pps_cable_delay;           /* +0x14a: gps' */
  uint8_t mask_angle;                /* +0x14e: gps' */
  uint8_t position_hold;             /* +0x14f: gps' */
  uint8_t douglas_duration_infinite; /* +0x150: nv_settings' (0x50d90) */
  uint8_t gps_field_576;             /* +0x151: the GPS record's byte at +0x576
                                        (0x50e38) */
  uint8_t gps_field_577;             /* +0x152: the next (0x50e3e) */
  uint8_t field_153;
  char* position_source;          /* +0x154: "" (0x50ea2) */
  int32_t position[3];            /* +0x158: latitude, longitude, height */
  oncore_height_type height_type; /* +0x164: MSL where field_592's bit 6
                                     picks the MSL height (0x50eb6) */
  uint8_t field_165;
  uint32_t average_count;      /* +0x166: gps' */
  int32_t douglas_duration;    /* +0x16a: nv_settings' */
  uint16_t hardware_condition; /* +0x16e: status_hardware_condition's */
} status_snapshot_record;

#include "../../common/types/psos.h"

#include "../../common/types/spoo_request.h"

#include "../../common/types/early_status_events.h"

#include "../../common/types/installer_test_choice.h"

/* The boot code's self-test results, boot_selftest: a checksum word,
 * then the fields of selftest_results' first 14 bytes in their order, the
 * output latch's shadow last, a word, where the other images keep the
 * DUART output bits boot_reset resets, a byte: boot_trap_11 clears it
 * and the latch (0x5ae, 0x5b4), ORs 0x80 into it for a failure and
 * writes it to output_latch (0x5c2 and 0x5ca, 0x764, 0x89c), and the
 * installer's latch writers set and clear its bits (installer::0x100fbe,
 * installer::0x100fe4), as the primary's do selftest.latch's.  The
 * installer's :DIAGnostic:TEST? reads all but the latch. */
typedef struct boot_selftest_results {
  int16_t checksum; /* +0x00: boot_reset's word sum: 1 if primary_vectors to
                       0x7fffd's is not the word at 0x7fffe (0x458 to 0x476) */
  int16_t cpu;      /* +0x02: 1 if the CPU test fails (0x754) */
  uint16_t ram;     /* +0x04: bit 0 a pattern failed (0x81a), bit 1 an address
                       line aliases (0x876) */
  uint16_t ram_data_bits;     /* +0x06: the bits that failed a pattern */
  uint32_t ram_address_lines; /* +0x08: the address lines that alias */
  int16_t uart;   /* +0x0c: nothing in this image's boot code, which has no
                     DUART check, sets it; :DIAGnostic:TEST? reads it */
  uint16_t latch; /* +0x0e: the output latch's shadow */
} boot_selftest_results;
