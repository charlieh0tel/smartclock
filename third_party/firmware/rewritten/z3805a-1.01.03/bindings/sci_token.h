/* The 58503B 1.01.04's bindings/sci_token.h, copied for the Z3805A
 * 1.01.03 less the installer's copies, which this image's installer, the
 * Z3805A 3543B's, does not have (docs/firmware/z3805a-1.01.03.md, "The
 * export and the checks"); its comments' addresses are the Z3805A
 * 1.01.03's.  The 58503B's note follows. */

/* The SCI's token routines as C calls them: each a wrapper under the
 * routine's own name and prototype that binds its argument to D0 and
 * makes the jsr in one asm statement, naming as changed each register
 * the routine changes (softfloat.h).  Each saves what it uses but D7,
 * its result (the movem at 0x1fce4 and 0x1fdd2), and its kernel calls,
 * ident, request_x and send_x, change nothing it does not save
 * (docs/firmware/psos.md, "Services"); each writes the SCI's state. */

/* D0, which token, in, D7, an error, the result. */
#define SCI_TOKEN_CALL(routine, write)                     \
  ({                                                       \
    register int32_t sci_token_d0 __asm__("d0") = (write); \
    register char sci_token_d7 __asm__("d7");              \
                                                           \
    __asm__ volatile("jsr " routine                        \
                     : "=r"(sci_token_d7)                  \
                     : "r"(sci_token_d0)                   \
                     : "cc", "memory");                    \
    sci_token_d7;                                          \
  })

/* Gives back the token for write, the write token for 1, or drops a
 * nested hold (0x1fce4). */
__attribute__((always_inline)) static inline char sci_unlock(char write) {
  return SCI_TOKEN_CALL("sci_unlock", write);
}

/* Takes the token for write, the write token for 1, else the read one,
 * or counts a nested hold (0x1fdd2). */
__attribute__((always_inline)) static inline char sci_lock(char write) {
  return SCI_TOKEN_CALL("sci_lock", write);
}
