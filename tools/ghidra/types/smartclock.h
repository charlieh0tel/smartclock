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

/* Proposed enums: a rewrite that needs a record field's or a global's
 * coded values adds their enum here, packed and with its evidence, and
 * reports the fields and prototypes it should type.  The lead moves each
 * to its subject below, types them, and empties this section on
 * merging. */
/* BEGIN PROPOSED ENUMS */
/* END PROPOSED ENUMS */

/* gps_receive_state: where gps_receive is in framing an Oncore message
 * into gps_response_buffer, its jump table at 0x4b892 (docs/firmware/
 * gps.md, "Framing").  A state above GPS_FRAMING_COMPLETE drops each
 * byte (0x4b884). */
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

/* What became of a GPS request, gps_count_response's kind: what it
 * counts in gps_counts (its jump table at 0x4ba0c), stores in
 * command_monitor_result or status_monitor_result for the monitors'
 * requests, and sends back to plla_exchange in gps_request_entry.result
 * for the others.  Every sender sends a request with 1;
 * gps_request_post sets its monitor's result to pending. */
typedef enum __attribute__((packed)) gps_request_result {
  GPS_RESULT_SUCCESS = 0,         /* successes */
  GPS_RESULT_REJECT = 1,          /* rejects: the same wrong response twice */
  GPS_RESULT_ABORT = 2,           /* aborts: emptied by a link reset */
  GPS_RESULT_UNKNOWN_SENDER = 3,  /* unknown_sender: request_kind_valid's */
  GPS_RESULT_UNKNOWN_COMMAND = 4, /* unknown_command: gps_request_class's 3 */
  GPS_RESULT_BUSY = 5,            /* busy: not queued, or replaced */
  GPS_RESULT_NO_COMMAND = 6, /* no_command: " // command not supported //" */
  GPS_RESULT_NO_QUERY = 7,   /* no_query: " // query form not supported //" */
  GPS_RESULT_TIMEOUT = 8,    /* timeouts: no response after three sends */
  GPS_RESULT_PENDING = 9,    /* not yet answered; counted as none */
} gps_request_result;

/* gps_request_class's sorting of a request code (gps_request_code). */
typedef enum __attribute__((packed)) gps_code_class {
  GPS_CLASS_MESSAGE = 0, /* 0 to 0x30 with argument mode 0 or 1: one message */
  GPS_CLASS_SCRIPT = 1,  /* 0x47 to 0x59: gps_script_lookup's */
  GPS_CLASS_STEP = 2,    /* 0x31 to 0x46: gps_console_request's */
  GPS_CLASS_INVALID = 3,
} gps_code_class;

/* A byte of the field programs oncore_decode_fields runs to unpack a
 * message into its record, oncore_status_field_codes (0x50fd2) to
 * oncore_receiver_id_field_codes (0x51004): docs/firmware/gps.md,
 * "Decoding".  1 to 0x7f copies that many bytes, and 0xc1 to 0xff
 * starts a group repeated 0x100 less that many times, 63 to 1; the
 * others, 0x86 to 0xc0, do nothing. */
typedef enum __attribute__((packed)) oncore_field_code {
  ONCORE_FIELD_SKIP_DESTINATION = 0x00, /* leaves a record byte */
  ONCORE_FIELD_END = 0x80,
  ONCORE_FIELD_GROUP_END = 0x81,   /* back to the group's start, or on */
  ONCORE_FIELD_SKIP_SOURCE = 0x82, /* drops a message byte */
  ONCORE_FIELD_SKIP_ZERO = 0x83,   /* drops a message byte, writes zero */
  ONCORE_FIELD_ZERO = 0x84,        /* writes a zero byte */
  ONCORE_FIELD_SIGN = 0x85,        /* the next message byte's sign extension */
  ONCORE_FIELD_GROUP_FIRST = 0xc1, /* the longest group, 63 times */
} oncore_field_code;

/* The receiver status byte of @@Ba, oncore_status_fields.receiver_status,
 * a set of these bits (VPCommands.pdf, Position/Status/Data Message,
 * "s - Receiver Status Message"). */
typedef enum __attribute__((packed)) oncore_receiver_status {
  ONCORE_STATUS_BAD_ALMANAC = 0x01,
  ONCORE_STATUS_INSUFFICIENT_SATELLITES = 0x02, /* fewer than 3 visible */
  ONCORE_STATUS_DIFFERENTIAL = 0x04,
  ONCORE_STATUS_ACQUIRING = 0x08, /* acquiring satellites or position hold */
  ONCORE_STATUS_2D_FIX = 0x10,    /* altitude hold */
  ONCORE_STATUS_3D_FIX = 0x20,
  ONCORE_STATUS_POOR_GEOMETRY = 0x40, /* DOP above 20 */
  ONCORE_STATUS_PROPAGATE = 0x80,     /* position propagate mode */
} oncore_receiver_status;
/* fine's states, pll.fine_state, as print_fine_state (0x1ba32) names
 * them: ": fine start" to ": fine fine slew" (0x1c5fe to 0x1c683). */
typedef enum __attribute__((packed)) pll_fine_state {
  PLL_FINE_START = 0,
  PLL_FINE_SYNC_MEASURE = 1,
  PLL_FINE_SYNC_1 = 2,
  PLL_FINE_SYNC_2 = 3,
  PLL_FINE_MEASURE_1 = 4,
  PLL_FINE_MEASURE_DELAY = 5,
  PLL_FINE_MEASURE_2 = 6,
  PLL_FINE_SLEW = 7,
  PLL_FINE_SLEW_MEASURE = 8,
  PLL_FINE_FINE_SLEW = 9,
} pll_fine_state;

/* The console's GPS command, gps_command.code, named for the word that
 * posts it (diag_print_stat 0x1b504 to diag_gps_query 0x1b706);
 * gps_command_send (0x470aa) maps each to its GPS request.  Nothing in
 * the console posts GPS_CONSOLE_SELFTEST. */
typedef enum __attribute__((packed)) gps_console_command {
  GPS_CONSOLE_NONE = 0x00,
  GPS_CONSOLE_PRINT_STATUS = 0x01,            /* print_stat */
  GPS_CONSOLE_STATUS_RATE = 0x02,             /* stat_rate */
  GPS_CONSOLE_TIME = 0x03,                    /* gps_time */
  GPS_CONSOLE_DATE = 0x04,                    /* gps_date */
  GPS_CONSOLE_HOLD_POSITION = 0x05,           /* gps_php */
  GPS_CONSOLE_POSITION_HOLD = 0x06,           /* gps_ph */
  GPS_CONSOLE_STATUS_INTERVAL = 0x07,         /* abr_stat: no request */
  GPS_CONSOLE_MASK_ANGLE = 0x08,              /* eman */
  GPS_CONSOLE_MASK_ANGLE_QUERY = 0x09,        /* q_eman */
  GPS_CONSOLE_HOLD_POSITION_QUERY = 0x0a,     /* q_gps_php */
  GPS_CONSOLE_CABLE_DELAY = 0x0b,             /* gps_cable_dly */
  GPS_CONSOLE_CABLE_DELAY_QUERY = 0x0c,       /* q_gps_cable_dly */
  GPS_CONSOLE_VISIBILITY_RATE = 0x0d,         /* vis_stat_rate */
  GPS_CONSOLE_DOP_RATE = 0x0e,                /* xdop_stat_rate */
  GPS_CONSOLE_PRINT_VISIBILITY = 0x0f,        /* print_vis */
  GPS_CONSOLE_PRINT_BEST_COMBINATIONS = 0x10, /* print_bc */
  GPS_CONSOLE_EXTENSION_RATE = 0x11,          /* ext_msg_rate */
  GPS_CONSOLE_PRINT_EXTENSION = 0x12,         /* print_ext_msg */
  GPS_CONSOLE_PRINT_ID = 0x13,                /* pr_gps_id */
  GPS_CONSOLE_PRINT_PPS = 0x14,               /* pr_1pps */
  GPS_CONSOLE_PRINT_TIME_RAIM = 0x15,         /* pr_time_raim */
  GPS_CONSOLE_AVERAGE = 0x16,                 /* avg */
  GPS_CONSOLE_CLEAR_AVERAGE = 0x17,           /* clr_avg */
  GPS_CONSOLE_INIT = 0x18,                    /* init_gps */
  GPS_CONSOLE_MASTER_RESET = 0x19,            /* master_reset */
  GPS_CONSOLE_SELFTEST = 0x1a,
  GPS_CONSOLE_QUERY_ALL = 0x1b,              /* gps_query_all */
  GPS_CONSOLE_QUIET_STATUS = 0x1c,           /* quiet_status */
  GPS_CONSOLE_PRINT_AVERAGE = 0x1d,          /* pr_avg */
  GPS_CONSOLE_PRINT_INSTRUMENT_STATE = 0x1e, /* pr_gps_istate */
  GPS_CONSOLE_CHANGE = 0x1f,                 /* gps_change: request, as set */
  GPS_CONSOLE_QUERY = 0x20, /* gps_query: request, as a query */
} gps_console_command;

/* The console's override of the GPS reference's validity,
 * reference_valid_override_request (0x100c68), which force_1pps
 * (diag_force_1pps, 0x1b68c) sets from -1, 0 or 1. */
typedef enum __attribute__((packed)) gps_reference_override {
  GPS_REFERENCE_OVERRIDE_NONE = 0x00,
  GPS_REFERENCE_OVERRIDE_VALID = 0x01,
  GPS_REFERENCE_OVERRIDE_INVALID = 0xff,
} gps_reference_override;

/* sync_request's kind, counter_configuration_fields.sync_kind: a step
 * against a positive TI, against a negative one, and a sync
 * (pll_stages.c's steps; sync_imm, diag_sync_imm 0x1b2ec, asks for a
 * sync). */
typedef enum __attribute__((packed)) counter_sync_kind {
  COUNTER_SYNC_POSITIVE = 1,
  COUNTER_SYNC_NEGATIVE = 2,
  COUNTER_SYNC_FULL = 3,
} counter_sync_kind;

/* The fault the console's crash word raises, its argument, by its jump
 * table at 0x1c09a (diag_crash, 0x1c07c); any other does nothing. */
typedef enum __attribute__((packed)) console_crash_kind {
  CONSOLE_CRASH_BUS_ERROR = 0,     /* a long written to 0x700000, unmapped */
  CONSOLE_CRASH_ADDRESS_ERROR = 1, /* a long written to an odd address */
  CONSOLE_CRASH_ZERO_DIVIDE = 2,   /* divul by zero */
  CONSOLE_CRASH_FLOAT_DIVIDE = 3,  /* a double divided by zero */
  CONSOLE_CRASH_FATAL_ERROR = 4,   /* fatal_error */
} console_crash_kind;
/* A satellite's cell on the status screen, the code at +4 of a
 * satellite_status_list entry: satellite_elevation_azimuth sets 1 or 2
 * (0x49654), satellite_status the rest (0x49688), and
 * format_satellite_cell prints each (its table at 0x49ab4). */
typedef enum __attribute__((packed)) satellite_cell_status {
  SATELLITE_CELL_NONE = 0,         /* blank */
  SATELLITE_CELL_POSITION = 1,     /* "%2d %3d", elevation and azimuth */
  SATELLITE_CELL_NO_POSITION = 2,  /* "-- ---" */
  SATELLITE_CELL_IGNORED = 3,      /* "Ignore" */
  SATELLITE_CELL_UNHEALTHY = 4,    /* "Not OK" */
  SATELLITE_CELL_ACQUIRING = 5,    /* "Acq   ": channel modes 1 to 4 */
  SATELLITE_CELL_SYNCHRONIZED = 6, /* "Acq . ": modes 5 and 6 */
  SATELLITE_CELL_EPHEMERIS = 7     /* "Acq ..": modes 7 and 8 */
} satellite_cell_status;

/* satellite_track_class's class of a @@Bb record, by its health
 * (oncore_health; 0x49612, its table at 0x49638): satellite_status
 * shows a removed one as "Ignore" and an unhealthy one as "Not OK". */
