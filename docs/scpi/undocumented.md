# Undocumented commands

What is known of the commands no manual here has: what their handlers
do in the firmware, and what a receiver answered.

`models.md` says which paths each image's tree holds and which a
manual lists, and lists, generated from the images, the undocumented
paths whose node has the handlers of a documented one and the stubs,
whose every handler only refuses.  This file is written by hand; a test
checks that every path heading here names a path some image's tree
holds.

Each command below gives:

- **Images:** the firmware images whose tree holds the path.
- **Handler:** what its handler does, where it has been read, at its
  address in `58503a-3704.bin` unless another image is named.
- **Read:** what a receiver answered: model, firmware, date, reply.

The receivers are the bench 58503A (3704-C), Z3805A (3543B-A) and
Z3801A (3543-A).  Readings from the sweeps (see the end) carry the
date they were written up.

A read can still change state: a status group read with no leaf, or
anything ending `:EVENt?`, clears the event register it returns
(`z3801a.md`).

## Other names for documented commands

Each of these runs, query and setter, the handlers of a documented
branch (`models.md`, "Same handlers as a documented path"), in every
image that has it.

| Word | Same handlers as |
| ---- | ---------------- |
| `DOUGlas` | `:GPSystem:POSition:SURVey` |
| `GARY` | `:DIAGnostic:LOG` |
| `KENneth` | `:SYNChronization:HOLDover:TUNCertainty` |
| `MATThew` | `:ALARm` |
| `ROBin` | `:SENSe:DATA` |

### `:DOUGlas`

- **Images:** all nine.
- **Handler:** the position survey's; `:PROGress?` raises -221,
  "Settings conflict", when no survey is running.
- **Read:** on 2026-10-09, the same from all three receivers:

  | Query | Reply |
  | ----- | ----- |
  | `:DOUGlas:DURation?` | `+7.20000E+003` |
  | `:DOUGlas:STATe?` | `0` |
  | `:DOUGlas:PROGress?` | -221 |

### `:GARY`

- **Images:** all nine.  In the 58503B, Z3805A 1.01.03, Z3815A and Z3817A images `GARY` keeps
  only its own query, the handler of `:DIAGnostic:LOG:READ:ALL`.
- **Handler:** the diagnostic log's.
- **Read:** `:GARY:COUNt?` on 2026-10-09:

  | Receiver | Reply |
  | -------- | ----- |
  | 58503A 3704-C | `+24` |
  | Z3805A 3543B-A | `+64` |
  | Z3801A 3543-A | `+41` |

### `:KENneth`

- **Images:** all nine.
- **Handler:** the holdover time uncertainty's.  `:PREDicted?` is "an
  estimate of the time interval error that can be expected for a one
  day holdover" and `:PRESent?` "the current time interval error
  during holdover" (097-59551-02,
  `:SYNChronization:HOLDover:TUNCertainty`).  Both read a state byte
  (`0x102476`): `:PRESent?` answers only in states 3 and 4, and
  `:PREDicted?` in 3 to 6, its second field 1 in 3 and 4.  In the
  Z3805A 3543B the byte is the loop's stage, `pll` at `0x102162`,
  whose 3 to 6 are holdover, holdover recovery, startup PLL and normal
  PLL (`../firmware/loop.md`): `kenneth_present_get` (`0x4299c`)
  answers with `pll.tfom` and `kenneth_predicted_get` (`0x429c6`) with
  the float at `pll` `+0x3a`, both to a resolution of 10^-7.
- **Read:** on 2026-10-09; the Z3805A's two replies were not read
  together:

  | Receiver | `:PREDicted?` | `:PRESent?` |
  | -------- | ------------- | ----------- |
  | 58503A 3704-C, locked | `+1.61E-005,0` | -221 |
  | Z3805A 3543B-A | `+3.2E-006,1` | -221 |
  | Z3801A 3543-A, 464 s into holdover | `+1.01E-005,1` | `+1.0E-006` |

### `:ROBin`

- **Images:** all nine.
- **Handler:** the measurement arrays', `:SENSe:DATA`.
- **Read:** `:ROBin:POINts?` answered `+4,+5,+6` from all three
  receivers on 2026-10-09.

