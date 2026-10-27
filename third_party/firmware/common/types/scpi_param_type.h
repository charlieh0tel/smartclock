/* The SCPI parameters' types, as the images' smartclock.h declare them (each
 * includes this header where they stand): docs/firmware/rewrite.md, "Shared C".
 * Where a comment cites code, it is the Z3805A 3543B's, which the
 * 58503A's copy of the comment came from. */

/* A SCPI parameter descriptor's structure, its first byte, which the
 * pllc task's validate_or_doaction, get_setup_vars and finish_scpi_cmd
 * switch on; each accepts 3 and does nothing with it. */
typedef enum __attribute__((packed)) scpi_param_type {
  SCPI_PARAM_NUMERIC = 0, /* scpi_numeric_param */
  SCPI_PARAM_SETTING = 1, /* scpi_setting_param */
  SCPI_PARAM_ACTION = 2,  /* scpi_action_param */
  SCPI_PARAM_DOUBLE =
      3 /* a double's scpi_numeric_param (scpi_param_SAMPle_FARR+0x4a) */
} scpi_param_type;
