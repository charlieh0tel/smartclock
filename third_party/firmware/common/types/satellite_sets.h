/* satellite_sets and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* satellite_sets_build's sets, which status_snapshot keeps: GPS
 * satellites as bits prn - 1 of a long, pseudolites as bits prn - 0x21
 * of a byte (satellite_add_to_set).  Tracked means on one of @@Ba's
 * channels; the three bands split those by the channel's tracking mode
 * (oncore_channel_mode), 1 to 4, 5 and 6, and 7 and 8
 * (satellite_sets_build+0x82 to satellite_sets_build+0xa6), and are cut to the
 * satellites not used (satellite_sets_build+0x11c). */
typedef struct satellite_sets {
  uint32_t visible; /* +0x00: visible_satellites' set, from @@Bb */
  uint32_t
      used; /* +0x04: satellites_in_use's GPS set, those used for position */
  uint32_t tracked;            /* +0x08 */
  uint8_t pseudolites_used;    /* +0x0c: satellites_in_use's pseudolite set */
  uint8_t pseudolites_tracked; /* +0x0d */
  uint32_t unused;             /* +0x0e: (visible | tracked) & ~used */
  uint32_t tracked_unused;     /* +0x12: tracked & ~used */
  uint32_t acquiring;          /* +0x16: of tracked_unused, modes 1 to 4 */
  uint32_t synchronized;       /* +0x1a: modes 5 and 6 */
  uint32_t ephemeris;          /* +0x1e: modes 7 and 8 */
  uint8_t pseudolites_tracked_unused; /* +0x22: pseudolites_tracked &
                                         ~pseudolites_used */
  uint8_t pseudolites_acquiring;      /* +0x23: likewise, by mode */
  uint8_t pseudolites_synchronized;   /* +0x24 */
  uint8_t pseudolites_ephemeris;      /* +0x25 */
  uint8_t visible_count;              /* +0x26: visible_satellites' count */
  uint8_t used_count;                 /* +0x27: satellites_in_use's GPS count */
  uint8_t pseudolites_used_count;     /* +0x28: its pseudolite count */
  uint8_t field_29;
} satellite_sets;

/* What satellite_find and the satellite lists are given:
 * gps.visibility_received (gps+0x110) and the @@Bb message after it, as
 * the image passes its address (status_snapshot+0x1ca to status_snapshot+0x204)
 * and its search reads it. */
typedef struct gps_visibility_record {
  uint8_t received; /* +0x00: gps.visibility_received */
  uint8_t field_01;
  oncore_visibility_fields visibility; /* +0x02: gps.visibility */
} gps_visibility_record;

/* A satellite's cell on the screen, at +4 of a satellite_status_list
 * entry: satellite_elevation_azimuth's and satellite_status', the long
 * status_screen gives format_satellite_cell (status_screen+0x524). */
typedef struct satellite_cell {
  satellite_cell_status status;
  uint8_t elevation; /* degrees, for SATELLITE_CELL_POSITION */
  uint16_t azimuth;  /* degrees */
} satellite_cell;
