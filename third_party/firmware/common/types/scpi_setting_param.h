/* scpi_setting_param and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

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
  void* group_cell; /* +0x18: a scpi_group_cells word by command group; no code
                       reads it */
} scpi_setting_param;
