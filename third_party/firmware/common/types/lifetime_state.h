/* lifetime_state and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The lifetime record in RAM, at lifetime: what lifetime_record_read
 * loads from EEPROM 0x400060 and lifetime_record_write saves there,
 * the EEPROM's order being the date, the count, then the EFC. */
typedef struct lifetime_state {
  float efc; /* +0x00: the EFC lifetime_update saves in normal PLL, mid-scale
                524288 by default; pll_init starts from it */
  uint32_t date; /* +0x04: year << 16 | month << 8 | day, 1994-01-01 by
                    default; clock_init takes the date from it */
  int32_t count; /* +0x08: one more for each new date lifetime_update is given;
                    :DIAGnostic:LIFetime:COUNt? returns it */
} lifetime_state;
