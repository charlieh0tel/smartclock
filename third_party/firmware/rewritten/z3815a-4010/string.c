/* strstr (`platform/string`), the image's own: the other images' C
 * library has none; common/string.c has the rest of the strings.  Written
 * by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); it keeps its generated prototype. */

/* The first place s2 is in s1, s1 itself for an empty s2, NULL for none
 * (0x6eec0). */
char* strstr(char* s1, char* s2) {
  for (char* start = s1;; start++) {
    char* a = start;
    char* b = s2;

    for (; *b != '\0' && *a == *b; a++, b++) {
    }
    if (*b == '\0') {
      return start;
    }
    if (*a == '\0') {
      return NULL;
    }
  }
}
