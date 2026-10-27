/* oncore_receiver_status and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

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
/* fine's states, pll.fine_state, as print_fine_state names
 * them: ": fine start" to ": fine fine slew" (diag_words_define+0x470 to
 * diag_words_define+0x4f5). */
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
 * posts it (diag_print_stat diag_print_stat to diag_gps_query diag_gps_query);
 * gps_command_send maps each to its GPS request.  Nothing in
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
