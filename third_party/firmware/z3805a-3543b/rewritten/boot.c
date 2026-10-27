/* The boot code's routines (reset at 0x550, its own program ahead of the
 * primary), written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `platform/boot`): its DUART self-test and its unpacker of the
 * installer's records, with the primary's copy of that unpacker.  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* Tests the DUART's interrupt vector register with two patterns and sets
 * its vector, then, if it held them, sets channel B up for 9600 8N1 and
 * checks it in loopback (0x4cc). */
void boot_duart_early_init(void) {
  boot_selftest.uart = 0;
  duart.ivr = DUART_IVR_PATTERN_1;
  if (duart.ivr != DUART_IVR_PATTERN_1) {
    boot_selftest.uart |= SELFTEST_UART_REGISTER;
  }
  duart.ivr = DUART_IVR_PATTERN_2;
  if (duart.ivr != DUART_IVR_PATTERN_2) {
    boot_selftest.uart |= SELFTEST_UART_REGISTER;
  }
  duart.ivr = DUART_IVR_VECTOR;
  if (boot_selftest.uart == 0) {
    duart.b.cr = CR_RESET_RECEIVER;
    duart.b.cr = CR_RESET_TRANSMITTER;
    duart.b.cr = CR_RESET_MR_POINTER;
    duart.b.mr = MR1_PARITY_NONE | DUART_MR1_EIGHT_BITS;
    duart.b.mr = DUART_MR2_ONE_STOP_BIT;
    duart.b.sr_csr = DUART_CSR_9600 << DUART_CSR_CLOCK_BITS | DUART_CSR_9600;
    duart.ipcr_acr = ACR_BRG_SET_2 | DUART_ACR_COUNTER_CRYSTAL_16;
    boot_duartb_loopback_check();
  }
}

/* Copies the installer's records, installer_records' 0xa76d bytes at
 * 0xbbc, into RAM (0xaa0). */
uint32_t boot_unpack_installer(void) {
  static const int32_t RECORDS_SIZE = 0xa76d;

  return boot_unpack(installer_records, RECORDS_SIZE, false);
}