typedef enum __attribute__((packed)) satellite_health_class {
  SATELLITE_CLASS_UNKNOWN = 0, /* no record, or a health past 3 */
  SATELLITE_CLASS_HEALTHY = 1,
  SATELLITE_CLASS_UNHEALTHY = 2,
  SATELLITE_CLASS_REMOVED = 3 /* healthy or not */
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

/* Which satellite IDs: satellite_id_valid's ranges. */
typedef enum __attribute__((packed)) satellite_kind {
  SATELLITE_GPS = 0,        /* 1 to 32 */
  SATELLITE_PSEUDOLITE = 1, /* 33 to 37 */
  SATELLITE_ANY = 2         /* 1 to 37 */
} satellite_kind;

/* Which list satellite_list_limits gives the limits of. */
typedef enum __attribute__((packed)) satellite_list {
  SATELLITE_LIST_TRACKING = 0, /* :SAT:TRAC?, six channels */
  SATELLITE_LIST_TRACKING_PSEUDOLITES = 1,
  SATELLITE_LIST_VISIBLE = 2, /* :SAT:VIS:PRED?, twelve */
  SATELLITE_LIST_ALL = 3      /* the ignore and include lists */
} satellite_list;

/* A time-interval reading's error bits, which counter_read reports:
 * "Gate failure", "Result uncomputable", "Measurement aborted",
 * "Interpolator calibration failure". */
typedef enum __attribute__((packed)) counter_error {
  COUNTER_GATE_FAILURE = 0x01,
  COUNTER_RESULT_UNCOMPUTABLE = 0x04,
  COUNTER_MEASUREMENT_ABORTED = 0x08,
  COUNTER_CALIBRATION_FAILURE = 0x10
} counter_error;

/* A SCPI parameter descriptor's structure, its first byte, which the
 * pllc task's validate_or_doaction and finish_scpi_cmd switch on; both
 * accept 3 and do nothing with it. */
typedef enum __attribute__((packed)) scpi_param_type {
  SCPI_PARAM_NUMERIC = 0, /* scpi_numeric_param */
  SCPI_PARAM_SETTING = 1, /* scpi_setting_param */
  SCPI_PARAM_ACTION = 2   /* scpi_action_param */
} scpi_param_type;

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

/* @@Bb: a satellite's health. */
typedef enum __attribute__((packed)) oncore_health {
  ONCORE_HEALTHY = 0,
  ONCORE_HEALTHY_REMOVED = 1,
  ONCORE_UNHEALTHY = 2,
  ONCORE_UNHEALTHY_REMOVED = 3
} oncore_health;

/* @@Aw: the time the engine reports. */
typedef enum __attribute__((packed)) oncore_time_mode {
  ONCORE_TIME_GPS = 0,
  ONCORE_TIME_UTC = 1
} oncore_time_mode;

/* @@Cg: fix or idle. */
typedef enum __attribute__((packed)) oncore_fix_mode {
  ONCORE_IDLE = 0,
  ONCORE_POSITION_FIX = 1
} oncore_fix_mode;

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

/* A numeric SCPI parameter's descriptor, in ROM; its small tables sit
 * just before it.  0x2c bytes. */
/* A numeric descriptor's validate: the new value, its index, the
 * bounds and step for it, and the request's source; its result is the
 * value as validated. */
typedef long scpi_validate_function(void* value, int index, void* minimum,
                                    void* maximum, void* step, int source);

/* printf's shape, for the routines that take one to print with. */
typedef int printf_function(char* format, ...);

/* console_write's shape, for the routines that take one to write a
 * string with: log_print and status_screen. */
typedef unsigned long write_function(char* s);

/* qsort's comparator. */
typedef int qsort_compare_function(void* a, void* b);

/* A function atexit registers and call_atexit_functions calls, the
 * last first (atexit_functions, 0x1038b4, 32 of them). */
typedef void atexit_function(void);

/* The holdover stage's steering, at 0x102042: each 10-second block's
 * mean time interval, and the accumulators holdover_ti_add keeps of the
 * readings for a least-squares slope, which holdover_correction_valid
 * turns into an EFC correction. */
typedef struct holdover_steering_state {
  float ti_mean; /* +0x00: the block's valid readings' sum over 10, scaled up at
                    its end if some failed */
  uint8_t readings; /* +0x04: the block's valid readings */
  uint8_t seconds;  /* +0x05: the block's seconds, to 10 */
  uint8_t field_06; /* +0x06: zeroed with the block; nothing reads it */
  uint8_t field_07;
  float aging_limit; /* +0x08: 2e-9, or 2e-9 a day as a rate a second once the
                        EFC history is full; nothing reads it */
  float ti_sum;      /* +0x0c */
  float ti_weighted_sum; /* +0x10: each reading times its index */
  float ti_largest; /* +0x14: the largest reading's magnitude, holdover_ti_max's
                     */
  int32_t ti_count; /* +0x18: at 10 or more the correction may be used */
} holdover_steering_state;

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

/* A DUART channel's receive ring: its interrupt stores at write, its
 * reader takes at read, each index wrapping in a byte; empty when the
 * two are equal. */
typedef struct serial_ring {
  uint8_t write;
  uint8_t read;
  uint8_t data[256];
} serial_ring;

/* DUART channel A's driver, the GPS receiver's link, at 0x102fe0; only
 * the GPS task sends on it.  Its receive ring is at 0x102edd. */
typedef struct duarta_driver {
  char* transmit_next;    /* +0x00: drta_send_message's next character */
  int16_t transmit_count; /* +0x04: its characters left; at 0 the GPS task is
                             signaled 0x800 */
  uint8_t overrun;        /* +0x06: the receiver overran ("DRTA receiver overrun
                             ocurred") */
  uint8_t loopback_test;  /* +0x07: set while duart_loopback_test_a runs */
  uint8_t held; /* +0x08: set while that test has the port, so a send leaves the
                   transmit interrupt off */
  uint8_t least_free; /* +0x09: the receive ring's least free space seen */
} duarta_driver;

/* A serial port's baud code, in its settings record (SERial1's and
 * SERial2's :BAUD; sci_baud_set and duartb_reconfigure). */
typedef enum __attribute__((packed)) serial_baud {
  SERIAL_BAUD_1200 = 0,
  SERIAL_BAUD_2400 = 1,
  SERIAL_BAUD_9600 = 2,
  SERIAL_BAUD_19200 = 3,
} serial_baud;

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

/* A kernel I/O device, by kernel_dispatch_io's (0x14c36) major number in
 * the high byte; the QSPI's minor, a qspi_device, goes in the low. */
typedef enum io_device {
  IO_DEVICE_DUART_B = 0x000,
  IO_DEVICE_QSPI = 0x100,
  IO_DEVICE_CONSOLE_STRING = 0x200,
  IO_DEVICE_SCI = 0x300,
} io_device;

/* The data format :FORMat:DATA sets, ASCii or INTeger (097-59551-02,
 * "Reading Time Stamps"), which only the :ROBin queries' responses use:
 * robin_store (0x29a68) gives ASCii as an array of the points' shorts
 * and INTeger as a block of their bytes, by its jump table at 0x29a96. */
typedef enum __attribute__((packed)) data_format {
  FORMAT_ASCII = 0,
  FORMAT_INTEGER = 1,
} data_format;

/* The time code's format, :PTIMe:TCODe:FORMat, which tcode_request
 * takes: T1, the GPS seconds; T2 with a test prefix; T2. */
typedef enum __attribute__((packed)) timecode_format {
  TIMECODE_T1 = 0,
  TIMECODE_T2_TEST = 1,
  TIMECODE_T2 = 2,
} timecode_format;

/* A klok_entry's event class, which klok_find looks entries up by:
 * the leap second's (leap_event_delete, 0x46394) and the lifetime
 * record's hourly save. */
typedef enum __attribute__((packed)) klok_event_class {
  KLOK_EVENT_LEAP = 1,
  KLOK_EVENT_LIFETIME = 2,
} klok_event_class;

/* DUART channel B's serial settings, SERial1's, at 0x101f58: the
 * :SYSTem:COMMunicate:SERial1 setters store them, through setting
 * descriptors that save the EEPROM parameter record, and
 * duartb_reconfigure applies them.  parameter_record_read loads +2 to
 * +8 from the record's bytes 2 to 8, or copies the defaults at 0x30a34
 * over all 9 bytes when its checksum fails; save_parameter_record
 * writes them back. */
typedef struct duartb_settings_record {
  uint8_t field_00[2]; /* +0x00: only the defaults' copy writes them, 0; nothing
                          reads them */
  serial_baud baud;    /* +0x02 */
  serial_parity parity; /* +0x03: not ONE */
  uint8_t xon_xoff;     /* +0x04: :PACE XON when nonzero, else NONE */
  uint8_t field_05;     /* +0x05: saved and restored; its setting descriptor,
                           0x33364, no setter uses */
  uint8_t eight_bits;   /* +0x06: else seven; with nv_settings.serial_restricted
                           set, parity set with 8 bits clears it, and 8 bits set
                           with parity clears parity */
  uint8_t two_stop_bits; /* +0x07: else one */
  uint8_t echo; /* +0x08: :FDUPlex ON, each character received echoed */
} duartb_settings_record;

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

/* Channel B's serial settings as last applied to the DUART, at
 * 0x102ecc: copies of duartb_settings', which
 * duartb_baud_from_settings and duartb_mode_from_settings program. */
typedef struct duartb_applied_settings {
  uint8_t baud;
  uint8_t parity;
  uint8_t eight_bits; /* +0x02: else seven, and duartb_isr strips the top bit */
  uint8_t two_stop_bits; /* +0x03: else one */
} duartb_applied_settings;

/* What the code last wrote to the 68681's write-only registers, at
 * 0x102ed0, so that it can change some bits of one and write it whole;
 * their bits are mc68681.h's. */
typedef struct duart_shadow_registers {
  uint8_t mr1a;
  uint8_t mr2a;
  uint8_t csra;
  uint8_t acr;
  uint8_t imr; /* +0x04: 0x10 channel B's transmitter, 0x20 its receiver */
  uint8_t ctur;
  uint8_t ctlr;
  uint8_t mr1b;
  uint8_t mr2b;
  uint8_t csrb;
  uint8_t opcr;
  uint8_t output_reset; /* +0x0b: the output bits reset, the failed self-tests'
                           lamps */
  uint8_t field_0c;     /* +0x0c: duart_init zeroes it; nothing reads it */
} duart_shadow_registers;

/* A routine that writes a character to a port. */
typedef void putc_function(char c);

/* A char argument through a function pointer.  This compiler pushes a
 * char in a 2-byte slot, its value at the slot's start, 4 bytes above
 * the stack pointer at entry (docs/firmware/README.md, "The calling
 * convention"), where Ghidra's convention for a pointer's prototype, and
 * gcc, put a char at the end of a 4-byte slot; custom storage, which
 * places a function's own char there, a pointer's prototype cannot
 * carry.  A 4-byte structure's first byte is at the slot's start in
 * both; gcc pads a 2-byte one to the slot's end. */
typedef struct char_slot {
  uint8_t value;    /* +0x00: the char */
  uint8_t field_01; /* unread: the rest of the original's 2-byte slot */
  uint8_t next; /* +0x02: unread: the next 2-byte slot's char, a second argument
                   the callee ignores; the self-test callers push it with move.b
                   (0x177b6, 0x177dc, 0x18310, 0x18316) */
  uint8_t field_03; /* unread: the rest of that slot */
} char_slot;

/* A self-test, in the tables at 0x18458 and 0x17fe0: run is 1 to run the
 * test, as selftest_execute's second call and selftest_run_all_counted
 * pass it, and 0 otherwise. */
typedef void selftest_test(char_slot run);
/* An entry of the self-test tables. */
typedef selftest_test* selftest_entry;

/* The measurement dac_search_bisect and dac_search_step move a DAC by:
 * it sets the DAC to its argument and returns a reading. */
typedef uint8_t dac_measure(char_slot dac);

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

/* The QSPI's lock and its transfer, at 0x1032a6: qspi_acquire takes the
 * lock if it is free, else queues its task and waits for 0x400, which
 * qspi_release or the interrupt signals the next waiting task.  The
 * interrupt copies the QSPI's receive RAM, qsm.rr at 0xfffd00
 * (MC68331UM Table D-13), to the caller's buffer. */
typedef struct qspi_state {
  uint8_t field_00; /* +0x00: qspi_send and qspi_transfer set it; nothing reads
                       it */
  uint8_t write;    /* +0x01: the next waiting slot, wrapping at 16 */
  uint8_t read;     /* +0x02: the next task to hand the lock to */
  uint8_t field_03;
  uint32_t waiting[16]; /* +0x04: the tasks queued for the lock */
  uint8_t busy;         /* +0x44: the lock is held */
  uint8_t field_45;
  uint16_t*
      receive_buffer; /* +0x46: where the interrupt copies the receive RAM */
  uint16_t receive_count; /* +0x4a: how many words */
  uint8_t copy_pending;   /* +0x4c: the interrupt is to copy them */
  uint8_t running; /* +0x4d: qspi_start waits while it is set; the interrupt
                      clears it */
} qspi_state;

/* A device on the QSPI, by the minor number of I/O device 1, which
 * qspi_queue_setup (0x205a0) takes and sets up a queue for, cases 0 to
 * 0xc.  Only those a caller passes are named: the ADC (one word), the
 * hex DAC (three), the EFC DAC (one), and the FPGA's configuration in
 * blocks of up to 16 words and a tail of two, and its readback of 8 to
 * 16 bits (fpga_program 0x22da4, fpga_verify_chip 0x2384a). */
typedef enum __attribute__((packed)) qspi_device {
  QSPI_ADC = 1,
  QSPI_HDAC = 2,
  QSPI_FPGA_BLOCK = 6,
  QSPI_EFC = 7,
  QSPI_FPGA_TAIL = 9,
  QSPI_FPGA_READBACK = 0xc,
} qspi_device;

/* A QSPI transfer's two buffers, on its caller's stack, which
 * qspi_transfer takes, and qspi_read hands its device's read: the words
 * to send and where the words received go.  adc_read and the FPGA's
 * routines pass one. */
typedef struct qspi_buffers {
  uint16_t* transmit; /* +0x00 */
  uint16_t* receive;  /* +0x04 */
} qspi_buffers;

/* The EEPROM lock's token, at 0x101d1c: the 16-byte message eeprom_init
 * sends the eepx exchange, which eeprom_lock receives into the same
 * cells and eeprom_unlock sends back.  The lock is recursive, as
 * mutex_acquire's is: the holder at +0x08, its nested holds at +0x0c. */
typedef struct eeprom_lock_token {
  uint32_t status;   /* +0x00: eeprom_init zeroes it; nothing reads it */
  uint32_t exchange; /* +0x04: eepx's id; eeprom_unlock checks it against
                        eepx_exchange ("eepx semaphore has been corrupted") */
  uint32_t owner;    /* +0x08: the task holding the lock, 0 for none */
  int32_t depth;     /* +0x0c: the holder's nested eeprom_lock calls, which
                        eeprom_unlock counts down before it sends */
} eeprom_lock_token;

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

/* Whether the EFC is at or near the end of its range, at 0x101cf4:
 * efc_clamp and hmon_check_efc report each change as a status
 * happening, and efc_clamp_reset clears all three. */
typedef struct efc_range_state {
  uint8_t clamped;  /* +0x00: efc_clamp clamped it to 0 or 1048560, happening
                       'B'; cleared with 'C' */
  uint8_t near_end; /* +0x01: "EFC near end of range", happening '@'; cleared
                       with 'A' */
  uint8_t near_end_checks; /* +0x02: hmon_check_efc's consecutive checks outside
                              52428 to 996132, held at 5 */
} efc_range_state;

/* The GPS command the console's GPS words leave for pllp_task, at
 * 0x10256c: gps_command_send maps the code to an Oncore request, code
 * 0x20 sending request as it is, and sends it with the arguments. */
typedef struct gps_command_mailbox {
  gps_console_command code; /* +0x00: 0 when none is pending */
  uint8_t field_01;
  int32_t arguments[4]; /* +0x02 */
  int32_t request;      /* +0x12: code 0x20's request */
} gps_command_mailbox;

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

/* A GPS script's step, as gpsm_task runs it.  The one at 0x101b56,
 * gps_request_step, is in RAM: the one-step script at 0x50cc2 points at
 * it, and gpsm_task fills it from a request gps_request_class puts in
 * class 0, a message. */
typedef struct gps_script_step {
  int16_t code; /* +0x00: the request code gps_request_build is given */
  uint8_t argument_mode; /* +0x02: 2 takes the arguments from
                            gps_request_step's, from the index in arguments[0];
                            otherwise given to gps_request_build */
  uint8_t field_03;
  int32_t arguments[4]; /* +0x04: gps_request_step's are also a class 1
                           request's, a script's, and raim_request_arguments' */
} gps_script_step;

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

/* selftest_results.uart, the DUART test's failures, by selftest_uart's
 * texts (0x17cbe): duart_test's IVR and counter checks (duart.c, 0x1e650)
 * and each channel's loopback (duarta.c, duartb.c). */
typedef enum __attribute__((packed)) selftest_uart_failure {
  SELFTEST_UART_REGISTER = 0x01,
  SELFTEST_UART_COUNTER = 0x02,
  SELFTEST_UART_CHANNEL_A_DATA = 0x04,
  SELFTEST_UART_CHANNEL_A_TIMEOUT = 0x08,
  SELFTEST_UART_CHANNEL_B_DATA = 0x10,
  SELFTEST_UART_CHANNEL_B_TIMEOUT = 0x20,
} selftest_uart_failure;

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

/* Who sends a GPS request (gps_request_entry.sender), so that its result
 * comes back to it: gpsx_send for the loop's GPS sequences (pllp_task
 * takes the result as gps_ack_code), pll_scpi_send_to_loop for a
 * setting (pll_scpi_complete), and gps_request_post for the command
 * and status monitors, whose results gps_count_response keeps in
 * command_monitor_result and status_monitor_result.  request_kind_valid
 * takes 1 to 4. */
typedef enum __attribute__((packed)) gps_request_sender {
  GPS_SENDER_SEQUENCE = 1,
  GPS_SENDER_SETTING = 2,
  GPS_SENDER_COMMAND_MONITOR = 3,
  GPS_SENDER_STATUS_MONITOR = 4,
} gps_request_sender;

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

/* A GPS request: the message pll_scpi_send_to_loop, gpsx_send and
 * gps_request_post build in 26 bytes of their frames and send
 * gpsx_exchange, which gpsm_task receives into 26 bytes, keeps in its
 * ring, and gps_count_response sends back to plla_exchange with the
 * result, for pllp_task and gps_selftest_run.  The kernel copies +0x04
 * to +0x17 (kernel_copy_message). */
typedef struct gps_request_entry {
  uint32_t
      field_00; /* +0x00: the senders clear it; the kernel does not copy it */
  uint32_t
      field_04; /* +0x04: the senders clear it; kernel_send_for_task refuses a
                   nonzero one other than the exchange's id, error 4 */
  gps_request_sender sender; /* +0x08: one with the same sender waiting is
                                not queued again; the monitors' may be
                                replaced */
  gps_request_result result; /* +0x09: 1 as sent; gps_count_response's kind as
                     it comes back, which pllp_task takes as gps_ack_code or
                     pll_scpi_complete's success */
  int16_t code; /* +0x0a: the request, gps_request_class's index; 0x45 empties
                   the ring before it is queued */
  uint8_t argument_mode; /* +0x0c: gps_request_class's second argument, copied
                            to gps_request_step's */
  uint8_t field_0d;
  int32_t* arguments; /* +0x0e: four longs, which gpsm_task copies into
                         gps_request_step's */
  uint8_t sent; /* +0x12: gps_request_post's last argument, 1 for the status
                   monitor's 0x4b; a newest waiting request with it set is not
                   replaced */
  uint8_t field_13[7];
} gps_request_entry;

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
  gps_request_entry request; /* +0x00: copied from the ring's oldest */
  gps_script_step** script;  /* +0x1a: the step it runs; the next after each
                                matched response, a null one ending the script */
  int16_t message; /* +0x1e: the step's code, an index of oncore_messages, which
                      gps_request_build built */
  uint8_t query;   /* +0x20: the step's argument mode, gps_request_build's query
                      form; the log prints q, else c */
  uint8_t length;  /* +0x21: the built message's bytes */
  uint8_t sent[80]; /* +0x22: the message as sent, copied from
                       gps_command_message */
} gps_running_command;

