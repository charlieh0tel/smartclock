# Structures

The firmware's data structures and named globals, as the code that
uses them shows them, in the Z3805A 3543B image.
`tools/ghidra/types/smartclock.h` declares the structures for Ghidra, a
field named only where this note gives the code that shows what it is;
`tools/ghidra/names/z3805a-3543b.tsv` names and types their instances
and the globals.  The header uses `<stdint.h>`'s types.

`make check-types` compiles `tools/ghidra/types/check-layout.c` with an
m68k compiler (`m68k-linux-gnu-gcc`, or `M68K_CC`) and fails if a
structure's size or a named field's offset is not the one given here,
an offset read from the image written as the difference of its
addresses.  The header packs its structures.  Every one but the GPS
task's records has the same layout unpacked, under the 68000's 2-byte
alignment, but for the pad byte a compiler would add after
`nv_settings_fields` and `interval_reading_fields`, which have an odd
size: they are what a 68000 compiler lays out, not byte layouts that
happen to fit.  The GPS task's are not ("The GPS task's state",
below).

## The tasks and exchanges

`0x1304a` is the root task.  It stores its own id (`psos_my_pid`) in
`task_root`, sets up the status system (`status_init`, below), creates
three exchanges and four tasks, and starts each task at its entry:

| Object | Created with | Id kept at | Started at |
| ------ | ------------ | ---------- | ---------- |
| exchange `gpsx` | `create_x("gpsx", 0, 0)` | `0x1037fc` | |
| exchange `plla` | `create_x("plla", 0, 0)` | `0x103800` | |
| exchange `pllc` | `create_x("pllc", 2, 8)` | `0x103804` | |
| task `gpsm` | priority 100 | `0x1037e0` | `0x4bf4a` |
| task `hmon` | priority 200 | `0x1037e4` | `0x247d4` |
| task `pllp` | priority `0xaf` | `0x1037ec` | `0x47528` |
| task `curv` | priority `0x19` | `0x1037f8` | `0x41784` |

Three more tasks are created elsewhere, each by its own routine, with
`psos_create`'s last argument the cell that receives the task's id and
`psos_start` given the entry:

| Task | Created by | Id kept at | Started at |
| ---- | ---------- | ---------- | ---------- |
| `klok` | `klok_task_create` (`0x109ae`) | `task_klok`, `0x10371e` | `klok_task`, `0x10b1e` |
| `spoo` | `spoo_task_create` (`0x20c96`) | `task_spoo`, `0x1032f4` | `spoo_task`, `0x20e4e` |
| `scpi` | `scpi_task_create` (`0x29fce`) | `task_scpi`, `0x1037f4` | `scpi_task` |

These are the tasks `:DIAGnostic:OS:PROCess?` lists
(`../scpi/undocumented.md`, "`:DIAGnostic:OS`"), with pSOS's own `ROOT`
and `IDLE`.  Of the exchanges it lists, `1pps` is created by
`pps_init` (`0x12e24`), straight through the kernel, service 20 with
the name `'1pps'` in D0, into `pps_exchange` (`0x100bc4`); `logr` is the
event log's mutex, `log_mutex`, which `log_init` (`0x3167c`) creates
with `mutex_create` (`0x104e6`), as `klok_task_create` does `klok_mutex`
under the name `klok`.  A mutex is an exchange of its name, which
`mutex_lock` and `mutex_unlock` (`0x10502`, `0x10518`) take and give
back.

The SCPI setters' requests go to `pllc`: five routines,
`0x29ec2`--`0x29f94`, fill a message on their stack and pass its address
to `request_and_wait` with the exchange, which `0x43340` receives
(`request_x`).  The message holds a pointer to a record in ROM (such as
`0x33bae`), two bytes and up to two longs; the five differ in which they
fill, which is not yet worked out.  `0x46636` sends four longs to
`gpsx`.

## The status system

`status_init` (`0x2f480`) builds the SCPI status registers:

    status_create(&status_object, 7, 0x1e, status_conditions, 0)

`status_create` (`0x280ea`) allocates `0x16` bytes plus four a register
and a 16-byte record for each register, and builds a queue with room
for `0x1e` entries at `+0x06`.  `status_init` then sets each register
up from the 22-byte entries at `0x2f7ac` with `status_register_setup`
(`0x2819c`), and sets `status_ready` (`0x102eb7`), without which
`status_report` (`0x2be9c`) does nothing.

The status model:

| Offset | Field | Shown by |
| ------ | ----- | -------- |
| `+0x00` | register count, a word | `status_create` stores its second argument, 7 |
| `+0x02` | an output object, with a table of methods | `status_register_setup` stores it for the register with no parent |
| `+0x06` | the queue | `status_create` builds it, room for 30 |
| `+0x0a` | the conditions table | `status_create`; `status_set_bit` indexes it |
| `+0x0e` | a long | `status_create` stores its last argument, 0 |
| `+0x12` | the registers, a pointer each | `status_create`, `status_set_bit` |

A register, 16 bytes, has its parent register at `+0x0a` and the bit
it reports into there at `+0x0e`, both set by `status_register_setup`
from its entry; `+0x00`, `+0x06` and `+0x08` are cleared there and
`+0x02` and `+0x04` set from the entry.  Which of these is the
condition, event, enable and transition register of the SCPI model is
not yet worked out.

A condition, an entry of `status_conditions` (`0x2f838`), is 6 bytes:
a kind, a register and a bit.  `status_set_bit` (`0x2820a`) looks the
condition up (a negative one in a table at `0x28720`), and by its kind
sets, clears or transfers the register's bit, or puts the condition in
the queue.  `:DIAG:STAT:HAPP` takes a condition from 0 to `0x38`, so
the table has 57.

