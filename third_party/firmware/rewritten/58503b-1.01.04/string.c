/* From the 58503A 3704's rewritten/string.c, the 58503B 1.01.04's own beside
 * common/'s: of its functions, those the 58503B's code carries identical or
 * relocated (docs/firmware/58503b-1.01.04.md, "The link list").  Its
 * comments' addresses are the 58503B's, but where they name another image.
 * Each keeps its generated prototype. */

/* Appends at most n characters of s2 to s1, then a NUL, and returns s1,
 * ANSI C's; with n 0 it does nothing.  The front panel's status line is
 * its one caller (0x5a734) (0x6a45c). */
char* strncat(char* s1, char* s2, uint32_t n) {
  if (n == 0) {
    return s1;
  }
  char* end = s1;

  while (*end != '\0') {
    end++;
  }
  while ((*end++ = *s2++) != '\0') {
    if (--n == 0) {
      *end = '\0';
      return s1;
    }
  }
  return s1;
}
