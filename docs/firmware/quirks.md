# The firmware's own faults

What the Z3805A 3543B image does that its code plainly did not mean,
or that only works by what lies next to it.  The hand-written C
(`third_party/firmware/rewritten/z3805a-3543b/`) does each the same
where C can, with a comment where it happens, and says where it
cannot; none of them is fixed, since the C is a reading of the image.
The harness runs its cases of each, not every input or caller's
state.  Each is by the routine and the address that shows it.

## Wrong results

- **`days_to_date` (`0x10e72`)** takes the year as the count's
  quarter-days over 1461, which reaches the next year a day early: a
  leap year's December 31 comes out as the next year's January 0.
- **`gps_to_utc` (`0x11580`)** takes the GPS-UTC offset as 0 while
  `get_gps_utc_offset` says it is valid, the test at `0x115a6` the
  other way round from `utc_to_gps`'s, so that it uses the offset only
  while it is not valid.
- **`crc16_with_sum` (`0x1213a`)** tests the byte's own low bit, never
  shifted, against the CRC's in each of its 8 steps, so that the CRC
  is not the CCITT one; it returns the bytes' sum, inverted and plain,
  above the CRC.  No call or table in the image names it.
- **`duart_op0_set` (`0x1dfe6`) and its kin** pass 6 to
  `set_interrupt_mask`, which keeps only the status register's bits 10
  to 8, so the mask they set is level 0, not 6.
- **`powers_of_ten_double` (`0x12614`)** holds 1e23 an ulp above the
  nearest double (`0x44b52d02c7e14af7`, not `...af6`; 1e23 lies half
  way between them, and rounding to even takes `...af6`), and
  `powers_of_ten_times_five` (`0x1234c`) 5e22 the same, its mantissa
  1e23's.
- **`float_raise` (`0x5e93c`)** picks its report's address, an operand
  of the math library's or its caller's caller's return, by the
  routine's bit 7, the math library's, but for two: a signaling NaN
  whose cause enables its report tests the cause's bit 7, precision
  lost (`0x5ec24`), and an underflow whose inexact report is enabled
  tests the result's high long, kept where the routine was
  (`0x5ebe0`).  A float's infinity and its quieted NaN return D1 as it
  was, the cause in its low word and its caller's in its high
  (`0x5e948`).  Its report pushes the address before the code
  (`0x5ec8c`), which `float_exception_report` prints as its code; its
  prototype takes both.
- **The installer's `:DIAGnostic:ERASe` (`installer::0x107e6e`) and
  its flash word programmer (`installer::0x107c18`)** count their
  tries and report failure, -200, when the count reaches five
  (`installer::0x107f4c`, `installer::0x107ca6`), not when the fifth
  try fails, so that a fifth try that succeeds is reported as failed.

## Overruns

- **The installer's `:DIAGnostic:DOWNload` (`installer::0x107cb8`)**
  copies its string parameter into 512 bytes of its frame unbounded
  (`installer::0x107ccc`), so that a text of 512 characters or more
  runs into the 256 words it then converts the digits into, and one of
  1024 or more over its saved frame pointer and return address.  The C
  copies it the same way, into a 512-byte array; the harness's texts
  are short.
- **`gps_debug_status` (`0x49210`)** cuts the last comma from its last
  channel's text by clearing the byte before the text's end
  (`0x49384`); with debug output off `debug_sprintf` leaves the text
  empty, and the byte cleared is the one below the buffer, the lowest
  of the A4 its `movem` saved (`0x49214`), which it then returns to
  `oncore_position_message` (`0x4f662`).  The C clears `part[-1]`,
  which gcc's frame puts elsewhere (`KNOWN`).

## Reads of what nothing set