## Numeric SCPI parameters

Each numeric parameter the SCPI commands set and query has a
descriptor in ROM, `0x2c` bytes, its small tables just before it.
`scpi_numeric_get` (`0x298fc`) and `scpi_numeric_set` (`0x2982c`) are
given one.

| Offset | Field | Shown by |
| ------ | ----- | -------- |
| `+0x00` | `type`, `SCPI_PARAM_NUMERIC` (0) | `finish_scpi_cmd` switches on it (below) |
| `+0x01` | `last_index`, the values less one | `validate_num`, `nv_save` |
| `+0x02` | `nv_widths`, each value's bits in the NV register | `nv_save` ("The NV register") |
| `+0x06` | `minimum` | the queries' `MINimum` reads through it; `:GPS:EMANgle`'s is 0 |
| `+0x0a` | `maximum` | `MAXimum` reads through it; `:GPS:EMANgle`'s is 89 |
| `+0x0e` | `validate` | `validate_num` checks each new value with it, given the minimum and maximum; `clamp_to_limits` for most; `scpi_numeric_get` passes it the bounds for `MINimum` and `MAXimum` |
| `+0x12` | `changed` | `finish_scpi_cmd` calls it as each value is stored; for most a bare `rts` (`0x12af0`) |
| `+0x16` | `value`, the current value, in RAM | `scpi_numeric_get` reads it; `finish_scpi_cmd` stores it |
| `+0x1a` | `done` | `finish_scpi_cmd` calls it once the values are stored |
| `+0x1e` | `flags` | 2: `finish_scpi_cmd` calls `nv_save`; 4: it writes the EEPROM record at `0x400000` (`0x30dea`) |
| `+0x26` | `is_integer`: 0 a float, 1 an integer, 2 or 3 several | `scpi_numeric_get` converts an integer to float for the reply; `validate_num` takes 2 and 3 as several values |

| Descriptor | Parameter | Minimum | Maximum | Value |
| ---------- | --------- | ------- | ------- | ----- |
| `0x334de` | `:SAMPle:INTeger` | 0 | 256 | `0x101de4` |
| `0x3354e` | `:SAMPle:IARR` | 0 | 31 | `0x101de8` |
| `0x3367a` | `:SAMPle:FLOat` | 0 | 5 | `0x101e18` |
| `0x336da` | `:SAMPle:FARR` | -10.2375 | 10.2375 | `0x101e1c` |
| `0x339b4` | `:SOURce:PULSe:CONTinuous:PERiod` | 1 | 40000000 | `0x101f26` |
| `0x33e20` | `:SOURce:ROSCillator:LIMit:THReshold` | 3e-7 | 1 | `0x101e9e` |
| `0x33e5e` | `:SOURce:ROSCillator:HOLDover:LIMit:THReshold` | 1e-6 | 1 | `0x101ea2` |
| `0x33e9c` | `:DIAGnostic:ROSCillator:TCOefficient` | -200 | 200 | `0x101d0c` |
| `0x33eda` | `:DOUGlas:DURation` | 600 | 864000 | `0x101e94` |
| `0x33f18` | `:SOURce:ROSCillator:HOLDover:DURation:STATus:THReshold` | 1000 | 3888000 | `0x101e9a` |
| `0x33f56` | `:SOURce:GPSystem:EMANgle` | 0 | 89 | `0x101e8c` |
| `0x33f94` | `:SOURce:GPSystem:ADELay` | 0 | 999999 | `0x101e90` |
| `0x33fd2` | `:DIAGnostic:TOFFset` | -3e-7 | 3e-7 | `0x101d10` |
| `0x3409c` | `:KENneth:PREDicted:DURation` | 3600 | 604800 | `0x101ec2` |
| `0x342f8` | `:DIAGnostic:GPSystem:POSition:HOLD:RAIM:THReshold` | 3 | 65535 | `0x101ed2` |
| `0x34336` | `:SOURce:ROSCillator:HOLDover:DURation:MEASurement:THReshold` | 0 | 130000 | `0x101eda` |

## EEPROM records

`record_checksum` (`0x3082e`) sums a record's bytes after its first two
and returns the sum and its complement, which a record keeps in those
two bytes.  It is called on the EEPROM's records at `0x400000` (16
bytes), `0x400010` (48), `0x400060` (32) and `0x400080` (64), and on
`0x94e` bytes of RAM from `0x100000`.

## Globals the console prints

The console's print words label what they print, so the firmware names
its own globals; the format and the word that prints each are the
evidence.

### The GPS task's state

`gps` (`0x100c6a`, `gps_state`, `0x55a` bytes) holds what the GPS
task's decoders store of each Oncore message and the task's own state
after them.  It is cleared as one, `memset(&gps, 0, 0x55a)`, and 27
functions address it through a base register, at offsets `0x1` to
`0x556`.  Each message's record sits at the cell the message table
(`oncore/z3805a-3543b.txt`) gives its decoder, in the table's order;
its fields are the VP Oncore reference's
(`third_party/motorola/VPCommands.pdf`), in the message's order.  A
decoder copies a field, a byte, word or long, or runs a field table
through `0x4f02a`, a byte code: n copies n bytes, `00` skips a RAM
byte, `0x85` writes a sign byte for the next, `0x83` writes 0 for a
byte it drops, `0x82` drops one, `0xc0` and up repeat the group to
`0x81` that many times less `0x100`, and `0x80` ends.  The tables pad
some messages so their words and longs fall on even addresses.