/* A time of day as a long holds it, hour << 16 | minute << 8 | second,
 * by its bytes, which the clock routines add and carry one at a time
 * (datetime_add, time_next_second). */
typedef struct time_of_day {
  uint8_t reserved; /* 0 */
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
} time_of_day;

/* A date as a long holds it, year << 16 | month << 8 | day, where a
 * routine keeps it in memory and reads or writes it by its parts:
 * leap_opportunity_date sets the month and day bytes (0x11648 to
 * 0x11672), diag_get_time and timecode_update print the year word and
 * the month and day bytes (0x1b474 to 0x1b484, 0x13e04 to 0x13e14),
 * and pulse_settings_done builds one from three longs (0x42b48 to
 * 0x42b58). */
typedef struct calendar_date {
  uint16_t year; /* +0x00: the full year, 1994 on */
  uint8_t month; /* +0x02: 1 to 12 */
  uint8_t day;   /* +0x03: 1 to 31 */
} calendar_date;

/* A GPS Receiver Command Log entry: what command_log_add takes of a
 * request gpsm_task has sent, and gps_print_command prints as
 * "%-5s %02d  %-2s %-2s [%s]   %11s  %8s %s". */
typedef struct gps_command_log_entry {
  int16_t code;     /* +0x00: the request's +0xa, printed under "ID" */
  uint8_t sender;   /* +0x02: the request's +8; 1 SCPI1, 2 PLL, 3 GPSc, 4 GPSs,
                       else "?" */
  uint8_t field_03; /* never written */
  int16_t message; /* +0x04: the request's +0x1e, its index in the message table
                      at 0x4fece, whose ID prints */
  char argument_text[3]; /* +0x06: characters 4 and 5 of gps_message_to_text's
                            text, the first byte after the ID in hex; NUL */
  uint8_t query; /* +0x09: the request's +0x20, gps_request_build's query form;
                    printed q, else c */
  uint8_t clock_invalid; /* +0x0a: get_date_time returned 0; printed blank, else
                            e */
  uint8_t field_0b;      /* never written */
  uint32_t date; /* +0x0c: @@Ba's, local; the clock's while clock_invalid */
  time_of_day
      time; /* +0x10: likewise; gps_print_command passes it to format_time */
} gps_command_log_entry;

/* The GPS Receiver Command Log, a ring at 0x1017f6, and its copy at
 * 0x1019a2, which status_snapshot_update takes with the snapshot at
 * 0x10168e: 21 entries, the oldest at read, the next free at write;
 * empty when the two are equal, and full at 20, when command_log_add
 * drops the oldest.  gps_print_command takes the oldest. */
typedef struct gps_command_log {
  gps_command_log_entry entries[21];
  gps_command_log_entry* read;  /* +0x1a4 */
  gps_command_log_entry* write; /* +0x1a8 */
} gps_command_log;

/* A date and time as datetime_add and datetime_subtract take them: the
 * time's hours, minutes and seconds in its low three bytes, and the date
 * packed as date_to_days reads it, or, in their first argument, a count
 * of days to add or take away. */
typedef struct packed_datetime {
  time_of_day time;
  uint32_t date;
} packed_datetime;

/* The lifetime record in RAM, at 0x101d00: what lifetime_record_read
 * loads from EEPROM 0x400060 and lifetime_record_write saves there,
 * the EEPROM's order being the date, the count, then the EFC. */
typedef struct lifetime_state {
  float efc; /* +0x00: the EFC lifetime_update saves in normal PLL, mid-scale
                524288 by default; pll_init starts from it */
  uint32_t date; /* +0x04: year << 16 | month << 8 | day, 1994-01-01 by default;
                    clock_init takes the date from it */
  int32_t count; /* +0x08: one more for each new date lifetime_update is given;
                    :DIAGnostic:LIFetime:COUNt? returns it */
} lifetime_state;

/* The EEPROM's records ("EEPROM records" in structures.md), each
 * checksummed by record_checksum in its first two bytes, as their
 * writers build them on their stacks: erased to 0xff, then filled. */

/* The parameter record at 0x400000: save_parameter_record's copy of
 * the two ports' settings. */
typedef struct parameter_record {
  uint16_t checksum;            /* +0x00 */
  uint8_t duartb_baud;          /* +0x02: duartb_settings' +2 to +8, in order */
  uint8_t duartb_parity;        /* +0x03 */
  uint8_t duartb_xon_xoff;      /* +0x04 */
  uint8_t duartb_field_05;      /* +0x05 */
  uint8_t duartb_eight_bits;    /* +0x06 */
  uint8_t duartb_two_stop_bits; /* +0x07 */
  uint8_t duartb_echo;          /* +0x08 */
  uint8_t sci_baud;             /* +0x09: sci_settings' +2 to +5, in order */
  uint8_t sci_parity;           /* +0x0a */
  uint8_t sci_xon_xoff;         /* +0x0b */
  uint8_t sci_echo;             /* +0x0c */
  uint8_t unused[3];            /* +0x0d: left erased */
} parameter_record;

/* The identification record at 0x400010: identification_record_write's
 * model, serial and suffix, and "S". */
typedef struct identification_record {
  uint16_t checksum; /* +0x00 */
  char model[18];    /* +0x02: idn_model */
  char serial[18];   /* +0x14: idn_serial */
  char suffix;       /* +0x26: idn_suffix */
  char tag[3];       /* +0x27: "S" */
  uint8_t unused[6]; /* +0x2a: left erased */
} identification_record;

/* The lifetime record at 0x400060, lifetime's date, count and EFC in
 * that order. */
typedef struct lifetime_record {
  uint16_t checksum;  /* +0x00 */
  uint32_t date;      /* +0x02 */
  int32_t count;      /* +0x06 */
  float efc;          /* +0x0a */
  uint8_t unused[18]; /* +0x0e: left erased */
} lifetime_record;

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

/* A reset-cause record, one of 32 from 0x4001c0, as reset_record_write
 * builds it; its checksum is reset_record_checksum's, over bytes 4 to
 * 0x5b. */
typedef struct reset_record {
  int16_t sequence;  /* +0x00: one past the slot's stored record's */
  uint16_t checksum; /* +0x02 */
  time_of_day time;  /* +0x04: get_date_time's time */
  uint32_t date;     /* +0x08: its date */
  char text[80];     /* +0x0c */
} reset_record;

/* What sprintf and vsprintf hand doprnt to pass to sprintf_put, on
 * their own stacks: sprintf_put writes at cursor and advances it.
 * Nothing reads unused. */
typedef struct sprintf_output {
  uint32_t unused;
  char* cursor;
} sprintf_output;

/* What doprnt writes its output through, as fwrite is called: the
 * text, the size of an element, the count, and the output; sprintf's
 * is sprintf_put (0x608c4).  The count written comes back. */
typedef int32_t sprintf_put_function(char* text, int32_t size, int32_t count,
                                     sprintf_output* output);

/* The record scpi_double_to_decimal fills at A0, on its caller's stack:
 * the double's sign, its significant digits in ASCII, and the exponent
 * of 0.d1d2... */
typedef struct scpi_decimal {
  int16_t sign;     /* +0x00 */
  char digits[16];  /* +0x02 */
  int16_t exponent; /* +0x12 */
} scpi_decimal;

/* A unit multiplier, one of scpi_multipliers at 0x27b2e, which
 * scpi_multiplier_exponent walks to the entry whose name is 0. */
typedef struct scpi_multiplier {
  uint16_t name;   /* +0x00: one or two letters, "M" milli, "MA" mega */
  int8_t exponent; /* +0x02: the power of ten */
  uint8_t unused;  /* +0x03: 0 */
} scpi_multiplier;

/* A unit a numeric parameter accepts: one entry of the table at +4 of
 * the parser's parameter descriptor, which scpi_parse_message_unit
 * passes scpi_parse_short and its kind, and scpi_match_suffix walks, 6
 * bytes a step, to the entry whose name is 0. */
typedef struct scpi_unit {
  char* name; /* +0x00: upper case and reversed, "ZHM" for MHZ;
                 scpi_suffix_compare_tail matches it against the suffix's end */
  int16_t
      exponent; /* +0x04: the power of ten the unit scales by; -128 for one that
                   takes a multiplier before it, scpi_multiplier_exponent's */
} scpi_unit;

/* An error code's message, one of scpi_error_messages at 0x28910, which
 * error_message_lookup walks to the entry whose code is -1. */
typedef struct scpi_error_message {
  int16_t code; /* +0x00 */
  char* text;   /* +0x02 */
} scpi_error_message;

/* The main tree's root child list at 0x53034 (docs/scpi/README.md, "How
 * it is read"): scpi_interpret's tree, beside the common commands'.  A
 * scpi_child_list with its 49 children counted, so that Ghidra types
 * them all. */
typedef struct scpi_root_children {
  uint16_t id;                    /* +0x00 */
  int16_t count;                  /* +0x02: 49 */
  int16_t default_child;          /* +0x04: 0x24 */
  struct scpi_node* children[49]; /* +0x06 */
} scpi_root_children;

typedef struct scpi_numeric_param {
  scpi_param_type type; /* SCPI_PARAM_NUMERIC */
  uint8_t last_index;   /* +0x01: values less one */
  int8_t* nv_widths;    /* +0x02: bits in the NV register; negative, signed */
  void* minimum;        /* +0x06: the least value accepted */
  void* maximum;        /* +0x0a: the greatest */
  scpi_validate_function*
      validate;   /* +0x0e: validate_num's check; clamp_to_limits */
  void* changed;  /* +0x12: called as each value is stored */
  void* value;    /* +0x16: the current value, in RAM */
  void* done;     /* +0x1a: called once they are stored */
  uint16_t flags; /* +0x1e: 2 saved in the NV register, 4 at 0x400000 */
  gps_request_code gps_request; /* +0x20: as scpi_setting_param's */
  uint8_t field_21;
  void* group_cell;   /* +0x22: as scpi_setting_param's; scpi_numeric_get_double
                         alone reads it */
  uint8_t is_integer; /* +0x26: 0 float, 1 integer, 2 or 3 several */
  uint8_t field_27;
  void* steps; /* +0x28: 8 bytes a value, whose routine at +4 gives validate the
                  step */
} scpi_numeric_param;

/* A value's step, one of scpi_numeric_param's steps, or on a setter's
 * stack for the clamp routines it calls: its routine, given the step,
 * the value and its index, gives the step; load_long gives the long at
 * +0. */
/* A keyword scpi_node_find looks up: its length, then its text from +2
 * (0x27bb2, 0x27bc6).  The text's size is scpi_trg_defined's buffer's,
 * the 20-byte frame at 0x27f7e. */
