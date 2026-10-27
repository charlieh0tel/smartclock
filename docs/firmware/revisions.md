# 58503A 3633 against 3704

What changed between the two 58503A images,
`third_party/firmware/images/58503a-3633.bin` and `58503a-3704.bin`.  The
bench 58503A runs 3704-C.  Addresses are given as 3633 → 3704.
[58503A 3704 against Z3805A 3543B](58503a-3704.md) sets 3704 against
the image whose every function is named, function by function.

## Method

Every function in each image was reduced to its instructions with
addresses and branch targets replaced by placeholders, then matched
across the images by that hash, by the strings it references, and by
its place among matched neighbors.  Of about 1,375 functions in each,
1,293 matched, 1,187 of them identical.  Of the 106 that differ,
most differ only by relocation: a moved string block, a struct field
2 bytes further on (the loop block field `loop.md` notes), or a
Ghidra function body that runs into data.  What follows is what is
left.  "Not traced" marks what was seen but not followed.

## Fixed in 3704

- **Date from a day count** (`0x10e72` → `0x10e98`).  On the last day
  of a year, where the remainder within the year is 0, 3633 walks the
  month table and produces day 0 of January; 3704 returns 31 December
  of the year before.
- **Late periodic timers** (`0x10b62`, the same address in both).  For
  a periodic entry whose time has already passed, 3633 advances it by
  its period without running its action; 3704 sets its time to now and
  runs the action.
- **Settings record with a bad checksum** (the 16-byte record at
  EEPROM `0x400000`; loader `0x309ca` → `0x30d12`).  Both restore the
  ROM defaults (`0x30698` → `0x3092a`); 3704 also writes them back to
  the EEPROM (`0x30c6c`).

## Changed

- **Health monitor.**  3633 checks each channel in its own code (a
  switch at `0x22dec` on the channel byte `0x102004`); 3704 serves one
  channel per call, round robin over eight, every 10 passes of the
  health task (`0x2220a`, index `0x101e6f`, task `0x23d16`), from
  42-byte descriptors at `0x2238a`.  Limits, events, the 10-sample
  mean and the trip count, more than 5 samples in a row, are the same:
  5V 4.5 to 5.5 (events 0x3e/0x3f), 15V 14 to 16 (0x3a/0x3b), −15V
  −16 to −14 (0x3c/0x3d).  3633's message per rail becomes one format
  with the channel's name (`0x21f68`).  The channel table is in
  `console.md`.
  - The oven alarm, events 0x40/0x41 in both, is named `Primary oven
    voltage` in 3633 and `Primary oven current` in 3704.  The test is
    the same, a reading below −2.0 while the stage byte (`0x10246e` →
    `0x102476`) is 3 to 6, on channel 5 in 3633 (`0x231f8`) and
    channel 3, `Oven`, in 3704 (`0x22362`).
  - 3704 holds three routines nothing calls: a 0.1/0.9 smoothing with
    a delta every 75 calls (`0x21e3e`), an antenna current check, 10
    to 200 on channel 7 while `0x102261` is set (`0x222f2`), and a
    range check taking its count as an argument (`0x220ae`).
  - The power-on supply check prints the descriptor's name after
    `POWER: ` where 3633 had fixed strings (`0x17ece` → `0x18046`), and
    the pForth word `pr_adc_avg` prints each channel whose flag is set
    where 3633 printed seven fixed ones (`0x1b0ec` → `0x1b150`).  The
    word `cal` reads channel 6 instead of 3 (`0x1bf0c` → `0x1befc`).
