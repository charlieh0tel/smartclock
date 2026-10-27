/* scpi_action_param and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A SCPI action's descriptor, in ROM, 6 bytes, from
 * scpi_action_DIAGnostic_PTIMe_TINTerval to
 * scpi_action_SOURce_PTIMe_LEAPsecond_ACCumulated_CALCulate+0x5.
 * validate_or_doaction refuses an index above last_index, then
 * pll_scpi_do_action calls action with the index and the source; it
 * stores nothing and leaves finish_scpi_cmd uncalled. */
typedef struct scpi_action_param {
  scpi_param_type type; /* SCPI_PARAM_ACTION */
  uint8_t last_index;   /* +0x01: indexes less one */
  void* action;         /* +0x02: called with the index and the source */
} scpi_action_param;