typedef struct scpi_keyword {
  int16_t length; /* +0x00 */
  char text[18];  /* +0x02 */
} scpi_keyword;

/* A decimal number scpi_scan_number parses into the lexer's token, and
 * the converters scpi_number_to_short to _double read: the parameters'
 * number docs/scpi/README.md describes.  0x26 bytes, the token's frame
 * in scpi_parse_message_unit (-0x28 to -0x2 of A6, 0x26ad8 and the
 * word at -0x2). */
typedef struct scpi_number {
  int16_t negative;  /* +0x00: 1 for a leading '-' (0x27002 to 0x2700c), which
                        the signed converters test (0x26118) */
  int8_t digits[16]; /* +0x02: the digits' values, 0 to 9 (0x27490), zero-filled
                        to 16 (0x274f4); digits[0] 0 for a zero */
  int16_t exponent;  /* +0x12: the written exponent, scpi_scan_exponent's,
                        adjusted by the point's place (0x2758c to 0x275b0) */
  int16_t suffix_length; /* +0x14: the units suffix's characters (0x27660), 0
                            for none */
  char suffix[12];       /* +0x16: the suffix in upper case (0x275f8); a 13th
                            character, which the limit at 0x275dc allows, lands in
                            scale, which scpi_apply_suffix then sets */
  int16_t scale; /* +0x22: the suffix's power of ten, scpi_match_suffix's, 0 for
                    no suffix (scpi_apply_suffix); the converters' scale */
  int16_t unit;  /* +0x24: the matched unit's index plus one, 0 for no suffix;
                    the parsers copy it after the value (0x25eda) */
} scpi_number;

/* A non-decimal number, #H, #Q or #B, token 0xd (0x27200). */
typedef struct scpi_token_non_decimal {
  uint16_t base;  /* +0x00: 16, 8 or 2 */
  uint32_t value; /* +0x02 */
} scpi_token_non_decimal;

/* A quoted string, token 0xf (0x27278, 0x2727c): its text, NUL
 * terminated, taken from the arena scpi_scan_token is given. */
typedef struct scpi_token_string {
  uint16_t length; /* +0x00: without the NUL */
  char* text;      /* +0x02 */
} scpi_token_string;

/* A definite or indefinite length block's header, #0 or #n and n
 * digits, token 0x12. */
typedef struct scpi_token_block {
  uint32_t length;     /* +0x00: the digits' value (0x270e4) */
  uint16_t indefinite; /* +0x04: 1 for #0 (0x270e8), else 0 (0x2708c) */
} scpi_token_block;

/* The SCPI lexer's class of a port's value, 0 to 0x105, as
 * scpi_char_classes (0x277ec) gives it. */
typedef enum __attribute__((packed)) scpi_char_class {
  SCPI_CLASS_LETTER = 0,
  SCPI_CLASS_DIGIT = 1,
  SCPI_CLASS_UNDERSCORE = 2,
  SCPI_CLASS_WHITE = 3, /* controls and space */
  SCPI_CLASS_HASH = 4,
  SCPI_CLASS_QUOTE = 5, /* " and ' */
  SCPI_CLASS_OTHER = 6,
  SCPI_CLASS_COLON = 7,
  SCPI_CLASS_SEMICOLON = 8,
  SCPI_CLASS_QUESTION = 9,
  SCPI_CLASS_OPEN = 10,
  SCPI_CLASS_CLOSE = 11,
  SCPI_CLASS_STAR = 15,
  SCPI_CLASS_PLUS = 16,
  SCPI_CLASS_MINUS = 17,
  SCPI_CLASS_POINT = 18,
  SCPI_CLASS_SLASH = 19,
  SCPI_CLASS_COMMA = 20,
  SCPI_CLASS_NEWLINE = 25,
  SCPI_CLASS_END = 26,        /* 0x100 and SCPI_PORT_END */
  SCPI_CLASS_PORT_ERROR = 27, /* SCPI_PORT_CLOSED to SCPI_PORT_INTERRUPTED */
  SCPI_CLASS_SKIPPED_END =
      28, /* 0x102, which scpi_skip_to_terminator reads past */
  SCPI_CLASS_DOLLAR = 30,
  SCPI_CLASS_AT = 31,
} scpi_char_class;

/* The SCPI lexer's token: scpi_scan_token fills the member for the
 * kind of token it returns, and the parameter converters take the
 * number.  A keyword (0x13, 0x14 with '?', 0x15 with ':') is its length
 * and up to 16 upper-case characters (0x26f98, 0x26fcc); '$' and a
 * digit 1 to 9 (0x11) is the digit, at +0 (0x2734a). */
typedef union scpi_token {
  scpi_number number;
  scpi_keyword keyword;
  scpi_token_non_decimal non_decimal;
  scpi_token_string string;
  scpi_token_block block;
  int16_t digit;
} scpi_token;

struct scpi_port;
/* A port's character routines.  read gives the next character, or
 * above 0xff an end: the console's gives 0x101 for a NUL; 0x103 to 0x105,
 * which the lexer reports as errors -102, -12 and -13 (0x2735c). */
typedef int32_t scpi_port_read_function(struct scpi_port* port);
typedef void scpi_port_unread_function(struct scpi_port* port);
typedef void scpi_port_put_function(struct scpi_port* port, int character);
typedef void scpi_port_write_function(struct scpi_port* port, char* text,
                                      int length);
typedef void scpi_port_bits_function(struct scpi_port* port, int bits);
typedef uint8_t scpi_port_byte_function(struct scpi_port* port);
typedef void scpi_port_close_function(struct scpi_port* port);

/* A port's method table, the console's at 0x29dd4: what the SCPI parser
 * reads commands through, the formatters write responses through, and
 * the status system keeps the status byte through.  Untyped entries are
 * those whose arguments no caller shows. */
typedef struct scpi_port_methods {
  void* open; /* +0x00: scpi_session_init, which returns the port */
  scpi_port_close_function*
      close; /* +0x04: scpi_session_close, which reads no argument */
  scpi_port_read_function*
      read; /* +0x08: console_port_read, as the port's own */
  scpi_port_unread_function*
      unread; /* +0x0c: scpi_input_ended: the next read gives the last character
                 again; the lexer calls it on the character after a token
                 (0x26f78) */
  void* read_text; /* +0x10: scpi_console_read, up to so many characters */
  scpi_port_put_function* put; /* +0x14: scpi_console_putc; above 0xff, CR LF */
  scpi_port_write_function*
      write; /* +0x18: scpi_console_write, up to length characters */
  scpi_port_write_function*
      write_block; /* +0x1c: scpi_console_write; scpi_format_block's */
  void* field_20;  /* +0x20: scpi_console_return_zero */
  void* field_24;  /* +0x24: scpi_console_return_zero */
  void* signal;    /* +0x28: scpi_console_return_zero; scpi_interpret passes it
                      scpi_response_begin_code, scpi_message_end_code or
                      scpi_input_end_code */
  void* field_2c;  /* +0x2c: scpi_console_return_zero */
  scpi_port_bits_function*
      status_set_bits; /* +0x30: status_byte_set_bits; status_condition_set's */
  scpi_port_bits_function* status_clear_bits; /* +0x34: status_byte_clear_bits;
                                                 status_condition_clear's */
  scpi_port_byte_function* status_byte_get;   /* +0x38: status_byte_get */
  scpi_port_bits_function* sre_set;           /* +0x3c: status_sre_set */
  scpi_port_byte_function* sre_get;           /* +0x40: status_sre_get */
} scpi_port_methods;

/* A port: the console's is at 0x29e18, which scpi_session_init returns.
 * The lexer reads through read, and through methods for the rest
 * (0x26ef6, 0x26f7a). */
typedef struct scpi_port {
  scpi_port_read_function* read; /* +0x00: console_port_read */
  scpi_port_methods* methods;    /* +0x04: 0x29dd4 */
} scpi_port;

struct scpi_step;
/* A step's routine: the step for value, the setting at index. */
typedef int32_t scpi_step_function(struct scpi_step* step, void* value,
                                   int32_t index);
typedef struct scpi_step {
  uint32_t value;               /* +0x00: the step's bits, a float or a long */
  scpi_step_function* function; /* +0x04 */
} scpi_step;

struct scpi_double_step;
/* A double step's routine: the step for value, the setting at index,
 * in D0:D1 (0x325c8, both stored at 0x325d2). */
typedef double scpi_double_step_function(struct scpi_double_step* step,
                                         double* value, int32_t index);
/* A double parameter's step, as clamp_double_to_step_and_limits reads
 * it: its routine follows 8 bytes, where scpi_step's follows 4
 * (0x325c4). */
typedef struct scpi_double_step {
  double value; /* +0x00: the step, which load_double gives */
  scpi_double_step_function* function; /* +0x08 */
} scpi_double_step;

/* The packed decimal record scpi_number_to_bcd fills and
 * scpi_bcd_to_double reads (docs/scpi/README.md, "Parameters and
 * responses"): two digits a byte, and the exponent of the 16 digits'
 * integer plus 16 and the scale (0x254fe). */
typedef struct scpi_bcd {
  int16_t negative;  /* +0x00 */
  uint8_t digits[8]; /* +0x02 */
  int16_t exponent;  /* +0x0a */
} scpi_bcd;

/* An arena, at the start of the buffer it hands out: its next free
 * byte and its last (arena_init, 0x29770). */
typedef struct scpi_arena {
  uint8_t* next; /* +0x00 */
  uint8_t* last; /* +0x04 */
} scpi_arena;

struct scpi_call;
/* A handler, the routine a node's setter or query entry holds: the
 * call's parameters, its response and the call (scpi_call_handler,
 * 0x29642). */
typedef int32_t scpi_handler_function(uint8_t* parameters, void* response,
                                      struct scpi_call* call);

/* A response item's formatter: the port, the value at its place in the
 * response, the item's argument and the call (scpi_format_response,
 * 0x29690). */
typedef int32_t scpi_format_function(struct scpi_port* output, void* value,
                                     uint32_t argument, struct scpi_call* call);

/* A choice parameter's list, the node's for scpi_parse_choice and
 * scpi_format_choice: its keywords to a NULL, and the value each gives,
 * or NULL for its index (0x26460). */
typedef struct scpi_choices {
  char** keywords; /* +0x00 */
  int16_t* values; /* +0x04 */
} scpi_choices;

/* A parameter's descriptor, what scpi_parameter_match and
 * scpi_parse_message_unit read (0x26c3e to 0x26c80): converters runs on
 * past the record, one for each parameter kind. */
typedef struct scpi_parameter_kinds {
  int16_t size;          /* +0x00: the value's bytes, after its kind word */
  int16_t optional;      /* +0x02: nonzero when it may be left out */
  scpi_unit* units;      /* +0x04 */
  scpi_choices* choices; /* +0x08 */
  void* converters[];    /* +0x0c: by the token's kind, NULL for one it does not
                            take; each kind's routine takes its own arguments */
} scpi_parameter_kinds;

/* A parameter a node takes, one of the list scpi_parameter_match walks
 * (0x278f4). */
typedef struct scpi_parameter {
  struct scpi_parameter* next; /* +0x00 */
  scpi_parameter_kinds* kinds; /* +0x04 */
} scpi_parameter;

/* A node's setter or query entry, 10 bytes (docs/scpi/README.md, "How
 * it is read"; scpi_call_handler, 0x29658). */
typedef struct scpi_handler {
  scpi_handler_function* function; /* +0x00 */
  scpi_parameter*
      parameters; /* +0x04: what it takes (scpi_header_resolve, 0x27d80) */
  int16_t parameters_size; /* +0x08: the bytes they take in the call's buffer */
} scpi_handler;

/* One item of a query's response: how scpi_format_response writes the
 * value at its place in the response, 10 bytes (0x296dc). */
typedef struct scpi_response_item {
  scpi_format_function* format; /* +0x00: NULL after the last */
  int16_t size;                 /* +0x04: the value's bytes */
  uint32_t argument;            /* +0x06: the node's list, for a choice */
} scpi_response_item;

/* A node of the SCPI tree (docs/scpi/README.md, "How it is read"). */
typedef struct scpi_node {
  char* keywords; /* +0x00: the short form, a NUL, the rest of the long form */
  struct scpi_child_list* children;   /* +0x04: NULL for a leaf */
  scpi_handler handlers[2];           /* +0x08: the setter, then the query */
  scpi_response_item* response_items; /* +0x1c: the query's (0x29488) */
  int16_t
      response_size; /* +0x20: the bytes the query's response takes (0x29666) */
  int16_t path_word; /* +0x22: the word it adds to the path, -1 for none
                        (0x27c50) */
} scpi_node;

/* A node's children, sorted by keyword for scpi_node_find's bisection
 * (docs/scpi/README.md, "How it is read"); scpi_root_children is the
 * main tree's. */
typedef struct scpi_child_list {
  uint16_t id;           /* +0x00 */
  int16_t count;         /* +0x02 */
  int16_t default_child; /* +0x04: the child a header may leave out, -1 for none
                            (0x27c80) */
  scpi_node* children[]; /* +0x06 */
} scpi_child_list;

/* A header's path, scpi_interpret's 20 bytes (0x2935e): the list the
 * next keyword is looked up in, and the words the nodes on the way and
 * their numeric suffixes add, the handler's first parameters. */
typedef struct scpi_path {
  scpi_child_list* list; /* +0x00 */
  int16_t count;         /* +0x04 */
  uint16_t words[7];     /* +0x06 */
} scpi_path;

struct status_model;
/* The call record scpi_interpret builds for a handler, on its stack
 * (docs/firmware/README.md, "Hand-written C"; 0x29322 to 0x2932e). */
typedef struct scpi_call {
  scpi_node* node;   /* +0x00 */
  uint16_t query;    /* +0x04: 0 the setter, 1 the query, the handler's index */
  scpi_arena* arena; /* +0x06 */
  uint8_t* parameters; /* +0x0a: each parameter's kind word, then its
                          value */
  uint8_t* response;   /* +0x0e: the query's, each item's value at its place */
  struct scpi_port* input;     /* +0x12 */
  struct scpi_port* output;    /* +0x16 */
  struct status_model* status; /* +0x1a */
} scpi_call;

/* The SCPI task's response areas, end to end from 0x10272a: a record
 * for each source return_zero gives, of which there are two, as
 * robin_blocks and robin_arrays hold.  A query writes its response into
 * its source's record and answers its address; a setter of several
 * values stages them in its source's scpi_staged_values and sends pllc
 * their address. */
