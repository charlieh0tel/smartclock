/* holdover_limit_setting and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* nv_settings.holdover_limit_state, :ROSCillator:HOLDover:LIMit:STATe's
 * reverse: its setter stores 1 for OFF, 0 for ON
 * (scpi_set_SOURce_ROSCillator_HOLDover_LIMit_STATe), its query answers 1 for 0
 * (scpi_query_SOURce_ROSCillator_HOLDover_LIMit_STATe), and
 * nv_settings_defaults' is 0.  The loop tests it against 0 (fine, startup_pll's
 * and pll_normal's readings, holdover) and the status screen against 1
 * (status_screen_tfom). */
typedef enum __attribute__((packed)) holdover_limit_setting {
  HOLDOVER_LIMIT_ON = 0,  /* the TI limit holds over */
  HOLDOVER_LIMIT_OFF = 1, /* no limit; the screen prints " Off" */
} holdover_limit_setting;