| Offset | Member | Message | Printed after |
| ------ | ------ | ------- | ------------- |
| `+0x001` | `time`: hour, minute, second | `@@Aa` | `TIME` |
| `+0x004` | `gmt_offset`: sign, hours, minutes | `@@Ab` | `GMT CORR` |
| `+0x008` | `date`: month, day, a word of year | `@@Ac` | `DATE` |
| `+0x00c` | `position`: latitude, longitude (mas), GPS and MSL heights (cm) | `@@Ad`, `@@Ae`, `@@Af` | `LAT`, `LON`, `GPS HGT`, `MSL HGT` |
| `+0x01c`, `+0x01d` | `mask_angle`, `satellite_select` | `@@Ag`, `@@Ah` | `ELEV MASK`, `SELECTION` |
| `+0x01e` | `manual_satellites[6]`, by channel (the decoder `0x4efd6` stores at channel - 1) | `@@Ai` | |
| `+0x024`--`+0x02a` | `xdop_type`, `xdop_hysteresis`, `xdop_threshold`, `hdop_threshold` | `@@Aj`, `@@Ak`, `@@Al`, `@@AC` | |
| `+0x02c` | `ignored_satellites`, a bit a satellite | `@@Am` | |
| `+0x08e` | `almanac_update` | `@@An` | |
| `+0x0a4`, `+0x0a5` | `ionospheric_correction`, `fix_algorithm` | `@@Aq`, `@@Ar` | `FIX ALG` |
| `+0x0a6` | `hold_position`: latitude, longitude, height | `@@As` | `POS HOLD` |
| `+0x0b4` | `position_hold` | `@@At` | |
| `+0x0bb`, `+0x0bc` | `altitude_hold`, `utc_time` | `@@Av`, `@@Aw` | |
| `+0x0be` | `epoch_offset`, ms | `@@Ax` | |
| `+0x0c0`, `+0x0c4` | `pps_offset`, `pps_cable_delay` | `@@Ay`, `@@Az` | `1PPS OFFS`, `CABLE DLY` |
| `+0x0c9` | `application_type` | `@@AB` | `APPL` |
| `+0x0ca`, `+0x0cb` | `position_fix_threshold`, `differential_output_threshold` | `@@AD` | `POS FIX THR`, `DIF OUT THR` |
| `+0x0ce` | `status`, 63 bytes (table `0x50fd2`): date, time, fraction, position, velocity, heading, DOP, counts, six channels' satellite, mode, signal and status, and the receiver status | `@@Ba` | `print_stat`'s page |
| `+0x110` | `visibility_received`, which `@@Bb`'s decoder sets | | |
| `+0x112` | `visibility`: a count, then 12 satellites of 10 bytes (table `0x50fd8`): satellite, Doppler, elevation, azimuth, health | `@@Bb` | `Visible Sats` |
| `+0x18d` | `xdop_table`, copied as it comes (`0x50fe2`): type, count, mode, 9 combinations of an xDOP and six satellites | `@@Bc` | `xDOP TYPE` |
| `+0x1da` | `almanac`: the RAM's and the EEPROM's valid flag, week, time, count and satellites | `@@Bd` | |
| `+0x2a8` | `corrections`, 64 bytes (table `0x50ff6`): the time, then six channels' satellite, range and rate corrections and issue of data | `@@Ce` | `GPS TIME`, the pseudorange page |
| `+0x2fb` | `raim`, 63 bytes (table `0x50fe6`): rate, on, alarm limit, 1PPS mode and rate, the time to the next pulse, pulse status and sync, solution and RAIM status, sigma, sawtooth, six channels' time estimates | `@@Bn` | `SAWT ERR`, the Time RAIM page |
| `+0x33a`, `+0x33c` | `leap_second_pending`, `self_test` (14 bits kept) | `@@Bj`, `@@Ca` | |
| `+0x344` | `extension`, 62 bytes: five DOPs, magnetic variation, velocity north, east and up, correction age, ECEF x, y and z, six URAs, the ten elements of the least-squares A-1 matrix, clock bias, oscillator offset | `@@Bk` | the DOP, `VEL`, `ECEF`, `URA` and clock lines |
| `+0x3ec` | `fix_mode` | `@@Cg` | |
| `+0x3ee`, `+0x3ef` | `receiver_id_received`, which `@@Cj`'s decoder sets; `receiver_id`, ten lines, each line's CR made a NUL and its LF dropped (table `0x51004`) | `@@Cj` | |
| `+0x506` | `average_count` | | |
| `+0x50a`--`+0x522` | `average_latitude`, `_longitude`, `_height_gps`, `_height_msl`, doubles | | `AVG LAT`, `LON`, `HGT` |

`@@Ba`'s decoder `0x4f59a`, in fix mode, adds its latitude, longitude
and heights to the averages, counting `average_count`, while position
hold is off, bit 5 of the receiver status is set, and either the byte
at `0x101e98` is 1 or the count is below `:DOUGlas:DURation`.  The print shows latitude and longitude as "ms"; the
reference's unit is the milliarcsecond.  `@@As`'s height goes through
a decoder of its own (`0x4efb4`), not to a fixed cell, but is printed
from `hold_position.height`.  `gps_print_extension` prints the matrix
as the triangle the reference numbers it in.

Whether the original was one structure or one source file's statics,
which a compiler can lay out together and reach through a base
register in the same way, the code cannot say.  Its records are byte
layouts, not a compiler's: `@@Ba`'s is 63 bytes, and `@@Bc`'s and
`@@Bn`'s begin at odd addresses, with words at odd offsets within them
that a 68000 compiler would pad.  The header packs it, and
`check-layout.c` checks every member's offset that a decoder or a
print gives.

