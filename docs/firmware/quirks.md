# The firmware's own faults

What the Z3805A 3543B image does that its code plainly did not mean,
or that only works by what lies next to it.  The hand-written C
(`third_party/firmware/rewritten/z3805a-3543b/`) does each the same
where C can, with a comment where it happens, and says where it
cannot; none of them is fixed, since the C is a reading of the image.
The harness runs its cases of each, not every input or caller's
state.  Each is by the routine and the address that shows it.  The
other images' own faults, where they differ, follow under each image's
name ("The 58503A 3704").

## Wrong results

- **`days_to_date` (`0x10e72`)** takes the year as the count's
  quarter-days over 1461, which reaches the next year a day early: a
  leap year's December 31 comes out as the next year's January 0.
- **`gps_to_utc` (`0x11580`)** takes the GPS-UTC offset as 0 while
  `get_gps_utc_offset` says it is valid, the test at `0x115a6` the
  other way round from `utc_to_gps`'s, so that it uses the offset only
  while it is not valid.
- **The Z3815A 4010's `strtoul` (`0x6f0a6`)** takes an overflow for a
  digit that leaves the value less than it was (`0x6f1c8`), so that one
  that wraps past it, as 805306368 times 10 does, goes unseen; and after
  a minus sign it gives ULONG_MAX with ERANGE for a value past LONG_MAX
  (`0x6f216`), where C negates it.
- **The Z3815A 4010's `aging_fit` (`0x4aa94`)** fits the log curve,
  and a line through few entries, only for more than 100 well weighted
  entries or entries (`0x4ab18`, `0x4ab90`, `0x4ac06`), of a history
  that holds 32, so that it fits a line or the newest entry as a
  constant and its HQ is never a log curve's.
- **The Z3815A 4010's `fit_linear_halves` (`0x4ace8`)**, which nothing
  calls, takes its halves of 16 entries but keeps the 58503A's slope
  over 1024 and middle 32.5 entries before the newest (`0x4ad66`,
  `0x4ad86`), a history of 64's, where halves of 16 need 256 and 16.5.
- **The Z3815A 4010's `n3_lock_pll` (`0x519ce`)** clamps its TI
  average above at 10 us, then compares -10 us with the average's
  negation (`0x52470` to `0x52486`), the test the upper clamp made, so
  that a negative average is never clamped.
- **The Z3815A 4010's `fine_sync_adjust` (`0x4e48e`)** asks for a TI's
  steps of 100 ns only within 90 ns (`0x4e4b6`, `0x4e512`), a bound
  inside the 100 ns it has already passed, so that it always asks for
  one step and gives 1, where the 58503A's asks for up to three and
  gives 4 past 300 ns; and `fine`, which fine-slews for fewer than 1
  step (`0x4f036`), never reaches `PLL_FINE_FINE_SLEW` itself.
- **The Z3815A 4010's `standby_from_unlock` (`0x33f5e`)** counts its calls
  as passes: in n3 lock pll's pll state (`pll.stage` 6,
  `pll.n3_lock_state` 12), each call with `interval_mean` within 4 ns adds one to `standby_from_unlock_count` (`0x1021b6`, `0x33ff2`) and
  the 700th sets `standby_from_unlock_ready` (`0x1021b3`), logging "STBY from UNLOCK"
  under debugging (`0x33fdc`); past 5 ns it clears both, logging
  "UNLOCK from STBY".  `efc_history_update` calls it once a second,
  but so do `kenneth_predicted_get` (`0x4bc52`) on each
  `:KENneth:PREDicted?` outside start-up PLL, the status screen in n3
  lock pll and `output_control_step` and `output_status_update`, so that each query and screen
  advances the count as a second's pass does and the 700 passes come
  sooner.  The C calls it where the image does.
- **The Z3815A 4010's `sci_scpi_session_init` (`0x3c37a`)** gives the
  SCI session's fourth receive error, condition `0x13`, -360
  Communication error, SCSR's IDLE bit (`0x3c396`), which the SCI's
  receive errors never hold: its interrupt keeps OR, FE and PF
  (`0x30010`) and a full ring sets OR (`0x3010c`).  So the SCI session
  reports no communication error, where the SCPI port's reports the
  DUART read's own bit 0.
