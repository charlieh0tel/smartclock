# The real-time kernel

The firmware runs on pSOS, the real-time kernel whose name and headers
the image carries (`@(#)$Header: psos.S,v 1.4 95/01/11`, `psos_indep.h`
of 95/03/24, `PSOS-6803`) and whose errors it reports ("pSOS error",
"Last Valid pSOS Msg").  Its vocabulary is the original pSOS's, not
pSOS+'s: messages go through exchanges and signals through what the
console calls variables.  Addresses below are the Z3805A 3543B's.

Which pSOS: the kernel's banner, beside its headers (`0x14ace`), is
`PSOS-6803`, then ` V4.1 COPYRIGHT 1983, 1984, 1985, 1986 SOFTWARE
COMPONENTS GROUP, INC. A`, so version 4.1, whose copyright ends in
1986, built into this firmware with headers of 1995.  Service 0,
`ver` (`kernel_service_0`, `0x14da2`), returns `0x33` in D7's high
byte and the banner's last letter, the `A` at `0x14b21`, in its low,
`0x3341`, which pForth's `ver` prints as the "pSOS version ID".  A
second copy of the banner and headers is at `0x1cac`.

No manual for pSOS 4.1 is at hand.  Its successor's, Integrated
Systems' *pSOSystem System Calls* (000-5070-002, September 1996, for
pSOS+), is a guide to what some of the services mean, not to their
numbers or registers: none of 4.1's calls (`spawn`, `create_x`,
`request_x`, `signal_v`, `wait_v`, `get_v`) and not the trap #3 binding
are in it.  `create` (`kernel_create`, `0x14dac`) fails as pSOS+'s
`t_create` does, the same four cases in the same order, numbered 0xD
lower: 1 no task record free (`ERR_NOTCB`, 0x0E), 2 no segment for the
stack (`ERR_NOSTK`, 0x0F), 3 a stack too small for the block it is to
start with (`ERR_TINYSTK`, 0x10), 4 priority 0 (`ERR_PRIOR`, 0x11).
And its two stack sizes, in D3's words, counted in words (`lsl.l #1`
at `0x14dee`), match `t_create`'s supervisor and user stacks: the high
word is the one that may be 0, and only when it is not is bit 0x1000
of the task record's `+0x44` set (`0x14e62`), as `t_create`'s user
stack may be 0 for a task that runs only in supervisor mode.