/* A query's text. */
typedef char scpi_response_text[0x11a];
/* A satellite list, or :ROBin:POINts?' counts. */
typedef int16_t scpi_response_list[32];
/* :ROBin:MEMory:OVERflow:COUNt?'s counts, an input's each. */
typedef double robin_overflow_counts[3];
/* A setter's values, the pllc_request's values[0], its index the last. */
typedef int32_t scpi_staged_values[3];
/* :ROBin? and :ROBin:TSTamp?'s points. */
typedef int16_t robin_point_record[80];

/* The parser's context, which scpi_context_init (0x26a80) fills for
 * scpi_interpret. */
typedef struct scpi_context {
  struct scpi_port* input; /* +0x00 */
  uint16_t word;     /* +0x04: 0 from scpi_interpret; the block and expression
                        converters' */
  scpi_path* path;   /* +0x06 */
  scpi_call* call;   /* +0x0a */
  scpi_arena* arena; /* +0x0e */
  scpi_child_list* main_tree;   /* +0x12: the main tree's root list */
  scpi_child_list* common_list; /* +0x16: the common commands' list */
  uint8_t field_1a[4];
  uint32_t field_1e; /* +0x1e: what scpi_interpret's frame holds there, unset;
                        the block converter's last */
} scpi_context;

/* A response's array, as scpi_format_double_array and _short_array take
 * it. */
typedef struct scpi_array {
  int32_t count; /* +0x00 */
  void* values;  /* +0x04: doubles or shorts */
} scpi_array;

/* Doubles each with its resolution, as scpi_format_resolution_array
 * takes them. */
typedef struct scpi_resolution_array {
  int32_t count;        /* +0x00 */
  double* values;       /* +0x04 */
  int16_t* resolutions; /* +0x08 */
} scpi_resolution_array;

/* A definite-length block, as scpi_format_block takes it. */
typedef struct scpi_block {
  uint32_t length; /* +0x00 */
  void* data;      /* +0x04: its bytes */
} scpi_block;

/* An array response by its kind, as scpi_format_array takes it and
 * robin_store makes it. */
typedef struct scpi_array_record {
  int8_t kind; /* +0x00: the jump table at 0x267dc */
  uint8_t unused;
  void* array; /* +0x02: a scpi_block or scpi_array */
} scpi_array_record;

/* A double scpi_format_double_precision writes, to so many digits or
 * to a resolution. */
typedef struct scpi_precise_double {
  double value; /* +0x00 */
  char
      to_digits; /* +0x08: nonzero for precision as digits, else a resolution */
  uint8_t unused;
  int16_t precision; /* +0x0a */
} scpi_precise_double;

/* A float scpi_format_float_resolution writes, and its resolution. */
typedef struct scpi_float_resolution {
  float value;        /* +0x00 */
  int16_t resolution; /* +0x04 */
} scpi_float_resolution;

/* One status register of the SCPI status tree.  16 bytes. */
typedef struct status_register {
  uint16_t condition; /* +0x00 */
  uint16_t
      negative_transition; /* +0x02: falling conditions that become events */
  uint16_t positive_transition;   /* +0x04: rising ones */
  uint16_t enable;                /* +0x06: events reported to the parent */
  uint16_t event;                 /* +0x08: latched, cleared as read */
  struct status_register* parent; /* +0x0a: the register it reports into */
  int16_t parent_bit;             /* +0x0e: at this bit */
} status_register;

/* An entry of the status conditions table: which register and bit a
 * condition sets, and how.  6 bytes. */
typedef struct status_condition {
  int16_t kind;
  int16_t register_index;
  int16_t bit;
} status_condition;

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
 * have no name here. */
typedef enum __attribute__((packed)) status_condition_code {
  STATUS_SCPI_NO_MEMORY = -56, /* -310 System error: an allocation failed */
  STATUS_SCPI_NUMERIC_DATA_ERROR = -55, /* -120 Numeric data error */
  STATUS_SCPI_GET_NOT_ALLOWED =
      -54, /* -105 GET not allowed: SCPI_PORT_SKIPPED_END after white space */
  STATUS_SCPI_SUFFIX_TOO_LONG = -53, /* -134 Suffix too long */
  STATUS_SCPI_TOO_MUCH_DATA = -52,   /* -223 Too much data */
  STATUS_SCPI_DATA_TYPE_ERROR = -51, /* -104 Data type error */
  STATUS_SCPI_EXPRESSION_NOT_ALLOWED =
      -50,                              /* -178 Expression data not allowed */
  STATUS_SCPI_BLOCK_NOT_ALLOWED = -49,  /* -168 Block data not allowed */
  STATUS_SCPI_STRING_NOT_ALLOWED = -48, /* -158 String data not allowed */
  STATUS_SCPI_CHARACTER_NOT_ALLOWED = -47, /* -148 Character data not allowed */
  STATUS_SCPI_NUMERIC_NOT_ALLOWED = -46,   /* -128 Numeric data not allowed */
  STATUS_SCPI_EXPONENT_TOO_LARGE = -45,    /* -123 Exponent too large */
  STATUS_SCPI_MACRO_EXECUTION_ERROR = -44, /* -272 Macro execution error */
  STATUS_SCPI_INVALID_SEPARATOR = -43,     /* -103 Invalid separator */
  STATUS_SCPI_MACRO_RECURSION = -42,       /* -276 Macro recursion error */
  STATUS_SCPI_INSIDE_MACRO = -41, /* -183 Invalid inside macro definition */
  STATUS_SCPI_FORMAT_FAILED =
      -40, /* -310 System error: a response it cannot format */
  STATUS_SCPI_INVALID_CHARACTER = -39,      /* -101 Invalid character */
  STATUS_SCPI_EXTRA_PARAMETER = -38,        /* -108 Parameter not allowed */
  STATUS_SCPI_MISSING_PARAMETER = -37,      /* -109 Missing parameter */
  STATUS_SCPI_MACRO_HEADER_NOT_FOUND = -36, /* -278 Macro header not found */
  STATUS_SCPI_ILLEGAL_MACRO_LABEL = -35,    /* -273 Illegal macro label */
  STATUS_SCPI_MACRO_REDEFINITION =
      -34, /* -277 Macro redefinition not allowed */
  STATUS_SCPI_HEADER_LEAF =
      -33, /* -113 Undefined header: a keyword past a leaf */
  STATUS_SCPI_UNDEFINED_HEADER = -32, /* -113 Undefined header */
  STATUS_SCPI_INVALID_SUFFIX = -31,   /* -131 Invalid suffix */
  STATUS_SCPI_PARAMETER_DOLLAR =
      -30, /* -181 Invalid outside macro definition: a $ digit as a parameter */
  STATUS_SCPI_NO_LIST =
      -28, /* -310 System error: a choice without its keywords */
  STATUS_SCPI_ILLEGAL_PARAMETER_VALUE = -27, /* -224 Illegal parameter value */
  STATUS_SCPI_EXPRESSION_ERROR = -24,        /* -170 Expression error */
  STATUS_SCPI_TOO_MANY_DIGITS = -23,         /* -124 Too many digits */
  STATUS_SCPI_STRING_ERROR = -22,            /* -150 String data error */
  STATUS_SCPI_INVALID_STRING = -21,          /* -151 Invalid string data */
  STATUS_SCPI_INVALID_DIGIT = -20,     /* -121 Invalid character in number */
  STATUS_SCPI_INVALID_RADIX = -19,     /* -101 Invalid character */
  STATUS_SCPI_INVALID_BLOCK = -18,     /* -161 Invalid block data */
  STATUS_SCPI_INVALID_NUMBER = -17,    /* -121 Invalid character in number */
  STATUS_SCPI_MNEMONIC_TOO_LONG = -16, /* -112 Program mnemonic too long */
  STATUS_SCPI_QUERY_AFTER_INDEFINITE =
      -15, /* -440 Query UNTERMINATED after indefinite response */
  STATUS_SCPI_QUERY_DEADLOCKED = -14,    /* -430 Query DEADLOCKED */
  STATUS_SCPI_QUERY_INTERRUPTED = -13,   /* -410 Query INTERRUPTED */
  STATUS_SCPI_QUERY_UNTERMINATED = -12,  /* -420 Query UNTERMINATED */
  STATUS_SCPI_DATA_OUT_OF_RANGE = -11,   /* -222 Data out of range */
  STATUS_SCPI_QUERY_ERROR = -10,         /* -400 Query error */
  STATUS_SCPI_SUFFIX_NOT_ALLOWED = -9,   /* -138 Suffix not allowed */
  STATUS_SCPI_EXECUTION_ERROR = -8,      /* -200 Execution error */
  STATUS_SCPI_COMMAND_ERROR = -7,        /* -100 Command error */
  STATUS_SCPI_SYNTAX_ERROR = -6,         /* -102 Syntax error */
  STATUS_SCPI_OUT_OF_MEMORY = -5,        /* -321 Out of memory */
  STATUS_ESR_QUERY_ERROR = -4,           /* Standard Event bit 2 */
  STATUS_ESR_DEVICE_ERROR = -3,          /* Standard Event bit 3 */
  STATUS_ESR_EXECUTION_ERROR = -2,       /* Standard Event bit 4 */
  STATUS_ESR_COMMAND_ERROR = -1,         /* Standard Event bit 5 */
  STATUS_NO_CONDITION = 0,               /* does nothing */
  STATUS_PARAMETER_NOT_ALLOWED = 0x01,   /* -108 Parameter not allowed */
  STATUS_MISSING_PARAMETER = 0x02,       /* -109 Missing parameter */
  STATUS_UNDEFINED_HEADER = 0x03,        /* -113 Undefined header */
  STATUS_INVALID_SUFFIX = 0x04,          /* -131 Invalid suffix */
  STATUS_INVALID_STRING_DATA = 0x05,     /* -151 Invalid string data */
  STATUS_EXECUTION_ERROR = 0x06,         /* -200 Execution error */
  STATUS_PARAMETER_ERROR = 0x07,         /* -220 Parameter error */
  STATUS_SETTINGS_CONFLICT = 0x08,       /* -221 Settings conflict */
  STATUS_DATA_OUT_OF_RANGE = 0x09,       /* -222 Data out of range */
  STATUS_TOO_MUCH_DATA = 0x0a,           /* -223 Too much data */
  STATUS_ILLEGAL_PARAMETER_VALUE = 0x0b, /* -224 Illegal parameter value */
  STATUS_DATA_CORRUPT_OR_STALE = 0x0c,   /* -230 Data corrupt or stale */
  STATUS_HARDWARE_MISSING = 0x0d,        /* -241 Hardware missing */
  STATUS_DEVICE_SPECIFIC_ERROR = 0x0e,   /* -300 Device-specific error */
  STATUS_SYSTEM_ERROR = 0x0f,            /* -310 System error */
  STATUS_MEMORY_ERROR = 0x10,            /* -311 Memory error */
  STATUS_CONFIGURATION_LOST = 0x11,      /* -315 Configuration memory lost */
  STATUS_SELFTEST_FAILED = 0x12,         /* -330 Self-test failed */
  STATUS_COMMUNICATION_ERROR = 0x13,     /* -360 Communication error */
  STATUS_PARITY_ERROR = 0x14,            /* -361 Parity error */
  STATUS_FRAMING_ERROR = 0x15,           /* -362 Framing error */
  STATUS_INPUT_OVERRUN = 0x16,           /* -363 Input buffer overrun */
  STATUS_POWER_ON = 0x19,                /* *ESR? bit 7 */
  STATUS_LOCKED_SET = 0x1a,              /* :OPERation bit 1, locked to GPS */
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

/* The Status Byte's bits the firmware names (structures.md, "The SCPI
 * session"): RQS, which status_update_rqs keeps and the service request
 * enable cannot set. */
typedef enum __attribute__((packed)) status_byte_bit {
  STATUS_BYTE_REQUEST_SERVICE = 0x40,
} status_byte_bit;

/* An entry of the error queue: an error code and what status_set_bit is
 * given with it, status_report's message (error_queue_push, 0x285d4). */
typedef struct status_error_entry {
  int16_t code;   /* +0x00 */
  uint32_t value; /* +0x02 */
} status_error_entry;

/* The error queue, which error_queue_create allocates and
 * status_model.queue points at: a ring of entries, one more than it
 * holds, so that head equal to tail is empty (0x28588). */
typedef struct status_error_queue {
  int16_t head;                 /* +0x00: the newest entry */
  int16_t tail;                 /* +0x02: the entry before the oldest */
  struct status_model* status;  /* +0x04: the model it reports into */
  int16_t size;                 /* +0x08: entries, the capacity plus one */
  status_error_entry entries[]; /* +0x0a */
} status_error_queue;

/* One register's entry in status_register_layouts (0x2f79e), seven of them,
 * which status_init sets the registers up from (0x2f4ec to 0x2f4fe) and
 * the :STATus enable and transition setters check a value against
 * (scpi_check_range's bounds, +0x04 and +0x06).  22 bytes. */
typedef struct status_register_layout {
  uint8_t parent_index;  /* +0x00: the register it reports into */
  uint8_t unused;        /* +0x01 */
  uint16_t parent_bit;   /* +0x02: at this bit; 0 for the Status Byte */
  uint16_t maximum;      /* +0x04: the greatest enable or filter */
  uint16_t minimum;      /* +0x06 */
  uint16_t defined_bits; /* +0x08: the bits the register has */
  uint32_t* enable;      /* +0x0a: its nv_settings.status_enable */
  uint32_t* positive;    /* +0x0e: its nv_settings filters, NULL for the */
  uint32_t* negative;    /* +0x12: Status Byte and Standard Event */
} status_register_layout;

/* The SCPI status system: status_create builds it, status_object points
 * at it. */
typedef struct status_model {
  int16_t register_count; /* +0x00: 7 */
  scpi_port* output; /* +0x02: set for the top register; the port whose status
                        byte it keeps (status_condition_set, 0x2802a) */
  status_error_queue* queue;    /* +0x06: built with room for 30 */
  status_condition* conditions; /* +0x0a */
  int32_t
      field_0e; /* +0x0e: status_create's last argument, 0; nothing reads it */
  status_register* registers[7]; /* +0x12 */
} status_model;

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

/* @@Ac, date. */
typedef struct oncore_date_fields {
  uint8_t month;
  uint8_t day;
  uint16_t year;
} oncore_date_fields;

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

/* The curv task's aging fit, at 0x101fd8: the curve a + b y + c w
 * aging_fit fits to the EFC history's weighted entries, which
 * aging_fit_collect copies into pll.  aging_fit fits a second one on
 * its stack for the three-term fit, and keeps it when it fits better. */
typedef struct aging_curve_state {
  aging_fit_mode mode;    /* +0x00 */
  uint8_t minimum_weight; /* +0x01: only entries of this weight or more are
                             fitted, 1 or 2 */
  uint32_t oldest_time;   /* +0x02: fit_span's, the fitted entries' least time,
                             truncated */
  uint32_t newest_time;   /* +0x06: their greatest */
  int32_t oldest_index;   /* +0x0a: the entry with oldest_time */
  int32_t newest_index;   /* +0x0e: the entry with newest_time */
  float a; /* +0x12: fit_evaluate_latest moves it so the curve meets the newest
              entry */
  float b; /* +0x16: the slope in time */
  float c; /* +0x1a: the coefficient of the log term, 0 for a line */
  float c_step; /* +0x1e: fit_log_search's step in c, a tenth as large each pass
                 */
  float rms;    /* +0x22: the residuals' */
  float predicted_uncertainty; /* +0x26: fit_quality's HQ, or 4.32e-4 in modes 0
                                  and 1 */
} aging_curve_state;

/* What a fit routine gives linear_regression, on its own stack: the
 * arrays to regress and the weight an entry needs, and the line it
 * fits back. */
typedef struct regression_record {
  uint8_t minimum_weight; /* +0x00: only entries of this weight or more */
  uint8_t field_01;
  float* x;         /* +0x02: pll.efc_history.times */
  float* y;         /* +0x06 */
  uint8_t* weights; /* +0x0a: pll.efc_history.weights */
  float slope;      /* +0x0e: 0 unless with_slope */
  float intercept;  /* +0x12: the mean of y when no slope is fitted */
  float rms;        /* +0x16: the residuals' */
} regression_record;

/* What fit_log_search gives fit_three_point, on its stack: three values
 * of the log coefficient and the rms each gave, through which
 * fit_three_point puts a parabola, and the parabola's vertex it writes
 * back. */
typedef struct three_point_fit {
  float* x;       /* +0x00: three values of aging_curve_state.c */
  float* y;       /* +0x04: the rms at each */
  float vertex_x; /* +0x08: the c fit_log_search keeps */
  float vertex_y; /* +0x0c */
} three_point_fit;

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

/* @@Bb: one visible satellite, padded by the decoder's table (0x50fd8)
 * from 7 bytes to 10. */
typedef struct oncore_visible_satellite {
  uint8_t satellite;
  uint8_t pad_1;
  int16_t doppler;   /* Hz */
  uint8_t elevation; /* degrees */
  uint8_t pad_5;
  uint16_t azimuth; /* degrees */
  oncore_health health;
  uint8_t pad_9;
} oncore_visible_satellite;

/* @@Bb, the visibility message. */
typedef struct oncore_visibility_fields {
  uint8_t count;
  uint8_t pad_1;
  oncore_visible_satellite satellites[12];
} oncore_visibility_fields;

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
  uint8_t hold_height_type;                  /* @@As: 0 GPS ellipsoid, 1 MSL */
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
  uint8_t
      command_monitor_state;   /* 0 disabled, 1 standby, 2 query, 3 query-ack */
  void* command_monitor_query; /* the next query of the list at 0x50e5e */
  uint32_t command_monitor_time; /* uptime of its last state change */
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
  FPGA_BIT_MODE = 0,   /* register 0, 0x03: 0 counts, 3 the interpolator's
                          references (counter_arm, interpolator_prepare) */
  FPGA_BIT_PRESET = 1, /* register 0, 0x40: set around arming; counter_selftest
                          reads a count of 0 under it */
  FPGA_BIT_START = 2,  /* register 1, 0x01: pulsed by interpolator_measure */
  FPGA_BIT_REFERENCE = 3, /* register 1, 0x02: an interpolator_reference */
  FPGA_BIT_ADDRESS = 4, /* register 1, 0xf8: the address bits fpga_read uses */
  FPGA_BIT_SYNC = 5,    /* register 3, 0x03: counter_set_sync */
} fpga_bit_index;