### The loop's state

`pll` (`0x102162`, `pll_state`, `0x3d6` bytes) is the loop's state
block (`../screen-format-strings.md`, "Mode suffixes"): its stage at
`+0`, the manual flag at `+0x12`, the recovery and fine sub-states at
`+0x15` and `+0x17`.  It is cleared as one, `memset(&pll, 0, 0x3d6)`,
and ends where `interval_mean` begins.  `pr_efc` prints `efc`
(`+0x32`, a float) and `pll_rep` `tfom` (`+0x36`, a float, "tfom =
%.1e secs"); `max_loop_time` (`0x101e84`, a float) is outside it.

The EFC history, `pll.efc_history` (`+0x4a`, `efc_ring`, `0x366`
bytes, cleared as one with `memset` too), is a ring of 64 averages of
the EFC.  `efc_history_update` (`0x4137e`) adds the EFC, less the
temperature coefficient (`:DIAG:ROSC:TCO`) times an ADC reading, to
`efc_sum` each sample in some states, counting `summed_count`, and
each 2700 samples (`sample_count`) stores an entry at `stop_ptr` and
advances it, pushing `start_ptr` on when the ring is full.  Its debug
message names the arrays: "e_avg= %.1f time= %f weight= %d log= %.2f
tfom= %.1e".

Its other named fields, by the routines that use them:

| Offset | Field | Use |
| ------ | ----- | --- |
| `+0x01` | `previous_stage` | `pllp_task`'s copy of `stage` |
| `+0x03` | `powerup_state` | `powerup`'s sub-state, the ladder of `../screen-format-strings.md` |
| `+0x06` | `reference_check_state` | `pll_check_reference`'s sub-state |
| `+0x0c`, `+0x0e`, `+0x10` | `startup_state`, `normal_state`, `holdover_state` | `startup_pll`'s, `pll_normal`'s and `holdover`'s sub-states |
| `+0x11` | `hold_causes` | why the loop holds over, a set of `pll_hold_cause` bits as the console's `pr_hold_cause` prints them: manual 1, GPS 2, TI error 4, TI limit exceeded 8, hardware failure 0x10, temporary mode 0x20, 1PPS selection 0x40; 0x80, which `enter_holdover` tests and nothing prints, is not known |
| `+0x16` | `coarse_state` | `coarse`'s sub-state |
| `+0x18` | `frequency_adjusted` | set when `fine` applies a frequency correction; `holdover_limit_exceeded` counts the limit exceeded while it is |
| `+0x1a` | `fine_retries` | `fine`'s failed attempts, a word; it gives up past 9 |
| `+0x24`, `+0x25` | `gps_diag_state`, `gps_diag_substate` | `gps_diag_sequence`'s |
| `+0x26` | `gps_init_state` | `gps_init_sequence`'s, which `powerup` waits past 13 for |
| `+0x2b` | `leap_check_state` | `gps_leap_sequence`'s |
| `+0x3a` | `predicted_uncertainty` | a float from the curv task's aging fit; `:KENneth:PREDicted?` returns it |
| `+0x46` | `frequency_tolerance` | a float, 1e-8 at first: the offset `fine` accepts, and the rate the holdover limit grows by |
| `+0x3b8` | `loop_time_constant` | a float, 150 s at first and at most `max_loop_time` |
| `+0x3c4` to `+0x3c6` | `led_gps_lock`, `led_holdover`, `led_tmh_valid` | the lamps, as `:LED:GPSLock?`, `:HOLDover?` and `:TMHValid?` read them and `leds_update` writes them |
| `+0x3c8` | `holdover_duration` | `:ROSCillator:HOLDover:DURation?` |
| `+0x3cc`, `+0x3d0` | `reference_uptime`, `reference_seconds` | the uptime and the FPGA's seconds count `clock_set_from_reference` sets the clock from |
| `+0x3d4` | `sync_requested` | `sync_perform` syncs whatever the TI while it is set, then clears it |

| Offset | Field | Shown by |
| ------ | ----- | -------- |
| `+0x000`, `+0x001` | `start_ptr`, `stop_ptr` | `pll_rep`'s `start ptr`, `stop_ptr`; the ring's indexes, `& 0x3f` |
| `+0x004` | `sample_count` | counted to 2700 |
| `+0x008` | `summed_count` | the samples summed |
| `+0x00a` | `averages[64]` | `e_avg`, `efc_sum / summed_count`; `pll_rep`'s `last efc average` |
| `+0x10a` | `weights[64]` | `weight`, 1 to 3 by `summed_count` |
| `+0x14a` | `times[64]` | `time` |
| `+0x24a` | `logs[64]` | `log`, -1 when `time` is at most 1 |
| `+0x34a` | `efc_sum` | the sum |
| `+0x352` | `drift` | `pll_rep`'s `current drift`, times `5.4e-8`, per day |

### Others

The other exchanges, from their `create_x` calls, and the routines
whose messages name them:

| Exchange | Id kept at | Created by | Taken and given back by |
| -------- | ---------- | ---------- | ----------------------- |
| `drtR`, `drtW` | `0x103102`, `0x103106` | `duart_init` (`0x1d93e`) | |
| `sciR`, `sciW` | `0x103150`, `0x10316c` | `sci_init` (`0x1fdb6`) | `sci_lock` (`0x1fd0e`), `sci_unlock` (`0x1fc20`): "Unable to acquire/release SCI token" |
| `eepx` | `0x101d18` | `eeprom_init` (`0x30f10`) | `eeprom_lock` (`0x30fae`), `eeprom_unlock` (`0x3105e`): "Unable to acquire/release eepx token" |
| `spoo` | `0x1032f8` | | |

The GPS print words do not print: each leaves a code in the mailbox at
`0x10256c`, which `0x470aa` turns into a request to the GPS task,
sent with `gpsx_send`.  The GPS task gives requests `0x31` to `0x45`
to `gps_console_request` (`0x4c572`), whose cases print the pages:

| Word | Code | Request | Printer |
| ---- | ---- | ------- | ------- |
| `print_stat` | 1 | `0x32` | `gps_print_status` (`0x49054`) |
| `print_vis` | `0xf` | `0x33` | `gps_print_visibility` (`0x48fac`) |
| `print_ext_msg` | `0x12` | `0x34` | `gps_print_extension` (`0x48300`) |
| `print_bc` | `0x10` | `0x35` | `gps_print_best_combinations` (`0x48224`), "xDOP ... VALID COMBOS" |
| `pr_gps_id` | `0x13` | `0x36` | `gps_print_id` (`0x493c0`) |
| `pr_1pps` | `0x14` | `0x37` | `gps_print_1pps` (`0x4c9e2`) |
| `pr_gps_istate` | `0x1e` | `0x38` | `gps_print_instrument_state` (`0x489a0`), "Instrument State Settings" |
| `pr_time_raim` | `0x15` | `0x39` | `gps_print_time_raim` (`0x484de`) |
| `pr_avg` | `0x1d` | `0x3a` | `gps_print_average` (`0x48ea0`) |

`gps_print_pseudorange_corrections` (`0x487f6`) prints "Pseudorange
Correction Status"; what calls it is not traced.

## What the prints name

Every `printf`-family call (`printf`, `sprintf`, `debug_printf`,
`debug_sprintf`, `printf_polled`, `log_write`; 306 calls, 681
conversions) was read for what it prints: each conversion paired with
its argument, the argument followed through the assignments before it
to the cell or member it reads, and labeled by the format's text before
and after it and any string literal a `%s` before it prints.  A value
computed from cells, such as a difference, does not name them.  There
is no `scanf`; input is parsed by the SCPI parser and two `atoi` calls.

**The GPS link's counters**, `gps_counts` (`0x1017bc`, `gps_statistics`,
`0x36` bytes, cleared with one `memset`), all words, are the
"GPS Process Monitor" page (`gps_print_process_monitor`, `0x4ca6c`):

