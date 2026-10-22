/* The installer's copies of the soft-float library's exception and its
 * report (`platform/math`; math.c has the primary's), whose bodies
 * libc.h's both share.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Raises a floating-point exception, as float_raise does, on the
 * installer's float_status, float_control and errno
 * (installer::0x108ca8).  installer_float_raise is this, keeping A0 and
 * A1 for its caller, as float_raise does. */
static __attribute__((used, noipa)) uint64_t
installer_float_raise_kept(uint32_t code, ...) {
  const uint32_t callers_d1 = CPU_CALLERS_REGISTER(uint32_t, "d1");
  uint32_t operand[LIBC_RAISE_OPERANDS];
  va_list list;

  va_start(list, code);
  libc_raise_operands(list, operand);
  va_end(list);

  const libc_raise_state state = {&installer_float_status,
                                  installer_float_control, &installer_errno};
  const libc_raised raised =
      libc_float_raise(code, callers_d1, operand, LIBC_CALLERS_RETURN(), state);

  if (raised.report) {
    installer_float_exception_report(code, raised.address);
  }
  return (uint64_t)raised.high << LIBC_WORD_BITS | raised.low;
}

CPU_KEEPING_A0_A1(installer_float_raise, installer_float_raise_kept,
                  LIBC_RAISE_ARGUMENTS);

/* Builds float_raise's report, "fp <cause>: <routine>", in
 * installer_float_exception_message and stops with
 * installer_fatal_error_with_code, as float_exception_report does;
 * never returns (installer::0x100fc0). */
void installer_float_exception_report(uint32_t code, uint32_t address) {
  libc_float_exception_message(installer_float_exception_message, code);
  installer_fatal_error_with_code((char*)installer_float_exception_message,
                                  (uint32_t)__builtin_return_address(0), code,
                                  address);
  for (;;) {
  }
}