## `:DIAGnostic`

### `:DIAGnostic:GPSystem:ACURrent`

- **Images:** 58503A 3633 and 3704, 58503B, Z3805A 1.01.03, Z3815A, Z3816A, Z3817A; a
  stub in the Z3817A (`models.md`, "Stubs").
- **Handler:** returns a word from a table of eight channels
  (`0x222a6`): the value read where one has been, the image's default
  where not.  Its unit is not established.
- **`:ALARm`:** in the 58503A images its flag (3633 `0x10225b`, 3704
  `0x102261`) is read by the query handler and the settings table, and
  in 3704 by an antenna current check nothing calls (`0x222f2`), so
  setting it changes no monitoring (`../firmware/revisions.md`).
- **Read:** 58503A 3704-C, 2026-10-09: `+2.08431E+002`.

### `:DIAGnostic:GPSystem:TIME`

- **Images:** all nine.
- **Read:** 58503A, written up 2026-09-20: `+21,+41,+53,+2007,+2,+4`,
  time then date.

### `:DIAGnostic:GPSystem:TRACking:LOG`

- **Images:** 58503A 3633 and 3704, 58503B, Z3805A 1.01.03, Z3815A, Z3816A, Z3817A.
- **Handler:** returns a flag.  On, the 58503A 3704 logs each track
  lost or resumed with packed channel words
  (`../firmware/gps.md`, "Channel mode and signal strength").
- **Read:** 58503A 3704-C, 2026-10-09: `0`.

### `:DIAGnostic:GPSystem:UTC`

- **Images:** all nine.
- **Read:** 58503A, written up 2026-09-20: `1`.

### `:DIAGnostic:IDENtification:DEFault`

- **Images:** all nine.
- **Read:** 58503A, written up 2026-09-20: `"58503A","CQ"`.

### `:DIAGnostic:IDENtification:GPSystem`

- **Images:** all nine; 097-z3801-01 lists it for the Z3801A.
- **Read:** 58503A, written up 2026-09-20: the GPS engine, also as
  `:GPS?`.  It names a Motorola with `SOFTWARE DATE 06 Aug 1996`,
  before the 1024-week rollovers of 1999 and 2019, and so the source
  of the receiver's 1024-week date error (`firmware/gps.md`, "The
  engines on the bench").
- **Read:** Z3817A 3752-A, 2026-11-11, with nothing on either
  reference's RXD: `-230`, data corrupt or stale; no engine has sent
  its identity (`../firmware/z3817a-3752.md`, "On the bench").

### `:DIAGnostic:INPut:DATA`

- **Images:** 58503A 3633 and 3704, 58503B, Z3805A 1.01.03, Z3815A, Z3816A, Z3817A.
- **Handler:** returns zero whatever the state.
- **Read:** 58503A 3704-C, 2026-10-09: `+0`.

### `:DIAGnostic:OS`

- **Images:** all nine.
- **Read:** Z3805A, written up 2026-09-22.  `:DIAGnostic:OS:MEMory?`,
  `:PROCess?` and `:STACk?` dump the state of the real-time system,
  pSOS, and name its tasks:

      scpi   the command parser        pllp   the disciplining loop
      gpsm   GPS manager               curv   curve fitting
      hmon   health monitor            klok   clock
      spoo   spooler                   ROOT, IDLE

  with message exchanges `1pps`, `pllc`, `plla`, `gpsx`, `logr`,
  `eepx`, `sciR`/`sciW` and `drtR`/`drtW`, and about 13 percent of the
  CPU idle.  `pllp` runs the disciplining loop and `curv` the aging fit
  (`firmware/loop.md`, "The disciplining loop").

### `:DIAGnostic:PTIMe:TINTerval`

- **Images:** all nine.
- **Read:** Z3805A, written up 2026-09-22: the time interval, the
  unlocked one-second reading (`firmware/interval.md`).
- **Read:** Z3817A 3752-A, 2026-11-11, five times: `-5.00E-008` each,
  on the grid of 50 ns plus a multiple of 100 ns its count gives
  (`../firmware/z3817a-3752.md`, "One reading", "On the bench").

