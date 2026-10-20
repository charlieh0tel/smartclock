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

/* The firmware's coded values.  Packed, so that gcc makes each the
 * smallest integer that holds its values, a byte for all of these;
 * SmartClockSetup.java sizes them so in Ghidra, whose C parser makes
 * every enum 4 bytes. */

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
	SATELLITE_LIST_TRACKING = 0,            /* :SAT:TRAC?, six channels */
	SATELLITE_LIST_TRACKING_PSEUDOLITES = 1,
	SATELLITE_LIST_VISIBLE = 2,             /* :SAT:VIS:PRED?, twelve */
	SATELLITE_LIST_ALL = 3                  /* the ignore and include lists */
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
typedef struct scpi_numeric_param {
	uint8_t field_00;
	uint8_t last_index; /* +0x01: values less one */
	int8_t *nv_widths;  /* +0x02: each value's bits in the NV register; negative, signed */
	void *minimum;      /* +0x06: the least value accepted */
	void *maximum;      /* +0x0a: the greatest */
	void *convert;      /* +0x0e: called by scpi_numeric_get */
	void *field_12;
	void *value;        /* +0x16: the current value, in RAM */
	void *field_1a;
	int16_t field_1e;
	int16_t field_20;
	void *field_22;
	uint8_t is_integer; /* +0x26: the value is an int, not a float */
	uint8_t field_27;
	void *table_28;
} scpi_numeric_param;

/* One status register of the SCPI status tree.  16 bytes. */
typedef struct status_register {
	int16_t field_00;
	int16_t field_02;
	int16_t field_04;
	int16_t field_06;
	int16_t field_08;
	struct status_register *parent; /* +0x0a: the register it reports into */
	int16_t parent_bit;             /* +0x0e: at this bit */
} status_register;

/* An entry of the status conditions table: which register and bit a
 * condition sets, and how.  6 bytes. */
typedef struct status_condition {
	int16_t kind;
	int16_t register_index;
	int16_t bit;
} status_condition;

/* The SCPI status system: status_create builds it, status_object points
 * at it. */
typedef struct status_model {
	int16_t register_count;        /* +0x00: 7 */
	void *output;                  /* +0x02: set for the top register */
	void *queue;                   /* +0x06: built with room for 30 */
	status_condition *conditions;  /* +0x0a */
	int32_t field_0e;
	status_register *registers[7]; /* +0x12 */
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
	uint8_t sign;   /* 0 positive, 0xff negative */
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
	int16_t magnetic_variation;        /* degrees, east positive */
	int16_t velocity_north;            /* m/s */
	int16_t velocity_east;
	int16_t velocity_up;
	uint16_t correction_age;           /* s, of differential corrections */
	int32_t ecef_x;                    /* cm */
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
	uint8_t field_002;
	uint8_t field_003;
	uint16_t sample_count; /* +0x004: samples toward this entry, to 2700 */
	int16_t field_006;
	uint16_t summed_count; /* +0x008: of those, summed */
	float averages[64];    /* +0x00a: e_avg, efc_sum / summed_count */
	uint8_t weights[64];   /* +0x10a: weight, 1 to 3 */
	float times[64];       /* +0x14a: time */
	float logs[64];        /* +0x24a: log */
	float efc_sum; /* +0x34a: the EFC, less its temperature correction, summed */
	int32_t field_34e;
	float drift; /* +0x352: pll_rep's current drift, times 5.4e-8 per day */
	int32_t field_356;
	float field_35a;
	int32_t field_35e;
	int32_t field_362;
} efc_ring;

/* @@Ba, the position/status/data message: one channel's status. */
typedef struct oncore_channel_status {
	uint8_t satellite;
	oncore_channel_mode mode;
	uint8_t signal_strength;
	uint8_t status; /* bit 7 used for the fix */
} oncore_channel_status;

/* @@Ba, as its decoder's table (0x50fd2) lays it out: a pad byte
 * before the hour, and another before the DOP type, put the longs and
 * words on even addresses. */
typedef struct oncore_status_fields {
	uint8_t month;
	uint8_t day;
	uint16_t year;
	uint8_t pad_04;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint32_t fraction;  /* ns */
	int32_t latitude;   /* mas */
	int32_t longitude;
	int32_t height_gps; /* cm */
	int32_t height_msl;
	uint16_t velocity;  /* cm/s */
	uint16_t heading;   /* tenths of a degree */
	uint16_t dop;       /* tenths */
	uint8_t pad_22;
	oncore_dop_type dop_type;
	uint8_t visible_count;
	uint8_t tracked_count;
	oncore_channel_status channels[6];
	uint8_t receiver_status;
} oncore_status_fields;

