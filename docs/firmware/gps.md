# The GPS receiver link

How the Z3801A 3543, the Z3805A 3543B and the 58503A 3633 and 3704
talk to their Motorola VP Oncore engine, and so what an engine in
their place must do.  The source is the hand-written C of each image,
`third_party/firmware/rewritten/IMAGE/` (`gps_oncore.c`,
`gps_requests.c`, `gps_task.c`, `gps_receiver.c`,
`pll_gps_control.c`, `types/smartclock.h`), and the ROMs.  Addresses
are the 58503A 3704's unless an image is named.  3633 has no C: its
facts here were read from its ROM and are marked so.  Message
layouts are `third_party/motorola/VPCommands.pdf`, cited by page.  The
Z3816A, the 58503B and the Z3815A come in where named, and in "In
every image".

## The serial link

The Oncore is on channel A of a 68681 DUART at `0x200000`, whose
sixteen registers sit on odd byte addresses (`0x200001` + 2*n).
`rewritten/common/duarta.c` is the driver: `duarta_isr` puts each
received byte in a ring, `drta_get_byte` takes one, and
`drta_send_message` sends a command by interrupt.  In the Z3816A 4001:

| Function | Role |
| -------- | ---- |
| `FUN_0002dbdc` | DUART interrupt handler; reads the interrupt status at `0x20000b` and dispatches |
| `FUN_0002dab2` | channel A receive; copies bytes from `0x200007` into a 256-byte ring at `0x103595` (write index `0x103593`, read index `0x103594`) and posts event `0x4000` |
| `FUN_0002d6dc` | takes one byte from the ring |
| `FUN_00050314` | the message framer |

Channel B's receive routine (`FUN_0002db42`) records error bits and
buffers no data; `FUN_0002dc9a` is a channel B loopback self-test that
sends `DUART` and checks the echo.

## Framing