### `:DIAGnostic:REFerence:CPORt`

- **Images:** Z3817A, 58503B, Z3805A 1.01.03; a setter only, a stub in
  the 58503B and the Z3805A 1.01.03 (`models.md`, "Stubs").
- **Handler:** in `z3817a-3752.bin` (`0x3cad4`), posts the loop's
  action `0x4d6ca`: in holdover, or in power-up with no good reference,
  it asks for a switch to the other time reference, TIMEREF1 or
  TIMEREF2, at the loop's next pass; otherwise it raises -221,
  "Settings conflict" (`../firmware/z3817a-3752.md`,
  "`:DIAGnostic:REFerence` and `:DIAGnostic:TCODe`").
- **Read:** not sent.

### `:DIAGnostic:REFerence:STATus`

- **Images:** Z3817A, 58503B, Z3805A 1.01.03; a query in the Z3817A, a
  stub setter in the 58503B and the Z3805A 1.01.03 (`models.md`,
  "Stubs").
- **Handler:** in `z3817a-3752.bin` (`0x3cafa`), five items, `+r,+m,e,g,p`:
  r the selected time reference, 1 TIMEREF1 or 2 TIMEREF2; m the
  reference pulse's mode, 1 1PPS or 2 PP2S; e 1 while the external
  (VALID) reference record is valid; g 1 while the GPS (Oncore)
  reference record is valid; p 1 while the reference's pulse is alive.
  A questionable record answers 0.  Codes and flags, no units
  (`../firmware/z3817a-3752.md`, "`:DIAGnostic:REFerence` and
  `:DIAGnostic:TCODe`").
- **Read:** Z3817A 3752-A, 2026-11-11, on TIMEREF2 with VALID and a
  1 PPS and no stream: `+2,+1,1,0,1`.  Z3805A 1.01.03-A, a
  correspondent's unit, 2026-11-12: the query -113, "Undefined header".

### `:DIAGnostic:REFerence:GPSystem:QUEStionable:HYSTeresis`

- **Images:** 58503A 3633 and 3704, 58503B, Z3805A 1.01.03, Z3815A, Z3816A, Z3817A.
- **Handler:** returns a stored value, converted for the reply.  Its
  sibling under `:EXTernal` is a stub ("Stubs a receiver refused").
- **Read:** 58503A 3704-C, 2026-10-09: `+60`.

### `:DIAGnostic:ROSCillator:CURRent`

- **Images:** all nine; a stub in the Z3817A (`models.md`, "Stubs"),
  which has no oven-current channel
  (`../firmware/z3817a-3752.md`, "The health monitor's channels").
- **Read:** 58503A, written up 2026-09-20: `+1.05882E+002`, oven
  current.
- **Of note:** the Z3801A image's strings include `Double oven`.  The
  58503A images carry it too (`firmware/console.md`, "The 58503A
  image"); the bench 58503A's oscillator is a 10811-60159 (`ocxo.md`).

### `:DIAGnostic:ROSCillator:EFControl`

- **Images:** all nine; 097-z3801-01 lists the bare node for the
  Z3801A.
- **Read:** the bare node answers as `:RELative?`: on the 58503A,
  written up 2026-09-20, and on the Z3805A, written up 2026-09-22.

### `:DIAGnostic:ROSCillator:EFControl:ABSolute`

- **Images:** all nine.
- **Read:** 58503A, written up 2026-09-20: `+713392`, DAC code.  See
  `efc.md` for what the EFC commands mean in volts and in frequency.

### `:DIAGnostic:ROSCillator:EFControl:DATA`

- **Images:** 58503A 3704, 58503B, Z3805A 1.01.03, Z3815A, Z3817A.
- **Read:** 58503A, written up 2026-09-20: `+0`.

### `:DIAGnostic:ROSCillator:EFControl:MODE`

- **Images:** 58503A 3704, 58503B, Z3805A 1.01.03, Z3815A, Z3817A; new in 3704.
- **Handler:** maps a mode byte (`0x10217a`) to one of three values.
  In the Z3817A (`0x3be1a`) the byte is `0x102394`, answered `NORM`,
  `TIM` or `USER`; a unit failure sets it to user
  (`../firmware/z3817a-3752.md`, "Failing the unit").
- **Read:** 58503A 3704-C, 2026-10-09: `NORM`.  Z3817A 3752-A,
  2026-11-11: `NORM`.

### `:DIAGnostic:ROSCillator:LTIMe`

- **Images:** 58503B, Z3805A 1.01.03, Z3817A.  In the Z3817A `:DATA?` (`0x3bf12`)
  returns `pll` + `0x3c8` (`0x102a62`), the cell its start-up ramp
  steps (`../firmware/z3817a-3752.md`, "The loop's constants"); its
  other handlers are not read.
- **Handler:** the loop's time constants, in `58503b-1.01.04.bin`
  (`firmware/loop.md`, "In every image").  `:INIT` is the constant the
  start-up ramp begins from (`0x1028cc`, 10 to 1000 s, default 150 s)
  and `:MAX` the one the loop settles at (`0x1028d0`, 10 to 10000 s,
  default 700 s); each setter keeps `:INIT` at or below `:MAX`.
  `:DATA?` returns the constant in force (`0x10328a`).
- **Read:** no 58503B on the bench.

### `:DIAGnostic:ROSCillator:TCOefficient`

- **Images:** 58503A 3633 and 3704, Z3801A, Z3805A 3543B, Z3815A, Z3816A.
- **Read:** 58503A, written up 2026-09-20: `-3.36500E+001`, the
  oven-current constant (`firmware/loop.md`).

### `:DIAGnostic:ROSCillator:TYPE`

- **Images:** 58503B, Z3805A 1.01.03, Z3817A; the Z3817A's handlers (`0x3c402`,
  `0x3c38a`) are not read.
- **Handler:** the oscillator type, the byte at `0x102726` in
  `58503b-1.01.04.bin`, which selects the loop's gain G from a table
  (`firmware/loop.md`, "In every image").  The setter accepts 0 or 1;
  type 0 has the Z3816A's G, type 1 the 58503A's.
- **Read:** no 58503B on the bench.

### `:DIAGnostic:SER1:RESTricted`

- **Images:** all nine, with `SER`, `SER2`, `SERIAL`, `SERIAL1` and
  `SERIAL2` beside `SER1`.
- **Read:** Z3805A, written up 2026-09-22: a restricted mode, on both
  ports.

### `:DIAGnostic:SLOG`

- **Images:** all nine.
- **Read:** 58503A, written up 2026-09-20: `:DIAGnostic:SLOG?` returns
  the oldest log entry, against `:LOG?` for the newest.  Z3805A,
  written up 2026-09-22: `:DIAGnostic:SLOG:COUNt?` answers, so the
  second log is real.

### `:DIAGnostic:SYSTem:PSTartup`

- **Images:** all nine, with `:DIAGnostic:SYSTem:DOUTput` beside it.
- **Handler:** a toggle (`firmware/restart.md`, "Restarting"): the
  setter posts its structure and the query returns its cell (Z3816A
  `0x3b372` and `0x3b3a2`, cell `0x102617`; Z3801A `0x2aaa0` and
  `0x2aad0`, cell `0x101f4b`).  It is one of the settings saved to the
  EEPROM when set.  At boot the root task tests the cell (Z3816A
  `0x232da`, Z3801A `0x131ca`) and, when it is set, starts the pForth
  console instead of the SCPI task, on every power cycle until it is
  cleared.  The start-up clear leaves it 0 unless a saved record holds
  1.  The way back is the console's `halt` (`firmware/console.md`,
  "Leaving it"), which hands the port to a new SCPI task, and then
  `:DIAGnostic:SYSTem:PSTartup 0`; `clear_nv` also clears it, with
  every other saved setting.
