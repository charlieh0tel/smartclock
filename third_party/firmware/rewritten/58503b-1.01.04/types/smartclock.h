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
  PRESERVED_REGION_WARM_STARTED = 2, /* the 58503B's system_init sets it on
                                        a warm start (0x22e66), where the
                                        other images set RESTORE again;
                                        neither of its tests takes it */
} preserved_region_restore;

/* BEGIN PROPOSED ENUMS */
/* END PROPOSED ENUMS */

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
 * alternate receiver's, gps_status_alternate (0x56ce6, 0x56dd4), which
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
 * satellite_status the rest (0x4fcc6 to 0x4fd5a), and
 * format_satellite_cell prints each (its table at 0x500c8), the 58503A
 * 3704's. */
typedef enum __attribute__((packed)) satellite_cell_status {
#include "../../common/types/satellite_cell_status.h"
  SATELLITE_CELL_ACQUIRING = 5,    /* "Acq   ": channel modes 1 to 4 */
  SATELLITE_CELL_SYNCHRONIZED = 6, /* "Acq . ": modes 5 and 6 */
  SATELLITE_CELL_EPHEMERIS = 7     /* "Acq ..": modes 7 and 8 */
} satellite_cell_status;

/* satellite_track_class's class of a @@Bb record, by its health
 * (oncore_health; its table at 0x4fbfc): satellite_status shows a
 * removed one as "Ignore" and an unhealthy one as "Not OK". */
typedef enum __attribute__((packed)) satellite_health_class {
#include "../../common/types/satellite_health_class.h"
} satellite_health_class;

/* satellite_status_list's and satellite_status' list: the satellites
 * not used, those the channels use, and those on the channels, used or
 * not, which the satellite statistics take (0x542bc), with their levels
 * and no health. */
typedef enum __attribute__((packed)) satellite_list_kind {
  SATELLITE_LIST_UNUSED = 0,
  SATELLITE_LIST_USED = 1,
  SATELLITE_LIST_ON_CHANNELS = 2,
} satellite_list_kind;

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

/* The engine's antenna current, gps.antenna_sense: 0x5706c's reading of
 * @@Ea's receiver status byte (0x57cb2), bit 6 then bit 7, which
 * hmon_check_antenna_current takes as the antenna current's average
 * above and below its range, 1 and 0xff (0x31af2 to 0x31b00). */
typedef enum __attribute__((packed)) gps_antenna_sense {
  GPS_ANTENNA_NORMAL = 0,
  GPS_ANTENNA_UNDER_CURRENT = 1, /* bit 7 */
  GPS_ANTENNA_OVER_CURRENT = 2,  /* bit 6 */
} gps_antenna_sense;

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
  GPS_STATUS_MONITOR_STANDBY = 1,   /* watches @@Ba come */
  GPS_STATUS_MONITOR_RECOVERY = 2,  /* waits for the outputs' request */
  GPS_STATUS_MONITOR_RECOVERED = 3, /* watches again, a miss failing it
                                       (0x523fe, 0x52378); the page's
                                       "wait-recover" */
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
 * and adc_monitors, named by its hmon_channels entry (0x31b12, eight
 * entries of 0x2a bytes): the 58503A 3704's channels but for entry 0,
 * the antenna voltage where the 58503A's is the temperature, and
 * entry 6, the oscillator current where the 58503A's is the primary
 * oven's (docs/firmware/58503b-1.01.04.md, "The health monitor's
 * channels"). */
