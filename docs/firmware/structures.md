# Structures

The firmware's data structures, as the code that uses them shows them,
in the Z3805A 3543B image.  `tools/ghidra/types/smartclock.h` declares
them for Ghidra, a field named only where this note gives the code that
shows what it is; `tools/ghidra/names/z3805a-3543b.tsv` names and types
their instances.

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