- **Read:** Z3805A, written up 2026-09-22: both answer.  Z3801A
  3543-A, 2026-10-10: set to 1, it read back 1; after a power cycle
  the port answered `p4th D > `; `halt` gave `scpi > `, and
  `:DIAGnostic:SYSTem:PSTartup 0` read back 0; after another power
  cycle the port answered `scpi > `.

### `:DIAGnostic:TCODe`

- **Images:** 58503A 3704, 58503B, Z3805A 1.01.03, Z3815A, Z3817A; new in 3704.
- **Handler:** four bytes, `ERRor:AMASk` and `:OMASk` and `STATus:AMASk`
  and `:OMASk`: each setter stores its argument's low byte and each
  query returns it, and `timecode_init` sets the AND masks to 255 and
  clears the OR masks.  Only the Z3815A uses them: its `es_build`
  (`0x24050`) puts the ES message's status byte through the STATus
  masks and its error byte through the ERRor masks, AND then OR
  (`0x2409c` to `0x240bc`).  In the 58503A 3704 (`0x103a06` to
  `0x103a09`), the 58503B (`0x1044c0` to `0x1044c3`) and the Z3817A
  (`0x103f00` to `0x103f03`) no code but `timecode_init` and the handlers holds their addresses.
- **Read:** 58503A 3704-C, 2026-10-09:

  | Query | Reply |
  | ----- | ----- |
  | `:DIAGnostic:TCODe:ERRor:AMASk?` | `+255` |
  | `:DIAGnostic:TCODe:ERRor:OMASk?` | `+0` |
  | `:DIAGnostic:TCODe:STATus:AMASk?` | `+255` |
  | `:DIAGnostic:TCODe:STATus:OMASk?` | `+0` |