/* A register the FPGA reads back through fpga_read and fpga_read_byte,
 * by the number fpga_read puts in fpga_bit_table entry 4's address
 * bits.  The count is 24 bits, three bytes, each two registers'
 * nibbles (counter_read_raw 0x40c06). */
typedef enum __attribute__((packed)) fpga_register {
  FPGA_REGISTER_STATUS = 0x00,       /* the counter's ready bits and the seconds
                                        (counter_wait_ready, fpga_read_seconds) */
  FPGA_REGISTER_INTERPOLATOR = 0x10, /* interpolator_read */
  FPGA_REGISTER_COUNT_LOW = 0x12,
  FPGA_REGISTER_COUNT_MIDDLE = 0x14,
  FPGA_REGISTER_COUNT_HIGH = 0x16,
} fpga_register;

/* The reference interpolator_measure measures, fpga_bit_table entry 3. */
typedef enum __attribute__((packed)) interpolator_reference {
  INTERPOLATOR_REFERENCE_LOW = 0,
  INTERPOLATOR_REFERENCE_HIGH = 1,
} interpolator_reference;

/* How interval_rearm (0x450c4), and interval_next_reading after its
 * reading, arm the next: calibrating the interpolator first, plainly,
 * or syncing the counter to the 1PPS first (0x4506e, 0x45082). */
typedef enum __attribute__((packed)) interval_arm_mode {
  INTERVAL_ARM_CALIBRATE = 0,
  INTERVAL_ARM_PLAIN = 1,
  INTERVAL_ARM_SYNC = 2,
} interval_arm_mode;

/* An FPGA configuration, a bitstream fpga_program loads and
 * fpga_verify checks, by its index in fpga_configurations.  The table
 * holds one, the first bitstream, the time-interval counter's design
 * (outputs.md, "The FPGA bitstreams"). */
typedef enum __attribute__((packed)) fpga_configuration_index {
  FPGA_CONFIGURATION_COUNTER = 0,
} fpga_configuration_index;

/* The chip of a configuration fpga_verify_chip reads back: the first
 * through port F bit 6, the second through bit 7, its bytes 0xf0d into
 * the configuration (0x2384a). */
typedef enum __attribute__((packed)) fpga_chip {
  FPGA_CHIP_FIRST = 1,
  FPGA_CHIP_SECOND = 2,
} fpga_chip;

/* A configuration's bitstream in ROM, from start to end. */
typedef struct fpga_configuration_entry {
  uint8_t* start;
  uint8_t* end;
} fpga_configuration_entry;

/* The FPGA's configurations, at 0x21e2c: the highest index the loader
 * and the checks take (cmp.b and bls at 0x22dbc, 0x23984 and 0x1891c),
 * 1, though only entry 0 is a configuration and entry 1 would be read
 * from the second bitstream's first bytes. */
typedef struct fpga_configuration_table {
  uint8_t last; /* +0x00 */
  uint8_t field_01;
  fpga_configuration_entry entries[1]; /* +0x02 */
} fpga_configuration_table;

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

/* The status screen's mode, pll_mode's result from the stage and
 * pll.manual (0x45b70), which the screen and the :ROSCillator:HOLDover
 * queries read (docs/screen-format-strings.md, "Mode suffixes"). */
typedef enum __attribute__((packed)) pll_screen_mode {
  PLL_MODE_NONE = 0,
  PLL_MODE_HOLDOVER_MANUAL = 1,
  PLL_MODE_HOLDOVER = 2,
  PLL_MODE_RECOVERY = 3,
  PLL_MODE_POWERUP = 4,
  PLL_MODE_LOCKED = 5,
} pll_screen_mode;

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

/* Which GPS sequence waits for the acknowledgment of the request it sent,
 * gps_ack_awaited, gpsx_send's last argument; pllp_task's reply handling
 * tells the console command's and the self-test's apart. */
typedef enum __attribute__((packed)) gps_ack_awaited_kind {
  GPS_ACK_NONE = 0,
  GPS_ACK_INIT = 1,
  GPS_ACK_LEAP_ACK = 2,
  GPS_ACK_LEAP_CHECK = 3,
  GPS_ACK_COMMAND = 4,
  GPS_ACK_SELFTEST = 5,
} gps_ack_awaited_kind;

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
  pll_fine_state fine_state;         /* +0x17: fine's; 7 to 9 slew the phase
                                 (PLL_FINE_SLEW_FIRST, _LAST) */
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

/* The GPS link's counters, which the "GPS Process Monitor" page
 * (gps_print_process_monitor) prints.  Cleared as one, memset(&gps_counts,
 * 0, 0x36).  All words. */
typedef struct gps_statistics {
  uint16_t received; /* Msgs Rcvd */
  uint16_t sent;     /* Msgs Sent */
  uint16_t jammed;   /* Msgs Jam */
  uint16_t sent_errors;
  uint16_t received_errors;
  uint16_t jam_errors;
  uint16_t successes;  /* Success */
  uint16_t field_0e;   /* no response code counts it and nothing prints it */
  uint16_t rejects;    /* Reject */
  uint16_t timeouts;   /* Timeout */
  uint16_t busy;       /* Busy */
  uint16_t aborts;     /* Abort */
  uint16_t no_command; /* No Cmd */
  uint16_t no_query;   /* No Qry */
  uint16_t unknown_command;  /* ? Cmd */
  uint16_t unknown_sender;   /* ? Sndr */
  uint16_t commands_sent;    /* Cmds Sent */
  uint16_t responses_read;   /* Resp Read */
  uint16_t e_retries;        /* E Retry */
  uint16_t t_retries;        /* T Retry */
  uint16_t raim_detected;    /* Time RAIM page: (%d detected) */
  uint16_t raim_alarms;      /* # Alarms */
  uint16_t unknown_response; /* ? Resp */
  uint16_t frame_errors;     /* Frame */
  uint16_t checksum_errors;  /* Cksum */
  uint16_t command_monitor_failures;
  uint16_t status_monitor_failures;
} gps_statistics;

/* The last exception, which the exception report prints: "Exception:
 * %s, vector %04X, PC = %08X, SR = %04X, addr = %08X". */
typedef struct exception_record {
  uint8_t pending;
  char name[5];
  uint16_t sr;
  uint32_t pc;
  uint16_t vector;
  uint32_t address; /* printed when the vector's format is not 0 */
} exception_record;

/* A mutex: an exchange holding one message, its token, which
 * mutex_acquire (0x1043a) takes with request_x unless the calling task
 * owns it already, and the owner and its nested holds.  mutex_clear
 * (0x104d2) clears all 0x18 bytes; mutex_init (0x10400) creates the
 * exchange and sends the token. */
typedef struct mutex {
  uint32_t field_00;
  uint32_t exchange; /* +0x04: 0 until mutex_init; mutex_acquire fails 1 */
  uint32_t owner;    /* +0x08: the holding task's id, 0 when free */
  int32_t holds;     /* +0x0c: the owner's holds past its first, each 0x80 */
  uint32_t field_10;
  uint32_t field_14;
} mutex;

/* A routine the spool task runs for a caller, with its three arguments. */
typedef void spoo_function(uint32_t a, uint32_t b, uint32_t c);

/* A clock (klok task) entry, which print_clk_list prints: when it fires,
 * its period, the function it calls and its event class.  A date is
 * year << 16 | month << 8 | day, a time hour << 16 | minute << 8 |
 * second.  0x1a bytes; a pool of five. */
typedef struct klok_entry {
  struct klok_entry* next;
  uint32_t time; /* at */
  uint32_t date;
  uint32_t period_time; /* period */
  uint32_t period_date;
  spoo_function* function; /* function address: spoo_run runs it */
  int16_t event_class;     /* event class */
} klok_entry;

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

/* An event log entry: its time and date, packed as a klok_entry's, and
 * the format and two arguments it is printed with.  log_read formats
 * one as "Log %03d:%04d%02d%02d.%02d:%02d:%02d" and its text. */
typedef struct log_entry {
  uint32_t time;
  uint32_t date;
  char* format;
  int32_t argument_1;
  int32_t argument_2;
} log_entry;

/* A page of the event log, three entries, as log_read loads it. */
typedef struct log_page {
  uint16_t write_count; /* +0x00: incremented by each log_page_write */
  uint16_t
      checksum; /* +0x02: log_page_checksum's byte sum and its complement */
  log_entry entries[3];
} log_page;

/* An entry of fpga_bit_table: the FPGA register a bit is in and its
 * mask, which fpga_set_bit sets or clears in fconf_regs. */
typedef struct fpga_bit {
  uint8_t reg;
  uint8_t mask;
  uint8_t is_flag; /* 1: fpga_fields_to_registers sets mask for a nonzero value;
                      0: ORs the value in */
  uint8_t field_3;
} fpga_bit;

/* The time-interval counter's interpolator calibration, which
 * interpolator_calibrate steps: HDAC channels 2 and 3 trim the counter
 * until its readings at the two reference points fall in 1 to 25 and
 * 201 to 225, exactly 200 apart. */
typedef struct interpolator_calibration {
  uint8_t initialized; /* set with both DACs at 0x1f the first time */
  uint8_t calibrated;
  uint8_t hdac_2; /* HDAC channel 2's value */
  uint8_t hdac_3; /* HDAC channel 3's value */
  uint8_t result_valid;
  uint8_t result_hdac_2; /* the values that gave a spread of 200 */
  uint8_t result_hdac_3;
  uint8_t reading_low;  /* interpolator_measure(0), wanted 1 to 25 */
  uint8_t reading_high; /* interpolator_measure(1), wanted 201 to 225 */
} interpolator_calibration;

/* A SCPI setting's descriptor, in ROM, the switches and choices nv_save
 * saves: 0x1e bytes. */
typedef struct scpi_setting_param {
  scpi_param_type type; /* SCPI_PARAM_SETTING */
  uint8_t last_index;   /* +0x01: values less one */
  int8_t* nv_widths;    /* +0x02: each value's bits in the NV register */
  int8_t maximum;       /* +0x06: toggle_change refuses a value above it */
  uint8_t field_07;
  void* changed;                /* +0x08: called as the value is stored */
  uint8_t* value;               /* +0x0c: the current value, a byte each */
  void* done;                   /* +0x10: called once it is */
  uint16_t flags;               /* +0x14: as scpi_numeric_param's */
  gps_request_code gps_request; /* +0x16: what pll_scpi_send_to_loop sends
                                   gpsx_exchange, GPS_REQUEST_NONE none */
  uint8_t field_17;
  void* group_cell; /* +0x18: 0x101f92 to 0x101f9a by command group; no code
                       reads it */
} scpi_setting_param;