| Offset | Fields | Printed as |
| ------ | ------ | ---------- |
| `+0x00`--`+0x0a` | `received`, `sent`, `jammed`, `sent_errors`, `received_errors`, `jam_errors` | `Msgs Rcvd`, `Msgs Sent`, `Msgs Jam` and each line's `Errors` |
| `+0x0c`, `+0x10`--`+0x1a` | `successes`, `rejects`, `timeouts`, `busy`, `aborts`, `no_command`, `no_query` | `Success`, `Reject`, `Timeout`, `Busy`, `Abort`, `No Cmd`, `No Qry` |
| `+0x1c`, `+0x1e`, `+0x2c` | `unknown_command`, `unknown_sender`, `unknown_response` | `? Cmd`, `? Sndr`, `? Resp` |
| `+0x20`--`+0x26` | `commands_sent`, `responses_read`, `e_retries`, `t_retries` | `Cmds Sent`, `Resp Read`, `E Retry`, `T Retry` |
| `+0x28`, `+0x2a` | `raim_detected`, `raim_alarms` | the Time RAIM page's `# Alarms: %d (%d detected)` |
| `+0x2e`, `+0x30` | `frame_errors`, `checksum_errors` | `Frame`, `Cksum` |
| `+0x32`, `+0x34` | `command_monitor_failures`, `status_monitor_failures` | `Command Monitor ... %5d failures`, `Status Monitor ...` |

`gps_last_command` (`0x1017f2`) points at the last command record,
whose `+8`, `+0xa` and `+0xc` the page prints as `Sender`, `Cmd #` and
its kind; `gps_last_program_message` (`0x101216`) is the "Last Rcvr
Prgm Msg", `(none issued)` when empty, and `gps_response_buffer`, 20
bytes from `0x101266`, the "Response Buffer", in hex.  `gps_receive`
(`0x4b846`) frames the engine's messages into it from their `@`,
through `gps_response_cursor`, counting frame errors.

**The receiver ID**, `gps.receiver_id` (`oncore_receiver_id`), takes its
fields from `gps_print_id`: `Copyright Notice`, `Software P/N`,
`Software Version`, `Software Revision`, `Software Date`, `Model
Number`, `Hardware P/N`, `Serial Number`, `Manufacture Date` and
`Installed Options`, 34 bytes and nine of 27, as `@@Cj`'s table lays
them.  `gps.correction_average` and `correction_deviation` (`+0x2e8`,
`+0x2f0`, doubles) are the pseudorange page's `AVG` and `STD DEV`.