### `:DIAGnostic:TEMPerature`

- **Images:** all nine; a stub in the 58503B, Z3815A, Z3816A and Z3817A
  (`models.md`, "Stubs").
- **Handler:** `firmware/console.md`, "`:DIAGnostic:TEMPerature?`".
- **Read:** 58503A, written up 2026-09-20: `+3.68550E+001`, degrees
  Celsius.

### `:DIAGnostic:TMODe`

- **Images:** 58503A 3633 and 3704, 58503B, Z3805A 1.01.03, Z3815A, Z3816A, Z3817A.
- **Handler:** `:DATA?` and `:STATe?` each return a byte.
- **Read:** 58503A 3704-C, 2026-10-09: `:DATA?` `+0`, `:STATe?` `0`.

### `:DIAGnostic:TOFFset`

- **Images:** all nine.
- **Read:** 58503A, written up 2026-09-20: `+0.00000E+000`.  Z3817A
  3752-A, 2026-11-11: `0`, its EEPROM image's 0.

## Elsewhere in the tree

### `:LED:ACTive`

- **Images:** all nine; 097-z3801-01 lists it for the Z3801A.
- **Read:** Z3805A, written up 2026-09-22: answers, as do
  `:LED:ALARm:MAJor?`, `:LED:ENABled?` and `:LED:TMHValid?`.

### `:LED:TMHValid`

- **Images:** Z3801A, Z3805A 3543B.
- **Read:** with `:LED:ACTive` above.

### `:SENSe:DATA:POINts`

- **Images:** all nine; 097-59551-02 and 097-58503-13 list it.
- **Read:** Z3805A, written up 2026-09-22: three measurement arrays.
  Its other name, `:ROBin:POINts?`, is above.

### `:SOURce:PTIMe:UTC`

- **Images:** 58503A 3704, 58503B, Z3805A 1.01.03, Z3815A, Z3817A; new in 3704.
- **Handler:** returns a flag (`0x102264`).
- **Read:** 58503A 3704-C, 2026-10-09: `:PTIMe:UTC?` `1`.

### `:SYSTem:PRINt`

- **Images:** all nine; the handlers of `:SYSTem:STATus`.
- **Read:** Z3805A, written up 2026-09-22: the whole status screen
  again.

## Only in the Z3815A, Z3817A, 58503B and Z3805A 1.01.03

Most of these are in the Z3817A's tree too, as each entry says.  No
Z3815A or 58503B is on the bench and none has been sent to the
Z3817A, so these are read from the images alone, and the Z3817A's
handlers are not read here.  Addresses are in `58503b-1.01.04.bin`
unless the Z3815A's are named; most handlers get or set a cell through a parameter record of
the kind `:DIAGnostic:ROSCillator:TCOefficient` uses -- a cell,
limits, a getter and a setter -- and the limits given are the record's.

