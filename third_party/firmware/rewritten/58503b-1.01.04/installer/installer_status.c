/* From common/installer/installer_status.c, the 58503B 1.01.04's own beside
 * common/'s, as the Z3817A 3752's: the installer's copies of the status
 * system's routines the function check finds the decompiler's reading wrong
 * for, the same in both images, whose installers are one byte for byte
 * (docs/firmware/58503b-1.01.04.md, "The function check").  Each keeps its
 * generated prototype.  With them, from other images' rewritten C, of its
 * functions, those the 58503B's code carries identical or relocated, with the
 * static helpers they use (docs/firmware/58503b-1.01.04.md, "The link list");
 * their comments' addresses are the 58503B's, but where they name another
 * image. */

/* A register's event word, without clearing it; for the Status Byte, the
 * one with no parent bit, the port's status byte (installer::0x10625a). */
void installer_status_peek_event(status_model* status, int32_t index,
                                 uint16_t* event) {
  const status_register* reg = status->registers[index];

  if (reg->parent_bit != 0) {
    *event = reg->event;
  } else {
    *event = status->output->methods->status_byte_get(status->output);
  }
}

/* Builds the installer's status model, two registers and its four
 * errors' conditions, marks it ready, and sets the registers up: the
 * Status Byte the SCI's port's, register 1 from its layout, its positive
 * filter its defined bits (installer::0x107938). */
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
  installer_status_register_setup(
      (int32_t)installer_status_object, 0, 0, 0, 0, 0,
      (int32_t)installer_port_object(SCPI_PORT_SCI));
  installer_status_register_setup((int32_t)installer_status_object, 1,
                                  layout->defined_bits, 0, layout->parent_index,
                                  layout->parent_bit, 0);
  return 0;
}
