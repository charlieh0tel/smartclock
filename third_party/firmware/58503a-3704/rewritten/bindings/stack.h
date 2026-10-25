/* The task stacks' routines, the primary's and the installer's copies,
 * as C calls them: each a wrapper under the routine's own name and
 * prototype that binds its arguments to the routine's registers and
 * makes the jsr in one asm statement, naming as changed each register
 * the routine changes (softfloat.h).  stack_usage (0x134f2 to 0x1350a)
 * leaves the stack's size, D0 less A1, in D1, steps A1 over the 0xabcd
 * fill and leaves the bytes above it in D0, and changes nothing else;
 * region_find_segment keeps D1 and A2 (the movem at 0x134d0 and
 * 0x134ec) and changes A1, its result; stack_fill keeps D0 and A1 (the
 * movem at 0x13470 and 0x134ca), A1 across its call of
 * region_find_segment (0x13484), and writes the task's stacks.  No C
 * calls the last two: the kernel's start does, through the
 * configuration's start hook (0x53fa2; docs/firmware/structures.md),
 * and stack_fill. */

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
 * (0x134f2). */
__attribute__((always_inline)) static inline int64_t stack_usage(
    uint16_t* bottom, char* top) {
  return STACK_USAGE("stack_usage", bottom, top);
}

/* The installer's copy (installer::0x100e62). */
__attribute__((always_inline)) static inline int64_t installer_stack_usage(
    uint16_t* bottom, char* top) {
  return STACK_USAGE("installer_stack_usage", bottom, top);
}

/* A0, the region, and D0, an address, in, A1, the segment, the
 * result. */
#define STACK_FIND_SEGMENT(routine, region, address)   \
  ({                                                   \
    register void* stack_a0 __asm__("a0") = (region);  \
    register char* stack_d0 __asm__("d0") = (address); \
    register void* stack_a1 __asm__("a1");             \
                                                       \
    __asm__ volatile("jsr " routine                    \
                     : "=r"(stack_a1)                  \
                     : "r"(stack_a0), "r"(stack_d0)    \
                     : "cc", "memory");                \
    stack_a1;                                          \
  })

/* Fills a new task's stacks with 0xabcd, its record at task
 * (0x13470). */
__attribute__((always_inline)) static inline void stack_fill(void* task) {
  register void* a0 __asm__("a0") = task;

  __asm__ volatile("jsr stack_fill" : : "r"(a0) : "cc", "memory");
}

/* The segment of a region that holds address, or 0 (0x134d0). */
__attribute__((always_inline)) static inline void* region_find_segment(
    void* region, char* address) {
  return STACK_FIND_SEGMENT("region_find_segment", region, address);
}

/* The installer's copy (installer::0x100e40). */
__attribute__((always_inline)) static inline void*
installer_region_find_segment(void* region, char* address) {
  return STACK_FIND_SEGMENT("installer_region_find_segment", region, address);
}
