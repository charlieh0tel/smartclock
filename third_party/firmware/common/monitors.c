/* The functions of monitors.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/monitors.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Clears efc_clamp's and hmon_check_efc's state. */
void efc_clamp_reset(void) {
  efc_range.clamped = 0;
  efc_range.near_end = 0;
  efc_range.near_end_checks = 0;
}

/* The reports, after a full one, for which the health monitor services
 * the watchdog. */
static const int16_t MONITORS_WATCHDOG_GRACE = 8;

/* Checks the tasks' watchdog counts, printing them if one is short,
 * and services the watchdog while the grace lasts; then clears them. */
void watchdog_report(void) {
  /* The watchdog: each task counts its passes, and the health monitor
   * services the MC68331's software watchdog (MC68331UM D.3.15, SWSR's
   * CPU_SWSR_FIRST then CPU_SWSR_SECOND) only while every count since
   * the last report is enough, and for eight reports after. */
  static const uint8_t WATCHDOG_GPS_LEAST = 21;
  static const uint8_t WATCHDOG_MONITOR_LEAST = 51;

  if (watchdog_clock != 0 && watchdog_gps >= WATCHDOG_GPS_LEAST &&
      watchdog_monitor >= WATCHDOG_MONITOR_LEAST && watchdog_spool != 0 &&
      watchdog_pll != 0) {
    watchdog_countdown = MONITORS_WATCHDOG_GRACE;
  } else {
    debug_printf(
        "watchdog: clock: %4d gps: %4d pll: %4d mon: %4d spool %4d\r\n",
        watchdog_clock, watchdog_gps, watchdog_pll, watchdog_monitor,
        watchdog_spool);
  }
  if (watchdog_countdown != 0) {
    watchdog_countdown--;
  }
  if (watchdog_countdown != 0) {
    uint16_t sr = cpu_enter_supervisor();

    sim.swsr = CPU_SWSR_FIRST;
    sim.swsr = CPU_SWSR_SECOND;
    cpu_sr_write(sr);
  }
  watchdog_clock = 0;
  watchdog_gps = 0;
  watchdog_pll = 0;
  watchdog_monitor = 0;
  watchdog_spool = 0;
}
