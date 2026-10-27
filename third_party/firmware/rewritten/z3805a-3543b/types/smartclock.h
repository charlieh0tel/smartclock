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

#include "../common/types/task_debug_flag.h"

/* preserved_region_fields.restore, a word: whether system_init restores
 * the region (0x12bb4).  pllp_task sets it each pass; nv_clear_and_reset
 * and system_init's defaults clear it. */
typedef enum __attribute__((packed)) preserved_region_restore {
  PRESERVED_REGION_DISCARD = 0,
  PRESERVED_REGION_RESTORE = 1,
} preserved_region_restore;

/* BEGIN PROPOSED ENUMS */
/* END PROPOSED ENUMS */

#include "../common/types/gps_framing_state.h"

#include "../common/types/gps_request_result.h"

/* gps_request_class's sorting of a request code (gps_request_code). */
typedef enum __attribute__((packed)) gps_code_class {
  GPS_CLASS_MESSAGE = 0, /* 0 to 0x30 with argument mode 0 or 1: one message */
  GPS_CLASS_SCRIPT = 1,  /* 0x47 to 0x59: gps_script_lookup's */
  GPS_CLASS_STEP = 2,    /* 0x31 to 0x46: gps_console_request's */
  GPS_CLASS_INVALID = 3,
} gps_code_class;

#include "../common/types/oncore_field_code.h"

#include "../common/types/oncore_receiver_status.h"

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

#include "../common/types/counter_sync_kind.h"

#include "../common/types/satellite_health_class.h"

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
/* The receiver status gps_receiver_status_update keeps (0x4b168),
 * gps.status_code, gps.status_reported and gps.status_code_reset, and
 * status_snapshot_record's copies: status_screen_sync's strings
 * (0x49db0) by its switch on them. */
typedef enum __attribute__((packed)) gps_sync_status {
  GPS_SYNC_RECEIVER_ERROR = 0,      /* "Invalid: GPS rcvr err": the status
                                       monitor failed */
  GPS_SYNC_ABSENT = 1,              /* "Absent or freq error": the GPS 1PPS is
                                       dead */
  GPS_SYNC_NOT_TRACKING = 2,        /* "Invalid: not tracking", and after a
                                       reset */
  GPS_SYNC_INACCURATE_POSITION = 3, /* "Invalid: inacc position": tracking,
                                       neither holding nor surveying */
  GPS_SYNC_RAIM_ERROR = 4,          /* "Invalid: Time RAIM error": holding or
                                       surveying, Time RAIM in alarm */
  GPS_SYNC_ASSESSING = 5,           /* "Assessing stability": the 30 s
                                       validation's start */
  GPS_SYNC_ASSESSING_27 = 6,        /* " .": 27 % of it */
  GPS_SYNC_ASSESSING_53 = 7,        /* " ..": 53 % */
  GPS_SYNC_ASSESSING_77 = 8,        /* " ...": 77 % */
  GPS_SYNC_SYNCHRONIZED = 9,        /* "Synchronized to UTC" or "GPS Time":
                                       reference_valid */
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
 * that no byte equals. */
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
      0x80 /* set in holdover recovery while the GPS command monitor has failed;
              logged as plain "Holdover started" */
} pll_hold_cause;

/* An ADC channel, by the console words that read and average them.  The
 * firmware prints channel 3 as "Oven current" (pr_adc_avg), "Antenna
 * current" (adc_ant_curr) and :DIAG:ROSC:CURR?. */
typedef enum __attribute__((packed)) adc_channel {
  ADC_TEMPERATURE = 0,
  ADC_DOUBLE_OVEN = 1,
  ADC_PLUS_15V = 2,
  ADC_CURRENT = 3,
  ADC_5V = 4,
  ADC_OVEN = 5,
  ADC_MINUS_15V = 6
} adc_channel;

#include "../common/types/satellite_kind.h"

#include "../common/types/satellite_list.h"

#include "../common/types/counter_error.h"

#include "../common/types/scpi_param_type.h"

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

#include "../common/types/oncore_health.h"

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

#include "../common/types/scpi_validate_function.h"

#include "../common/types/stdlib.h"

#include "../common/types/holdover_steering_state.h"

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

#include "../common/types/serial_ring.h"

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

#include "../common/types/io_device.h"

#include "../common/types/data_format.h"

#include "../common/types/timecode_format.h"

#include "../common/types/klok_event_class.h"

#include "../common/types/duartb_settings_record.h"

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

#include "../common/types/duartb_applied_settings.h"

/* A routine that writes a character to a port. */
typedef void putc_function(char c);

#include "../common/types/char_slot.h"

/* A self-test, in the tables at 0x18458 and 0x17fe0: run is 1 to run the
 * test, as selftest_execute's second call and selftest_run_all_counted
 * pass it, and 0 otherwise. */
typedef void selftest_test(char_slot run);
/* An entry of the self-test tables. */
typedef selftest_test* selftest_entry;

#include "../common/types/dac_measure.h"

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

/* DUART channel B's driver, the host port, at 0x1030ec.  Its receive
 * ring is just before it, at 0x102fea. */
