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

/* gps_receive's state, gps_receive_state (0x10170a): the NMEA framer's,
 * this image's own where the others' frame an Oncore message from its
 * @@ -- the index of its state's transitions (0x5e036; gps_nmea.c), the
 * sentence complete at 5, which gpsm_task tests (0x57f6a). */
typedef enum __attribute__((packed)) gps_framing_state {
  GPS_FRAMING_IDLE = 0,        /* waits for a '$' */
  GPS_FRAMING_FIELD = 1,       /* the sentence's first field */
  GPS_FRAMING_FIELD_START = 2, /* a field after a comma */
  GPS_FRAMING_CHECKSUM = 3,    /* the checksum's digits, after '*' */
  GPS_FRAMING_LINE_END = 4,    /* after the CR */
  GPS_FRAMING_COMPLETE = 5,    /* gps_receive reads no more */
} gps_framing_state;

#include "../../common/types/gps_request_result.h"

/* gps_request_class's sorting of a request code (gps_request_code). */
typedef enum __attribute__((packed)) gps_code_class {
  GPS_CLASS_MESSAGE = 0, /* 0 to 0x4c with argument mode 0 or 1: one message
                            (0x57ae0) */
  GPS_CLASS_SCRIPT = 1,  /* 0x6b to 0x83: a script */
  GPS_CLASS_STEP = 2,    /* 0x4d to 0x6a: gps_console_request's */
  GPS_CLASS_INVALID = 3,
} gps_code_class;

#include "../../common/types/oncore_field_code.h"

#include "../../common/types/oncore_receiver_status.h"

/* Which reference gps_receiver_status_update qualifies:
 * the GPS's, from the receiver's status, or the external's, from the
 * alternate receiver's, gps_alternate_status (0x56ce6, 0x56dd4), which
 * the 58503A 3704's ignores; gpsm_task and status_snapshot call it with
 * each. */
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
 * satellite_status the rest (0x54b06), and format_satellite_cell prints
 * each (its table at 0x54f58).  One more than the others': satellite_status
 * gives a satellite satellite_track_class calls 4, healthy and in the set
 * on its stack (0x54aa6), "Isolat", 5, and the acquiring bands 6 to 8. */
typedef enum __attribute__((packed)) satellite_cell_status {
#include "../../common/types/satellite_cell_status.h"
  SATELLITE_CELL_ISOLATED = 5,     /* "Isolat" */
  SATELLITE_CELL_ACQUIRING = 6,    /* "Acq   ": channel modes 1 to 4 */
  SATELLITE_CELL_SYNCHRONIZED = 7, /* "Acq . ": modes 5 and 6 */
  SATELLITE_CELL_EPHEMERIS = 8     /* "Acq ..": modes 7 and 8 */
} satellite_cell_status;

/* satellite_track_class's class of a visible satellite, by its health
 * (oncore_health, which the anc and ssd commits set; its table at
 * 0x54aa2): satellite_status shows a removed one as "Ignore" and an
 * unhealthy one as "Not OK".  One more than the others': a healthy one in
 * the set satellite_status gives it, Time RAIM's (gps.raim_satellites,
 * rrm's, 0x54b58), is 4, which satellite_status shows as "Isolat"
 * (0x54aba). */
typedef enum __attribute__((packed)) satellite_health_class {
#include "../../common/types/satellite_health_class.h"
  SATELLITE_CLASS_ISOLATED = 4,
} satellite_health_class;

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
/* A reference's state, gps_reference_record.state, and
 * status_snapshot_record's copies.  One more than the 58503A 3704's:
 * gps_receiver_status_update stores 0 to 5 by the faults
 * it checks in turn, 5 when it finds none (0x56f78 to 0x56fa2), then 6
 * to 9 (0x56e84 to 0x56ec2), and the status line names each (0x552ba;
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

/* The loop's stage, pll.stage: the console's "PLL state: " strings, by
 * its jump table (0x2c1d8; docs/firmware/loop.md, "The stages").  One
 * more than the 58503A 3704's: 6, "n3 lock pll", which the log names
 * "Went into PLL lock to other card" (loop.md, "The Z3815A's extra
 * stage"), so that normal pll, diag, idle and fatal error are each one
 * further on. */
