/* What the SCPI :SOURce:PTIMe tree's files share (scpi_source_ptime.c
 * and each image's own scpi_source_ptime.c): a date's or time's response
 * items and the time code's formats.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* A date's or time of day's response items. */
typedef struct scpi_source_ptime_fields {
  scpi_short_item first;  /* +0x00: the year, or the hour */
  scpi_short_item second; /* +0x04: the month, or the minute */
  scpi_short_item third;  /* +0x08: the day, or the second */
} scpi_source_ptime_fields;

/* A packed date, year << 16 | month << 8 | day (calendar_date), as
 * response items. */
static void scpi_source_ptime_date_items(uint32_t date,
                                         scpi_source_ptime_fields* fields) {
  static const uint32_t YEAR_SHIFT = 16;
  static const uint32_t MONTH_SHIFT = 8;

  fields->first.value = (uint16_t)(date >> YEAR_SHIFT);
  fields->second.value = (date >> MONTH_SHIFT) & CPU_BYTE_MASK;
  fields->third.value = date & CPU_BYTE_MASK;
}

/* :PTIMe:TCODe:FORMat's keywords, by their indexes in its list
 * (scpi_choices_set_SOURce_PTIMe_TCODe_FORMat_1): F1 and F2, the manual's T1
 * and T2 (097-z3801-01, page 4-12). */
enum {
  SCPI_SOURCE_PTIME_F1 = 0,
  SCPI_SOURCE_PTIME_F2 = 1,
};
