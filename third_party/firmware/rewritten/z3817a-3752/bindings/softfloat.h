/* double_divide_pointer's binding, this image's own: the other images'
 * copies of the divide have its entry and no caller, this image's 0x4d8f8
 * calls it (0x4da20); common/bindings/softfloat.h has the float
 * library's other routines and SOFTFLOAT_PAIR_AND_POINTER. */

/* D0:D1 / *A1 (0x65bec). */
__attribute__((always_inline)) static inline double double_divide_pointer(
    double a, double* b) {
  return SOFTFLOAT_PAIR_AND_POINTER("double_divide_pointer", a, b);
}