typedef enum __attribute__((packed)) adc_channel {
  ADC_ANTENNA_VOLTAGE = 0,    /* "Antenna voltage", through the 5V's convert */
  ADC_5V = 1,                 /* "5V"; the oven's and -15V's converts subtract
                                 it */
  ADC_CHANNEL_2 = 2,          /* no convert routine and an empty name */
  ADC_OVEN = 3,               /* "Oven" */
  ADC_PLUS_15V = 4,           /* "15V" */
  ADC_MINUS_15V = 5,          /* "-15V" */
  ADC_OSCILLATOR_CURRENT = 6, /* "Oscillator current", filtered by
                                 monitor_filter_add */
  ADC_ANTENNA_CURRENT = 7,    /* "Antenna current" */
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

/* The holdover stage's steering, at holdover_steering: each block's sum of
 * valid readings, then their mean, the block's valid readings and seconds,
 * and the largest reading's magnitude, which holdover_ti_add keeps (make
 * record-access: holdover at 0x48a80 to 0x48a88, holdover_ti_add at
 * 0x486e0).  The other images' holdover_steering_state, each its own,
 * keeps a field and an aging limit after the seconds and a least-squares
 * slope's sums before the largest reading, which this image has none of. */
typedef struct holdover_steering_state {
  float ti_mean;    /* +0x00: the block's valid readings' sum, then over their
                       count at its end */
  uint8_t readings; /* +0x04: the block's valid readings */
  uint8_t seconds;  /* +0x05: the block's seconds, to pll.block_seconds;
                       holdover recovery's failed readings */
  float ti_largest; /* +0x06: the largest reading's magnitude,
                       holdover_ti_max's */
} holdover_steering_state;

/* startup_pll's and pll_normal's steering, at loop_steering: a
 * proportional-integral loop on each 10-second block's mean time
 * interval, its gains from the loop's time constant. */
typedef struct loop_steering_state {
  float integral; /* +0x00: the integral term, in EFC counts */
  float ti_sum;   /* +0x04: the block's valid readings, then their mean */
  int32_t settled_blocks; /* +0x08: startup_pll's consecutive blocks whose
                             |mean| is below 150 ns, to 15; one at or above
                             it clears it */
  uint8_t seconds;        /* +0x0c: the block's seconds, to 10 */
  uint8_t readings;       /* +0x0d: the block's valid readings */
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

/* The ACIA's driver, the GPS engine's link, at acia (0x103d9c), which
 * acia_send_message, acia_isr and acia_tx_interrupt keep: duarta_driver's
 * layout, its matches', but that +0x07 gathers the receiver's errors
 * where DUART channel A's marks its loopback test.  Its receive ring is
 * at acia_ring. */
typedef struct acia_driver {
  uint8_t* transmit_next; /* +0x00: acia_send_message's next byte */
  int16_t transmit_count; /* +0x04: its bytes left; at 0 acia_tx_interrupt
                             signals the GPS task 0x800 */
  uint8_t overrun;        /* +0x06: acia_isr sets it for the status' overrun
                             bit; acia_get_byte hands it back and clears it */
  uint8_t receive_errors; /* +0x07: the status' error bits acia_isr gathers
                             (0x2d48a), which acia_get_byte reports and
                             clears */
  uint8_t held; /* +0x08: a send leaves the transmit interrupt off while it
                   is set; acia_init clears it (0x2d2dc), nothing sets it */
  uint8_t least_free; /* +0x09: the receive ring's least free space seen */
} acia_driver;

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
  LATCH_HEARTBEAT = 0x08, /* hmon_task's, through board_leds_change (0x3353e;
                             docs/firmware/outputs.md, "The heartbeat") */
  LATCH_BIT_4 = 0x10,     /* the power-on self-tests', through
                             board_leds_change (0x28f1c to 0x29014) */
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
  TIMECODE_LENGTH_KEPT = 6, /* timecode_update's jump table's last entry
                               (0x23e98), which keeps the length its
                               caller passes; no :PTIMe:TCODe:FORMat
                               value gives it */
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
  uint8_t field_09;       /* duart_loopback_test_b sets it; while it is set
                             the send leaves the transmit interrupt off */
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

/* The SCI port's driver, sci at 0x103ee6, qspi after it at 0x103f04:
 * the 58503B 1.01.04's own, 0x1e bytes, each field where the driver's
 * code addresses it (make record-access): the Z3817A 3752's fields to
 * reconfiguring, then async_allowed (sci_async_allowed_update, 0x2e972)
 * and the byte sci_release_for_timecode sets, with no
 * in_transmit_interrupt, transmitting or transmit_busy, and the baud
 * constant, the echo and the last terminator after them. */
typedef struct sci_driver {
  char* transmit_next;     /* +0x00: sci_puts's next character */
  uint16_t transmit_count; /* +0x04: sci_puts's characters left */
  char* async_next;        /* +0x06: sci_send_async's */
  uint8_t async_count;     /* +0x0a */
  uint8_t async_pending;   /* +0x0b */
  uint8_t field_0c; /* the SCI's interrupt handler (primary_user_64) sets,
                       tests and clears it */
  uint8_t flow; /* +0x0d: 0x80 an XOFF received, holding transmission; 0x20 one
                   sent */
  uint8_t receive_errors; /* +0x0e: SCSR's OR, FE and PF, overrun, framing and
                             parity (MC68331UM D.4.6) */
  uint8_t read_errors;    /* +0x0f: receive_errors as the last read left them */
  uint8_t xon_xoff;       /* +0x10: flow control, from the serial settings */
  uint8_t mark_bit;       /* +0x11: each character goes out with its top bit set
                             (sci_putc_polled, 0x2dcb2) */
  uint8_t
      seven_bits; /* +0x12: seven data bits, the top bit received stripped */
  uint8_t reconfiguring; /* +0x13: set while sci_reconfigure changes the port */
  uint8_t async_allowed; /* +0x14: an asynchronous send may start */
  uint8_t released_for_timecode; /* +0x15: sci_release_for_timecode sets it,
                                    sci_timecode_released returns it */
  uint16_t baud_constant;        /* +0x16: sci_baud_set's */
  putc_function* echo; /* +0x18: from the serial settings; sci_echo_none with
                          echo off */
  uint8_t last_terminator; /* +0x1c: the CR or LF that ended the last line
                              (sci_gets) */
  uint8_t field_1d;
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
  QSPI_ADC = 1,
  QSPI_HDAC = 2,
  QSPI_UNUSED_WORD = 4,
  QSPI_UNUSED_TWO_WORDS = 5,
  QSPI_FPGA_BLOCK = 6,
  QSPI_EFC = 7,
  QSPI_FPGA_TAIL = 9,
  QSPI_FPGA_WORD = 0xa,
  QSPI_FPGA_BYTE = 0xb,
  QSPI_FPGA_READBACK = 0xc,
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

/* The self-tests' results, selftest (0x101192), 0x48 bytes, the 58503A
 * 3704's layout, each field where this image's code reaches it (make
 * record-access IMAGE=58503b-1.01.04 RECORD='0x101192 0x48 --fit
 * selftest_results'): each test's word nonzero if it failed, and what
 * selftest_run reports for test n.  The latch's copy a byte, as the
 * 58503A's, where the Z3817A 3752's is a word followed by a DUART mask,
 * so that from summary on each field is two bytes nearer than its. */
typedef struct selftest_results {
  int16_t cpu;  /* +0x00: the reset code's CPU test, selftest_failure */
  uint16_t ram; /* +0x02: the RAM test's, selftest_ram_failure's bits */
  uint16_t ram_data_bits; /* +0x04: the bits that failed a pattern, both halves
                             of a long ORed */
  uint32_t ram_address_lines; /* +0x06: the address lines that alias */
  int16_t uart;    /* +0x0a: selftest_uart_failure's bits, which selftest_uart
                      reports (0x27dee) */
  latch_bit latch; /* +0x0c: the latch at 0x300000's bits, which latch_bits_set
                      and its kin keep and write (0x236ca to 0x23792) */
  uint8_t field_00d; /* nothing reaches it */
  uint16_t summary;  /* +0x0e: selftest_summary_bit's */
  int16_t eeprom;    /* +0x10: selftest_failure: nv_restore sets it when
                        neither NV slot holds a record (0x42af4) */
  int16_t fpga;      /* +0x12: the FPGA's counters, counter_selftest;
                        selftest_failure */
  uint32_t gps; /* +0x14: the receiver's self-test, a long: gps_selftest_run
                   and gps_diag_sequence store it, selftest_gps tests it by
                   bit */
  int16_t interpolator;            /* +0x18: selftest_failure */
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
  uint8_t lamp_testing; /* +0x36: set while lamp_test runs (0x28702) */
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

/* Who sends a GPS request (gps_request_entry.sender), so that its result
 * comes back to it: gpsx_send for the loop's GPS sequences (pllp_task
 * takes the result as gps_ack_code), pll_scpi_send_to_loop for a
 * setting (pll_scpi_complete), and gps_request_post for the command
 * and status monitors, whose results gps_count_response keeps in
 * command_monitor_result and status_monitor_result (0x5288a, 0x52894).
 * request_kind_valid takes 1 to 6, from its table at 0x546d8, which
 * GPS_SENDER_NONE ends.  The Z3816A 4001's, where the 58503A's monitors
 * are 3 and 4: pll_scpi_send_to_loop's caller passes 3 for a message
 * whose byte +5 is neither 0 nor 5, 2 for the rest; gps_request_post is
 * called with 4, 5 and 6 and keeps their results in poll_monitor_result,
 * command_monitor_result and status_monitor_result (0x52cc6 to
 * 0x52cea), and request_ring_newest_replaceable lets a request of 5 or 6
 * be replaced (0x52a6a). */
typedef enum __attribute__((packed)) gps_request_sender {
#include "../../common/types/gps_request_sender.h"
  GPS_SENDER_SETTING_OTHER = 3,
  GPS_SENDER_POLL_MONITOR = 4,    /* poll_monitor (0x524de) */
  GPS_SENDER_COMMAND_MONITOR = 5, /* command_monitor's queries (0x521aa) */
  GPS_SENDER_STATUS_MONITOR = 6,  /* its GPS_SCRIPT_OUTPUTS_ON (0x52360) */
} gps_request_sender;

/* A GPS request's code (gps_request_entry.code, a descriptor's
 * gps_request), which gps_request_class (0x52d14) sorts: 0 to 0x3e an
 * Oncore message, the index of its entry in oncore_messages (0x58ae0;
 * the VP Oncore Command Reference names each,
 * third_party/motorola/VPCommands.pdf, and the UT Plus engineering
 * notes those it lacks, third_party/motorola/ut-engg-notes.pdf); 0x40
 * to 0x5a a step of the GPS task's own, gps_console_request's
 * (0x535c4, its jump table at 0x535ee); 0x5b to 0x6e a script, each
 * named by the messages it sends (docs/firmware/oncore/58503b-1.01.04.txt).
 * The Z3816A 4001's codes, whose message table, steps and scripts these
 * are, entry for entry: the 58503A 3704's 0 to 0x29, then @@Bo at 0x2a,
 * so that the 58503A's @@Ca to @@Fa are one higher, and @@Gj, @@Sz and
 * @@Ta after them.  The table's empty slots, 0x1a, 0x22, 0x23, 0x25,
 * 0x28, 0x2c, 0x2d and 0x3f, and the steps gps_console_request does
 * nothing for, 0x55, 0x56 and 0x5a, have no name; 0x4b, which it does
 * nothing for either, is the console's avg (gps_command_send). */
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
  GPS_REQUEST_UTC_OFFSET_STATUS = 0x2a,        /* @@Bo */
  GPS_REQUEST_SELFTEST = 0x2b,                 /* @@Ca */
  GPS_REQUEST_CORRECTION_INPUT = 0x2e,         /* @@Ce */
  GPS_REQUEST_DEFAULTS = 0x2f,                 /* @@Cf, set to defaults */
  GPS_REQUEST_FIX_MODE = 0x30,                 /* @@Cg, fix or idle */
  GPS_REQUEST_IO_FORMAT = 0x31,                /* @@Ci */
  GPS_REQUEST_RECEIVER_ID = 0x32,              /* @@Cj */
  GPS_REQUEST_POSITION_STATUS_8 = 0x33,        /* @@Ea, eight channels' @@Ba */
  GPS_REQUEST_POSITION_EXTENSION_8 = 0x34,     /* @@Ek, eight channels' @@Bk */
  GPS_REQUEST_TRAIM_8 = 0x35,                  /* @@En, eight channels' @@Bn */
  GPS_REQUEST_SELFTEST_8 = 0x36,               /* @@Fa, eight channels' @@Ca */
  GPS_REQUEST_LEAP_SECOND_STATUS = 0x37,       /* @@Gj */
  GPS_REQUEST_POWER_ON_FAILURE = 0x38,         /* @@Sz */
  GPS_REQUEST_TA = 0x39,                       /* @@Ta, which neither manual
                                                  names */
  GPS_REQUEST_POSITION_STATUS_8_SECOND = 0x3a, /* @@Ea again, without a decoder
                                                  or a filter */
  GPS_REQUEST_SELFTEST_SECOND = 0x3b,          /* @@Ca again, its filter
                                                  0x5839c */
  GPS_REQUEST_SELFTEST_8_SECOND = 0x3c,        /* @@Fa again, its filter
                                                  0x583d2 */
  GPS_REQUEST_SELFTEST_8_THIRD = 0x3d,         /* @@Fa again, its filter
                                                  0x582ac */
  GPS_REQUEST_FIX_ALGORITHM_SECOND = 0x3e,     /* @@Ar again, its filter
                                                  0x5843c */
  GPS_STEP_STATUS_SNAPSHOT = 0x40,             /* status_snapshot */
  GPS_STEP_PRINT_STATUS = 0x41,
  GPS_STEP_PRINT_VISIBILITY = 0x42,
  GPS_STEP_PRINT_EXTENSION = 0x43,
  GPS_STEP_PRINT_BEST_COMBINATIONS = 0x44,
  GPS_STEP_PRINT_ID = 0x45,
  GPS_STEP_PRINT_PPS = 0x46,
  GPS_STEP_PRINT_INSTRUMENT_STATE = 0x47,
  GPS_STEP_PRINT_TIME_RAIM = 0x48,
  GPS_STEP_PRINT_AVERAGE = 0x49,
  GPS_STEP_AVERAGE_RESET = 0x4a,       /* gps.average_count = 0 */
  GPS_STEP_AVERAGE = 0x4b,             /* the console's avg, gps_command_send's
                                          (0x4c374); gps_console_request does
                                          nothing with it */
  GPS_STEP_GPS_HYSTERESIS = 0x4c,      /* the GPS reference's hysteresis from
                                          its argument */
  GPS_STEP_EXTERNAL_HYSTERESIS = 0x4d, /* the external 1PPS reference's */
  GPS_STEP_TRACKING_LOG = 0x4e,        /* gps.tracking_log, its argument, and
                                          tracking_log_active cleared with it 0 */
  GPS_STEP_SURVEY_START = 0x4f,        /* gps.field_5bf = 1 */
  GPS_STEP_SURVEY_STOP = 0x50,         /* gps.field_5bf and field_5c0 = 0 */
  GPS_STEP_FIELD_5C0_SET = 0x51,       /* gps.field_5c0 = 1 */
  GPS_STEP_SURVEYING_CLEAR = 0x52,     /* gps.surveying = 0 while field_608's
                                          bit 9 is set; the scripts send it
                                          after idling, @@Cg 0 */
  GPS_STEP_PPS_CHECK = 0x53,      /* gps.pps_check_disabled, its argument, and
                                     gps.fix_mode its inverse while field_608's
                                     bit 9 is clear */
  GPS_STEP_RAIM_ARGUMENTS = 0x54, /* raim_request_arguments */
  GPS_STEP_STATUS_MONITOR = 0x57, /* gps.status_monitor_enabled, its argument */
  GPS_STEP_MONITOR_5FC = 0x58,    /* gps.field_5fc, its argument: the monitor
                                     at 0x50102's */
  GPS_STEP_LINK_RESET = 0x59,     /* gps_link_reset */
  /* master_reset's alone: its lookup runs gps_init, then it
   * sends @@Cf and the defaults */
  GPS_SCRIPT_INIT_DEFAULTS = 0x5b,
  /* power-up's and init_gps's: every setting, without @@Cf or a position */
  GPS_SCRIPT_INIT = 0x5c,
  GPS_SCRIPT_FIX_RESTART = 0x5d,     /* idle and fix again, the PPS check off */
  GPS_SCRIPT_ENGINE_SELFTEST = 0x5e, /* @@Ea off, @@Ca and @@Fa, idle */
  GPS_SCRIPT_OUTPUTS_ON = 0x5f,      /* @@Ba, @@Ea, @@Bb and @@Bo each second */
  GPS_SCRIPT_OUTPUTS_OFF = 0x60,     /* @@Ba, @@Ea, @@Bb, @@Bc and @@Bk off */
  GPS_SCRIPT_QUERY_SETTINGS = 0x61,  /* each setting's query, @@Ta and @@Gj */
  GPS_SCRIPT_UTC_SET = 0x62,         /* @@Aw from its arguments, @@Ba, @@Ea */
  GPS_SCRIPT_HOLD_AT =
      0x63, /* @@As from its arguments, hold on, TRAIM, restart */
  GPS_SCRIPT_POSITION_SET = 0x64,  /* @@Ad, @@Ae, @@Af, idle around them */
  GPS_SCRIPT_HOLD_RELEASE = 0x65,  /* survey stop, average reset, hold off,
                                      TRAIM, restart */
  GPS_SCRIPT_SURVEY_FROM = 0x66,   /* hold off, @@Ad to @@Af from its
                                      arguments, TRAIM, restart, survey start,
                                      average reset */
  GPS_SCRIPT_HOLD_OFF = 0x67,      /* hold off, TRAIM, average reset */
  GPS_SCRIPT_HOLD_AT_QUIET = 0x68, /* @@As, hold on, TRAIM, no restart */
  GPS_SCRIPT_HOLD_SELECT = 0x69,   /* @@At from its arguments, TRAIM, restart */
  GPS_SCRIPT_HOLD_POSITION = 0x6a, /* @@As from its arguments, restart */
  GPS_SCRIPT_TRAIM = 0x6b,         /* @@Bn and @@En from their arguments */
  GPS_SCRIPT_TIME_SET = 0x6c,      /* @@Aa from its arguments, idle around it */
  GPS_SCRIPT_DATE_SET = 0x6d,      /* @@Ac from its arguments, idle around it */
  GPS_SCRIPT_SATELLITE_SELECT = 0x6e, /* @@Ah 1, each channel's @@Ai cleared,
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

/* A GPS Receiver Command Log entry, as the 58503B's command_log_add
 * builds it in its frame from the running command and the sent text
 * (0x53744 to 0x5376e) and copies it into the log, 0x14 bytes: the
 * 58503A 3704's fields, but the sender first and the code after it, as
 * gps_print_command reads them (0x53838). */
typedef struct gps_command_log_entry {
  gps_request_sender sender;     /* +0x00: the command's +0x8; printed PLL,
                                    SCPI1, GPSc and GPSs, else "?" */
  uint8_t field_01;              /* never written */
  gps_request_code_word code;    /* +0x02: the command's +0xa, printed under
                                    "ID" */
  gps_request_code_word message; /* +0x04: the command's +0x1e, its index in the
                                    message table at oncore_messages, whose ID
                                    prints (ext.l gps_print_command+0xe4) */
  char argument_text[3];         /* +0x06: characters 4 and 5 of
                                    gps_message_to_text's text, the first byte
                                    after the ID in hex; NUL */
  gps_argument_mode query;       /* +0x09: the command's; printed q, else c */
  uint8_t from_clock; /* +0x0a: 1 when the clock is set, the date and time its;
                         0 for a clock not set since power-up (get_date_time's
                         1), the date and time @@Ba's.  Printed blank, else e */
  uint8_t field_0b;   /* never written */
  uint32_t date; /* +0x0c: the clock's while from_clock, else @@Ba's, local */
  time_of_day
      time; /* +0x10: likewise; gps_print_command passes it to format_time */
} gps_command_log_entry;

/* The GPS Receiver Command Log, read and write at +0x3fc and +0x400
 * (command_log_add, 0x53788 to 0x537c8): a ring at command_log of 51
 * entries, the oldest at read, the next free at write; empty when the
 * two are equal, and full at 50, when command_log_add drops the oldest.
 * gps_print_command takes the oldest. */
typedef struct gps_command_log {
  gps_command_log_entry entries[51];
  gps_command_log_entry* read;  /* +0x3fc */
  gps_command_log_entry* write; /* +0x400 */
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
 * TC, time offset and cable delay, the 58503A 3704's, and the EFC
 * calibration after them, the Z3817A 3752's layout, each field where
 * the 58503B's save_calibration_block builds it (0x40aba to 0x40b08). */
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
  float tc_coefficient_second; /* +0x18: tc_coefficient_second (0x40aca),
                                  read back 0 while erased (0x40d2a) */
  uint8_t oscillator;          /* +0x1c: oscillator_type (0x40b02), which
                                  oscillator_type_read loads back (0x40c82) */
  uint8_t unused[35];          /* +0x1d: left erased */
} calibration_block;

/* An oscillator type's EFC calibration, an entry of efc_oscillators
 * (0x40dfc), by oscillator_type: :DIAGnostic:ROSCillator:EFControl:ASLOPe's
 * default, which efc_calibration_defaults takes (0x40b8a), then its least
 * and greatest, which efc_calibration_limits takes (0x40cc8, 0x40cd4). */
typedef struct efc_oscillator_entry {
  float efc_a;         /* +0x00 */
  float efc_a_minimum; /* +0x04 */
  float efc_a_maximum; /* +0x08 */
} efc_oscillator_entry;

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

/* The EFC history: a ring of 64 averages of the EFC, one each 2700
 * seconds of uptime, which the holdover prediction draws on.  Its
 * routine's debug message names its arrays: "e_avg= time= weight= log=
 * tfom=".  The 58503B 1.01.04's own, 0x66e bytes, each field where
 * efc_history_update (0x46186) and the fits reach it: the 58503A 3704's
 * 64 entries, with the Z3815A 4010's replaced entries after each array
 * but the logs' (0x46294 to 0x462b6) and the three arrays of each
 * entry's fit after the logs (measured_uncertainty_update), and no
 * sample count but the summed one. */
typedef struct efc_ring {
  uint8_t start_ptr;       /* +0x000: the oldest entry */
  uint8_t stop_ptr;        /* +0x001: the next to fill */
  uint8_t loop_settled;    /* +0x002: startup_pll sets it on settling without a
                              fit; aging_state then goes to 3 */
  uint8_t field_003;       /* nothing refers to it */
  uint16_t field_004;      /* nothing refers to it */
  uint16_t summed_count;   /* +0x006: samples summed toward this entry, whose
                              count gives its weight (0x462be to 0x46316) */
  float averages[64];      /* +0x008: e_avg, efc_sum / summed_count */
  float replaced_average;  /* +0x108: averages[stop_ptr] before the entry is
                              filled again */
  uint8_t weights[64];     /* +0x10c: weight, 1 to 3 */
  uint8_t replaced_weight; /* +0x14c: weights[stop_ptr], likewise */
  uint8_t field_14d;       /* nothing refers to it */
  float times[64];         /* +0x14e: time */
  float replaced_time;     /* +0x24e: times[stop_ptr], likewise */
  float logs[64];          /* +0x252: log */
  float fit_a_history[64]; /* +0x352: aging_curve.a, by
                              measured_uncertainty_update */
  float fit_b_history[64]; /* +0x452: aging_curve.b */
  float fit_c_history[64]; /* +0x552: aging_curve.c */
  uint32_t next_entry_uptime; /* +0x652: the uptime the next entry is due at,
                                 2700 on each (0x461d4, 0x46282) */
  float efc_sum;              /* +0x656: the samples, less efc_first, summed */
  float efc_first;   /* +0x65a: the entry's first sample, the EFC less its
                        temperature term (0x4621a), which each sample is summed
                        less (0x46234) */
  int32_t field_65e; /* nothing refers to it */
  float fit_a;       /* +0x662: the curv task's aging fit a + b y + c ln y */
  float fit_b;       /* +0x666 */
  float fit_c;       /* +0x66a */
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
 * (time_of_day's own) putting the longs and words on even addresses, as
 * the @@Ba and @@Ea descriptors unpack it (gps.md). */
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
  uint8_t mode;           /* the Bn and En decoders copy the channel's
                             @@Ea mode here (0x57f98) */
  uint32_t time_estimate; /* ns */
} oncore_raim_channel;

/* The Time RAIM record, @@Bn's and @@En's, which their decoders fill by
 * their field tables (0x5a1d3, 0x5a1e7): the Z3817A 3752's layout, the
 * alarm limit a long, 10 ns, where the 58503A's is a word of 100 ns
 * after a pad, so that from pps_mode on each field is a byte further
 * in; the sawtooth at +0x1a as in the others (gps.md, "Where the
 * sawtooth goes"); its eight channels after it, each with the channel's
 * @@Ea mode, which the decoders copy in (0x57f98). */
typedef struct oncore_raim_fields {
  uint8_t rate;             /* +0x00: as last requested */
  uint8_t raim_on;          /* +0x01 */
  int32_t alarm_limit;      /* +0x02: 10 ns */
  oncore_pps_mode pps_mode; /* +0x06 */
  uint8_t field_07[0x14 - 0x07];
  uint8_t pulse_status;         /* +0x14: the 1PPS pulse on, which the Time RAIM
                                   page prints (0x4e4b6) */
  oncore_pulse_sync pulse_sync; /* +0x15 */
  oncore_raim_solution solution_status; /* +0x16 */
  oncore_raim_status raim_status;       /* +0x17 */
  uint16_t sigma;                       /* +0x18: ns */
  int8_t sawtooth;                      /* +0x1a: ns */
  uint8_t field_1b;
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

/* gps.field_608's bits in this image, by what sets and tests them, the
 * Z3816A 4001's as the high byte of its word: the receiver ID's commit
 * sets the signal level's kind, "B"'s or "R"'s (0x5813e, 0x581aa), from
 * which status_screen takes the level column's heading and the
 * elevation mask line (0x516d8, 0x518fc), and with "B" bits 0 and 5 by
 * its software (0x5814e to 0x5818c) and bit 6 (0x56f80), with "R" bits
 * 0, 5 and 7 (0x56f46) and bit 6 by its software (0x581ba); the
 * eight-channel decoders set bit 7 (0x57c9a); the @@Bn and @@En decoders
 * clear bit 3 (0x57fc8, 0x58058).  Nothing sets bit 4, the MSL height's
 * (gps.md, "The 58503B's height datum").  Bit 0 has no value here: an
 * enum naming all eight bits makes Ghidra write a test for none as one
 * against all of them complemented, which C takes as nonzero. */
typedef enum __attribute__((packed)) gps_field_608_bit {
  GPS_FIELD_608_SIGNAL_SS = 0x02, /* "   SS" */
  GPS_FIELD_608_SIGNAL_CN = 0x04, /* "  C/N" */
  GPS_FIELD_608_BIT_3 = 0x08,
  GPS_FIELD_608_HEIGHT_MSL = 0x10,
  GPS_FIELD_608_TIME_RAIM = 0x20, /* gps_message_supported's for @@Bn and
                                     @@En (0x5848e) */
  GPS_FIELD_608_BIT_6 = 0x40,
  GPS_FIELD_608_EIGHT_CHANNELS = 0x80,
} gps_field_608_bit;

/* gps_receive_state: where gps_receive is in framing an Oncore message
 * into gps_response_buffer, the 58503A 3704's framer, relocated here
 * (docs/firmware/gps.md, "Framing").  A state above GPS_FRAMING_COMPLETE
 * drops each byte. */
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

/* gps.time_status, how far the engine's time has come, the Z3816A
 * 4001's, as oncore_position_message keeps it after each @@Ba or @@Ea
 * (0x57942): UTC while the engine gives UTC time (gps.utc_time) and
 * gps.utc_offset_received is set; else UNTRACKED until a channel tracks a
 * satellite available for position or utc_offset_received is set, then
 * GPS, never UNTRACKED again until gps_init.  status_snapshot keeps it,
 * and status_screen_time takes the scale as UTC once it is UTC and marks
 * the time "[?]" while it is UNTRACKED, else "(?)", while the clock is
 * not set (0x506bc).  The names, what each says of the time, are
 * inferred from the values' tests. */
typedef enum __attribute__((packed)) gps_time_status {
  GPS_TIME_STATUS_UNTRACKED = 1, /* no satellite usable for position yet */
  GPS_TIME_STATUS_GPS = 2,       /* the engine's time, its UTC offset not
                                    applied */
  GPS_TIME_STATUS_UTC = 3,
} gps_time_status;

/* gps.status_received: whether @@Ba's or @@Ea's status has come, and
 * lately, as the status monitor keeps it (command_monitor, 0x52310 to
 * 0x523d4), the 58503A 3704's. */
typedef enum __attribute__((packed)) gps_status_freshness {
  GPS_STATUS_NONE = 0,  /* none since the monitor last failed */
  GPS_STATUS_STALE = 1, /* none for three of its periods */
  GPS_STATUS_FRESH = 2,
} gps_status_freshness;

/* The GPS task's state: what its decoders store of each Oncore message,
 * in the order of the message table, and its own state after.  The
 * 58503B 1.01.04's: 0x61e bytes, the 0x187 longs and a word pllp_task
 * copies into preserved_region (0x4ce0a).  Through the almanac status
 * its records are laid as the Z3817A 3752's, the 58503A 3704's, which
 * the 58503B's Oncore code, the 58503A's, reads (the @@Ba and @@Ea
 * status at +0xd4, gps.md, "The 58503B's height datum").  From the fix
 * mode on, the 58503A's own state, 0x48 bytes on, each field where this
 * image's code reaches it (make record-access): four DCXO fields after
 * the receiver ID, 0x18 bytes of the instrument state page's after the
 * survey's, two longs of the command monitor's before the status time,
 * and two after the status monitor's.  The records between the almanac
 * status and the fix mode are not yet laid
 * (docs/firmware/58503b-1.01.04.md, "Types"). */
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
  uint8_t field_198[0x19d - 0x198];
  oncore_xdop_table_fields xdop_table; /* +0x19d: @@Bc, its descriptor's
                                          record (0x59284) */
  uint8_t field_1e8[2];
  oncore_almanac_status_fields almanac; /* +0x1ea: @@Bd (0x592c0) */
  uint8_t field_1fa[0x2ba - 0x1fa];
  oncore_corrections_fields corrections; /* +0x2ba: @@Ce (0x595cc) */
  double correction_average;             /* +0x2fa: m, printed AVG */
  double correction_deviation;           /* +0x302: m, printed STD DEV */
  uint8_t field_30a[4];
  oncore_extension_fields extension; /* +0x30e: @@Bk, @@Ek (0x59428,
                                        0x59734) */
  oncore_raim_fields raim;           /* +0x34e: @@Bn, @@En (0x594a0,
                                        0x59770) */
  uint8_t raim_received;             /* +0x39a: the Bn and En decoders set it
                                        (0x57fdc, 0x5806c); command_monitor
                                        takes and clears it (0x5238c) */
  uint8_t field_39b;
  /* What raim_monitor keeps as it detects an alarm (0x51b76 to 0x51b90):
   * the date and time 0x50e8c gives, the one-sigma estimate it last saw
   * with an OK solution, and the channels' time estimates; the Time RAIM
   * page prints them (gps_print_time_raim, 0x4e5fc to 0x4e708). */
  uint32_t raim_alarm_date;                   /* +0x39c */
  time_of_day raim_alarm_time;                /* +0x3a0 */
  uint16_t raim_alarm_sigma;                  /* +0x3a4 */
  oncore_raim_channel raim_alarm_channels[8]; /* +0x3a6 */
  uint16_t raim_last_sigma;    /* +0x3d6: raim.sigma while the solution is OK
                                  (0x51b32) */
  uint16_t self_test_response; /* +0x3d8: @@Ca's, @@Fa's, as the engine
                                  gives it (0x57140's record, 0x1015b8) */
  uint8_t sz_received;         /* +0x3da: @@Sz's decoder sets it (0x58224's
                                  record, 0x1015ba) */
  uint8_t field_3db;
  uint16_t self_test_mask;         /* +0x3dc: the failures self_test keeps,
                                      gps_request_build's at the request
                                      (0x585ce), then self_test's after each
                                      response (0x571e4) */
  gps_antenna_sense antenna_sense; /* +0x3de: 0x5706c's, from @@Ea's
                                      receiver status (0x57cb2) */
  uint8_t field_3df;
  uint16_t self_test; /* +0x3e0: self_test_mask and the response's failures
                         as the Oncore's generic bits (0x5709c), which
                         gps_selftest_run reads (0x4ba82) */
  uint8_t field_3e2[0x44e - 0x3e2];
  oncore_fix_mode fix_mode; /* +0x44e: @@Cg; status_snapshot copies it */
  uint8_t field_44f;
  uint8_t receiver_id_received;   /* +0x450: set by @@Cj's decoder */
  oncore_receiver_id receiver_id; /* +0x451: @@Cj, whose fields gps_print_id
                                     prints from +0x451 to +0x54b */
  uint16_t field_566; /* :DIAGnostic:GPSystem:DCXO? and the instrument state
                         page print it, with the next two */
  uint8_t field_568;
  uint8_t field_569;
  uint16_t field_56a;
  uint8_t field_56c; /* gps_init clears it */
  uint8_t field_56d;
  uint32_t average_count;             /* +0x56e: positions averaged */
  double average_latitude;            /* +0x572: of @@Ba's, while surveying */
  double average_longitude;           /* +0x57a */
  double average_height_gps;          /* +0x582 */
  double average_height_msl;          /* +0x58a */
  gps_reference_record references[2]; /* +0x592: the GPS's, then the external
                                         1PPS's */
  uint8_t surveying;                  /* +0x5be: averaging @@Ba's position */
  uint8_t field_5bf; /* gps_init sets it; gps_receiver_status_update reads it;
                        with field_5c0, status_snapshot's "EST " */
  uint8_t field_5c0;
  uint8_t field_5c1;
  oncore_leap_pending
      leap_second_reported; /* +0x5c2: @@Bj's record (0x56f34), which the
                               instrument state page prints with what follows
                               to +0x5d4 (gps_print_instrument_state) */
  uint8_t field_5c3;
  uint8_t field_5c4;
  uint8_t field_5c5;
  uint16_t field_5c6;
  uint8_t field_5c8;
  uint8_t field_5c9;
  uint8_t field_5ca;
  uint8_t field_5cb;
  uint32_t field_5cc;
  uint8_t field_5d0;
  uint8_t field_5d1;
  uint8_t field_5d2;
  uint8_t field_5d3;
  oncore_leap_pending
      leap_second_pending;     /* +0x5d4: @@Bj's decoder's copy of
                                  leap_second_reported (0x56f36), which
                                  leap_event_create tests (0x4b69c), as the
                                  58503A's tests the record itself */
  gps_time_status time_status; /* +0x5d5: oncore_position_message's */
  uint8_t utc_offset_received; /* +0x5d6: the Z3816A 4001's, @@Bo's record
                                  nonzero */
  uint8_t field_5d7;
  gps_raim_assessment raim_assessment; /* +0x5d8: 0 inactive, 1 OK, 2 alarm
                                          detected, 3 alarm */
  uint8_t pps_check_disabled;     /* +0x5d9: gps_pps_check skips while set */
  uint8_t receiver_health_failed; /* +0x5da: "GPS Receiver Health" */
  uint8_t field_5db;
  uint8_t command_monitor_enabled;                 /* +0x5dc */
  gps_command_monitor_state command_monitor_state; /* +0x5dd */
  gps_script_step** command_monitor_query; /* +0x5de: its next query, a place
                                              in command_monitor_queries */
  uint32_t command_monitor_time; /* +0x5e2: uptime of its last state change */
  uint8_t command_monitor_error; /* +0x5e6: set by gpsm_task when a command goes
                                    unanswered */
  uint8_t command_monitor_failed; /* +0x5e7 */
  uint8_t status_monitor_enabled; /* +0x5e8 */
  gps_status_monitor_state
      status_monitor_state;     /* +0x5e9: 0 disabled, 1 standby, 2 recovery */
  uint32_t status_monitor_time; /* +0x5ea: uptime of the last fresh @@Ba */
  uint32_t field_5ee;           /* command_monitor's, with the next */
  uint32_t field_5f2;
  uint32_t status_last_time; /* +0x5f6: the last @@Ba's time bytes, compared as
                                one with the next's (0x57b12), as the Z3817A
                                3752's and the 58503A 3704's */
  uint8_t status_repeat_count; /* +0x5fa: @@Ba with the same time in a row */
  uint8_t status_monitor_timed_out; /* +0x5fb */
  uint8_t status_monitor_failed;    /* +0x5fc */
  uint8_t field_5fd;
  uint32_t field_5fe; /* poll_monitor's and gps_init's, with the next */
  uint32_t field_602;
  uint8_t channel_count; /* +0x606: the engine's channels; the channel loops'
                            bound */
  uint8_t field_607;
  gps_field_608_bit field_608; /* +0x608: the 58503A 3704's field_592's bits,
                                  set by @@Ea's decoders and from @@Cj */
  uint8_t field_609;
  uint8_t tracking_log;        /* +0x60a: nv_settings.tracking_log, copied by
                                  gps_init */
  uint8_t tracking_log_active; /* +0x60b: tracking_log, as the receiver state
                                  update takes it */
  uint32_t track_words[3];     /* +0x60c: the tracking log's channels, packed by
                                  gps_track_words_pack, as the 58503A's */
  uint32_t channel_word;       /* +0x618: gps_channel_word_pack's */
  uint16_t channel_word_lowest; /* +0x61c: gps_channel_word_lowest's */
} gps_state;

/* The satellites' map and hourly counts the Sat View page prints
 * (gps_print_sat_view), sky_statistics, 0x324 bytes at 0x102206: what
 * sky_statistics_update gathers from the satellites on the channels
 * each time gpsm_task runs it (0x5349e), and sky_statistics_clear
 * clears (0x5421c), the Z3816A 4001's at 0x101ee2, both unnamed there.
 * The map is a nibble for each 5 degrees of elevation, 0 to 85, and of
 * azimuth, 0 to 355: a symbol of sky_map_symbols, the level class of a
 * used satellite last seen there or the Doppler's sign of one tracked
 * but not used (0x54312 to 0x54470). */
typedef struct sky_statistics_record {
  uint8_t map[72][9];     /* +0x000: by azimuth / 5, then elevation / 10, the
                             high nibble for the upper 5 degrees */
  uint32_t hours_seen;    /* +0x288: bit h for each hour of the day sampled */
  uint8_t used_range[24]; /* +0x28c: the fewest satellites used in each hour
                             in the high nibble, the most in the low; 0xf0
                             as the hour starts */
  uint8_t hour;           /* +0x2a4: the hour last sampled */
  uint8_t unused_average[24]; /* +0x2a5: the satellites tracked but not used,
                                 averaged over the hour, rounded */
  uint8_t field_2bd;          /* not written */
  float average;              /* +0x2be: the running average they are */
  uint16_t samples;           /* +0x2c2: the samples it takes, to 7201 */
  /* +0x2c4: for each hour the Doppler bins (6 less the Doppler in 500 Hz
   * steps, 0 to 13) of the satellites tracked but not used, then of those
   * used, a bit each */
  struct {
    uint16_t unused;
    uint16_t used;
  } doppler_bins[24];
} sky_statistics_record;

/* An entry of fpga_bit_table (0x45aaa, eight entries), which fpga_set_bit
 * and fpga_set_field take: a register of fconf_regs and its mask.  The
 * first five are every image's; after them,
 * read from the table (0x45abe to 0x45ac6), the Z3815A's seconds phase
 * flag in register 2, then register 3's sync field and the 58503A's alarm
 * flag, where the Z3815A's keeps its start source field. */
typedef enum __attribute__((packed)) fpga_bit_index {
  FPGA_BIT_MODE = 0,   /* register 0, 0x03: an fpga_mode */
  FPGA_BIT_PRESET = 1, /* register 0, 0x40: set around arming; counter_selftest
                          reads a count of 0 under it */
  FPGA_BIT_START = 2,  /* register 1, 0x01: pulsed by interpolator_measure */
  FPGA_BIT_REFERENCE = 3, /* register 1, 0x02: an interpolator_reference */
  FPGA_BIT_ADDRESS = 4, /* register 1, 0xf8: the address bits fpga_read uses */
  /* register 2, 0x01, a flag: pps_sync_pulse pulses it on an odd GPS
   * second, as the Z3815A's pulses its own (docs/firmware/z3815a-4010.md) */
  FPGA_BIT_SECONDS_PHASE = 5,
  /* register 3, 0x03, a field: the sync kind, which counter_set_sync
   * writes directly */
  FPGA_BIT_SYNC = 6,
  /* register 3, 0x04, a flag: the alarm, as the 58503A's */
  FPGA_BIT_ALARM = 7,
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
 * efc_calibration_measure's switch takes them (its jump table, 0x4d37a);
 * any other starts it again.  A point takes three readings, where the
 * other images' take two; nothing goes to the ADC's two steps, the fit
 * going on to the check (0x4d59a). */
typedef enum __attribute__((packed)) efc_calibration_step {
  EFC_CALIBRATION_START = 0,
  EFC_CALIBRATION_WAIT_REFERENCE = 1,
  EFC_CALIBRATION_SETTLE = 2,
  EFC_CALIBRATION_FIRST_READING = 3,
  EFC_CALIBRATION_SECOND_READING = 4,
  EFC_CALIBRATION_THIRD_READING = 5,
  EFC_CALIBRATION_POINT = 6,
  EFC_CALIBRATION_ADC_LOW = 7,
  EFC_CALIBRATION_ADC_HIGH = 8,
  EFC_CALIBRATION_CHECK = 9,
} efc_calibration_step;

/* The OCXO calibration's measurement's state, at 0x10332a (make
 * record-access; efc_calibration_measure's and efc_calibration_fit's,
 * which index frequency_offset by 4 from 0x10332e, 0x4d41e). */
typedef struct efc_calibration_fields {
  efc_calibration_step step;  /* +0x00 */
  uint8_t point;              /* +0x01: the EFC's point, a tenth of its range
                                 each, "point= %d"; the ADC's readings' count */
  uint8_t field_02;           /* +0x02: cleared, never read */
  uint8_t field_03;           /* nothing refers to it */
  float frequency_offset[10]; /* +0x04: each point's, "delta_f" */
  int32_t adc_low;            /* +0x2c: the ADC at a tenth of the range,
                                 "low= %d"; nothing here sets it */
  int32_t adc_high;           /* +0x30: at nine tenths, "high= %d"; nor
                                 this */
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

/* gps_leap_ack's states, pll.leap_ack_state (0x4c0dc, its jump table at
 * 0x4c128): the 58503A 3704's, the engine's time mode switched with @@Aw
 * to measure the GPS-UTC offset, then switched back, and this image's
 * own for an "R" engine, which takes the offset from @@Bo instead. */
typedef enum __attribute__((packed)) pll_leap_ack_state {
  PLL_LEAP_ACK_IDLE = 0,
  PLL_LEAP_ACK_CHECK = 1,
  PLL_LEAP_ACK_UTC_OFFSET = 2, /* waits for @@Bo's offset, gps.field_5c3 */
  PLL_LEAP_ACK_MODE_SEND = 3,
  PLL_LEAP_ACK_MODE_ACK = 4,
  PLL_LEAP_ACK_SWITCH_SEND = 5,
  PLL_LEAP_ACK_SWITCH_ACK = 6,
  PLL_LEAP_ACK_BACK_SEND = 7,
  PLL_LEAP_ACK_BACK_ACK = 8,
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
 * values.  The 58503B 1.01.04's own, 0x6fe bytes, the 0x1bf longs and a
 * word pllp_task copies into preserved_region (0x4ce24), each field where
 * this image's code reaches it (make record-access): the 58503A 3704's
 * fields in its order, no n3 lock pll's, with two longs of holdover's
 * after the hold causes, a byte of fine's before its retries, the
 * measured time uncertainty after the predicted, the EFC history this
 * image's own (efc_ring), and after it the Z3815A 4010's tail, the aging
 * term and the loop's EFC step, the holdover start's uptime and three
 * bytes of its own near the end.  A field no code reaches is named by its
 * offset. */
typedef struct pll_state {
  pll_stage stage;
  pll_stage previous_stage;  /* +0x01: pllp_task's copy of stage */
  uint8_t powerup_countdown; /* +0x02: readings left before powerup checks the
                                clock against GPS */
  pll_powerup_state powerup_state; /* +0x03: powerup's sub-state,
                            screen-format-strings.md's ladder */
  uint8_t powerup_reference_lost;  /* +0x04: GPS or its PPS went, or temporary
                                      mode; powerup goes back to state 3 */
  uint8_t field_005;
  pll_reference_check_state
      reference_check_state; /* +0x06: pll_reference_check's */
  uint8_t field_007[5];
  pll_startup_state startup_state; /* +0x0c: startup_pll's, which
                                      efc_history_update tests in stage 5 */
  uint8_t field_00d;
  pll_normal_state normal_state; /* +0x0e: pll_normal's, through a pointer in
                                    its frame */
  uint8_t field_00f;
  pll_hold_cause hold_causes; /* +0x10: a set of pll_hold_cause bits, a word
                                 (diag_pr_hold_cause) */
  pll_holdover_state holdover_state; /* +0x12: holdover's */
  uint8_t manual;                    /* +0x13 */
  uint8_t ti_limit_override; /* +0x14: holdover checks the TI limit while 0;
                                holdover_recovery_limit_ignore sets it */
  uint8_t field_015;
  float holdover_efc_offset; /* +0x16: holdover's, from the temperature term,
                                the EFC and efc_history.fit_a as it starts
                                (0x48a58) */
  float holdover_efc_step;   /* +0x1a: holdover_efc_offset / 15 (0x48a68) */
  uint8_t
      recovery_countdown; /* +0x1e: holdover_recovery's, as powerup_countdown */
  pll_recovery_state recovery_state; /* +0x1f: recovery_suffix_index's */
  pll_coarse_state coarse_state;     /* +0x20: coarse's */
  pll_fine_state fine_state;         /* +0x21: fine's; PLL_FINE_SLEW to
                                 PLL_FINE_FINE_SLEW slew the phase */
  uint8_t frequency_adjusted; /* +0x22: fine applied a frequency correction */
  uint8_t fine_index;         /* +0x23: fine acquisition's "i= %d" */
  uint8_t fine_limit;         /* +0x24: fine sets it (0x4a482) and compares
                                 fine_index with it (0x4a79e) */
  uint8_t field_025;
  uint16_t fine_retries; /* +0x26: fine's failed attempts */
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
  uint8_t leap_ack_requested;            /* +0x39: leap_ack_request sets it */
  uint8_t leap_ack_done;                 /* +0x3a: the offset is set */
  uint8_t field_03b;
  uint8_t
      leap_ack_again; /* +0x3c: holdover_recovery measures again after fine */
  uint8_t field_03d;
  float efc;                   /* +0x3e: pr_efc's efc */
  float tfom;                  /* +0x42: pll_rep's tfom, s */
  float predicted_uncertainty; /* +0x46: the curv fit's, :KENneth:PREDicted? */
  float measured_uncertainty;  /* +0x4a: :KENneth:MEASured?, which
                                  measured_uncertainty_update computes */
  uint8_t field_04e[8];
  float frequency_tolerance; /* +0x56: fine's bound, holdover's limit per
                                second; pll_init and pll_normal set it */
  efc_ring efc_history;      /* +0x5a */
  float aging_term;    /* +0x6c8: aging_fit_collect's, the fit's slope at the
                          present uptime */
  float loop_efc_step; /* +0x6cc: startup_pll's EFC step */
  float ti_filtered;   /* +0x6d0: the TI, filtered */
  float efc_base;      /* +0x6d4: the EFC less the temperature term */
  float loop_time_constant; /* +0x6d8: :DIAGnostic:ROSCillator:LTIMe:DATA? */
  uint8_t field_6dc;        /* nothing refers to it */
  uint8_t led_gps_lock;     /* +0x6dd: :LED:GPSLock? */
  uint8_t led_holdover;     /* +0x6de: :LED:HOLDover? */
  uint8_t field_6df;
  uint32_t holdover_duration;     /* +0x6e0: :ROSCillator:HOLDover:DURation? */
  uint32_t holdover_start_uptime; /* +0x6e4: as enter_holdover starts the
                                     count (holdover_duration_count) */
  uint32_t reference_uptime;      /* +0x6e8: pllp_task's */
  time_of_day reference_time;     /* +0x6ec: the @@Ba time pllp_task stores
                                     (0x4cdda), which clock_check_against_gps
                                     compares the GPS time with */
  uint32_t efc_control_passes;    /* +0x6f0: efc_mode_expire's count toward
                                     clearing HOLD_CAUSE_EFC_CONTROL */
  uint8_t reference_seconds;      /* +0x6f4: the FPGA's seconds count with it */
  fpga_configuration_index
      fpga_configuration; /* +0x6f5: the configuration fpga_setup loaded and
                             fpga_load_test loads again */
  uint8_t fpga_loaded;    /* +0x6f6: the PPS checks wait for it */
  uint8_t loop_running;   /* +0x6f7: pll_restart sets it; while set,
                             pps_sync_pulse pulses the FPGA's sync bit */
  uint8_t sync_requested; /* +0x6f8: sync_perform syncs whatever the TI, then
                             clears it */
  uint8_t field_6f9;      /* pll_init clears it, pll_stage_status tests it and
                             status_snapshot copies it */
  uint8_t field_6fa;      /* nothing refers to it */
  uint8_t field_6fb;      /* pll_init sets it, pll_tfom_digit_store writes it */
  uint8_t block_seconds;  /* +0x6fc: readings to a block of holdover's and
                             the loops' steering, 10, which pll_init sets
                             (0x4c75e); the other images' constant */
  uint8_t measured_uncertainty_valid; /* +0x6fd: measured_uncertainty_update's
                                         result, :KENneth:MEASured?'s test */
} pll_state;

/* The GPS link's counters, gps_counts at 0x10192a, which the "GPS
 * Process Monitor" page (gps_print_process_monitor, 0x53a18) prints
 * through A2.  Cleared as one, 0x40 bytes (clear_gps_debug, 0x53918).
 * All words but responses_read, a long (0x53ae0); the 58503A's through
 * the Time RAIM counts, then four words nothing refers to, and the
 * page's "? Resp", "Frame" and "Cksum" (0x53b9a, 0x53bce, 0x53c02) and
 * the monitors' failures (0x53ea0, 0x53f14), each where the image's code
 * reaches it (make record-access IMAGE=58503b-1.01.04 RECORD='0x10192a
 * 0x40 --fit gps_statistics'). */
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
  uint16_t raim_detected;    /* +0x2a: Time RAIM page: (%d detected)
                                (0x4e452) */
  uint16_t raim_alarms;      /* +0x2c: # Alarms (0x4e45c) */
  uint16_t field_2e[4];      /* nothing refers to them */
  uint16_t unknown_response; /* +0x36: ? Resp: an ID not in oncore_messages
                                (gps_response_decode, 0x586be) */
  uint16_t frame_errors;     /* +0x38: Frame (gps_receive_expect, 0x5262c) */
  uint16_t checksum_errors;  /* +0x3a: Cksum (0x5278a) */
  uint16_t command_monitor_failures; /* +0x3c: command_monitor's (0x5214e) */
  uint16_t status_monitor_failures;  /* +0x3e: the status monitor's
                                        (0x5237e) */
} gps_statistics;

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
 * one for each command group (scpi_group_cells, 0x102680, 12 bytes, up
 * to interval_reading at 0x10268c), placed by the descriptors' 85
 * group_cell pointers.  No code reads or writes them, only
 * primary_reset's clear (0x14616), so each is a word by their spacing
 * alone.  The groups are the Z3805A's, but none is :SAMPle's, so the
 * second, the Z3805A's sample, is named for its first, :GPS:INITial. */
typedef struct scpi_group_cells_fields {
  uint16_t serial_1;    /* +0x00: :SYSTem:COMMunicate:SERial1's and :FDUPlex's
                           (0x47aa0 on) */
  uint16_t gps_initial; /* +0x02: :GPS:INITial's, :SYSTem:PRINt's,
                           scpi_param_DOUGlas_STATe_request's and
                           :DIAGnostic:ROSCillator:EFControl's (0x47c08 on) */
  uint16_t pulse;       /* +0x04: the output state's, in nv_save's second
                           table (0x47e0c), and :PULSe's (0x47e28 on) */
  uint16_t serial_2;    /* +0x06: :SYSTem:COMMunicate:SERial2's (0x47f44 on) */
  uint16_t status;      /* +0x08: :STATus's (0x47fb6 on) */
  uint16_t rest;        /* +0x0a: every other descriptor's, 0x48124 to
                           0x48aea */
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
 * (oncore_message_entry.match_kind; its jump table at 0x5877c), this
 * image's own, the Z3816A 4001's code, where the 58503A 3704's kinds are
 * twelve: the kinds that compare take the response as the command's
 * echo, its bytes after the ID the same (VPCommands.pdf for each
 * message), and those that name a message are answered by it alone. */
typedef enum __attribute__((packed)) oncore_match_kind {
  ONCORE_MATCH_ANY = 0,        /* answered at once; gpsm_task waits for none */
  ONCORE_MATCH_TIMEOUT = 1,    /* answered by the timeout alone (0x53388);
                                  no message has it */
  ONCORE_MATCH_ECHO = 2,       /* the same message, its bytes the same */
  ONCORE_MATCH_GMT_OFFSET = 3, /* @@Ab: an echo, but a negative offset under
                                  an hour, sign 0xff and hours 0, compared
                                  first as it is, then as sign 0 and its
                                  minutes negated */
  ONCORE_MATCH_HEIGHT = 4,     /* @@Af: the height alone, the response's MSL
                                  one for type 1, answered too when
                                  oncore_hold_heights_match finds them
                                  close */
  ONCORE_MATCH_SECOND_AR = 5,  /* the second @@Ar, entry 0x3e: an echo of
                                  entry 0x3e (0x587a6) */
  ONCORE_MATCH_HOLD_POSITION = 6, /* @@As: latitude and longitude alone when
                                     the type is the same and
                                     oncore_hold_heights_match finds the
                                     heights close */
  ONCORE_MATCH_HOLD_HEIGHT = 7,   /* @@Au: as ONCORE_MATCH_HEIGHT, the
                                     response's height as it is */
  ONCORE_MATCH_FIX_MODE = 8,      /* @@Cg: an echo, but mode 2, a query of the
                                     mode, matched at once */
  ONCORE_MATCH_SAME = 9,          /* the same message, its bytes not compared */
  ONCORE_MATCH_USER_DATUM = 10,   /* @@Ao: answered by @@Ap */
  ONCORE_MATCH_SAME_SIGNAL_CN = 11, /* @@Bo: as ONCORE_MATCH_SAME, but gpsm_task
                                       waits for it from an "R" engine alone
                                       (0x532e8) */
  ONCORE_MATCH_SELFTEST = 12,       /* the second @@Ca, entry 0x3b: answered by
                                       the first, entry 0x2b */
  ONCORE_MATCH_NEVER = 13,          /* never answered; no message has it */
  ONCORE_MATCH_CORRECTIONS = 14,    /* @@Bh: answered by @@Ce */
  ONCORE_MATCH_EA = 15,             /* the second @@Ea, entry 0x3a: answered by
                                       the first, entry 0x33 */
  ONCORE_MATCH_FA = 16,             /* the second and third @@Fa, entries 0x3c
                                       and 0x3d: answered by the first, entry
                                       0x36 */
} oncore_match_kind;

/* gps_response_match's verdict on a response, as gpsm_task takes it
 * (0x53422, 0x53436): this image's own, the Z3816A 4001's code, where the
 * 58503A 3704's has no ONCORE_MATCH_TIMEOUT and answers 0 to 2. */
typedef enum __attribute__((packed)) gps_match_result {
  GPS_MATCH_OTHER = 0,    /* not the command's answer */
  GPS_MATCH_TIMEOUT = 1,  /* ONCORE_MATCH_TIMEOUT's: none but the timeout */
  GPS_MATCH_DIFFERS = 2,  /* its answer, its bytes not the command's */
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

/* counter_configuration_fields.start_source_mode: when counter_arm sets
 * FPGA_BIT_START_SOURCE to the configuration's start source (0x4988c);
 * any other value never. */
typedef enum __attribute__((packed)) counter_start_source_mode {
  COUNTER_START_SOURCE_WHEN_WANTED = 1,
} counter_start_source_mode;

/* FPGA_BIT_START_SOURCE's values, in place in register 2's bits 2 and 3,
 * EG.X and EG.Y: the source the FPGA's BG starts the count on, read from the
 * netlist (docs/firmware/z3815a-trace.md, "Register port" and "Start,
 * stop and status bits").  Bit 2, EG.X, also gives P24 its level and
 * P25 the other while P27 is Low ("Every output pin"); the P24 levels
 * the names give assume P27 Low, which is not established.  The firmware
 * writes 0 at init and then 0x08, 0x0c and 0x00 through
 * counter_start_source_request, never 0x04 ("What the firmware takes from P35
 * and P84"). */
typedef enum __attribute__((packed)) fpga_start_source {
  FPGA_START_SOURCE_GPS_PPS = 0x00,      /* P82, the GPS 1 PPS */
  FPGA_START_SOURCE_P84 = 0x04,          /* P84; never written */
  FPGA_START_SOURCE_P78 = 0x08,          /* P78, P24 Low */
  FPGA_START_SOURCE_P78_P24_HIGH = 0x0c, /* P78, P24 High */
} fpga_start_source;

/* The time-interval counter's configuration, counter_configuration at
 * 0x102a00, which counter_arm and counter_read are given.  A mode and a
 * state go in pairs: the mode says when the work is done, the state asks
 * for it or says it is done.  The 58503A's, its counter being the
 * 58503A's: sync_request stores the sync's pair at +0x10 and +0x11, its
 * kind at +0x12 and its count at +0x14 (0x44a66), and counter_calibrate
 * the interpolator's offset and scale at +0x18 and +0x1c (0x4568e).
 * 0x20 bytes, interpolator_calibration_state after them (0x102a20). */
typedef struct counter_configuration_fields {
  uint8_t fpga_registers[4]; /* counter_arm copies them into fconf_regs */
  double calibration;        /* s, which counter_read subtracts */
  counter_registers_mode registers_mode; /* when fpga_registers load */
  counter_work_state registers_state;
  counter_calibrate_mode calibrate_mode; /* when the interpolator calibrates */
  counter_work_state calibrate_state;
  counter_sync_mode sync_mode;   /* when counter_arm syncs */
  counter_work_state sync_state; /* sync_request sets both */
  counter_sync_kind sync_kind;   /* sync_request's first; into fconf_regs[3] */
  uint8_t pad_13;
  int32_t sync_count;          /* sync_request's second; counted down */
  int32_t interpolator_offset; /* reading_low, subtracted from the reading */
  int32_t interpolator_scale;  /* the spread it is divided by */
} counter_configuration_fields;

/* The battery-backed RAM a warm restart restores, preserved_region at
 * 0x100000: pllp_task copies gps, pll and the RAM blocks below into it
 * each pass and checksums it.  The 58503B 1.01.04's: 0xebe bytes,
 * record_checksum's length (system_init, 0x22e30; pllp_task, 0x4ce4c),
 * each block where pllp_task's copies put it (0x4cdf8 to 0x4ce3e). */
typedef struct preserved_region_fields {
  uint16_t
      checksum; /* +0x000: record_checksum's sum and complement of the rest */
  uint16_t restore;            /* +0x002: a preserved_region_restore */
  gps_state gps;               /* +0x004 */
  pll_state pll;               /* +0x622: pll's, 0x1bf longs and a word */
  adc_monitor adc_monitors[8]; /* +0xd20: adc_monitors' */
  uint8_t values[0x1d];        /* +0xea0: retained_block's (0x1028b8) */
  uint8_t field_ebd;           /* in the checksum; nothing else refers to it */
} preserved_region_fields;

#include "../../common/types/clock_record.h"

#include "../../common/types/pps_exchange_message.h"

#include "../../common/types/gps_process_state.h"

#include "../../common/types/satellite_sets.h"

/* A satellite_status_list entry, 10 bytes, the Z3816A 4001's: the
 * stride satellite_status_list steps by (0x4fe7a, 0x4ffb8) and the empty
 * one it copies (0x55b80); status_snapshot keeps sixteen not tracked and
 * eight tracked, twice and once the eight channels.  Its cell carries
 * the satellite's Doppler after the 58503A 3704's four bytes. */
typedef struct satellite_screen_cell {
  satellite_cell cell; /* +0x00: satellite_elevation_azimuth's and
                          satellite_status' */
  int16_t doppler;     /* +0x04: the @@Bb record's, 0x8000 in the empty
                          entry; format_satellite_cell does not read it */
} satellite_screen_cell;

typedef struct satellite_list_entry {
  uint8_t satellite;          /* 0 for none */
  uint8_t attempting;         /* not tracked: on a channel but not used */
  int16_t level;              /* tracked: its channel's signal strength; -1
                                 none */
  satellite_screen_cell cell; /* +0x04 */
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

/* A status snapshot, status_snapshot's record (0x50ee0), which the
 * status screen prints.  The 58503B 1.01.04's own, 0x19e bytes, two of
 * them 0x19e apart (status_screen_print, 0x51a4c), each field where
 * status_snapshot stores it: the 58503A 3704's order, but the time
 * code's TFOM digit where the 58503A keeps pll.tfom, the interval's
 * fields two bytes nearer, sixteen satellites not tracked and eight
 * tracked of 10-byte entries, the GPS record's flags word after the reference
 * records, and three GPS bytes of its own beside the hold flags. */
typedef struct status_snapshot_record {
  pll_stage stage;         /* +0x000: pll.stage */
  uint8_t mode;            /* +0x001: pll_mode's */
  uint8_t powerup_suffix;  /* +0x002: pll_powerup_suffix_index's */
  uint8_t recovery_suffix; /* +0x003: recovery_suffix_index's */
  uint16_t tfom_code;      /* +0x004: tfom_code's of pll.tfom (0x50f4e) */
  uint8_t ffom;            /* +0x006: get_ffom's */
  uint8_t ffom_valid;      /* +0x007: get_ffom's result */
  double interval;         /* +0x008: interval_reading.value, copied with the
                              next three as three longs (0x50f26 to 0x50f34) */
  uint8_t interval_valid;  /* +0x010: interval_reading.valid */
  counter_error interval_errors;   /* +0x011: interval_reading.errors */
  uint16_t interval_tag;           /* +0x012: interval_reading.tag */
  float interval_mean;             /* +0x014: with the next, six bytes from
                                      0x1032b0 (0x50f36) */
  uint16_t interval_mean_flag;     /* +0x018 */
  uint8_t holdover_waiting_reason; /* +0x01a */
  holdover_limit_setting holdover_limit_state; /* +0x01b: nv_settings' */
  float holdover_limit_threshold;              /* +0x01c: nv_settings' */
  uint32_t holdover_duration;          /* +0x020: pll.holdover_duration */
  float kenneth_present;               /* +0x024: kenneth_present_get's */
  uint8_t kenneth_present_valid;       /* +0x028: its result */
  int8_t kenneth_present_resolution;   /* +0x029: a power of ten */
  float kenneth_predicted;             /* +0x02a: kenneth_predicted_get's */
  uint8_t kenneth_predicted_valid;     /* +0x02e: its result */
  int8_t kenneth_predicted_resolution; /* +0x02f: a power of ten */
  uint8_t channel_count;               /* +0x030: gps' (0x510b2) */
  uint8_t field_031;
  satellite_sets satellites;            /* +0x032: satellite_sets_build's */
  satellite_list_entry not_tracked[16]; /* +0x05c: satellite_status_list's
                                           list 0, twice the channels */
  satellite_list_entry tracked[8];      /* +0x0fc: its list 1 */
  uint8_t status_received;              /* +0x14c: gps' */
  uint8_t not_tracked_count;   /* +0x14d: satellite_status_list's count of
                                  not_tracked */
  uint8_t tracked_count;       /* +0x14e: its count of tracked */
  uint8_t pll_field_6f9;       /* +0x14f: pll.field_6f9 (0x50fce) */
  uint8_t reference_valid;     /* +0x150: gps.references[0].valid */
  uint8_t pps_dead;            /* +0x151: gps.references[0].pps_dead */
  gps_sync_status status_code; /* +0x152: gps.references[0].state */
  uint8_t field_153;
  uint32_t reference_elapsed;           /* +0x154: gps.references[0].elapsed */
  uint8_t external_valid;               /* +0x158: gps.references[1].valid */
  uint8_t external_pps_dead;            /* +0x159: gps.references[1].pps_dead */
  gps_sync_status external_status_code; /* +0x15a: gps.references[1].state */
  uint8_t field_15b;
  uint32_t external_elapsed; /* +0x15c: gps.references[1].elapsed */
  uint16_t gps_flags;        /* +0x160: the GPS record's word at +0x608
                                (0x51010); its bit 4 picks the height */
  uint8_t fix_mode;          /* +0x162: gps' */
  oncore_receiver_status receiver_status; /* +0x163: gps.status' */
  time_of_day time;     /* +0x164: gps.status' time made local;
                           get_date_time's while from_clock */
  uint32_t date;        /* +0x168: the same's date */
  int32_t time_zone[2]; /* +0x16c: nv_settings' */
  uint8_t from_clock;   /* +0x174: 1 when the clock is set, the time its */
  uint8_t utc_time;     /* +0x175: gps' */
  gps_time_status time_status; /* +0x176: gps.time_status (0x51080) */
  oncore_leap_pending
      leap_second_pending; /* +0x177: from nv_settings.leap_minute_length,
                              59 a second subtracted, 61 added (0x5104c) */
  int32_t pps_cable_delay; /* +0x178: gps' */
  gps_antenna_sense antenna_sense;   /* +0x17c: gps' (0x5107a); status_screen
                                        heads the tracked satellites "antenna
                                        shorted" for an over current (0x51700) */
  uint8_t mask_angle;                /* +0x17d: gps' */
  uint8_t position_hold;             /* +0x17e: gps' */
  uint8_t douglas_duration_infinite; /* +0x17f: nv_settings' */
  uint8_t gps_field_5bf;             /* +0x180: the GPS record's byte at +0x5bf
                                        (0x510a6); with the next, "EST " for the
                                        position while nothing is averaged */
  uint8_t gps_field_5c0;             /* +0x181: the next (0x510ac) */
  char* position_source;             /* +0x182: "", "EST ", "INIT " or "AVG " */
  int32_t position[3];               /* +0x186: latitude, longitude, height */
  oncore_height_type height_type;    /* +0x192: 1 and the averaged height at
                                        +0x58a where gps_flags' bit 4 is set,
                                        else 0 and the one at +0x582 (0x511b2) */
  uint8_t field_193;
  uint32_t average_count;      /* +0x194: gps' */
  int32_t douglas_duration;    /* +0x198: nv_settings' */
  uint16_t hardware_condition; /* +0x19c: status_hardware_condition's */
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
