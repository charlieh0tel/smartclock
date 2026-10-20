# The pulse, time-tag and output commands

The 59551A of `097-59551-02` adds to the 58503A a programmable pulse
output, time-tag inputs and an IRIG-B output.  What the firmware does
with the commands for them, read in 58503A 3704 unless another image
is named.

## Present and stubbed in every image

`:SOURce:PULSe`, `:SENSe:DATA` and `:SENSe:TST1` to `TST4`,
`:OUTPut:PIN1` to `PIN8` and `:SOURce:PTIMe:PPS:EDGE` are in all seven
command trees (`../scpi/`), the Z380x's included, so their presence
says nothing about a unit's hardware.

- The programmable pulse stores its settings and drives nothing.  The
  `PERiod` and `CONTinuous:STATe` setters (`0x2da0a`, `0x2da70`) post
  records whose cells are `0x10223e` (period), `0x10223c` (state) and
  `0x10224e` (start); the record's function `0x47faa` gathers them
  and calls `0x47fa8`, a bare `rts`.  The same empty routine is in
  3633 (`0x47e82`), the 58503B (`0x4d126`), the Z3815A (`0x4be5e`), the
  Z3816A (`0x4646a`) and the Z3801A (`0x42b46`).
- `:OUTPut:STATe` likewise, in the Z3805A 3543B: the setting is saved
  in `nv_settings.output_state`, and its descriptor's done hook
  (`0x42b7c`) hands it to `0x42b18`, a bare `rts` beside the pulse's
  (`0x42b1a`).  Nothing else reads the setting but its query.
- `:PULSe:REFerence:EDGE`, `:PTIMe:PPS:EDGE` and `:SENSe:TSTn:EDGE`
  store nothing: their queries share the stub `0x28d70`, and their
  setters call `0x2bcfa(3, value)`, which for some values posts
  message 3 of the table at `0x2bd2a`, `Undefined header`, and
  otherwise returns.
- `:OUTPut:PINn:DELay` and `:FREQuency` are stubs: the setters
  (`0x2bf42`, `0x2bfae`) are `moveq #$e0,d0; rts`, and the queries go
  to the same stub.
- `:SENSe:DATA?` (`0x2eb8a`) reads a buffer through `0x2e84a`; where
  that buffer is filled was not traced.

## What the 58503A drives

- The FPGA at `0x304000` (chip select 5) is the interval counter and
  its interpolator.  Its write routine is `0x40784`, with five byte
  shadows at `0x1022bb` to `0x1022bf`; the field table at `0x413ac`
  has seven fields, none a period or a divider, and every caller is
  the interpolator module (`0x40000` to `0x41000`), the loop's
  interval reads (`0x44fba` to `0x45266`) or a pForth word
  (`0x1b2c4`).
- The latch at `0x300000` (chip select 1, shadow `0x100e0e`): a 3-bit
  field the self-test and health code set (`0x186ac`, `0x23b7e`), and
  bits 6 and 7 the front-panel code sets (`0x53750`).
- Bits 4 to 2 of PORTGP, which 3633's DUART initialization writes
  (`0x1d5cc`, with 0) and no 3704 code does (`revisions.md`).  What
  those pins drive was not traced.
- Nothing sets the rate of the 1 PPS.

So no 58503A image drives a second pulse channel, a pulse period or a
time-tag input.  None has a two-second pulse mode either: no `PP2S`,
`ESEC` or option word selects one, and no setting reaches the
hardware.  The Z3815A alone names a PP2S, as a signal its health
monitor watches (bit 0x104 of the table at `0x5964a`; `PP2S Fail`
from `0x3429e`), not one it makes.

Not traced: what drives the alarm output, and the time-code worker's
output path (`0x13d28`).

## The Z3805A's lamps

097-z3801-01 (1-2, 2-9) gives the Z3801A six front-panel lamps: Power,
Enabled, Active, Alarm, GPS Lock and Holdover.  In the Z3805A 3543B
five are bits of the latch at `0x300000` (chip select 1), which
`leds_update` (`0x464de`) writes whole at the end of each `pllp_task`
pass, about once a second; Power is not the firmware's.