- **`task_stack_info`** reads two locals it never sets ("no system
  stack found", `0x16c28` to `0x16c32`); the C reads them from the
  frame too, though gcc puts them in each other's slots; the function
  check fills the stack report's frames with one segment's address, so
  that both slots hold it in both copies (`function_check.py`,
  `TASK_STACKS`).
- **`task_stack_report`** prints the kernel's supervisor stack's use
  from D2 when its words are all zero (`0x16e06`): the last printed
  task's supervisor stack's use, or, with none printed, D2 as its
  caller left it.
- **`os_report_process`**, when it finds no stack segment (`0x1635e`),
  prints D5 as a task's stack share (`0x1643e`), which
  `os_report_processes` leaves holding the address of `"%s"`
  (`0x16768`), and sums memory onto its caller's D6 (`0x1632e`).  The
  hand-written caller need not leave the same there.
- **`interpolator_verify`** returns, on its measurement's failure
  paths, D2's low byte as its caller left it (`0x4054a`); an
  uninitialized calibration returns 0 (`0x40494`).
- **`hmon_task`** tests both 1PPS checks' results through one register,
  D4 (`0x248b6`, `0x24912`): on a check's tick, each its own; between
  them, both the internal check's last; and until the first, 204 ticks
  in, its caller's.
- **`finish_scpi_cmd`** passes each changed hook a byte of its caller's
  D3, which it never sets (`0x430b6`, `0x43114`, `0x4315c`).
- **The six `:STATus` enable and transition setters** take D2's low
  word as the value for a parameter neither decimal nor non-decimal
  (`0x2a588` to `0x2a5a2`), and **`:DIAG:TEST?`** D2's low byte as the
  test for a choice past its list (`0x2bc36`); the parser gives neither.
  So do `:SAMPle:TOGGle`, `:SAMPle:TARRay` and `:FORMat:DATA` with a
  choice past their two (`0x2e3d0`, `0x2e450`, `0x2ee76`), and
  `:SYSTem:COMMunicate:SERial:RECeive:PARity:TYPE` past its four
  (`0x3049c`).
- **`:DIAGnostic:QUERy:RESPonse?`** from a port other than the console
  copies the count of bytes at A3 from A4 to A2, and ends them with a
  NUL, all three registers as its caller left them (`0x2b92c` to
  `0x2b958`).
- **`gps_response_lookup` (`0x4b7cc`)** compares a response's ID with
  each of `oncore_messages`' entries, the seven empty ones too, whose
  `id` is null, so that it reads ROM 0 and 1 for them; `gps_oncore.c`
  does the same.
- **`time_offset_done`** (`0x45a3c`), `:DIAGnostic:TOFFset`'s done hook,
  passes `save_calibration_block` its two stack arguments' low bytes,
  sign-extended (`0x45a42` to `0x45a50`), which `finish_scpi_cmd` does
  not push for a numeric hook (`0x43172`): the low bytes of the D2 and
  D3 it saved (`0x4306a`).  The C's caller need not leave the same.
- **`qspi_queue_setup` (`0x20578`)**, for QSPI device 4,
  `QSPI_UNUSED_WORD`, loads one word and its command (`0x2067a`,
  `0x2067c`) but ends the queue at entry 1 (SPCR2 `0x8100`,
  `0x20690`), so a transfer also sends entry 1's word under entry 1's
  command, chip select included, as the last transfer left them.  No
  caller passes device 4, in this image or any other.
- **`scpi_interpret` (`0x292d6`) and its installer copy** test D3
  for the session's end after a message unit whose handler they did
  not call (`0x29622`), the handler's result left there, or, for the
  first unit, D3 as their caller left it; pass `scpi_context_init` a
  local they never set as the context's `field_1e` (`-0x34(a6)`,
  `0x29352`); and, when a handler ends the session, return the call
  record's `+0x1e`, set only when the port fails (`0x295c6`).  The
  C keeps the record as the image lays it out, smartclock.h's
  `scpi_call`, to its `+0x3a`.
- **`happening_flush` (`0x2f6dc`)**, given a buffer that is neither
  early buffer, replays it in the mode of D2's low byte as its caller
  left it, setting a mode only for those two (`0x2f6f2`, `0x2f6fe`).

The C reads such a register as the image does, first thing in the
function, with `cpu.h`'s `CPU_CALLERS_REGISTER`.

## What depends on where things are

- **`log_erase`**'s page checksum (`0x31382` to `0x31390`) sums the
  entry's bytes, among them the address of its format string,
  `"Log cleared"` (`0x31d2b`, stored at `0x31c5e`), so that a copy whose
  string lies elsewhere checksums differently.

## Loops that stop early

- **`fine` (`0x4538c`)** checks the fit against its readings in a loop
  it leaves after the first either way (`0x45756`), so that only
  reading 1 is checked against the fitted line (`0x45708` to
  `0x4577c`).

## Reads past a table

- **`random_dither` (`0x12af2`)** reads the byte after `dither_table`
  for every `rand` value from 1 to 127: `start_nothing`'s `rts`,
  `0x4e`, so that about one dither in 258 is -78, twice the table's
  largest, 39.
