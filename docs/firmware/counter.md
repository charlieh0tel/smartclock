# The time-interval counter

How the Z3805A 3543B (`z3805a-3543b.bin`) drives the FPGA's
time-interval counter: the registers it writes and reads, the order it
writes them in each second, how it waits for a reading and turns it
into seconds, and what the loop does with the result.  `interval.md`
covers the same routines in the Z3816A image at a coarser grain; this
file is for the emulator's model of the counter
(`crates/smartclock-emu/src/board/fpga.rs`).

Addresses are in the Z3805A image.  Function names are those of
`third_party/firmware/z3805a-3543b/names.tsv`, the code is the
hand-written C in `third_party/firmware/z3805a-3543b/rewritten/`
(`ti.c`, `fpga.c`, `pll_reading.c`, `pll_stages.c`, `monitors.c`) and
the types are `smartclock.h`'s; each was checked against the image's
own instructions with `m68k-linux-gnu-objdump -D -b binary -m
m68k:cpu32`.  Every claim is **read** from the code unless it is marked
**inferred**.  The firmware only writes registers and reads bits; what
the FPGA's design does between them is never in the code, so every
statement about which edge starts or stops the count is an inference.

## The register port

Chip select 5's block puts the FPGA's port at `0x304000`
(`README.md`, the SIM table).  Four registers are written there:
`fpga_write` (`0x40756`) stores a byte at `0x304000 + reg`, reg 0 to 3.
The firmware keeps their images in `fconf_regs` (`0x101fa9`, four
bytes) and changes a field through `fpga_bit_table` (`0x41310`, read
from the image):

| Entry | `fpga_bit_index` | Register | Mask | Kind | Written by |
| ----- | ---------------- | -------- | ---- | ---- | ---------- |
| 0 | `FPGA_BIT_MODE` | 0 | `0x03` | field | `counter_arm` 0, `interpolator_prepare` 3 |
| 1 | `FPGA_BIT_PRESET` | 0 | `0x40` | flag | `counter_arm`, `interpolator_measure`, `counter_selftest` |
| 2 | `FPGA_BIT_START` | 1 | `0x01` | flag | `interpolator_measure` pulses it; `counter_arm` clears it |
| 3 | `FPGA_BIT_REFERENCE` | 1 | `0x02` | flag | `interpolator_measure`: 0 low, 1 high |
| 4 | `FPGA_BIT_ADDRESS` | 1 | `0xf8` | field | `fpga_read`, `counter_arm` |
| 5 | `FPGA_BIT_SYNC` | 3 | `0x03` | field | `counter_set_sync` |

`fpga_set_bit` (`0x409a4`) sets a flag's mask for any nonzero value
(the callers pass 8 for the preset) and clears it for 0;
`fpga_set_field` (`0x40a0e`) clears a field's mask and ORs the value in,
already shifted.  Each call writes the whole register.

A read returns one register, the one register 1's address field
selects.  `fpga_read` (`0x4076a`) puts `reg << 3` into register 1's
top five bits and writes register 1, reads the byte at `0x304000`, then
writes register 1 back with its old address bits.  So every read is two
writes of register 1, with its start and reference bits as they stand.
The registers read (`smartclock.h`, `fpga_register`):

| Address | What | Read by |
| ------- | ---- | ------- |
| `0x00` | status: bit 0 interpolated, bit 1 counted, bits 4 to 7 seconds modulo 16 | `counter_wait_ready`, `fpga_read_seconds` |
| `0x10`, `0x11` | the interpolator's byte, low nibble, high nibble, complemented | `interpolator_read` |
| `0x12`, `0x13` | the count's low byte, low nibble, high nibble | `counter_read_raw` |
| `0x14`, `0x15` | the count's middle byte | `counter_read_raw` |
| `0x16`, `0x17` | the count's high byte | `counter_read_raw` |

A byte is two reads: `fpga_read_byte` (`0x40ba6`) reads reg + 1 first,
then reg, and returns `(reg & 0x0f) | (reg + 1) << 4`.  Only the low
nibble of each read is used.

What is never written, read from every writer of `0x304000` in the
image (the decompiled C's `fpga_write` calls: `fpga_set_bit`,
`fpga_set_field`, `fpga_read`, `counter_set_sync`, the two register
loads, `fpga_address_sweep_debug`):

