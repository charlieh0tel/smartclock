/* The C library's strings, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `platform/string`).  The exported C includes this file after its own,
 * so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype. */

/* The first difference of two blocks' bytes, taken as signed (0x63116). */
int32_t memcmp(void* s1, void* s2, uint32_t n) {
  int8_t* a = s1;
  int8_t* b = s2;

  if (a == b) {
    return 0;
  }
  for (; n != 0; n--, a++, b++) {
    int32_t difference = *a - *b;

    if (difference != 0) {
      return difference;
    }
  }
  return 0;
}

/* Copies n bytes (0x6314c). */
void* memcpy(void* s1, void* s2, uint32_t n) { return libc_memcpy(s1, s2, n); }

/* Fills n bytes (0x6316a). */
void* memset(void* s, int32_t c, uint32_t n) {
  uint8_t* to = s;

  for (; n != 0; n--) {
    *to++ = c;
  }
  return s;
}

/* Appends s2 to s1 (0x6332c). */
char* strcat(char* s1, char* s2) {
  char* end = s1;

  while (*end != '\0') {
    end++;
  }
  while ((*end++ = *s2++) != '\0') {
  }
  return s1;
}

/* The first c in s, its NUL among them (0x63344). */
char* strchr(char* s, int32_t c) {
  do {
    if (*s == (char)c) {
      return s;
    }
  } while (*s++ != '\0');
  return NULL;
}

/* Compares at most n characters, as signed (0x6338c). */
int32_t strncmp(char* s1, char* s2, uint32_t n) {
  for (; n != 0 && *s1 == *s2; n--, s1++, s2++) {
    if (*s1 == '\0') {
      return 0;
    }
  }
  if (n == 0) {
    return 0;
  }
  return *s1 - *s2;
}

/* Copies at most n characters and pads with NULs (0x633c2). */
char* strncpy(char* s1, char* s2, uint32_t n) {
  return libc_strncpy(s1, s2, n);
}

/* A lower-case ASCII letter in upper case (0x633f0). */
int32_t toupper(int32_t c) { return libc_toupper(c); }

/* A string in upper case, in place, indexed by a signed byte, so that
 * past 127 characters it wraps to before the string (0x2e362). */
void string_upper(char* text) {
  for (int8_t i = 0; text[i] != '\0'; i++) {
    text[i] = toupper(text[i]);
  }
}

/* Advances a cursor past spaces, storing it at each step (0x2e39e). */
void skip_spaces(char** cursor) {
  char* text = *cursor;

  while (*text == ' ') {
    *cursor = ++text;
  }
}

/* Clears a string's trailing spaces; like the image, it does not stop
 * at the string's start, so that one all spaces clears the spaces
 * before it too (0x2e3b6). */
void string_trim_right(char** text) {
  char* end = *text;

  while (*end != '\0') {
    end++;
  }
  while (*--end == ' ') {
    *end = '\0';
  }
}
