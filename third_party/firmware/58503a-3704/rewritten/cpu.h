/* The MC68331's CPU32 and SIM as the hand-written C uses them, written
 * by hand for the decompiler's C (docs/firmware/README.md, "Hand-written
 * C"): the status register's interrupt levels and the helpers that move
 * to and from it, an exception frame's format and vector word, and the
 * software watchdog's service values.  The exported C includes this
 * file before the hand-written .c files, after its own declarations, so
 * every one of them can use it.  The registers are mc68331.h's. */

/* A byte's bits and its mask, a word's, and a long's bits and bytes. */
static const uint32_t CPU_BYTE_BITS = 8;

static const uint32_t CPU_BYTE_MASK = 0xff;

static const uint32_t CPU_WORD_BITS = 16;

static const uint32_t CPU_WORD_MASK = 0xffff;

static const int32_t CPU_LONG_BITS = 32;

static const int32_t CPU_LONG_BYTES = 4;

/* A shift by a register takes its count modulo 64 (MC68000PRM, LSL). */
static const uint32_t CPU_SHIFT_COUNT_MASK = 0x3f;

/* The status register's interrupt mask, bits 10 to 8, as
 * set_interrupt_mask takes it (0x135a6).  Level 7, all three bits,
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

/* Defines routine, a function the export declares, as a wrapper that
 * keeps A0 and A1 for its caller, which the m68k C convention does not,
 * around body: the image's routine
 * keeps them where an assembly caller holds a value in one across the
 * call (float_raise's movem at 0x62b44, for the soft-float routines).
 * body, a C function of one argument, is given a pointer to the
 * routine's stack arguments where its caller pushed them, past the two
 * kept registers and the return address, so that it reads only those
 * the routine reads, and D1 and A6 as the caller left them, so that it
 * reads the caller's frame as the routine does; its result, in D0:D1,
 * is the routine's. */
#define CPU_KEEPING_A0_A1(routine, body)               \
  __asm__("\t.pushsection .text." #routine             \
          ",\"ax\",@progbits\n"                        \
          "\t.globl " #routine                         \
          "\n"                                         \
          "\t.type " #routine ", @function\n" #routine \
          ":\n"                                        \
          "\tmovem.l %a0/%a1,-(%sp)\n"                 \
          "\tpea (12,%sp)\n"                           \
          "\tjsr " #body                               \
          "\n"                                         \
          "\taddq.l #4,%sp\n"                          \
          "\tmovem.l (%sp)+,%a0/%a1\n"                 \
          "\trts\n"                                    \
          "\t.size " #routine ", .-" #routine          \
          "\n"                                         \
          "\t.popsection\n")

/* Defines routine, a function the export declares void (void), as a
 * wrapper that keeps the scratch registers too, D0, D1, A0 and A1, which
 * the m68k C convention does not, around body, a C function of that
 * prototype: the image's routine keeps every register it uses where an
 * interrupt calls it having saved none (pps_interrupt's movem at
 * 0x12f5e, which the internal 1PPS interrupt at 0x12fb2 calls at
 * 0x12fce). */
#define CPU_KEEPING_SCRATCH(routine, body)             \
  __asm__("\t.pushsection .text." #routine             \
          ",\"ax\",@progbits\n"                        \
          "\t.globl " #routine                         \
          "\n"                                         \
          "\t.type " #routine ", @function\n" #routine \
          ":\n"                                        \
          "\tmovem.l %d0-%d1/%a0-%a1,-(%sp)\n"         \
          "\tjsr " #body                               \
          "\n"                                         \
          "\tmovem.l (%sp)+,%d0-%d1/%a0-%a1\n"         \
          "\trts\n"                                    \
          "\t.size " #routine ", .-" #routine          \
          "\n"                                         \
          "\t.popsection\n")

/* Defines routine, a function the export declares void (void) that the
 * kernel's I/O table names, as body, a C function of that prototype,
 * then D7 0: kernel_dispatch_io (0x14c64) jumps to the table's entry,
 * whose return, through 0x14c64's rte, gives the trap's caller D7 as the
 * I/O function's error, and the image's C driver functions clear it
 * before they return (duartb_reconfigure at 0x1dbe2), where the m68k C
 * convention keeps D7 for the caller. */
#define CPU_IO_FUNCTION(routine, body)                 \
  __asm__("\t.pushsection .text." #routine             \
          ",\"ax\",@progbits\n"                        \
          "\t.globl " #routine                         \
          "\n"                                         \
          "\t.type " #routine ", @function\n" #routine \
          ":\n"                                        \
          "\tjsr " #body                               \
          "\n"                                         \
          "\tmoveq #0,%d7\n"                           \
          "\trts\n"                                    \
          "\t.size " #routine ", .-" #routine          \
          "\n"                                         \
          "\t.popsection\n")

/* Defines routine, a function the export declares, as a wrapper that
 * reads its caller's condition codes before anything sets them and
 * passes them to body, with a pointer to the routine's stack arguments
 * where its caller pushed them, past the return address: body is
 * uint16_t (uint32_t callers_ccr, const uint32_t* arguments), and its
 * result is the routine's.  For a routine that returns the status
 * register trap #7 saved, whose condition codes are its caller's
 * (enter_supervisor_mode, set_interrupt_mask): in C, gcc's code before
 * an asm statement may set them, a callee-saved register's push or an
 * argument's load, and no constraint says that the statement reads
 * them (docs/firmware/quirks.md, "What C does not say").  The wrapper's
 * pushes are on the stack it was called on, and body returns there:
 * body enters supervisor mode and moves back a status register of its
 * caller's mode before it returns. */
