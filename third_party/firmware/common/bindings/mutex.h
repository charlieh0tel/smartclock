/* The mutex routines, the primary's and the
 * installer's copies, as C calls them: each a wrapper under the
 * routine's own name and prototype that binds its arguments to the
 * routine's registers and makes the jsr in one asm statement, naming as
 * changed each register the routine changes (softfloat.h).
 *
 * What each changes is read from its code: mutex_init, mutex_acquire
 * and mutex_release save what they use but D7, their result (the movem
 * at mutex_init, mutex_acquire and mutex_release), and the kernel services they
 * call through psos_call, ident, attach exchange, create_x, request_x and
 * send_x (the moveq to D7 before each jsr), change nothing they do not
 * save (os.h, the services' shapes); mutex_get_owner changes only A0,
 * its result, keeping D0 by exg, and mutex_clear saves what
 * it uses.  Each writes the mutex, and the kernel's state, so
 * each statement changes memory. */

/* A1, the mutex, in, D7, an error, the result. */
#define MUTEX_CALL(routine, lock)                   \
  ({                                                \
    register void* mutex_a1 __asm__("a1") = (lock); \
    register int32_t mutex_d7 __asm__("d7");        \
                                                    \
    __asm__ volatile("jsr " routine                 \
                     : "=r"(mutex_d7)               \
                     : "r"(mutex_a1)                \
                     : "cc", "memory");             \
    mutex_d7;                                       \
  })

/* D0, the name, and A1, the mutex, in, D7, an error, the result. */
#define MUTEX_INIT(routine, name, lock)               \
  ({                                                  \
    register int32_t mutex_d0 __asm__("d0") = (name); \
    register mutex* mutex_a1 __asm__("a1") = (lock);  \
    register int32_t mutex_d7 __asm__("d7");          \
                                                      \
    __asm__ volatile("jsr " routine                   \
                     : "=r"(mutex_d7)                 \
                     : "r"(mutex_d0), "r"(mutex_a1)   \
                     : "cc", "memory");               \
    mutex_d7;                                         \
  })

/* A1, the mutex, in, A0, its owner, the result. */
#define MUTEX_GET_OWNER(routine, lock)               \
  ({                                                 \
    register mutex* mutex_a1 __asm__("a1") = (lock); \
    register uint32_t mutex_a0 __asm__("a0");        \
                                                     \
    __asm__ volatile("jsr " routine                  \
                     : "=r"(mutex_a0)                \
                     : "r"(mutex_a1)                 \
                     : "cc", "memory");              \
    mutex_a0;                                        \
  })

/* A1, the mutex, in, nothing changed but the mutex. */
#define MUTEX_CLEAR(routine, lock)                                       \
  ({                                                                     \
    register mutex* mutex_a1 __asm__("a1") = (lock);                     \
                                                                         \
    __asm__ volatile("jsr " routine : : "r"(mutex_a1) : "cc", "memory"); \
  })

/* Creates the mutex's exchange for name and clears the mutex. */
__attribute__((always_inline)) static inline int32_t mutex_init(int32_t name,
                                                                mutex* lock) {
  return MUTEX_INIT("mutex_init", name, lock);
}

/* Takes the mutex, or counts a nested hold. */
__attribute__((always_inline)) static inline int32_t mutex_acquire(
    mutex* lock) {
  return MUTEX_CALL("mutex_acquire", lock);
}

/* Drops a nested hold, or gives the mutex back. */
__attribute__((always_inline)) static inline int32_t mutex_release(
    mutex* lock) {
  return MUTEX_CALL("mutex_release", lock);
}

/* The task holding the mutex. */
__attribute__((always_inline)) static inline uint32_t mutex_get_owner(
    mutex* lock) {
  return MUTEX_GET_OWNER("mutex_get_owner", lock);
}

/* Clears the mutex's six longs. */
__attribute__((always_inline)) static inline void mutex_clear(mutex* lock) {
  MUTEX_CLEAR("mutex_clear", lock);
}

/* The installer's copies, byte for byte the primary's but for their
 * calls of the installer's binding (installer_mutex_init on), change
 * what the primary's do. */

/* mutex_init's copy. */
__attribute__((always_inline)) static inline int32_t installer_mutex_init(
    int32_t name, mutex* lock) {
  return MUTEX_INIT("installer_mutex_init", name, lock);
}

/* mutex_acquire's copy. */
__attribute__((always_inline)) static inline int32_t installer_mutex_acquire(
    void* lock) {
  return MUTEX_CALL("installer_mutex_acquire", lock);
}

/* mutex_release's copy. */
__attribute__((always_inline)) static inline int32_t installer_mutex_release(
    void* lock) {
  return MUTEX_CALL("installer_mutex_release", lock);
}

/* mutex_get_owner's copy. */
__attribute__((always_inline)) static inline uint32_t installer_mutex_get_owner(
    mutex* lock) {
  return MUTEX_GET_OWNER("installer_mutex_get_owner", lock);
}

/* mutex_clear's copy. */
__attribute__((always_inline)) static inline void installer_mutex_clear(
    mutex* lock) {
  MUTEX_CLEAR("installer_mutex_clear", lock);
}
