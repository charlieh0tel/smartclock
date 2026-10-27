/* status_register_layout and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* One register's entry in status_register_layouts, seven of them,
 * which status_init sets the registers up from (status_init+0x6c to
 * status_init+0x7e) and the :STATus enable and transition setters check a value
 * against (scpi_check_range's bounds, +0x04 and +0x06).  22 bytes. */
typedef struct status_register_layout {
  uint8_t parent_index;  /* +0x00: the register it reports into */
  uint8_t unused;        /* +0x01 */
  uint16_t parent_bit;   /* +0x02: at this bit; 0 for the Status Byte */
  uint16_t maximum;      /* +0x04: the greatest enable or filter */
  uint16_t minimum;      /* +0x06 */
  uint16_t defined_bits; /* +0x08: the bits the register has */
  uint32_t* enable;      /* +0x0a: its nv_settings.status_enable */
  uint32_t* positive;    /* +0x0e: its nv_settings filters, NULL for the */
  uint32_t* negative;    /* +0x12: Status Byte and Standard Event */
} status_register_layout;
