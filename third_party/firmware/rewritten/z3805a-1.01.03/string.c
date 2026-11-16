/* From the Z3815A 4010's rewritten/string.c, the Z3805A 1.01.03's own
 * beside common/'s: of its functions, those the Z3805A 1.01.03's code
 * carries identical or relocated (docs/firmware/z3805a-1.01.03.md, "The
 * link list"). Its comments' addresses are the Z3805A 1.01.03's, but where
 * they name another image. Each keeps its generated prototype. */

/* The first place s2 is in s1, s1 itself for an empty s2, NULL for none
 * (0x64234). */
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