typedef struct duartb_driver {
  char* transmit_next;    /* +0x00: drtb_puts's next character */
  char* async_next;       /* +0x04: drtb_send_async's */
  int16_t transmit_count; /* +0x08: drtb_puts's characters left */
  uint8_t async_count;    /* +0x0a */
  uint8_t async_pending;  /* +0x0b: when the asynchronous send ends, the SCPI
                             task is signaled 0x20 */
  uint8_t flow; /* +0x0c: 0x80 an XOFF received, holding transmission; 0x20 one
                   sent */
  uint8_t receive_errors; /* +0x0d: the status register's error bits, 0x10 an
                             overrun */
  uint8_t read_errors;    /* +0x0e: receive_errors as the last read left them */
  uint8_t loopback_test;  /* +0x0f: set while duart_loopback_test_b runs */
  uint8_t xon_xoff;       /* +0x10: flow control, from the serial settings */
  uint8_t least_free;    /* +0x11: the receive buffer's least free space seen */
  uint8_t parity;        /* +0x12: from the serial settings */
  uint8_t reconfiguring; /* +0x13: set while duartb_reconfigure has the port's
                            interrupts off */
  uint8_t in_transmit_interrupt; /* +0x14 */
  uint8_t field_15;
  uint32_t read_exchange;    /* +0x16: drtR */
  uint32_t write_exchange;   /* +0x1a: drtW */
  exchange_token read_token; /* +0x1e: its task is signaled 0x800 on a ctrl-C,
                                0x4000 when input comes */
  uint8_t field_2e[8];
  exchange_token write_token; /* +0x36: its task is signaled 0x1000 when
                                 drtb_puts's string is out */
  uint8_t field_46[8];
  uint8_t async_allowed; /* +0x4e: an asynchronous send may start; an XOFF does
                            not stop it */
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

#include "../common/types/qspi_state.h"

/* A device on the QSPI, by the minor number of I/O device 1, which
 * qspi_queue_setup (0x20578) takes and sets up a queue for, by its jump
 * table at 0x205c4, cases 0 to 0xc; 3 and 8 are unknown to it.  The ADC
 * (one word), the hex DAC (three), the EFC DAC (one), and the FPGA's
 * configuration in blocks of up to 16 words and a tail of two, and its
 * readback of 8 to 16 bits (fpga_program 0x22da4, fpga_verify_chip
 * 0x2384a).  Devices 0 to 7 drive their own number on PCS3-PCS0, the
 * FPGA's all 6 (docs/firmware/structures.md, "The QSPI's devices").
 * No caller passes 0, 4, 5, 10 or 11: 0 is three 12-bit words that only
 * the unreferenced 0x1f278 sends; 4 one 16-bit word set up as the EFC
 * DAC's; 5 two 16-bit words; 10 and 11 one word and one byte to the
 * FPGA, which only the unreferenced 0x1f698 and 0x1f6b0 send. */
typedef enum __attribute__((packed)) qspi_device {
  QSPI_UNUSED_36_BIT = 0,
#include "../common/types/qspi_device.h"
} qspi_device;

#include "../common/types/qspi_buffers.h"

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

#include "../common/types/efc_range_state.h"

#include "../common/types/gps_command_mailbox.h"

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

#include "../common/types/selftest_uart_failure.h"

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
 * garbled.  Bits 14 and 15, spare in the reference, are
 * gps_selftest_run's (0x466ca): the engine did not answer, "SW
 * problem", or timed out. */
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
  SELFTEST_GPS_NO_ANSWER = 0x4000,
  SELFTEST_GPS_TIMED_OUT = 0x8000,
} selftest_gps_failure;

/* selftest_results.power, the supplies out of range, as supplies_test
 * sets them (0x18546). */
typedef enum __attribute__((packed)) selftest_power_failure {
  SELFTEST_POWER_5V = 0x01,
  SELFTEST_POWER_P15V = 0x02,
  SELFTEST_POWER_M15V = 0x04,
} selftest_power_failure;

/* The self-tests' results, at 0x100c24: each test's word nonzero if it
 * failed, and what selftest_run reports for test n, 0 to 13.  The
 * start-up clear begins at 0x100c3c, so the reset code's results
 * before it (CPU, RAM, UART) and the rest up to the interpolator's
 * survive a warm restart. */
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
  uint8_t lamp_mask; /* +0x0c: the DUART output bits the failures reset, which
                        duart_init writes back */
  uint8_t field_0d;
  uint16_t summary; /* +0x0e: selftest_summary_bit's */
  int16_t eeprom;   /* +0x10: selftest_failure */
  int16_t
      fpga; /* +0x12: the FPGA's counters, counter_selftest; selftest_failure */
  int16_t
      gps; /* +0x14: the receiver's @@Ca result, selftest_gps_failure's bits */
  int16_t interpolator;            /* +0x16: selftest_failure */
  uint8_t interpolator_calibrated; /* +0x18: 1 once a calibration succeeded */
  uint8_t interpolator_tries; /* +0x19: the calibrations it took, up to 20 */
  uint16_t interpolator_readings[2]; /* +0x1a: both ends' readings,
                                        interpolator_sample_test's */
  uint8_t abort;   /* +0x1e: set to stop a test running, selftest_abort */
  uint8_t running; /* +0x1f: set while selftest_execute runs a test */
  int16_t power;   /* +0x20: the 5 V, +15 V and -15 V supplies,
                      selftest_power_failure's bits */
  int16_t internal_reference; /* +0x22: the internal 1PPS's, selftest_failure */
  int16_t qspi; /* +0x24: the QSPI's devices, selftest_qspi_failure's bits */
  uint8_t field_26[14];
  uint8_t passed; /* +0x34: 1 unless a test selftest_print_failure ran failed */
  uint8_t field_35;
  uint32_t result;     /* +0x36: selftest_run's test's result, what *TST? and
                          :DIAGnostic:TEST? report */
  uint32_t details[2]; /* +0x3a: the RAM test's bits and lines, or the
                          interpolator's tries and outcome or readings */
  selftest_test_number test; /* +0x42: the test selftest_run was asked for */
  uint8_t in_run; /* +0x43: set during selftest_run; nothing reads it */
} selftest_results;

#include "../common/types/gps_request_sender.h"

