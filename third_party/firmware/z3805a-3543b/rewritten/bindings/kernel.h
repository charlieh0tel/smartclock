/* The kernel's checks of an object's id and its tick count, the
 * primary's and the installer's copies, as C calls them: each a wrapper
 * under the routine's own name and prototype that binds its arguments
 * to the routine's registers and makes the jsr in one asm statement,
 * naming as changed each register the routine changes (softfloat.h).
 * No C calls them: the kernel's services do (kernel_start's at 0x14ea2,
 * kernel_suspend's at 0x15006 and the like; docs/firmware/psos.md,
 * "Inside the kernel").
 *
 * What each changes is read from its code: kernel_check_task and
 * kernel_check_exchange (0x15b3a, 0x15b6c) compare in D6 and set D7 to
 * an error, leaving it as it was for an id that checks, and
 * kernel_check_task gives the running task's record, A5, in A0 for an id
 * of 0 (0x15b68), A0 as it was for another; kernel_check_segment
 * (0x15b96) masks the segment's mark in D7, leaving 0 or the error;
 * tick_account (0x16eb6) keeps A5, which it loads with the kernel's
 * data area, and task_count_tick, which it branches to for a mode of 1
 * or more, keeps A2 (0x16ee6), so that it changes no register: its
 * branch for a mode of 1 alone, which changes A6 (0x16ed6), is never
 * taken, bpl having taken a mode of 1 first (0x16eba).  tick_account
 * writes the kernel's counts; each statement changes memory, as every
 * binding's does. */

/* A0, the id, and A5, the running task, in, A0, the record, the result;
 * D6 and D7 changed. */
#define KERNEL_CHECK_TASK(routine, id, running)         \
  ({                                                    \
    register int32_t kernel_a0 __asm__("a0") = (id);    \
    register void* kernel_a5 __asm__("a5") = (running); \
                                                        \
    __asm__ volatile("jsr " routine                     \
                     : "+r"(kernel_a0)                  \
                     : "r"(kernel_a5)                   \
                     : "d6", "d7", "cc", "memory");     \
    (void*)kernel_a0;                                   \
  })

/* A0, the id, and A5, the running task, in; D6 and D7 changed. */
#define KERNEL_CHECK_EXCHANGE(routine, id, running)     \
  ({                                                    \
    register int32_t kernel_a0 __asm__("a0") = (id);    \
    register void* kernel_a5 __asm__("a5") = (running); \
                                                        \
    __asm__ volatile("jsr " routine                     \
                     :                                  \
                     : "r"(kernel_a0), "r"(kernel_a5)   \
                     : "d6", "d7", "cc", "memory");     \
  })

/* A0, the segment, in; D7 changed. */
#define KERNEL_CHECK_SEGMENT(routine, segment)          \
  ({                                                    \
    register void* kernel_a0 __asm__("a0") = (segment); \
                                                        \
    __asm__ volatile("jsr " routine                     \
                     :                                  \
                     : "r"(kernel_a0)                   \
                     : "d7", "cc", "memory");           \
  })

/* Nothing in, D7, no error, the result. */
#define KERNEL_SERVICE_NONE(routine)                                       \
  ({                                                                       \
    register char kernel_d7 __asm__("d7");                                 \
                                                                           \
    __asm__ volatile("jsr " routine : "=r"(kernel_d7) : : "cc", "memory"); \
    kernel_d7;                                                             \
  })

/* The task the id names, the running task's for 0; D7 its error if it
 * names none, one deleted or another group's (0x15b3a). */
__attribute__((always_inline)) static inline void* kernel_check_task(
    int32_t task, void* running) {
  return KERNEL_CHECK_TASK("kernel_check_task", task, running);
}

/* Checks the exchange the id names; D7 its error if it names none, one
 * deleted or another group's (0x15b6c). */
__attribute__((always_inline)) static inline void kernel_check_exchange(
    int32_t exchange, void* running) {
  KERNEL_CHECK_EXCHANGE("kernel_check_exchange", exchange, running);
}

/* Checks the segment's mark, kernel_region_allocate's; D7 0 or its
 * error (0x15b96). */
__attribute__((always_inline)) static inline void kernel_check_segment(
    void* segment) {
  KERNEL_CHECK_SEGMENT("kernel_check_segment", segment);
}

/* Services 11 to 15, 17 and 34: no error (0x1537a). */
__attribute__((always_inline)) static inline char kernel_service_none(void) {
  return KERNEL_SERVICE_NONE("kernel_service_none");
}

/* Counts a tick by the mode in D6: to the kernel's idle count below 1,
 * else to the running task (0x16eb6). */
__attribute__((always_inline)) static inline void tick_account(int16_t mode) {
  register int32_t d6 __asm__("d6") = mode;

  __asm__ volatile("jsr tick_account" : : "r"(d6) : "cc", "memory");
}

/* The installer's copies, byte for byte the primary's
 * (installer::0x101d80 on), change what the primary's do. */

/* kernel_check_task's copy (installer::0x102540). */
__attribute__((always_inline)) static inline void* installer_kernel_check_task(
    int32_t task, void* running) {
  return KERNEL_CHECK_TASK("installer_kernel_check_task", task, running);
}

/* kernel_check_exchange's copy (installer::0x102572). */
__attribute__((always_inline)) static inline void
installer_kernel_check_exchange(int32_t exchange, void* running) {
  KERNEL_CHECK_EXCHANGE("installer_kernel_check_exchange", exchange, running);
}

/* kernel_check_segment's copy (installer::0x10259c). */
__attribute__((always_inline)) static inline void
installer_kernel_check_segment(void* segment) {
  KERNEL_CHECK_SEGMENT("installer_kernel_check_segment", segment);
}

/* kernel_service_none's copy (installer::0x101d80). */
__attribute__((always_inline)) static inline char installer_kernel_service_none(
    void) {
  return KERNEL_SERVICE_NONE("installer_kernel_service_none");
}
