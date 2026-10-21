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

/* Why the loop holds over, pll.hold_causes, a set of these bits: the
 * console's pr_hold_cause prints each as "Holdover due to ..."
 * (docs/firmware/loop.md).  enter_holdover logs a code for each, and
 * holdover_waiting_reason answers :HOLDover:WAITing? with one. */
typedef enum __attribute__((packed)) pll_hold_cause {
	HOLD_CAUSE_MANUAL = 0x01,
	HOLD_CAUSE_GPS = 0x02,
	HOLD_CAUSE_TI_ERROR = 0x04,
	HOLD_CAUSE_TI_LIMIT_EXCEEDED = 0x08,
	HOLD_CAUSE_HARDWARE_FAILURE = 0x10,
	HOLD_CAUSE_TEMPORARY_MODE = 0x20,
	HOLD_CAUSE_1PPS_SELECTION = 0x40,
	HOLD_CAUSE_GPS_COMMAND_MONITOR = 0x80 /* set in holdover recovery while the GPS command monitor has failed; logged as plain "Holdover started" */
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

/* A SCPI parameter descriptor's structure, its first byte, which the
 * pllc task's finish_scpi_cmd switches on; it accepts 2 and 3 and does
 * nothing with them. */
typedef enum __attribute__((packed)) scpi_param_type {
	SCPI_PARAM_NUMERIC = 0, /* scpi_numeric_param */
	SCPI_PARAM_SETTING = 1  /* scpi_setting_param */
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
typedef long scpi_validate_function(void *value, int index, void *minimum, void *maximum,
	void *step, int source);

/* printf's shape, for the routines that take one to print with. */
typedef int printf_function(char *format, ...);

/* qsort's comparator. */
typedef int qsort_compare_function(void *a, void *b);

/* The holdover stage's steering, at 0x102042: each 10-second block's
 * mean time interval, and the accumulators holdover_ti_add keeps of the
 * readings for a least-squares slope, which holdover_correction_valid
 * turns into an EFC correction. */
typedef struct holdover_steering_state {
	float ti_mean;           /* +0x00: the block's valid readings' sum over 10, scaled up at its end if some failed */
	uint8_t readings;        /* +0x04: the block's valid readings */
	uint8_t seconds;         /* +0x05: the block's seconds, to 10 */
	uint8_t field_06;        /* +0x06: zeroed with the block; nothing reads it */
	uint8_t field_07;
	float aging_limit;       /* +0x08: 2e-9, or 2e-9 a day as a rate a second once the EFC history is full; nothing reads it */
	float ti_sum;            /* +0x0c */
	float ti_weighted_sum;   /* +0x10: each reading times its index */
	float ti_largest;        /* +0x14: the largest reading's magnitude, holdover_ti_max's */
	int32_t ti_count;        /* +0x18: at 10 or more the correction may be used */
} holdover_steering_state;

/* startup_pll's and pll_normal's steering, at 0x10205e: a
 * proportional-integral loop on each 10-second block's mean time
 * interval, its gains from the loop's time constant. */
typedef struct loop_steering_state {
	float integral;            /* +0x00: the integral term, in EFC counts */
	float ti_sum;              /* +0x04: the block's valid readings, then their mean */
	int32_t large_offset_blocks; /* +0x08: startup_pll's consecutive blocks whose mean passed 150 ns, to 15 */
	uint8_t seconds;           /* +0x0c: the block's seconds, to 10 */
	uint8_t readings;          /* +0x0d: the block's valid readings */
	uint8_t measurement_failed; /* +0x0e: a reading failed; a block with none valid then holds for a TI error, not the limit */
	uint8_t field_0f;
	float proportional_gain;   /* +0x10: efc_gains.reciprocal_ti over max_loop_time */
	float integral_gain;       /* +0x14: efc_gains.reciprocal_tau over max_loop_time squared, over 4 */
	float filter_weight;       /* +0x18: 29.75 over max_loop_time, pll.ti_filtered's */
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
	char *transmit_next;     /* +0x00: drta_send_message's next character */
	int16_t transmit_count;  /* +0x04: its characters left; at 0 the GPS task is signaled 0x800 */
	uint8_t overrun;         /* +0x06: the receiver overran ("DRTA receiver overrun ocurred") */
	uint8_t loopback_test;   /* +0x07: set while duart_loopback_test_a runs */
	uint8_t held;            /* +0x08: set while that test has the port, so a send leaves the transmit interrupt off */
	uint8_t least_free;      /* +0x09: the receive ring's least free space seen */
} duarta_driver;

/* Channel B's serial settings as last applied to the DUART, at
 * 0x102ecc: copies of the parameter record's, which
 * duartb_baud_from_settings and duartb_mode_from_settings program. */
typedef struct duartb_applied_settings {
	uint8_t baud;
	uint8_t parity;
	uint8_t eight_bits;      /* +0x02: else seven, and duartb_isr strips the top bit */
	uint8_t two_stop_bits;   /* +0x03: else one */
} duartb_applied_settings;

/* What the code last wrote to the 68681's write-only registers, at
 * 0x102ed0, so that it can change some bits of one and write it whole. */
typedef struct duart_shadow {
	uint8_t mr1a;
	uint8_t mr2a;
	uint8_t csra;
	uint8_t acr;
	uint8_t imr;            /* +0x04: 0x10 channel B's transmitter, 0x20 its receiver */
	uint8_t ctur;
	uint8_t ctlr;
	uint8_t mr1b;
	uint8_t mr2b;
	uint8_t csrb;
	uint8_t opcr;
	uint8_t output_reset;   /* +0x0b: the output bits reset, the failed self-tests' lamps */
	uint8_t field_0c;       /* +0x0c: duart_init zeroes it; nothing reads it */
} duart_shadow;

/* A routine that writes a character to a port. */
typedef void putc_function(char c);

/* The 16-byte message a driver sends its exchange once, at start-up, so
 * that a task takes the exchange as a token before using the port: its
 * status, the exchange's id, and the task that holds it, which the
 * driver's interrupts signal. */
typedef struct exchange_token {
	uint32_t status;    /* +0x00: printed if the send fails ("Unable to initialize drtR: %X") */
	uint32_t exchange;  /* +0x04 */
	uint32_t task;      /* +0x08: the task waiting on the port, 0 for none */
	uint32_t field_0c;
} exchange_token;

/* DUART channel B's driver, the host port, at 0x1030ec.  Its receive
 * ring is just before it, at 0x102fea. */
typedef struct duartb_driver {
	char *transmit_next;          /* +0x00: drtb_puts's next character */
	char *async_next;             /* +0x04: drtb_send_async's */
	int16_t transmit_count;       /* +0x08: drtb_puts's characters left */
	uint8_t async_count;          /* +0x0a */
	uint8_t async_pending;        /* +0x0b: when the asynchronous send ends, the SCPI task is signaled 0x20 */
	uint8_t flow;                 /* +0x0c: 0x80 an XOFF received, holding transmission; 0x20 one sent */
	uint8_t receive_errors;       /* +0x0d: the status register's error bits, 0x10 an overrun */
	uint8_t read_errors;          /* +0x0e: receive_errors as the last read left them */
	uint8_t loopback_test;        /* +0x0f: set while duart_loopback_test_b runs */
	uint8_t xon_xoff;             /* +0x10: flow control, from the serial settings */
	uint8_t least_free;           /* +0x11: the receive buffer's least free space seen */
	uint8_t parity;               /* +0x12: from the serial settings */
	uint8_t reconfiguring;        /* +0x13: set while duartb_reconfigure has the port's interrupts off */
	uint8_t in_transmit_interrupt; /* +0x14 */
	uint8_t field_15;
	uint32_t read_exchange;       /* +0x16: drtR */
	uint32_t write_exchange;      /* +0x1a: drtW */
	exchange_token read_token;    /* +0x1e: its task is signaled 0x800 on a ctrl-C, 0x4000 when input comes */
	uint8_t field_2e[8];
	exchange_token write_token;   /* +0x36: its task is signaled 0x1000 when drtb_puts's string is out */
	uint8_t field_46[8];
	uint8_t async_allowed;        /* +0x4e: an asynchronous send may start; an XOFF does not stop it */
	uint8_t field_4f;
	putc_function *echo;          /* +0x50: drtb_putc, or a routine that does nothing, from the serial settings */
	uint8_t last_terminator;      /* +0x54: the CR or LF that ended the last line, so its partner is skipped */
	uint8_t field_55;
} duartb_driver;

/* The EFC's gain and what efc_gain_set derives from it, at 0x102554:
 * pllp_task sets the gain, the fractional frequency one EFC count
 * moves, 6.25e-13. */
typedef struct efc_gain_values {
	float gain;            /* +0x00: fractional frequency a count */
	float reciprocal_fine; /* +0x04: counts per unit fractional frequency, as fine and coarse use it */
	float reciprocal_ti;   /* +0x08: the same, as startup_pll and pll_normal scale the time interval with it */
	float reciprocal_tau;  /* +0x0c: the same, as they divide it by the loop's time constant */
	float step_limit;      /* +0x10: 6.25e-10 in counts, the bound startup_pll and pll_normal compare a step with */
	float aging_per_day;   /* +0x14: 5e-9 a day in counts a second; nothing reads it */
} efc_gain_values;

/* The GPS command the console's GPS words leave for pllp_task, at
 * 0x10256c: gps_command_send maps the code to an Oncore request, code
 * 0x20 sending request as it is, and sends it with the arguments. */
typedef struct gps_command_mailbox {
	uint8_t code;          /* +0x00: 0 when none is pending */
	uint8_t field_01;
	int32_t arguments[4];  /* +0x02 */
	int32_t request;       /* +0x12: code 0x20's request */
} gps_command_mailbox;

/* The self-tests' results, at 0x100c24: each test's word nonzero if it
 * failed, and what selftest_run reports for test n, 0 to 13.  The
 * start-up clear begins at 0x100c3c, so the reset code's results
 * before it (CPU, RAM, UART) and the rest up to the interpolator's
 * survive a warm restart. */
typedef struct selftest_results {
	int16_t cpu;                     /* +0x00: the reset code's CPU test; lights OP4 */
	uint16_t ram;                    /* +0x02: the RAM test's, bit 0 a pattern, bit 1 an address line; lights OP5 */
	uint16_t ram_data_bits;          /* +0x04: the bits that failed a pattern, both halves of a long ORed */
	uint32_t ram_address_lines;      /* +0x06: the address lines that alias */
	int16_t uart;                    /* +0x0a: duart_early_init's IVR and loopback checks; lights OP6 */
	uint8_t lamp_mask;               /* +0x0c: the DUART output bits the failures reset, which duart_init writes back */
	uint8_t field_0d;
	uint16_t summary;                /* +0x0e: selftest_summary's failure flags as bits */
	int16_t eeprom;                  /* +0x10 */
	int16_t fpga;                    /* +0x12: the FPGA's counters, counter_selftest */
	int16_t gps;                     /* +0x14: the receiver's @@Ca result, its bits as the VP reference gives them */
	int16_t interpolator;            /* +0x16 */
	uint8_t interpolator_calibrated; /* +0x18: 1 once a calibration succeeded */
	uint8_t interpolator_tries;      /* +0x19: the calibrations it took, up to 20 */
	uint16_t interpolator_readings[2]; /* +0x1a: both ends' readings, interpolator_sample_test's */
	uint8_t field_1e;
	uint8_t running;                 /* +0x1f: set while selftest_execute runs a test */
	int16_t power;                   /* +0x20: the 5 V, +15 V and -15 V supplies */
	int16_t internal_reference;      /* +0x22: the internal 1PPS's count */
	int16_t qspi;                    /* +0x24: the QSPI's devices: ADC, DACs, FPGA, EFC */
	uint8_t field_26[14];
	uint8_t passed;                  /* +0x34: 1 unless a test selftest_print_failure ran failed */
	uint8_t field_35;
	uint32_t result;                 /* +0x36: selftest_run's test's result, what *TST? and :DIAGnostic:TEST? report */
	uint32_t details[2];             /* +0x3a: the RAM test's bits and lines, or the interpolator's tries and outcome or readings */
	uint8_t test;                    /* +0x42: the test selftest_run was asked for */
	uint8_t in_run;                  /* +0x43: set during selftest_run; nothing reads it */
} selftest_results;

/* A GPS request waiting in gpsm_task's ring: a copy of the 26-byte
 * message it took from gpsx_exchange.  Only the fields the ring's
 * routines read are named. */
typedef struct gps_request_entry {
	uint8_t field_00[8];
	uint8_t id;          /* +0x08: one with the same id waiting is not queued again; 3 and 4 may be replaced */
	uint8_t field_09;
	int16_t code;        /* +0x0a: 0x45 empties the ring before it is queued */
	uint8_t field_0c[6];
	uint8_t sent;        /* +0x12: nonzero once sent; a sent request is not replaced */
	uint8_t field_13[7];
} gps_request_entry;

/* gpsm_task's ring of waiting GPS requests: five entries, the oldest at
 * read, the next free at write; empty when the two are equal, so it
 * holds four. */
typedef struct gps_request_ring {
	gps_request_entry entries[5];
	gps_request_entry *read;     /* +0x82 */
	gps_request_entry *write;    /* +0x86 */
} gps_request_ring;

/* A date and time as datetime_add and datetime_subtract take them: the
 * time's hours, minutes and seconds in its low three bytes, and the date
 * packed as date_to_days reads it, or, in their first argument, a count
 * of days to add or take away. */
typedef struct packed_datetime {
	uint32_t time;
	uint32_t date;
} packed_datetime;

/* What sprintf and vsprintf hand doprnt to pass to sprintf_put, on
 * their own stacks: sprintf_put writes at cursor and advances it.
 * Nothing reads unused. */
typedef struct sprintf_output {
	uint32_t unused;
	char *cursor;
} sprintf_output;

typedef struct scpi_numeric_param {
	scpi_param_type type; /* SCPI_PARAM_NUMERIC */
	uint8_t last_index;   /* +0x01: values less one */
	int8_t *nv_widths; /* +0x02: bits in the NV register; negative, signed */
	void *minimum;        /* +0x06: the least value accepted */
	void *maximum;        /* +0x0a: the greatest */
	scpi_validate_function *validate; /* +0x0e: validate_num's check; clamp_to_limits */
	void *changed;        /* +0x12: called as each value is stored */
	void *value;          /* +0x16: the current value, in RAM */
	void *done;           /* +0x1a: called once they are stored */
	uint16_t flags; /* +0x1e: 2 saved in the NV register, 4 at 0x400000 */
	uint8_t gps_request;  /* +0x20: as scpi_setting_param's */
	uint8_t field_21;
	void *group_cell;     /* +0x22: as scpi_setting_param's; scpi_numeric_get_double alone reads it */
	uint8_t is_integer;   /* +0x26: 0 float, 1 integer, 2 or 3 several */
	uint8_t field_27;
	void *steps;          /* +0x28: 8 bytes a value, whose routine at +4 gives validate the step */
} scpi_numeric_param;

/* One status register of the SCPI status tree.  16 bytes. */
typedef struct status_register {
	uint16_t condition;           /* +0x00 */
	uint16_t negative_transition; /* +0x02: falling conditions that become events */
	uint16_t positive_transition; /* +0x04: rising ones */
	uint16_t enable;              /* +0x06: events reported to the parent */
	uint16_t event;               /* +0x08: latched, cleared as read */
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
	int32_t field_0e;              /* +0x0e: status_create's last argument, 0; nothing reads it */
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
	uint8_t loop_settled;  /* +0x002: startup_pll sets it on settling without a fit; aging_state then goes to 3 */
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
	float drift; /* +0x352: pll_rep's current drift, times 5.4e-8 per day */
	int32_t field_356; /* nothing refers to it */
	float fit_a; /* +0x35a: the curv task's aging fit a + b y + c ln y, y = samples / 2700 */
	float fit_b; /* +0x35e */
	float fit_c; /* +0x362 */
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
	uint8_t hold_height_type;                  /* @@As: 0 GPS ellipsoid, 1 MSL */
	uint8_t field_0b3;
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
	uint8_t status_received;  /* @@Ba has come; the status monitor sets it */
	uint8_t status_rate;      /* s between @@Ba, as last requested */
	oncore_status_fields status;               /* @@Ba */
	uint8_t field_10d;
	uint8_t reference_qualified; /* reference_valid before the PPS check */
	uint8_t status_reported;     /* status_code, held while it is 5 to 8 */
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
	uint8_t field_2f8[2];
	uint8_t raim_received; /* @@Bn has come; the status monitor sets it */
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
	uint8_t reference_valid;            /* :GPS:REF:VALid; 0 while the PPS is dead */
	uint8_t pps_dead;                   /* gps_pps_dead, while it counts */
	uint8_t status_code;                /* 1 PPS dead, 2 after a reset, 3 tracking, 4 holding or surveying, 5 to 8 validating, 9 valid */
	uint8_t reference_valid_override;   /* the byte at 0x100c68: 1 forces valid, 0xff invalid */
	uint32_t validation_start;          /* uptime as status_code reached 5 */
	uint8_t field_532[2];               /* status_snapshot reads them; nothing writes them */
	uint8_t status_code_reset;          /* 2, set by gps_receiver_status_reset */
	uint8_t field_535[5];
	uint8_t surveying;                  /* averaging @@Ba's position */
	uint8_t raim_assessment;            /* 0 inactive, 1 OK, 2 alarm detected, 3 alarm */
	uint8_t pps_check_disabled;         /* gps_pps_check skips while set */
	uint8_t receiver_health_failed;     /* "GPS Receiver Health" */
	uint8_t command_monitor_enabled;
	uint8_t command_monitor_state;      /* 0 disabled, 1 standby, 2 query, 3 query-ack */
	void *command_monitor_query;        /* the next query of the list at 0x50e5e */
	uint32_t command_monitor_time;      /* uptime of its last state change */
	uint8_t command_monitor_error;      /* set by gpsm_task when a command goes unanswered */
	uint8_t command_monitor_failed;
	uint8_t status_monitor_enabled;
	uint8_t status_monitor_state;       /* 0 disabled, 1 standby, 2 recovery */
	uint32_t status_monitor_time;       /* uptime of the last fresh @@Ba */
	uint32_t status_last_time;          /* the last @@Ba's time bytes, compared as one */
	uint8_t status_repeat_count;        /* @@Ba with the same time in a row */
	uint8_t status_monitor_timed_out;
	uint8_t status_monitor_failed;
	uint8_t field_557;
	uint8_t raim_option; /* its options list has an I */
	uint8_t field_559;
} gps_state;

/* The disciplining loop's state block: the stage the status screen
 * shows (docs/screen-format-strings.md, "Mode suffixes") and the PLL's
 * values.  Cleared as one, memset(&pll, 0, 0x3d6). */
typedef struct pll_state {
	pll_stage stage;
	pll_stage previous_stage;     /* +0x01: pllp_task's copy of stage */
	uint8_t powerup_countdown;    /* +0x02: readings left before powerup checks the clock against GPS, 25 then 2 */
	uint8_t powerup_state;        /* +0x03: powerup's sub-state, screen-format-strings.md's ladder */
	uint8_t powerup_reference_lost; /* +0x04: GPS or its PPS went, or temporary mode; powerup goes back to state 3 */
	uint8_t field_005;
	uint8_t reference_check_state; /* +0x06: pll_check_reference's */
	uint8_t field_007[5];
	uint8_t startup_state;        /* +0x0c: startup_pll's */
	uint8_t field_00d;
	uint8_t normal_state;         /* +0x0e: pll_normal's */
	uint8_t field_00f;
	uint8_t holdover_state;       /* +0x10: holdover's */
	pll_hold_cause hold_causes;   /* +0x11: a set of pll_hold_cause bits */
	uint8_t manual;               /* +0x12 */
	uint8_t ti_limit_override;    /* +0x13: holdover checks the TI limit while 0; only ever cleared */
	uint8_t recovery_countdown;   /* +0x14: holdover_recovery's, as powerup_countdown */
	uint8_t recovery_state;       /* +0x15 */
	uint8_t coarse_state;         /* +0x16: coarse's */
	uint8_t fine_state;           /* +0x17 */
	uint8_t frequency_adjusted;   /* +0x18: fine applied a frequency correction */
	uint8_t fine_index;           /* fine acquisition's "i= %d" */
	uint16_t fine_retries;        /* +0x1a: fine's failed attempts, given up past 9 */
	float fine_d;                 /* fine acquisition's "d = %f" */
	uint8_t aging_state;          /* +0x20: efc_history_update's; 3 once the aging fit holds */
	uint8_t field_021[3];
	uint8_t gps_diag_state;       /* +0x24: gps_diag_sequence's */
	uint8_t gps_diag_substate;    /* +0x25 */
	uint8_t gps_init_state;       /* +0x26: gps_init_sequence's, print_gps_init_state's */
	uint8_t field_027;
	uint16_t leap_check_timer;    /* +0x28: gps_leap_sequence checks again when it wraps */
	uint8_t leap_check_requested; /* +0x2a: pll_stage_status sets it */
	uint8_t leap_check_state;     /* +0x2b: gps_leap_sequence's, print_leap_check_state's */
	uint8_t leap_ack_state;       /* +0x2c: gps_leap_ack's, measuring the GPS-UTC offset by switching @@Aw */
	uint8_t leap_ack_requested;   /* +0x2d */
	uint8_t leap_ack_done;        /* +0x2e: the offset is set */
	uint8_t field_02f;
	uint8_t leap_ack_again;       /* +0x30: holdover_recovery measures again after fine */
	uint8_t field_031;
	float efc;                    /* pr_efc's efc */
	float tfom;                   /* pll_rep's tfom, s */
	float predicted_uncertainty;  /* +0x3a: the curv fit's, :KENneth:PREDicted? */
	uint8_t field_03e[8];
	float frequency_tolerance;    /* +0x46: 1e-8 at first; fine's bound, holdover's limit per second */
	efc_ring efc_history;
	float ti_filtered;            /* +0x3b0: the TI, filtered with weight 29.75 / tau */
	float efc_base;               /* +0x3b4: the EFC less the temperature term, TC times the current */
	float loop_time_constant;     /* +0x3b8: 150 s at first, at most max_loop_time */
	uint32_t holdover_start_time; /* +0x3bc: get_date_time's as holdover starts */
	uint32_t holdover_start_date; /* +0x3c0 */
	uint8_t led_gps_lock;         /* +0x3c4: :LED:GPSLock? */
	uint8_t led_holdover;         /* +0x3c5: :LED:HOLDover? */
	uint8_t led_tmh_valid;        /* +0x3c6: :LED:TMHValid? */
	uint8_t field_3c7;
	uint32_t holdover_duration;   /* +0x3c8: :ROSCillator:HOLDover:DURation? */
	uint32_t reference_uptime;    /* +0x3cc: the uptime clock_set_from_reference counts from */
	uint8_t reference_seconds;    /* +0x3d0: the FPGA's seconds count with it */
	uint8_t fpga_load_pending;    /* +0x3d1: 1 from ROM 0x21e2c until fpga_setup loads it */
	uint8_t fpga_loaded;          /* +0x3d2: the PPS checks wait for it */
	uint8_t loop_running;         /* +0x3d3: 0 in diagnostic mode; nothing reads it */
	uint8_t sync_requested;       /* +0x3d4: sync_perform syncs whatever the TI, then clears it */
	uint8_t field_3d5;            /* read by the GPS status code, never written but by the block copy */
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
	uint16_t field_0e;         /* no response code counts it and nothing prints it */
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
	uint16_t write_count; /* +0x00: incremented by each log_page_write */
	uint16_t checksum;    /* +0x02: log_page_checksum's byte sum and its complement */
	log_entry entries[3];
} log_page;

/* An entry of fpga_bit_table: the FPGA register a bit is in and its
 * mask, which fpga_set_bit sets or clears in fconf_regs. */
typedef struct fpga_bit {
	uint8_t reg;
	uint8_t mask;
	uint8_t is_flag; /* 1: fpga_fields_to_registers sets mask for a nonzero value; 0: ORs the value in */
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
	scpi_param_type type; /* SCPI_PARAM_SETTING */
	uint8_t last_index;   /* +0x01: values less one */
	int8_t *nv_widths;    /* +0x02: each value's bits in the NV register */
	int8_t maximum;       /* +0x06: toggle_change refuses a value above it */
	uint8_t field_07;
	void *changed;        /* +0x08: called as the value is stored */
	uint8_t *value;       /* +0x0c: the current value, a byte each */
	void *done;           /* +0x10: called once it is */
	uint16_t flags;       /* +0x14: as scpi_numeric_param's */
	uint8_t gps_request;  /* +0x16: what pll_scpi_send_to_loop sends gpsx_exchange, 0xff none */
	uint8_t field_17;
	void *group_cell;     /* +0x18: 0x101f92 to 0x101f9a by command group; no code reads it */
} scpi_setting_param;

/* The settings nv_save keeps in the NV register and nv_restore loads:
 * each field the value of the descriptor in nv_save's tables that points
 * at it, named by the SCPI command whose setter is given that descriptor.
 * 0xc5 bytes. */
typedef struct nv_settings_fields {
	uint8_t field_00[2];                    /* no code refers to them */
	int32_t elevation_mask;                 /* :GPS:EMANgle */
	int32_t antenna_delay;                  /* :GPS:ADELay */
	int32_t douglas_duration;               /* :DOUGlas:DURation */
	uint8_t douglas_duration_infinite;      /* :DOUGlas:DURation INFinity */
	uint8_t pad_0f;
	int32_t holdover_status_threshold;      /* :ROSC:HOLD:DUR:STAT:THR */
	float limit_threshold;                  /* :ROSC:LIMit:THReshold */
	float holdover_limit_threshold;         /* :ROSC:HOLD:LIMit:THReshold */
	uint8_t holdover_limit_state;           /* :ROSC:HOLD:LIMit:STATe */
	uint8_t douglas_state;                  /* :DOUGlas:STATe */
	uint8_t field_1e; /* only its descriptor refers to it */
	uint8_t pad_1f;
	int32_t position[3]; /* :GPS:POSition, mas, mas, cm */
	uint32_t ignored_satellites;            /* :GPS:SAT:TRAC:IGNore */
	int32_t time_zone[2]; /* :PTIMe:TZONe, hours, minutes */
	int32_t prediction_duration; /* :KENneth:PREDicted:DURation */
	uint32_t leap_date;                     /* a packed date */
	int32_t leap_minute_length;             /* 59 for a second removed */
	uint8_t leap_pending;
	uint8_t raim_state[2]; /* :DIAG:GPS:POS:HOLD:RAIM:STATe */
	uint8_t pad_47;
	int32_t raim_threshold[2]; /* :DIAG:GPS:POS:HOLD:RAIM:THR */
	int32_t holdover_measurement_threshold; /* :ROSC:HOLD:DUR:MEAS:THR */
	uint32_t status_enable[7];              /* *ESE, *SRE, :STATus ENABle */
	uint32_t status_positive_transition[5]; /* :STATus PTRansition */
	uint32_t status_negative_transition[5]; /* :STATus NTRansition */
	uint8_t questionable_user;              /* :STAT:QUES:...:USER */
	uint8_t serial_restricted;              /* :DIAG:SER1:RESTricted */
	uint8_t pulse_state;                    /* :PULSe:CONTinuous:STATe */
	uint8_t pad_9b;
	int32_t pulse_period;                   /* :PULSe:CONTinuous:PERiod */
	int32_t pulse_start_time[3];            /* :PULSe:STARt:TIME */
	int32_t pulse_start_date[3];            /* :PULSe:STARt:DATE */
	int32_t field_b8; /* 0 to 50000; only its descriptor */
	uint8_t field_bc; /* only its descriptor refers to it */
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
	uint8_t fpga_registers[4]; /* counter_arm copies them into fconf_regs */
	double calibration;          /* s, which counter_read subtracts */
	uint8_t registers_mode;      /* 1: load fpga_registers ... */
	uint8_t registers_state;     /* ... when this is 2 */
	uint8_t calibrate_mode;      /* 3: always calibrate; 1: ... */
	uint8_t calibrate_state;     /* ... when this is 2 */
	uint8_t sync_mode;           /* 2: counter_arm syncs ... */
	uint8_t sync_state; /* ... when this is 2; sync_request sets both */
	uint8_t sync_kind; /* sync_request's first; into fconf_regs[3] */
	uint8_t pad_13;
	int32_t sync_count;          /* sync_request's second; counted down */
	int32_t interpolator_offset; /* reading_low, subtracted from the reading */
	int32_t interpolator_scale;  /* the spread it is divided by */
} counter_configuration_fields;