| Bit | Lamp | Lit while | Query |
| --- | ---- | --------- | ----- |
| 0 | Enabled | the byte at `0x101e75` is set | `:LED:ENABled?` |
| 1 | Active | the byte at `0x101e74` is set | `:LED:ACTive?` |
| 2 | Alarm | `status_summary` is not 0 | `:LED:ALARm?`, `:LED:ALARm:MAJor?` |
| 4 | GPS Lock | `pll.led_gps_lock` | `:LED:GPSLock?` |
| 5 | Holdover | `pll.led_holdover` | `:LED:HOLDover?` |

Bits 3, 6 and 7 drive nothing the firmware names.  The self-test's
`lamp_test` (`0x184ee`) writes `0xff`, then 0, then each bit in turn,
`0x133` ticks apiece, and then `leds_update`: the flashing at power-up
the manual describes (1-8).

- *Enabled and Active* are the user's: `:LED:ENABled` and
  `:LED:ACTive` store them through their setting descriptors, and no
  other code writes them.  They live in the block at `0x101e70` that
  `pllp_task` copies to battery-backed RAM (`loop.md`'s table of
  3704's, +4 and +5).
- *GPS Lock* follows the loop's stage (`pll_stage_status`, `0x46524`,
  through the table at `0x4654a`): lit on entering startup PLL (5) or
  normal PLL (6), with log event 6 and status condition `0x1a`;
  cleared, with condition `0x1b`, on entering power-up, power-up
  recovery, holdover, holdover recovery, diagnostic or fatal error.
  Idle leaves it as it is.
- *Holdover* likewise (table `0x465ca`): lit on entering holdover (3)
  or holdover recovery (4), with condition `0x2a` (`0x28` if holdover
  is manual) or `0x2c`; cleared in every other stage but idle.
- *Alarm* is lit while any of `status_summary`'s six bits is, which
  `status_summary_update` (`0x46470`) recomputes each pass from the
  `:STATus:OPERation:HARDware` condition, so it goes out when the
  condition does:

  | Summary bit | From |
  | ----------- | ---- |
  | 1 | hardware bit 0, self-test failure |
  | 2 | hardware bits 1 to 3: the +15 V, −15 V and +5 V supplies |
  | 4 | hardware bit 7, the EFC at full scale |
  | 8 | hardware bits 8 and 9, GPS 1 PPS failure and GPS failure |
  | `0x10` | hardware bit 12, internal reference failure |
  | `0x20` | the holdover duration past `:ROSCillator:HOLDover:DURation:STATus:THReshold` |

  The oven supplies (bits 4 and 5), the EFC near full scale (bit 6)
  and the time-interval and EEPROM events (bits 10 and 11) do not
  light it.  The `es` message carries the summary byte
  (`es_build`).  `:LED:ALARm:USER`, which 097-z3801-01 (4-5) says
  chooses the alarm's conditions, does nothing here: its setter
  and query (`0x2d996`, `0x2d99a`) are `moveq #$e0,d0; rts`, the same stub as
  `:OUTPut:PINn:DELay`'s above, and `:LED:ALARm:MINor?` fails.
- `:LED:TMHValid?` answers `pll.led_tmh_valid`, which no code writes:
  always 0.

