/* The MC68331's CPU32 and SIM as the hand-written C uses them, written
 * by hand for the decompiler's C (docs/firmware/README.md, "Hand-written
 * C"): the status register's interrupt levels and the helpers that move
 * to and from it, an exception frame's format and vector word, and the
 * software watchdog's service values.  The exported C includes this
 * file before the hand-written .c files, after its own declarations, so
 * every one of them can use it.  The registers are mc68331.h's. */

/* The status register's interrupt mask, bits 10 to 8, as
 * set_interrupt_mask takes it (0x1338e).  Level 7, all three bits,
 * masks every interrupt but the nonmaskable. */
enum {
  CPU_IPL_1 = 0x100,
  CPU_IPL_2 = 0x200,
  CPU_IPL_3 = 0x300,
  CPU_IPL_4 = 0x400,
  CPU_IPL_6 = 0x600,
  CPU_IPL_7 = 0x700,
  /* what the output-port routines pass, meaning level 6 but taken as
   * level 0, so that they leave interrupts enabled */
  CPU_IPL_6_UNSHIFTED = 6,
};

/* The status register, which C has no name for: the image's critical
 * sections save it, change its interrupt mask in place with ori and
 * andi, and move the saved word back, all in supervisor mode.  The
 * decompiler's C leaves these moves out. */
static inline uint16_t cpu_sr_read(void) {
  uint16_t sr;

  __asm__ volatile("move.w %%sr,%0" : "=d"(sr) : : "memory");
  return sr;
}

static inline void cpu_sr_write(uint16_t sr) {
  __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory", "cc");
}

/* enter_supervisor (0x13422), called directly: from a task in user
 * mode it returns, through trap #7's rte (0x1341a), on the supervisor
 * stack, so its bridge, which saves gcc's registers on the user stack
 * and restores them from the stack it returns on, cannot call it.  It
 * pops its return address into A0 and returns the status register as
 * it was in D0's low word, the rest of D0 as it found it. */
static inline uint32_t cpu_enter_supervisor(void) {
  register uint32_t sr __asm__("d0");

  __asm__ volatile("jsr enter_supervisor" : "=r"(sr) : : "a0", "memory", "cc");
  return sr;
}

/* Raises the interrupt mask by ORing bits into it; returns the status
 * register as it was. */
static inline uint16_t cpu_interrupts_raise(uint16_t mask) {
  uint16_t saved = cpu_sr_read();

  cpu_sr_write(saved | mask);
  return saved;
}

/* Sets the interrupt mask to a level, as ori and then andi do; returns
 * the status register as it was. */
static inline uint16_t cpu_interrupts_at(uint16_t level) {
  uint16_t saved = cpu_interrupts_raise(level);

  cpu_sr_write(cpu_sr_read() & ~(CPU_IPL_7 & ~level));
  return saved;
}

/* An exception frame's format and vector word: the format in the top
 * four bits, the vector's offset, 4 times its number, in the rest
 * (the CPU32's exception stack frame); debug.c's exception_name masks the
 * offset. */
static const uint16_t CPU_VECTOR_FORMAT_MASK = 0xf000;

/* Moves saved, the status register as it was, back with its interrupt
 * mask, bits 10 to 8, mask's: set_interrupt_mask's and its installer
 * copy's body; returns saved. */
static uint16_t cpu_interrupt_mask_set(uint16_t saved, uint16_t mask) {
  cpu_sr_write((saved & ~CPU_IPL_7) | (mask & CPU_IPL_7));
  return saved;
}

/* 1 shifted left by count as lsl.l shifts by a register: the count
 * modulo 64, and the bit shifted out from 32 on (MC68000PRM, LSL). */
static uint32_t cpu_long_bit(uint32_t count) {
  static const uint32_t COUNT_MASK = 0x3f;
  static const uint32_t LONG_BITS = 32;

  count &= COUNT_MASK;
  return count < LONG_BITS ? (uint32_t)1 << count : 0;
}

/* The SIM's software watchdog is serviced by writing these two to SWSR
 * in turn (MC68331UM, "Software Watchdog"; D.3.15). */
static const uint8_t CPU_SWSR_FIRST = 0x55;
static const uint8_t CPU_SWSR_SECOND = 0xaa;

/* A register as the function's caller left it: the image's code reads
 * some it never sets (docs/firmware/quirks.md, "Reads of what nothing
 * set").  Use it first in the function, before gcc's code uses the
 * register: CPU_CALLERS_REGISTER(uint32_t, "d3"). */
#define CPU_CALLERS_REGISTER(type, reg)           \
  ({                                              \
    register type cpu_callers_value __asm__(reg); \
    __asm__("" : "=r"(cpu_callers_value));        \
    cpu_callers_value;                            \
  })

/* The index'th long of the stack arguments where a caller would have
 * pushed them, for code that reads arguments its callers do not push
 * (docs/firmware/quirks.md): above the frame pointer's saved copy and
 * the return address, which -fno-omit-frame-pointer keeps. */
#define CPU_CALLERS_STACK_LONG(index) \
  (((volatile uint32_t*)__builtin_frame_address(0))[2 + (index)])
