/* oncore_visible_satellite and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* @@Bb: one visible satellite, padded by the decoder's table
 * (oncore_visibility_field_codes) from 7 bytes to 10. */
typedef struct oncore_visible_satellite {
  uint8_t satellite;
  uint8_t pad_1;
  int16_t doppler;   /* Hz */
  uint8_t elevation; /* degrees */
  uint8_t pad_5;
  uint16_t azimuth; /* degrees */
  oncore_health health;
  uint8_t pad_9;
} oncore_visible_satellite;

/* @@Bb, the visibility message. */
typedef struct oncore_visibility_fields {
  uint8_t count;
  uint8_t pad_1;
  oncore_visible_satellite satellites[12];
} oncore_visibility_fields;