- **`kernel_init_checked` (`0x15caa`)** compares the kernel's word
  sum from `0x14acc` to `psos_call`, `0x028d24cc`, with
  `kernel_checksum` (`0x14ac8`), `0x028a9b53`, which no image's kernel
  matches, so that it would halt the CPU (`0x15cd2`, `kernel_halt`).
  Trap #6 enters `kernel_init` past it (`0x14ab6`) and nothing refers
  to it, so the check never runs (`psos.md`, "Inside the kernel").
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
  prototype takes both.  It reads its caller's return address through
  the caller's frame pointer only on the branches that may report it,
  so that a caller with no frame is safe on the others: a quiet NaN
  returns at `0x5ec04`, a signaling one whose report is not enabled
  at `0x5ec1e`, before either reads it.  The math library's underflow
  report address is not a long of its own: it overlaps the mantissa,
  `A6+0x18` for a double (`0x5e990`), `A6+0x16` for a float
  (`0x5ea6c`).
- **The installer's `:DIAGnostic:ERASe` (`installer::0x107e6e`) and
  its flash word programmer (`installer::0x107c18`)** count their
  tries and report failure, -200, when the count reaches five
  (`installer::0x107f4c`, `installer::0x107ca6`), not when the fifth
  try fails, so that a fifth try that succeeds is reported as failed.
  The Z3815A 4010's (`installer::0x108086`, `installer::0x107e3e`),
  over another flash part, report the last try's status instead.

- **pForth's `fill` (`0x191fa`)**, `( address count byte -- )`, pops
  the byte and the count, and for a count of 0 or less returns before
  it pops the address (`0x19200`), so that `0 fill` leaves the address
  on the stack; and it counts with `dbf`, which decrements only a word,
  so that a count above 65536 fills ((count - 1) mod 65536) + 1 bytes
  (`0x19206`).  It is
  the image's assembly, which the export keeps as it is.

- **pForth's digit (`0x1995a`)**, which `.` and `u.` print through,
  compares the digit as a signed byte (`cmpi.b #9; bgt`), so that a
  digit of 128 or more takes `'0' + d`; for a `base` of 209 or more a
  digit of 208 comes out a zero byte, which the print loop
  (`0x1996c`) takes for the marker it pushed first: printing stops
  early and the digits above it stay on the data stack.  Read from the
  code, not run.

## Overruns

- **The installer's `:DIAGnostic:DOWNload` (`installer::0x107cb8`)**
  copies its string parameter into 512 bytes of its frame unbounded
  (`installer::0x107ccc`), so that a text of 512 characters or more
  runs into the 256 words it then converts the digits into, and one of
  1024 or more over its saved frame pointer and return address; the
  Z3815A 4010's the same (`installer::0x107eca`, its copy at
  `installer::0x107ede`).  The C copies it the same way, into a 512-byte
  array; the harness's texts are short.
- **`gps_debug_status` (`0x49210`)** cuts the last comma from its last
  channel's text by clearing the byte before the text's end
  (`0x49384`); with debug output off `debug_sprintf` leaves the text
  empty, and the byte cleared is the one below the buffer, the lowest
  of the A4 its `movem` saved (`0x49214`), which it then returns to
  `oncore_position_message` (`0x4f662`).  The C clears `part[-1]`,
  which gcc's frame puts elsewhere (`KNOWN`).

- **The Z3815A 4010's `gps_periodic_send` (`0x57cd8`)** sends a
  setting's query with the byte of the length `gps_request_build`
  gives it (`0x57d52`), so that a query the builder refuses, -1, is
  sent 255 bytes long from `gps_command_message`, which holds 89, and
  runs into `gps_last_program_message`; `gpsm_task` sends only a
  positive length (`0x581a2`).  Only rrs, `int,rrm` and the second zzz
  can be refused, without time RAIM, and `gpsm_task` leaves a setting
  to query only once it has sent it, so the fault needs time RAIM lost in
  between.  The C sends the same (`GPS_PERIODIC_STATES_Z3815A`).

