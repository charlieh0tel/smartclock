# 58503A 3633 against 3704

What changed between the two 58503A images,
`third_party/firmware/58503a-3633.bin` and `58503a-3704.bin`.  The
bench 58503A runs 3704-C.  Addresses are given as 3633 → 3704.

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
- **Reference valid.**  3633's `:GPS:REFerence:VALid?` (`0x2ce6c`)
  answers `0x100f68 != 0 && 0x101393 == 0`; 3704's (`0x2d0a0`) answers
  `(0x101394 == 1 && 0x101397 == 0) || 0x101394 == 2`, and the loop
  tests the same byte against 2 (`0x44f54`).  What the value 2 stands
  for was not traced.  The health task's event 0x4a, posted after 0x200
  passes of a valid reference, also requires `0x102000 == 0` in 3704
  (`0x2392c` → `0x23b88`).
- **Holdover cause.**  The cause byte `0x10247f` becomes a word,
  `0x102486`, in its writers (`0x4491e` → `0x4495a`, `0x45460` →
  `0x454d4`, `0x47492` → `0x475ac`) and in the pForth word
  `pr_hold_cause`.  3704 adds `0x4396e`, which writes cause bits to the
  event log through `0x3189a` (bit 0 as code 7, bits 1 to 7 as 0x0a to
  0x14), and `0x45c46`, which in EFC mode 2 clears bit 8 and the mode
  after 600 passes (`screen-format-strings.md` has bit 8).
- **Status screen text.**  `Invalid: GPS rcvr fail` becomes `Invalid:
  GPS rcvr failed`, and 3704 adds `Invalid: No UTC data` (its picker,
  `0x4a0fe` → `0x4a266`, has nine cases where 3633's has eight).  The
  track-resumed reasons gain `[NU]`, `[TR]` and `[ER]`.  What selects
  `No UTC data` or `[NU]` was not traced.
- **Oncore.**
  - The third request poster (`0x42dfc`, the same address in both)
    accepts record code 0x2e, `@@Cf` (set to defaults), and treats it
    as it treats 0x38; in 3633 the code falls through to error 0xe.
  - The `@@Bb` decoder (`0x50534` → `0x50716`) clamps the visible
    satellite count at `0x100f6e` to 12.
  - The GPS initialization step machine (`0x468ea` → `0x46a00`, state
    `0x102494` → `0x10249e`) has the same request and wait pairs, with
    states 2 and 3 skipped and the later states 2 higher.
  - 3633's mode bytes `0x100f68`/`0x100f69` are gone; the channel
    status parser (`0x4b77e` → `0x4b8a2`) moves its tracking-mode
    compares from 4, 5, 6 to 5, 6, 7 and from +2 to +1 of the entry.
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

The flags 3704 adds to the loop, `0x10237e`, `0x10238c`, `0x10238d`,
`0x102870` and `0x10285c`; the reordered Oncore state bytes; the
time-code worker (`0x13bcc` → `0x13d28`), which grows from 546 to 726
bytes and whose Ghidra bodies run into data; and the routines 3704's
installer adds (`0x1e70`, `0x1ebc`, `0x2096`, `0x29e6`, `0x2cb6`,
`0x2da6`).