/* A GPS request's code (gps_request_entry.code, a descriptor's
 * gps_request), which gps_request_class sorts: 0 to 0x30 an Oncore
 * message, the index of its entry in oncore_messages (0x4fece; the VP
 * Oncore Command Reference names each, third_party/motorola/
 * VPCommands.pdf); 0x31 to 0x46 a step of the GPS task's own,
 * gps_console_request's (0x4c572); 0x47 to 0x59 a script,
 * gps_script_lookup's, each named by the messages it sends
 * (docs/firmware/oncore/z3805a-3543b.txt).  The table's empty slots,
 * 0x14, 0x1a, 0x22, 0x23, 0x25, 0x2a and 0x2b, have no name. */
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
  GPS_REQUEST_TRAIM = 0x28,                    /* @@Bn, Time RAIM */
  GPS_REQUEST_SELFTEST = 0x29,                 /* @@Ca */
  GPS_REQUEST_CORRECTION_INPUT = 0x2c,         /* @@Ce */
  GPS_REQUEST_DEFAULTS = 0x2d,                 /* @@Cf, set to defaults */
  GPS_REQUEST_FIX_MODE = 0x2e,                 /* @@Cg, fix or idle */
  GPS_REQUEST_IO_FORMAT = 0x2f,                /* @@Ci */
  GPS_REQUEST_RECEIVER_ID = 0x30,              /* @@Cj */
  GPS_STEP_STATUS_SNAPSHOT = 0x31,             /* status_snapshot */
  GPS_STEP_PRINT_STATUS = 0x32,
  GPS_STEP_PRINT_VISIBILITY = 0x33,
  GPS_STEP_PRINT_EXTENSION = 0x34,
  GPS_STEP_PRINT_BEST_COMBINATIONS = 0x35,
  GPS_STEP_PRINT_ID = 0x36,
  GPS_STEP_PRINT_PPS = 0x37,
  GPS_STEP_PRINT_INSTRUMENT_STATE = 0x38,
  GPS_STEP_PRINT_TIME_RAIM = 0x39,
  GPS_STEP_PRINT_AVERAGE = 0x3a,
  GPS_STEP_AVERAGE_RESET = 0x3b,  /* gps.average_count = 0 */
  GPS_STEP_AVERAGE = 0x3c,        /* the console's avg; gps_console_request
                                     does nothing with it */
  GPS_STEP_SURVEY_START = 0x3d,   /* gps.surveying = 1 */
  GPS_STEP_SURVEY_STOP = 0x3e,    /* gps.surveying = 0 */
  GPS_STEP_PPS_CHECK = 0x3f,      /* gps.pps_check_disabled, its argument */
  GPS_STEP_RAIM_ARGUMENTS = 0x40, /* raim_request_arguments */
  GPS_STEP_PARSE_OPTIONS = 0x43,  /* gps_parse_options */
  GPS_STEP_STATUS_MONITOR = 0x44, /* gps.status_monitor_enabled, its argument */
  GPS_STEP_LINK_RESET = 0x45,     /* gps_link_reset; gps_request_queue empties
                                     the ring for it */
  GPS_SCRIPT_INIT_DEFAULTS = 0x47, /* @@Cf, then every setting; gps_init too */
  GPS_SCRIPT_INIT = 0x48,          /* every setting, without @@Cf */
  GPS_SCRIPT_FIX_RESTART = 0x49,   /* idle and fix again, the PPS check off */
  GPS_SCRIPT_ENGINE_SELFTEST = 0x4a, /* @@Ca, idle */
  GPS_SCRIPT_OUTPUTS_ON = 0x4b,      /* @@Ba and @@Bb each second */
  GPS_SCRIPT_OUTPUTS_OFF = 0x4c,     /* @@Ba, @@Bb, @@Bc and @@Bk off */
  GPS_SCRIPT_QUERY_SETTINGS = 0x4d,  /* @@Cj, then each setting's query */
  GPS_SCRIPT_UTC_SET = 0x4e,         /* @@Aw from its arguments, @@Ba */
  GPS_SCRIPT_HOLD_AT =
      0x4f, /* @@As from its arguments, hold on, TRAIM, restart */
  GPS_SCRIPT_POSITION_SET = 0x50,  /* @@Ad, @@Ae, @@Af, idle around them */
  GPS_SCRIPT_HOLD_RELEASE = 0x51,  /* survey stop, average reset, hold off,
                                      TRAIM, restart */
  GPS_SCRIPT_SURVEY_FROM = 0x52,   /* hold off, @@Ad to @@Af from its
                                      arguments, TRAIM, restart, survey start,
                                      average reset */
  GPS_SCRIPT_HOLD_OFF = 0x53,      /* hold off, TRAIM, average reset */
  GPS_SCRIPT_HOLD_AT_QUIET = 0x54, /* @@As, hold on, TRAIM, no restart */
  GPS_SCRIPT_HOLD_SELECT = 0x55,   /* @@At from its arguments, TRAIM, restart */
  GPS_SCRIPT_HOLD_POSITION = 0x56, /* @@As from its arguments, restart */
  GPS_SCRIPT_TIME_SET = 0x57,      /* @@Aa from its arguments, idle around it */
  GPS_SCRIPT_DATE_SET = 0x58,      /* @@Ac from its arguments, idle around it */
  GPS_SCRIPT_SATELLITE_SELECT = 0x59, /* @@Ah 1, each channel's @@Ai cleared,
                                         then from its arguments */
  GPS_REQUEST_NONE = 0xff,            /* a descriptor's, for none */
} gps_request_code;

#include "../common/types/gps_request_entry.h"

/* gpsm_task's ring of waiting GPS requests: five entries, the oldest at
 * read, the next free at write; empty when the two are equal, so it
 * holds four. */
