/* The functions of pllp.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/pllp.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Does nothing; pllp_task calls it after counter_init. */
void pllp_start_nothing_a(void) {}

/* Does nothing; pllp_task calls it after pllp_start_nothing_a. */
void pllp_start_nothing_b(void) {}

/* Sets the clock from the reference: the GPS time plus the seconds the
 * FPGA's count and the uptime say have passed since pllp_task started,
 * 0 to 59. */
void clock_set_from_reference(void) {
  static const uint8_t FPGA_SECONDS_MASK = 0x0f;
  static const uint8_t FPGA_SECONDS_WRAP = 0x10;
  static const int32_t LAST_SECOND = 60 - 1;
  uint8_t seconds = fpga_read_seconds() & FPGA_SECONDS_MASK;
  packed_datetime change;
  packed_datetime now;

  restart_reference_seconds &= FPGA_SECONDS_MASK;
  if (seconds <= restart_reference_seconds) {
    seconds += FPGA_SECONDS_WRAP;
  }
  uint32_t uptime = get_uptime();

  if (uptime < restart_reference_uptime) {
    restart_reference_uptime = 0;
  }
  int32_t elapsed = (int32_t)(seconds - restart_reference_seconds) +
                    (int32_t)(restart_reference_uptime - uptime);

  if (elapsed < 0) {
    elapsed = 0;
  } else if (elapsed > LAST_SECOND) {
    elapsed = LAST_SECOND;
  }
  change.time = clock_time_of(elapsed);
  change.date = 0;
  get_gps_datetime(&now.date, &now.time);
  datetime_add(&change, &now);
  set_timedate_unflagged(now.date, clock_time_bits(now.time));
}
