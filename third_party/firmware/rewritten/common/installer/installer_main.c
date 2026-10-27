/* The installer's copy of the system's start (`applications/main`; the
 * primary's system_init, in main.c, does far more).
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); it keeps its generated prototype. */

/* Marks the kernel and the status system not yet running and reads the
 * parameter and identification records. */
void installer_system_init(void) {
  installer_kernel_running = 0;
  installer_status_ready = 0;
  installer_parameter_record_read();
  installer_identification_record_read();
}
