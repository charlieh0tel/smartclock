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

/* What exception_hook_per_task holds while set, and what
 * exception_hook_set returns when a hook was set before (0x118b6,
 * 0x118d8). */
static const uint8_t OS_SWITCH_ON = 0xff;

/* mutex_owner's and its installer copy's body: the task holding a
 * mutex. */
static uint32_t os_mutex_owner(void* lock) { return ((mutex*)lock)->owner; }

/* busy_wait's and its installer copy's body: spins 729 empty loops for
 * each count, the count a word once it is not 0. */
static void os_busy_wait(uint32_t count) {
  /* busy_wait's empty loops for each count (0x13432). */
  static const uint16_t BUSY_WAIT_SPINS = 0x2d9;

  if (count == 0) {
    return;
  }
  uint16_t remaining = count;

  do {
    for (uint16_t spin = BUSY_WAIT_SPINS; spin != 0; spin--) {
      __asm__ volatile("");
    }
    remaining--;
  } while (remaining != 0);
}
