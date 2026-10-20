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

/* A numeric SCPI parameter's descriptor, in ROM; its small tables sit
 * just before it.  0x2c bytes. */
typedef struct scpi_numeric_param {
	int16_t field_00;
	void *table_02;
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
	uint8_t mode;   /* 0 code search .. 8 available for position */
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
	uint8_t dop_type;   /* 0 PDOP, 1 HDOP */
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
	uint8_t health;    /* 0 healthy .. 3 unhealthy and removed */
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
	uint8_t type; /* 0 GDOP .. 4 TDOP */
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
	uint16_t alarm_limit;    /* 100 ns */
	uint8_t pps_mode;
	uint8_t pad_06;
	int32_t pps_rate;
	uint8_t next_month;      /* the time to the next pulse */
	uint8_t next_day;
	uint16_t next_year;
	uint8_t pad_0f;
	uint8_t next_hour;
	uint8_t next_minute;
	uint8_t next_second;
	uint8_t pulse_status;
	uint8_t pulse_sync;      /* 0 UTC, 1 GPS */
	uint8_t solution_status; /* 0 OK, 1 alarm, 2 unknown */
	uint8_t raim_status;
	uint16_t sigma;          /* ns */
	int8_t sawtooth;         /* ns, of the next pulse */
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
	uint8_t xdop_type;                         /* @@Aj */
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
	uint8_t utc_time;                          /* @@Aw: 0 GPS, 1 UTC */
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
	uint8_t leap_second_pending;               /* @@Bj */
	uint8_t field_33b;
	uint16_t self_test;                        /* @@Ca, failed tests */
	uint8_t field_33e[0x344 - 0x33e];
	oncore_extension_fields extension;         /* @@Bk */
	uint8_t field_382[0x3ec - 0x382];
	uint8_t fix_mode;                          /* @@Cg: 0 idle, 1 fix */
	uint8_t field_3ed;
	uint8_t receiver_id_received;              /* set by @@Cj's decoder */
	oncore_receiver_id receiver_id;            /* @@Cj */
	uint8_t field_504[2];
	uint32_t average_count;                    /* positions averaged */
	double average_latitude; /* of @@Ba's, while surveying */
	double average_longitude;
	double average_height_gps;
	double average_height_msl;
	uint8_t field_52a[0x55a - 0x52a];
} gps_state;

/* The disciplining loop's state block: the stage the status screen
 * shows (docs/screen-format-strings.md, "Mode suffixes") and the PLL's
 * values.  Cleared as one, memset(&pll, 0, 0x3d6). */
typedef struct pll_state {
	uint8_t stage;          /* 1 powerup .. 6 normal pll */
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
