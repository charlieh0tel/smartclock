/* string.c's functions that are this image's own: the rest are
 * common/string.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Appends at most n characters of s2 to s1, then a NUL, and returns s1,
 * ANSI C's; with n 0 it does nothing.  The front panel's status line is
 * its one caller (0x52af2) (0x6335e). */
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