`gps_receive` (3704 `0x4c124`, Z3805A `0x4b846`, Z3801A `0x4b804`;
the Z3816A's `FUN_00050314`, state at `0x1016e7`) frames one message
at a time into `gps_response_buffer`, 294 bytes:

| State | Takes |
| ----- | ----- |
| 0, 1 | `@` |
| 2 | first ID character, an upper-case letter |
| 3 | second ID character, a letter; then looks the ID up in the message table for its length (`gps_response_lookup`, `0x4c090`) |
| 4 | the body, to the table's length; the last byte is the checksum, the XOR of every byte from the ID on (`oncore_checksum`) |
| 5, 6 | CR, then LF |

A wrong byte counts a frame error and starts again, from that byte if
it is an `@`; a bad checksum counts and starts again.  So:

- The engine must send each message at exactly the table's length
  (below).
- An ID not in the table is counted and not framed: the framer stays
  in state 3 and tests each further letter with the one before as an
  ID.
- Never send `@@Ao`, `@@Bh` or `@@Ci`.  Their table length is 0, so
  the body count starts at -6 in a 16-bit counter
  (`gps_response_remaining`) and runs 65530 bytes, past the buffer
  and into `gps_response_cursor` behind it.

Each of the three, given to the 58503A 3704's framer amid the engine's
once-a-second `@@Ea` and `@@En`, was run against the ROM's code and
against the hand-written C, with the same outcome; the times are the
ROM's.  Nothing more is framed.  The buffer's 294 bytes fill; the
295th and 296th bytes from the message's first `@` land on the top
half of `gps_response_cursor` (`0x101520`), which the framer steps in
memory before each store (`0x4c234`), so the stores go on from
`0x1522` plus the 296th byte times `0x10000`, the 295th lying above
the 24-bit bus, wherever the traffic puts them:

- `@@Ao`: the byte was `0x00`, so the stores went to the flash's
  write select, CS6, from `0x1522` (restart.md, "The chip selects");
  the Am29F010s ignore a write outside a command sequence (Am29F010B
  data sheet, "Command Definitions"), and ascending stores never form
  one.  The count ran out 435 s later and framing resumed.
  Meanwhile the unit went to holdover 63 s after the message (`Holdover
  started, not tracking GPS`) with its alarm, and locked again 543 s
  after it.
- `@@Bh` and `@@Ci`: the byte was `0x23`.  `0x231522` has no chip
  select, so the bus monitor ended the store with a bus error in
  `gpsm`, which no handler takes, and the watchdog restarted the unit
  7 s later: `Re-boot`, and `Exception: gpsm, vector C008, PC =
  0004C23A, SR = 0000, addr = 00231522` in the second log
  (restart.md).

The outcome is the line's, not the message's.  A 296th byte of `0x10`
would send the stores through RAM from `0x101522`, the cursor's own
low half, `gps_counts` and the framer's state first; that was not
seen.

A complete message is decoded at once, whatever the task is waiting
for (`gps_response_decode`, `0x50d3e`): its decoders unpack it into
the task's records.  So the firmware's view of the engine, its fix
mode (`@@Cg`) and position hold (`@@At`) among it, is what the engine
last sent, not what the firmware commanded.

## The message table

Each image's GPS task holds one table of the messages it sends and
takes: Z3801A at `0x4fe8c`, Z3805A `0x4fece`, 3633 `0x50f82`, 3704
`0x51178`, Z3816A `0x56732`.  `oncore/README.md` gives an entry's
layout, `oncore/models.md` the tables' sizes, and `oncore/IMAGE.txt`
each table, from `smartclock-cli dump-oncore`.

An entry's timeout and match kind say how long the task waits for an
answer and what answers.  The messages, with each one's length, `@@`
to LF, and timeout, the same in each of the four images that has the
message:

| Message | Length | Timeout, s | Answered by | Images |
| ------- | ------ | ---------- | ----------- | ------ |
| `Aa`, `Ac` | 10, 11 | 10 | any `Aa`, `Ac` | all |
| `Ab` | 10 | 10 | an echo, fixed up (below) | all |
| `Ad`, `Ae` | 11 | 10 | an echo | all |
| `Af` | 15 | 10 | the height (below) | all |
| `Ag`, `Ah`, `Aj`, `An`, `Aq`, `Ar`, `At`, `Av`, `Aw`, `AB` | 8 | 10 | an echo | all |
| `Ai` | 9 | 20 | an echo | all |
| `Ak`, `Al`, `Ax`, `AC`, `AD` | 9 | 10 | an echo | all |
| `Am`, `Au` | 12 | 10 | an echo | all; `Au` 58503A only |
| `Ay`, `Az` | 11 | 10 | an echo | all |
| `Ao` | 0 | 10 | `@@Ap`, 25 bytes | all |
| `As` | 20 | 10 | the position (below) | all |
| `Ba` | 68 | 10 | any `Ba` | all |
| `Bb`, `Bc`, `Bd` | 92, 82, 23 | 10 | any of the same ID | all |
| `Bh` | 0 | 10 | `@@Ce`, 52 bytes | all |
| `Bj` | 8 | 10 | any `Bj` | all |
| `Bk` | 69 | 10 | any `Bk` | all |
| `Bn` | 59 | 10 | any `Bn` | all |
| `Ca` | 9 | 20 | any `Ca` | all |
| `Cf` | 7 | 10 | an echo | all |
| `Cg` | 8 | 20 | an echo; mode 2, any `Cg` | all |
| `Ci` | 0 | -- | nothing; not waited for | all |
| `Cj` | 294 | 10 | any `Cj` | all |
| `Ea` | 76 | 10 | any `Ea` | 58503A |
| `Ek`, `En` | 71, 69 | 10 | any of the same ID | 58503A |
| `Fa` | 9 | 20 | any `Fa` | 58503A |
| second `Ea`, entry `0x36` | 76 | 4 (3633: 5) | any `Ea` | 58503A |
| second `Ar`, entry `0x37` | 8 | 10 | an echo; see below | 58503A |

Tables, lengths, timeouts and kinds were read from the ROMs; 3633's
differ from 3704's only in the second `Ea`'s timeout and the filters'
addresses.

## Answer matching

After sending a message the task waits for its answer
(`gps_task_wait`, 3704 `0x4cdda`, Z3805A `0x4c31a`).
`gps_response_match_58503a` (3704 `0x50e16`, jump table `0x50e54`;
3633 `0x50c1a`, read from the ROM) and the Z380x's
`gps_response_match` (Z3805A `0x4fb86`, Z3801A `0x4fb44`) judge each
complete message by the sent message's kind (`oncore_match_kind`).
The 58503A adds kinds 4 and 11, so the Z380x's 4 to 9 are its 5 to 10:

| 58503A | Z380x | Kind | Answered by |
| ------ | ----- | ---- | ----------- |
| 0 | 0 | any | nothing; the task goes on at once (`@@Ci`) |
| 1 | 1 | echo | the same ID, every byte after it the same, checksum included |
| 2 | 2 | GMT offset | an echo, but a negative offset under an hour, sign `0xff`, hours 0, minutes *m*, is compared as sign 0, hours 0, minutes -*m* (VPCommands p. 10) |
| 3 | 3 | height | the 4-byte height alone, in the response's GPS height for type 0 and its MSL height, 4 bytes on, for type 1 (p. 26) |
| 4 | -- | second `Ar` | an echo, taken as entry `0x37`'s |
| 5 | 4 | hold position | an echo; latitude and longitude alone when the type is the same, the height sent is -100000 to 1800000 cm and the response's is within 2 cm of it (p. 55) |
| 6 | 5 | fix mode | an echo; mode 2, the query, by any `@@Cg` (p. 119) |
| 7 | 6 | same | any message of the same ID, its bytes not compared |
| 8 | 7 | user datum | `@@Ap` |
| 9 | 8 | never | no message has it |
| 10 | 9 | corrections | `@@Ce` |
| 11 | -- | `Ea` | `@@Ea`, decoded as entry `0x32` |

A query (argument mode 1) of kinds 1 to 6 is answered by any message
of the same ID.  The verdict is 2, answered; 1, the answer but its
bytes differ; or 0, not the answer.  The task then:

- On 2, goes to the script's next step.
- On 1, sends again, unless the response is the one that differed
  last (its first 25 bytes kept), which is a REJECT and ends the
  request.
- On 0, keeps waiting.
- When uptime seconds since the send exceed the timeout, sends again;
  after 3 sends, TIMEOUT, ending the request.  The second `@@Ea` is the
  exception: after 3 sends the script passes it over (3704 `0x4ce02`;
  3633 `0x4cc82`, read from the ROM).

Kind 4 sets the response's index to `0x37` before comparing whatever
it is (`0x50e6c`).  So while it waits on the second `@@Ar`, any other
message, a continuous `@@Ba` say, is compared as its echo, differs in
length and makes the task send `@@Ar` again.

`@@Ao` (p. 46) is answered by `@@Ap` in every image, where the
reference gives `@@Ao` itself, 25 bytes.  With `@@Ao`'s table length
0, a 25-byte `@@Ao` overruns the framer (above), so these engines
presumably answer with `@@Ap`.  `@@Ap` has no decoder; only its ID,
length and checksum matter.

## Decoding

Decoders unpack a message through a byte program rather than code, so
no instruction reads a field of the raw message directly.  In the
Z3816A, through `FUN_00054ec8`; the opcodes:

| Byte | Meaning |
| ---- | ------- |
| 1..127 | copy that many bytes |
| `0x00` | skip one destination byte |
| `0x80` | end |
| `0x81` | end of a repeat group |
| `0x82` | skip one source byte |
| `0x83` | skip one source byte, write zero |
| `0x84` | write a zero byte |
| `0x85` | write a sign-extension byte from the next source byte |
| other negative | start a repeat group of that count |

The Z3816A's `En` program, at `0x57e39`:

    02 84 84 03 00 84 03 04 00 0a 00 f8 01 00 04 81 80

Against the layout in VPCommands pp. 141-142 (message byte 0 being
the first `@`), its fifth copy moves message bytes 16 to 25 -- hours,
minutes, seconds, pulse status, 1 PPS sync, Time RAIM solution status,
Time RAIM status, the two-byte one-sigma estimate, and the negative
sawtooth -- to record offsets 17 to 26.  Its `Bn` program at `0x57e25`
differs only in its channel count.  The sawtooth is at offset 26 of
the decoded record.

The Z380x and 58503A programs put it one byte earlier, at record + 25
(`oncore_raim_fields`):

| Image | `Bn` | `En` |
| ----- | ---- | ---- |
| Z3805A, `0x50fe6` | `02 00 03 00 85 03 04 00 0a 00 fa 01 00 04 81 80` | -- |
| 3704, `0x525ef`, `0x52602` | `02 00 03 00 84 03 04 00 0a 00 fa 01 00 04 81 f4 84 81 80` | `02 00 03 00 84 03 04 00 0a 00 f8 01 00 04 81 80` |

The 58503A writes zero for the top byte of the 1PPS rate where the
Z380x sign-extends it, and its `Bn` zero-fills channels 7 and 8, so
one record serves both messages.

## Six or eight channels

Which engines each image can take, by the two message pairs the VP
Oncore reference gives in six- and eight-channel forms: position/status
`@@Ba` / `@@Ea` (pp. 70, 124) and Time RAIM `@@Bn` / `@@En` (pp. 102,
138).  A message counts as handled when the image holds a pointer to
its ID:

| Image | `Ba` | `Ea` | `Bn` | `En` |
| ----- | ---- | ---- | ---- | ---- |
| Z3801A 3543 | yes | no | yes | no |
| Z3805A 3543B | yes | no | yes | no |
| 58503A 3633 | yes | yes | yes | yes |
| 58503A 3704 | yes | yes | yes | yes |
| Z3816A 4001 | yes | yes | yes | yes |

The Z380x send every message to any engine but `@@Bn`, which
`gps_message_supported` (Z3805A `0x4f8fc`) refuses before `@@Cj` has
come and after one without an `I` in its `OPTIONS LIST`
(`gps_parse_options`, `0x4f8b6`).

The 58503A decides at run time, from three bits of `gps.field_592`
(`0x1013e2`, `gps_field_592_bit`) and the channel count
`gps.channel_count` (`0x1013e0`):

- **Bit 5**, an `@@Ea` seen, is set only by the `@@Ea` decoders,
  `oncore_position_status_8_message` (`0x506e2`) and
  `oncore_eight_channels_seen` (`0x5098a`), and cleared only when `gps`
  is.  `gps_channel_count_update` (`0x509fe`) makes the count 8 with
  it and 6 without.
- **Bit 6**, the Time RAIM option, and **bit 7**, software 8.4, come
  from `@@Cj` in `gps_receiver_abilities_update` (`0x50a16`), step
  `0x4d` of the scripts: bit 6 when the line starting `OPTIONS LIST `
  contains an `I`, bit 7 when the `SOFTWARE VER #` value × 10 plus the
  `SOFTWARE REV #` value is 84.  Before a `@@Cj` it clears both.  The
  labels are matched from the table at `0x5262c`; a line without its
  label gives `strchr` and `atoi` a null pointer.
- `gps` is cleared at start (`main`) and by the master reset script
  `0x52` (`gps_script_lookup` calls `gps_init_58503a`).  The count is
  then 0, which refuses `Ba`, `Bn`, `Ea`, `Ek` and `En`, until step
  `0x4d` after `@@Cj` makes it 6.

`gps_request_build` (`0x50bc8`) sends nothing when an entry's send
filter, at `+0x38`, returns 0:

| Entry | Filter | Passes |
| ----- | ------ | ------ |
| `Ba` `0x1e` | `oncore_filter_channels`, `0x50ad8` | count 6; count 8 at rate 0, its first argument |
| `Bn` `0x29` | `oncore_filter_traim`, `0x50b48` | bit 6, then as `Ba` |
| `Ca` `0x2a` | `oncore_filter_six_channels`, `0x50ab8` | bit 5 clear |
| `Ea` `0x32`, `Ek` `0x33` | `oncore_filter_channels` | count 8 |
| `En` `0x34` | `oncore_filter_traim` | bit 6 and count 8 |
| `Fa` `0x35` | `oncore_filter_eight_channels`, `0x50ac8` | bit 5 set |
| second `Ar` `0x37` | `oncore_filter_software_8_4`, `0x50b6c` | bit 7 |

`@@Bk` (`0x27`) has no filter, so goes to every engine:
`oncore_filter_channels` tests for it, but no entry gives it that
filter.  Neither has the second `@@Ea` (`0x36`).  So an eight-channel
engine is polled with `Ba` and `Bn` (rate 0) and given continuous
output only in `Ea` and `En`; `Bk` is polled whatever the count.  A
refused message is skipped and the script goes on.

**The second `@@Ea`** opens script `0x55`, the engine self-test:
`Ea`(`0x36`)/0(0), step `0x49`, `Ca`, `Fa`, `Cg`(1), step `0x49`,
`Ea`(`0x32`)/0(1).  With no filter it goes to every engine, at rate
0, a poll.  An `@@Ea` answers it (kind 11); the framer looks the ID up
as entry `0x32`, whose decoder sets bit 5, so the count becomes 8, and
entry `0x36`'s own decoder never runs.  After 3 unanswered sends the
script goes on, so a six-channel engine costs it about 3 × (timeout +
1) seconds.  The script then runs whichever self-test the count
passes and, with eight channels, turns `Ea` on.  It runs at every
power-up: `pllp_task` (`0x47716`), in the power-up stage, calls the
power-up self-test `selftest_print_failure` (`0x18764`), whose `GPS`
check, `selftest_gps_with_lamp` (`0x18ed6`), calls `gps_selftest_run`
(`0x4687e`).  A requested self-test runs it too (`gps_diag_sequence`,
`0x473c6`; the console's).  So an eight-channel engine is found at
power-up, and a six-channel one costs the boot the three unanswered
sends before the init script, and its `@@Bk`, can run.  That this script is how the firmware means to find an
eight-channel engine is inference.

**The second `@@Ar`**, argument 65 (`0x41`), follows `Ar`(1) in 3704's
scripts `0x52` and `0x53` and 3633's `0x53`; 3633's `0x52` sends
`Ar`(65) alone (read from the ROM).  It goes only to software 8.4.
VPCommands p. 53 defines 0, Best-4, and 1, N-in-View; what 65 does is
not known.  The Z380x send only `Ar`(1); the Z3816A and the 58503B
also send 65.

3633 has the same selector at `0x50802`, the same two `bset` sites at
`0x5051c` and `0x5078e`, the same identity tests at `0x50834` and
`0x5088e`, and filters at `0x508dc`, `0x508bc`, `0x508cc` and
`0x5094c` (read from the ROM).  The three bench engines all report
options `IB`; the two Z380x engines report software 8.4 and the
58503A's 8.8.  None of this has been run with an engine other than
the one each unit came with.

## The cable delay

`@@Az` sets the 1PPS cable delay, `tttt`, a signed 32-bit count of ns
(p. 69, 0 to 999999).  The Z380x send the `:GPS:ADELay` setting as
given.  The 58503A adds `pps_cable_delay_offset` (`0x10201c`) in
`oncore_cable_delay_encode` (3704 `0x4feec`; 3633 `0x4fd64`, the same
code, read from the ROM), with no clamp.

- The offset is `:GPS:REFerence:BIAS`, 0 to 999 ns: the descriptor at
  `0x33c8e` holds its value there, and its done hook,
  `calibration_block_save_reported` (`0x30c0a`), writes it to the
  EEPROM calibration block (`save_calibration_block_58503a`,
  `0x30b24`).  3633 has a pointer to the same address at `0x3395a`
  (read from the ROM).  The Z3805A has no such command
  (`58503a-3704.md`, "The SCPI trees").
- `calibration_block_read` (`0x30ba8`) loads it from the block's word
  at `0x40008a`, unsigned, so 0 to 65534; `0xffff` or a bad checksum
  gives 0.
- The decoder, `oncore_cable_delay_decode` (`0x5003e`), keeps the
  engine's value in `gps.pps_cable_delay_given` and, less the offset,
  in `gps.pps_cable_delay`, which the status screen shows as
  `CABLE DLY`.
- `@@Az` is an echo: the engine must return `tttt` exactly.  Start-up
  sends it from `gps_init_sequence` (`0x46a00`), which on any failure
  logs `ant delay ack failure` and sends it again, without end.

## Time RAIM

`@@Bn` (pp. 102-106) and `@@En` (pp. 138-142) carry Time RAIM's
settings and status.  The decoders (3704 `0x50924`, `0x50946`; 3633
`0x50728`, `0x5074a`, read from the ROM) fill `gps.raim` and set
`traim_message_arrived`.  By message byte, `@` being 0:

| Byte | Field | Used by |
| ---- | ----- | ------- |
| 4 | output rate | the status monitor (below) |
| 5 | Time RAIM on | `raim_monitor`: must be 1 |
| 6-7 | alarm limit, 100 ns | display |
| 8 | 1PPS control mode | display |
| 9-18 | 1PPS rate, time of next pulse | nothing |
| 19, 20 | pulse status, pulse sync | display |
| 21 | solution status: 0 OK, 1 ALARM, 2 UNKNOWN | `raim_monitor` |
| 22 | Time RAIM status: 2, neither detection nor isolation | `raim_monitor` |
| 23-24 | one-sigma estimate, ns | display |
| 25 | negative sawtooth, ns | display ("Where the sawtooth is read") |
| 26 on | per channel: satellite, time estimate | display |

`raim_monitor` (3704 `0x4b734`, 3633 `0x4b5e0` read from the ROM,
Z3805A `0x4b01e`, Z3801A `0x4afdc`) is the same in all four:

- Inactive unless Time RAIM is on (1), a `Bn` or `En` has been
  received (`gps.raim_received`, which the status monitor sets), the
  Time RAIM status is not 2 and the solution status not 2.
- From inactive, OK on an OK solution, else alarm detected.  From OK,
  alarm detected on solution 1.  Alarm detected becomes an alarm
  after 4 s not OK.  An OK solution clears either.
- An alarm makes the GPS reference invalid, state
  `GPS_SYNC_RAIM_ERROR`, "Invalid: Time RAIM error", when the unit
  holds position or surveys; the loop then enters holdover, logged as
  the RAIM alarm (`pll_stages_gps_holdover_event`).  In the 58503A
  (`gps_receiver_status_update_58503a`, `0x4b8a2`) it goes there
  directly, not through questionable.

What the firmware sends: `raim_request_arguments` (`0x4cfbc`), step
`0x4a`, gives rate 1, Time RAIM on or off from
`nv_settings.raim_state` and the alarm limit from
`nv_settings.raim_threshold`, each the in-hold setting `[0]` or the
other `[1]`, and 1PPS mode 1; `gps_request_build` adds 1PPS rate and
time `0 0 1 0 0 0 0 0 0 0`.  The 58503A's init scripts also poll
`Bn`/0(0, 0, 10, 1).  1PPS mode is always 1, on all the time, so the
engine's pulse gating is never used, and no code outside the display
and `raim_monitor` reads the record.  For an engine, only the rate
and on bytes, the two statuses and the steady arrival of `Bn` or `En`
change the firmware's behavior.

The SCPI Time RAIM commands (`:DIAG:GPS:POS:HOLD:RAIM:STATe` and
`THReshold`) ask `gps_message_supported` too: before `@@Cj` they fail
with an execution error, or for a query data stale; without the
option, with -241, hardware missing.

## The monitors

Both run on each pass of the GPS task (`command_monitor`, 3704
`0x4bce6`); while either has failed, the receiver's health is failed.

**The status monitor** (3704 `0x4be5c` to `0x4c02a`) watches while
script step `0x4e` has enabled it and the last `Ba` or `Ea` sent asked
a nonzero rate (`gps.status_rate`); in the Z380x also only in
position-fix mode.  Each `Ba` or `Ea` decoded is fresh.  With *r* the
rate:

| | 58503A | Z380x |
| - | ------ | ----- |
| stale after, s | 3*r* | -- |
| missing after, s | 36*r* | 15*r* |
| new `Ba` times without a `Bn` or `En`, while the last `Bn`'s rate is nonzero | 20 | 7 |
| `Ba`s with the same time | 46 | 45 |

The 58503A's figures were read from the C and checked against both
ROMs' compares.  On any of the last three it posts script `0x56`
(Z380x `0x4b`), outputs on, and watches again; a second miss in a row
fails the monitor, and the reference becomes "Invalid: GPS rcvr
failed".  So the engine's `Ba` or `Ea` time must advance, and with
Time RAIM output on, `Bn` or `En` must keep coming.

**The command monitor** (3704 `0x4bd16` to `0x4be4a`), after a
command times out or is rejected, fails and every 20 s posts the
next query of `command_monitor_queries` (3704 list `0x523d0`: `Cj`,
step `0x4d`, then the queries of `Aa` to `Cg`) until one is answered.

## Channel mode and signal strength

`@@Ba` and `@@Ea` give each channel a satellite ID, a tracking mode, a
signal strength and a status byte (VPCommands p. 72, p. 126), kept as
`gps.status.channels` (`oncore_channel_status`).  The modes:

| Mode | VPCommands |
| ---- | ---------- |
| 0 | Code Search |
| 1 | Code Acquire |
| 2 | AGC Set |
| 3 | Freq Acquire |
| 4 | Bit Sync Detect |
| 5 | Message Sync Detect |
| 6 | Satellite Time Available |
| 7 | Ephemeris Acquire |
| 8 | Avail for Position |

`channels_reset` (3704 `0x501fc`, Z3805A `0x4f18c`, Z3801A `0x4f14a`)
sets every mode to 0: in the Z380x when a `@@Ba` comes outside
position-fix mode, in the 58503A from `command_monitor` while the fix
mode is idle, `@@Ba` is not fresh or the 1PPS check is off.

**Position.**  A channel counts only at mode 8.
`channel_position_satellite` (3704 `0x50182`, Z3805A `0x4f118`,
Z3801A `0x4f0d6`) gives a channel's satellite when its mode is 8, its
ID a valid GPS or pseudolite ID, and `@@Ba` received: in the 58503A,
fresh (`GPS_STATUS_FRESH`, not stale); in the Z380x,
`gps.status_received` set, which the status monitor clears only when
it fails.  Else 0.  VPCommands p. 70 lets a satellite be used for
position in modes 5 to 8 once its ephemeris is in; the firmware takes 8
alone.  Its users:

- `satellites_in_use` (3704 `0x50302`, Z3805A `0x4f28e`, Z3801A
  `0x4f24c`): the used sets and counts, so `:GPS:SATellite:TRACking?`
  and `:COUNt?` (`common/scpi_source_gpsystem.c`), the screen's
  tracked list, the first-satellite event (`pll_stages.c`) and the
  58503A front panel's `N TRACKED` (`front_panel_satellites`,
  `0x528be`).
- `satellite_used_for_position` (3704 `0x501ce`, Z3805A `0x4f162`):
  the `@@Ce` corrections' mean (`oncore_corrections_message`) and the
  `*` by a used satellite on the Time RAIM page
  (`gps_print_time_raim`).
- Reference qualification: at least one channel at mode 8, in
  position-fix mode, or the GPS reference is not tracking
  (`gps_receiver_status_update`, Z3805A `0x4b168`;
  `gps_receiver_status_update_58503a`, 3704 `0x4b8a2`).

**The status screen.**  `satellite_sets_build` (3704 `0x498a2`) puts
each tracked satellite in a band by its channel's mode,
`screen_band_add` (3704 `0x4991e` to `0x4994a`, Z3805A `0x494f0` to
`0x4951a`, Z3801A `0x494ae` to `0x494d8`), in each image's
`screen.c`; the mode is compared signed, as an `int8_t`.  The bands
are kept only for satellites tracked and not used for position, and
`satellite_status` (`common/screen.c`) marks those cells by them, in
`format_satellite_cell` (3704 `0x49eb8`):

| Mode | Cell |
| ---- | ---- |
| 0, or negative (128 to 255) | no band: elevation and azimuth, as for a used one |
| 1 to 4 | `Acq   ` |
| 5, 6 | `Acq . ` |
| 7, 8 | `Acq ..` |

A mode of 9 to 127 would show `Acq ..`.  A used satellite, at mode 8,
shows its elevation and azimuth.

**The 58503A's tracking log.**  With `:DIAGnostic:GPSystem:TRACking:LOG`
on, the 58503A logs each track lost or resumed to the event log
(`gps_log_track_change`, `0x4dc5c`) as `Track lost    [SY] %08X %08X`
and the like, the bracket the reference state left, the two longs
packed channel words, from `gps_receiver_status_update_58503a`
(`0x4bb5c` to `0x4bc16`).  The Z380x have no such log.  Three fields
of `gps`, all in `gps_receiver.c`:

- `gps.track_words[3]` (`gps + 0x596`, `0x1013e6`), packed by
  `gps_track_words_pack` (`0x4da70`) at each update that leaves the
  reference valid, assessing or clearing.  10 bits a channel from the
  top of `[0]`, run on across the longs, 80 bits for eight channels:
  - 5 bits of satellite, 32 packed as 10 (below);
  - 3 bits of mode: 0 stays 0, 1 and 2 become 1, 3 to 8 become 2 to 7;
  - 2 bits of signal strength (below).
- `gps.channel_word` (`gps + 0x5a2`), packed by
  `gps_channel_word_pack` (`0x4db52`) when a track is lost: per
  channel from the top, 2 bits of mode (`gps_receiver_mode_bits`: 0
  is 0, 1 to 4 is 1, 5 is 2, 6 to 8 is 3), then 2 of strength.
- `gps.channel_word_lowest` (`gps + 0x5a6`, 16 bits): each channel's
  lowest 2-bit mode since the loss, 2 bits a channel from the top, set
  to `0xffff` at the loss and lowered at each update while the track is
  out (`gps_channel_word_lowest`, `0x4dbe8`).

`Track lost` logs `track_words[0]` and `[1]`, as the last good update
packed them: channels 1 to 6 and the top 4 bits of channel 7's
satellite.  `Track resumed` ORs `channel_word_lowest` into the low half
of `track_words[2]`, just packed, so channels 7 and 8 now (less those
4 bits) over the lowest modes, and logs it with `channel_word`, the
channels at the loss.  Nothing else reads the three fields: a scan of
the ROM for their displacements from `gps` and their absolute
addresses finds only these routines and `0x4bbb2` to `0x4bc0a`, with
the usual caveat for a stepped pointer.  So they reach no SCPI query,
console word or screen but through the event log, where
`:DIAGnostic:LOG:READ?` prints them in hex.

**PRN 32 in 5 bits.**  Five bits hold 0 to 31, and 0 is an empty
channel, so the packer stores PRN 32 as another PRN.  The later images
carry the same tracking log and packer with a different alias:

| Image | Packer's alias load | PRN 32 stored as |
| --- | --- | --- |
| 58503A 3704 | `0x4da9c` | 10 |
| Z3815A 4010 | `0x59204` | 12 |
| 58503B 1.01.04 | `0x53fba` | 12 |
| Z3816A 4001 | `0x51c7c` | 12 |

The 3704's is from its hand-written C; the Z3815A's from its decompiled
packer.  The 58503B's and Z3816A's are from the ROMs alone: each has a
`cmpi.b #32` then `moveq #12` inside code with the same strength cuts
(30, 60, 120), the same mode squeeze and the same word breaks at
channels 4 and 7.  The Z3805A and Z3801A have no tracking log (no
`Track lost` text) and no alias.

Why 10, then 12, is inference from the constellation's history: each
alias is a PRN no satellite broadcast when the code was written.  The
images say "Copyright Hewlett-Packard Co. 1993".  SVN 32 broadcast
PRN 32 from its launch in November 1992 until 1993-01-28, when it was
changed to 01, and no satellite broadcast PRN 10 until SVN 40 in July
1996 ([R. Langley's constellation list, 1995-08-04](https://abnormal.com/~thogard/gps/toadd/gps-sat-stats)).
The later images moved the alias to 12 once PRN 10 was in use.
Today each alias collides: PRN 10 and 32 in the 3704, PRN 12 and 32 in
the others, so a decoder cannot tell them apart.

**Signal strength.**  VPCommands gives it as 0 to 255, "number
proportional to SNR", with no scale (p. 72, and p. 126 for `@@Ea`); it
is meaningless at mode 0 (p. 70).  The firmware prints it raw, without
units: the screen's `SS` column for used satellites (`format_level`,
`common/screen.c`; 3704 `0x49e86`), the console's `CHN SVID MODE SS
STATUS` table (`gps_print_status`, 3704 `0x49472`, Z3805A `0x49054`)
and `abr_stat`'s `gs:` line (`gps_debug_status`, 3704 `0x49636`).  The only arithmetic
is the 58503A's 2-bit bucket for the tracking log,
`gps_receiver_strength_bits` (inline in each packer): 0 up to 30, 1 up
to 60, 2 up to 120, else 3, each cut point double the last.  The C
calls the cut points `WEAK`, `FAIR` and `GOOD`; the names are ours, not
HP's.

### The scale against C/N0

For a healthy VP engine of either model, the B4121 in the 58503A or
the B1121 in the Z3805A:

    C/N0 ≈ 20·log10(SS) + 3 dB-Hz, ±2 dB

The reasoning is inference:

1. SS is the engine's own SNR estimate, taken after its front end.  A
   deaf front end lowers SS for the same sky but does not change what
   an SS value means.
2. The engine drops a channel at a fixed SS, a constant of its
   tracking loop.
3. Motorola's tracking threshold is −143 dBm (Synergy, VP Oncore
   Engineering Notes Archive, SSLLC, 14 JUL 02, §3,
   <https://synergy-gps.com/wp-content/uploads/2018/11/vponcoreengnotesarchive.pdf>),
   about 29 ± 1 dB-Hz against −174 dBm/Hz with an assumed noise figure
   of 1.5 to 2.5 dB.
4. Both bench units stop tracking near SS 20 (58503A 12 to 22, Z3805A
   18 to 20), so K ≈ 29 − 26 ≈ 3.
5. The full scale agrees: SS 255 is about 51 dB-Hz, about the
   strongest signal a good antenna gives.

The two models share the scale: paired, the slope is 1.05 in log-log,
and both stop at about the same SS; a different scale would put the
Z3805A's cutoff near SS 6.  The successor UT Plus Oncore's note (§5,
Testing, <https://synergy-gps.com/wp-content/uploads/2018/11/ut-engg-notes.pdf>)
calls the Position/Status/Data message's signal strength "C/No" and
takes a 2 dB difference between receivers as significant.  Trimble's
AMU has the same form, C/N0 ≈ 20·log10(AMU) + 23.5 to 27
(<https://www.febo.com/pipermail/time-nuts/2010-February/044972.html>).

So a healthy unit on a good antenna reads SS about 20 at its tracking
cutoff and 100 to 200 overhead, about 43 to 49 dB-Hz, and the
firmware's cut points, SS 30, 60 and 120, are about 32.5, 38.6 and
44.6 dB-Hz: 6 dB a doubling.

**The measurement**, the 2026-10-03 run on the bench, not a tool's
output.  The 58503A and a u-blox NEO-M8T shared one antenna through a
splitter for 24 h
([`bench-sky-2026-10-03.html`](https://htmlpreview.github.io/?https://github.com/charlieh0tel/smartclock/blob/main/docs/bench-sky-2026-10-03.html)),
with C/N0 in dB-Hz as the NEO-M8T reports it.  A quantile match, not a
paired fit: the u-blox's per-satellite data was lost, so its per-band
counts and the 58503A's tracked shares give the C/N0 distribution of
the satellites the 58503A tracked, and its logged SS was matched to it
quantile by quantile at the band edges, 30 to 45 dB-Hz, taking SS to
rise monotonically with C/N0.

- The exponent: SS is proportional to amplitude.  Fitted free, n =
  2.09 (amplitude 2, power 1; 1.9 to 2.3 realistic); the Z3805A,
  fitted the same way, 1.98.  A power model (RMS 2.7 dB, residuals
  trending) and one linear in dB (residuals curved) were rejected.
- The bench 58503A: K = 3.5, ±0.5 dB systematic and about ±1 dB per
  satellite-minute, within error of healthy.  It peaked at SS 148 of
  255, so no clipping.
- The bench Z3805A, against the 58503A on the same antenna, same
  minute and satellite, 4105 pairs: about 11 dB (10.5 to 10.9) lower
  despite its 20 dB LNA, K = 14.4, about 11 dB short of healthy.  That
  unit has lost sensitivity.
- The NEO-M8T's own C/N0 calibration and integer rounding add ±1 to
  2 dB to any comparison against it.

## Emulating the engine

What an engine in a Z380x or 58503A must do, from the above:

- **Frame exactly.**  Send each message at its table length with a
  correct checksum.  Send nothing outside the table, and never
  `@@Ao`, `@@Bh` or `@@Ci`.
- **Answer in time.**  Within the timeout, 10 s for most; 20 s for
  `Ai`, `Ca`, `Fa`, `Cg`; 4 s for the 58503A's second `Ea` (3633: 5 s).
  Three unanswered sends fail the request.
- **Echo settings byte for byte**, checksum included: every kind-1
  message, `Az` and `Cf` among them.  Special cases:
  - `Ab`: a negative offset under an hour comes back as sign 0, hours
    0, minutes negated (two's complement).
  - `Af`: the commanded height in the GPS height field for type 0 and
    the MSL field for type 1.
  - `As`: latitude, longitude and type as sent; the height within
    2 cm.
  - `Cg` query (mode 2): any `@@Cg`.
- **Answer queries** (mode 1, the query forms) with the message of the
  same ID, and polls and output requests (kind 7) with one message of
  the same ID: `Aa`, `Ac`, `Ba`, `Bb`, `Bc`, `Bd`, `Bj`, `Bk`, `Bn`,
  `Ca`, `Cj`, `Ea`, `Ek`, `En`, `Fa`.  A request for continuous output
  is answered by the first output.
- **Answer `@@Ao` with a 25-byte `@@Ap`**, `@@Bh` with a 52-byte
  `@@Ce`, and nothing to `@@Ci`.  The Z380x send `Ci`(0) first in their
  start-up scripts; VPCommands p. 121 defines only 1 and 2, so stay in
  binary.
- **Answer `@@Cj`** with the 294-byte identity (pp. 122-123); start-up
  fails without it.  Each line must start with its label.  `OPTIONS
  LIST` must contain `I`, or no Time RAIM is sent.  In the 58503A,
  software 8.4 brings the second `Ar`(65), which must be echoed; any
  other version avoids it.
- **Six or eight channels**, in the 58503A:
  - Six: answer `Ba`, `Bk`, `Bn`, `Ca`; ignore `Ea` (the power-up
    self-test waits about 15 s for it).
  - Eight: answer the second `Ea` (rate 0) with one 76-byte `@@Ea`, or
    send `@@Ea` unasked; from then on, `Ea`, `Ek`, `En` and `Fa`, but
    still `Ba` and `Bn` polls at rate 0 and `Bk` polls, 69 bytes.
  - The Z380x are six-channel only.
- **Keep state.**  The firmware takes fix mode from `@@Cg` and position
  hold from `@@At` as the engine reports them.  Keep `@@Ba`/`@@Ea`
  time advancing, and at least one channel with a satellite in mode 8,
  available for position (p. 72), for the reference to qualify
  ("Channel mode and signal strength").
- **Time RAIM.**  Send `Bn` or `En` at the rate asked, with the on byte
  and the solution and Time RAIM statuses as wanted; the rest is shown
  and not used.

The start-up the engine sees: the init script (Z380x `0x48`, 58503A
`0x53`, in `oncore/IMAGE.txt`), then from `gps_init_sequence` `@@Aw`
(UTC or GPS time), `@@Ag` (`:GPS:EMANgle`), `@@Az`, `@@Am`
(ignored satellites), then a hold or survey script.  The console can
send any message in the table.

## The engines on the bench

`:DIAGnostic:IDENtification:GPSystem?` returns the engine's identity,
a list of quoted `LABEL value` strings.  Read on 2026-09-26:

| | 58503A 3710A01056 | Z3801A 3542A01548 | Z3805A 3625A01487 |
| - | ----------------- | ----------------- | ----------------- |
| Copyright | Motorola 1991-1996 | Motorola 1991-1995 | Motorola 1991-1995 |
| Model | B4121P1115 | B1121P1114 | B1121P1114 |
| Software P/N | 98-P36830P | 98-P39972M | 98-P39972M |
| Version, revision | 8, 8 | 8, 4 | 8, 4 |
| Software date | 06 Aug 1996 | 13 Jul 1995 | 13 Jul 1995 |
| Serial | SSG0220999 | SSG0178541 | SSG0163878 |
| Manufactured | 7D01 | 6J25 | 6G09 |
| Options | IB | IB | IB |

The daemon recorded the Z3805A's on 2026-09-26 when it reattached.
The two Z380x engines are the same model with the same software; the
58503A's is a different model with later software.  From schema 9 the
daemon reads it once per connection, keeps it as answered in the
receiver table of each unit's log, and notes in the journal when it
is first seen or changes.

The Z3801A's engine answered while tracking no satellites on an
antenna the 58503A tracks on.

## Where the sawtooth is read

In the Z3816A, the one reader of offset 26 found is `FUN_0004c062`,
at `0x4c2b4`, which sign-extends it and prints it on a Time RAIM status page whose
format strings are:

    ALARM LIM  %-24s   TIME REF  %s
    SIGMA EST  %-24s   SAWT ERR  %+d ns

`SIGMA EST` is read from record offset 24 and `SAWT ERR` from offset
26, matching the layout above.  The page is reached through a pointer
table; the command that shows it has not been identified.  In the
Z380x and 58503A the page is `gps_print_time_raim` (3704 `0x4888c`,
Z3805A `0x484de`, Z3801A `0x4849c`), the task's step
`GPS_STEP_PRINT_TIME_RAIM` (58503A `0x40`, Z380x `0x39`), which the
console asks for.

## The 58503B's height datum

The `@@Ba` and `@@Ea` descriptors (`0x591e8`, `0x596d4`) unpack into
`0x1012b4` through the byte programs at `0x5a1b0` and `0x5a1ba`
(`04 00 1d 00 ...`): month, day and year, a skipped byte, then 29
message bytes from the hour on.  Against the layout in
`VPCommands.pdf`, the GPS-ellipsoid height `hhhh` lands at `0x1012c8`
and the MSL height `mmmm` at `0x1012cc`.

Bit 4 of `0x1017e8` (`0x608` off `0x1011e0`) chooses between them:

| Clear | Set | Where |
| ----- | --- | ----- |
| averages `0x1012c8` into `0x101762` and copies it to `0x10176a` | averages `0x1012cc` into `0x10176a` | survey, `FUN_00057942` |
| zeroes the last byte, the height type (0 = GPS ellipsoid), of `@@Af`, `@@As` and `@@Au` (commands 5, 18 and 20) | leaves it as encoded | sender, `FUN_000584c0` |
| `%+9.2f m  (GPS)` | `%+9.2f m  (MSL)` | status screen, `0x51160` and `FUN_0005011a` |
| `GPS` | `MSL` | `Position hold started.  [%s Hgt = %+8d cm]`, `0x41bc8` |

The held position comes from the survey average, `0x101762`
(`FUN_0004ba9e`, `0x4bea2`), into `0x1028c0` and `0x1028fc`, which
`:GPS:POSition?` (`FUN_0003cbae`) returns.  Two diagnostic reports
print both heights regardless: `GPS HGT` and `MSL HGT` (`0x4e8fe`),
`HGT (msl)` and `HGT (gps)` (`0x4f276`).

No code found sets the bit.  The only writes to it are clears
(`0x56f5a`, `0x56f86`, `0x56fb6`), one on each engine-type path of
`FUN_00058098`.  A scan of every disassembled store whose operand can
reach `0x1017e8` and a byte search for set, OR and move encodings by
absolute address or displacement from `0x1011e0` found no other, and
cold start zeroes the byte.  So 1.01.04 surveys, holds, reports and
commands the ellipsoid height, as 097-58503-13 (3-17, 4-5) says.  A
write through a computed pointer would escape this search.

## Requests

`oncore/` holds each Oncore image's message table, request scripts and
polling lists, read by `smartclock-cli dump-oncore`, and `oncore/models.md`
sets the messages side by side.  The GPS task reads requests from a
queue -- Z3801A `0x103678`, Z3805A `0x1037fc`, 3633 `0x1039be`, 3704
`0x1039d0`, Z3816A `0x103d62`, 58503B `0x10448a` -- and three places
post to it in each image.

A helper copies four argument longs and posts an index and a mode
(Z3801A `FUN_00046600`, Z3816A `FUN_0004a13c`).  Its calls:

| Z3801A | Z3816A | Index | Names |
| ------ | ------ | ----- | ----- |
| `0x466d4`, `0x47248` | `0x4a210`, `0x4adc0` | `0x45`, `0x59` | a step |
| `0x4677a`, `0x472d8` | `0x4a2b8`, `0x4ae54` | `0x4a`, `0x5e` | the script holding `Ca` (and in the Z3816A `Fa`) |
| `0x46dac` | `0x4a8f4` | `0x26` | `@@Bj`, mode 0 |
| `0x471ae` | `0x4ad26` | from a switch | in the Z3801A, the console's `gps_` words |

In the Z3801A the console words leave a code at `0x102562` and
arguments at `0x102564` -- `gps_php` (`0x1b352`) code 5 and four
longs -- which `FUN_00047074` maps to an index: code 5 to `0x12`,
`@@As`.  In the Z3816A, step `0x59` goes through the step jump table
at `0x512b0` to `0x513dc`, which clears `0x1016ee` and calls
`FUN_00050274`, which points `0x101602` back at `0x1014dc` and clears
`0x1016e7`, and `FUN_0005059e`, which empties the task's ring of
pending requests: six 26-byte messages at `0x1016f0`, filled from the
queue by `FUN_00050710` and emptied by `0x505f6`.  So the `diag`
stage's request discards whatever requests were waiting.

The third poster maps an index read from a request record (Z3801A
`FUN_00042df0`, which reads the record through `0x10200a`; Z3816A
`FUN_00046714`) and copies arguments out of the record.  For `@@As`
and the scripts that set it (Z3801A `0x12`, `0x4f`, `0x56`) it copies
the record's `+0x1e`, `+0x22` and `+0x26` as the first three
arguments and a constant as the fourth, the height type.

**The `@@As` height type.**  `@@As` sets the position-hold position;
its last byte is the height type, 0 for GPS-ellipsoid height and 1 for
MSL (`VPCommands.pdf`, Position-Hold Position).  An encoder sends the
low byte of its argument long (`oncore/README.md`).

| Image | Init script's `@@As` | Third poster's type | Sender |
| ----- | -------------------- | ------------------- | ------ |
| Z3801A 3543 | 37.3256° N, 121.9978° W, 55 m, type 1 (script `0x47`, record `0x50a20`) | 1 (`0x42f60`) | as given |
| Z3805A 3543B | the same (record `0x50a62`) | 1 (`0x42f34`) | as given |
| 58503A 3633 | the same (script `0x52`, record `0x51d92`) | 1 (`0x42f88`) | as given |
| 58503A 3704 | none | 1 (`0x42f90`) | as given |
| Z3816A 4001 | none | 1 (`0x468a0`) | 0 unless bit 4 of `+0x608` in its state block is set (`0x56228`) |
| 58503B 1.01.04 | none | 0 (`0x47e40`) | 0 unless bit 4 of `0x1017e8` is set (`0x585d6`) |

The Z380x and 3633 init scripts also set the initial position with
`@@Ad`, `@@Ae` and `@@Af` to the same place, `@@Af` with height type
1.  So the Z380x and 58503A images send MSL heights, as their manuals give
them, and the 58503B ellipsoid heights, as its manual gives them
(above).  Whether anything sets the Z3816A's bit was not checked.

**On the bench, against a u-blox.**  On 2026-10-10 the 58503A, the
Z3805A and the Z3801A held one position, surveyed by the 58503A and
copied to the others, and a u-blox through gpsd had a 3D fix from an
antenna at the same site.  Horizontally they agreed to 0.3 m.  The
receivers' height was 5.0 m above the u-blox's MSL height and 35 m
above its ellipsoid height: MSL, as above.  The u-blox's antenna is
mounted lower than the one surveyed, so the heights agree no closer
than that.  The Z3801A's decoded
`@@Ba` record (`0x100d36`, read from RAM with `smartclock-cli
read-memory`, through the pForth debug console) held the two
heights of the held position, 28.41 m apart -- the Oncore's geoid
separation there, −28.41 m -- where the u-blox's two heights give
−30.0 m and gpsd's own model −32.3 m.  So an MSL height is the device's
own: the models differ by up to 4 m at this site.  No Z380x query
returns the engine's ellipsoid height: in the Z3801A and Z3805A its
one reader is the survey's averaging (Z3801A `0x4f5e6`), whose
ellipsoid average nothing reads, and `:DIAGnostic:GPSystem:POSition:DEGRees?`
and `:MSEConds?` post requests rather than read the record.  The
separation is read from RAM with `read-memory`, as here; in the 58503A images
a search for readers of the field found none, which leaves them
unsettled.  Every image selects
datum 49, WGS-84, with `@@Ao` in its init scripts, so latitude and
longitude agree by datum.

**`@@Ci`.**  The Z3801A's and Z3805A's init scripts (`0x47`, `0x48`)
open with `@@Ci`, switch I/O format, with an argument long of 0
(Z3801A record `0x50ae8`), so format 0.  `VPCommands.pdf` defines 1,
NMEA, and 2, Loran emulation, and no other.  No other image's scripts
send `@@Ci`.

## In every image

The Z3801A, Z3805A, both 58503As, the Z3816A and the 58503B talk to an
Oncore in its binary `@@` messages; the 58503B names the engine
`Oncore`, as the Z3816A does.  The Z3815A's engine is a Furuno GT-74
(its identification template reads `MODEL # FURUNO GT-74`), on NMEA
sentences: it takes `GGA`, `GSA`, `GSV`, `RMC` and Furuno's own `anc`,
`ssd`, `tst`, `tps`, `rrm` and `rsd` (`0x5e398`), and sends `$PFEC,GP`
commands -- `set`, `srq`, `rrs`, `rrq`, `clr`, `ZDA`, `GLL`, and
`int,` with a sentence name to set its interval (`0x5e64c` to
`0x5e6ca`).  It keeps a `GT-74 Command Log` and a `GT-74 Messages`
screen.  Read on 2026-10-10.

**Time RAIM.**  The 58503B carries the Z3816A's `@@En` program, byte
for byte, at `0x5a1e7`.  The Z3815A, with no `@@` messages, fills a
record of the same layout from its engine's sentences: its Time RAIM
page reads the sawtooth as `move.b (0x1a,A3)` at `0x53df0`, offset 26
as in the others, and prints it on the same `SAWT ERR  %+d ns` line;
it has no `SIGMA EST` line.

**Where the sawtooth goes.**  A search of each image for byte reads at
offset `0x1a` from an address register finds, in the Z3816A, the
58503B and the Z3815A, one read of the decoded record each -- the Time
RAIM page (`0x4c2b4`, `0x4e5da`, `0x53df0`) -- and no other: the rest
read offset `0x1a` of a message-table descriptor (`0x504ae`,
`0x527ec`, `0x575c6`), or, in the Z3815A, the loop's state byte, in the
console's state printer at `0x2c3bc`, whose names include a state the
others lack, `n3 lock pll`.  So in those three, as in the Z3816A, the
sawtooth is displayed and not applied to the interval.  In the
Z3816A a wider sweep -- every byte, word and long read whose
displacement reaches offset 26, through any address register, indexed
or not -- finds no other: nothing points into the record but its two
descriptors (`0x570f0`, `0x573c0`), and the other reads in the GPS
code at those displacements are of other blocks (`0x4cff0`,
`0x4d1d2`, `0x4f0c2`, `0x5191c`).  A reader stepping a pointer, or a
copy of the whole record, would still escape it.

The same sweep over the Z3801A, Z3805A and both 58503A images finds
the same.  Their programs ("Decoding") put the sawtooth one byte earlier, at
record + 25, which the display reaches
as offset `0x1a` from the T-RAIM-present flag just below the record:
Z3801A `0x486ec`, Z3805A `0x4872e`, 3633 `0x489c0`, 3704 `0x48ae6`.
The only other code that points into the record is the Time RAIM alarm
machine (Z3801A `0x4affc`, Z3805A `0x4b03e`, 3633 `0x4b600`, 3704
`0x4b754`), which reads the flag, T-RAIM on or off, the solution
status and the T-RAIM status, never the sawtooth; no other long in
any of the four images points into the record.  So in every image the
sawtooth is displayed and not applied, with the same caveat for
stepped pointers and whole-record copies.

## Not known

- What `@@Ar` 65 does on a software 8.4 engine.
- Whether these engines answer `@@Ao` with `@@Ap`, as the tables
  imply.
- Whether a six-channel engine stays silent on `@@Ea`, and whether an
  eight-channel one answers `@@Bk`.
- Whether the bench 58503A's B4121 ever runs at a count of 8.
  `gps.channel_count`, `0x1013e0`, read with `read-memory`, would say.
