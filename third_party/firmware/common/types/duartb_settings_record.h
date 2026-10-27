/* duartb_settings_record and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* DUART channel B's serial settings, SERial1's, at duartb_settings: the
 * :SYSTem:COMMunicate:SERial1 setters store them, through setting
 * descriptors that save the EEPROM parameter record, and
 * duartb_reconfigure applies them.  parameter_record_read loads +2 to
 * +8 from the record's bytes 2 to 8, or copies the defaults at
 * duartb_settings_defaults over all 9 bytes when its checksum fails;
 * save_parameter_record writes them back. */
typedef struct duartb_settings_record {
  uint8_t field_00[2];  /* +0x00: only the defaults' copy writes them, 0;
                           nothing reads them */
  serial_baud baud;     /* +0x02 */
  serial_parity parity; /* +0x03: not ONE */
  uint8_t xon_xoff;     /* +0x04: :PACE XON when nonzero, else NONE */
  /* +0x05: saved and restored; its setting descriptor,
     scpi_param_SYSTem_COMMunicate_SER1_RECeive_PACE+0x1e, no setter
     uses */
  uint8_t field_05;
  uint8_t eight_bits;    /* +0x06: else seven; with
                            nv_settings.serial_restricted set, parity set with
                            8 bits clears it, and 8 bits set with parity clears
                            parity */
  uint8_t two_stop_bits; /* +0x07: else one */
  uint8_t echo; /* +0x08: :FDUPlex ON, each character received echoed */
} duartb_settings_record;