typedef struct gps_request_ring {
  gps_request_entry entries[5];
  gps_request_entry* read;  /* +0x82 */
  gps_request_entry* write; /* +0x86 */
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

#include "../common/types/time_of_day.h"

#include "../common/types/gps_command_log_entry.h"

#include "../common/types/packed_datetime.h"

#include "../common/types/lifetime_state.h"

/* The EEPROM's records ("EEPROM records" in structures.md), each
 * checksummed by record_checksum in its first two bytes, as their
 * writers build them on their stacks: erased to 0xff, then filled. */

#include "../common/types/parameter_record.h"

#include "../common/types/identification_record.h"

/* The calibration block at 0x400080: save_calibration_block's TC and
 * time offset. */
typedef struct calibration_block {
  uint16_t checksum;    /* +0x00 */
  float tc_coefficient; /* +0x02: scpi_value_DIAGnostic_ROSCillator_TCOefficient
                         */
  float time_offset;    /* +0x06: scpi_value_DIAGnostic_TOFFset */
  uint8_t unused[54];   /* +0x0a: left erased */
} calibration_block;

/* An NV register record, one of nv_records at 0x4000c0, as nv_save
 * builds it (structures.md, "The NV register"); its checksum is
 * nv_checksum's, over bytes 8 to 0x7f. */
typedef struct nv_record {
  char tag[2];         /* +0x00: "S" */
  uint16_t checksum;   /* +0x02 */
  int32_t sequence;    /* +0x04: one past the stored record's */
  char revision[8];    /* +0x08: "3543B" */
  uint32_t packed[28]; /* +0x10: the settings packed into longs, then 0xff */
} nv_record;

#include "../common/types/reset_record.h"

#include "../common/types/sprintf.h"

#include "../common/types/scpi_decimal.h"

#include "../common/types/scpi_error_message.h"

#include "../common/types/scpi.h"

/* The two command trees scpi_task gives scpi_interpret, a pair in ROM
 * at 0x5d014, and the installer's at installer::0x108930. */
typedef struct scpi_tree_pair {
  scpi_child_list* common; /* +0x00: the common commands' list, *IDN? on */
  scpi_child_list* main;   /* +0x04: the main tree's root list */
} scpi_tree_pair;

/* :SYSTem:LANGuage's choice of what follows the SCPI session
 * (scpi_language), which scpi_task acts on (0x2a0fe to 0x2a136). */
typedef enum __attribute__((packed)) scpi_language_choice {
  SCPI_LANGUAGE_PFORTH = 0,  /* the pForth console */
  SCPI_LANGUAGE_INSTALL = 1, /* the installer */
} scpi_language_choice;

#include "../common/types/scpi_call.h"

#include "../common/types/scpi_staged_values.h"

#include "../common/types/scpi_context.h"

#include "../common/types/scpi_array.h"

#include "../common/types/status_register.h"

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

#include "../common/types/status_condition_kind.h"

#include "../common/types/status_condition.h"

/* A condition of the status system, the code status_happening,
 * status_report and the SCPI engine raise.  From 0, an index of
 * status_conditions (0x2f838): 1 to 0x16 queue the SCPI error beside
 * each (kind 3); from 0x19 each sets (_SET) or clears (_CLEARED) a
 * register's condition bit, or is an event only, as the entry's kind
 * says (1, 2 and 0).  Those names are the bits' in the Z3801A guide's
 * status register tables (097-z3801-01, pages 4-15 to 4-17); bit 7 of
 * the Standard Event register is IEEE 488.2's power-on.  :DIAG:STAT:HAPP
 * n raises n + 0x16.  Below 0, minus an index of
 * status_negative_conditions (0x28720), what the SCPI engine's routines
 * return: -1 to -4 set Standard Event bits 5 to 2, the error classes
 * error_queue_push reports, and the rest queue their SCPI error.
 * 0x17, 0x18 and 0x30 to 0x33, -29 and -58 to -62 do nothing, as 0 does;
 * -25 and -26, -171, and -57, -224, which no code shows the cause of,
 * have no name here.  The SCPI standard's, to 0x16, the same in every
 * image, are common/types/status_scpi.h's, included as the head of the
 * members. */
typedef enum __attribute__((packed)) status_condition_code {
#include "../common/types/status_scpi.h"
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
  STATUS_SELFTEST_FAILURE_SET = 0x34, /* :HARDware bit 0 */
  STATUS_SELFTEST_FAILURE_CLEARED = 0x35,
  STATUS_PLUS_15V_SET = 0x36, /* :HARDware bit 1, +15 V supply out of range */
  STATUS_PLUS_15V_CLEARED = 0x37,
  STATUS_MINUS_15V_SET = 0x38, /* :HARDware bit 2, -15 V supply out of range */
  STATUS_MINUS_15V_CLEARED = 0x39,
  STATUS_PLUS_5V_SET = 0x3a, /* :HARDware bit 3, +5 V supply out of range */
  STATUS_PLUS_5V_CLEARED = 0x3b,
  STATUS_PRIMARY_OVEN_SET =
      0x3c, /* :HARDware bit 4, primary oven voltage out of range */
  STATUS_PRIMARY_OVEN_CLEARED = 0x3d,
  STATUS_SECONDARY_OVEN_SET =
      0x3e, /* :HARDware bit 5, secondary oven voltage out of range */
  STATUS_SECONDARY_OVEN_CLEARED = 0x3f,
  STATUS_EFC_NEAR_LIMIT_SET =
      0x40, /* :HARDware bit 6, EFC voltage within 5% of limit */
  STATUS_EFC_NEAR_LIMIT_CLEARED = 0x41,
  STATUS_EFC_AT_LIMIT_SET = 0x42, /* :HARDware bit 7 */
  STATUS_EFC_AT_LIMIT_CLEARED = 0x43,
  STATUS_GPS_PPS_FAILURE_SET = 0x44, /* :HARDware bit 8, GPS 1 PPS failure */
  STATUS_GPS_PPS_FAILURE_CLEARED = 0x45,
  STATUS_GPS_FAILURE_SET = 0x46, /* :HARDware bit 9 */
  STATUS_GPS_FAILURE_CLEARED = 0x47,
  STATUS_TIME_INTERVAL_ERROR = 0x48,  /* :HARDware bit 10, an event only */
  STATUS_EEPROM_WRITE_FAILURE = 0x49, /* :HARDware bit 11, an event only */
  STATUS_INTERNAL_REFERENCE_SET =
      0x4a, /* :HARDware bit 12, internal reference failure */
  STATUS_INTERNAL_REFERENCE_CLEARED = 0x4b,
  STATUS_TIME_SET = 0x4c,          /* :QUEStionable bit 0, an event only */
  STATUS_USER_REPORTED_SET = 0x4d, /* :QUEStionable bit 1 */
  STATUS_USER_REPORTED_CLEARED = 0x4e,
} status_condition_code;

#include "../common/types/status_byte_bit.h"

#include "../common/types/status_error_entry.h"

#include "../common/types/status_register_layout.h"

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

#include "../common/types/status_register_mask.h"

#include "../common/types/status_preset_form.h"

#include "../common/types/status_model.h"

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

#include "../common/types/oncore_date_fields.h"

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
  uint8_t ura[6];                    /* per channel */
  int16_t least_squares_inverse[10]; /* A-1, upper triangle by rows */
  int16_t clock_bias;                /* m */
  uint16_t oscillator_offset;        /* m/s */
} oncore_extension_fields;

