/* aging_curve_state and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The curv task's aging fit, at aging_curve: the curve a + b y + c w
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
  float c_step; /* +0x1e: fit_log_search's step in c, a tenth as large each
                   pass */
  float rms;    /* +0x22: the residuals' */
  float predicted_uncertainty; /* +0x26: fit_quality's HQ, or 4.32e-4 in modes
                                  0 and 1 */
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