#define CPU_GIVING_CALLERS_CCR(routine, body)          \
  __asm__("\t.pushsection .text." #routine             \
          ",\"ax\",@progbits\n"                        \
          "\t.globl " #routine                         \
          "\n"                                         \
          "\t.type " #routine ", @function\n" #routine \
          ":\n"                                        \
          "\tpea (4,%sp)\n"                            \
          "\tmove.w %ccr,-(%sp)\n"                     \
          "\tclr.w -(%sp)\n"                           \
          "\tjsr " #body                               \
          "\n"                                         \
          "\taddq.l #8,%sp\n"                          \
          "\trts\n"                                    \
          "\t.size " #routine ", .-" #routine          \
          "\n"                                         \
          "\t.popsection\n")

/* enter_supervisor (0x1363a), called in an asm statement: from a task
 * in user mode it returns, through trap #7's rte (0x13632), on the
 * supervisor stack.  It pops its return address into A0 and returns the
 * status register as it was in D0's low word, the rest of D0 as it found
 * it, its condition codes those at the call, which gcc's code before it
 * may have set (CPU_GIVING_CALLERS_CCR).  CPU_ENTER_SUPERVISOR calls
 * routine, the primary's or the installer's copy (installer::0x100f38),
 * so. */
#define CPU_ENTER_SUPERVISOR(routine)               \
  ({                                                \
    register uint32_t cpu_entered_sr __asm__("d0"); \
    __asm__ volatile("jsr " routine                 \
                     : "=r"(cpu_entered_sr)         \
                     :                              \
                     : "a0", "memory", "cc");       \
    cpu_entered_sr;                                 \
  })

static inline uint32_t cpu_enter_supervisor(void) {
  return CPU_ENTER_SUPERVISOR("enter_supervisor");
}

static inline uint32_t cpu_installer_enter_supervisor(void) {
  return CPU_ENTER_SUPERVISOR("installer_enter_supervisor");
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
 * (the CPU32's exception stack frame). */
static const uint16_t CPU_VECTOR_FORMAT_MASK = 0xf000;

static const uint16_t CPU_VECTOR_OFFSET_MASK = 0x0fff;

/* exception_names and installer_exception_names name the vectors from 2,
 * bus error, to 58 (offset 0xe8), by an exception frame's vector
 * offset. */
static const uint16_t CPU_VECTOR_OFFSET_FIRST_NAMED = 2 * 4;

static const uint16_t CPU_VECTOR_OFFSET_LAST_NAMED = 0xe8;

/* The status register's low byte, the condition code register. */
static const uint16_t CPU_SR_CCR_MASK = 0x00ff;

/* The status register enter_supervisor gave, entered_sr, with the
 * condition codes at the call of the function it was entered for,
 * callers_ccr, which CPU_GIVING_CALLERS_CCR gives. */
static uint16_t cpu_sr_at_call(uint16_t entered_sr, uint32_t callers_ccr) {
  return (entered_sr & ~CPU_SR_CCR_MASK) | (callers_ccr & CPU_SR_CCR_MASK);
}

/* Masks every interrupt, from sr, the status register as it was at the
 * call: enter_supervisor_mode's and its installer copy's body; returns
 * sr. */
static uint16_t cpu_interrupts_mask_all(uint16_t sr) {
  cpu_sr_write(sr | CPU_IPL_7);
  return sr;
}

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
  count &= CPU_SHIFT_COUNT_MASK;
  return count < CPU_LONG_BITS ? (uint32_t)1 << count : 0;
}

/* Adds 1 to the long at counter in one instruction, addq.l #1 to
 * memory, as root_task's idle loop does (0x133ee): an interrupt comes
 * before it or after it, never between its read and its write, so that
 * a clear another task makes meanwhile, klok_take_pending's, is kept.
 * gcc's increment of a volatile is a load, an add and a store, between
 * which the clear is lost. */
static inline void cpu_long_increment(volatile uint32_t* counter) {
  __asm__ volatile("addq.l #1,%0" : "+m"(*counter) : : "cc");
}

/* The SIM's software watchdog is serviced by writing these two to SWSR
 * in turn (MC68331UM, "Software Watchdog"; D.3.15). */
static const uint8_t CPU_SWSR_FIRST = 0x55;
static const uint8_t CPU_SWSR_SECOND = 0xaa;

/* A register as the function's caller left it: the image's code reads
 * some it never sets (docs/firmware/quirks.md, "Reads of what nothing
 * set").  Use it first in the function, before gcc's code uses the
 * register: CPU_CALLERS_REGISTER(uint32_t, "d3").  Volatile, since
 * without inputs two such statements with one template are the same
 * expression to gcc, which then reads only the first's register for
 * both. */
#define CPU_CALLERS_REGISTER(type, reg)             \
  ({                                                \
    register type cpu_callers_value __asm__(reg);   \
    __asm__ volatile("" : "=r"(cpu_callers_value)); \
    cpu_callers_value;                              \
  })

/* The index'th long of the stack arguments where a caller would have
 * pushed them, for code that reads arguments its callers do not push
 * (docs/firmware/quirks.md): above the frame pointer's saved copy and
 * the return address, which -fno-omit-frame-pointer keeps. */
#define CPU_CALLERS_STACK_LONG(index) \
  (((volatile uint32_t*)__builtin_frame_address(0))[2 + (index)])