- **Reference state.**  Each 1 PPS reference has a 22-byte record,
  the GPS one at `0x101394` and the external one at `0x1013aa`, kept by
  the parser `0x4b8a2` (3633 `0x4b77e`): +0 a valid code, +1 a state, +2
  a forced value, +3 a GPS 1 PPS fault qualifier.  The state picks the
  status line (picker `0x4a0d4` → `0x4a1fc`):

  | 3633 | 3704 | Status line |
  | ---- | ---- | ----------- |
  | 0 | 0 | `Invalid: GPS rcvr fail` (3704: `failed`) |
  | 1 | 1 | `Invalid: not tracking` |
  | 2 | 2 | `Invalid: inacc position` |
  | -- | 3 | `Invalid: No UTC data` |
  | 3 | 4 | `Invalid: Time RAIM error` |
  | 4 | 5 | `Assessing stability` |
  | 5, 6 | 6, 7 | `Questionable accuracy` |
  | 7 | 8 | valid |

  3704 never stores state 3: the branch that would choose 3 or 4 tests
  a local set to 1 just before it (`0x4b936`) and written nowhere
  else, so it always chooses 4.  `No UTC data` is never shown, nor the
  track-resumed reason `[NU]`, which names state 3; the other reasons
  3704 adds name the state the reference left, `[ER]` 0 and `[TR]` 4
  (table at `0x4dcc2`).
  - What decides the states in 3704 (link watchdog `0x4bce6`; message
    layouts from `VPCommands.pdf`):
    - State 0, `GPS rcvr failed`, is a lost link, not a receiver
      fault: `0x1013de` is set when more than 36 output intervals pass
      without an `@@Ba` or `@@Ea`, or two counters (`0x1016ac`,
      `0x1013dc`) pass their limits (`0x4bf06`), and cleared when one
      arrives (`0x4bf88`).  The loss also posts script 0x56, which turns
      `@@Ba`, `@@Ea` and `@@Bb` back on and sends `@@Cg` 1.
    - State 1, `not tracking`: no channel of the latest `@@Ba` or
      `@@Ea` (pp. 125--126) is in tracking mode 8, available for
      position, with a satellite ID in range (`0x50182`).  Channels
      are searched only while an `@@Ba`/`@@Ea` has arrived within 3
      intervals (`0x100f22` = 2), `@@Cg` reports position fix mode
      (`0x101256` = 1), and `0x1013c3` is clear; the scripts that
      reconfigure the engine (0x52 to 0x55, 0x5a) set it for their
      duration.
    - State 2, `inacc position`: neither position hold (`@@At` = 1,
      `0x100f06`) nor `0x1013c0`, which `0x50610` sets on an
      `@@Ba`/`@@Ea` with a 3D fix (bit 5 of the receiver status) while
      the survey runs, the same routine that averages the survey's
      position.
    - State 4, `Time RAIM error`: the T-RAIM machine `0x4b734` has
      timed an `@@Bn`/`@@En` solution status ALARM (pp. 140--141) for
      4 or more units of `0x13a04`, with T-RAIM on.  The parser's other
      test, `0x1013c1 == 0`, never passes: `0x1013c1` is written only at
      start-up, to 1 (`0x4c926`).
  - The valid code +0 is 1 in state 8 and 2 in states 6 and 7
    (`0x4bc2a`), unless +2 is set, which the pForth words
    `force_gps_1pps` and `force_ext_1pps` write: −1 forces invalid, 1
    valid, 0 restores the state's own code.  3704's
    `:GPS:REFerence:VALid?` (`0x2d0a0`) answers true in state 8 without
    a 1 PPS fault and in states 6 and 7; 3633's (`0x2ce6c`) answers
    `0x100f68 != 0 && 0x101393 == 0`.  The loop's flag `0x102872` is
    set while +0 is 2 (`0x44f48`), where 3633's `0x102862` is set while
    `0x100f69` is 5 (`0x44ef2`).
  - The health task's event 0x4a, posted after 0x200 passes of a valid
    reference, also requires `0x102000 == 0` in 3704 (`0x2392c` →
    `0x23b88`), a byte only ever cleared.
