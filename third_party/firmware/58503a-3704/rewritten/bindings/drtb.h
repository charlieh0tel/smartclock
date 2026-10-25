/* DUART channel B's mutexes, the primary's and the installer's
 * routines, as C calls them: each a wrapper under the routine's own name
 * and prototype that binds its argument to D0 and makes the jsr in one
 * asm statement, naming as changed each register the routine changes
 * (softfloat.h).  Each keeps A1 (the move at 0x1d758 and 0x1d774), in
 * which it passes the mutex at 0x10342e, or for a D0 not 0 the one at
 * 0x103446, to mutex_release or mutex_acquire, which keep all but D7,
 * their error (mutex.h); so each changes D7, and the mutex.  No C calls them:
 * drtb_write's assembly does (0x1dc02, 0x1dc18), and the I/O table
 * (0x54426). */

/* D0, which mutex, in; D7 changed. */
#define DRTB_LOCK_CALL(routine, write)                                        \
  ({                                                                          \
    register int32_t drtb_d0 __asm__("d0") = (write);                         \
                                                                              \
    __asm__ volatile("jsr " routine : : "r"(drtb_d0) : "d7", "cc", "memory"); \
  })

/* Releases channel B's mutex at 0x10342e, or for write the one at
 * 0x103446 (0x1d758). */
__attribute__((always_inline)) static inline void drtb_unlock(char write) {
  DRTB_LOCK_CALL("drtb_unlock", write);
}

/* Takes channel B's mutex at 0x10342e, or for write the one at
 * 0x103446 (0x1d774). */
__attribute__((always_inline)) static inline void drtb_lock(char write) {
  DRTB_LOCK_CALL("drtb_lock", write);
}

/* The installer's copies, which call the installer's mutexes. */

/* drtb_unlock's copy (installer::0x10337a). */
__attribute__((always_inline)) static inline void installer_drtb_unlock(
    char write) {
  DRTB_LOCK_CALL("installer_drtb_unlock", write);
}

/* drtb_lock's copy (installer::0x103396). */
__attribute__((always_inline)) static inline void installer_drtb_lock(
    char write) {
  DRTB_LOCK_CALL("installer_drtb_lock", write);
}