/* @@Bb: one visible satellite, padded by the decoder's table (0x50fd8)
 * from 7 bytes to 10. */
typedef struct oncore_visible_satellite {
	uint8_t satellite;
	uint8_t pad_1;
	int16_t doppler;   /* Hz */
	uint8_t elevation; /* degrees */
	uint8_t pad_5;
	uint16_t azimuth;  /* degrees */
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
	uint8_t next_month;   /* the time to the next pulse */
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
	uint16_t sigma;       /* ns */
	int8_t sawtooth;      /* ns, of the next pulse */
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
	oncore_time_fields time;                   /* @@Aa */
	oncore_gmt_offset_fields gmt_offset;       /* @@Ab */
	uint8_t field_007;
	oncore_date_fields date;                   /* @@Ac */
	oncore_position_fields position;           /* @@Ad, @@Ae, @@Af */
	uint8_t mask_angle;                        /* @@Ag */
	uint8_t satellite_select;                  /* @@Ah */
	uint8_t manual_satellites[6];              /* @@Ai, by channel */
	oncore_xdop_type xdop_type;                /* @@Aj */
	uint8_t field_025;
	uint16_t xdop_hysteresis;                  /* @@Ak */
	uint16_t xdop_threshold;                   /* @@Al, 3D to 2D */
	uint16_t hdop_threshold;                   /* @@AC, 2D to 0D */
	uint32_t ignored_satellites;               /* @@Am */
	uint8_t field_030[0x8e - 0x30];
	uint8_t almanac_update;                    /* @@An */
	uint8_t field_08f[0xa4 - 0x8f];
	uint8_t ionospheric_correction;            /* @@Aq */
	uint8_t fix_algorithm;                     /* @@Ar */
	oncore_hold_position_fields hold_position; /* @@As */
	uint8_t field_0b2[2];
	uint8_t position_hold;                     /* @@At */
	uint8_t field_0b5[0xbb - 0xb5];
	uint8_t altitude_hold;                     /* @@Av */
	oncore_time_mode utc_time;                 /* @@Aw */
	uint8_t field_0bd;
	int16_t epoch_offset;                      /* @@Ax, ms */
	int32_t pps_offset;                        /* @@Ay */
	int32_t pps_cable_delay;                   /* @@Az */
	uint8_t field_0c8;
	uint8_t application_type;                  /* @@AB */
	uint8_t position_fix_threshold;            /* @@AD, s */
	uint8_t differential_output_threshold;     /* @@AD, s */
	uint8_t field_0cc[2];
	oncore_status_fields status;               /* @@Ba */
	uint8_t field_10d[3];
	uint8_t visibility_received;               /* set by @@Bb's decoder */
	uint8_t field_111;
	oncore_visibility_fields visibility;       /* @@Bb */
	uint8_t field_18c;
	oncore_xdop_table_fields xdop_table;       /* @@Bc */
	uint8_t field_1d8[2];
	oncore_almanac_status_fields almanac;      /* @@Bd */
	uint8_t field_1ea[0x2a8 - 0x1ea];
	oncore_corrections_fields corrections;     /* @@Ce */
	double correction_average;                 /* m, printed AVG */
	double correction_deviation;               /* m, printed STD DEV */
	uint8_t field_2f8[0x2fb - 0x2f8];
	oncore_raim_fields raim;                   /* @@Bn */
	oncore_leap_pending leap_second_pending;   /* @@Bj */
	uint8_t field_33b;
	uint16_t self_test;                        /* @@Ca, failed tests */
	uint8_t field_33e[0x344 - 0x33e];
	oncore_extension_fields extension;         /* @@Bk */
	uint8_t field_382[0x3ec - 0x382];
	oncore_fix_mode fix_mode;                  /* @@Cg */
	uint8_t field_3ed;
	uint8_t receiver_id_received;              /* set by @@Cj's decoder */
	oncore_receiver_id receiver_id;            /* @@Cj */
	uint8_t field_504[2];
	uint32_t average_count;                    /* positions averaged */
	double average_latitude; /* of @@Ba's, while surveying */
	double average_longitude;
	double average_height_gps;
	double average_height_msl;
	uint8_t field_52a[0x558 - 0x52a];
	uint8_t raim_option; /* its options list has an I */
	uint8_t field_559;
} gps_state;