### `:DIAGnostic:ROSCillator:EFControl:ASLOPe`

- **Images:** 58503B, Z3805A 1.01.03, Z3815A, Z3817A.
- **Handler:** the loop's gain G (`../firmware/loop.md`, "In every
  image").  In the 58503B its record (`0x43eb8`) names the cell the
  oscillator type loads G into, `0x10271e`, with limits held in RAM
  (`0x1026ee`, `0x1026f2`); in the Z3815A its record (`0x48374`) names
  `0x1023c8`, where bit 12 of the word at `0x302000` puts G, with
  limits ±10⁻¹¹.  So both models let G be read and set.

### `:DIAGnostic:ROSCillator:EFControl:BSLOPe`

- **Images:** 58503B, Z3805A 1.01.03, Z3815A, Z3817A.
- **Handler:** a second coefficient B beside G: the 58503B's record
  (`0x43eec`) names `0x102722`, which choosing the oscillator type
  clears and the EFC calibration clears too -- it fits only a straight
  line, whose slope becomes G; its limits too are held in RAM, and the
  setter only stores and saves.  The loop never applies it; the measured holdover
  uncertainty does, taking the oscillator's gain as A + B·e² at EFC e,
  which is A unless this command has set B (`../firmware/loop.md`, "In
  every image").

### `:DIAGnostic:ROSCillator:EFControl:DADC`

- **Images:** 58503B, Z3805A 1.01.03, Z3815A, Z3817A.
- **Handler:** a DAC-to-ADC ratio with limits ±20 (`0x43e84`, cell
  `0x10271a`): the EFC calibration measures it (`low= %d high= %d
  dac/adc= %e`), and choosing the oscillator type sets it to 16.0.

### `:DIAGnostic:ROSCillator:TCO`

- **Images:** 58503B, Z3805A 1.01.03, Z3817A, with `TCO1`, `TCO2` and `TCOEFFICIENT` beside
  it.
- **Handler:** the 58503B's `TCOefficient`: its record (`0x43e48`)
  names `0x10270a`, limits ±200 as in every image, and both handlers
  pass the keyword's numeric suffix as an index, a coefficient for each
  of the two oscillator types.

### `:DIAGnostic:ROSCillator:PTESt`

- **Images:** 58503B, Z3805A 1.01.03, Z3815A, Z3817A.
- **Handler:** settings of a test: `:PTHR` (and `PTHR1`, `PTHR2`,
  `PTHRESHOLD...`), limits 10⁻⁹ to 10⁻³ (`0x44076`, cell `0x1028ae`);
  `:TTHR` and its kin, 0 to 240 (`0x4402a`, cell `0x1028a6`); `:COUNt`,
  an integer at `0x1028a2`; `:MODE`, a byte at `0x10289f`; `:TYPE`, a
  byte at `0x1028a0`, answered as 0 when it is 1 and 1 otherwise.
  `:STATe?` and `:DURation?` were not decompiled.  What the test does
  was not traced.

### `:DIAGnostic:SYSTem:PDEBug`

- **Images:** 58503B, Z3805A 1.01.03, Z3815A, Z3817A.
- **Handler:** a setter only: sets the byte at `0x1032b7` to whether
  its argument is non-zero.  `pll_normal` (`0x498ee`) and the
  fine-acquisition machine (`0x4a356`) test that byte; what each test
  guards was not traced.

### `:DIAGnostic:TCODe:ASCii`

- **Images:** 58503B, Z3805A 1.01.03.
- **Handler:** a flag at `0x1044be`, set to whether the argument is
  non-zero and read back by the query.

### `:DIAGnostic:LOG:WRAP`

- **Images:** 58503B, Z3805A 1.01.03, Z3815A, Z3817A.
- **Handler:** a byte at `0x102974`, set through a parameter record
  (`0x44660`) and read back by the query.

### `:DIAGnostic:ADC`

- **Images:** 58503B, Z3805A 1.01.03, Z3815A, Z3817A.
- **Handler:** answers 0 after running `0x3ab96`, passing it the
  console's print routine; what that prints was not traced.

### `:DIAGnostic:TEST:FLASh`

- **Images:** 58503B, Z3805A 1.01.03, Z3815A, Z3817A.
- **Handler:** prints, as `%d`, what `0x3b944` computes, and answers 0.

### `:DIAGnostic:CALibration:ROSCillator`

- **Images:** 58503B, Z3805A 1.01.03, Z3815A, Z3817A.
- **Handler:** a setter only, which passes the action list at
  `0x45ae4`, as `:SYSTem:PRESet`'s handler passes its own
  (`../firmware/restart.md`); what the actions do was not traced.

### `:DIAGnostic:CALibration:ROSCillator:GPS`

- **Images:** Z3805A 1.01.03 alone, with `:REFerence` beside it, a
  stub (`models.md`, "Stubs").  Its tree is the 58503B's with these two
  paths added.
- **Handler:** not read (`../firmware/z3805a-1.01.03.md`).
- **Read:** not sent.

### `:DIAGnostic:GPSystem:ACURrent:STATe`

- **Images:** 58503B, Z3805A 1.01.03, Z3815A, Z3817A.
- **Handler:** maps a state from `0x31aa6` to 0, 1, 2 or 3.

### `:DIAGnostic:GPSystem:DCXO`

- **Images:** 58503B; the Z3805A 1.01.03 has the node, its setter
  never passing its test and its query always -241.
- **Handler:** only while bit 1 of the byte at `0x1017e8` is set: the
  query returns three values (`0x101746`, `0x101748`, `0x10174a`) once
  any is non-zero, and the setter stores three for the port
  (`0x1037b4` on) and hands them to the parameter record at
  `0x446cc`, which clamps them to 0 to 32767, 215 and 65535 and sends
  them as `@@Ta`.
- **Parameters:** a VP engine's drift reference in Hz at L1, the
  setting of its DCXO and Hz at L1 per step of the setting; the engine
  takes none of them unless all three are in its range, the third
  below 32768 ([`../firmware/gps.md`](../firmware/gps.md), "The fields").

### `:SOURce:PTIMe:TDATe:GPS`

- **Images:** 58503B, Z3805A 1.01.03, Z3815A, Z3817A, with `:DEFault` and `:UTC` beside it.
- **Handler:** one handler for all three: the date and time, converted
  to GPS time for one argument and to UTC for another (`0x3dddc`).

### `:SOURce:GPSystem:SATellite:TRACking:DATA`

- **Images:** 58503B, Z3805A 1.01.03; `:CHOE?` runs the same handler.
- **Handler:** the tracking data, as a string built by `0x57648`.

### `:KENneth:MEASured`

- **Images:** 58503B, Z3805A 1.01.03, Z3815A, Z3817A; the handler of
  `:SYNChronization:HOLDover:TUNCertainty:MEASured`, which the older
  images lack.
- **Handler:** returns, through `0x4781c`, the time error the aging
  fit of 24 hours before would have accumulated since, which each new
  fit recomputes (`../firmware/loop.md`, "In every image"); it refuses
  until that has been computed once.

## Stubs a receiver refused

| Query | Receivers | Reply |
| ----- | --------- | ----- |
| `:OUTPut:PIN1:DELay?` | 58503A 3704-C, Z3805A 3543B-A, Z3801A 3543-A | -113 |
| `:OUTPut:PIN1:FREQuency?` | 58503A 3704-C, Z3805A 3543B-A, Z3801A 3543-A | -113 |
| `:DIAGnostic:REFerence:EXTernal:QUEStionable:HYSTeresis?` | 58503A 3704-C | -113 |

All read on 2026-10-09.  -113 is "Undefined header", the refusal a
stub's handler makes (`models.md`, "Stubs").

## Sweeps

Candidate paths built from the keyword table and sent to a receiver
show which it implements: an unknown header returns -113 and changes
nothing.

- **58503A, written up 2026-09-20:** 1,530 candidates over the
  `:DIAGnostic` subtree.  What answered is under its own heading above.
- **Z3805A, written up 2026-09-22:** 282 tree paths put as queries;
  124 answered or declined and 158 came back undefined (`z3801a.md`).
  118 answered, 88 of them `:SYSTem:COMMunicate`: the serial settings,
  which are per port and per direction.
