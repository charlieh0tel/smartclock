/* What the kernel's binding shares with the installer's copy of it
 * (os.c, installer_os.c).  The exported C includes the hand-written
 * headers after its own declarations and before the hand-written .c
 * files, so every one of them can use it. */

/* A task's supervisor and user stack sizes, in words, as spawn takes
 * them in D3: a word each, the supervisor's high (psos.md, service 1). */
static uint32_t os_stack_sizes(uint32_t supervisor, uint32_t user) {
  static const uint32_t WORD_BITS = 16;
  static const uint32_t WORD_MASK = 0xffff;

  return supervisor << WORD_BITS | (user & WORD_MASK);
}
