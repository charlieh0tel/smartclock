/* What the status screen's files share (screen.c and each image's own
 * screen.c): the satellite list's elements and the comparison of two
 * strings.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* An element satellite_status_list sorts with satellite_compare: a
 * visible satellite's number and elevation. */
typedef struct screen_satellite_elevation {
  uint8_t satellite;
  uint8_t elevation;
} screen_satellite_elevation;

/* Whether two strings are the same, as format_dms compares in line. */
static bool screen_equal(const char* a, const char* b) {
  while (*a == *b) {
    if (*a == '\0') {
      return true;
    }
    a++;
    b++;
  }
  return false;
}