The earliest description found is Force Computers' brochure for
pSOS-68K, Software Components Group's kernel for the 68000
(<https://bitsavers.trailing-edge.com/pdf/forceComputers/brochures/Force_pSOS-68K_Brochure.pdf>),
undated, which names its "twenty-nine kernel calls" and six I/O
calls in a table.  Its vocabulary is 4.1's -- processes, exchanges,
spawn -- and its list runs in this kernel's service order where the
services are known: `Spawn_process` and `Activate_process` are 1 and
2, `spawn`'s two halves; `Identify`, `Delete`, `Suspend` and
`Resume_process` and the priority call 3 to 7 (Identify and Delete in
the other order); then `Allocate_segment`, `Free_segment` and
`Assign_segment`, which fit 8, 9 and 10 -- `pforth_dictionary_init`
gets the dictionary's segment with 8, and `console_task_create` gives
it to the new task with 10 -- and the six calls the brochure marks
"for MMU implementation only", which fit 11 to 15 and 17, those that
only succeed, there being no MMU.  `Create_exchange`,
`Delete_exchange`, `Attach_exchange`, `Send_message` and
`Request_message` fit 20 to 24, 21 taking a name and returning an
exchange's id; `Get_time_of_day` and `Set_time_of_day` fit 30, which
returns two longs and a word, and 31, which takes three longs.  The
I/O supervisor's six device procedures are trap #4's functions 0 to 5,
`Device_init` to `Device_control`.  4.1 has more than twenty-nine
services, 0 to `0x26`: the variables (`signal_v`, `wait_v`, `get_v`)
where the brochure has a process event, and others the brochure does
not name.

The handlers bear the fits out.  9's (`0x15130`) checks the
segment's stamp and that it is on the caller's list, unlinks it and
merges it with its free neighbors, and gives the memory to the tasks
waiting for it: `Free_segment`.  10's (`0x15324`) moves a segment from
the caller's list to another task's, `Assign_segment`; 16's
(`0x1532a`) is the same body with the two tasks exchanged
(`0x15346`), taking a segment back, which the brochure does not name.
29's (`0x1592e`) counts a tick at `+0x6c` of the data area, which the
kernel's exit runs (`kernel_switch_task`, `0x14c7e`), and advances the
time of day a second each time the ticks reach the configured rate
(`+0x86`): `Announce_tick`, which the periodic interrupt (`0x11f82`)
makes.  30 (`0x15974`) and 31 (`0x1599e`) keep the date as the year
in the high word, the month and the day, and the time as the hours,
the minutes and the seconds, a byte each, as pSOS+'s `tm_get` and
`tm_set` do, with the ticks into the second.  `Ret_from_interrupt` is
not a service but trap #5 (`kernel_return_from_interrupt`).
`Dispatch` is not identified: every service's exit switches tasks when
the trap came from interrupt mask 0 (`0x14b54`), so any of the
services that do nothing, 11 to 15, 17 and 34, would serve.  The rest
are named from pSOS+'s calls, which they match: 18 (`0x154ae`) gives a
message to every task waiting at an exchange, as `q_broadcast` does,
and queues copies up to a count; 32's (`0x158da`) mode byte holds
0x80, which stops the kernel's exit preempting the task (`0x14c9e`),
and 0x40, time slicing (`0x14d60`), `t_mode`'s `T_NOPREEMPT` and
`T_TSLICE`; and 35 to 38 (`0x15a10` to `0x15aa2`) make, delete, take
from and return to a partition of fixed blocks, as `pt_create`,
`pt_delete`, `pt_getbuf` and `pt_retbuf` do.  33 (`0x158f6`) puts the
caller in supervisor mode, as trap #7 does (below), which pSOS+ does
with a `t_mode` bit instead.

## Calls

A program calls the kernel with `trap #3`, the service's number in D7
and its arguments in D0 to D3, A0 and A1; the kernel's handler,
`0x14b26`, takes 0 to `0x26` through the table at `0x14b6e` and answers
any other number with error `0x55`.  The error comes back in the low
byte of D7, zero for none, and results in D0, D1, D2, A0 and A1.

I/O goes through `trap #4`, `0x14c0a`: a function, 0 to 5, in D7 and a
device in the high byte of D0, which indexes a table of six routines a
device at `0x24` in the kernel's configuration.  The kernel jumps to the
routine with the caller's registers, and the routine's error in D7 and
its results in the others go back through the `rte`; so these are
register routines, not C, the names file's `family=device`
(`structures.md`, `io_table`, lists them).  The ROM table the kernel's
is copied from, `io_table_defaults` (`0x5171a`), is followed by a fifth
device's six slots, `io_device_unused_stubs` (`0x517aa`), each `rts;
nop; nop` in place of a `jmp.l`; `psos_config_init` copies only the
four before it and `last_device` is 3, so no call reaches them.

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
The stubs are assembly a C caller calls (`psos_ident`, `0x16fca`;
`psos_service_21`, `0x171fe`; `psos_broadcast_x`, `0x1728a`): each
branches on the Z flag `0x16f1e` returns, set for no error, and stores
the service's results from the registers the trap left, saving only D7
and what it uses of D2 and D3.  Their hand-written C (`os.c`) calls the
binding the same way, in an asm statement that binds the registers
(`os.h`, `os_psos_call`; README.md, "Hand-written C"), not through a
wrapper that would hand the results on in globals another task's call
may overwrite.
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
pending word and wakes it if they satisfy its wait.  The drivers' events
are named by the firmware's own messages, `SIG_SIQ` and the like, in
`tools/ghidra/types/psos-signals.h`.  `jam_x` is
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
of `0xb2` bytes, exchanges of `0x22` and message buffers of `0x18`
(`smartclock.h`'s `psos_task`, `psos_exchange` and `psos_message`, the
data area `psos_kernel`, the services `psos_service`);
`kernel_return_from_interrupt`, trap #5, and `kernel_switch_task`, on
the way out of a service, save the running task's registers in its
record and switch to the first ready one.  Before `kernel_init` sits
`kernel_init_checked` (`0x15caa`), which nothing refers to: trap #6's
entry in the kernel's header, `primary_trap_6` (`0x14ab6`), branches
past it to `kernel_init` (`0x15cd8`).  It sums the kernel's words,
unsigned, into a long, from `0x14acc`, just after the stored sum, to
`psos_call` (`0x1606c`), both found PC-relative (`0x15caa`, `0x15cb0`),
and, unless `kernel_checksum` (`0x14ac8`, read at `0x15cc8`) is 0 or
the sum, puts error 4 in D7 and halts in `kernel_halt` (`0x15cd2`,
`0x15ec4`: `0xdead` in A0, then `stop #0`); else it falls into
`kernel_init`.  The stored sum is `0x028a9b53`; the words sum to
`0x028d24cc`, so the check would halt the kernel (`quirks.md`).  The
range holds the kernel's name, banner and code, the same bytes in the
installer's copy, `0xeca06` higher, so nothing in it depends on where
it lies; the long that depends on where the firmware put its
configuration, `0x51226` at `0x14ac2`, lies before it, as do the trap
entries.  Nor is it a wrong end: no run of words that starts between
`0x14a00` and `0x14d00` sums to the stored value.  Every image in
`third_party/firmware/` -- the 58503A's 3633 and 3704, the 58503B's
1.01.04, the Z3801A's 3543, this one, the Z3815A's 4010 and the
Z3816A's 4001 -- has the same stored sum in both its copies of the
kernel, and every copy sums to `0x028d24cc` but the 58503A 3633's
first (`0x1c8e`), which differs from its second in 758 bytes and sums
to `0x028ce1d4`: the sum was not made over the kernel these images
carry.  The installer's copy here
(`installer_kernel_init_checked`, `installer::0x1026b0`) is the
primary's byte for byte, and as unreachable.  Service 0
has a binding too, `psos_version` (`0x16f68`), which nothing calls.

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
| 1 | `0x16f74` | `spawn` (create) | name, a long, the priority, two stack sizes in words (supervisor and user, as `t_create`'s, above), a long; the task's id through the last |
| 2 | `0x16fae` | `spawn` (start) | the task, its entry point, and no mode, which pSOS+'s `t_start` takes (000-5070-002, `t_start`): `kernel_start` (`0x14ea0`) reads only A0 and A1, so the D0 to D3 the stub passes on as its caller left them go unread, and the task starts in user mode with its interrupts unmasked, the status register its record's `+0x44`, which `kernel_create` clears (`0x14e5c`) and only `kernel_init` sets, for IDLE, to supervisor mode (`0x15eb4`) |
| 3 | `0x16fca` | `ident`, `my_pid` | name; the id and a byte through the next two; `0x16ff8` passes no name and returns the caller's own |
| 4 | `0x17008` | `delete` | the task |
| 5 | `0x17020` | `suspend` | the task |
| 6 | `0x17038` | `resume` | the task |
| 7 | `0x17050` | `priority` | the task, the new priority; the old through the last |
| 8 | `0x170c0` | allocate segment, by the brochure (above) | four longs; a long through the last |
| 9 | `0x170f4` | free segment, likewise | the segment, as 8 returns it; error 5 one not stamped `0xe1d0`, 1 one not the caller's |
| 10 | `0x1710c` | assign segment, likewise | the segment, the task (0 the caller): moves the segment from the caller's list to the task's; the task's errors, 1 to 3, then 9's |
| 16 | `0x17128` | | the segment, the task: 10 the other way, from the task's list to the caller's; 10's errors; nothing calls it |
| 18 | `0x1728a` | broadcast | the exchange, the message, a count in D0's low byte: gives the message to every task waiting at the exchange, then queues as many copies as the count exceeds the tasks given it; the larger of the count and the tasks given it, and those tasks, a byte each, through the next two; error 7 a message whose first long, `send_x`'s reply exchange, is not 0, 6 the exchange's queue full, 5 no buffer; nothing calls it |
| 19 | `0x1726e` | `jam_x` | the exchange, the message |
| 20 | `0x171ce` | `create_x` | name, two longs; the exchange's id through the last |
| 21 | `0x171fe` | attach exchange, likewise | name; three longs through the next three |
| 22 | `0x1723a` | `delete_x` | the exchange |
| 23 | `0x17252` | `send_x` | the exchange, the message |
| 24 | `0x172c4` | `request_x` | the exchange, the buffer, a flag (D1) not to wait, and the ticks to wait (D0), 0 without end: error 0x80 for none without waiting, as from an interrupt (`0x15740`), 0x81 when the ticks run out (`kernel_process_ticks`, `0x14d18`) |
| 25 | `0x172e8` | `signal_v` | the variable, a long |
| 26 | `0x17304` | `wait_v` | three longs; a long through the last |
| 27 | `0x17332` | `get_v` | two longs; a long through the last |
| 28 | `0x17076` | `pause` | one long |
| 29 | `0x1735c` | announce tick, by the brochure | none: counts a tick for the kernel's exit to process and advances the time of day a second every second's worth of ticks; the periodic interrupt, `0x11f82`, calls it |
| 30 | `0x17370` | get time of day, likewise | none; the date (the year in the high word, the month, the day), the time (the ticks' low byte, the hours, the minutes, the seconds) and the ticks into the second, a word, through three pointers, which the stub stores even on error 1, the time never set; nothing calls it |
| 31 | `0x173a6` | set time of day, likewise | the date and time as 30 returns them, the time's top byte ignored, and the ticks in D2's low word; error 2 a bad time or tick count, 1 a bad date; `klok_task_create` sets midnight of the date at `0x101d04` |
| 32 | `0x1708e` | `mode` | the new mode and a mask, low bytes, to set the masked bits of the task's mode byte, `+0x11` of its record: 0x80 no preemption, 0x40 time slicing; the old mode, a byte, through the last; no errors |
| 33 | `0x170b4` | | none: sets the supervisor bit in the status register the trap returns to, so the caller goes on in supervisor mode, and returns the old in D0's low word; made by a trap in line, D7 kept, since the stack changes; `klok_task` and `spoo_task` open with it |
| 35 | `0x17144` | create partition | the area, the block size, the count of blocks in D1's low word: a 12-byte header, then the blocks, each after 8 bytes of its own (its next free block or its partition, and the size); the area, the partition's id, through the last; no errors; nothing calls 35 to 38 |
| 36 | `0x1716e` | delete partition | the partition, its block size; error 1 a size not the partition's, 2 a block not returned |
| 37 | `0x1718a` | get block | the partition, its block size; a block, from A1, through the last; error 1 a size not the partition's, 2 none free |
| 38 | `0x171b2` | return block | the partition, the block; error 1 a block not out from that partition |

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