- **Holdover cause.**  The cause byte `0x10247f` becomes a word,
  `0x102486`, in its writers (`0x4491e` → `0x4495a`, `0x45460` →
  `0x454d4`, `0x47492` → `0x475ac`) and in the pForth word
  `pr_hold_cause`.  3704 adds `0x4396e`, which writes cause bits to the
  event log through `0x3189a` (bit 0 as code 7, bits 1 to 7 as 0x0a to
  0x14), and `0x45c46`, which in EFC mode 2 clears bit 8 and the mode
  after 600 passes (`screen-format-strings.md` has bit 8).
- **Measurement errors.**  3704 adds `0x10238d`, set when a
  measurement read is replaced by a wait while a 1 PPS fault flag is
  set and cleared when a read starts with both clear (`0x450e0`,
  `0x45272`).  While it is set, `holdover`, `startup_pll` and
  `pll_normal` post no `Error with measurement` message (`0x43c10`,
  `0x444ac`, `0x44bd4`) and `pll_normal` does not choose cause bit 2
  over bit 3; 3633 gates these on the fault flags alone.  `pll_normal`
  also suppresses the empty-window cause bits when a read fails while
  `0x102872` is set, where 3633 does so only after a good read.
- **Oncore.**
  - The third request poster (`0x42dfc`, the same address in both)
    accepts record code 0x2e, `@@Cf` (set to defaults), and treats it
    as it treats 0x38; in 3633 the code falls through to error 0xe.
  - The `@@Bb` decoder (`0x50534` → `0x50716`) clamps the visible
    satellite count at `0x100f6e` to 12.
  - The GPS initialization step machine (`0x468ea` → `0x46a00`, state
    `0x102494` → `0x10249e`) has the same request and wait pairs, with
    states 2 and 3 skipped and the later states 2 higher.
  - 3633 picks its six- or eight-channel messages with code that
    special-cases entries 0x1e/0x32 and 0x29/0x34 (`0x509b6`); 3704's
    selection is in `gps.md`, "Six or eight channels".
- **Serial and DUART.**
  - On a receive error, 3633's SCPI input reader (`0x7da8` →
    `0x7ca6`, in the installer) tests status bits 0xc0, 0x10, 0x20 and
    0x01 and reports 3, 4, 2 or 1; 3704 makes one call with the status
    byte.
  - 3633's DUART initialization (`0x1d9ac`) writes bits 4:2 of PORTGP
    (`0xfff907`) through `0x1d5cc`, and its installer writes PORTGP
    too (`0x37d7`); neither does in 3704.
  - The DUART loopback self-test (`0x1e2f2` → `0x1e10e`) spins up to
    10000 passes with no delay in 3633 and waits up to 1000 passes,
    calling `0x170d0(1)` on each, in 3704.
- **EEPROM.**  When writing the 64-byte record at `0x400080` fails,
  3633 posts code 0x10 through `0x2bac6` (`0x308f4`); 3704 returns
  without posting (`0x30b24`).
- **Installer.**  Rebuilt: every non-empty record differs.  Its
  `psos_indep.h` header goes from 1.4 (94/07/08) to 1.5 (95/03/24),
  the revision both primaries carry, and 3633's `dev_tab.S,v 1.8`
  header is not in 3704.  The second size argument of its task creation
  call goes from 0x300 to 0x400 (`0x8178` → `0x822e`).

## Removed in 3704

- The front-panel key handler (`0x5346a` → `0x5362e`) in 3633, given
  key code 0x17 while the byte at its third argument is 3, scrolls
  `MATT SEMERSKY` across the display: 24 strings at `0x53f09` to
  `0x54034`, written through `0x48008` with `0x134e4(100)` between.
  3704 has neither the strings nor the branch.  Which keys produce
  code 0x17 was not traced.

## Not traced

The two link counters and the units of `0x13a04`; the time-code worker (`0x13bcc` → `0x13d28`), which grows
from 546 to 726 bytes and whose Ghidra bodies run into data; and the
routines 3704's installer adds (`0x1e70`, `0x1ebc`, `0x2096`, `0x29e6`,
`0x2cb6`, `0x2da6`).  3704's `0x10285c`, set to 9 at loop start and by
`0x45eb4`, has no reader and `0x45eb4` no caller.
