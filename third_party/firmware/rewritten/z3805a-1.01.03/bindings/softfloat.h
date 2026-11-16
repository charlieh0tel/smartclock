/* double_divide_pointer's binding, as the 58503B 1.01.04's: the other
 * images' copies of the divide have its entry and no caller, this image's
 * efc_calibration_fit_linear calls it (0x489e4);
 * common/bindings/softfloat.h has the float library's other routines and
 * SOFTFLOAT_PAIR_AND_POINTER. */

/* D0:D1 / *A1 (0x62aca). */
__attribute__((always_inline)) static inline double double_divide_pointer(
    double a, double* b) {
  return SOFTFLOAT_PAIR_AND_POINTER("double_divide_pointer", a, b);
}