- **`installer_strtol` (`installer::0x1091ac`)** indexes the ctype
  table by a signed character (`installer::0x1091e0`), so that `0x80` to
  `0xfe` read the 127 bytes of code before the table, and `0xff` its EOF
  entry.
- **`clock_days_before_month`'s callers** that take a date index
  `days_before_month` and `days_before_month_leap` (`0x1186c`) by an
  unchecked month (`0x10e16`, `0x10e26`, `0x10f8a`, `0x11000`): 0 reads
  the word before, past 12 the words after.  `days_to_date` makes its
  own month, in range.
- **`scpi_real_to_text` (`0x2553a`)** takes an exponent's hundreds'
  digits from `decimal_digit_pairs` (`0x25c82`) unchecked, so that an
  exponent of 10000 or more reads past the table into the code after
  it, which is not digits: the conversion fails, or, for a width that
  skips the leading-zero checks, gives the code's bytes as digits.
- **`scpi_real_to_text`**, for an exponent of -32768, overflows its
  `divu.w` (`0x2562e`) and takes pair 0 and the word before
  `decimal_digit_pairs`; and, having refused a width not positive as a
  word (`0x25650`), it takes its low byte, signed, for the digits to
  skip (`0x25658`): a positive width whose low byte is negative checks
  bytes above its exponent's digits, in its own frame and its caller's
  (`0x25662`), and copies the whole width (`0x25674`).  The C does the
  first; the second it cannot, and gives 1 for such a width and copies
  the low byte's count, where no caller passes one.
- **`exception_name` (`0x11a2a`)** indexes `exception_names` by a frame's
  vector offset, unchecked below vector 2 and unaligned, as bytes.
- **The status system** indexes `status_negative_conditions`
  (`0x28720`) by any negative condition (`0x28220`, `0x282e2`,
  `0x283a2`), where `status_report_messages` (`0x2bf30`) is checked to
  codes 1 to 22 (`0x2beae`); and `fpga_set_bit` and `fpga_set_field`
  index `fpga_bit_table` (`0x41310`) by any bit (`0x409be`, `0x40a22`),
  the seventh entry on `pll_action_nothing`'s `rts`.  The C's table
  holds six, so that beyond them it need not read what the image's
  does; the harness keeps to the six (`IN_RANGE`).

## What C does not say

- **`int64_divide` and `uint64_divide`**, on a quotient too wide for 32
  bits, return the dividend's low long, as `divs.l` and `divu.l` leave
  it (`0x41170`, `0x41190`); the C does the same.
- **`set_interrupt_mask` (`0x13378`) and its installer copy** return
  the status register trap #7 saved, whose condition codes are their
  caller's.  The C enters supervisor mode first, with `cpu.h`'s
  `cpu_enter_supervisor` and `cpu_installer_enter_supervisor`, asm
  statements, and agrees.  Called as C, as the export declares them,
  `enter_supervisor` and `installer_enter_supervisor` come after gcc's
  copy of the narrow argument, which sets them, so that the low byte of
  the result can differ.
- **`float_raise` (`0x5e93c`) and its installer copy** keep A0 and A1
  for their callers (`movem` at `0x5e940`), which the m68k C
  convention does not, for the soft-float routines that hold a value
  in them across the call.  The C is the body, and `cpu.h`'s
  `CPU_KEEPING_A0_A1` defines `float_raise` as a wrapper of it that
  saves and restores them, and agrees.
- **`duartb_reconfigure` (`0x1dc7e`) and `sci_reconfigure`
  (`0x1fffa`)**, the I/O control functions of DUART channel B and the
  SCI in the kernel's I/O table (`0x5173a`, `0x517a6`), return no error
  in D7 (`0x1dd8a`, `0x2014a`), which `kernel_dispatch_io` returns to
  the trap's caller (`0x14c64`), where the C convention keeps D7.  The C
  is the body, and `cpu.h`'s `CPU_IO_FUNCTION` defines each as a
  wrapper of it that clears D7 after, and agrees.
- **`setjmp` and `longjmp` (`0x5112a`, `0x51100`)** save and restore
  their caller's registers, frame and stack, which C has no names for:
  the C is the image's instructions in an asm statement after gcc's
  `link`, and agrees.  `longjmp` with a value of 0 has trap #1 save the
  status register again rather than restore it (`0x510ec`).  No call or
  table in either image names either.

The harness counts these where they show (`KNOWN` in
`tools/firmware/function_check.py`), or keeps its cases inside a
table (`IN_RANGE`).