/* The EFC history: a ring of 64 averages of the EFC, one each 2700
 * samples, which the holdover prediction draws on.  Its routine's debug
 * message names its arrays: "e_avg= time= weight= log= tfom=".
 * Cleared as a whole, 0x366 bytes. */
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
  float efc_sum; /* +0x34a: the EFC, less its temperature correction, summed */
  int32_t field_34e; /* nothing refers to it */
  float drift;       /* +0x352: pll_rep's current drift, times 5.4e-8 per day */
  int32_t field_356; /* nothing refers to it */
  float fit_a;       /* +0x35a: the curv task's aging fit a + b y + c ln y, y =
                        samples / 2700 */
  float fit_b;       /* +0x35e */
  float fit_c;       /* +0x362 */
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

#include "../common/types/aging_curve_state.h"

/* @@Ba, the position/status/data message: one channel's status. */
typedef struct oncore_channel_status {
  uint8_t satellite;
  oncore_channel_mode mode;
  uint8_t signal_strength;
  uint8_t status; /* bit 7 used for the fix */
} oncore_channel_status;

/* @@Ba, as its decoder's table (0x50fd2) lays it out: a pad byte
 * before the hour (time_of_day's own), and another before the DOP type, put the
 * longs and words on even addresses. */
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
  oncore_channel_status channels[6];
  oncore_receiver_status receiver_status;
} oncore_status_fields;

#include "../common/types/oncore_visible_satellite.h"

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
 * (0x50fe6) lays it out. */
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
  oncore_raim_channel channels[6];
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

/* The GPS task's state: what its decoders store of each message, in
 * the order of the message table, and its own state after.  Cleared as
 * one, memset(&gps, 0, 0x55a), and addressed through a base register
 * by 27 functions.  Members are named by their message, field_NNN by
 * offset where none fills them. */
typedef struct gps_state {
  uint8_t field_000;
  oncore_time_fields time;             /* @@Aa */
  oncore_gmt_offset_fields gmt_offset; /* @@Ab */
  uint8_t field_007;
  oncore_date_fields date;                  /* @@Ac */
  oncore_position_fields position;          /* @@Ad, @@Ae, @@Af */
  uint8_t mask_angle;                       /* @@Ag */
  oncore_satellite_select satellite_select; /* @@Ah */
  uint8_t manual_satellites[6];             /* @@Ai, by channel */
  oncore_xdop_type xdop_type;               /* @@Aj */
  uint8_t field_025;
  uint16_t xdop_hysteresis;    /* @@Ak */
  uint16_t xdop_threshold;     /* @@Al, 3D to 2D */
  uint16_t hdop_threshold;     /* @@AC, 2D to 0D */
  uint32_t ignored_satellites; /* @@Am */
  uint8_t field_030[0x8e - 0x30];
  uint8_t almanac_update; /* @@An */
  uint8_t field_08f[0xa4 - 0x8f];
  uint8_t ionospheric_correction;            /* @@Aq */
  oncore_fix_algorithm fix_algorithm;        /* @@Ar */
  oncore_hold_position_fields hold_position; /* @@As */
  oncore_height_type hold_height_type;       /* @@As */
  uint8_t field_0b3;
  uint8_t position_hold; /* @@At */
  uint8_t field_0b5[0xbb - 0xb5];
  uint8_t altitude_hold;     /* @@Av */
  oncore_time_mode utc_time; /* @@Aw */
  uint8_t field_0bd;
  int16_t epoch_offset;    /* @@Ax, ms */
  int32_t pps_offset;      /* @@Ay */
  int32_t pps_cable_delay; /* @@Az */
  uint8_t field_0c8;
  oncore_application_type application_type; /* @@AB */
  uint8_t position_fix_threshold;           /* @@AD, s */
  uint8_t differential_output_threshold;    /* @@AD, s */
  uint8_t status_received;     /* @@Ba has come; the status monitor sets it */
  uint8_t status_rate;         /* s between @@Ba, as last requested */
  oncore_status_fields status; /* @@Ba */
  uint8_t field_10d;
  uint8_t reference_qualified;     /* reference_valid before the PPS check */
  gps_sync_status status_reported; /* status_code, held while it is 5 to 8 */
  uint8_t visibility_received;     /* set by @@Bb's decoder */
  uint8_t field_111;
  oncore_visibility_fields visibility; /* @@Bb */
  uint8_t field_18c;
  oncore_xdop_table_fields xdop_table; /* @@Bc */
  uint8_t field_1d8[2];
  oncore_almanac_status_fields almanac; /* @@Bd */
  uint8_t field_1ea[0x2a8 - 0x1ea];
  oncore_corrections_fields corrections; /* @@Ce */
  double correction_average;             /* m, printed AVG */
  double correction_deviation;           /* m, printed STD DEV */
  uint8_t field_2f8[2];
  uint8_t raim_received;   /* @@Bn has come; the status monitor sets it */
  oncore_raim_fields raim; /* @@Bn */
  oncore_leap_pending leap_second_pending; /* @@Bj */
  uint8_t field_33b;
  uint16_t self_test; /* @@Ca, failed tests */
  uint8_t field_33e[0x344 - 0x33e];
  oncore_extension_fields extension; /* @@Bk */
  uint8_t field_382[0x3ec - 0x382];
  oncore_fix_mode fix_mode; /* @@Cg */
  uint8_t field_3ed;
  uint8_t receiver_id_received;   /* set by @@Cj's decoder */
  oncore_receiver_id receiver_id; /* @@Cj */
  uint8_t field_504[2];
  uint32_t average_count;  /* positions averaged */
  double average_latitude; /* of @@Ba's, while surveying */
  double average_longitude;
  double average_height_gps;
  double average_height_msl;
  uint8_t reference_valid;     /* :GPS:REF:VALid; 0 while the PPS is dead */
  uint8_t pps_dead;            /* gps_pps_dead, while it counts */
  gps_sync_status status_code; /* 1 PPS dead, 2 after a reset, 3 tracking, 4
                          holding or surveying, 5 to 8 validating, 9 valid */
  gps_reference_override reference_valid_override; /* the byte at 0x100c68: 1
                                       forces valid, 0xff invalid */
  uint32_t validation_start; /* uptime as status_code reached 5 */
  uint8_t field_532[2]; /* status_snapshot reads them; nothing writes them */
  gps_sync_status status_code_reset; /* 2, set by gps_receiver_status_reset */
  uint8_t field_535[5];
  uint8_t surveying; /* averaging @@Ba's position */
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
  gps_status_monitor_state
      status_monitor_state;     /* 0 disabled, 1 standby, 2 recovery */
  uint32_t status_monitor_time; /* uptime of the last fresh @@Ba */
  uint32_t status_last_time;   /* the last @@Ba's time bytes, compared as one */
  uint8_t status_repeat_count; /* @@Ba with the same time in a row */
  uint8_t status_monitor_timed_out;
  uint8_t status_monitor_failed;
  uint8_t field_557;
  uint8_t raim_option; /* its options list has an I */
  uint8_t field_559;
} gps_state;

