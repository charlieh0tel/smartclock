# Structures

The firmware's data structures and named globals, as the code that
uses them shows them, in the Z3805A 3543B image.
`tools/ghidra/types/smartclock.h` declares the structures for Ghidra, a
field named only where this note gives the code that shows what it is;
`tools/ghidra/names/z3805a-3543b.tsv` names and types their instances
and the globals.

`make check-types` compiles `tools/ghidra/types/check-layout.c` with an
m68k compiler (`m68k-linux-gnu-gcc`, or `M68K_CC`) and fails if a
structure's size or a named field's offset is not the one given here,
an offset read from the image written as the difference of its
addresses.  The header packs its structures, but every one has the
same layout unpacked under the 68000's 2-byte alignment: they are what
a 68000 compiler lays out, not byte layouts that happen to fit.

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
| `+0x06` | the minimum | the queries' `MINimum` reads through it; `:GPS:EMANgle`'s is 0 |
| `+0x0a` | the maximum | `MAXimum` reads through it; `:GPS:EMANgle`'s is 89 |
| `+0x0e` | a conversion routine | `scpi_numeric_get` calls it with the value |
| `+0x16` | the current value, in RAM | `scpi_numeric_get` reads it |
| `+0x26` | the value is an integer | `scpi_numeric_get` converts an integer to float for the reply |

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

The GPS engine's data, each record filled by a message's decoders in
the GPS task's message table (`oncore/z3805a-3543b.txt`), its fields
those of the message in the VP Oncore reference
(`third_party/motorola/VPCommands.pdf`), in the message's order:

| Record | Type | Message | Fields | Printed after |
| ------ | ---- | ------- | ------ | ------------- |
| `oncore_time`, `0x100c6b` | `oncore_time_fields` | `@@Aa` | hour, minute, second | `TIME` |
| `oncore_gmt_offset`, `0x100c6e` | `oncore_gmt_offset_fields` | `@@Ab` | sign, hours, minutes | `GMT CORR` |
| `oncore_date`, `0x100c72` | `oncore_date_fields` | `@@Ac` | month, day, a word of year | `DATE` |
| `oncore_position`, `0x100c76` | `oncore_position_fields` | `@@Ad`, `@@Ae`, `@@Af` | latitude, longitude (mas), GPS and MSL heights (cm) | `LAT`, `LON`, `GPS HGT`, `MSL HGT` |
| `oncore_mask_angle`, `0x100c86` | byte | `@@Ag` | | `ELEV MASK` |
| `oncore_satellite_select`, `0x100c87` | byte | `@@Ah` | | `SELECTION` |
| `oncore_hold_position`, `0x100d10` | `oncore_hold_position_fields` | `@@As` | latitude, longitude, height | `POS HOLD` |
| `oncore_pps_offset`, `0x100d2a` | long | `@@Ay` | | `1PPS OFFS` |
| `oncore_pps_cable_delay`, `0x100d2e` | long | `@@Az` | | `CABLE DLY` |
| `oncore_visibility`, `0x100d7c` | | `@@Bb` | | `Visible Sats` |
| `oncore_extension`, `0x100fae` | `oncore_extension_fields` | `@@Bk` | five DOPs, magnetic variation, velocity north, east and up, correction age, ECEF x, y and z, six URAs, the ten elements of the least-squares A-1 matrix, clock bias, oscillator offset | the DOP, `VEL`, `ECEF`, `URA` and clock lines |

The print shows latitude and longitude as "ms"; the reference's unit is
the milliarcsecond.  `@@As`'s height goes through a decoder of its own
(`0x4efb4`), not to a fixed cell, but is printed from `+0x08`.
`@@Bk`'s 62 bytes of data fill `0x100fae`--`0x100feb` exactly, and
`gps_print_extension` prints the matrix as the triangle the reference
numbers it in.

The PLL's EFC history, `efc_history` at `0x1021ac` (`efc_ring`, `0x366`
bytes, cleared as one with `memset`), is a ring of 64 averages of the
EFC.  `efc_history_update` (`0x4137e`) adds the EFC, less the temperature coefficient
(`:DIAG:ROSC:TCO`) times an ADC reading, to `efc_sum` each sample in
some states, counting `summed_count`, and each 2700 samples
(`sample_count`) stores an entry at `stop_ptr` and advances it, pushing
`start_ptr` on when the ring is full.  Its debug message names the
arrays: "e_avg= %.1f time= %f weight= %d log= %.2f tfom= %.1e".

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

The PLL's other values `pll_rep` and `pr_efc` print: `tfom`
(`0x102198`, a float, "tfom = %.1e secs"), `max_loop_time`
(`0x101e84`, a float) and `efc` (`0x102194`, a float).

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
