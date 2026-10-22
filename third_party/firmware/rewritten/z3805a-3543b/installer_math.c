/* The installer's copy of the soft-float library's exception report
 * (`platform/math`; math.c has the primary's).  Written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C"); it
 * keeps its generated prototype. */

/* Builds float_raise's report, "fp <cause>: <routine>", in
 * installer_float_exception_message and stops with
 * installer_fatal_error_with_code, as float_exception_report does;
 * never returns (installer::0x100fc0). */
void installer_float_exception_report(uint32_t code) {
  const uint32_t* frame = __builtin_frame_address(0);

  libc_float_exception_message(installer_float_exception_message, code);
  installer_fatal_error_with_code((char*)installer_float_exception_message,
                                  (uint32_t)__builtin_return_address(0), code,
                                  frame[LIBC_FRAME_LONG_ABOVE_CODE]);
  for (;;) {
  }
}
