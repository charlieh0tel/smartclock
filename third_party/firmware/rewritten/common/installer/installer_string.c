/* The installer's copies of the string routines (`platform/string`;
 * string.c has the primary's), whose bodies, libc.h's, both share.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Copies n bytes. */
void* installer_memcpy(void* s1, void* s2, uint32_t n) {
  return libc_memcpy(s1, s2, n);
}

/* Copies at most n characters and pads with NULs. */
char* installer_strncpy(char* s1, char* s2, uint32_t n) {
  return libc_strncpy(s1, s2, n);
}

/* A lower-case ASCII letter in upper case. */
int32_t installer_toupper(int32_t c) { return libc_toupper(c); }
