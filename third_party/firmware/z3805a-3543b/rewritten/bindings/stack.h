/* The stack measure, the primary's and the installer's copy, as C calls
 * it: a wrapper under the routine's own name and prototype that binds
 * its arguments to the routine's registers and makes the jsr in one asm
 * statement, naming as changed each register the routine changes
 * (softfloat.h).  stack_usage (0x132da to 0x132f2) leaves the stack's
 * size, D0 less A1, in D1, steps A1 over the 0xabcd fill and leaves the
 * bytes above it in D0, and changes nothing else. */

/* The bits of the result's low long, D1, below D0's. */
static const uint32_t STACK_LOW_BITS = 32;

/* A1, the stack's bottom, and D0, its top, in, D0:D1 the result. */
#define STACK_USAGE(routine, bottom, top)                             \
  ({                                                                  \
    register uint16_t* stack_a1 __asm__("a1") = (bottom);             \
    register uint32_t stack_d0 __asm__("d0") = (uint32_t)(top);       \
    register uint32_t stack_d1 __asm__("d1");                         \
                                                                      \
    __asm__ volatile("jsr " routine                                   \
                     : "+r"(stack_d0), "=r"(stack_d1), "+r"(stack_a1) \
                     :                                                \
                     : "cc", "memory");                               \
    (int64_t)((uint64_t)stack_d0 << STACK_LOW_BITS | stack_d1);       \
  })

/* The bytes of a task's stack above its fill, high, and its size, low
 * (0x132da). */
__attribute__((always_inline)) static inline int64_t stack_usage(
    uint16_t* bottom, char* top) {
  return STACK_USAGE("stack_usage", bottom, top);
}

/* The installer's copy (installer::0x100e64). */
__attribute__((always_inline)) static inline int64_t installer_stack_usage(
    uint16_t* bottom, char* top) {
  return STACK_USAGE("installer_stack_usage", bottom, top);
}