typedef enum __attribute__((packed)) pll_stage {
  PLL_STAGE_INVALID = 0,
  PLL_STAGE_POWERUP = 1,
  PLL_STAGE_POWERUP_RECOVERY = 2,
  PLL_STAGE_HOLDOVER = 3,
  PLL_STAGE_HOLDOVER_RECOVERY = 4,
  PLL_STAGE_STARTUP_PLL = 5,
  PLL_STAGE_N3_LOCK_PLL = 6, /* "n3 lock pll" */
  PLL_STAGE_NORMAL_PLL = 7,
  PLL_STAGE_DIAG = 8,
  PLL_STAGE_IDLE = 9,
  PLL_STAGE_FATAL_ERROR = 10
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

/* startup_pll's and pll_normal's steering, at loop_steering: a
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

/* The time code's format, :PTIMe:TCODe:FORMat, which tcode_request
 * takes: T1, the GPS seconds; T2 with a test prefix; T2; and this image's
 * FL1 and FL2, its keywords' third and fourth (scpi_keyword_FL1), which
 * :PTIMe:TCODe:FORMat stores as 3 and 4 (0x42082) and timecode_update
 * builds nothing for (0x242ac). */
typedef enum __attribute__((packed)) timecode_format {
#include "../../common/types/timecode_format.h"
  TIMECODE_FL1 = 3,
  TIMECODE_FL2 = 4,
} timecode_format;

/* tcode_send's routine, which timecode_schedule sends the time code
 * through (0x14128): drtb_send_async's shape, which tcode_request stores
 * (0x13d6a). */
typedef void tcode_send_function(char* block, uint8_t count);

#include "../../common/types/klok_event_class.h"

#include "../../common/types/duartb_settings_record.h"

/* The SCI's serial settings, the console's, at 0x102640, and the
 * installer's at 0x10acb0: one byte for parity and data bits together and
 * no stop-bit setting.  sci_init and sci_reconfigure apply them.
 * parameter_record_read reads +2 to +5 from the parameter record's
 * bytes 9 to 0xc (0x452ec to 0x45304), or copies the defaults at 0x44d88
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
 * sci_init sends them, at 0x103cc2, as the Z3805A's. */
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

/* The SCI port's driver, at 0x103dfc, after its receive ring at 0x103cfa
 * as in the Z3805A, but this image's own layout: sci_puts' count after
 * its next character (0x2fdc2), and the transmitting flag and the
 * character time output compare 2 ends it by (sci_transmitting_set,
 * sci_transmit_end_schedule), each field where the driver's code
 * addresses it (check-layout.c). */
typedef struct sci_driver {
  char* transmit_next;     /* +0x00: sci_puts's next character */
  uint16_t transmit_count; /* +0x04: sci_puts's characters left */
  char* async_next;        /* +0x06: sci_send_async's */
  uint8_t async_count;     /* +0x0a */
  uint8_t async_pending;   /* +0x0b */
  uint8_t flow; /* +0x0c: 0x80 an XOFF received, holding transmission; 0x20 one
                   sent */
  uint8_t receive_errors; /* +0x0d: SCSR's OR, FE and PF, overrun, framing and
                             parity (MC68331UM D.4.6) */
  uint8_t read_errors;    /* +0x0e: receive_errors as the last read left them */
  uint8_t xon_xoff;       /* +0x0f: flow control, from the serial settings */
  uint8_t mark_bit; /* +0x10: each character goes out with its top bit set: mark
                       parity, which SCCR1's PE and PT cannot give */
  uint8_t
      seven_bits; /* +0x11: seven data bits, the top bit received stripped */
  uint8_t reconfiguring; /* +0x12: set while sci_reconfigure changes the port */
  uint8_t field_13;      /* +0x13: sci_send_async needs it clear, as the
                            Z3805A's +0x11 */
  uint8_t async_allowed; /* +0x14: an asynchronous send may start */
  uint8_t field_15;      /* +0x15: sci_release_for_timecode sets it */
  uint8_t transmitting;  /* +0x16: from a character's write until a character
                            time after the last one; nothing else reads it */
  uint8_t field_17;
  uint16_t baud_constant; /* +0x18: a character's time in timer counts at the
                             baud rate, which sci_transmit_end_schedule waits */
  putc_function* echo;    /* +0x1a: from the serial settings; sci_echo_none with
                             echo off */
  uint8_t transmit_busy;  /* +0x1e: set at each character's write, cleared when
                             its end is scheduled; while it is set, output
                             compare 2's interrupt leaves transmitting */
  uint8_t last_terminator; /* +0x1f: the CR or LF that ended the last line */
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
  /* Past the table, this image's own: :DIAGnostic:CALibration:ROSCillator
   * requests it (0x3f5f4), and gps_diag_sequence runs it by stepping
   * efc_calibration_measure (0x5093a); what it calibrates is not
   * established past that. */
  SELFTEST_TEST_15 = 15,
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
 * Engine timeout" (selftest_gps, 0x1801c, 0x18030).  An
 * eight-channel engine's @@Fa result, which selftest_gps reads
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
  int16_t uart;      /* +0x0a: duart_early_init's IVR and loopback checks,
                        selftest_uart_failure's bits; lights OP6 */
  uint16_t latch;    /* +0x0c: the 16-bit write latch at 0x300000 (CS1), which
                        latch_bits_set and its kin keep and write; a word here,
                        where the 58503A 3704's is a byte */
  uint8_t lamp_mask; /* +0x0e: the DUART output bits the failures reset, which
                        primary_reset sets and duart_init writes back */
  uint8_t field_00f;
  uint16_t summary; /* +0x10: selftest_summary_bit's */
  int16_t eeprom;   /* +0x12: selftest_failure */
  int16_t
      fpga; /* +0x14: the FPGA's counters, counter_selftest; selftest_failure */
  uint32_t gps; /* +0x16: the receiver's self-test, a long: gps_selftest_run
                   and gps_diag_sequence store it, selftest_gps tests it by
                   bit */
  int16_t interpolator;            /* +0x1a: selftest_failure */
  uint8_t interpolator_calibrated; /* +0x1c: 1 once a calibration succeeded */
  uint8_t interpolator_tries; /* +0x1d: the calibrations it took, up to 20 */
  uint16_t interpolator_readings[2]; /* +0x1e: both ends' readings,
                                        interpolator_sample_test's */
  uint8_t abort;   /* +0x22: set to stop a test running, selftest_abort */
  uint8_t running; /* +0x23: set while selftest_execute runs a test */
  int16_t power;   /* +0x24: the 5 V, +15 V and -15 V supplies,
                      selftest_power_failure's bits */
  int16_t internal_reference; /* +0x26: the internal 1PPS's, selftest_failure */
  int16_t qspi; /* +0x28: the QSPI's devices, selftest_qspi_failure's bits */
  uint8_t field_2a[14];
  uint8_t lamp_testing; /* +0x38: set while lamp_test runs */
  uint8_t field_39;
  uint8_t passed; /* +0x3a: 1 unless a test selftest_print_failure ran failed */
  uint8_t field_3b;
  uint32_t result;     /* +0x3c: selftest_run's test's result, what *TST? and
                          :DIAGnostic:TEST? report */
  uint32_t details[2]; /* +0x40: the RAM test's bits and lines, or the
                          interpolator's tries and outcome or readings */
  selftest_test_number test; /* +0x48: the test selftest_run was asked for */
  uint8_t in_run; /* +0x49: set during selftest_run; nothing reads it */
} selftest_results;

/* Who sends a GPS request (gps_request_entry.sender), so that its result
 * comes back to it: gpsx_send for the loop's GPS sequences (pllp_task
 * takes the result as gps_ack_code), pll_scpi_send_to_loop for a
 * setting (pll_scpi_complete), and gps_request_post for the command
 * and status monitors, whose results gps_count_response keeps in
 * command_monitor_result and status_monitor_result (0x57a90, 0x57652).
 * request_kind_valid takes 1 to 5, from its table at 0x594dc, which
 * GPS_SENDER_NONE ends.  Its own, where the others' monitors are 3 and
 * 4: pll_scpi_send_to_loop's caller (0x4c66a) passes 3 for a message
 * whose byte +5 is neither 0 nor 5, 2 for the rest, and for 3 the status
 * snapshot step takes its snapshot at 0x10193c, not 0x1017c6 (0x58690);
 * gps_request_post is called with 4 and 5 (0x57204, 0x57384). */
typedef enum __attribute__((packed)) gps_request_sender {
#include "../../common/types/gps_request_sender.h"
  GPS_SENDER_SETTING_OTHER = 3,
  GPS_SENDER_COMMAND_MONITOR = 4,
  GPS_SENDER_STATUS_MONITOR = 5,
} gps_request_sender;

/* A GPS request's code (gps_request_entry.code, a descriptor's
 * gps_request), which gps_request_class (0x57ae0) sorts: 0 to 0x4c a
 * message, 0x4d to 0x6a a step of the GPS task's own,
 * gps_console_request's (0x58690), 0x6b to 0x83 a script,
 * gps_script_lookup's (0x57b2a).  The messages below 0x39 are the
 * Oncore's codes, the 58503A 3704's, which this image's requests still
 * send (gps_command_send, the init sequence) and which the GPS task
 * translates before it runs one (0x5d7a8, its table 0x5e6d4): a code
 * the table gives, set and not queried, to the Furuno sentence or the
 * script or step that does its work, any other to GPS_REQUEST_NONE.
 * From 0x39 each is a Furuno GT-74 sentence, its descriptor
 * 0x5dcac + 0x20 * code (gps_request_descriptor, 0x5c0f6), named by the
 * sentence it sends:
 * "$PFEC,GP" and the descriptor's text ("set,M"), or "$GP" and it
 * ("ZDA,"), then each argument through the descriptor's encoders.  The
 * steps are named by gps_console_request's case, the scripts by
 * the steps they hold (each 0x5e3c8 when empty), as the 58503A's; a
 * code with neither has no name. */
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
  GPS_NMEA_MASK_ANGLE = 0x39,                  /* set,M: the Oncore's @@Ag's */
  GPS_NMEA_TIME_DATE = 0x3a,                   /* ZDA: time, day, month, year */
  GPS_NMEA_POSITION = 0x3b,        /* GLL: latitude, longitude; @@As's */
  GPS_NMEA_HEIGHT = 0x3c,          /* set,H: @@Af's */
  GPS_NMEA_IGNORE_LIST = 0x3d,     /* set,U: @@Am's, a hex mask */
  GPS_NMEA_POSITION_HOLD = 0x3e,   /* set,Z: @@At's */
  GPS_NMEA_CABLE_DELAY = 0x3f,     /* set,t: @@Az's */
  GPS_NMEA_RRS = 0x40,             /* rrs, queried with rrq */
  GPS_NMEA_CLEAR = 0x41,           /* clr */
  GPS_NMEA_INTERVAL_GGA = 0x42,    /* int,GGA: the sentence's interval */
  GPS_NMEA_INTERVAL_GSA = 0x43,    /* int,GSA */
  GPS_NMEA_INTERVAL_GSV = 0x44,    /* int,GSV */
  GPS_NMEA_INTERVAL_VTG = 0x45,    /* int,VTG */
  GPS_NMEA_INTERVAL_ZDA = 0x46,    /* int,ZDA */
  GPS_NMEA_INTERVAL_ANC = 0x47,    /* int,anc */
  GPS_NMEA_INTERVAL_RRM = 0x48,    /* int,rrm */
  GPS_NMEA_INTERVAL_TPS = 0x49,    /* int,tps */
  GPS_NMEA_SETTINGS_QUERY = 0x4a,  /* zzz, queried with srq */
  GPS_NMEA_RRQ = 0x4b,             /* zzz, queried with rrq */
  GPS_NMEA_SELFTEST = 0x4c,        /* int,tst00 */
  GPS_STEP_STATUS_SNAPSHOT = 0x4d, /* status_snapshot */
  GPS_STEP_PRINT_STATUS = 0x4e,
  GPS_STEP_PRINT_VISIBILITY = 0x4f,
  GPS_STEP_PRINT_EXTENSION = 0x50,
  GPS_STEP_PRINT_BEST_COMBINATIONS = 0x51,
  GPS_STEP_PRINT_ID = 0x52,
  GPS_STEP_PRINT_PPS = 0x53,
  GPS_STEP_PRINT_INSTRUMENT_STATE = 0x54,
  GPS_STEP_PRINT_TIME_RAIM = 0x55,
  GPS_STEP_PRINT_AVERAGE = 0x56,
  GPS_STEP_AVERAGE_RESET = 0x57, /* the average count = 0 */
  GPS_STEP_AVERAGE = 0x58,       /* the console's avg; nothing */
  GPS_STEP_GPS_HYSTERESIS = 0x59,
  GPS_STEP_EXTERNAL_HYSTERESIS = 0x5a,
  GPS_STEP_TRACKING_LOG = 0x5b,
  GPS_STEP_SURVEY_START = 0x5c,
  GPS_STEP_SURVEY_STOP = 0x5d,
  GPS_STEP_PPS_CHECK = 0x5e,
  GPS_STEP_RAIM_ARGUMENTS = 0x5f, /* raim_request_arguments */
  GPS_STEP_STATUS_MONITOR = 0x62,
  GPS_STEP_LINK_RESET = 0x64,          /* gps_link_reset */
  GPS_STEP_UTC_SELECT = 0x65,          /* UTC or GPS time, its argument; the
                                          Oncore's @@Aw and UTC_SET's */
  GPS_STEP_TIME_ARGUMENTS = 0x66,      /* 0x58506, before TIME_SET's ZDA */
  GPS_STEP_DATE_ARGUMENTS = 0x67,      /* 0x585c6, before DATE_SET's ZDA */
  GPS_STEP_LATITUDE_ARGUMENTS = 0x68,  /* 0x5862c(0), before a GLL */
  GPS_STEP_LONGITUDE_ARGUMENTS = 0x69, /* 0x5862c(1), before a GLL */
  GPS_SCRIPT_INIT_DEFAULTS = 0x6b,     /* clr, then INIT's; gps_init too */
  GPS_SCRIPT_INIT = 0x6c,              /* each sentence's interval, RAIM, rrs */
  GPS_SCRIPT_FIX_RESTART = 0x6d,       /* the PPS check, clr, RAIM, rrs */
  GPS_SCRIPT_ENGINE_SELFTEST = 0x6e,   /* empty */
  GPS_SCRIPT_OUTPUTS_ON = 0x6f,        /* the sentences' intervals */
  GPS_SCRIPT_OUTPUTS_OFF = 0x70,       /* empty */
  GPS_SCRIPT_QUERY_SETTINGS = 0x71,    /* empty */
  GPS_SCRIPT_DEFAULTS = 0x72,          /* clr */
  GPS_SCRIPT_UTC_SET = 0x73,           /* empty; translated to UTC_SELECT */
  GPS_SCRIPT_HOLD_AT = 0x74,           /* set,Z, GLL and set,H from its
                                          arguments, RAIM, rrs */
  GPS_SCRIPT_HOLD_RELEASE = 0x76,      /* survey stop, average reset, set,Z */
  GPS_SCRIPT_HOLD_OFF = 0x78,          /* set,Z, average reset */
  GPS_SCRIPT_HOLD_AT_QUIET = 0x79,     /* HOLD_AT's steps */
  GPS_SCRIPT_HOLD_SELECT = 0x7a,       /* set,Z from its arguments, RAIM, rrs */
  GPS_SCRIPT_HOLD_POSITION = 0x7b,     /* HOLD_AT's steps */
  GPS_SCRIPT_TRAIM = 0x7c,             /* int,rrm and rrs from its arguments */
  GPS_SCRIPT_TIME_SET = 0x7d,          /* TIME_ARGUMENTS, ZDA; @@Aa's */
  GPS_SCRIPT_DATE_SET = 0x7e,          /* DATE_ARGUMENTS, ZDA; @@Ac's */
  GPS_SCRIPT_LATITUDE_SET = 0x7f,      /* LATITUDE_ARGUMENTS, GLL; @@Ad's */
  GPS_SCRIPT_LONGITUDE_SET = 0x80,     /* LONGITUDE_ARGUMENTS, GLL; @@Ae's */
  GPS_REQUEST_NONE = 0xff,             /* a descriptor's, and the translation's,
                                          for none */
} gps_request_code;

/* A GPS request: the message pll_scpi_send_to_loop, gpsx_send and
 * gps_request_post (0x57a3e) build in 28 bytes of their frames and send
 * gpsx_exchange, which gpsm_task receives into 28 bytes (-0xce(A6),
 * 0x57e84), keeps in its ring, and gps_count_response (0x575da) sends
 * back to plla_exchange with the result, for pllp_task and
 * gps_selftest_run.  Two bytes longer than the others': before it
 * translates a request to the GT-74's (gps_request_translate),
 * gpsm_task keeps its code and argument mode at +0x14 and +0x16
 * (0x57fd2), and gps_count_response puts them back before sending it
 * back (0x57646). */
typedef struct gps_request_entry {
  uint32_t
      field_00; /* +0x00: the senders clear it; the kernel does not copy it */
  uint32_t field_04; /* +0x04: the senders clear it; kernel_send_for_task
                        refuses a nonzero one other than the exchange's id,
                        error 4 (0x25c26) */
  gps_request_sender
      sender; /* +0x08: one with the same sender waiting is not queued again
                 (request_ring_contains, 0x577e2); the monitors' may be replaced
                 (request_ring_newest_replaceable, 0x57828) */
  gps_request_result result; /* +0x09: 1 as sent; gps_count_response's kind as
                                it comes back, which pllp_task takes as
                                gps_ack_code or pll_scpi_complete's success */
  gps_request_code_word
      code; /* +0x0a: the request, gps_request_class's index;
               GPS_STEP_LINK_RESET, 0x64, empties the ring before it is queued
               (gps_request_queue, 0x5799e) */
  gps_argument_mode argument_mode; /* +0x0c: gps_request_class's second
                                      argument, copied to gps_request_step's */
  uint8_t field_0d;
  int32_t* arguments; /* +0x0e: four longs, which gpsm_task copies into
                         gps_request_step's */
  uint8_t sent; /* +0x12: gps_request_post's last argument (0x57a3e), 1 for the
                   status monitor's GPS_SCRIPT_OUTPUTS_ON, 0x6f (0x5737a); a
                   newest waiting request with it set is not replaced */
  uint8_t field_13;
  gps_request_code_word untranslated_code; /* +0x14: code as received */
  gps_argument_mode
      untranslated_argument_mode; /* +0x16: argument_mode as received */
  uint8_t field_17[5];
} gps_request_entry;

/* gpsm_task's ring of waiting GPS requests: six entries, as the 58503A
 * 3704's, of 28 bytes (request_ring_put's step and bound, +0xa8, at
 * 0x576ce), the oldest at read, the next free at write; empty when the
 * two are equal, so it holds five. */
typedef struct gps_request_ring {
  gps_request_entry entries[6];
  gps_request_entry* read;  /* +0xa8 */
  gps_request_entry* write; /* +0xac */
} gps_request_ring;

/* The GPS command gpsm_task is running, on its stack at -0x96(A6)
 * (gpsm_task's command local, 0x57fbc): the request it took from the
 * ring, where it is in the request's script, and the message it last
 * sent, which gps_response_match matches each response against and
 * command_log_add logs.  gps_last_command points at it.  0x74 bytes,
 * the 58503A 3704's with its request two bytes longer (0x5806a to
 * 0x581be). */
typedef struct gps_running_command {
  gps_request_entry request;     /* +0x00: copied from the ring's oldest */
  gps_script_step** script;      /* +0x1c: the step it runs; the next after each
                                    matched response, a null one ending the script */
  gps_request_code_word message; /* +0x20: the step's code, which
                                    gps_request_build built */
  gps_argument_mode query;       /* +0x22: the step's, as gps_request_build
                                    was given it; the log prints q, else c */
  uint8_t length;                /* +0x23: the built message's bytes */
  uint8_t sent[80];              /* +0x24: the message as sent, copied from
                                    gps_command_message */
} gps_running_command;