**The time code.**  `timecode_update` (`0x13c50`) builds the
`:PTIMe:TCODe?` message in `tcode_message` (`0x10386f`) exactly as the
Z3801A manual gives it (097-z3801-01, "Detailed Description of the Two
Time Code Formats"): `T1#H%08X` or `T2%04d%02d%02d%02d%02d%02d`, then
t, f, l, r and v, then the checksum, cc.  So t is `tfom_code`
(`0x13a48`) of `pll.tfom`; f `get_ffom` (`0x42a18`), which `pll_rep`
prints as `ffom`; l is `0` unless `nv_settings.leap_pending` (`0x101ece`), and then
`-` when `nv_settings.leap_minute_length` (`0x101eca`) is 59 and `+` otherwise; r
is `status_request_service` (`0x102eb6`), the Status Byte's
request-for-service bit, next to `status_ready`; v is `0` when
`time_valid` (`0x1399a`) is true; cc is `tcode_checksum` (`0x139e4`).
`tcode_request` (`0x139f6`) sets `tcode_format` and `tcode_pending`.
While `es_enabled` (`0x103831`, which `pllp_task` sets) it also builds a
second message, `es_message` (`0x103832`, `es_build` at `0x13a6a`):
`%02d%03d%02d%02d%02d%02d%02X%02X` -- the year, day of the year, time,
a leap value, status bits and `status_summary` -- as digits or, when
the `es_ascii` console word has set `es_ascii` (`0x103830`), as ASCII.
`status_summary` (`0x102544`) folds groups of status register 6's
condition bits into six.

**The 1PPS monitors** run in `hmon`.  `gps_pps_check` (`0x245a6`)
counts the GPS 1PPS with the pulse accumulator `PACNT`: each check
expects 1 to 3 edges since `gps_pps_last_count` ("short gate, count
%d"), and every 10 s, `gps_pps_gate` counting by 2, 8 to 12 ("long
gate, count %d"), setting `gps_pps_dead` with "GPS 1pps signal is dead"
and clearing it with "is ok".  `internal_pps_check` (`0x2471a`) does the
same for the internal 1PPS from `internal_pps_count` ("Internal 1pps
signal is dead", "count %d").

**Exceptions and the watchdog.**  `last_exception` (`0x10099e`,
`exception_record`) is what "Exception: %s, vector %04X, PC = %08X, SR
= %04X, addr = %08X" prints: a pending flag, the exception's name, SR,
PC, vector and the fault address, printed when the vector word's format
is not 0.  The watchdog's per-task counters are bytes from `0x1009b2`:
"watchdog: clock: %4d gps: %4d pll: %4d mon: %4d spool %4d";
`pllp_task` counts `watchdog_pll`.

**The clock and the log.**  The `klok` task's entries (`klok_entry`,
`0x1a` bytes) are what `print_clk_list` prints (`klok_print_entries`,
`0x107ee`): "at:", "period:", "function address:" and "event class:".
`klok_entries` (`0x103722`) is the list, `klok_free` (`0x103726`) the
free list, `klok_pool` (`0x10372a`) five entries, and `klok_mutex`
(`0x1037b4`) guards them ("Sem_open of clk_msg failed").  A date is
`year << 16 | month << 8 | day` and a time `hour << 16 | minute << 8 |
second`, and the event log keeps them so too: `log_read` (`0x31762`)
loads a page of three `log_entry`s into `log_page_buffer` (`0x101d94`,
page number at `0x101dd4`) and prints one as "Log
%03d:%04d%02d%02d.%02d:%02d:%02d" and its format with its two
arguments, "First satellite tracked at" among them.

**The tasks' stacks.**  `task_stack_report` (`0x16c44`) prints each task
in `task_table` (`0x1026b8`, six bytes a task, `task_count` of them) with
its user and supervisor stacks' top, size and use, measured against a
`0xabcd` fill, then ">> pSOS system supervisor stack:" from
`psos_config` (`0x10264c`), the pSOS configuration table copied from
ROM (`0x5122a`, `0x62` bytes): the stack starts 200 bytes into its
data area and is `psos_supervisor_stack_size` (`+0x28`) long.

**Deferred printing.**  `deferred_vprintf` (`0x20dd2`) formats into
`deferred_text` (`0x1032fd`) at `deferred_end`, and `deferred_flush`
(`0x20ebe`) prints from `deferred_start`.

**Others.**  `*IDN?` prints `idn_model` and `idn_serial` (`0x101f68`,
`0x101f7a`, 18 bytes each) and `idn_suffix` (`0x101f8c`) after
`3543B-`: `3543B-A` on the bench (`../bench.md`).  The FPGA's shadow
registers are the firmware's own `fconf_regs` (`0x101fa9`), four of
them: "old_addr, fconf_regs[1] = %x" is `fpga_read` (`0x4076a`), which
puts a register number in `fconf_regs[1]`'s address bits to read it.
`fpga_bit_table` (`0x41310`) gives each bit `fpga_set_bit` takes its
register and mask.  The time-interval counter's routines are each
given `interval_reading` (`0x101f9c`) and `counter_configuration`
(`0x101fae`): `counter_arm` (`0x40c6a`) reloads `fconf_regs` from the
configuration's first four bytes and re-arms the FPGA;
`counter_calibrate` (`0x405bc`) runs `interpolator_calibrate` and keeps
`reading_low` and the spread at the configuration's `+0x18` and
`+0x1c`; `counter_read` (`0x40d34`) is `interval.md`'s "One reading",
the interpolator's part `(interpolator_read() - +0x18) / +0x1c` and
the calibration double the configuration's `+4`.  `interval_next_reading`
is `interval_wait_reading` (`0x4513c`), then `interval_rearm`
(`0x450c4`).
"mask angle ack failure: %02X" and eight like it print `gps_ack_code`
(`0x102584`); "Unable to initialize eepx: %X" and the like print
`eepx_status`, `drtR_status`, `drtW_status`, `sciR_status` and
`sciW_status`.  `fine_y0` (`0x102154`), `pll.fine_index` and
`pll.fine_d` are fine acquisition's "y0", "i" and "d" (`loop.md`).

## What the words name

A console word or SCPI handler that does little but call one unnamed
function names it.  The ADC words print what they read, so each reading
is named for its word: `read_adc_temp` (`0x1f308`, channel 0 × 0.273,
"Temperature: %f", also `:DIAG:TEMP?`), `read_adc_5v` (`0x1f32e`,
channel 4 × 0.0242), `read_adc_p15v` (`0x1f406`, channel 2 × 0.0763),
`read_adc_m15v`, `read_adc_oven` and `read_adc_doven` (`0x1f42e`,
`0x1f3ae`, `0x1f356`, channels 6, 5 and 1 against channel 4) and
`read_adc_ant_curr` (`0x1f4ba`, channel 3 × 250/255).
`:DIAG:ROSC:CURR?` calls `read_adc_osc_curr` (`0x1f486`), whose body
is `read_adc_ant_curr`'s.  Each returns a float in D0.

The others:

| Function | Called by | What it does |
| -------- | --------- | ------------ |
| `hdac_write` (`0x1f4ee`) | `hdac_write` | a value into one of six HDAC channels, through `hdac_set` |
| `hdac_write_all` (`0x1f5bc`) | `hdac_all` | six 6-bit values, two to a word, into `hdac_shadow` (`0x10314a`), written to device 2 |
| `efc_write` (`0x1f646`) | `efc_write` | the EFC to device 7, then a strobe at `0x306000` |
| `qspi_write`, `qspi_read` (`0x20acc`, `0x20aec`) | the two above | `psos_io_write` and `_read` to device `0x100` \| n |
| `status_read_event`, `status_read_condition` (`0x284b6`, `0x28506`) | the `STATus` `EVENt?` and `CONDition?` queries, `*ESR?`, `*STB?` | a status register's event or condition |
| `os_process_report`, `os_memory_report` (`0x16a4c`, `0x16ace`) | `:DIAG:OS:PROC?`, `:DIAG:OS:MEM?` | the task and memory reports |
| `psos_debug_output` (`0x16e5a`) | `debug_psos`, `:DIAG:SYST:DOUT` | |
| `eeprom_write_byte` (`0x31128`) | `wr_eeprom` | writes a byte and polls until it reads back, five tries |
| `clear_nv` (`0x3281a`), `clear_gps_debug` (`0x4c918`) | `clear_nv`, `clr_gps_debug` | the latter clears `gps_counts` and the last command |
| `sync_request` (`0x40a64`) | `clk_synch`, `sync_imm` | stores a mode and a time and sets two states to 2 |
| `pll_mode` (`0x45b70`) | `:SOUR:ROSC:HOLD:DUR?`, `HOLD:INIT` | the status screen's mode accessor (`../screen-format-strings.md`, "Mode suffixes"), from `pll.stage` and `pll.manual` |
| `log_count` (`0x330ee`) | "Log status: %-3d entries" | `log_entry_count` (`0x101f8e`); full past 31 |
| `pps_init` (`0x12e24`), `set_timedate` (`0x137d8`) | "Pps_init error %#04x", "set_timedate error: %2X" | the firmware's own names |
| `gps_seconds` (`0x111cc`) | the T1 time code's `#H%08X` | seconds since 1980 from a packed date and time |
| `leap_gps_time` (`0x11284`) | `:PTIM:LEAP:GPST?` | |
| `get_clock_date`, `get_clock_time` (`0x136d6`, `0x136de`) | the T2 time code | `clock_date`, `clock_time` |
| `get_gps_date`, `get_gps_time` (`0x136e6`, `0x136ee`) | the T1 time code | `gps_date` (`0x1009d6`), `gps_time` (`0x1009d2`) |
| `day_of_year` (`0x10de4`) | the es message's `%03d` | |
| `exception_name` (`0x11a2a`) | an exception's "%s: %s" | "Oooh! That tickles. I don't know that vector." past 0xe8 |

## The interpolator calibration

`interpolator_calibrate` (`0x40248`) is the time-interval counter's
interpolator calibration that `ovens.md` ("What `hdac` is not")
describes in the Z3816A.  It is given an `interpolator_calibration`,
9 bytes, at `interpolator_calibration_state` (`0x101fce`), or a copy on
one caller's stack:

| Offset | Field | Shown by |
| ------ | ----- | -------- |
| `+0` | `initialized` | set the first time, with both DACs at `0x1f` |
| `+1` | `calibrated` | set when the readings fit |
| `+2`, `+3` | `hdac_2`, `hdac_3` | written to HDAC channels 2 and 3 (`hdac_set`), stepped until they fit |
| `+4` | `result_valid` | set when a result is kept |
| `+5`, `+6` | `result_hdac_2`, `result_hdac_3` | `+2` and `+3` copied when the spread is 200, or 199 or 201 |
| `+7`, `+8` | `reading_low`, `reading_high` | `interpolator_measure(0)`, wanted 1 to 25, and `(1)`, wanted 201 to 225 |

`interpolator_measure` (`0x40030`) pulses the FPGA bits for one
measurement and returns `interpolator_read` (`0x40c56`), the complement
of FPGA register `0x10`.

## The NV register

The settings the SCPI setters keep, `nv_settings` (`0x101e8a`, `0xc5`
bytes, `nv_settings_fields`), are saved to the EEPROM as
the "NV register": two records of `0x80` bytes, `nv_records` at
`0x4000c0`, used in turn.  `nv_save` (`0x3293a`) builds a record --
`"S"`, a sequence number one past the stored record's at `+4`,
`"3543B"`, then the fields of 21 settings and 18 more, from descriptor
tables at `0x32d5e` and `0x32db2`, packed into longs, then `0xff`
("Warning: Entire nv register is being used" when they do not fit),
with `nv_checksum` (`0x328e0`) over bytes 8 to `0x7f` -- and writes it
to slot `nv_slot` (`0x101f62`) when `nv_record_differs` (`0x3290e`)
says it changed.  `nv_restore` (`0x3282c`) loads slot 0, else slot 1
(`nv_load_slot`, `0x32bc2`); when neither is good it copies
`nv_settings_defaults` (`0x30966`) over the settings, takes the slot
with the older sequence number, erases it (`nv_slot_erase`, `0x327f4`)
and saves.  `clear_nv` erases both slots.

Each value a descriptor in `nv_save`'s two tables points at is a field
of `nv_settings`; the descriptors are the SCPI setters' own, so each
field is named for the command whose setter is given its descriptor.
The first table (`nv_numeric_params`, `0x32d5e`) holds 21
`scpi_numeric_param`s, eleven of them those of "Numeric SCPI
parameters" above, a long a value; a descriptor's `+1` is its values
less one and `+2` (`nv_widths`) each value's bits in the record,
negative for a signed one.  The second (`nv_setting_params`,
`0x32db2`) holds 18 `scpi_setting_param`s, `0x1e` bytes, for the
switches and choices, a byte a value at `+0xc`, mostly one bit wide.
Among them are the five status registers' transition masks and seven
enables (`*ESE`, `*SRE`, `:STATus ... ENABle`), the time zone, the
ignored satellites, the pulse's start time and date, and
`douglas_duration_infinite`, which `:DOUG:DUR` sets for INFinity and
the survey's averaging tests.  Four descriptors no setter refers to
(`nv_param_leap_minute_length`, `_leap_date`, `_leap_pending` and two
`field_`s) keep the leap second's state and two values not traced.

## The time-interval counter's records

`interval_reading` (`0x101f9c`, `interval_reading_fields`, 13 bytes)
is what `counter_read` leaves: the reading in seconds, its `valid`
flag (`+8`), its `errors` (`+9`), the `tag` `0xfff6` at `+0xa` that the
`:DIAG` query returns with it (`interval.md`), and `armed` (`+0xc`),
without which the routines report the measurement aborted.  On an
error the reading is SCPI's NaN, 9.91e37 (`0x47d2a37dced46143`), and
`counter_read` prints each error bit, which `counter_error` names:
1 "Gate failure", 4 "Result uncomputable", 8 "Measurement aborted",
`0x10` "Interpolator calibration failure".

`counter_configuration` (`0x101fae`, `counter_configuration_fields`,
`0x20` bytes, `interpolator_calibration_state` right after it) holds
the four values `counter_arm` loads into `fconf_regs` (`+0`), the
calibration double `counter_read` subtracts (`+4`), and the
interpolator's offset and scale (`+0x18`, `+0x1c`), which
`counter_calibrate` stores from `reading_low` and the spread.  Between
them are pairs of a mode and a state: `counter_arm` loads the registers
when `registers_mode` is 1 and `registers_state` 2, and sets the state
to 1; `counter_calibrate` calibrates when `calibrate_mode` is 3, or 1
with `calibrate_state` 2; a third pair at `+0x10` counts `+0x14` down.

## The pllc task's SCPI requests

A setter does not store its new value: it sends the descriptor and the
value to the `pllc` exchange, and the loop task takes the request there
(`pll_scpi_receive`, `0x43340`) and handles it (`pll_scpi_command`,
`0x432bc`) with the firmware's own `validate_num` and
`finish_scpi_cmd`, which its messages name ("pll_scpi: validate_num()
-- too many in multiple", "pll_scpi: finish_scpi_cmd() -- unknown
structure type").  The request's descriptor, value index, source and
values are kept in `pll_scpi_param`, `pll_scpi_index`,
`pll_scpi_source` and `pll_scpi_values` (`0x102012` to `0x102020`).
`validate_num` (`0x42c10`) runs each value through the descriptor's
`validate` into `pll_scpi_validated` (`0x102030`).  `finish_scpi_cmd`
(`0x4306a`) switches on the descriptor's `type`: a numeric one's values
are stored through its `value`, its `changed` hook called for each and
its `done` hook once; a setting's byte, `pll_scpi_setting`, is stored
and its `changed` and `done` hooks called; types 2 and 3 store nothing.
It then saves by the descriptor's `flags`: bit 2 calls `nv_save`
(`scpi_param_saved_in_nv`, `0x4301a`), bit 4 writes the EEPROM record
at `0x400000` (`scpi_param_saved_in_record`, `0x43042`).

The setting descriptor, `scpi_setting_param`, has the same `type`,
`last_index` and `nv_widths`, its `changed` and `done` hooks at `+8`
and `+0x10`, its value at `+0xc` and its `flags` at `+0x14`.  Both
kinds point at `0x101f9a` (`+0x22`, `+0x18`), which nothing else
refers to; what it is was not traced.

In `nv_settings`, the bytes at `+0x0f`, `+0x1f`, `+0x47` and `+0x9b`
are padding: nothing refers to them, and each comes before a long.
`field_1e`, `field_b8` (0 to 50000) and `field_bc` are saved and
restored, but only their descriptors refer to them.  In
`counter_configuration` the third mode and state are the clock sync's:
`sync_request` sets `sync_mode` and `sync_state` to 2 with a
`sync_kind` and a `sync_count`, and `counter_arm` writes the kind into
`fconf_regs[3]` (`counter_set_sync`, `0x40a84`) while counting the
count down.

