/* efc_range_state and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* Whether the EFC is at or near the end of its range, at efc_range:
 * efc_clamp and hmon_check_efc report each change as a status
 * happening, and efc_clamp_reset clears all three. */
typedef struct efc_range_state {
  uint8_t clamped;  /* +0x00: efc_clamp clamped it to 0 or 1048560, happening
                       STATUS_EFC_AT_LIMIT_SET; cleared with _CLEARED */
  uint8_t near_end; /* +0x01: "EFC near end of range", happening
                       STATUS_EFC_NEAR_LIMIT_SET; cleared with _CLEARED */
  uint8_t near_end_checks; /* +0x02: hmon_check_efc's consecutive checks
                              outside 52428 to 996132, held at 5 */
} efc_range_state;