/* An entry of fpga_bit_table (0x41310), which fpga_set_bit and
 * fpga_set_field take: a register of fconf_regs and its mask.  The
 * names are from how ti.c's routines use them; only the address and
 * sync are in structures.md. */
typedef enum __attribute__((packed)) fpga_bit_index {
#include "../common/types/fpga_bit_index.h"
  FPGA_BIT_SYNC = 5, /* register 3, 0x03: counter_set_sync */
} fpga_bit_index;

#include "../common/types/fpga_mode.h"

#include "../common/types/interval_arm_mode.h"

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

#include "../common/types/fpga_chip.h"

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
 * acknowledgment awaited. */
typedef enum __attribute__((packed)) pll_gps_init_state {
  PLL_GPS_INIT_START = 0x00,
  PLL_GPS_INIT_ACK = 0x01,
  PLL_GPS_INIT_UTC_SEND = 0x02,
  PLL_GPS_INIT_UTC_ACK = 0x03,
  PLL_GPS_INIT_MASK_ANGLE_SEND = 0x04,
  PLL_GPS_INIT_MASK_ANGLE_ACK = 0x05,
  PLL_GPS_INIT_ANTENNA_DELAY_SEND = 0x06,
  PLL_GPS_INIT_ANTENNA_DELAY_ACK = 0x07,
  PLL_GPS_INIT_IGNORE_SEND = 0x08,
  PLL_GPS_INIT_IGNORE_ACK = 0x09,
  PLL_GPS_INIT_POSITION_SEND = 0x0a,
  PLL_GPS_INIT_POSITION_ACK = 0x0b,
  PLL_GPS_INIT_HOLD_OFF_SEND = 0x0c,
  PLL_GPS_INIT_HOLD_OFF_ACK = 0x0d,
  PLL_GPS_INIT_SURVEY = 0x0e,
  PLL_GPS_INIT_SURVEY_POSITION_SEND = 0x0f,
  PLL_GPS_INIT_SURVEY_POSITION_ACK = 0x10,
  PLL_GPS_INIT_DONE = 0x11,
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

#include "../common/types/pll_screen_mode.h"

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

#include "../common/types/gps_ack_awaited_kind.h"

/* The disciplining loop's state block: the stage the status screen
 * shows (docs/screen-format-strings.md, "Mode suffixes") and the PLL's
 * values.  Cleared as one, memset(&pll, 0, 0x3d6). */
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
  pll_holdover_state holdover_state; /* +0x10: holdover's */
  pll_hold_cause hold_causes;        /* +0x11: a set of pll_hold_cause bits */
  uint8_t manual;                    /* +0x12 */
  uint8_t ti_limit_override; /* +0x13: holdover checks the TI limit while 0;
                                holdover_recovery_limit_ignore sets it */
  uint8_t
      recovery_countdown; /* +0x14: holdover_recovery's, as powerup_countdown */
  pll_recovery_state recovery_state; /* +0x15 */
  pll_coarse_state coarse_state;     /* +0x16: coarse's */
  pll_fine_state fine_state;         /* +0x17: fine's; PLL_FINE_SLEW to
                                 PLL_FINE_FINE_SLEW slew the phase */
  uint8_t frequency_adjusted; /* +0x18: fine applied a frequency correction */
  uint8_t fine_index;         /* fine acquisition's "i= %d" */
  uint16_t fine_retries; /* +0x1a: fine's failed attempts, given up past 9 */
  float fine_d;          /* fine acquisition's "d = %f" */
  pll_aging_state
      aging_state; /* +0x20: efc_history_update's; 3 once the aging fit holds */
  uint8_t field_021[3];
  pll_gps_diag_state gps_diag_state; /* +0x24: gps_diag_sequence's */
  uint8_t gps_diag_substate;         /* +0x25 */
  pll_gps_init_state
      gps_init_state; /* +0x26: gps_init_sequence's, print_gps_init_state's */
  uint8_t field_027;
  uint16_t leap_check_timer;    /* +0x28: gps_leap_sequence checks again when it
                                   wraps */
  uint8_t leap_check_requested; /* +0x2a: pll_stage_status sets it */
  pll_leap_check_state leap_check_state; /* +0x2b: gps_leap_sequence's,
                               print_leap_check_state's */
  pll_leap_ack_state leap_ack_state;     /* +0x2c: gps_leap_ack's, measuring the
                                 GPS-UTC offset     by switching @@Aw */
  uint8_t leap_ack_requested;            /* +0x2d */
  uint8_t leap_ack_done;                 /* +0x2e: the offset is set */
  uint8_t field_02f;
  uint8_t
      leap_ack_again; /* +0x30: holdover_recovery measures again after fine */
  uint8_t field_031;
  float efc;                   /* pr_efc's efc */
  float tfom;                  /* pll_rep's tfom, s */
  float predicted_uncertainty; /* +0x3a: the curv fit's, :KENneth:PREDicted? */
  uint8_t field_03e[8];
  float frequency_tolerance; /* +0x46: 1e-8 at first; fine's bound, holdover's
                                limit per second */
  efc_ring efc_history;
  float ti_filtered; /* +0x3b0: the TI, filtered with weight 29.75 / tau */
  float efc_base;    /* +0x3b4: the EFC less the temperature term, TC times the
                        current */
  float loop_time_constant; /* +0x3b8: 150 s at first, at most max_loop_time */
  uint32_t holdover_start_time; /* +0x3bc: get_date_time's as holdover starts */
  uint32_t holdover_start_date; /* +0x3c0 */
  uint8_t led_gps_lock;         /* +0x3c4: :LED:GPSLock? */
  uint8_t led_holdover;         /* +0x3c5: :LED:HOLDover? */
  uint8_t led_tmh_valid;        /* +0x3c6: :LED:TMHValid? */
  uint8_t field_3c7;
  uint32_t holdover_duration; /* +0x3c8: :ROSCillator:HOLDover:DURation? */
  uint32_t reference_uptime;  /* +0x3cc: the uptime clock_set_from_reference
                                 counts from */
  uint8_t reference_seconds;  /* +0x3d0: the FPGA's seconds count with it */
  fpga_configuration_index
      fpga_configuration; /* +0x3d1: the configuration fpga_setup loaded and
                             fpga_load_test loads again; pll_init starts it at
                             fpga_configurations.last */
  uint8_t fpga_loaded;    /* +0x3d2: the PPS checks wait for it */
  uint8_t loop_running;   /* +0x3d3: 0 in diagnostic mode; nothing reads it */
  uint8_t sync_requested; /* +0x3d4: sync_perform syncs whatever the TI, then
                             clears it */
  uint8_t field_3d5; /* read by the GPS status code, never written but by the
                        block copy */
} pll_state;

