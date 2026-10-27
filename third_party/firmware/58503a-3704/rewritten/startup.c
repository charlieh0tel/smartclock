/* startup.c's functions that are this image's own: the rest are
 * common/startup.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Enters the installer with trap #11, which does not come back
 * (restart.md), first deleting the front panel's task, where it runs,
 * and blanking the display; scpi_task_run calls it for
 * :SYSTem:LANGuage (0x130da). */
void enter_installer(void) {
  if (task_fpnl != 0) {
    psos_delete(task_fpnl);
    front_panel_send_line(0, "");
  }
  __asm__ volatile("trap #11");
}