/* The disciplining loop's state block: the stage the status screen
 * shows (docs/screen-format-strings.md, "Mode suffixes") and the PLL's
 * values.  Cleared as one, memset(&pll, 0, 0x3d6). */
typedef struct pll_state {
	pll_stage stage;
	uint8_t field_001[0x12 - 0x01];
	uint8_t manual;         /* +0x12 */
	uint8_t field_013[2];
	uint8_t recovery_state; /* +0x15 */
	uint8_t field_016;
	uint8_t fine_state;     /* +0x17 */
	uint8_t field_018;
	uint8_t fine_index;     /* fine acquisition's "i= %d" */
	uint8_t field_01a[2];
	float fine_d;           /* fine acquisition's "d = %f" */
	uint8_t field_020[0x32 - 0x20];
	float efc;              /* pr_efc's efc */
	float tfom;             /* pll_rep's tfom, s */
	uint8_t field_03a[0x4a - 0x3a];
	efc_ring efc_history;
	uint8_t field_3b0[0x3d6 - 0x3b0];
} pll_state;

/* The GPS link's counters, which the "GPS Process Monitor" page
 * (gps_print_process_monitor) prints.  Cleared as one, memset(&gps_counts,
 * 0, 0x36).  All words. */
typedef struct gps_statistics {
	uint16_t received;         /* Msgs Rcvd */
	uint16_t sent;             /* Msgs Sent */
	uint16_t jammed;           /* Msgs Jam */
	uint16_t sent_errors;
	uint16_t received_errors;
	uint16_t jam_errors;
	uint16_t successes;        /* Success */
	uint16_t field_0e;
	uint16_t rejects;          /* Reject */
	uint16_t timeouts;         /* Timeout */
	uint16_t busy;             /* Busy */
	uint16_t aborts;           /* Abort */
	uint16_t no_command;       /* No Cmd */
	uint16_t no_query;         /* No Qry */
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

/* A clock (klok task) entry, which print_clk_list prints: when it fires,
 * its period, the function it calls and its event class.  A date is
 * year << 16 | month << 8 | day, a time hour << 16 | minute << 8 |
 * second.  0x1a bytes; a pool of five. */
typedef struct klok_entry {
	struct klok_entry *next;
	uint32_t time;        /* at */
	uint32_t date;
	uint32_t period_time; /* period */
	uint32_t period_date;
	void *function;       /* function address */
	int16_t event_class;  /* event class */
} klok_entry;

/* An event log entry: its time and date, packed as a klok_entry's, and
 * the format and two arguments it is printed with.  log_read formats
 * one as "Log %03d:%04d%02d%02d.%02d:%02d:%02d" and its text. */
typedef struct log_entry {
	uint32_t time;
	uint32_t date;
	char *format;
	int32_t argument_1;
	int32_t argument_2;
} log_entry;

/* A page of the event log, three entries, as log_read loads it. */
typedef struct log_page {
	uint32_t field_00;
	log_entry entries[3];
} log_page;

/* An entry of fpga_bit_table: the FPGA register a bit is in and its
 * mask, which fpga_set_bit sets or clears in fconf_regs. */
typedef struct fpga_bit {
	uint8_t reg;
	uint8_t mask;
	uint8_t field_2;
	uint8_t field_3;
} fpga_bit;

/* The time-interval counter's interpolator calibration, which
 * interpolator_calibrate steps: HDAC channels 2 and 3 trim the counter
 * until its readings at the two reference points fall in 1 to 25 and
 * 201 to 225, exactly 200 apart. */
typedef struct interpolator_calibration {
	uint8_t initialized;   /* set with both DACs at 0x1f the first time */
	uint8_t calibrated;
	uint8_t hdac_2;        /* HDAC channel 2's value */
	uint8_t hdac_3;        /* HDAC channel 3's value */
	uint8_t result_valid;
	uint8_t result_hdac_2; /* the values that gave a spread of 200 */
	uint8_t result_hdac_3;
	uint8_t reading_low;   /* interpolator_measure(0), wanted 1 to 25 */
	uint8_t reading_high;  /* interpolator_measure(1), wanted 201 to 225 */
} interpolator_calibration;

/* A SCPI setting's descriptor, in ROM, the switches and choices nv_save
 * saves: 0x1e bytes. */
typedef struct scpi_setting_param {
	uint8_t field_00;
	uint8_t last_index; /* +0x01: values less one */
	int8_t *nv_widths;  /* +0x02: each value's bits in the NV register */
	uint16_t field_06;
	void *field_08;
	uint8_t *value;     /* +0x0c: the current value, a byte each */
	void *field_10;
	int16_t field_14;
	int16_t field_16;
	void *field_18;
	uint8_t field_1c;
	uint8_t field_1d;
} scpi_setting_param;

/* The settings nv_save keeps in the NV register and nv_restore loads:
 * each field the value of the descriptor in nv_save's tables that points
 * at it, named by the SCPI command whose setter is given that descriptor.
 * 0xc5 bytes. */
typedef struct nv_settings_fields {
	uint8_t field_00[2];
	int32_t elevation_mask;                 /* :GPS:EMANgle */
	int32_t antenna_delay;                  /* :GPS:ADELay */
	int32_t douglas_duration;               /* :DOUGlas:DURation */
	uint8_t douglas_duration_infinite;      /* :DOUGlas:DURation INFinity */
	uint8_t field_0f;
	int32_t holdover_status_threshold;      /* :ROSC:HOLD:DUR:STAT:THR */
	float limit_threshold;                  /* :ROSC:LIMit:THReshold */
	float holdover_limit_threshold;         /* :ROSC:HOLD:LIMit:THReshold */
	uint8_t holdover_limit_state;           /* :ROSC:HOLD:LIMit:STATe */
	uint8_t douglas_state;                  /* :DOUGlas:STATe */
	uint8_t field_1e;                       /* saved; no setter */
	uint8_t field_1f;
	int32_t position[3];                    /* :GPS:POSition, mas, mas, cm */
	uint32_t ignored_satellites;            /* :GPS:SAT:TRAC:IGNore */
	int32_t time_zone[2];                   /* :PTIMe:TZONe, hours, minutes */
	int32_t prediction_duration;            /* :KENneth:PREDicted:DURation */
	uint32_t leap_date;                     /* a packed date */
	int32_t leap_minute_length;             /* 59 for a second removed */
	uint8_t leap_pending;
	uint8_t raim_state[2];                  /* :DIAG:GPS:POS:HOLD:RAIM:STATe */
	uint8_t field_47;
	int32_t raim_threshold[2];              /* :DIAG:GPS:POS:HOLD:RAIM:THR */
	int32_t holdover_measurement_threshold; /* :ROSC:HOLD:DUR:MEAS:THR */
	uint32_t status_enable[7];              /* *ESE, *SRE, :STATus ENABle */
	uint32_t status_positive_transition[5]; /* :STATus PTRansition */
	uint32_t status_negative_transition[5]; /* :STATus NTRansition */
	uint8_t questionable_user;              /* :STAT:QUES:...:USER */
	uint8_t serial_restricted;              /* :DIAG:SER1:RESTricted */
	uint8_t pulse_state;                    /* :PULSe:CONTinuous:STATe */
	uint8_t field_9b;
	int32_t pulse_period;                   /* :PULSe:CONTinuous:PERiod */
	int32_t pulse_start_time[3];            /* :PULSe:STARt:TIME */
	int32_t pulse_start_date[3];            /* :PULSe:STARt:DATE */
	int32_t field_b8;                       /* saved, 16 bits; no setter */
	uint8_t field_bc;                       /* saved; no setter */
	uint8_t output_state;                   /* :OUTPut:STATe */
	uint8_t format_data;                    /* :FORMat:DATA */
	uint8_t memory_save;                    /* :ROBin:MEMory:SAVE */
	uint8_t douglas_state_powerup;          /* :DOUGlas:STATe:POWerup */
	uint8_t tcode_format;                   /* :PTIMe:TCODe:FORMat */
	uint8_t gps_utc;                        /* :DIAG:GPS:UTC */
	uint8_t pforth_at_startup;              /* :DIAG:SYSTem:PSTartup */
	uint8_t debug_output;                   /* :DIAG:SYSTem:DOUTput */
} nv_settings_fields;

/* A time-interval reading, interval_reading, as counter_read leaves it.
 * 13 bytes. */
typedef struct interval_reading_fields {
	double value;         /* s; SCPI's NaN, 9.91e37, on an error */
	uint8_t valid;        /* the :DIAG query's flag */
	counter_error errors;
	uint16_t tag;         /* 0xfff6, which the :DIAG query returns with it */
	uint8_t armed;        /* clear, the routines report a measurement aborted */
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
	uint8_t field_10;
	uint8_t field_11;
	uint8_t field_12;
	uint8_t field_13;
	int32_t field_14;            /* counted down while field_10 and _11 are 2 */
	int32_t interpolator_offset; /* reading_low, subtracted from the reading */
	int32_t interpolator_scale;  /* the spread it is divided by */
} counter_configuration_fields;