#include "../common/types/gps_statistics.h"

#include "../common/types/exception_record.h"

#include "../common/types/os.h"

/* A fixed event log message, by the code log_event (0x319a2) takes
 * and its table's order; the names are the messages'.  enter_holdover
 * (0x4397a) picks a holdover's by its cause. */
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
  LOG_SPECIFICATIONS_EXCEEDED = 0x15,  /* "Output specifications exceeded" */
  LOG_HARDWARE_FAILURE = 0x16,         /* "Hardware failure" */
  LOG_SELFTEST_FAILED = 0x17,          /* "Self-test failed" */
  LOG_EEPROM_SAVE_FAILED = 0x18,       /* "EEPROM save failed" */
  LOG_OUT_OF_EVENT_BUFFERS = 0x19,     /* "Clk_do: out of event buffers" */
  LOG_SYSTEM_PRESET = 0x1a,            /* "System preset" */
} log_event_code;

#include "../common/types/log_entry.h"

#include "../common/types/fpga_bit.h"

#include "../common/types/interpolator_calibration.h"

#include "../common/types/scpi_setting_param.h"

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

#include "../common/types/scpi_action_param.h"

#include "../common/types/request_source.h"

#include "../common/types/pllc_request.h"

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
  ONCORE_MATCH_HOLD_POSITION = 4, /* @@As: latitude and longitude alone when
                                     the type is the same and the height in
                                     range and within 2 cm */
  ONCORE_MATCH_FIX_MODE = 5,      /* @@Cg: an echo, but mode 2, a query of the
                                     mode, matched at once */
  ONCORE_MATCH_SAME = 6,          /* the same message, its bytes not compared */
  ONCORE_MATCH_USER_DATUM = 7,    /* @@Ao: answered by @@Ap */
  ONCORE_MATCH_NEVER = 8,         /* never answered; no message has it */
  ONCORE_MATCH_CORRECTIONS = 9,   /* @@Bh: answered by @@Ce */
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

/* An entry of the GPS task's Oncore message table, oncore_messages: 56
 * bytes (docs/firmware/oncore/README.md, "The message table"). */
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
} oncore_message_entry;

#include "../common/types/holdover_limit_setting.h"

/* The settings nv_save keeps in the NV register and nv_restore loads:
 * each field the value of the descriptor in nv_save's tables that points
 * at it, named by the SCPI command whose setter is given that descriptor.
 * 0xc5 bytes. */
typedef struct nv_settings_fields {
  uint8_t field_00[2];               /* no code refers to them */
  int32_t elevation_mask;            /* :GPS:EMANgle */
  int32_t antenna_delay;             /* :GPS:ADELay */
  int32_t douglas_duration;          /* :DOUGlas:DURation */
  uint8_t douglas_duration_infinite; /* :DOUGlas:DURation INFinity */
  uint8_t pad_0f;
  int32_t holdover_status_threshold;           /* :ROSC:HOLD:DUR:STAT:THR */
  float limit_threshold;                       /* :ROSC:LIMit:THReshold */
  float holdover_limit_threshold;              /* :ROSC:HOLD:LIMit:THReshold */
  holdover_limit_setting holdover_limit_state; /* :ROSC:HOLD:LIMit:STATe */
  uint8_t douglas_state;                       /* :DOUGlas:STATe */
  uint8_t field_1e; /* only its descriptor refers to it */
  uint8_t pad_1f;
  int32_t position[3];         /* :GPS:POSition, mas, mas, cm */
  uint32_t ignored_satellites; /* :GPS:SAT:TRAC:IGNore */
  int32_t time_zone[2];        /* :PTIMe:TZONe, hours, minutes */
  int32_t prediction_duration; /* :KENneth:PREDicted:DURation */
  uint32_t leap_date;          /* a packed date */
  int32_t leap_minute_length;  /* 59 for a second removed */
  uint8_t leap_pending;
  uint8_t raim_state[2]; /* :DIAG:GPS:POS:HOLD:RAIM:STATe */
  uint8_t pad_47;
  int32_t raim_threshold[2];              /* :DIAG:GPS:POS:HOLD:RAIM:THR */
  int32_t holdover_measurement_threshold; /* :ROSC:HOLD:DUR:MEAS:THR */
  uint32_t status_enable[7];              /* *ESE, *SRE, :STATus ENABle */
  uint32_t status_positive_transition[5]; /* :STATus PTRansition */
  uint32_t status_negative_transition[5]; /* :STATus NTRansition */
  uint8_t questionable_user;              /* :STAT:QUES:...:USER */
  uint8_t serial_restricted;              /* :DIAG:SER1:RESTricted */
  uint8_t pulse_state;                    /* :PULSe:CONTinuous:STATe */
  uint8_t pad_9b;
  int32_t pulse_period;          /* :PULSe:CONTinuous:PERiod */
  int32_t pulse_start_time[3];   /* :PULSe:STARt:TIME */
  int32_t pulse_start_date[3];   /* :PULSe:STARt:DATE */
  int32_t field_b8;              /* 0 to 50000; only its descriptor */
  uint8_t field_bc;              /* only its descriptor refers to it */
  uint8_t output_state;          /* :OUTPut:STATe */
  data_format format_data;       /* :FORMat:DATA */
  uint8_t memory_save;           /* :ROBin:MEMory:SAVE */
  uint8_t douglas_state_powerup; /* :DOUGlas:STATe:POWerup */
  timecode_format tcode_format;  /* :PTIMe:TCODe:FORMat */
  uint8_t gps_utc;               /* :DIAG:GPS:UTC */
  uint8_t pforth_at_startup;     /* :DIAG:SYSTem:PSTartup */
  uint8_t debug_output;          /* :DIAG:SYSTem:DOUTput */
} nv_settings_fields;

