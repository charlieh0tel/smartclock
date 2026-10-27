/* installer/installer_status.c's functions that are this image's own: the rest
 * are common/installer/installer_status.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* Builds the installer's status model, two registers and its four
 * errors' conditions, marks it ready, and sets the registers up: the
 * Status Byte the console port's, register 1 from its layout, its
 * positive filter its defined bits (installer::0x107a00). */
uint32_t installer_status_init(void) {
  static const int32_t REGISTERS = 2;
  const installer_status_register_layout* layout =
      &installer_status_register_layouts[1];
  const uint32_t error = installer_status_create(
      &installer_status_object, REGISTERS, STATUS_ERROR_QUEUE_ROOM,
      installer_status_conditions, 0);

  if (error != 0) {
    return error;
  }
  installer_status_ready = 1;
  installer_status_register_setup((int32_t)installer_status_object, 0, 0, 0, 0,
                                  0, (int32_t)installer_scpi_console_port());
  installer_status_register_setup((int32_t)installer_status_object, 1,
                                  layout->defined_bits, 0, layout->parent_index,
                                  layout->parent_bit, 0);
  return 0;
}
