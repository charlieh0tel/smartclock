# The firmware's own faults

What the Z3805A 3543B image does that its code plainly did not mean,
or that only works by what lies next to it.  The hand-written C
(`third_party/firmware/rewritten/z3805a-3543b/`) does each the same,
with a comment where it happens, and the harness proves it; none of
them is fixed, since the C is a reading of the image.  Each is by the
routine and the address that shows it.

## Wrong results

- **`days_to_date` (`0x10e72`)** takes the year as the count's
  quarter-days over 1461, which reaches the next year a day early: a
  leap year's December 31 comes out as the next year's January 0.
- **`gps_to_utc` (`0x11580`)** takes the GPS-UTC offset as 0 while
  `get_gps_utc_offset` says it is valid, the test at `0x115a6` the
  other way round from `utc_to_gps`'s, so that it uses the offset only
  while it is not valid.
- **`crc16_ccitt` (`0x1213a`)** tests the byte's own low bit, never
  shifted, against the CRC's in each of its 8 steps, so that the CRC
  is not the CCITT one.  Nothing calls it.
- **`duart_op0_set` (`0x1dfe6`) and its kin** pass 6 to
  `set_interrupt_mask`, which keeps only the status register's bits 10
  to 8, so the mask they set is level 0, not 6.
- **`powers_of_ten_double` (`0x12614`)** holds 1e23 an ulp high
  (`0x44b52d02c7e14af7`, not `...af6`).

## Reads of what nothing set

- **`task_stack_info`** reads two locals it never sets ("no system
  stack found", `0x16c28` to `0x16c32`); the C reads them from the
  frame too, so that both fault on the harness's filled frame.
- **`os_report_process`** prints D5 as a task's stack share, which
  `os_report_processes` leaves holding the address of `"%s"`
  (`0x16768`), and sums memory onto its caller's D6 (`0x1632e`,
  `0x1643e`).
- **`interpolator_verify`** returns, on its failure paths, D2 as its
  caller left it.
- **`log_erase`** checksums a pointer's bytes.

## Reads past a table

- **`random_dither` (`0x12af2`)** reads the byte after `dither_table`
  for every `rand` value from 1 to 127: `start_nothing`'s `rts`,
  `0x4e`, so that about one dither in 258 is -78, twice the table's
  largest, 39.
- **`clock_days_before_month`'s callers** index `days_before_month`
  and `days_before_month_leap` (`0x1186c`) by an unchecked month: 0
  reads the word before, past 12 the words after.
- **`exception_name` (`0x11a2a`)** indexes `exception_names` by a frame's
  vector offset, unchecked below vector 2 and unaligned, as bytes.
- **The status system** indexes `status_negative_conditions`
  (`0x28720`) and `status_report_messages` (`0x2bf30`) by any
  condition, and `fpga_set_bit` and `fpga_set_field` index
  `fpga_bit_table` (`0x41310`) by any bit.

## What C does not say

- **`int64_divide` and `uint64_divide`**, on a quotient too wide for 32
  bits, return the dividend's low long, as `divs.l` leaves it.
- **`setjmp`** reads its caller's registers.  Nothing calls it or
  `longjmp`: no call, and no table holds either's address.

The harness counts these where they show (`KNOWN` in
`tools/firmware/function_check.py`), or keeps its cases inside a
table (`IN_RANGE`).