#include "../common/types/interval_reading_fields.h"

#include "../common/types/counter_work_state.h"

/* The battery-backed RAM a warm restart restores, preserved_region at
 * 0x100000: pllp_task copies gps, pll and the 0x19 bytes from 0x101e70
 * into it each pass and checksums it, and system_init copies them back
 * when the checksum is good, RSR shows neither an external nor a
 * power-up reset, and restore is 1.  0x94e bytes, record_checksum's
 * length. */
typedef struct preserved_region_fields {
  uint16_t
      checksum; /* +0x000: record_checksum's sum and complement of the rest */
  uint16_t restore;     /* +0x002: a preserved_region_restore */
  gps_state gps;        /* +0x004 */
  pll_state pll;        /* +0x55e */
  uint8_t values[0x19]; /* +0x934: 0x101e70 to 0x101e88 */
  uint8_t field_94d;    /* in the checksum; nothing else refers to it */
} preserved_region_fields;

#include "../common/types/clock_record.h"

#include "../common/types/pps_exchange_message.h"

#include "../common/types/gps_process_state.h"

#include "../common/types/satellite_sets.h"

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
 * prints: 0x12e bytes, three of them from 0x10142c. */
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
                                  format_si's resolution for the TI (0x4a5de) */
  float interval_mean;         /* +0x016 */
  uint16_t interval_mean_flag; /* +0x01a: the word at interval_mean_flag */
  uint8_t holdover_waiting_reason;             /* +0x01c */
  holdover_limit_setting holdover_limit_state; /* +0x01d: nv_settings' */
  float holdover_limit_threshold;              /* +0x01e: nv_settings' */
  uint32_t holdover_duration;                  /* +0x022: pll's */
  float kenneth_present;                /* +0x026: kenneth_present_get's */
  uint8_t kenneth_present_valid;        /* +0x02a: its result */
  int8_t kenneth_present_resolution;    /* its resolution, a power of ten */
  float kenneth_predicted;              /* +0x02c: kenneth_predicted_get's */
  uint8_t kenneth_predicted_valid;      /* +0x030: its result */
  int8_t kenneth_predicted_resolution;  /* its resolution, a power of ten */
  satellite_sets satellites;            /* +0x032: satellite_sets_build's */
  satellite_list_entry not_tracked[12]; /* +0x05c: satellite_status_list's
                                           list 0, the visible satellites no
                                           channel uses for the fix */
  satellite_list_entry tracked[6];      /* +0x0bc: its list 1, those the
                                           channels use, with their signal */
  uint8_t status_received;              /* +0x0ec: gps' */
  uint8_t not_tracked_count;         /* +0x0ed: satellite_status_list's count of
                                        not_tracked */
  uint8_t tracked_count;             /* +0x0ee: its count of tracked */
  uint8_t pll_field_3d5;             /* +0x0ef: pll.field_3d5 */
  uint8_t reference_valid;           /* +0x0f0: gps' */
  uint8_t pps_dead;                  /* +0x0f1: gps' */
  gps_sync_status status_code;       /* +0x0f2: gps' */
  uint8_t gps_field_532[2];          /* +0x0f3: gps.field_532 */
  gps_sync_status status_code_reset; /* +0x0f5: gps' */
  uint8_t fix_mode;                  /* +0x0f6: gps' */
  oncore_receiver_status receiver_status; /* +0x0f7: gps.status' */
  time_of_day time;     /* +0x0f8: gps.status' time made local (0x4a720);
                           get_date_time's while from_clock (0x4a74e) */
  uint32_t date;        /* +0x0fc: the same's date */
  int32_t time_zone[2]; /* +0x100: nv_settings' */
  uint8_t from_clock;   /* +0x108: 1 when the clock is set, the time its; 0 for
                          a clock not set since power-up (get_date_time's 1),
                          the time @@Ba's, which the screen marks "[?]" */
  uint8_t utc_time;     /* +0x109: gps' */
  uint8_t leap_second_pending; /* +0x10a: gps' */
  uint8_t field_10b;
  int32_t pps_cable_delay;           /* +0x10c: gps' */
  uint8_t mask_angle;                /* +0x110: gps' */
  uint8_t position_hold;             /* +0x111: gps' */
  uint8_t douglas_duration_infinite; /* +0x112: nv_settings' */
  uint8_t field_113;
  char* position_source;       /* +0x114: "INIT ", "AVG " or "" */
  int32_t position[3];         /* +0x118: latitude, longitude, height; as
                                  position_source says */
  uint32_t average_count;      /* +0x124: gps' */
  int32_t douglas_duration;    /* +0x128: nv_settings' */
  uint16_t hardware_condition; /* +0x12c: status_hardware_condition's */
} status_snapshot_record;

#include "../common/types/psos.h"

#include "../common/types/spoo_request.h"

#include "../common/types/early_status_events.h"

#include "../common/types/installer_test_choice.h"