/* A SCPI action's descriptor, in ROM, 6 bytes, from 0x4132a to 0x4137d.
 * validate_or_doaction refuses an index above last_index, then
 * pll_scpi_do_action calls action with the index and the source; it
 * stores nothing and leaves finish_scpi_cmd uncalled. */
typedef struct scpi_action_param {
  scpi_param_type type; /* SCPI_PARAM_ACTION */
  uint8_t last_index;   /* +0x01: indexes less one */
  void* action;         /* +0x02: called with the index and the source */
} scpi_action_param;

/* A SCPI request to the pllc task: the 24-byte message pllc_request_a
 * to pllc_request_e build in their frames and send pllc_exchange, which
 * pll_scpi_receive receives into 24 bytes and get_setup_vars copies
 * into pll_scpi_param to pll_scpi_values.  The kernel copies +0x04 to
 * +0x17 (kernel_copy_message). */
typedef struct pllc_request {
  uint32_t field_00;  /* +0x00: not set; the kernel does not copy it */
  uint32_t field_04;  /* +0x04: cleared; as gps_request_entry's */
  void* param;        /* +0x08: the descriptor, a scpi_numeric_param,
                         scpi_setting_param or scpi_action_param; pll_scpi_param */
  uint8_t index;      /* +0x0c: pll_scpi_index */
  uint8_t source;     /* +0x0d: pll_scpi_source; for 0 the SCPI task is signaled
                         when it is done */
  uint8_t field_0e;   /* +0x0e: not set */
  uint8_t setting;    /* +0x0f: a setting's new value, pll_scpi_setting;
                         pllc_request_a's alone */
  uint32_t values[2]; /* +0x10: by the numeric descriptor's is_integer: 0 the
                         value in values[1], 1 in values[0], 3 both, 2 values[0]
                         the address of index + 1 longs */
} pllc_request;

/* A decoder of an Oncore message and the record in gps it fills. */
typedef struct oncore_decoder {
  void* decode; /* gps_response_decode calls it with the cursor and record, and
                   takes the cursor back */
  void* record;
} oncore_decoder;

/* An entry of the GPS task's Oncore message table, oncore_messages: 56
 * bytes (docs/firmware/oncore/README.md, "The message table"). */
typedef struct oncore_message_entry {
  char* id;     /* +0x00: the two-letter ID; zero for an empty slot */
  uint8_t kind; /* +0x04: gps_request_build composes kinds 1 and 3 */
  uint8_t field_05;
  void* encoders[4]; /* +0x06: argument encoders */
  char* query;       /* +0x16: the query form; zero if none */
  uint8_t
      timeout; /* +0x1a: gps_response_timed_out's limit on the uptime elapsed */
  uint8_t field_1b;
  uint16_t length; /* +0x1c: the response's, @@ to line end; gps_response_lookup
                      returns it */
  uint8_t
      match_kind; /* +0x1e: what gps_response_match switches on when below 10 */
  uint8_t field_1f;
  oncore_decoder
      decoders[3]; /* +0x20: gps_response_decode runs them to the first zero */
} oncore_message_entry;

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
  int32_t holdover_status_threshold; /* :ROSC:HOLD:DUR:STAT:THR */
  float limit_threshold;             /* :ROSC:LIMit:THReshold */
  float holdover_limit_threshold;    /* :ROSC:HOLD:LIMit:THReshold */
  uint8_t holdover_limit_state;      /* :ROSC:HOLD:LIMit:STATe */
  uint8_t douglas_state;             /* :DOUGlas:STATe */
  uint8_t field_1e;                  /* only its descriptor refers to it */
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

/* A time-interval reading, interval_reading, as counter_read leaves it.
 * 13 bytes. */
typedef struct interval_reading_fields {
  double value;  /* s; SCPI's NaN, 9.91e37, on an error */
  uint8_t valid; /* the :DIAG query's flag */
  counter_error errors;
  uint16_t tag;  /* 0xfff6, which the :DIAG query returns with it */
  uint8_t armed; /* clear, the routines report a measurement aborted */
} interval_reading_fields;

/* The time-interval counter's configuration, counter_configuration, which
 * counter_arm, counter_calibrate and counter_read are given.  A mode and
 * a state go in pairs: a state of 2 asks for the work, 1 says it is done.
 * 0x20 bytes, interpolator_calibration_state after them. */
typedef struct counter_configuration_fields {
  uint8_t fpga_registers[4];   /* counter_arm copies them into fconf_regs */
  double calibration;          /* s, which counter_read subtracts */
  uint8_t registers_mode;      /* 1: load fpga_registers ... */
  uint8_t registers_state;     /* ... when this is 2 */
  uint8_t calibrate_mode;      /* 3: always calibrate; 1: ... */
  uint8_t calibrate_state;     /* ... when this is 2 */
  uint8_t sync_mode;           /* 2: counter_arm syncs ... */
  uint8_t sync_state;          /* ... when this is 2; sync_request sets both */
  counter_sync_kind sync_kind; /* sync_request's first; into fconf_regs[3] */
  uint8_t pad_13;
  int32_t sync_count;          /* sync_request's second; counted down */
  int32_t interpolator_offset; /* reading_low, subtracted from the reading */
  int32_t interpolator_scale;  /* the spread it is divided by */
} counter_configuration_fields;

/* The battery-backed RAM a warm restart restores, preserved_region at
 * 0x100000: pllp_task copies gps, pll and the 0x19 bytes from 0x101e70
 * into it each pass and checksums it, and system_init copies them back
 * when the checksum is good, RSR shows neither an external nor a
 * power-up reset, and restore is 1.  0x94e bytes, record_checksum's
 * length. */
typedef struct preserved_region_fields {
  uint16_t
      checksum; /* +0x000: record_checksum's sum and complement of the rest */
  uint16_t restore;     /* +0x002: 1, system_init restores; pllp_task sets it,
                           nv_clear_and_reset and system_init's defaults clear it */
  gps_state gps;        /* +0x004 */
  pll_state pll;        /* +0x55e */
  uint8_t values[0x19]; /* +0x934: 0x101e70 to 0x101e88 */
  uint8_t field_94d;    /* in the checksum; nothing else refers to it */
} preserved_region_fields;

/* The clock record at 0x1009b8, clock, which the clock_record_ routines
 * reach through A6 or A4: the uptime, two dates and times, each with its
 * month table, the minute's length and the GPS-UTC offset.
 * clock_record_checksum sums +0x02 to +0x32.  0x33 bytes. */
typedef struct clock_record {
  uint16_t checksum; /* +0x00: the sum's complement, then the sum */
  uint32_t uptime;   /* +0x02: seconds since the clock started; addq.l #1 at
                        0x13508, each second; cleared only by clock_init */
  time_of_day time;  /* +0x06: get_date_time's time; clock_record_derive's
                        utc_to_local of gps_time */
  uint32_t date;     /* +0x0a: get_date_time's date */
  uint8_t month_days[12]; /* +0x0e: date's days a month, January first;
                             clock_set_leap_year sets February's */
  time_of_day gps_time;   /* +0x1a: get_gps_time's */
  uint32_t gps_date; /* +0x1e: get_gps_date's; year << 16 | month << 8 | day */
  uint8_t gps_month_days[12]; /* +0x22: gps_date's */
  uint8_t minute_length; /* +0x2e: seconds in this minute, 60; 59 or 61 from the
                            leap stubs at 0x13666 and 0x1366c */
  uint8_t set; /* +0x2f: the clock has been set since power-up, from GPS or by
                  the console: set_timedate sets it (0x13870), clock_init
                  clears it (0x13974); get_date_time returns 1 while clear */
  int16_t gps_utc_offset; /* +0x30: GPS less UTC in seconds, days times 86400
                             plus the difference of two times of day, negated in
                             UTC mode; a 59- or 61-second minute or offset_step
                             changes it */
  uint8_t gps_utc_offset_valid; /* +0x32: set_gps_utc_offset sets it; clock_init
                                   clears it */
} clock_record;

/* The message pps_interrupt sends pps_exchange each 1PPS, at 0x100bac,
 * which pps_wait_time receives into the same cells.  pps_init clears
 * 0x18 bytes from it with mutex_clear. */
typedef struct pps_exchange_message {
  uint32_t field_00[2]; /* only cleared */
  uint32_t gps_time;    /* +0x08: get_gps_time's at the pulse */
  uint32_t gps_date;    /* +0x0c: get_gps_date's */
  uint32_t field_10[2]; /* only cleared */
} pps_exchange_message;

/* The GPS task's state, gps_task_state: the GPS Process Monitor's
 * "State: " strings. */
typedef enum __attribute__((packed)) gps_process_state {
  GPS_PROCESS_TASK_IDLE = 0,    /* takes the next request from gps_requests */
  GPS_PROCESS_SEND_COMMAND = 1, /* runs the script's next step */
  GPS_PROCESS_WAIT_FOR_RESPONSE =
      2 /* waits for the step's response, retrying on a timeout */
} gps_process_state;

/* satellite_sets_build's sets, which status_snapshot keeps: GPS
 * satellites as bits prn - 1 of a long, pseudolites as bits prn - 0x21
 * of a byte (satellite_add_to_set).  Tracked means on one of @@Ba's
 * channels; the three bands split those by the channel's tracking mode
 * (oncore_channel_mode), 1 to 4, 5 and 6, and 7 and 8 (0x494f6 to
 * 0x4951a), and are cut to the satellites not used (0x49590). */
typedef struct satellite_sets {
  uint32_t visible; /* +0x00: visible_satellites' set, from @@Bb */
  uint32_t
      used; /* +0x04: satellites_in_use's GPS set, those used for position */
  uint32_t tracked;            /* +0x08 */
  uint8_t pseudolites_used;    /* +0x0c: satellites_in_use's pseudolite set */
  uint8_t pseudolites_tracked; /* +0x0d */
  uint32_t unused;             /* +0x0e: (visible | tracked) & ~used */
  uint32_t tracked_unused;     /* +0x12: tracked & ~used */
  uint32_t acquiring;          /* +0x16: of tracked_unused, modes 1 to 4 */
  uint32_t synchronized;       /* +0x1a: modes 5 and 6 */
  uint32_t ephemeris;          /* +0x1e: modes 7 and 8 */
  uint8_t pseudolites_tracked_unused; /* +0x22: pseudolites_tracked &
                                         ~pseudolites_used */
  uint8_t pseudolites_acquiring;      /* +0x23: likewise, by mode */
  uint8_t pseudolites_synchronized;   /* +0x24 */
  uint8_t pseudolites_ephemeris;      /* +0x25 */
  uint8_t visible_count;              /* +0x26: visible_satellites' count */
  uint8_t used_count;                 /* +0x27: satellites_in_use's GPS count */
  uint8_t pseudolites_used_count;     /* +0x28: its pseudolite count */
  uint8_t field_29;
} satellite_sets;

/* What satellite_find and the satellite lists are given:
 * gps.visibility_received (0x100d7a) and the @@Bb message after it, as
 * the image passes its address (0x4a800 to 0x4a83a) and its search
 * reads it (0x4f32c). */
typedef struct gps_visibility_record {
  uint8_t received; /* +0x00: gps.visibility_received */
  uint8_t field_01;
  oncore_visibility_fields visibility; /* +0x02: gps.visibility */
} gps_visibility_record;

/* A satellite's cell on the screen, at +4 of a satellite_status_list
 * entry: satellite_elevation_azimuth's and satellite_status' (0x49654,
 * 0x49688), the long status_screen gives format_satellite_cell
 * (0x4ae0e). */
typedef struct satellite_cell {
  satellite_cell_status status;
  uint8_t elevation; /* degrees, for SATELLITE_CELL_POSITION */
  uint16_t azimuth;  /* degrees */
} satellite_cell;

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
  uint8_t holdover_waiting_reason;      /* +0x01c */
  uint8_t holdover_limit_state;         /* +0x01d: nv_settings' */
  float holdover_limit_threshold;       /* +0x01e: nv_settings' */
  uint32_t holdover_duration;           /* +0x022: pll's */
  float kenneth_present;                /* +0x026: kenneth_present_get's */
  uint8_t kenneth_present_valid;        /* +0x02a: its result */
  uint8_t field_02b;                    /* its second output */
  float kenneth_predicted;              /* +0x02c: kenneth_predicted_get's */
  uint8_t kenneth_predicted_valid;      /* +0x030: its result */
  uint8_t field_031;                    /* its second output */
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
                           get_date_time's while clock_invalid (0x4a74e) */
  uint32_t date;        /* +0x0fc: the same's date */
  int32_t time_zone[2]; /* +0x100: nv_settings' */
  uint8_t
      clock_invalid; /* +0x108: 1 when get_date_time reports no date or time */
  uint8_t utc_time;  /* +0x109: gps' */
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

/* An entry of status_screen_mode_lines: a mode line of the status
 * screen and the mode it stands for, which status_screen marks ">>"
 * when it is the snapshot's.  A null text ends the table. */
typedef struct status_screen_mode_line {
  status_screen_line
      mode; /* +0x00: 3 Locked, 2 Recovery, 1 Holdover, 0 Power-up */
  uint8_t field_01;
  char* text; /* +0x02 */
} status_screen_mode_line;
/* One entry of the kernel's I/O table, a "jmp.l routine" instruction:
 * kernel_dispatch_io jumps into the table at (device * 6 + function) * 6
 * (docs/firmware/psos.md, "Calls"). */
typedef struct io_jump {
  uint16_t opcode; /* +0x00: 0x4ef9, jmp.l */
  void* routine;   /* +0x02 */
} io_jump;

/* A device's six I/O routines, functions 0 to 5 in the order of the
 * console's dev_init, dev_open, dev_close, dev_read, dev_write and
 * dev_ctrl.  io_table at 0x1025bc holds four, which psos_config_init
 * copies from ROM 0x5171a: DUART channel B, the QSPI, the console's
 * string device and the SCI.  The installer's, at 0x10ab60, holds one. */
typedef struct io_device_jumps {
  io_jump function[6]; /* +0x00 */
} io_device_jumps;