The last summary bit's flag (`0x101e88`) is set by `holdover` and
`holdover_recovery` once `pll.holdover_duration` passes the threshold,
with condition `0x2e` (`:STATus:OPERation:HOLDover` bit 3, "Exceeding
User-Threshold"), and cleared on regaining lock.  With it,
`pwma_force_level` forces the PWMA pin low, and releases it on lock,
as `loop.md` records for 3704; what the pin drives was not traced.
`:ROSCillator:HOLDover:DURation:STATus:THReshold:EXCeeded?` returns
the flag.

The hardware register's bits are set and cleared by conditions `0x34`
to `0x4b` of `status_conditions`, a pair for each bit (bits 10 and 11
by one event each), in the order 097-59551-02 (5-38) gives the
58503A's.  The health monitor's messages confirm three: `6`/`7` with
"15V is out of tolerance" and "now within tolerance" on bit 1, `8`/`9`
with "-15V" on bit 2, and `>` with "Secondary oven voltage is out of
tolerance" on bit 5, which the 58503A's list leaves out.

## The FPGA bitstreams

Every image carries two 3860-byte blocks, each starting `ff 20 07 86`,
the header and length count of a Xilinx XC3000-family bitstream.  In
the Z3805A, the loader `0x22da4` takes an index and reads that entry
of the table at `0x21e2c`.  Each entry is a start and an end address
(`0x22e94` to `0x22ea0`).  The table holds one entry, `0x20f18` to
`0x21e2c`, which is the first bitstream.  The boot code at `0x22d70`
loads index 0 and keeps it at `0x102533`; the only other caller,
`0x18938`, reloads that stored index.  The loader drives PROGRAM with
PORTE bit 6 and reads INIT and DONE on `0xfff90c` bit 3.  The second
block starts right after the table, at `0x21e36`.  No table entry and
no code address points to it.

Copies of the five designs are in `third_party/firmware/fpga/`.
`tools/fpga/iobdump.js` prints a bitstream's I/O blocks, using the
decoder of the XC3000 viewer at <https://github.com/Arisotura/xc3000>.

| Image | First bitstream | sha1 | Second block |
| ----- | ------------ | ---- | ------------- |
| 58503A 3633 | `0x1ff84` | `e8789ce4` | `0x20ea2`, `c57a94d1` |
| 58503A 3704 | `0x1fd16` | `e8789ce4` | `0x20c34`, `c57a94d1` |
| Z3801A 3543 | `0x1fddc` | `e8789ce4` | `0x20cfa`, `c57a94d1` |
| Z3805A 3543B | `0x20f18` | `955494aa` | `0x21e36`, `c57a94d1` |
| 58503B 1.01.04 | `0x2f5a8` | `3110273c` | `0x304c6`, `c57a94d1` |
| Z3815A 4010 | `0x316a6` | `9fe2370e` | `0x325c4`, `c57a94d1` |
| Z3816A 4001 | `0x2ffb8` | `8061d2bb` | `0x30ed6`, `c57a94d1` |

The second block is the same in all seven.  The first comes in five
designs:
- The 58503A and the Z3801A share one, byte for byte.
- The Z3805A's differs from it in 1868 of 3860 bytes, the 58503B's in
  2347, the Z3816A's in 3315 and the Z3815A's in 3392.
- The Z3815A's and the Z3816A's differ from each other in 1686.

Which pins or registers a difference touches cannot be read from the
bytes.

The CPU code around the FPGA is the same in the Z3805A and the Z3801A:
- the writes to the FPGA at `0x304000`;
- the latch at `0x300000` (`0x184f8` to `0x1851e`, `0x4651c`);
- the GPT setup (`0x12050` to `0x120b2`), apart from CFORC.

The Z3805A alone has a second output-compare timer, OC4 (`0x1406a` to
`0x1412c`).  It drives no pin: TCTL1 is cleared and PGP6 is an input.
No image has a command, option or model check that enables a pulse
output.

Decoded with that viewer (at commit 8563397) and its PLCC84 pinout,
the 58503A's and the Z3805A's designs set the same I/O blocks on every
pin but two.  "Registered" means the output passes through the I/O
block's flip-flop, "unregistered" that it does not; either way the pin
is the FPGA's, not a jack's, and a jack is reached through the board's
drivers.

| Pin | 58503A and Z3801A | Z3805A |
| --- | ----------------- | ------ |
| P14 | output, unregistered | output, registered, fast slew |
| P45 | unused, pull-up only | output, registered, fast slew |

The viewer also reports a bad clock-line multiplexer setting
("111111") on the Z3805A design's bottom-left and left-upper clock
lines.  It is a work in progress, and its pinout has not been checked
against Xilinx's data book.  None of this has been checked on a board.

The Z3805A drives its J4 and J5, which are cabled to its two rear
1 PPS outputs.  The 58503A's J4 is fitted, and showed no 1 PPS on the
bench (`../hardware-investigations.md`, item 19).