#include "../../common/types/time_of_day.h"

/* A GT-74 Command Log entry, this image's own: what command_log_add
 * takes of a request gpsm_task has sent, built in its frame and copied
 * whole (0x58808 to 0x5892c), and gps_print_command prints as
 * "%-5s %3d%-5s %3d  %-7.7s %-2s [%s]   %11s  %8s %s" (0x5893a).  0x16
 * bytes, the stride command_log_add steps by (0x588ec), where the
 * others' is 0x14: the sender first, and both of the request's codes. */
typedef struct gps_command_log_entry {
  gps_request_sender sender; /* +0x00: the request's; printed PLL, SCPI1,
                                SCPI2, GPSc and GPSs, else "?" */
  uint8_t field_01;          /* never written */
  gps_request_code_word untranslated_code; /* +0x02: the request's code as
                                              received, printed under "ID" */
  gps_request_code_word code;    /* +0x04: the request's code as translated,
                                    printed "->%3d" after the ID when it
                                    differs */
  gps_request_code_word message; /* +0x06: the sentence's code, the running
                                    command's, printed under "CID", its
                                    descriptor's address or query after */
  char argument_text[3];         /* +0x08: up to two characters of the sent
                                    text after the descriptor's address, to
                                    a NUL, '<' or '*'; NUL */
  gps_argument_mode query;       /* +0x0b: the command's; printed q, else c */
  uint8_t from_clock; /* +0x0c: 1 when the clock is set, the date and time its;
                         0 for a clock not set since power-up (get_date_time's
                         1), the date and time the status', local.  Printed
                         blank, else e */
  uint8_t field_0d;   /* never written */
  uint32_t date;      /* +0x0e: the clock's while from_clock, else the
                         status', local */
  time_of_day time;   /* +0x12: likewise */
} gps_command_log_entry;

/* The GT-74 Command Log, a ring at command_log, and its copy at
 * command_log_snapshot, which status_snapshot_update takes with the
 * snapshot at status_snapshot_flagged: 21 entries, the oldest at read,
 * the next free at write (command_log_clear, 0x587f8); empty when the two
 * are equal, and full at 20, when command_log_add drops the
 * oldest.  gps_print_command takes the oldest. */
typedef struct gps_command_log {
  gps_command_log_entry entries[21];
  gps_command_log_entry* read;  /* +0x1ce */
  gps_command_log_entry* write; /* +0x1d2 */
} gps_command_log;

#include "../../common/types/packed_datetime.h"

#include "../../common/types/lifetime_state.h"

/* The EEPROM's records ("EEPROM records" in structures.md), each
 * checksummed by record_checksum in its first two bytes, as their
 * writers build them on their stacks: erased to 0xff, then filled. */

#include "../../common/types/parameter_record.h"

#include "../../common/types/identification_record.h"

/* input_debounce_update's record for a DUART input it debounces, one for
 * each of input port bits 0 and 1 (0x1023a0, 0x1023a4; 0x36bcc to
 * 0x36bf0): the value read last, and how many reads after it agreed,
 * from 0, the value taken once it is past 5. */
typedef struct input_debounce {
  uint8_t value;
  uint8_t field_1; /* nothing refers to it */
  int16_t count;   /* +2 */
} input_debounce;

/* The reader input_debounce_update debounces: the input's value, read
 * once (duart_input_0_debounced, duart_input_1_debounced). */
typedef uint8_t input_debounce_reader(void);

/* What adc_averages_print prints through: :DIAGnostic:ADC? passes
 * duartb_printf for a request from source 0 and printf for any other
 * (0x3eaca). */
typedef void adc_average_printer(char* format, ...);

/* The OCXO's port E link, which the code calls the puck's, at 0x102374
 * (make record-access; FUN_00036e84 sets it up, 0x36ebe to 0x36f24): a
 * frame hmon_task's pass shifts in a bit at a time (FUN_0003658e), checked
 * by its sum (FUN_00036502), the reading and status it carries
 * (FUN_0003622c), and the EFC correction drawn from the reading
 * (FUN_0003607c, FUN_00036350).  Named by what the code does with each
 * field, or by the puck word's text where it prints one (0x35fa0). */
typedef struct puck_link {
  int32_t efc_adc_expected;   /* +0x00: pll.efc times :DIAG:ROSC:EFC:DADC, as
                                 an integer (0x36386), the reading the ADC
                                 should give */
  float correction;           /* +0x04: added to pll.efc while efc_compensation
                                 is set, "corr= %f" (0x3610e, 0x36130) */
  float efc_written;          /* +0x08: pll.efc, plus correction while
                                 efc_compensation is set; efc_write_dithered's */
  uint8_t correction_restart; /* +0x0c: the next correction is the error over
                                 :DADC, not a thousandth of it added
                                 (0x360e8) */
  uint8_t field_0d;           /* nothing refers to it */
  uint32_t frames_since_efc;  /* +0x0e: frames since the EFC was set, from 0
                                 (0x36364); the correction waits for 3, or 1
                                 in n3 lock pll's pll state */
  uint8_t adc_error_logged;   /* +0x12: "EFC ADC out of range" logged; the
                                 puck word's "puck EFC ADC err" */
  uint8_t alive;              /* +0x13: a good frame came before alive_until;
                                 "puck communication is alive" */
  uint32_t alive_until;       /* +0x14: the uptime alive lapses at, 10 s past
                                 the last good frame */
  uint8_t frame[7];           /* +0x18: a frame as shifted in, its first byte
                                 0x55 or 0xa1, its last its sum */
  uint8_t frame_length;       /* +0x1f: 6 after 0x55, 7 after 0xa1 */
  uint8_t header;             /* +0x20: the last good frame's first byte */
  uint8_t status;             /* +0x21: a 0x55 frame's fifth byte; bit 0 "puck
                                 warm" (0x36054) */
  uint8_t status1;            /* +0x22: the same byte, "status1 byte" */
  uint8_t status2;            /* +0x23: cleared by a 0x55 frame, "status2
                                 byte" */
  uint8_t framing;            /* +0x24: a frame's first byte has come, its
                                 bits are counted */
  uint8_t field_25;           /* nothing refers to it */
  uint8_t* cursor;            /* +0x26: the frame's byte being shifted in */
  uint8_t bits;               /* +0x2a: the bits of it shifted in, to 8 */
  uint8_t field_2b;           /* nothing refers to it */
} puck_link;

/* The calibration block at 0x400080: save_calibration_block's
 * TC, time offset and cable delay, the 58503A 3704's, and this image's
 * EFC calibration after them (0x44fbe to 0x44fdc, 0x450ae to 0x450be). */
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
  uint8_t unused[40]; /* +0x18: left erased */
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

/* What doprnt writes its output through, as fwrite is called: the
 * text, the size of an element, the count, and the output; sprintf's
 * is sprintf_put (0x608c4).  The count written comes back. */
typedef int32_t sprintf_put_function(char* text, int32_t size, int32_t count,
                                     sprintf_output* output);

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

/* One of port_error_reports' four entries, which scpi_session_init
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
 * shows the cause of. */
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
                                 (0x43376) */
  STATUS_CONDITION_37 = 0x37, /* does nothing; status_happening clears it
                                 entering lock (0x4334e) */
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
                                     current's filter, monitor_filter_add */
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
  uint16_t correction_age; /* 0.1 s, of differential corrections */
  int32_t ecef_x;          /* cm */
  int32_t ecef_y;
  int32_t ecef_z;
  uint8_t ura[8]; /* per channel; this image has no @@Bk or @@Ek decoder */
  int16_t least_squares_inverse[10]; /* A-1, upper triangle by rows */
  int16_t clock_bias;                /* 0.1 m */
  uint16_t oscillator_offset;        /* 0.1 m/s */
} oncore_extension_fields;

/* The EFC history: a ring of 32 averages of the EFC, one each 2700
 * samples, which the holdover prediction draws on.  Its routine's debug
 * message names its arrays: "e_avg= time= weight= log= tfom=".  This
 * image's own, 0x354 bytes, each field where efc_history_update
 * (0x4a588) and the fits reach it: the ring is 32 entries, its pointers
 * stepped modulo 32 (0x4a902), where the 58503A 3704's is 64; the
 * entry a new one replaces is kept after each array but the logs'
 * (0x4a6de to 0x4a6fa), and each entry's fit is kept in three arrays
 * more after the logs (measured_uncertainty_update, 0x4a1a2). */