/* The pSOS kernel's records and codes (docs/firmware/psos.md, "Inside
 * the kernel"). */
/* A task record's status byte, +0x13, by task_status_name's text. */
typedef enum __attribute__((packed)) psos_task_status {
  PSOS_TASK_SUSPENDED = 0x04,     /* "susp" */
  PSOS_TASK_PAUSED = 0x08,        /* "paus", as a nonzero delay is */
  PSOS_TASK_VARIABLE_WAIT = 0x10, /* "vblk": in wait_v */
  PSOS_TASK_EXCHANGE_WAIT = 0x20, /* "xblk": in request_x */
  PSOS_TASK_MEMORY_WAIT = 0x40,   /* "mblk": on a region's queue, presumably */
  PSOS_TASK_SPAWNING = 0x80,      /* "spwn": created, not started */
} psos_task_status;

/* A task record's mode byte, +0x11 (psos.md, service 32). */
typedef enum __attribute__((packed)) psos_task_mode {
  PSOS_MODE_TIME_SLICE = 0x40,    /* the report's "R" */
  PSOS_MODE_NO_PREEMPTION = 0x80, /* the report's "*" */
} psos_task_mode;

/* task_table_entry.state, by which os_report_processes groups the
 * tasks; PSOS_TABLE_EMPTY marks an unused entry. */
typedef enum __attribute__((packed)) psos_table_state {
  PSOS_TABLE_READY = 0,
  PSOS_TABLE_PAUSED = 1,
  PSOS_TABLE_BLOCKED = 2,
  PSOS_TABLE_EMPTY = 0xff,
} psos_table_state;

/* The kernel's services, trap #3's D7 (psos.md, "Services"); 8 and 21
 * are named by the pSOS-68K brochure's list there. */
typedef enum __attribute__((packed)) psos_service {
  PSOS_SERVICE_START = 2,
  PSOS_SERVICE_IDENT = 3,
  PSOS_SERVICE_DELETE = 4,
  PSOS_SERVICE_SUSPEND = 5,
  PSOS_SERVICE_RESUME = 6,
  PSOS_SERVICE_PRIORITY = 7,
  PSOS_SERVICE_ALLOCATE_SEGMENT = 8,
  PSOS_SERVICE_FREE_SEGMENT = 9,
  PSOS_SERVICE_ASSIGN_SEGMENT = 10,
  PSOS_SERVICE_RECLAIM_SEGMENT = 16,
  PSOS_SERVICE_BROADCAST_X = 18,
  PSOS_SERVICE_JAM_X = 19,
  PSOS_SERVICE_CREATE_X = 20,
  PSOS_SERVICE_ATTACH_X = 21,
  PSOS_SERVICE_DELETE_X = 22,
  PSOS_SERVICE_SEND_X = 23,
  PSOS_SERVICE_REQUEST_X = 24,
  PSOS_SERVICE_SIGNAL_V = 25,
  PSOS_SERVICE_WAIT_V = 26,
  PSOS_SERVICE_GET_V = 27,
  PSOS_SERVICE_PAUSE = 28,
  PSOS_SERVICE_ANNOUNCE_TICK = 29,
  PSOS_SERVICE_GET_TIME = 30,
  PSOS_SERVICE_SET_TIME = 31,
  PSOS_SERVICE_MODE = 32,
  PSOS_SERVICE_CREATE_PARTITION = 35,
  PSOS_SERVICE_DELETE_PARTITION = 36,
  PSOS_SERVICE_GET_BLOCK = 37,
  PSOS_SERVICE_RETURN_BLOCK = 38,
} psos_service;

/* The I/O functions, trap #4's D7 (psos.md, "Calls"). */
typedef enum __attribute__((packed)) psos_io_function {
  PSOS_IO_INIT = 0,
  PSOS_IO_OPEN = 1,
  PSOS_IO_READ = 3,
  PSOS_IO_WRITE = 4,
} psos_io_function;

typedef struct psos_task psos_task;

/* A segment of a region, on a task's list or a region's free list:
 * os_report_region_line prints its first five fields. */
typedef struct psos_segment {
  struct psos_segment* next; /* +0x00 */
  uint32_t field_04;
  uint32_t field_08;
  uint32_t size;  /* +0x0c: bytes, from the segment's start */
  uint16_t flags; /* +0x10: the region, 1 or 2, in bits 1 to 0 */
} psos_segment;

/* One of a task's resources, on its list at +0x1c. */
typedef struct psos_resource {
  struct psos_resource* next; /* +0x00 */
  void* object;               /* +0x04: an exchange; object_name names it */
} psos_resource;

/* A task's record, 0xb2 bytes, which kernel_init (0x15cd8) carves from
 * the data area; its address is its id. */
struct psos_task {
  psos_task* next; /* +0x00: the next on the ready queue, or on the circular
                    queue of an exchange or region it waits at */
  uint32_t field_04;
  psos_task* timer_next; /* +0x08: the next on the delay list */
  uint32_t field_0c;
  uint8_t priority;        /* +0x10 */
  psos_task_mode mode;     /* +0x11: service 32's mode byte */
  uint8_t delay;           /* +0x12: nonzero while paused */
  psos_task_status status; /* +0x13: 0 when ready */
  uint8_t group;           /* +0x14 */
  uint8_t field_15;
  uint16_t events_waited;   /* +0x16 */
  uint16_t events_pending;  /* +0x18 */
  uint16_t slice_ticks;     /* +0x1a: the report's TIX for a task not paused */
  psos_resource* resources; /* +0x1c */
  psos_segment* segments;   /* +0x20: the segments it holds */
  char name[4];             /* +0x24 */
  uint32_t id_check;        /* +0x28: the id with its low word complemented */
  uint32_t field_2c;
  psos_task* list_next; /* +0x30: the next on the kernel's task list or its
                         free list */
  uint16_t field_34;
  uint32_t supervisor_pointer; /* +0x36: the saved SSP */
  uint32_t user_pointer;       /* +0x3a: the saved USP */
  uint8_t field_3e[0xc];
  int32_t timer_ticks; /* +0x4a: its ticks past the delay list's one before */
  uint32_t field_4e;
  uint32_t region_request; /* +0x52: the bytes a region waiter asks */
  uint32_t region_wait;    /* +0x56: bits 3 to 0, its region less 1 */
  uint8_t field_5a[0x28];
  void* exception_hook; /* +0x82: exception_hook_get's, per task */
  uint32_t field_86;
  uint32_t user_top;       /* +0x8a: the user stack's top */
  uint32_t supervisor_top; /* +0x8e: the supervisor stack's top; the user
                              stack starts 4 above it */
  uint32_t stack_bottom;   /* +0x92: the supervisor stack's bottom */
  uint8_t field_96[0x18];
  uint32_t ticks; /* +0xae: ticks run since the last process report */
};

/* An exchange's record, 0x22 bytes; its address is its id. */
typedef struct psos_exchange {
  psos_task* waiting; /* +0x00: the circular queue of tasks waiting, which
                       ends at the exchange itself */
  uint8_t field_04[0xc];
  uint8_t by_priority; /* +0x10: queues by priority, else first in first out */
  uint8_t group_only;  /* +0x11: only its group may use it, else any */
  uint8_t limited;     /* +0x12: queue_limit holds, else no limit */
  uint8_t field_13;
  char name[4];               /* +0x14 */
  uint32_t id_check;          /* +0x18: the id with its words swapped */
  struct psos_exchange* next; /* +0x1c: the next on the kernel's list */
  uint8_t queue_limit;        /* +0x20 */
  uint8_t queued;             /* +0x21: messages queued */
} psos_exchange;

/* A message buffer, 0x18 bytes, on the free list by its first long. */
typedef struct psos_message {
  struct psos_message* next; /* +0x00 */
} psos_message;

/* The kernel's data area, psos_config.data_area, which it keeps in A6;
 * the regions' arrays are region 1's and region 2's. */
typedef struct psos_kernel {
  psos_task* running;            /* +0x00 */
  psos_task* ready;              /* +0x04: the ready queue, by priority */
  psos_task* timers;             /* +0x08: the delay list, through timer_next */
  psos_task* tasks;              /* +0x0c: every task, through list_next */
  psos_task* free_tasks;         /* +0x10 */
  psos_exchange* exchanges;      /* +0x14 */
  psos_exchange* free_exchanges; /* +0x18 */
  psos_message* free_messages;   /* +0x1c */
  uint8_t field_20[0x10];
  void* debug_hooks;            /* +0x30: psos_debug_output's table or 0 */
  uint32_t region_minimum[2];   /* +0x34: os_report_regions' "min" */
  uint32_t region_maximum[2];   /* +0x3c: "max" */
  uint32_t region_pending[2];   /* +0x44: "pend" */
  uint32_t region_end[2];       /* +0x4c */
  psos_segment* region_free[2]; /* +0x54 */
  uint8_t field_5c[8];
  psos_task* region_waiting; /* +0x64: the circular queue of tasks waiting for a
                              region, which ends at this field */
} psos_kernel;

/* The kernel's configuration table, psos_config at 0x10264c, which
 * psos_config_init copies from ROM 0x5122a; the installer's at 0x10ab84.
 * The kernel reaches it through the pointer at 0x51226, which the long
 * at 0x14ac2 holds, and keeps data_area in A6.  kernel_init carves the
 * data area into the supervisor stack and the records, and gives
 * region 1 the rest up to data_end. */
typedef struct psos_configuration {
  struct psos_kernel* data_area; /* +0x00: the kernel's state, A6; the
                                    supervisor stack starts
                      200 bytes in */
  uint32_t data_end; /* +0x04: region 1's end; kernel_init fails unless its
                        records fit below it; task_id_valid and
                        exchange_id_valid take ids from data_area up to it */
  uint32_t region_1_minimum; /* +0x08: at least 18; os_report_regions' "min" */
  uint32_t region_2_start;   /* +0x0c: 0 */
  uint32_t region_2_end; /* +0x10: 0; under 72 bytes, kernel_init leaves region
                            2 empty */
  uint32_t region_2_minimum; /* +0x14 */
  uint16_t task_records;     /* +0x18: kernel_init makes one more 0xb2-byte task
                                record than this */
  uint16_t exchange_records; /* +0x1a: 0x22 bytes each */
  uint16_t
      message_buffers; /* +0x1c: 0x18 bytes each; os_report_exchange_summary's
                          "Msg buffer(s)" total */
  uint32_t field_1e; /* +0x1e: kernel_init copies it to the data area's +0x86 */
  uint16_t last_device; /* +0x22: kernel_init runs function 0 of each device up
                           to it; kernel_dispatch_io refuses a higher one */
  io_device_jumps* io_table;      /* +0x24 */
  uint32_t supervisor_stack_size; /* +0x28: ">> pSOS system supervisor stack:"
                                     divides by it */
  void* root_entry; /* +0x2c: where kernel_init starts ROOT, root_task */
  uint16_t
      root_stack_sizes[2]; /* +0x30: ROOT's two sizes, kernel_create's D3 */
  uint16_t root_priority; /* +0x34: ROOT's priority, kernel_create's D2; IDLE is
                             given 1 */
  void* start_hook;  /* +0x36: kernel_start calls it with the started task's
                        record in A0: stack_fill */
  void* delete_hook; /* +0x3a: kernel_delete calls it while nonzero; 0 */
  uint32_t field_3e; /* +0x3e: kernel_init copies it to the data area's +0xb4 */
  void** debug_hooks; /* +0x42: a cell holding a hook table's address;
                         psos_debug_output puts the table at the data area's
                         +0x30, whose +4 kernel_dispatch_service and
                         kernel_dispatch_io call */
  void** task_hooks; /* +0x46: a cell holding a hook table, whose +4 kernel_init
                        calls and +8 kernel_delete; 0 */
  uint8_t field_4a[24];
} psos_configuration;

/* One of the 17 entries of task_table at 0x1026b6, which
 * task_table_build fills from the running task and the kernel's task
 * list, the running task first. */
typedef struct task_table_entry {
  psos_table_state state; /* +0x00: ready when the record's status is clear;
                             paused when its delay is set or it has
                             PSOS_TASK_PAUSED, as timer_ticks_until counts;
                             blocked otherwise.  os_report_processes lists
                             by it */
  uint8_t field_01;
  struct psos_task* task; /* +0x02: the task's record */
} task_table_entry;

/* A request on the spoo exchange, which spoo_run builds on its stack and
 * spoo_task receives at 0x103702: a function and its three arguments. */
typedef struct spoo_request {
  uint32_t field_00;       /* +0x00: spoo_run does not set it */
  uint32_t field_04;       /* +0x04: spoo_run clears it */
  spoo_function* function; /* +0x08 */
  uint32_t arguments[3];   /* +0x0c */
} spoo_request;

/* A buffer of the status events reported before the status model is
 * ready: early_status_reports at 0x102ebc, status_report's, and
 * early_status_happenings at 0x102ec4, status_happening's.  They pass
 * it to status_event_hook, which does nothing, so nothing fills them;
 * happening_flush replays one as a ring of five. */
typedef struct early_status_events {
  int8_t count; /* +0x00: the events reported; above 5 the ring has wrapped */
  uint8_t next; /* +0x01: where the next goes, the oldest once wrapped */
  int8_t events[5]; /* +0x02 */
  uint8_t field_07;
} early_status_events;

/* The boot code's self-test results, boot_selftest at 0x10ab20: a
 * checksum word, then the fields of selftest_results' first 14 bytes in
 * their order.  The installer's :DIAGnostic:TEST? reads them. */
typedef struct boot_selftest_results {
  int16_t
      checksum; /* +0x00: boot_reset's byte-lane sums: 2 and 1 for the two lanes
                   of 0x10000 to 0x3ffff, 8 and 4 for 0x40000 to 0x7ffff */
  int16_t cpu;  /* +0x02: 1 if the CPU test fails */
  uint16_t
      ram; /* +0x04: bit 0 a pattern failed, bit 1 an address line aliases */
  uint16_t ram_data_bits;     /* +0x06: the bits that failed a pattern */
  uint32_t ram_address_lines; /* +0x08: the address lines that alias */
  int16_t uart;      /* +0x0c: boot_duart_early_init's IVR and loopback checks;
                        lights OP6 */
  uint8_t lamp_mask; /* +0x0e: the DUART output bits boot_reset resets; nothing
                        reads it */
  uint8_t field_0f;
} boot_selftest_results;
