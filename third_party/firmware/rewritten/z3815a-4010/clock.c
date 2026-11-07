/* From the 58503A 3704's rewritten/clock.c, the Z3815A 4010's base: of its
 * functions, those the export needs hand-written to compile, each the
 * same in both images (docs/firmware/z3815a-4010.md, "The first wave"),
 * with the static helpers they use.  Its comments' addresses are the
 * Z3815A's, moved from the 58503A's through image_diff's matches. */

/* The clock record, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `services/clock`).  The exported C includes this file after its own,
 * so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype.  It is the record's
 * initialization, its tick and its readers and setters
 * (docs/firmware/structures.md, "The clock record"). */

/* Whether the clock record's checksum is good: clock_record_sum
 * (0x237d0) sums the bytes of +0x02 to +0x32 of the record at A6 and
 * leaves Z set when the word at +0 is the sum's complement and the sum,
 * changing D0, D1, A0 and A1.  clock_init holds the record in A6
 * (0x23c36); here A6 is the frame pointer, so the asm statement saves
 * it around the call. */
static bool clock_record_valid(const clock_record* record) {
  uint8_t valid;

  __asm__ volatile(
      "move.l %%a6,-(%%sp)\n\t"
      "movea.l %[record],%%a6\n\t"
      "jsr clock_record_sum\n\t"
      "movea.l (%%sp)+,%%a6\n\t"
      "seq %[valid]"
      : [valid] "=d"(valid)
      : [record] "a"(record)
      : "d0", "d1", "a0", "a1", "cc", "memory");
  return valid != 0;
}

/* Starts the clock afresh, when forced or its record's checksum fails,
 * at no time on the lifetime record's date; either way marks it not
 * set, so that time_valid gives 0, and clears the pending leap minute
 * and offset step (0x23c32).
 * system_init passes selftest.eeprom. */
void clock_init(int16_t force) {
  /* Days a month, January first, February's 0 for clock_set_leap_year
   * (the immediates at 0x23c4c). */
  static const uint8_t MONTH_DAYS[12] = {31, 0,  31, 30, 31, 30,
                                         31, 31, 30, 31, 30, 31};
  static const uint8_t FEBRUARY = 1;

  if (force != 0 || !clock_record_valid(&clock)) {
    for (size_t month = 0; month < sizeof MONTH_DAYS; month++) {
      clock.month_days[month] = MONTH_DAYS[month];
      clock.gps_month_days[month] = MONTH_DAYS[month];
    }
    clock.uptime = 0;
    clock.gps_time = (time_of_day){.hour = 0};
    clock.gps_date = lifetime.date;
    /* The routine takes the whole date, in D7, and reads its year. */
    clock_set_leap_year((char*)&clock.gps_month_days[FEBRUARY], lifetime.date);
    clock_record_derive(&clock);
    clock.minute_length = CLOCK_SECONDS_PER_MINUTE;
    clock.gps_utc_offset = 0;
    clock.gps_utc_offset_valid = 0;
  }
  clock.set = 0;
  clock_record_checksum(&clock);
  leap_minute_pending = 0;
  offset_step_pending = 0;
  offset_step_time = 0;
  offset_step = 0;
}

/* Derives the clock's local date and time from its GPS time again and
 * checksums its record, in supervisor mode (0x23a88). */
void clock_record_refresh(void) {
  uint16_t sr = cpu_enter_supervisor();

  clock_record_derive(&clock);
  clock_record_checksum(&clock);
  cpu_sr_write(sr);
}
