/* The boot code's routines (reset at 0x550, its own program ahead of the
 * primary) that are this image's own: the rest are common/boot.c's
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `platform/boot`); each keeps its generated
 * prototype. */

/* Tests the DUART's interrupt vector register with two patterns and sets
 * its vector, then, if it held them, sets channel B up for 9600 8N1 and
 * checks it in loopback (0x4cc).  The counter is clocked from channel
 * B's transmitter (0x53c), where the Z3805A's takes the crystal over
 * 16. */
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
    duart.ipcr_acr = ACR_BRG_SET_2 | DUART_ACR_COUNTER_TXCB;
    boot_duartb_loopback_check();
  }
}

/* Copies the installer's records, installer_records' 0xa811 bytes at
 * 0xbc4, into RAM (0xaa8). */
uint32_t boot_unpack_installer(void) {
  return boot_unpack(installer_records, sizeof installer_records, false);
}