- **The Z3815A 4010's `satellite_status_list` (`0x54c5c`)**, when more
  satellites are not tracked than its list's entries, gathers the rest
  in a frame list of 12 (`0x54ce4`), which more than 12 visible overrun,
  as the 58503A 3704's does; `status_snapshot` asks for twice the
  engine's eight channels.

- **The Z3815A 4010's `gps_pps_check` (`0x368ca`)**, while the reference
  is questionable, sets its gate back to 3 on every good short gate
  (`0x36a04`), which its steps of 2 never take to 10, so that the long
  gate's frequency test never runs then.
- **The Z3815A 4010's `tcode_request` (`0x23d34`)** takes time code
  formats 3 and 4, releasing the port and marking the request pending,
  but `timecode_update` builds and sends nothing for them (`0x242ac`).

- **The Z3815A 4010's `output_control_step` (`0x347e4`)** sets the
  output's inactive cause to 5 as it leaves active for a time deviation
  ("STBY from ACT, time deviation", `0x34a48`), which `:OUTPut:INACtive?`
  reports as NONE, its jump table having no case for 5 (`0x3fe3c`).
- **The Z3815A 4010's `:DIAGnostic:QUERy:RESPonse?` (`0x3ec8e`)**, for
  an echo of 255 characters, puts its NUL a byte past the response
  buffer: `sci_scpi_echo_response`'s first for the SCPI port's, and
  `robin_keyword_code` for the SCI's.
- **The Z3815A 4010's `installer_printf_polled` (`installer::0x103dba`)**
  formats into an 80-byte frame, half `installer_printf`'s, which a
  longer text overruns.

## Reads of what nothing set

- **The Z3815A 4010's `gpsm_task` (`0x57e3c`)** matches a response by
  the word at `0x10170e`, as its length, which nothing in the image
  writes (`0x5832a`), and tests its count of tries, D3, before any
  sentence sets it, as the task's entry left it.

- **The Z3815A 4010's `parameter_record_read` (`0x4528e`)**, when
  the record is good, returns D3 as its caller left it, which it never
  sets (`0x45358`), as the 58503A 3704's does.
  Its installer's `installer_parameter_record_read`
  (`installer::0x108372`) does the same (`installer::0x1083e0`).

- **`ptest_log_unavailable`** (Z3815A 4010, `0x4a37e`) passes
  `log_write` one argument, the EFC average, but an event log entry
  keeps two longs (`log_append`), so its second is the long past the
  argument on the stack: its own return address into its caller,
  `ptest_update` (`0x4a540`).  The C passes the one argument too, and so
  logs gcc's return address there; the function check logs an armed
  test's entry only from `ptest_log_unavailable` itself, where both
  copies' callers are the harness's (`function_check.py`,
  `PTEST_UPDATE_CASES_Z3815A`).
- **`task_stack_info`** reads two locals it never sets ("no system
  stack found", `0x16c28` to `0x16c32`); the C reads them from the
  frame too, though gcc puts them in each other's slots; the function
  check fills the stack report's frames with one segment's address, so
  that both slots hold it in both copies (`function_check.py`,
  `TASK_STACKS`).
- **`fit_quality` (`0x42644`)**, in a mode other than 2, 3 and 4,
  stores as the HQ its frame's local at `-4(A6)`, which nothing set
  (`0x4298e`); `aging_fit` calls it in none.  The C's local is
  volatile, so that it is read from the frame, the same slot as the
  image's, where gcc 13 had read a register.
- **`task_stack_report`** prints the kernel's supervisor stack's use
  from D2 when its words are all zero (`0x16e06`): the last printed
  task's supervisor stack's use, or, with none printed, D2 as its
  caller left it.
- **`os_report_process`**, when it finds no stack segment (`0x1635e`),
  prints D5 as a task's stack share (`0x1643e`), which
  `os_report_processes` leaves holding the address of `"%s"`
  (`0x16768`), and sums memory onto its caller's D6 (`0x1632e`; the
  Z3815A 4010's at `0x2699a`, which its `scpi_interpret`'s session
  reaches).  The hand-written caller need not leave the same there.
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
- **The Z3801A's `:SYSTem:COMMunicate:SERial:RECeive:BITS` and
  `:SBITs` setters**, for a port other than SERial1, return D0 as
  their caller left it (Z3801A 3543 `0x2f288`, `0x2f4a6`), where the
  Z3805A's return -113, Undefined header.
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
- **The Z3815A 4010's `efc_calibration_measure` (`0x52a34`)** calls
  `efc_calibration_done`, a descriptor's done hook, with no arguments
  (`0x52d78`, `0x52e92`), so that the hook takes the D2 and D3 it saved
  (`0x52a38`) as its index and source.
