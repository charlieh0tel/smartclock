/* pForth's code copier as C calls it: a wrapper under the routine's
 * own name and prototype that binds its arguments to the routine's
 * registers and makes the jsr in one asm statement, naming as changed
 * each register the routine changes (softfloat.h).  forth_compile_code
 * (0x19338 to 0x1934c) steps A0 over the runs it copies and A4 over the
 * dictionary, keeping D0 and the condition codes on the stack and
 * leaving with rtr. */

/* Copies runs of code words, each ended by a zero, from code into the
 * dictionary at dp, up to an empty run; the dictionary's new end, on
 * the last zero (0x19338). */
__attribute__((always_inline)) static inline void* forth_compile_code(
    uint16_t* code, void* dp) {
  register uint16_t* a0 __asm__("a0") = code;
  register void* a4 __asm__("a4") = dp;

  __asm__ volatile("jsr forth_compile_code" : "+r"(a0), "+r"(a4) : : "memory");
  return a4;
}
