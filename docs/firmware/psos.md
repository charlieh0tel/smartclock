# The real-time kernel

The firmware runs on pSOS, the real-time kernel whose name and headers
the image carries (`@(#)$Header: psos.S,v 1.4 95/01/11`, `psos_indep.h`
of 95/03/24, `PSOS-6803`) and whose errors it reports ("pSOS error",
"Last Valid pSOS Msg").  Its vocabulary is the original pSOS's, not
pSOS+'s: messages go through exchanges and signals through what the
console calls variables.  Addresses below are the Z3805A 3543B's.

## Calls

A program calls the kernel with `trap #3`, the service's number in D7
and its arguments in D0 to D3, A0 and A1; the kernel's handler,
`0x14b26`, takes 0 to `0x26` through the table at `0x14b6e` and answers
any other number with error `0x55`.  The error comes back in the low
byte of D7, zero for none, and results in D0, D1, D2, A0 and A1.

I/O goes through `trap #4`, `0x14c0a`: a function, 0 to 5, in D7 and a
device in the high byte of D0, which indexes a table of six routines a
device at `0x24` in the kernel's configuration.

`trap #7`, `0x1341a`, hands back the caller's status register in D0
and returns to it in supervisor mode.  `0x13422` wraps it, and
`0x13378` uses that to set the interrupt mask from its argument and
return the old status register.

The C programs call through a binding, one stub a service from
`0x16f74` up, each moving its arguments from the stack into registers,
calling `0x16f1e`, which makes the trap and turns D7 into the result,
and on success storing results through its pointer arguments.  A stub
that takes a name passes it through `0x16f50`, which packs four
characters into D0.  The I/O stubs, from `0x173ca`, call `0x16f36`.
The function check's model of the kernel (`tools/firmware/harness.py`,
`KERNEL_SERVICES` and `IO_FUNCTIONS`) tabulates the registers each stub
loads before the trap and stores from after it.  The installer has its
own copy of the binding, from `0x102a72`, and of the kernel: byte for
byte the primary's, `0xeca06` higher, from `installer::0x10152c`, but
for one absolute address.  Its routines carry the primary's names with
`installer_` before them.

## Inside the kernel

The handlers are named `kernel_` and the service, or `kernel_service_`
and its number where no console word names it; services 11 to 15, 17
and 34 share one that only clears D7, so they do nothing and succeed.
`movea.l (A6),A5` opens most handlers, loading the running task's
record; some handlers' bodies after it are entered again by
`kernel_delete` and `kernel_create`, with A5 the task being deleted
or created: `kernel_allocate_for_task` (`0x150dc`),
`kernel_free_for_task` (`0x15132`) and `kernel_send_for_task`
(`0x1559e`); `kernel_delete` also calls `kernel_signal_task`
(`0x157f0`), `signal_v`'s body, which ORs the events into a task's
pending word and wakes it if they satisfy its wait.  `jam_x` is
`send_x`'s body with A4 set.

A6 holds the kernel's configuration and state throughout.  The ready
queue is ordered by priority (`kernel_ready_insert`), delays wait in a
delta list in which each task holds its ticks past the one before
(`kernel_timer_insert`, `kernel_timer_unlink`), and each tick
(`kernel_process_ticks`) also ends the running task's time slice and,
every 24 hours, advances a date kept with a days-in-month table in which
`kernel_set_leap_year` makes February 28 or 29 days by the Gregorian
rule.  Memory comes
from two regions by first fit (`kernel_region_allocate`), each segment
stamped `0xe1d0`, which `kernel_check_segment` checks before a free.
Task and exchange IDs carry their record's address, complemented or
swapped, which `kernel_check_task` and `kernel_check_exchange` compare;
error 2 is a bad ID, 1 a deleted one and 3 one of another group.
`kernel_init`, trap #6, carves the configured memory into task records
of `0xb2` bytes, exchanges of `0x22` and message buffers of `0x18`;
`kernel_return_from_interrupt`, trap #5, and `kernel_switch_task`, on
the way out of a service, save the running task's registers in its
record and switch to the first ready one.

## Services

The SCPI task's creator, `0x29fce` (`console.md`, "Leaving
it"), shows three of them together: it looks the task up by name
with service 3, and if there is none creates it with service 1, at
the priority it is given and with `0x300` and `0x400` for the two
sizes, and starts it at `0x2a04a` with service 2.

The pForth console's words call the kernel directly (`console.md`),
and name the services they call; `0x19ed0` is their caller, which
reports an error.  A service no word calls keeps its number.

| Service | Stub | Word | Arguments, in order, and results |
| ------- | ---- | ---- | -------------------------------- |
| 0 | | `ver` | returns the version in D7 |
| 1 | `0x16f74` | `spawn` (create) | name, a long, the priority, two words that look like stack sizes, a long; the task's id through the last |
| 2 | `0x16fae` | `spawn` (start) | the task, its entry point |
| 3 | `0x16fca` | `ident`, `my_pid` | name; the id and a byte through the next two; `0x16ff8` passes no name and returns the caller's own |
| 4 | `0x17008` | `delete` | the task |
| 5 | `0x17020` | `suspend` | the task |
| 6 | `0x17038` | `resume` | the task |
| 7 | `0x17050` | `priority` | the task, the new priority; the old through the last |
| 8 | `0x170c0` | | four longs; a long through the last |
| 9 | `0x170f4` | | one long |
| 10 | `0x1710c` | | two longs |
| 16 | `0x17128` | | two longs |
| 18 | `0x1728a` | | three longs; two bytes through the next two |
| 19 | `0x1726e` | `jam_x` | the exchange, the message |
| 20 | `0x171ce` | `create_x` | name, two longs; the exchange's id through the last |
| 21 | `0x171fe` | | name; three longs through the next three |
| 22 | `0x1723a` | `delete_x` | the exchange |
| 23 | `0x17252` | `send_x` | the exchange, the message |
| 24 | `0x172c4` | `request_x` | the exchange, the buffer, two longs |
| 25 | `0x172e8` | `signal_v` | the variable, a long |
| 26 | `0x17304` | `wait_v` | three longs; a long through the last |
| 27 | `0x17332` | `get_v` | two longs; a long through the last |
| 28 | `0x17076` | `pause` | one long |
| 29 | `0x1735c` | | none |
| 30 | `0x17370` | | none; two longs and a word through three pointers |
| 31 | `0x173a6` | | three longs |
| 32 | `0x1708e` | `mode` | two longs; the old mode, a byte, through the last |
| 33 | `0x170b4` | | made by a trap in line, D7 kept |
| 35 | `0x17144` | | three longs; a long through the last |
| 36 | `0x1716e` | | two longs |
| 37 | `0x1718a` | | two longs; a long through the last |
| 38 | `0x171b2` | | two longs |

The console's `dev_init`, `dev_open`, `dev_close`, `dev_read`,
`dev_write` and `dev_ctrl` make I/O functions 0 to 5 in turn, through
`0x1a1b4`; `!iodev` closes a device and opens another, and `expect`
reads.

| Function | Stub | Arguments |
| -------- | ---- | --------- |
| 0, init | `0x173ca` | the device |
| 1, open | `0x173e2` | the device, a long |
| 2, close | `0x173fe` | the device |
| 3, read | `0x17416` | the device, the buffer, the length, in and out |
| 4, write | `0x17448` | the device, the buffer, the length, in and out |
| 5, control | `0x1747a` | the device, two longs |