- **The Z3815A 4010's `n3_lock_pll` (`0x519ce`)**, in measure1, compares
  with 1 its frame's TI (`-0x86(a6)`, `0x51d1c`), which only wait2, a
  slew and the pll state set, so that a pass that starts there reads
  what the last call left in the slot.  The C reads its local through a
  volatile pointer, from the frame.
- **The Z3815A 4010's `n3_lock_slew` (`0x517e0`)**, slewed in, prints
  as its stopping TI ("zero_ti") D4, which it sets only on its other
  path (`0x518ac`): what its caller left there.  Nothing calls it.
- **The Z3815A 4010's `es_send` (`0x24684`)** reads the free-running
  count into D0's low word and compares the long (`0x246be`, `0x246ce`),
  its high word what `es_build` left there: on each of its paths 0, above
  the checksum's last byte or `sprintf`'s count, so that the C, which
  takes the count alone, agrees.

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
  entry.  The primary's `atoi` (`0x5ecfa`) does the same with
  `ctype_table` (`0x5ed0a`), reading its own code: `0x90` is a space,
  `0x95` a digit.
- **The SCPI lexer (`0x26ee0` to `0x277d2`) and its installer copy**
  index `scpi_char_classes` by a port's value as a signed word
  (`0x26f02`), and `console_port_read` gives a high-bit character sign
  extended (`0x29c4e`), so that `0x80` to `0xff` read the 128 bytes of
  code before the table: `0xfe` reads `0x4e`, no class, and is an
  invalid character; `0xf8` reads 0, a letter's; and a keyword, which
  compares its class signed (`0x26fc0`), takes `0xf9`, whose byte is
  `0xdf`, as one of its characters.
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
- **`set_interrupt_mask` (`0x13378`), `enter_supervisor_mode`
  (`0x13368`) and their installer copies** return the status register
  trap #7 saved, whose condition codes are their caller's.  No C says
  that an asm statement reads the condition codes, and gcc's code
  before one may set them: gcc 13 loads the mask with `move.l` before
  `enter_supervisor`, setting N for a mask with bit 31 set, and a
  callee-saved register's push sets them too, so that the low byte of
  the result differed by compiler.  The C is each one's body, and
  `cpu.h`'s `CPU_GIVING_CALLERS_CCR` defines the routine as a wrapper
  that reads its caller's condition codes with `move.w %ccr` before
  anything sets them and passes them to the body, which puts them in
  the status register enter_supervisor gives.
- **`float_raise` (`0x5e93c`) and its installer copy** keep A0 and A1
  for their callers (`movem` at `0x5e940`), which the m68k C
  convention does not, for the soft-float routines that hold a value
  in them across the call.  The C is the body, and `cpu.h`'s
  `CPU_KEEPING_A0_A1` defines `float_raise` as a wrapper of it that
  saves and restores them and passes it a pointer to the arguments
  where the caller pushed them, so that it reads only the operands the
  image reads for the cause (none for a code of no cause, two for a
  float's NaN, `0x5ebf8`), and agrees, at the stack's edge too.
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

## The 58503A 3704

- **The Time RAIM commands**
  (`:DIAGnostic:GPSystem:POSition:HOLD:RAIM:STATe`, `:THReshold` and
  their queries) ask of their descriptors' request, `GPS_SCRIPT_TRAIM`
  (`0x62`), read at `0x2a7aa`, `0x2a810`, `0x2a884` and `0x2a8f6`, not
  `@@Bn`, and `gps_message_supported` (`0x50b7c`) decides only `@@Bn`
  and `@@En`, so that they always fail ("Time RAIM" in `gps.md`).  The
  hand-written C (`rewritten/58503a-3704/scpi_diagnostic.c`) asks the
  same.
