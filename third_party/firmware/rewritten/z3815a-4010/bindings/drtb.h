/* DUART channel B's mutexes, the primary's routines, as C calls them
 * (the installer's host port is the SCI, whose mutexes are
 * installer_sci_lock and installer_sci_unlock): each a wrapper under the
 * routine's own name and prototype that binds its argument to D0 and makes the
 * jsr in one asm statement, naming as changed each register the routine changes
 * (softfloat.h).  Each keeps A1 (the move at 0x2dfd4 and 0x2dff0), in
 * which it passes the mutex at 0x103c7a, or for a D0 not 0 the one at
 * 0x103c92, to mutex_release or mutex_acquire, which keep all but D7,
 * their error (mutex.h); so each changes D7, and the mutex.  No C calls them:
 * drtb_write's assembly does (0x2e476, 0x2e48c), and the I/O table
 * (0x5f230, channel B's open, device 3's). */

/* D0, which mutex, in; D7 changed. */
#define DRTB_LOCK_CALL(routine, write)                                        \
  ({                                                                          \
    register int32_t drtb_d0 __asm__("d0") = (write);                         \
                                                                              \
    __asm__ volatile("jsr " routine : : "r"(drtb_d0) : "d7", "cc", "memory"); \
  })

/* Releases channel B's mutex at 0x103c7a, or for write the one at
 * 0x103c92 (0x2dfd4). */
__attribute__((always_inline)) static inline void drtb_unlock(char write) {
  DRTB_LOCK_CALL("drtb_unlock", write);
}

/* Takes channel B's mutex at 0x103c7a, or for write the one at
 * 0x103c92 (0x2dff0). */
__attribute__((always_inline)) static inline void drtb_lock(char write) {
  DRTB_LOCK_CALL("drtb_lock", write);
}