- Register 2 is only written from `counter_configuration.fpga_registers[2]`,
  which no field reaches, so it is always 0.
- Register 3's bits 2 to 7 are always 0.
- The mode field takes only 0 (count, `FPGA_MODE_COUNT`) and 3 (the
  interpolator's references, `FPGA_MODE_REFERENCES`): `fpga_mode`.
- Status bits 2 and 3 are never tested: `counter_wait_ready`'s callers
  pass 1 and 2 only; `fpga_bit_test` (`0x407d4`) is called by nothing.

## The registers at start

`counter_configuration_init` (`0x40ed8`) builds the field values
`{0, 8, 0, 1, 0, 0}` (the preset set, `TI_PRESET_ON`, and the high
reference, `INTERPOLATOR_REFERENCE_HIGH`) into both
`counter_configuration.fpga_registers` and `fconf_regs`:

| Register | Value |
| -------- | ----- |
| 0 | `0x40`: preset, mode 0 |
| 1 | `0x02`: reference high, address 0, start clear |
| 2 | `0x00` |
| 3 | `0x00`: no sync |

It also sets the calibration to 0.4000003 s (below), the interpolator
scale to 200, `calibrate_mode` to `COUNTER_CALIBRATE_ALWAYS` (3), and
both work states to `COUNTER_WORK_WANTED` (2); `counter_init`
(`0x40f76`) calls it and arms the reading.
The registers reach the FPGA two ways: `counter_load_registers`
(`0x40ea2`), from `counter_reset` (`0x24976`), which `pllp_task`
(`0x47528`) runs at power-up and `pll_check_reference` (`0x46188`)
runs after an invalid first reading, both after reloading the FPGA
(`fpga_setup`, `0x22d70`); and the first `counter_arm`, which loads them
while `registers_state` is `COUNTER_WORK_WANTED` (2) and sets it to
`COUNTER_WORK_DONE` (1).

## Each second

The loop takes one reading a pass, through `interval_next_reading`
(`0x44fbc`): `interval_wait_reading` (`0x4513c`), then
`interval_rearm` (`0x450c4`).  Some stages call the two halves
separately; the order is the same.

1. **Wait and read.**  With both 1 PPS alive, `interval_wait_reading`
   calls `counter_read` (`0x40d34`).  With either dead
   (`gps_pps_dead` `0x101b6e`, `internal_pps_dead` `0x101b6f`) it does
   not touch the FPGA: it sets `interval_tick_wait` and waits for event
   `0x200`, which the periodic interrupt (`primary_user_66`,
   `0x11f82`, 1024 ticks a second) signals when `ticks_since_pps`
   reaches `0x199`, 409 ticks or 0.3994 s after the last
   `pps_interrupt`; the reading is then invalid.
2. **Rearm.**  `interval_rearm` sets `interval_reading.armed`
   (`0x101fa8`), clears its errors, then by its `interval_arm_mode`:
   `INTERVAL_ARM_CALIBRATE` (0) calibrates the interpolator
   (`counter_calibrate`, below), `INTERVAL_ARM_PLAIN` (1) does nothing
   more, `INTERVAL_ARM_SYNC` (2) asks for a full sync
   (`sync_request(COUNTER_SYNC_FULL, 1)`); then calls `counter_arm`
   (`0x40c6a`).

`counter_arm`'s writes, in order (read from `0x40c6a`):

| # | Register | Value | Why |
| - | -------- | ----- | --- |
| 1 | 0 to 3 | the configuration's | only while `registers_state` is `COUNTER_WORK_WANTED` |
| 2 | 0 | `\| 0x40` | preset on |
| 3 | 0 | mode 0 | count |
| 4 | 1 | start clear | |
| 5 | 1 | address 0 | the status register |
| 6 | 3 | sync field 0 | only while a sync is wanted |
| 7 | 3 | sync field = kind | the same |
| 8 | 0 | `& ~0x40` | preset off |

So between arms register 0 reads `0x00` (mode 0, no preset) and the
count runs; register 1 keeps whichever reference the last
interpolator measurement left, low or high.  `counter_arm` does
nothing at all while the reading carries an error: a failed
calibration (below) leaves the counter unarmed for that second.

**Inferred:** the preset clears both ready bits.  `counter_read` runs
right after the arm with nothing else between, and waits for the
counted bit; if the bit survived the arm, every reading would be the
last one again.  `counter_selftest` (`0x40f98`) sets the preset and
reads the count as exactly `0x555555`, so the preset also holds the
count there (read); it leaves the preset on, which the next arm turns
off.

## The ready bits and the wait

`counter_read` waits for status bit 1 (counted), then bit 0
(interpolated), each with `counter_wait_ready` (`0x4091a`):

- It reads the status register once, then loops while neither 1 PPS
  is dead, the reading is armed and polls remain: if the bit is set it
  returns 1; else `psos_pause(10)` (`0x17076`, 10 ticks, 9.8 ms),
  one poll fewer, read again.  It starts with 512 polls, so it gives
  up after 5 s.
- On running out of polls, or with either 1 PPS dead, it clears
  `armed`.  It returns 0 then.

A failed wait sets errors 1 and 8 (`COUNTER_GATE_FAILURE`,
`COUNTER_MEASUREMENT_ABORTED`): the reading becomes 9.91e37
(`0x47d2a37dced46143`), its flag 0, and `counter_read` prints "Gate
failure" and "Measurement aborted".  `interval_wait_reading` then
raises happening `'H'` unless the reading was armed with a 1 PPS dead.

For the model: the firmware sees a ready bit within 10 ticks of its
setting, needs it within 5 s of the arm, and, since the count also
needs both 1 PPS alive, the two monitors must pass first
(`gps_pps_check` `0x245a6`, `internal_pps_check` `0x2471a`).

The status register's high nibble is a count of seconds modulo 16:
`fpga_read_seconds` (`0x407fa`) reads it until two reads agree.
`pllp_task` stores it in `pll.reference_seconds` each pass, and after
a warm restart `clock_set_from_reference` (`0x46290`) takes the
seconds elapsed from it, 0 to 59, to set the clock.  **Inferred:** it
counts the internal 1 PPS, and the FPGA keeps counting across a warm
restart (the firmware reloads it at power-up, and again only when
`pll_check_reference` finds its first reading invalid and calls
`counter_reset`, `0x46222`, before the clock is set from it); which
1 PPS it counts is not in the code.

## The reading

`counter_read`, once both bits are set (read from `0x40d34`):

    count        = counter_read_raw()                      24-bit count − 0x555555
    interpolated = interpolator_read()                     ~byte at 0x10/0x11
    fraction     = (interpolated − offset) / scale         offset, scale: counter_calibrate's
    reading      = (count + fraction) × 1e−7 − 0.4000003 + :DIAGnostic:TOFFset

- `counter_read_raw` (`0x40c06`) shifts in the bytes at `0x16`, `0x14`,
  `0x12`, high to low, zero-extended, and `counter_offset_apply`
  (`0x40bda`) subtracts the 64-bit `0x555555` at `0x412f8` in 64 bits,
  so a count below the preset is negative.
- `interpolator_read` (`0x40c56`) complements `fpga_read_byte(0x10)`;
  `counter_read` keeps the low byte.  The FPGA returns the reading
  complemented (read; the emulator's model complements it).
- The fraction is `double_divide_reversed` (`0x5da6a`, A0:A1 / D0:D1):
  `(interpolated − offset)` in A0:A1 over the scale in D0:D1.
  `offset` and `scale` are `counter_configuration` `+0x18` and `+0x1c`.
- `1e−7` is the immediate `0x3e7ad7f29abcaf48` at `0x40dda`: the count
  is of 100 ns ticks, a 10 MHz clock.
- The calibration is the double at `counter_configuration + 4`.
- `:DIAGnostic:TOFFset` is the float at `0x101d10`, converted and added.
- The tag is `0xfff6` (`0x40fd6`), and `valid` is set.

So one interpolator count is 1/200 of a tick, 0.5 ns, and the reading
spans the interval with the preset removed exactly: the preset does not
enter the result.

## The interpolator's calibration

`counter_calibrate` (`0x405bc`) runs whenever `interval_rearm` is given
`INTERVAL_ARM_CALIBRATE`, because `calibrate_mode` is
`COUNTER_CALIBRATE_ALWAYS`, before `counter_arm`.
It tries `interpolator_calibrate` (`0x40248`) up to 20 times, falling
back on `interpolator_verify` (`0x4047c`), and on success stores
`offset = reading_low` and `scale = reading_high − reading_low`; on
failure `scale = 200`, `offset = 0` and error
`COUNTER_CALIBRATION_FAILURE` (`0x10`), "Interpolator calibration
failure", which makes the reading that follows invalid.

`interpolator_prepare` (`0x401c4`) sets the preset, mode 3, start
clear, address 0.  Each measurement, `interpolator_measure`
(`0x40030`, read from the image):

| # | Register | Bit | |
| - | -------- | --- | - |
| 1 | 1 | reference = 0 low, 1 high | |
| 2 | 0 | preset on | |
| 3 | 1 | start on | |
| 4 | 0 | preset off | |
| 5 | 1 | start off | |

then it waits for status bit 0 alone and reads the interpolator.  The
hex DAC's slots 2 and 3 (`hdac_set`, `0x1f53a`) trim the low and high
references; the search wants the low reading in 1 to 25, the high in
201 to 225, and exactly 200 apart (199 or 201 accepted after 20
nudges).

**Inferred:** the two references are one tick, 100 ns, apart.  The
fraction is added to a count of ticks before the count is scaled by
1e−7, so the scale it is divided by, the spread between the
references, must be the interpolator's reading of one tick.  The
offset puts the fraction at 0 at the low reference; readings run to
the byte's 255, a fraction up to about 1.2.

## Which 1 PPS edges

**Read:**

- **The internal 1 PPS** reaches the GPT's input capture 1, rising
  edges (`gpt_init`'s TCTL2 `0x05`, `startup.h`).  Its interrupt,
  `primary_user_81` (`0x12e04`, vector 81, ICR's base `0x50` plus 1),
  clears ICF1, counts `internal_pps_count` (`0x101b70`), and calls
  `pps_interrupt` (`0x12db6`) while the internal 1 PPS is alive.  The
  periodic interrupt calls `pps_interrupt` itself every 1024 ticks while
  it is dead.
- **The GPS engine's 1 PPS** reaches the GPT's pulse accumulator input,
  PAI: `gps_pps_check` sets PACTL to `0x50` (PAEN, PEDGE: count rising
  edges) and counts PACNT, wanting 1 to 3 pulses a check and 8 to 12 a
  ten-second gate.
- **No external 1 PPS.**  No field selects a 1 PPS input: the table
  has six fields, register 2 is always 0, and register 3's sync field
  only steps or syncs.  The log table names `EXT lock started` and two
  EXT holdovers (`log.c`), but no code logs them.  The 58503A 3704 does
  write a seventh field (`58503a-3704.md`); this image has none.
- **The count needs both.**  `counter_wait_ready` gives up when either
  1 PPS is dead, and `interval_wait_reading` does not read the counter
  then.
- **Inferred:** the FPGA makes the internal 1 PPS from the 10 MHz
  oscillator.  Its monitor checks only while `pll.fpga_loaded` is set
  and no FPGA load is running (`monitors_pps_checkable`).

**Inferred: what starts and stops the count.**  Four observations:

1. With the interval at zero the count, less its preset, is
   0.4000003 s / 100 ns = 4,000,003 ticks, so the count runs about
   0.4 s, counting up from the preset (a count down would read near
   −0.8 s).
2. There is one interpolator, and its fraction of a tick is added.  So
   one edge is asynchronous to the 10 MHz clock and interpolated; the
   other is synchronous and needs none.  An edge the FPGA makes from
   its own divider is synchronous; the GPS engine's is not.
3. With a 1 PPS dead the loop waits until 0.3994 s after the last
   `pps_interrupt`, normally the internal 1 PPS, in place of a reading:
   the pace a reading keeps when it completes about 0.4 s after the
   internal 1 PPS.  The same `0x199` is in all seven images.
4. The other images' calibrations (the immediate before the
   `move.l #200` of each `counter_configuration_init`): 0.4000003 s in
   the Z3801A 3543, the Z3805A 3543B and both 58503A images; 0.400000204 s
   in the 58503B; 0.3932162 s in the Z3815A and Z3816A, which is
   `0x3c0000 + 2` ticks.  Each is a round count, decimal or binary, plus
   about two or three ticks.

The reading these fit best: **the GPS 1 PPS starts the count and the
FPGA's divider stops it 4,000,000 ticks after its own internal 1 PPS**,
with three ticks of fixed delay in the path.  The count is then

    count = 0.4 s + 300 ns + (t_internal − t_GPS)

and the interpolator measures from the GPS edge to the clock edge the
count starts on.  The other order, a divider tap 0.6 s into the second
starting the count and the GPS 1 PPS stopping it, gives the same
arithmetic, but its reading would be ready right after the 1 PPS, not
at 0.4 s, and the Z3816A's tap would be at 6,067,840, not a round
count.  Neither is excluded by the code.  The bitstream
(`outputs.md`, "The FPGA bitstreams") would settle it.

The emulator's `fpga.rs` and `docs/emulator.md` say the GPS 1 PPS stops
the count; under this reading it starts it.  What the firmware needs
from the model is the same either way (below).

## The 0.4000003 s calibration

Read: it is an immediate, `0x3fd9999a`, `0xdbb8f8da` at `0x40f2c` in
`counter_configuration_init`, the only writer of
`counter_configuration + 4`.  Nothing computes it, and no other
writer was found: the routines given `counter_configuration` write its
sync, calibration and register states and the interpolator's offset
and scale, not `+4`.  `:DIAGnostic:TOFFset` is a separate term, and the
EEPROM block at `0x400080` holds the time offset and TC
(`calibration_block_read`, `0x30c1e`), not this.

How it follows from the rest (inferred):

- The preset `0x555555` (5,592,405) cancels exactly in
  `counter_offset_apply`, so it is not part of the calibration.  A
  count of 0.4 s ends at `0x925e58`; the 24-bit counter would wrap
  11,184,811 ticks, 1.118 s, after the preset, past any one-second
  count.  `0x555555`'s alternating bits are a test pattern for
  `counter_selftest`'s read through each nibble.
- The 10 MHz clock turns ticks into seconds: 4,000,003 × 100 ns.
- The interpolator's scale, 200 counts a tick, and its offset, the low
  reference, make the fraction 0 to 1 tick; they absorb the
  interpolator's own delay, so the calibration need not.
- That leaves 4,000,000 ticks, the span from one 1 PPS to the divider's
  stop, and 3 ticks of fixed delay: the synchronizer or start logic
  between the GPS edge and the first counted clock edge, or the stop's
  pipeline.  Which, the code cannot say.

## The sign, and the loop

The manuals: `:SYNChronization:TINTerval?` "Returns the filtered GPS
1 PPS to internal oscillator 1 PPS interval" (097-z3801-01, Table 4-2,
page 4-8); "the difference or timing shift between the SmartClock
1 PPS and the GPS 1 PPS signals" (097-59551-02, page 5-25).  Under the
edges above, the reading is

    TI = t_internal − t_GPS + :DIAGnostic:TOFFset

positive when the internal 1 PPS comes after the GPS's (inferred).

What the loop does with it (read; `loop.md` has the update in full):

- **Normal.**  `pll_normal` (`0x448fc`) averages ten
  readings into `interval_mean` (`0x102538`), what
  `:SYNChronization:TINTerval?` returns, subtracts `ti_setpoint`
  (`0x10254a`), filters it, and sets the EFC from
  `K·f + B + I + c·s`, K = (1/G)/τ.  G is 6.25e−13, positive
  (`efc_gain_set`, `0x474c2`, from `pllp_task`): a positive TI raises
  the EFC.
- **Coarse** (`coarse`, `0x451c2`): two readings a second apart,
  `fine_d = TI₁ − TI₂`, `EFC ← EFC − fine_d / G`, until the two agree
  within 100 ns.  With TI = t_internal − t_GPS, TI₁ − TI₂ over one
  second is the oscillator's fractional frequency offset, so the
  firmware takes one EFC count to raise the frequency by 6.25e−13
  (inferred; the two signs are one assumption).
- **Steps.**  `fine_sync_adjust` (`0x44e8e`): for |TI| of 100 ns to
  300 ns it asks for TI / 100 ns steps, `COUNTER_SYNC_POSITIVE` for a
  positive TI, `COUNTER_SYNC_NEGATIVE` for a negative; at 300 ns or
  more, one step and a slew.  Under
  100 ns, none.
- **Sync.**  `sync_perform` (`0x44f5e`): when |TI| passes
  `limit_threshold` or a sync was asked for,
  `sync_request(COUNTER_SYNC_FULL, 1)` and
  "Peforming sync" (sic).  The coarse stage starts with one
  (`interval_next_reading(INTERVAL_ARM_SYNC)`).

## The sync field

`sync_request` (`0x40a64`) stores a kind and a count and marks the sync
wanted; `counter_arm` then, on each arm while it is wanted, calls
`counter_set_sync` (`0x40a84`), which writes register 3 twice: sync
field 0, then the kind, and counts the count down.  So one write pair
for each step, all with the preset on.  After the last, register 3
keeps the kind: nothing clears it.  The kinds (`counter_sync_kind`):

| Kind | Name | Asked by |
| ---- | ---- | -------- |
| 1 | `COUNTER_SYNC_POSITIVE` | `fine_sync_adjust`, a positive TI; the console's `clk_synch` |
| 2 | `COUNTER_SYNC_NEGATIVE` | `fine_sync_adjust`, a negative TI; `clk_synch` |
| 3 | `COUNTER_SYNC_FULL` | `sync_perform`, `INTERVAL_ARM_SYNC`'s rearm, `sync_imm` (`diag_sync_imm`, `0x1b2ec`) |

`clk_synch` (`diag_clk_synch`, `0x1b2d8`) passes its two arguments
straight to `sync_request`.

**Inferred:**

- The FPGA acts on the field's change from 0, not on its level: the
  field stays at the kind after the last step, and if each count
  stepped while it was set, steps would never stop.
- Kind 1 moves the internal 1 PPS one tick, 100 ns, earlier and kind 2
  one tick later: `fine_sync_adjust` counts steps as TI / 100 ns and
  picks kind 1 to cancel a positive TI, a late internal 1 PPS.
- Kind 3 restarts the divider on the GPS 1 PPS, putting the internal
  1 PPS, and with it the outputs, on GPS: `sync_perform`'s comment in
  the rewrite, "Syncs the outputs to GPS", and the loop's measuring
  again from the start after it.
- A step or sync applies before the next count, which begins after the
  preset comes off.

## What the emulator needs

Whatever the edges, the firmware sees only this:

- **Arm** (`counter_arm`): register 0 written `0x40`, then `0x00` with
  mode 0; possibly a register 3 pair before the preset comes off.
- **Ready:** set bit 1 (counted) and bit 0 (interpolated) once the
  count is done, within 5 s of the arm, both 1 PPS alive.  To keep the
  firmware's pace, about 0.4 s after the internal 1 PPS (inferred).
- **Count:** registers `0x12` to `0x17` as six nibbles of
  `0x555555 + floor((0.4000003 s + TI) / 100 ns)`.
- **Interpolator:** registers `0x10`, `0x11` as the complement of
  `low + 200 × fraction`, fraction the remainder of a tick, with
  `low` the low reference's reading under its present trim.
- **TI:** t_internal − t_GPS, the GPS 1 PPS from `Board::gps_pps`.
  For the loop to close, the internal 1 PPS's period must follow the
  EFC, 6.25e−13 of frequency a count, rising with it (inferred).
- **Sync:** register 3 going 0 then 1 moves the internal 1 PPS 100 ns
  earlier, 0 then 2 100 ns later, 0 then 3 onto the next GPS 1 PPS
  (inferred).
- **Preset:** clears both ready bits and holds the count at
  `0x555555` (`counter_selftest` reads it there).

## What is not established

- Which edge starts the count and which stops it, and whether the
  0.4 s is a divider tap; the bitstream at `0x20f18` would settle it.
- What the three ticks in 0.4000003 s are.
- Whether the seconds nibble counts the internal or the GPS 1 PPS.
- What sync kinds 1 to 3 do in the FPGA: the direction of a step, and
  what a full sync aligns to.
- What modes 1 and 2 and status bits 2 and 3 are; the firmware uses
  none of them.