typedef struct efc_ring {
  uint8_t start_ptr;       /* +0x000: the oldest entry */
  uint8_t stop_ptr;        /* +0x001: the next to fill */
  uint8_t loop_settled;    /* +0x002: startup_pll sets it on settling without a
                              fit; aging_state then goes to 3 */
  uint8_t field_003;       /* nothing refers to it */
  uint16_t sample_count;   /* +0x004: samples toward this entry, to 2700 */
  int16_t field_006;       /* nothing refers to it */
  uint16_t summed_count;   /* +0x008: of those, summed */
  float averages[32];      /* +0x00a: e_avg, efc_sum / summed_count */
  float replaced_average;  /* +0x08a: averages[stop_ptr] before the entry is
                              filled again; nothing reads it */
  uint8_t weights[32];     /* +0x08e: weight, 1 to 3 */
  uint8_t replaced_weight; /* +0x0ae: weights[stop_ptr], likewise */
  uint8_t field_0af;       /* nothing refers to it */
  float times[32];         /* +0x0b0: time */
  float replaced_time;     /* +0x130: times[stop_ptr], likewise */
  float logs[32];          /* +0x134: log */
  float fit_a_history[32]; /* +0x1b4: aging_curve.a at aging_curve.newest_index,
                              by measured_uncertainty_update */
  float fit_b_history[32]; /* +0x234: aging_curve.b */
  float fit_c_history[32]; /* +0x2b4: aging_curve.c */
  uint32_t next_entry_uptime; /* +0x334: the uptime the next entry is due at,
                                 2700 on each (0x4a5f6, 0x4a6c6) */
  float efc_sum; /* +0x338: the EFC, less its temperature correction, summed */
  int32_t field_33c; /* nothing refers to it */
  float drift;       /* +0x340: pll_rep's current drift */
  int32_t field_344; /* nothing refers to it */
  float fit_a;       /* +0x348: the curv task's aging fit a + b y + c ln y, y =
                        samples / 2700 */
  float fit_b;       /* +0x34c */
  float fit_c;       /* +0x350 */
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

/* @@Ba's record, the 58503A 3704's, a pad byte before the hour
 * (time_of_day's own) putting the longs and words on even addresses;
 * here the GT-74's commits fill it (gps_nmea.c), and the GSA commit puts
 * the DOP type where the 58503A's has its second pad (0x5cfd8), the
 * byte after unread. */
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
  oncore_dop_type dop_type;
  uint8_t field_23;
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

/* The Time RAIM record, the 58503A's @@Bn record as this image lays it
 * out, which no message fills: the GT-74's rsd and rrm sentences do,
 * through their commits (gps_nmea.c), and gps_request_build
 * stores the rate it requests (0x5da54).  Its readers address it from
 * gps.raim (gps_print_time_raim, 0x53be4; raim_monitor, 0x56b84): the
 * alarm limit a long, 10 ns (0x53dac), where the 58503A's is a word of
 * 100 ns after a pad, so that from pps_mode on each field is a byte
 * further in; the sawtooth at +0x1a as in the others (gps.md, "Where the
 * sawtooth goes").  Nothing reads its channels' place by offset. */
typedef struct oncore_raim_fields {
  uint8_t rate;             /* +0x00: as last requested */
  uint8_t raim_on;          /* +0x01: rsd's */
  int32_t alarm_limit;      /* +0x02: rsd's, 10 ns */
  oncore_pps_mode pps_mode; /* +0x06: rsd's */
  uint8_t field_07[0x15 - 0x07];
  oncore_pulse_sync pulse_sync;         /* +0x15 */
  oncore_raim_solution solution_status; /* +0x16: rrm's */
  oncore_raim_status raim_status;       /* +0x17: rrm's */
  uint16_t sigma;  /* +0x18: ns, formatted and not printed (0x53dda) */
  int8_t sawtooth; /* +0x1a: rrm's, ns */
  uint8_t field_1b[0x4c - 0x1b];
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

/* A reference's validity, gps_reference_record.valid, from its state:
 * :SOURce:GPSystem:REFerence:VALid? answers 1 for valid with its 1PPS
 * alive, or questionable (0x41088). */
typedef enum __attribute__((packed)) gps_reference_validity {
  GPS_REFERENCE_INVALID = 0,
  GPS_REFERENCE_VALID = 1,        /* GPS_SYNC_VALID, or forced valid */
  GPS_REFERENCE_QUESTIONABLE = 2, /* GPS_SYNC_QUESTIONABLE and _CLEARING */
} gps_reference_validity;

/* One reference's validation, the GPS's or the external 1PPS's: in
 * the 58503A 3704, gps_receiver_status_update's (0x4b8a2) state for
 * each, gps.references[index]; 0x4b82a sets valid from override and
 * state to 1, 0x4b858 and 0x4b882 the GPS's and the external's
 * hysteresis.  22 bytes (0x101394 and 0x1013aa, 0x4b830). */
typedef struct gps_reference_record {
  gps_reference_validity valid;    /* 1 at state 9, 2 at 7 and 8, else 0; while
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

/* gps.field_592's bits in this image, by what sets and tests them: the
 * receiver ID's commit (0x5c00e) sets the ID's and 0x40 with the ID, the
 * time RAIM's unless the engine's software is 0x4a01c1 at revision 2,
 * and clears all but the eight channels' without it; 0x5bfee sets the
 * eight channels'.  gps_print_time_raim tests the time RAIM's (0x53c02),
 * the tps commit 0x40 for the hold height's type (0x5d1e0),
 * gps_message_supported 0x08 and the time RAIM's, and
 * oncore_filter_eight_channels 0x04, which nothing sets. */
typedef enum __attribute__((packed)) gps_field_592_bit {
  GPS_FIELD_592_BIT_2 = 0x04,
  GPS_FIELD_592_BIT_3 = 0x08, /* the ID's commit clears it; nothing sets it */
  GPS_FIELD_592_ID_SEEN = 0x10,
  GPS_FIELD_592_TIME_RAIM = 0x20,
  GPS_FIELD_592_BIT_6 = 0x40,
  GPS_FIELD_592_EIGHT_CHANNELS = 0x80,
} gps_field_592_bit;

/* A sentence field decoder's result (nmea_decimal_parse, 0x5c56e, and
 * the decoders a sentence's table names). */
typedef enum __attribute__((packed)) nmea_field_status {
  NMEA_FIELD_GOOD = 0,
  NMEA_FIELD_EMPTY = 1, /* no value, or not the field's letter */
  NMEA_FIELD_BAD = 2,   /* not all digits */
} nmea_field_status;

/* A sentence field's decoder: the field's text and the place its entry
 * gives, its record's for the commit; the field's status, which the
 * dispatch ignores (nmea_sentence_decode, 0x5dbca). */
typedef nmea_field_status nmea_field_decoder(char* field, void* place);

/* A sentence's field as nmea_sentence_decode takes it, each list ending
 * at an index of 0, the sentence's name, whose decoder, if any, commits
 * the sentence (0x5e096 to 0x5e320). */
typedef struct nmea_field_entry {
  int8_t index;  /* +0x00: the field's number, the name 0 */
  int8_t length; /* +0x01: its characters where positive, else any */
  nmea_field_decoder* decode; /* +0x02: none for NULL */
  void* place;                /* +0x06 */
} nmea_field_entry;

/* A sentence nmea_sentence_decode takes, nmea_sentences (0x5e32a), the
 * table ending at a NULL name. */
typedef struct nmea_sentence_entry {
  char* name;    /* +0x00: after "GP" */
  int8_t length; /* +0x04: its characters to the LF where positive, else
                    any */
  int8_t fields; /* +0x05: its commas where positive; where negative,
                    any for all but GSV (0x5d8be) */
  const nmea_field_entry* fields_decoded; /* +0x06: none for NULL */
} nmea_sentence_entry;

/* An Oncore request's GT-74 one, gps_request_translations (0x5e6d4),
 * which gps_request_translate (0x5d7a8) and gps_request_descriptor
 * (0x5c0f6) look up by code, the
 * table ending at code GPS_REQUEST_NONE, whose to_code is too. */
typedef struct gps_request_translation {
  gps_request_code_word code;      /* +0x00: below GPS_NMEA_MASK_ANGLE */
  gps_argument_mode argument_mode; /* +0x02 */
  uint8_t field_03;
  gps_request_code_word to_code;      /* +0x04 */
  gps_argument_mode to_argument_mode; /* +0x06 */
  uint8_t field_07;
} gps_request_translation;

/* A sentence descriptor's kind: the two gps_request_build builds
 * (0x5d97a, 0x5d982), and none, for which it builds an empty sentence.
 * Every entry of gps_sentence_descriptors (0x5e3cc), codes 0x39 to
 * 0x4c, zzz's and int,tst00's among them, is GPS_SENTENCE_BUILT: no
 * valid code's descriptor has GPS_SENTENCE_NONE, only a code past the
 * table, whose descriptor is whatever bytes follow it. */
typedef enum __attribute__((packed)) gps_sentence_kind {
  GPS_SENTENCE_NONE = 0,
  GPS_SENTENCE_BUILT = 1,
  GPS_SENTENCE_BUILT_TOO = 3 /* no entry has it */
} gps_sentence_kind;

/* A sentence's argument encoder, a gps_sentence_descriptor's: the
 * request's argument longs, the argument's index and where its field
 * goes; returns past it (gps_nmea.c's encoders). */
typedef char* gps_sentence_encoder(int32_t* arguments, uint8_t index,
                                   char* text);

/* Whether a sentence may be sent, a gps_sentence_descriptor's test:
 * gps_request_build passes the code, the first encoder and the
 * arguments (0x5d95a), which gps_time_raim_supported, the only one,
 * reads none of. */
typedef uint8_t gps_sentence_test(gps_request_code_word code,
                                  gps_sentence_encoder* encoder,
                                  int32_t* arguments);

/* How gps_response_match takes a response to a sentence sent, a
 * gps_sentence_descriptor's match kind (its jump table at 0x5dc4e);
 * named by the sentences whose descriptors have it.  A sentence of a
 * kind that compares is answered at once when only queried
 * (GPS_ARGUMENTS_QUERY). */
typedef enum __attribute__((packed)) gps_sentence_match_kind {
  GPS_SENTENCE_MATCH_NONE = 0,    /* ZDA and int: gpsm_task awaits none */
  GPS_SENTENCE_MATCH_TIMEOUT = 1, /* clr: no response answers it, and its
                                     timeout ends the step (0x582bc) */
  GPS_SENTENCE_MATCH_GLL = 2,     /* GLL: GGA, its latitude and longitude
                                     within 12 mas of the sent while the
                                     position is held */
  GPS_SENTENCE_MATCH_RRS = 3,     /* rrs: rsd, its three fields the sent */
  GPS_SENTENCE_MATCH_RRQ = 4,     /* rrq, rrs' query: rsd */
  GPS_SENTENCE_MATCH_SET = 5,     /* set: ssd, the field of the set's letter
                                     the sent, or a height within 10 of it
                                     while the position is held */
  GPS_SENTENCE_MATCH_SRQ = 6,     /* srq, set's query: ssd */
  GPS_SENTENCE_MATCH_TST = 7,     /* int,tst00: tst */
} gps_sentence_match_kind;

/* A GT-74 sentence's descriptor, one of gps_sentence_descriptors
 * (0x5e3cc), by its code less GPS_NMEA_MASK_ANGLE: 0x20 bytes, which
 * gps_request_descriptor (0x5c0f6) indexes from 0x5dcac by 0x20 times
 * the code, an Oncore request's translated first. */
typedef struct gps_sentence_descriptor {
  char* address;          /* +0x00: after "$", "PFEC," for a lowercase one, and
                             "GP" (0x5d994 to 0x5d9ce) */
  gps_sentence_kind kind; /* +0x04 */
  uint8_t field_05;
  gps_sentence_encoder* encoders[4]; /* +0x06: each argument's, to the first
                                        NULL */
  char* query;     /* +0x16: what a query sends after "GP"; NULL none */
  uint8_t timeout; /* +0x1a: gps_response_timed_out's (0x575c6) */
  gps_sentence_match_kind match_kind; /* +0x1b: 0 none awaited (0x58232) */
  gps_sentence_test* test; /* +0x1c: whether it may be sent; NULL always */
} gps_sentence_descriptor;

/* A sentence sent as it stands, its text and length as drta_send_message
 * takes them: gps_periodic_send's two, which it copies to its frame
 * (0x57d26, 0x57d72). */
typedef struct gps_fixed_sentence {
  char* text;     /* +0x00 */
  uint8_t length; /* +0x04: the text's, without a NUL */
  uint8_t pad_5;
} gps_fixed_sentence;

/* gps_periodic's stage: what gps_periodic_send sent last, by the case
 * each value takes (0x57d0e to 0x57d14); any other value sends nothing. */
typedef enum __attribute__((packed)) gps_periodic_stage {
  GPS_PERIODIC_GSV = 0,   /* int,GSV00 or a query, counting to int,anc00 */
  GPS_PERIODIC_ANC = 1,   /* int,anc00 */
  GPS_PERIODIC_QUERY = 2, /* a setting's query */
} gps_periodic_stage;

/* What gps_periodic_send, which tps's commit calls (0x5d118), keeps
 * between calls, each sending one sentence: the query of the setting
 * gpsm_task sent last, which it leaves in query_code when the setting's
 * descriptor has a query (0x58218 to 0x58220), else int,GSV00, and
 * int,anc00 after 59 sends.  gps_init sets it up (0x57c2c to
 * 0x57c38); 8 bytes, to status_screen_print_interval. */
typedef struct gps_periodic_state {
  gps_periodic_stage stage;         /* +0x00 */
  uint8_t field_01;                 /* nothing reaches it */
  gps_request_code_word query_code; /* +0x02: the setting to query, or
                                       GPS_REQUEST_NONE */
  uint8_t queries;  /* +0x04: the setting's queries sent, from 1; at most 4 */
  uint8_t field_05; /* nothing reaches it */
  uint16_t sends;   /* +0x06: the sends since int,anc00, to 59 */
} gps_periodic_state;

/* The GT-74's sentences, each as its decoders' table (0x5e32a) fills it
 * in gps, field by field: each field's decoder writes the place its
 * entry names, and the commit at the table's end, where one is, copies
 * them into the 58503A's records (gps_nmea.c).  Named by what the
 * commits make of them; the field's number in the sentence, counting
 * its name as 0, after each. */

/* GGA, position (0x5e096). */
typedef struct nmea_gga_fields {
  char latitude_hemisphere; /* 3: 'S' negates it (0x5ceae) */
  uint8_t field_01;
  int32_t latitude;          /* 2: mas */
  char longitude_hemisphere; /* 5: 'W' negates it */
  uint8_t field_07;
  int32_t longitude;     /* 4: mas */
  int32_t height;        /* 9: cm, the commits' height above mean sea level */
  int32_t height_offset; /* 11: cm, added to make the height above the
                            ellipsoid; nmea_encode_height subtracts it */
  uint8_t satellites;    /* 7 */
  uint8_t angle_decoded; /* set by each angle's decoder (0x5cb54), cleared
                            by the tps commit (0x5d206) */
} nmea_gga_fields;

/* GSA, the fix and its DOPs (0x5e0e6). */
typedef struct nmea_gsa_fields {
  uint8_t fix; /* 2: 2 and 3 a 2D and a 3D fix (0x5cfb4) */
  uint8_t field_01;
  uint32_t satellites; /* 3 to 14: a satellite set */
  uint16_t pdop;       /* 15: hundredths */
  uint16_t hdop;       /* 16: hundredths */
} nmea_gsa_fields;

/* GSV: a satellite in view, four to a sentence. */
typedef struct nmea_gsv_satellite {
  uint8_t satellite; /* 0 for none */
  uint8_t elevation; /* degrees */
  uint16_t azimuth;  /* degrees */
  uint8_t snr;
  uint8_t field_05;
} nmea_gsv_satellite;

/* GSV, the satellites in view (0x5e118): each sentence's four in
 * incoming, gathered into satellites by its last decoder (0x5ce4a), the
 * first sentence restarting them (0x5ce22). */
typedef struct nmea_gsv_fields {
  uint8_t count;    /* those gathered */
  uint8_t sentence; /* 2: the sentence's number, from 1 */
  nmea_gsv_satellite satellites[8];
  nmea_gsv_satellite incoming[4]; /* 4 to 19 */
} nmea_gsv_fields;

/* anc (0x5e1cc). */
typedef struct nmea_anc_fields {
  oncore_date_fields date; /* 2, with time: ddmmyyhhmmss (0x5ccd6) */
  time_of_day time;
  uint32_t satellites; /* 3: one bit a satellite from a '1' (0x5ccf4) */
} nmea_anc_fields;

/* ssd, the settings (0x5e1ea), each field after its letter. */
typedef struct nmea_ssd_fields {
  uint8_t mask_angle;          /* 7: M, gps.mask_angle's */
  uint8_t field_z;             /* 13: Z, a digit */
  uint32_t ignored_satellites; /* 10: U, hexadecimal */
  int32_t height;              /* 6: H, cm */
  int32_t cable_delay;         /* 14: t, plus gps.cable_delay_correction */
} nmea_ssd_fields;

/* tst, the self-test and the software (0x5e226). */
typedef struct nmea_tst_fields {
  uint8_t result;         /* 2: a digit, 0 for a result in self_test_bits */
  uint8_t field_01;       /* 4: a hexadecimal digit */
  uint8_t self_test_bits; /* 5: a hexadecimal digit, bits 0 and 1 */
  uint8_t field_03;
  uint32_t software_part_number; /* 3: its first seven digits */
  uint16_t software_version;     /* 3: the rest */
} nmea_tst_fields;

/* tps' time mode, nmea_tps_fields.time_mode and the status snapshot's
 * copy: 2 UTC, 3 GPS time from its week and seconds, which tps' commit
 * takes (0x5cc5e, 0x5d41c); the status screen marks a time not from
 * the clock "[?]" for 1 and "(?)" for any other, and takes 3 for UTC
 * (0x5556a to 0x555e6).  What 1 is is not established. */
typedef enum __attribute__((packed)) nmea_time_mode {
  NMEA_TIME_MODE_1 = 1,
  NMEA_TIME_MODE_UTC = 2,
  NMEA_TIME_MODE_GPS = 3,
} nmea_time_mode;

/* tps, the time (0x5e258), whose commit commits the others (0x5d41c). */
typedef struct nmea_tps_fields {
  uint8_t field_00;         /* 4: a digit */
  nmea_time_mode time_mode; /* 3 */
  oncore_date_fields date;  /* 2, with time */
  time_of_day time;
  uint16_t week;      /* 10 */
  int32_t seconds;    /* 11: of the week */
  uint8_t utc_offset; /* 8: GPS less UTC, s, gps_leap_ack's (0x50794) */
  int8_t leap_second; /* 7: -1, 0 or 1 */
  oncore_date_fields leap_date; /* 6, with leap_time */
  time_of_day leap_time;
  oncore_date_fields date_9; /* 9, with time_9 */
  time_of_day time_9;
  uint8_t position_hold; /* 5: 1 off, 2 on */
  uint8_t field_23;
  oncore_date_fields gps_date; /* from week and seconds (0x5cc5e) */
  time_of_day gps_time;
} nmea_tps_fields;

/* gps.status_received: whether the tps sentences' status has come, and
 * lately, as the status monitor keeps it (command_monitor, 0x57322 and
 * 0x573da), the 58503A 3704's; channel_position_satellite takes a
 * channel's satellite only while it is fresh (0x5b880). */
typedef enum __attribute__((packed)) gps_status_freshness {
  GPS_STATUS_NONE = 0,  /* none since the monitor last failed */
  GPS_STATUS_STALE = 1, /* none for three of its periods */
  GPS_STATUS_FRESH = 2,
} gps_status_freshness;

/* rrm, the time RAIM's result (0x5e2c6). */
typedef struct nmea_rrm_fields {
  oncore_raim_solution solution_status; /* 2 */
  oncore_raim_status raim_status;       /* 3 */
  uint32_t satellites;                  /* 4 to 7: a satellite set */
  int8_t sawtooth;                      /* 9: ns */
  uint8_t field_07;
} nmea_rrm_fields;

/* rsd, the time RAIM's settings (0x5e2f8). */
typedef struct nmea_rsd_fields {
  uint8_t raim_on;          /* 2 */
  oncore_pps_mode pps_mode; /* 4 */
  uint16_t alarm_limit;     /* 3: 10 ns */
} nmea_rsd_fields;

/* The GPS task's state, gps (0x100f2c): the 58503A 3704's records, which
 * its carried code reads, at this image's own offsets, and the GT-74's
 * sentences, from which the commits fill them.  Cleared as one,
 * memset(&gps, 0, 0x678) (gps_init, 0x57c14), and addressed
 * through a base register.  Each field is where this image's code
 * reaches it, named as the 58503A's field of the instruction image_diff
 * aligns with it; field_NNN, by offset, where none does.  Against the
 * 58503A's: the cable delay correction takes two bytes before
 * status_received; the rrm sentence's satellites and a byte before the
 * xDOP table four more; the time RAIM record, a byte longer, moves
 * raim_received after it; and the sentences follow the receiver ID, so
 * that from field_5ee on each field is 0xd0 bytes further in. */
typedef struct gps_state {
  uint8_t field_000[0x1c];
  uint8_t mask_angle; /* the ssd commit's (0x5d0e8) */
  uint8_t field_01d[0x2e - 0x1d];
  uint32_t ignored_satellites; /* the ssd commit's */
  uint8_t field_032[0xa8 - 0x32];
  oncore_hold_position_fields hold_position; /* the tps commit's */
  oncore_height_type hold_height_type;       /* the tps commit's */
  uint8_t field_0b5;
  uint8_t position_hold; /* the tps commit's */
  uint8_t field_0b7[0xbe - 0xb7];
  oncore_time_mode utc_time;
  uint8_t field_0bf[0xc2 - 0xbf];
  int32_t pps_offset;
  int32_t pps_cable_delay;        /* the ssd commit's, negated (0x5d0f6) */
  int32_t pps_cable_delay_given;  /* ssd's t as the engine gives it
                                     (0x5cdec) */
  int16_t cable_delay_correction; /* the engine's software needs, -400
                                     for software 0x4a01b6 at revision 8,
                                     else 0 (0x5c05e, 0x5c066) */
  uint8_t field_0d0[0xd4 - 0xd0];
  gps_status_freshness status_received; /* command_monitor sets it */
  uint8_t status_rate;                  /* as last requested (0x5da4a) */
  oncore_status_fields status;          /* the commits' */
  uint8_t field_11d;
  uint8_t visibility_received; /* set by the GSV commit (0x5d0d2) */
  uint8_t field_11f;
  oncore_visibility_fields visibility; /* the GSV commit's */
  uint32_t raim_satellites; /* rrm's (0x5d252), the set satellite_status
                               gives satellite_track_class past its
                               visibility record (0x54b58) */
  uint8_t field_19e;
  oncore_xdop_table_fields xdop_table; /* nothing fills it */
  uint8_t field_1ea[2];
  oncore_almanac_status_fields almanac; /* nothing fills it */
  uint8_t field_1fc[0x2ba - 0x1fc];
  oncore_corrections_fields corrections; /* nothing fills it */
  double correction_average;             /* m, printed AVG */
  double correction_deviation;           /* m, printed STD DEV */
  uint8_t field_30a[2];
  oncore_raim_fields raim;
  uint8_t raim_received; /* command_monitor sets it (0x57456) */
  uint8_t field_359;
  oncore_leap_pending leap_second_pending; /* the tps commit's */
  uint8_t field_35b;
  uint16_t self_test; /* the tst commit's, failed tests */
  uint8_t field_35e[0x364 - 0x35e];
  oncore_extension_fields extension; /* nothing fills it */
  uint8_t field_3a4[0x40e - 0x3a4];
  oncore_fix_mode fix_mode; /* gps_init sets it */
  uint8_t field_40f;
  uint8_t receiver_id_received;   /* set by the tst commit (0x5d33a) */
  oncore_receiver_id receiver_id; /* the tst commit's */
  uint8_t field_526[2];
  nmea_gga_fields gga;    /* +0x528 */
  nmea_gsa_fields gsa;    /* +0x53e */
  nmea_gsv_fields gsv;    /* +0x548 */
  nmea_anc_fields anc;    /* +0x592 */
  nmea_ssd_fields ssd;    /* +0x59e */
  nmea_tst_fields tst;    /* +0x5ac */
  nmea_tps_fields tps;    /* +0x5b6 */
  nmea_rrm_fields rrm;    /* +0x5e2 */
  nmea_rsd_fields rsd;    /* +0x5ea */
  uint8_t field_5ee[2];   /* gps_init clears the first */
  uint32_t average_count; /* positions averaged */
  double average_latitude;
  double average_longitude;
  double average_height_gps;
  double average_height_msl;
  gps_reference_record references[2]; /* the GPS's, then the external
                                         1PPS's */
  uint8_t surveying;
  uint8_t field_641; /* the tps commit's, its ninth field's date set */
  gps_raim_assessment raim_assessment;
  uint8_t pps_check_disabled;
  uint8_t receiver_health_failed;
  uint8_t field_645;
  uint8_t command_monitor_enabled;
  gps_command_monitor_state command_monitor_state;
  gps_script_step** command_monitor_query;
  uint32_t command_monitor_time; /* uptime of its last state change */
  uint8_t command_monitor_error;
  uint8_t command_monitor_failed;
  uint8_t status_monitor_enabled;
  gps_status_monitor_state status_monitor_state;
  uint32_t status_monitor_time;
  time_of_day status_last_time; /* status.time as command_monitor last saw
                                   it (0x57420, 0x57434) */
  uint8_t status_repeat_count;
  uint8_t status_monitor_timed_out;
  uint8_t status_monitor_failed;
  uint8_t field_65f;
  uint8_t channel_count; /* 8 once 0x5bfee has run */
  uint8_t field_661;
  gps_field_592_bit field_592;
  uint8_t field_663;
  uint8_t tracking_log;
  uint8_t tracking_log_active;
  uint32_t track_words[3];      /* gps_track_words_pack's */
  uint32_t channel_word;        /* gps_channel_word_pack's */
  uint16_t channel_word_lowest; /* gps_channel_word_lowest's */
} gps_state;

/* An entry of fpga_bit_table (0x49eae, eight entries), which fpga_set_bit
 * and fpga_set_field take: a register of fconf_regs and its mask.  The
 * shared prefix's names are common/types/fpga_bit_index.h's; the three
 * below are the Z3815A's own, read from the table, in registers 2 and 3
 * where the 58503A's alarm flag is register 3's.  fpga_set_bit and
 * fpga_set_field take only the first seven; the eighth, register 3's low
 * two bits, is what counter_set_sync writes directly, and
 * fpga_fields_to_registers (0x4965e) and counter_configuration_init
 * (0x49a62) run over all eight.  The non-shared bits' semantics are not
 * established (docs/firmware/z3815a-4010.md, "Not established"). */
typedef enum __attribute__((packed)) fpga_bit_index {
#include "../../common/types/fpga_bit_index.h"
  FPGA_BIT_SYNC = 5,  /* register 2, 0x01, a flag: the 1PPS sync bit 0x516de
                         pulses (docs/firmware/z3815a-4010.md) */
  FPGA_BIT_ALARM = 6, /* register 2, 0x0c, a field: set while arming through
                         0x494f2 */
  FPGA_BIT_COUNTER_SYNC = 7, /* register 3, 0x03, a field: counter_set_sync's
                                sync kind, which it writes directly */
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

/* pll_normal's states, pll.normal_state; any other starts it again. */
typedef enum __attribute__((packed)) pll_normal_state {
  PLL_NORMAL_RUNNING = 1,
} pll_normal_state;

/* The n3 lock pll stage's states, pll.n3_lock_state, as diag_pr_pll
 * names them (0x2c3bc); it names no 11, which the stage passes through
 * from PLL_N3_LOCK_SLEW to PLL_N3_LOCK_PLL (0x52130). */
typedef enum __attribute__((packed)) pll_n3_lock_state {
  PLL_N3_LOCK_WAIT1 = 0,
  PLL_N3_LOCK_WAIT2 = 1,
  PLL_N3_LOCK_SYNC1 = 2,
  PLL_N3_LOCK_SYNC2 = 3,
  PLL_N3_LOCK_LEAPSEC = 4,
  PLL_N3_LOCK_CHECK_TIME = 5,
  PLL_N3_LOCK_MEASURE1 = 6,
  PLL_N3_LOCK_MEASURE2 = 7,
  PLL_N3_LOCK_MEASURE3 = 8,
  PLL_N3_LOCK_CALC_FREQ = 9,
  PLL_N3_LOCK_SLEW = 10,
  PLL_N3_LOCK_STATE_11 = 11,
  PLL_N3_LOCK_PLL = 12,
  PLL_N3_LOCK_EXIT1 = 13,
  PLL_N3_LOCK_EXIT2 = 14,
} pll_n3_lock_state;

/* The n3 lock pll stage's own state, apart from pll, at 0x102d60
 * (make record-access; n3_lock_pll sets it up, 0x51a34 to 0x51aee), each
 * field named by what the stage does with it or by its debug text. */
typedef struct n3_lock_fields {
  int32_t phase;            /* +0x00: the pForth word n3_phase's (0x2c6b8), in
                               ps; the phase offset's measure takes it off */
  uint8_t seating_error;    /* +0x04: "There is a card seating error" logged,
                               n3_lock_seating_error's result */
  uint8_t field_05;         /* nothing refers to it */
  int32_t pll_passes;       /* +0x06: the passes the loop steers, 60 */
  int32_t phase_passes;     /* +0x0a: the passes after them that measure the
                               phase offset, 1 */
  float phase_offset;       /* +0x0e: "s3_phase_offset", filtered */
  float frequency_sum;      /* +0x12: the loop's integral of the TI average */
  uint8_t averaging;        /* +0x16: the first of two TI readings is in
                               ti_average */
  uint8_t field_17;         /* +0x17: cleared, never read */
  uint16_t efc_limit_count; /* +0x18: n3_lock_efc_set's passes at an EFC
                               limit, to 20 */
  float gain_ti;            /* +0x1a: efc_gains.reciprocal_ti / 2.5 */
  float gain_tau;           /* +0x1e: efc_gains.reciprocal_tau over 2.5 twice
                               and 4 */
  float frequency_offset_1; /* +0x22: "freq_off"'s second, ti0 - ti1 over 2 */
  float frequency_offset_2; /* +0x26: its third, ti1 - ti2 over 2 */
  float slew_offset; /* +0x2a: 0x517e0's "curr offset", which nothing calls */
  uint32_t passes;   /* +0x2e: counted by 2 a call; past 10 pll.field_3e9
                        is cleared */
  uint8_t reading_valid;  /* +0x32: the last reading valid */
  uint8_t field_33;       /* nothing refers to it */
  uint16_t pass;          /* +0x34: the pass within pll_passes and
                             phase_passes */
  uint16_t seating_count; /* +0x36: TI averages between 1 and 3 ns in a
                             row, to 300 */
  float ti_average;       /* +0x38: two TI readings less the phase offset,
                             averaged */
  uint16_t field_3c;      /* +0x3c: cleared, never read */
} n3_lock_fields;

/* The OCXO calibration's steps, efc_calibration.step, as
 * efc_calibration_measure's switch takes them (0x52a90); any other starts it
 * again. */
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

/* The OCXO calibration's measurement's state, at 0x102d9e, up to the
 * kernel's I/O table (make record-access; efc_calibration_measure's and
 * efc_calibration_fit's, which index frequency_offset by 4 from 0x102da2,
 * 0x52ba0, 0x528b8). */
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
 * named by the choice :PTESt:STATe? gives it (its jump table, 0x3f232;
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
 * stores 0 for PREDicted and 1 for MEASured (0x3f084), pll's
 * predicted_uncertainty or measured_uncertainty. */
typedef enum __attribute__((packed)) ptest_uncertainty_type {
  PTEST_TYPE_PREDICTED = 0,
  PTEST_TYPE_MEASURED = 1,
} ptest_uncertainty_type;

/* The time uncertainty test's run, at 0x1026fe (make record-access; its
 * reset clears it whole, 0x4a2f8), each field named by what the test does
 * with it. */
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

/* gps_diag_sequence's states, gps_diag_state (0x1028a2): the 58503A's,
 * and self-test 15's, this image's own (0x50986). */
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

/* gps_leap_ack's states, pll.leap_ack_state, the 58503A 3704's: the
 * engine's time mode switched with @@Aw to measure the GPS-UTC offset,
 * then switched back.  This image's gps_leap_ack takes the offset the
 * GT-74 gives and stores only PLL_LEAP_ACK_IDLE (0x507a4). */
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
 * values.  This image's own, 0x3ee bytes, the copy pllp_task keeps in
 * preserved_region (0x513c8), cleared as one, memset(&pll, 0, 0x3ee)
 * (pll_init, 0x50b4c),
 * each field where this image's code reaches it, through pll's address
 * or a base register loaded with it (make record-access), and named as
 * the 58503A 3704's field of the instruction image_diff aligns with it:
 * the 58503A's fields reordered, n3 lock pll's state and values before
 * the hold causes, which moves them and the fields to the EFC 14 bytes
 * on; the measured time uncertainty after the predicted; the EFC
 * history this image's own (efc_ring); the aging term and the loop's
 * EFC step after it; the holdover start's uptime where the 58503A's
 * has an unused long; and three bytes of its own near the end.  A field
 * no code reaches is named by its offset, the 58503A's name kept only
 * where an access proves it. */
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
  float ti0; /* +0x0e: n3 lock pll's first TI of its three, "ti0" */
  float ti1; /* +0x12: its second, "ti1" */
  float ti2; /* +0x16: its third, "ti2" */
  pll_n3_lock_state n3_lock_state; /* +0x1a: n3_lock_pll's */
  uint8_t field_01b;
  pll_normal_state normal_state; /* +0x1c: pll_normal's (stage 7), through a
                                    pointer in its frame (0x4de20) */
  uint8_t field_01d;
  pll_hold_cause hold_causes; /* +0x1e: a set of pll_hold_cause bits, a word
                                 (diag_pr_hold_cause) */
  pll_holdover_state holdover_state; /* +0x20: holdover's */
  uint8_t manual;                    /* +0x21 */
  uint8_t ti_limit_override; /* +0x22: holdover checks the TI limit while 0;
                                holdover_recovery_limit_ignore sets it */
  uint8_t field_023;
  uint8_t
      recovery_countdown; /* +0x24: holdover_recovery's, as powerup_countdown */
  pll_recovery_state recovery_state; /* +0x25 */
  pll_coarse_state coarse_state;     /* +0x26: coarse's */
  pll_fine_state fine_state;         /* +0x27: fine's; PLL_FINE_SLEW to
                                 PLL_FINE_FINE_SLEW slew the phase */
  uint8_t frequency_adjusted; /* +0x28: fine applied a frequency correction */
  uint8_t fine_index;         /* +0x29: fine acquisition's "i= %d" */
  uint16_t fine_retries; /* +0x2a: fine's failed attempts, given up past 9 */
  float fine_d;          /* +0x2c: fine acquisition's "d = %f" */
  pll_aging_state
      aging_state; /* +0x30: efc_history_update's; 3 once the aging fit holds */
  uint8_t field_031[3];
  pll_gps_diag_state gps_diag_state; /* +0x34: gps_diag_sequence's */
  uint8_t gps_diag_substate;         /* +0x35 */
  pll_gps_init_state
      gps_init_state; /* +0x36: gps_init_sequence's, print_gps_init_state's */
  uint8_t field_037;
  uint16_t leap_check_timer;    /* +0x38: gps_leap_sequence checks again when it
                                   wraps */
  uint8_t leap_check_requested; /* +0x3a: pll_stage_status sets it */
  pll_leap_check_state leap_check_state; /* +0x3b: gps_leap_sequence's,
                               print_leap_check_state's */
  pll_leap_ack_state leap_ack_state;     /* +0x3c: gps_leap_ack's */
  uint8_t leap_ack_requested;            /* +0x3d */
  uint8_t leap_ack_done;                 /* +0x3e: the offset is set */
  uint8_t field_03f;
  uint8_t
      leap_ack_again; /* +0x40: holdover_recovery measures again after fine */
  uint8_t field_041;
  float efc;                   /* +0x42: pr_efc's efc */
  float tfom;                  /* +0x46: pll_rep's tfom, s */
  float predicted_uncertainty; /* +0x4a: the curv fit's, :KENneth:PREDicted? */
  float measured_uncertainty;  /* +0x4e: pr_bad_act's "tunc:meas", which
                                  measured_uncertainty_compute computes
                                  (measured_uncertainty_update) */
  uint8_t field_052[8];
  float frequency_tolerance; /* +0x5a: 1e-8 at first; fine's bound, holdover's
                                limit per second */
  efc_ring efc_history;      /* +0x5e */
  float aging_term;    /* +0x3b2: aging_fit_collect's c 2700 / uptime + b, the
                          fit's slope at the present uptime */
  float loop_efc_step; /* +0x3b6: startup_pll's EFC step, filtered and clamped
                          to plus or minus efc_gains.step_limit */
  float ti_filtered;   /* +0x3ba: the TI, filtered with weight 29.75 / tau */
  float efc_base; /* +0x3be: the EFC less the temperature term, TC times the
                     current */
  float loop_time_constant; /* +0x3c2: 150 s at first, at most max_loop_time */
  uint8_t field_3c6[8];     /* nothing refers to it */
  uint8_t led_gps_lock;     /* +0x3ce: :LED:GPSLock? */
  uint8_t led_holdover;     /* +0x3cf: :LED:HOLDover? */
  uint32_t holdover_duration;     /* +0x3d0: :ROSCillator:HOLDover:DURation? */
  uint32_t holdover_start_uptime; /* +0x3d4: get_uptime's as enter_holdover
                                     starts the count (0x4ccb0) */
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
  uint8_t loop_running;   /* +0x3e7: pll_restart sets it; while set,
                             pps_sync_pulse pulses the FPGA's sync bit */
  uint8_t sync_requested; /* +0x3e8: sync_perform syncs whatever the TI, then
                             clears it */
  uint8_t field_3e9;      /* n3_lock_pll sets and clears it, pll_init clears
                             it */
  uint8_t external_reference; /* +0x3ea: the reference the loop takes, 0 the
                                 GPS's 1PPS, else the external; pllp_task's
                                 (0x510da), status_snapshot copies it */
  uint8_t field_3eb;          /* pll_init sets 9 (0x50bc4), pll_tfom_digit_store
                                 writes it */
  uint8_t measured_uncertainty_valid; /* +0x3ec: pr_bad_act's "tunc:meas
                                         valid", measured_uncertainty_compute's
                                         result */
  uint8_t field_3ed;
} pll_state;

/* The GPS link's counters, which the "GPS Process Monitor" page
 * (gps_print_process_monitor, 0x58cb2) prints through A2.  Cleared as
 * one, memset(&gps_counts, 0, 0x3c) (clear_gps_debug).  All words but
 * responses_read, a long (0x58d80); the 58503A's through commands_sent,
 * then two longer and two more, "Addr" and "Data". */
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
  uint16_t unknown_response; /* ? Resp */
  uint16_t frame_errors;     /* Frame: the NMEA framer's */
  uint16_t checksum_errors;  /* Cksum */
  uint16_t address_errors;   /* Addr */
  uint16_t data_errors;      /* Data */
  uint16_t command_monitor_failures; /* command_monitor's (0x571b0) */
  uint16_t status_monitor_failures;  /* its status monitor's (0x5735c) */
} gps_statistics;

/* The NMEA framer's record, gps_response_buffer (0x1015b0): the sentence
 * gps_receive frames from its '$', one character at a time through its
 * state table (0x5e036), each state's transitions a predicate of the
 * character and the record and an action on them (0x5e04a, 0x5e072).
 * Its length is to gps_counts (0x10161c), the last field's bound. */
typedef struct nmea_frame {
  char text[0x52];            /* +0x00: the sentence, a NUL for each comma */
  uint8_t* cursor;            /* +0x52: gps_response_cursor, the next place */
  uint8_t fields;             /* +0x56: the fields ended so far */
  uint8_t checksum_digits;    /* +0x57: those framed after '*', to 2 */
  uint8_t field_starts[0x14]; /* +0x58: each field's offset in text,
                                 the first 1, past the '$' */
} nmea_frame;

#include "../../common/types/exception_record.h"

#include "../../common/types/os.h"

/* A fixed event log message, by the code log_event (0x3189a) takes
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
 * one for each command group (scpi_group_cells, 0x101f90, 12 bytes, up
 * to interval_reading).  No code reads or writes them, only
 * primary_reset's clear, so each is a word by their spacing alone. */
typedef struct scpi_group_cells_fields {
  uint16_t serial_1; /* +0x00: :SYSTem:COMMunicate:SERial1's (0x33328 on) */
  uint16_t sample;   /* +0x02: :SAMPle's, :GPS:INITial's, :SYSTem:PRINt's
                        and scpi_param_DOUGlas_STATe_request's */
  uint16_t pulse;    /* +0x04: :OUTPut:STATe's and :PULSe's (0x33968 on) */
  uint16_t serial_2; /* +0x06: :SYSTem:COMMunicate:SERial2's (0x33aa2 on) */
  uint16_t status;   /* +0x08: :STATus's (0x33b10 on) */
  uint16_t rest;     /* +0x0a: every other descriptor's, 0x33c8a to 0x34374 */
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

/* gps_response_match's verdict on a response, this image's own:
 * gpsm_task takes the next step on 3 and retries the sentence on 2, up
 * to three times, and waits on for 0 and 1 (0x5835a to 0x58398). */
typedef enum __attribute__((packed)) gps_match_result {
  GPS_MATCH_OTHER = 0,    /* not the sentence that answers the command */
  GPS_MATCH_IGNORED = 1,  /* GPS_SENTENCE_MATCH_TIMEOUT's, for any response */
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

/* The settings nv_save keeps in the NV register and nv_restore loads,
 * nv_settings (0x102570): each field the value of the descriptor in
 * nv_save's tables that points at it, named by the SCPI command whose
 * setter is given that descriptor.  0xcf bytes, four more than the
 * 58503A 3704's: its tables (nv_numeric_params, 0x474c4, 24 descriptors,
 * nv_save's first loop's bound, 0x47106; nv_setting_params, 0x47524, 19,
 * its second's, 0x47180) add a value, :OUTPut:ACTive:HOLDover:DURation:
 * THReshold's, after holdover_status_threshold, and a setting,
 * :DIAGnostic:LOG:WRAP's, where the 58503A's has a pad before
 * questionable_user. */
typedef struct nv_settings_fields {
  uint8_t field_00[2];               /* no code refers to them */
  int32_t elevation_mask;            /* :GPS:EMANgle */
  int32_t antenna_delay;             /* :GPS:ADELay */
  int32_t douglas_duration;          /* :DOUGlas:DURation */
  uint8_t douglas_duration_infinite; /* :DOUGlas:DURation INFinity */
  uint8_t tracking_log;              /* :DIAG:GPS:TRACking:LOG (0x48ace) */
  int32_t questionable_hysteresis;   /* :DIAG:REF:GPS:QUES:HYSTeresis
                                        (0x48a64), 10 bits */
  int32_t external_questionable_hysteresis; /* :DIAG:REF:EXT:QUES:HYSTeresis
                                               (0x48aa2), 4 bits */
  int32_t holdover_status_threshold;        /* :ROSC:HOLD:DUR:STAT:THR */
  int32_t active_holdover_threshold;        /* :OUTP:ACTive:HOLD:DUR:THReshold
                                               (0x4857c), 17 bits */
  float limit_threshold;                    /* :ROSC:LIMit:THReshold */
  float holdover_limit_threshold;           /* :ROSC:HOLD:LIMit:THReshold */
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
  uint8_t pad_51;
  int32_t raim_threshold[2];              /* :DIAG:GPS:POS:HOLD:RAIM:THR */
  int32_t holdover_measurement_threshold; /* :ROSC:HOLD:DUR:MEAS:THR */
  uint32_t status_enable[7];              /* *ESE, *SRE, :STATus ENABle */
  uint32_t status_positive_transition[5]; /* :STATus PTRansition */
  uint32_t status_negative_transition[5]; /* :STATus NTRansition */
  uint8_t log_wrap;                       /* :DIAG:LOG:WRAP (0x48aea) */
  uint8_t questionable_user;              /* :STAT:QUES:...:USER */
  uint8_t serial_restricted;              /* :DIAG:SER1:RESTricted */
  uint8_t pulse_state;                    /* :PULSe:CONTinuous:STATe */
  int32_t pulse_period;                   /* :PULSe:CONTinuous:PERiod */
  int32_t pulse_start_time[3];            /* :PULSe:STARt:TIME */
  int32_t pulse_start_date[3];            /* :PULSe:STARt:DATE */
  int32_t field_c2;              /* 16 bits (0x48a26); only its descriptor */
  uint8_t output_state;          /* :OUTPut:STATe */
  data_format format_data;       /* :FORMat:DATA */
  uint8_t memory_save;           /* :ROBin:MEMory:SAVE */
  uint8_t antenna_current_alarm; /* :DIAG:GPS:ACURrent:ALARm (0x4894e) */
  uint8_t douglas_state_powerup; /* :DOUGlas:STATe:POWerup */
  timecode_format tcode_format;  /* :PTIMe:TCODe:FORMat */
  uint8_t gps_utc;               /* :DIAG:GPS:UTC */
  uint8_t pforth_at_startup;     /* :DIAG:SYSTem:PSTartup */
  uint8_t debug_output;          /* :DIAG:SYSTem:DOUTput */
} nv_settings_fields;

#include "../../common/types/interval_reading_fields.h"

#include "../../common/types/counter_work_state.h"

/* counter_configuration_fields.alarm_mode: when counter_arm sets
 * FPGA_BIT_ALARM to the configuration's alarm (0x4988c); any other value
 * never. */
typedef enum __attribute__((packed)) counter_alarm_mode {
  COUNTER_ALARM_WHEN_WANTED = 1,
} counter_alarm_mode;

/* The time-interval counter's configuration, counter_configuration at
 * 0x1026a2, which counter_arm, counter_calibrate and counter_read are
 * given.  A mode and a state go in pairs: the mode says when the work is
 * done, the state asks for it or says it is done.  The 58503A's (its
 * smartclock.h) but for eight FPGA
 * registers, not four, and a third pair, the alarm's, after the
 * interpolator's scale: 0x28 bytes, interpolator_calibration_state
 * after them (0x1026ca, counter_init 0x49b1e). */
typedef struct counter_configuration_fields {
  uint8_t fpga_registers[8]; /* counter_arm copies them into fconf_regs */
  double calibration;        /* s, which counter_read subtracts */
  counter_registers_mode registers_mode; /* when fpga_registers load */
  counter_work_state registers_state;
  counter_calibrate_mode calibrate_mode; /* when the interpolator calibrates */
  counter_work_state calibrate_state;
  counter_sync_mode sync_mode;   /* when counter_arm syncs */
  counter_work_state sync_state; /* sync_request sets both */
  counter_sync_kind sync_kind;   /* sync_request's first; into fconf_regs[3] */
  uint8_t pad_17;
  int32_t sync_count;             /* sync_request's second; counted down */
  int32_t interpolator_offset;    /* reading_low, subtracted from the reading */
  int32_t interpolator_scale;     /* the spread it is divided by */
  counter_alarm_mode alarm_mode;  /* when the alarm is set */
  counter_work_state alarm_state; /* 0x494d8 sets both */
  uint8_t alarm;                  /* 0x494d8's; FPGA_BIT_ALARM's value */
  uint8_t pad_27;
} counter_configuration_fields;

/* The battery-backed RAM a warm restart restores, preserved_region at
 * 0x100000: pllp_task copies gps, pll and the RAM blocks below into it
 * each pass and checksums it, and system_init copies them back when the
 * checksum is good, RSR shows neither an external nor a power-up reset,
 * and restore is 1.  0xc04 bytes, record_checksum's length (system_init,
 * 0x22dd0), each block where system_init's copies take it (0x22e06 to
 * 0x22e48). */
typedef struct preserved_region_fields {
  uint16_t
      checksum; /* +0x000: record_checksum's sum and complement of the rest */
  uint16_t restore;            /* +0x002: a preserved_region_restore */
  gps_state gps;               /* +0x004 */
  pll_state pll;               /* +0x67c: pll's, 0xfb longs and a word */
  adc_monitor adc_monitors[8]; /* +0xa6a: adc_monitors' */
  uint8_t values[0x19];        /* +0xbea: retained_block's (0x102556) */
  uint8_t field_c03;           /* in the checksum; nothing else refers to it */
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

/* A card line of the status screen's heading, one of
 * status_screen_card_lines (0x59716): five, and a terminator whose text
 * is "-------------", which status_screen picks by the
 * snapshot's redundancy_code (0x56104 to 0x5613a). */
typedef struct status_screen_card_line {
  uint8_t code; /* +0x00: 1 "[ Standby ]", 2 "[ Unlocked ]", 0 "[ Active ]",
                   3 "[ Blocked ]", 4 "[ ALARM ]" */
  uint8_t field_01;
  char* text; /* +0x02 */
} status_screen_card_line;

/* The switch word's bits, the word at 0x302000 (switch_word, CS7), each
 * named by its position: what most select is not established
 * (docs/firmware/z3815a-4010.md, "Not established").  Bit 12 picks
 * the OCXO's warmth, the puck's or the oven monitor's (0x33c44), the
 * loop's longest time constant and the OCXO calibration's default a
 * (docs/firmware/restart.md, "The switch byte at 0x302000"); bit 13
 * makes a low antenna current a fault (0x33c08). */
typedef enum __attribute__((packed)) switch_word_bit {
  SWITCH_WORD_BIT_0 = 0x0001,
  SWITCH_WORD_BIT_1 = 0x0002,
  SWITCH_WORD_BIT_2 = 0x0004,
  SWITCH_WORD_BIT_3 = 0x0008,
  SWITCH_WORD_BIT_4 = 0x0010,
  SWITCH_WORD_BIT_5 = 0x0020,
  SWITCH_WORD_BIT_6 = 0x0040,
  SWITCH_WORD_BIT_7 = 0x0080,
  SWITCH_WORD_BIT_8 = 0x0100,
  SWITCH_WORD_BIT_9 = 0x0200,
  SWITCH_WORD_BIT_10 = 0x0400,
  SWITCH_WORD_BIT_11 = 0x0800,
  SWITCH_WORD_BIT_12 = 0x1000,
  SWITCH_WORD_BIT_13 = 0x2000,
  SWITCH_WORD_BIT_14 = 0x4000,
  SWITCH_WORD_BIT_15 = 0x8000,
} switch_word_bit;

/* The output's controller's state, output_control_state (0x10218a),
 * which output_control_step steps by its jump table (0x34836), each
 * named by the debug texts it logs going to it and by output_status'
 * bits output_status_update gives it.  The texts name the unlocked
 * state standby going there from active ("STBY from ACT"), and unlocked
 * coming out of it ("ACT from UNLOCK", "STBY from UNLOCK"). */
typedef enum __attribute__((packed)) output_control {
  OUTPUT_CONTROL_WARMUP = 0,   /* "WARMUP from ACT, clock off" */
  OUTPUT_CONTROL_WARMUP_1 = 1, /* warm-up's handler; nothing sets it */
  OUTPUT_CONTROL_STANDBY = 2,
  OUTPUT_CONTROL_ACTIVE = 3,
  OUTPUT_CONTROL_UNLOCKED = 4,
  OUTPUT_CONTROL_HARDWARE_FAULT = 5, /* alarm_hardware_update's fault */
  OUTPUT_CONTROL_BLOCKED = 6,        /* "BLK from ACT, user", "other card" */
  OUTPUT_CONTROL_BLOCK_HOLD = 7,     /* input 1 back or the output disabled */
  OUTPUT_CONTROL_EXTRACTED = 8,      /* "BLK from ACT, extractor" */
  OUTPUT_CONTROL_BLOCK_RELEASE = 9,  /* to warm-up on input 1, an enable or
                                        60 s */
} output_control;

/* The output's state, output_state_get's (0x340fa), from output_status'
 * bits in this order: bit 6, then bit 0, then bit 1, then bit 3 without
 * bit 2, else unlocked.  :OUTPut:STATe? reports each by a keyword
 * (output_state_choice, 0x3fe92), the status screen's heading by its
 * card line (status_screen_card_lines: Active, Standby, Unlocked, Blocked,
 * ALARM), and es_build sets es_status' bits 5 and 3 for active and
 * standby and es_error's bits 6 and 1 for blocked and alarm. */
typedef enum __attribute__((packed)) output_state {
  OUTPUT_ACTIVE = 0,   /* output_status bit 0 */
  OUTPUT_STANDBY = 1,  /* bit 1 */
  OUTPUT_UNLOCKED = 2, /* none of the others */
  OUTPUT_BLOCKED = 3,  /* bit 3, bit 2 clear */
  OUTPUT_ALARM = 4,    /* bit 6, ahead of the others */
} output_state;

/* :OUTPut:STATe?'s response, the index of its keyword in the choices of
 * its response item (scpi_choice_keywords_response_OUTPut_STATe_1,
 * 0x61b7e). */
typedef enum __attribute__((packed)) output_state_choice {
  OUTPUT_STATE_CHOICE_ACTIVE = 0,
  OUTPUT_STATE_CHOICE_ALARM = 1,
  OUTPUT_STATE_CHOICE_BLOCK = 2,
  OUTPUT_STATE_CHOICE_STANDBY = 3,
  OUTPUT_STATE_CHOICE_UNLOCK = 4,
} output_state_choice;

/* The ES message's status byte, es_status's bits for a date (0x23fb2),
 * named by what sets each: the time not valid or no GPS-UTC offset; a
 * leap second pending on the date, its minute longer than 60 seconds or
 * not; the output's state standby or active (output_state_get); the loop
 * in holdover or its recovery; the time GPS's, nv_settings.gps_utc clear;
 * and drtb_puts sending on channel B (drtb_sending).  What each means to
 * the receiving unit is not established. */
typedef enum __attribute__((packed)) es_status_bit {
  ES_STATUS_TIME_INVALID = 0x01,
  ES_STATUS_LEAP_LONGER = 0x02,
  ES_STATUS_LEAP_SHORTER = 0x04,
  ES_STATUS_STANDBY = 0x08,
  ES_STATUS_HOLDOVER = 0x10,
  ES_STATUS_ACTIVE = 0x20,
  ES_STATUS_GPS_TIME = 0x40,
  ES_STATUS_DRTB_SENDING = 0x80,
} es_status_bit;

/* The ES message's error byte, es_error, es_error_update's bits
 * (0x23f3a), named by what sets each: the switch word's bit 10
 * (switch_word_bit_10); the output's state alarm or blocked
 * (output_state_get); hmon_check_antenna_current's fault; the
 * :STATus:OPERation:HARDware condition's bit 6, the EFC near its limit;
 * holdover_duration_exceeded; and clock_deviation_alarm (0x1021ae).  What
 * each means to the receiving unit is not established. */
typedef enum __attribute__((packed)) es_error_bit {
  ES_ERROR_SWITCH_WORD_BIT_10 = 0x01,
  ES_ERROR_ALARM = 0x02,
  ES_ERROR_ANTENNA_CURRENT = 0x08,
  ES_ERROR_EFC_NEAR_LIMIT = 0x10,
  ES_ERROR_HOLDOVER_DURATION_EXCEEDED = 0x20,
  ES_ERROR_BLOCKED = 0x40,
  ES_ERROR_CLOCK_DEVIATION = 0x80,
} es_error_bit;

/* Why the output is inactive, output_inactive_cause (0x1021a0), a word,
 * which output_control_step sets as it leaves active, each value named
 * by the keyword :OUTPut:INACtive? reports for it (0x3fe3c), or, 5, by
 * the debug text "STBY from ACT, time deviation" (0x34a48): the query
 * reports 0, 5 and any other as NONE (docs/firmware/quirks.md), and
 * output_control_reset clears it (0x34624). */
typedef enum output_inactive_value {
  OUTPUT_INACTIVE_NONE = 0,
  OUTPUT_INACTIVE_HARD = 1,
  OUTPUT_INACTIVE_HOLD = 2,
  OUTPUT_INACTIVE_MANUAL = 3,
  OUTPUT_INACTIVE_EXTERNAL = 4,
  OUTPUT_INACTIVE_TIME_DEVIATION = 5,
  OUTPUT_INACTIVE_EXTRACTOR = 6,
} output_inactive_value;

/* :OUTPut:INACtive?'s response, the index of its keyword in the choices
 * of its response item (scpi_choice_keywords_response_OUTPut_INACtive_1,
 * 0x61b36). */
typedef enum __attribute__((packed)) output_inactive_choice {
  OUTPUT_INACTIVE_CHOICE_EXTERNAL = 0,
  OUTPUT_INACTIVE_CHOICE_EXTRACTOR = 1,
  OUTPUT_INACTIVE_CHOICE_HARD = 2,
  OUTPUT_INACTIVE_CHOICE_HOLD = 3,
  OUTPUT_INACTIVE_CHOICE_MANUAL = 4,
  OUTPUT_INACTIVE_CHOICE_NONE = 5,
} output_inactive_choice;

/* A status snapshot, status_snapshot's record (0x55d1e), which the
 * status screen prints: 0x176 bytes, the stride of the four this image
 * keeps from 0x1017c6 to status_snapshot_flagged (0x101c2e), each field
 * where status_snapshot stores it and its readers read it.  The 58503A
 * 3704's fields, reordered, and six of this image's: two bytes first,
 * the byte after field_592, the height's type after the position, and
 * three after the hardware condition. */
typedef struct status_snapshot_record {
  uint8_t field_000;       /* +0x000: the byte at 0x102180; the screen prints
                              '2' while it is set, else '1' (0x56142) */
  uint8_t redundancy_code; /* +0x001: output_state_get's, an output_state,
                              which the card's heading names
                              (status_screen_card_lines) */
  pll_stage stage;         /* +0x002: pll.stage */
  uint8_t mode;            /* +0x003: pll_mode's */
  uint8_t powerup_suffix;  /* +0x004: pll_powerup_suffix_index's */
  uint8_t recovery_suffix; /* +0x005: recovery_suffix_index's */
  float tfom;              /* +0x006: pll.tfom */
  uint8_t ffom;            /* +0x00a: get_ffom's */
  uint8_t ffom_valid;      /* +0x00b: get_ffom's result */
  double interval;         /* +0x00c: interval_reading.value */
  uint8_t interval_valid;  /* +0x014: interval_reading.valid */
  counter_error interval_errors;   /* +0x015: interval_reading.errors */
  uint16_t interval_tag;           /* +0x016: interval_reading.tag */
  float interval_mean;             /* +0x018 */
  uint16_t interval_mean_flag;     /* +0x01c: the word at interval_mean_flag */
  uint8_t holdover_waiting_reason; /* +0x01e */
  holdover_limit_setting holdover_limit_state; /* +0x01f: nv_settings' */
  float holdover_limit_threshold;              /* +0x020: nv_settings' */
  uint32_t holdover_duration;        /* +0x024: pll.holdover_duration */
  float kenneth_present;             /* +0x028: kenneth_present_get's */
  uint8_t kenneth_present_valid;     /* +0x02c: its result */
  int8_t kenneth_present_resolution; /* its resolution, a power of ten */
  float kenneth_predicted; /* +0x02e: 0x4bc0e's, made its absolute value */
  uint8_t kenneth_predicted_valid;     /* +0x032: its result */
  int8_t kenneth_predicted_resolution; /* its resolution, a power of ten */
  uint8_t channel_count;               /* +0x034: gps' */
  uint8_t field_035;
  satellite_sets satellites;            /* +0x036: satellite_sets_build's */
  satellite_list_entry not_tracked[16]; /* +0x060: satellite_status_list's
                                           list 0, twice the channels */
  satellite_list_entry tracked[8];      /* +0x0e0: its list 1 */
  uint8_t status_received;              /* +0x120: gps' */
  uint8_t not_tracked_count;   /* +0x121: satellite_status_list's count of
                                  not_tracked */
  uint8_t tracked_count;       /* +0x122: its count of tracked */
  uint8_t external_reference;  /* +0x123: pll.external_reference */
  uint8_t reference_valid;     /* +0x124: gps.references[0].valid */
  uint8_t pps_dead;            /* +0x125: gps.references[0].pps_dead */
  gps_sync_status status_code; /* +0x126: gps.references[0].state */
  uint8_t field_127;
  uint32_t reference_elapsed;           /* +0x128: gps.references[0].elapsed */
  uint8_t external_valid;               /* +0x12c: gps.references[1].valid */
  uint8_t external_pps_dead;            /* +0x12d: gps.references[1].pps_dead */
  gps_sync_status external_status_code; /* +0x12e: gps.references[1].state */
  uint8_t field_12f;
  uint32_t external_elapsed;   /* +0x130: gps.references[1].elapsed */
  gps_field_592_bit field_592; /* +0x134: gps', copied with
                                  gps_field_663 as a word (0x55ec4) */
  uint8_t gps_field_663;       /* +0x135: gps.field_663 */
  uint8_t fix_mode;            /* +0x136: gps' */
  oncore_receiver_status receiver_status; /* +0x137: gps.status' */
  time_of_day time;     /* +0x138: gps.status' time made local;
                           get_date_time's while from_clock */
  uint32_t date;        /* +0x13c: the same's date */
  int32_t time_zone[2]; /* +0x140: nv_settings' */
  uint8_t from_clock;   /* +0x148: 1 when the clock is set, the time its */
  uint8_t utc_time;     /* +0x149: gps' */
  nmea_time_mode tps_time_mode; /* +0x14a: gps.tps.time_mode */
  oncore_leap_pending
      leap_second_pending; /* +0x14b: from nv_settings.leap_minute_length,
                              59 a second subtracted, 61 added (0x55f00) */
  int32_t pps_cable_delay; /* +0x14c: gps' */
  uint8_t mask_angle;      /* +0x150: gps' */
  uint8_t position_hold;   /* +0x151: gps' */
  uint8_t douglas_duration_infinite; /* +0x152: nv_settings' */
  uint8_t field_153;
  char* position_source;          /* +0x154: "INIT ", "AVG " or "" */
  int32_t position[3];            /* +0x158: latitude, longitude, height; as
                                     position_source says */
  oncore_height_type height_type; /* +0x164: the hold height's type, or
                                     MSL where field_592's bit 6 picks the
                                     MSL height (0x55fec) */
  uint8_t field_165;
  uint32_t average_count;        /* +0x166: gps' */
  int32_t douglas_duration;      /* +0x16a: nv_settings' */
  uint16_t hardware_condition;   /* +0x16e: status_hardware_condition's */
  uint16_t alarms_upper;         /* +0x170: the word at 0x10219c (0x3413c); the
                                    screen's alarms of a mask above 0x100 */
  uint16_t alarms;               /* +0x172: the word at 0x10219e (0x34144); the
                                    screen's other alarms, which sets its 0x80 from
                                    alarm_clock_deviation (0x568cc) */
  uint8_t alarm_clock_deviation; /* +0x174: 0x34412's, the byte at 0x1021ae
                                    set: the screen's "Clk Dev" */
  uint8_t pad_175; /* to the records' stride; nothing refers to it */
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
