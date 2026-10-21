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
 * (the CPU32's exception stack frame); os.c's exception_name masks the
 * offset. */
static const uint16_t CPU_VECTOR_FORMAT_MASK = 0xf000;

/* The SIM's software watchdog is serviced by writing these two to SWSR
 * in turn (MC68331UM, "Software Watchdog"; D.3.15). */
static const uint8_t CPU_SWSR_FIRST = 0x55;
static const uint8_t CPU_SWSR_SECOND = 0xaa;
