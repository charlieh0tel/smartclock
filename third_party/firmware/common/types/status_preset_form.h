/* status_preset_form and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* status_preset_form: :STATus:PRESet's two forms, the action index
 * scpi_set_STATus_PRESet and scpi_set_STATus_PRESet_ALARm give
 * status_preset_done, by its jump table at status_preset_done+0x1a. */
typedef enum __attribute__((packed)) status_preset_form {
  /* The filtered registers' enables (status_preset_done+0x1e). */
  STATUS_PRESET_SCPI = 0,
  /* Every enable, and :QUEStionable:USER off (status_preset_done+0x52). */
  STATUS_PRESET_ALARM = 1,
} status_preset_form;
