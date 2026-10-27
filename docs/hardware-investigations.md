# Hardware investigations

Questions a bench can settle and the firmware images cannot.  Each
item names the open question in `docs/firmware/README.md` it would close, what
to measure, and what the answer changes.  Receiver access needs
authorization under `AGENTS.md`; `smartclock-cli flash` has a specific
exception for firmware installation.  Items that need the console are
marked as wanting a spare unit.

Stop the daemon before any direct-mode work; it holds the port.

*Photos* paragraphs record what the bench's board photos settle, read
on 2026-10-23: the photo under `images/`, and the region as pixels
x0,y0 to x1,y1 of the full-size photo after its EXIF rotation.  The
Z3805A's parts are mapped in
[`z3805a-board.html`](https://htmlpreview.github.io/?https://github.com/charlieh0tel/smartclock/blob/main/docs/z3805a-board.html).
Only the component sides were photographed, and most signal lines
leave a part's pins through vias within a few millimeters, so no line
between two parts was followed end to end.

## Working on the open Z3805A board

The procedures under items 13, 20, 21 and 23 are written for the bench
Z3805A (Z3805-68001, `z3805a-board.html`) with the power off, with an
ohmmeter.  The photos they call for are in `bench.md`, "Photos still
wanted".  These rules hold for all of them.

*Before measuring.*

1. Stop the daemon, unplug the DC input at the rear panel, and wait a
   minute.  Read +5 V (U33 pin 63 or U28 pin 8, below) and the ±15 V
   rails to ground on the DC volts range; go on only when each reads
   under 0.1 V.
2. Wear a wrist strap to the chassis.  The oscillator and its outer
   oven stay hot for a while after power-off; keep hands off them.
3. Leave S1 alone: changing a position is item 13's own test.
4. The board can stay in the chassis.  Where a cable or bracket hides
   a pin (J12 is partly behind one), note it rather than remove parts,
   and ask before unplugging the GPS engine or the front ribbon.

*Measuring.*

- Resistance range, lowest scale; null the leads.  Read each pair
  both ways round.  A trace reads under 1 Ω.  Anything from a few ohms
  to a few hundred kilohms is a part in series or a path through other
  parts, often a CMOS input's protection diodes, which read differently
  with the leads swapped: record the value and both polarities.  A
  continuity beeper alone cannot tell a trace from a 10 Ω resistor.
- U33's leads are fine pitch.  Probe the toe of the lead where it
  meets its pad, or a via on the same line, with a sharpened tip; never
  slide along the row.  Count from a corner, then confirm the count by
  a ground pin near it (below).
- Ground is any VSS pin of U33, U3's GND, or the J4/J5 shells.  Check
  the chosen point against two others before using it.

*Pins this file uses.*  The MC68331 in the 132-pin QFP, from
MC68331UM Figure 3-2 (page 3-4; again as Figure B-1).  Pin 1 is at
the middle of the top edge, marked by the dot; the numbers run
counterclockwise seen from above: 1 down to 17 leftward along the top,
18 to 50 down the left side, 51 to 83 left to right along the bottom,
84 to 116 up the right side, 117 to 132 along the top to pin 1.

| U33 pin | Signal | Used by |
| ------- | ------ | ------- |
| 51, 59, 67, 83 | VSS | the bottom row's count check: the 1st, 9th, 17th and 33rd pins from its left corner |
| 29, 34, 40 | VSS | the left side's count check |
| 63, 65, 84 | VDD | +5 V |
| 54 | IPIPE/DSO | item 13, J12 |
| 55 | IFETCH/DSI | item 13, J12 |
| 56 | BKPT/DSCLK | item 13, J12 |
| 58 | FREEZE/QUOT | item 13, J12 |
| 68 | RESET | items 13, 20 |
| 69 | HALT | item 13 |
| 70 | BERR | item 13, J12 |
| 85 | PE5/DS | item 13, J12 |
| 71 to 77 | PF7/IRQ7 down to PF1/IRQ1; pin 76 is PF2/IRQ2 | item 21 |
| 79 | R/W | item 20 |
| 89 | PE0/DSACK0 | item 21 |
| 91, 92, 93, 94, 97, 98, 99, 100 | DATA15, 14, 13, 12, 11, 10, 9, 8 | item 20; an 8-bit port is on DATA15 to DATA8 (MC68331UM 4.4.2) |
| 35, 36, 37 | ADDR13, ADDR14, ADDR15 | item 20 |
| 120 | PC2/FC2/CS5 | item 20 |
| 123 | PC5/ADDR21/CS8 | item 21 |
| 125 | ADDR23/CS10 | item 21 |

The other parts' pinouts are from their data sheets in
`third_party/`; check each on the board by its ground pin before use:

| Part | Pins used | Source |
| ---- | --------- | ------ |
| U3, SCC68692 PLCC-44 | GND 22, /INTR 24, /CS 39, /IACK 41, VCC 44, /DTACK 10; no-connects 1, 12, 23 and 34 | `philips/SCC68692.pdf`, Figure 1 (page 3); the 40-pin DIP there, as in MC68681.pdf 6.1, is GND 20, /INTR 21, /CS 35, /IACK 37, VCC 40, /DTACK 9.  On the board, GND 22 reads 0 Ω to ground and VCC 44 to +5 V. |
| U28, TL7705A SO-8 | /RESIN 2, GND 4, /RESET 5, RESET 6 (active high), VCC 8 | `ti/tl7705a.pdf`, section 5 (page 3) |
| U44 (Z3801A), 74ABT574A SO-20 | /OE 1, D0 to D7 2 to 9, GND 10, CP 11, Q7 to Q0 12 to 19, VCC 20 | `philips/74ABT574A.pdf`, Table 2 |
| U22, U23, U25, U27, 74AC138 | A0 to A2 1 to 3, /E1 4, /E2 5, E3 6, /Y7 7, GND 8, /Y6 to /Y0 9 to 15, VCC 16 | `fairchild/74AC138.pdf`, connection diagram (page 1; it names the outputs /O0 to /O7) |
| U29, DM74ALS09 | inputs 1, 2, 4, 5, 9, 10, 12, 13; open-collector outputs 3, 6, 8, 11; GND 7, VCC 14 | `fairchild/DM74ALS09.pdf`, connection diagram (page 1) |
| U37, MC144110DW SOG-20 | Din 1, ENB 8, VSS 9, CLK 12, Dout 19, VDD 20; no-connects 10 and 11 | `motorola/MC144110.pdf`, Figure 2 (the DW package) |

PLCC pins are numbered from pin 1 at the middle of the edge with the
chamfered corner nearest it, counterclockwise seen from above.

*Recording.*  Under the item, a dated paragraph in the form the
others use (*Measured on the bench Z3805A, 3625A01487, date*), the
meter, and a table of every pair read, including the ones that read
open: a reading of "open" is a result.  Tick the item's TODO and
update `z3805a-board.html`'s notes where a part's identification
changes.

## 1. The word that chooses G

*Open item:* what drives bit 8 of the read-only port at `0x302000`
(the Z3816A's chip select 7, CSBAR7 `0x3020` at `0x24706`; in the
Z3801A and Z3805A the address is in chip select 5's window instead,
`firmware/restart.md`, "The chip selects"), which picks
−1.25 × 10⁻¹² or −2.125 × 10⁻¹² per EFC unit on the Z3816A.

- [ ] TODO: needs a Z3816A, which the bench lacks: trace CS7 to the
  device it strobes and what feeds its D8.

- Follow the MC68331's CS7 pin (CSPAR1 field 1; pin table in
  MC68331UM appendix D) to the device it strobes -- a buffer, a latch,
  or a switch pack.  Note what feeds its D8 line.
- If it is a link or switch, record its position and the unit's
  oscillator option.  If it is a signal, trace it to the oscillator or
  DAC board.
- Cross-check against the actual EFC sensitivity (item 2).

*Changes:* the G row of the loop table loses "by a hardware bit", and
the two Z3816A gains get names.

## 2. The actual EFC sensitivity and its sign

*Open item:* G is the sensitivity the firmware *assumes*, negative on
the Z3816A and positive on the Z3801A; neither is measured.

- [x] Z3801A, 2026-10-10 (below).
- [ ] TODO: measure G on the Z3805A; the Z3816A needs a unit the bench
  lacks.

- With the receiver in holdover or with the loop open (a `phase_off`
  setpoint is console-only; do not use it -- instead compare EFC
  against a frequency counter over a long locked run), log
  `:DIAGnostic:ROSCillator:EFControl:RELative?` and the 10 MHz
  frequency against an external reference.
- Fit frequency against EFC.  The slope in fractional frequency per
  percent, scaled by the EFC word per percent, is the real G.

*Measured on the bench 58503A, 3704-C, 2026-09-26* (`efc.md`, "The pull,
measured"): **+3.94 × 10⁻¹³ per count**, count up raising the frequency,
from a retrim that left the crystal on 10 MHz at 0 V and the receiver
holding count 0, 4.568 V, with the output 2.840 × 10⁻⁷ low.  The
58503A image, revision 3633, assumes G = +6.25 × 10⁻¹³: the sign
agrees and the real gain is 0.63 of the assumed.  Still open for the
Z3816A, whose image picks one of two negative values and of which the
bench has no unit, the Z3801A and the Z3805A.

*Measured on the bench Z3801A, 3543-A, 2026-10-10*: **+6.0 × 10⁻¹³
per count**, count up raising the frequency.  Method: with the
receiver in manual holdover (`:ROSCillator:HOLDover:INITiate`), whose
EFC is the float at `0x1024fe` (`0x35a` off `0x1021a4`) plus an aging
term (`0x102502`, then 0) and the oscillator-current term
(`firmware/loop.md`), that float was set from the pForth debug console
with Forth's `!` to its value (866261.625), +16384.0, its value,
−16384.0 and its value again.
`:DIAGnostic:ROSCillator:EFControl:ABSolute?` followed to the count
each time.  The 10 MHz output, on an Agilent 53230A with a 1 s gate
subtracting 10 MHz, averaged over 100 to several hundred readings
(standard deviation 2.5 to 3.7 mHz):

| EFC | Mean |
|---|---|
| 866261 | +0.130 mHz |
| 882645 | +99.880 mHz |
| 866261 | −0.176 mHz |
| 849877 | −97.061 mHz |
| 866261 | +0.447 mHz |

Taking each step against the mean of the readings either side of it,
+16384 moved the output +99.90 mHz (6.10 × 10⁻¹³ per count) and
−16384 moved it −97.20 mHz (5.93 × 10⁻¹³); the span, 197.10 mHz over
32768 counts, is 6.02 × 10⁻¹³ per count.  The three readings at the
setpoint lie within 0.62 mHz, so the two steps differ by more than
the readings' scatter.  The Z3801A image assumes +6.25 × 10⁻¹³
(`firmware/loop.md`, "In every image"): the sign agrees and the real
gain is 0.96 of it.  A first try the same day at ±4096, five or six
readings per point read by eye, gave 5.7 × 10⁻¹³.  The pForth word
`efc_wr`, which writes the EFC cell itself, did not hold: the
holdover recomputes the cell from the float each pass.

*Changes:* whether the loop's poles sit where the derivation puts
them (−1/(2τ)) or are scaled by the ratio of real to assumed G.

## 3. Which connector the SCI reaches

*Open item:* the SCPI port is the processor's own SCI in the Z3816A
image; that it is the RS-422 port on J3 is inferred, not traced.

- [ ] TODO: with a board open, trace the SCI's and the DUART channel B's
  pins to their connectors.

- Follow the SCI's TXD/RXD pins (PQS7/PQS6 on the MC68331) to the
  level converter and connector.
- On the Z3801A, follow the 68681 DUART's channel B (TXB/RXB) the
  same way, since that image drives its host port there.

*Photos:* on the board the serial connector is J1, silkscreened RS232
and RS422 (`z3805a-cpu-fpga-flash.jpg`, 150,2850 to 1050,3072); J3 is
the rear panel's designator for it (`z3801a-rear-panel.jpg`, "Port 1
J3").  Beside J1 sit U2, a Linear Technology LT1180ACS RS-232 driver
and receiver, two of each (`z3805a-interior.jpg`, 450,900 to
1000,1250), and the footprints U5, U6 and U7, bare on the Z3805A.  The
58503A fits U7 alone (`58503a-interior.jpg`, 1120,3650 to 1300,3830,
an SO-8, marking not legible); the Z3801A, whose port the manual makes
RS-422, fits an AT&T part at U5 or U6 (`z3801a-main-board.jpg`,
1500,2550 to 2150,3072), which suggests the RS-422 driver goes there.
The lines from U33's PQS6 and PQS7 leave through vias; not followed.
Still needs the bench.

*Changes:* closes "That the SCI is the port wired to J3".

## 4. Whether DUART channel B reaches a header

*Open item:* the Z3816A firmware leaves channel B idle apart from a
loopback self-test; a 59551A would need a second port for PORT 2.

- [ ] TODO: trace TXB/RXB from the DUART to any header.

- Follow TXB/RXB from the DUART.  Note any unpopulated header or
  spare connector they reach; no firmware talks on it, so this is
  documentation only.

*Photos:* the DUART, U3, is a Philips SCC68692C1A44, the 68681's
44-pin PLCC successor, read across `z3805a-main-board.jpg` (380,2150
to 680,2560), `z3805a-board-right.jpg` (2680,950 to 3072,1350) and
`z3805a-cpu-fpga-flash.jpg` (1950,2800 to 2550,3072); the 58503A's is
the same part.  The unfitted footprints near it on the Z3805A, J2 and
the one at J3's place on the 58503A, both boxed headers on the 58503A
(`58503a-interior.jpg`), are the candidates; TXB and RXB leave U3
through vias and were not followed.  Still needs the bench.

The SCC68692's data sheet (`third_party/philips/SCC68692.pdf`) gives
TxDB as PLCC pin 13 and RxDB as pin 11.  It is the MC68681's register
map and commands, with additions the firmware does not use: CR
commands `0x8` to `0xf` (RTS, the receiver timeout mode and, on
channel A, power down), where the MC68681 has bit 7 reserved; and
reads of registers 2 and 10, "do not access" on the MC68681, which
here toggle the baud-rate and 1X/16X test modes (Table 1, Table 6).
The firmware issues only commands `0x0` to `0x7` and only writes the
command registers (`firmware/include/mc68681.h`), so the MC68681's
description is the board's as far as the firmware goes.

## 5. The outer oven's enable and readback

*Open items:* that PORTGP bit 5 (PGP5) reaches P2/8, that the ADC is
an ADC0838, and how the Z3801A's "Oven" and "Secondary oven voltage"
channels map to volts at P2/9 are owners' reports, not traced here.

- [x] The ADC, from photos, 2026-10-23: U35 is a National ADC0838CCV
  on the Z3805A (`z3805a-cpu-fpga-flash.jpg`, 700,500 to 1700,1000)
  and on the 58503A (`58503a-interior.jpg`, 50,2100 to 800,2900).
  Public data sheet: National ADC0831/2/4/8.  Beside it, U36 is a TI
  TLC0820AC, a second 8-bit converter with a parallel output, on all
  three boards.
- [ ] TODO: trace PGP5 to P2/8 and calibrate the P2/9 channel against
  a meter.  P2 is not in the Z3805A photos, which are out of focus
  over the oscillator side; PGP5 leaves U33 through a via.

- Trace PGP5 (MC68331 pin, see the manual's pinout) to P2/8 and
  confirm the level changes when the firmware's state passes
  `external oven warmup` (the `Oven Pwr` field on the status screen).
- Identify the ADC by its markings and confirm which of its inputs
  P2/9 feeds.
- Record the ADC count for P2/9 against a meter at two or three
  heater voltages, to give the "Secondary oven voltage" channel its
  unit.  The firmware's alarm limit is 6.8 in that unit.

*Changes:* the ovens section's owner-report caveats become
measurements; the health-monitor table gets a unit for the oven
channels.  The 58503A image (revision 3633) has two oven channels
with a default reading of 4.0 and a message naming one `Primary oven
voltage`, so the unit to establish is volts at P2/9 against that
reading.

## 6. The oscillator-current channel's unit

*Open item:* channel 6 (`Oscillator current`) has a nominal 250 and a
limit 650 after 4.489 per ADC count, unit unknown.

- [ ] TODO: measure across the oven supply's sense resistor against
  channel 6.

- Find the sense resistor in the oven supply; measure the voltage
  across it and the supply current at warm-up and at steady state, and
  compare with the channel's value from `:DIAGnostic:...` or the status
  screen.

*Changes:* the c·s term of the loop gets a physical scale; the
`Oscillator current` health channel gets a unit.

## 7. The warm-restart path

*Open item:* the firmware keeps its loop state across a reset when
the reset-status register shows neither EXT nor POW and the RAM
checksum holds.  Read, not exercised.

- [ ] TODO: on a spare unit, apply a brief external reset and watch
  whether tau and the aging fit survive.

- Both software restarts in the image discard that state before
  resetting (`docs/firmware/restart.md`, "Restarting"): `:SYSTem:PON` zeroes
  the region and `:SYSTem:PRESet` clears its flag.  Neither exercises
  the warm path, and this project sends neither.  `*TST?` does not
  reset the processor: it resets the GPS engine and sends the loop
  back through `powerup` with the loop's RAM intact, so it does not
  test this path either.  On a spare unit it is the one way to watch
  the receiver re-acquire from the daemon's log without touching the
  power.
- A brief external reset, if the board has a reset input, shows
  whether EXT alone (without POW) counts as warm.  No command triggers
  a RESET-instruction restart with the flag intact.
- Watch whether τ (loop time constant) and the aging fit survive: the
  EFC should not jump and the drift term should not go to zero.

*Changes:* confirms which reset sources keep the loop's state, and
whether `*TST?` is one of them.

## 8. The one-second reading versus the ten-second mean -- done

Measured 25 September 2026 on the bench 58503A: one hour of
`:DIAGnostic:PTIMe:TINTerval?` beside `:SYNChronization:TINTerval?`,
one pass a second through the daemon with raw commands enabled.  The
ten-second value is the mean of the ten one-second readings to
0.15 ns rms; MDEV of the means equals MDEV of the readings to 1 to
3 % at every tau; ADEV of the means is √10 low at every tau to 500 s.
The numbers are in `PLAN.md`, "Allan deviation is computed over
segments".  Plain `:PTIMe:TINTerval?` is the ten-second mean, by the
image and by 187 polls.

## 9. The console, on a spare unit only -- done

The console has been used on the bench units to read their ROM and
EEPROM, and `halt` returns the port to SCPI (item 14).

## 10. A 58503A image -- done

`third_party/firmware/images/58503a-3633.bin`, assembled from willhb's flash dumps
(`NOTICE`), answered the first questions (`firmware/console.md`, "The 58503A
image"): the loop has the c·s term, `TCOefficient` is that c and its
setter writes it, the oscillator current is channel 3 with the same
exponential average, and `EFControl:ABSolute?` reports the DAC word
with the term in it.

The bench receiver's own flash, revision 3704-C, was read through its
pForth console (`firmware/console.md`, "Reading memory through it"): its loop
has the same term, on the same current renumbered as channel 6.

*Done:* the bench receiver applies the term.  On 2026-10-10, locked,
setting `:DIAGnostic:ROSCillator:TCOefficient` to 0 moved
`EFControl:ABSolute?` by +3726 at the next update, 3728 predicted from
the coefficient and the oven-current average read from its RAM, and
restoring −33.65 moved it back (`efc.md`, "The regression").  The
record shows the term too, over 5 to 15 minutes against a rebuilt
monitor average (−29 ± 3 counts per unit at 300 s, −35 ± 5 at 900 s);
the earlier tests looked at 10 s, where it is under the noise.

## 11. The two Z380x units that track weakly or not at all

*Open item:* why, as of 2026-09-27, the Z3801A (3542A01548) had
tracked no satellite since it came to the bench, and the Z3805A
(3625A01487) none since 2026-09-23, when it held six.  Both have the
same engine, a Motorola B1121P1114 with software 8.4
(`firmware/gps.md`, "The engines on the bench").  The Z3805A has since
tracked weakly: lock about 98% of nine hours on 2 to 4 satellites
(2026-09-28, below), 2.9 satellites on average in the 2026-10-03
comparison (below), and lock 88.3% of the 24 hours to 21:00 UTC on
2026-10-05, on about 2.7 satellites on average while locked.

The Z3801A's own log dates it: it cycled between GPS lock and
holdover from 2016-08-05 to its last lock on 2016-09-20, and every
power-on since is stamped 2016-09-24, the date its engine still holds.
It has tracked nothing since 2016, before the 2019 week rollover.

Checked on 2026-09-27:

- *The feed.*  Four receivers off the bench, on the same HP-designed
  distribution amplifier, track.  Both units are on DC-block ports,
  whose load resistor is what their antenna-current reading sees.
- *The Z3805A's internal RF cable.*  Checked by the operator.
- *The rails,* read in the pForth console with `adc_5v`, `adc_p15v`,
  `adc_m15v` and `adc_ant_curr`, and `hardware_bits` 0 in every
  snapshot of the six hours before:

  | | +5 V | +15 V | −15 V | Antenna current |
  | - | ---- | ----- | ----- | --------------- |
  | Z3801A | 4.94 | 14.95 | −15.21 | 30.39 |
  | Z3805A | 4.94 | 14.88 | −15.08 | 26.47 |

- *The engines' state,* from `print_stat` in the same sessions: date
  1/01/2007, no satellite visible or tracked, every assigned channel
  in mode 0, Code Search.  RX STATUS was 08 on the Z3801A and 09 on
  the Z3805A, whose bit 0 is Bad Almanac in the VP Oncore reference.
  The day before, the Z3801A's engine had read 02/10/2007, 2026-09-26
  less 1024 weeks.
- *An initial date and time.*  Both accepted
  `:GPS:INITial:DATE 2007,2,11` and `:GPS:INITial:TIME` at the same
  offset, 1024 weeks behind UTC, with no error (097-59551-02, 5-7 and
  5-8; volatile, and valid only before the first satellite is
  tracked).  Neither tracked a satellite in the 30 minutes after.
- *The feed, again.*  The HP 58517A eight-way amplifier then failed
  on and off for every receiver on it, whichever one powered it.  On
  an HP 58516A four-way with the Taoglas, a known-good receiver held
  a surveyed fix throughout while neither unit tracked a satellite in
  the 29 minutes after a power cycle.
- *`:SYSTem:PRESet`* on both, then an hour on the same four-way beside
  the same locked receiver: neither tracked a satellite.  Both came up
  surveying (`:GPS:POS:SURV:STAT?` `ONCE`) with a 10 degree mask and
  no PRNs ignored; the power-up survey setting read 1 on the Z3805A
  and 0 on the Z3801A.  `:SYSTem:PON` was refused by both.
- *What each engine expects to see.*  With the bench position held
  and the date and time given (1024 weeks behind UTC), on the same
  four-way as a known-good receiver, 2026-09-27 at about 21:25 UTC:
  the Z3805A predicted PRNs 3, 4, 6, 7, 9, 16, 26 and 27 within a
  degree or two of where the known-good receiver was tracking them, 42
  to 46 dB-Hz for the higher ones, and was attempting 3, 4, 7, 9, 16
  and 26, tracking none.  The Z3801A predicted 1, 8, 10, 11, 14, 18,
  22, 31 and 32, with the same elevations and azimuths its console's
  `print_vis` gave on 2026-09-26: its 2016 almanac, not the sky.  The
  Z3805A searches the right satellites in the right places and does
  not acquire them; the Z3801A searches the wrong ones.
- *`master_reset`, then 20 dB more gain* (a Raven LA-21-1575-100-T,
  under 3 dB noise figure) on the Z3805A: its engine held PRNs 9, 14
  and 22, which the known-good receiver had at 39 to 44 dB-Hz, for
  nearly five minutes without acquiring any.
- *Overnight, 2026-09-28, both after `master_reset`, a power cycle and
  the same 20 dB ahead of the four-way:* the Z3805A held lock about 98%
  of nine hours on 2 to 4 satellites; the Z3801A locked for part of an
  hour, then tracked nothing from 10:00 UTC on.

On 2026-10-03, over 24 hours on one splitter beside a 58503A and a
u-blox NEO-M8T, the Z3805A held no satellite the NEO-M8T read below
39 dB-Hz, with a 20 dB LNA ahead of it that the others lacked; the
58503A held 88% of those at 36 to 39 dB-Hz ([`bench-sky-2026-10-03.html`](https://htmlpreview.github.io/?https://github.com/charlieh0tel/smartclock/blob/main/docs/bench-sky-2026-10-03.html)).

*Next:* one of these engines in the 58503A (3710A01056), whose
firmware takes a six-channel engine (`firmware/gps.md`, "Six or eight
channels").  If it tracks there, the fault is in the Z380x unit; if
not, in the engine.  The 58503A's power supply has been replaced.

- [ ] TODO: fit a Z380x engine into the 58503A.

*Photos:* the Z3805A engine's antenna connector joints, on the side
away from the connector (`z3805a-gps-engine-antenna-input.jpg`,
1550,1650 to 2900,2800), carry brown and black residue around the
center pin's joint and its pad, and on the trace that leaves it.  The
Z3801A engine's same five joints show no black residue
(`z3801a-gps-engine-top-unshielded.jpg`, 550,1100 to 1050,1650), but
that engine tracks worse, so the comparison proves nothing.  Flux left
from a rework, or contamination;
the photo cannot say whether it loads the RF input.  Suggests cleaning
those joints and checking the center pin's resistance to ground before
the engine swap; settles nothing.

## 12. Field upgrades -- Z3801A reinstall verified

The flasher (now `smartclock-cli flash`) reinstalled the Z3801A's own
dump on 2026-09-28; PRIMARY boot and the recorded settings were
verified.  See [the flasher](firmware/restart.md#the-flasher) for the
procedure, checks and limits.  Other models and revision changes have
simulator coverage, not hardware validation.  Which flash part holds
which byte lane is unknown.

An interrupted load that leaves invalid primary checksums enters the
protected installer at power-up.  Recovery after Ctrl-C mid-record is
untested, and no test is planned.  Recovery with valid checksums but
an unusable primary is item 13.

## 13. S1 and recovery from a checksum-valid unusable primary

*Open item:* what each of the eight positions on S1 controls, and
whether any forces INSTALL without a working primary interpreter.
The Z3801A 3543 and Z3805A 3543B reset-to-primary paths contain no
switch test, and a running primary enters the installer only through
the SCPI task (`firmware/restart.md`, "Forced installer entry with an unusable
primary").  The pForth console's `execute` reaches that same exit,
verified on the Z3801A and Z3805A on 2026-09-28.  This does not exclude
a hardware effect on booting.  The hypotheses below model flash read
faults that preserve the installer while failing primary checks.

- [ ] TODO: with the unit open, read the byte at `0x302000` from the
  pForth console (`3153920 c@ .`) before and after changing each S1
  position, one at a time, powered down.  The Oman installer reads that
  byte for its host-port settings (`firmware/restart.md`, "The switch byte at
  `0x302000`"); whether S1 drives it is unknown.  See hypothesis 4
  below.
- [ ] TODO: on an unpowered board, map S1's connections to buffers,
  CPU pins, flash or programmable logic.  Record the assembly
  revision, physical switch numbering and which contacts close in the
  marked ON position.
- Check whether any contact reaches the CPU's reset/debug signals or
  the flash's address/control signals.  Keep those possibilities
  separate from an ordinary input byte read by software.
- Locate BKPT/DSCLK, IFETCH/DSI and IPIPE/DSO.  MC68331UM section
  5.10.2 documents debug access independent of a running primary.
  Protected installer entry `0xa58` is a candidate destination after
  reset has initialized the memory interfaces; see `firmware/restart.md`,
  "Forced installer entry with an unusable primary".  Neither the
  board connection nor that recovery method has been tested.

*Photos:* S1 is an 8-position DIP switch on the Z3805A and 58503A.  On the
Z3805A it is under the oven cable in the close-up and whole in
`z3805a-main-board.jpg` (2390,1960 to 2620,2120), too blurred to say
which way any position is set; on the 58503A it is a CTS part marked
ON at position 1's end (`58503a-interior.jpg`, 1170,1770 to
1350,2100), all eight actuators alike.  Its lines leave through vias.
A fitted right-angle pin header, J12, sits at the board edge beside
U33 (`z3805a-cpu-fpga-flash.jpg`, 3500,750 to 3850,1150), five
positions seen in one row and partly behind a bracket: the place the
MC68331 manual recommends for its 10-pin BDM header (MC68331UM
5.10.2.8, Figure 5-11), but whether it carries BKPT/DSCLK,
IFETCH/DSI and IPIPE/DSO needs a continuity check.  J9, a 2 x 5
socket near U24 (3250,1550 to 3500,1850), is the other unexplained
connector.

- [ ] TODO: J12 continuity, below.

*Procedure, J12 (power off; "Working on the open Z3805A board" above).*
MC68331UM 5.10.2.8, Figure 5-11, gives the recommended 10-pin, 2 × 5
BDM header:

| Header pin | Signal | U33 pin |
| ---------- | ------ | ------- |
| 1 | DS | 85 |
| 2 | BERR | 70 |
| 3 | GND | VSS |
| 4 | BKPT/DSCLK | 56 |
| 5 | GND | VSS |
| 6 | FREEZE | 58 |
| 7 | RESET | 68 |
| 8 | IFETCH/DSI | 55 |
| 9 | VDD | VDD |
| 10 | IPIPE/DSO | 54 |

1. Count J12's pins and rows: look behind the bracket with a mirror
   or a phone camera, and note whether it is 1 × 5 or 2 × 5 and any
   key, missing pin or pin-1 mark on the silkscreen.  Photograph it
   (`bench.md`, "Photos still wanted").  Number the pins by the
   silkscreen mark, else from the end nearest U33, odd pins in the row
   nearer the board edge, and say which.
2. Read every J12 pin to ground and to +5 V.  Record each value.  On
   the standard header two pins read 0 Ω to ground and one 0 Ω to
   +5 V.
3. Read every J12 pin to each of U33 pins 54, 55, 56, 58, 68, 70 and
   85.  A pin can reach more than one if two U33 pins share a
   pull-up; record all readings under 1 kΩ.
4. On the BDM lines that reach J12, read U33's pin to +5 V and to
   ground.  A pull-up here shows a line held inactive for a debugger
   to override; record its value.
5. If none of 54, 55 and 56 reaches J12, read J12's pins against the
   other candidates: U33 pins 52 and 53 (TXD, RXD), 43 to 49 (the
   QSPI), the XC3042's configuration pins (U32; the PC84 pin table in
   `third_party/xilinx/xc3000.pdf`), and the ±15 V rails.

Expected:

| Hypothesis | J12 reads |
| ---------- | --------- |
| The standard BDM header | the table above, pin for pin, 2 × 5 |
| BDM in another order or a subset (a 1 × 5 with GND, DSCLK, DSI, DSO and RESET, say) | 54, 55 and 56 each on one J12 pin, in some order; record the order |
| Not BDM | none of 54, 55, 56 on any pin; step 5 names what it is |

Record the table of J12 pin to U33 pin, ground and +5 V, with
resistances, under this item, and update J12's entry in
`z3805a-board.html`.  If it is BDM, hypothesis 2's "board access"
becomes known and its recovery remains untested; using it needs a
BDM pod and the owner's say-so, powered, and is a separate step.

*Changes:* a verified switch map and, if the board supports it, a
recovery procedure for a primary whose checksums pass but which cannot
accept `:SYSTem:LANGuage "INSTALL"`.

### Recovery hypotheses

*Speculative, by request: hypotheses to guide a bench investigation,
not findings.  Facts they rest on are in `firmware/README.md`.*

Research question: can a jumper or the eight-position S1 force recovery
when PRIMARY has valid checksums but cannot accept the command to enter
INSTALL?  The boards of interest are the Z3801A and Z3805A, with a
58503A available for comparison.  No S1 position has been identified;
the one candidate input is the byte at `0x302000` (hypothesis 4).

These are hypotheses, not instructions for changing switches or wiring.
The investigation used the firmware dumps and the MCU manual; no
receiver was accessed.  Results recorded on 2026-09-28.

### Candidate mechanisms

| Hypothesis | Mechanism | Current evidence |
| ---------- | --------- | ---------------- |
| 1. Alter primary flash reads | Address aliasing or select gating makes primary checksums fail while protected boot code remains readable. | Fits the reset code; modeled against both dumps below.  S1 wiring is unknown. |
| 2. Enable background debugging | A contact enables BDM through BKPT at reset; a debugger redirects execution to the protected installer. | MCU support and installer entry are established; board access and recovery are untested.  A switch alone does not select INSTALL. |
| 3. Select alternate boot storage | Bank selection changes the reset vectors or startup code presented to the CPU. | No alternate recovery image or selection circuit identified. |
| 4. S1 is the byte at `0x302000` | An 8-bit input in CS5's window that software reads. | Only the Oman installer reads it, to override host-port settings; no firmware on the bench units reads it.  Nothing ties it to S1. |

The related 55300A manual assigns S1 B1 to "Preset All Serial Ports at
Powerup" and B2 to "Password Required" (097-55300-01, figures 3-14 and
3-15A; `third_party/hp/097-55300-01-iss-1.pdf`).  Those assignments are
not established for these boards.  The reset path and installer entry
the hypotheses rely on are in
[`firmware/restart.md`](firmware/restart.md#forced-installer-entry-with-an-unusable-primary).

### 1. Alter primary flash reads

#### What the firmware establishes

The Z3801A 3543 and Z3805A 3543B dumps have identical reset code from
`0x550` through `0x745`; all 502 bytes were disassembled.  The path has
no switch-byte test: four lane checksum comparisons decide whether to enter installer startup at `0x746` or jump to PRIMARY at
`0x744`.

Both configure two separate 256 KiB flash banks.  Addresses below are
CPU byte addresses, not individual flash-chip addresses.

| CPU range | Read select and setup | Write select and setup |
| --------- | --------------------- | ---------------------- |
| `0x00000`--`0x3ffff` | CSBOOT: CSBARBT = `0x0005`, CSORBT = `0x6870`, written at `0x578`/`0x580` | CS6: CSBAR6 = `0x0005`, CSOR6 = `0x7070`, written at `0x5e8`/`0x5f0` |
| `0x40000`--`0x7ffff` | CS1: CSBAR1 = `0x0405`, CSOR1 = `0x6870`, written at `0x598`/`0x5a0` | CS7: CSBAR7 = `0x0405`, CSOR7 = `0x7070`, written at `0x5f8`/`0x600` |

MC68331UM sections 4.8.1.2 and 4.8.1.3, tables 4-21 and 4-22, decode
these settings: 256 KiB blocks, both byte lanes, reads or writes only,
one internally generated wait state, supervisor and user spaces.
How those selects connect to the flash parts is not yet traced.

The low bank contains both the protected installer and part of PRIMARY:

- `0x00000`--`0x0ffff`: protected boot region.
- `0x10000`--`0x3ffff`: first part of PRIMARY, including its vectors.
- `0x40000`--`0x7ffff`: remainder of PRIMARY.

The installer copy records begin at `0xbbc` and end with the terminator
at `0xb328` in both dumps.  Every record's source bytes are below
`0x10000`.  The checksum-failure startup, copy routine and installer
source therefore remain readable if a fault leaves the whole protected
region unchanged.  That is a necessary condition, not successful
execution on faulty hardware.

#### Read-fault modeling

For each altered read view, the model sums even and odd bytes
separately, modulo 65536.  Data ends at `0x3fffb` and `0x7fffb`; each
bank's final four bytes contain the two interleaved stored sums.  The
model alters those stored-sum reads too, as actual address aliasing or
bank deselection would.  It does not modify either source dump.

| Modeled read behavior | Protected region unchanged? | Z3801A 3543 checks | Z3805A 3543B checks |
| --------------------- | --------------------------- | ----------------- | ------------------ |
| Normal reads | Yes | Pass | Pass |
| Upper bank returns `0xff` for every byte | Yes | Fail | Fail |
| Upper bank returns `0x00` for every byte | Yes | **Pass** | **Pass** |
| Upper bank aliases the lower bank | Yes | Fail | Fail |
| Flash reads with CPU address bit 16 cleared | Yes | Fail | Fail |
| Flash reads with CPU address bit 17 cleared | Yes | Fail | Fail |

The address-bit cases were checked for the even lane alone, odd lane
alone and both lanes; all six cases fail on each image while preserving
every protected byte.  For interleaved eight-bit chips with
conventional sequential wiring, CPU address bit 16 is chip address bit
15, and CPU bit 17 is chip bit 16; this needs a board trace.  The model
changes the address flash sees; it does not model grounding a driven
CPU address line.

For the all-`0xff` case, each upper lane sums 131070 data bytes to
`0xfe02`, while its stored sum reads `0xffff`: under that read value
the check always fails.  All-zero data and stored sums compare equal.  A floating bus is not guaranteed to read either value.

The MCU supplies the read acknowledgment internally for these settings.
So gating the flash output externally, with the MCU's chip-select
configuration intact, could complete bus cycles with wrong data rather
than produce a bus error.  That is an inference from the manual; board
loading and decode logic are unmeasured.

#### Implications for S1

The strongest specific versions of hypothesis 1 are:

1. A contact gates the upper bank's read enable, leaving the low bank
   alone.  It must produce checksum-breaking read values; deselection
   alone is not enough, because zero-filled reads pass.
2. A contact causes flash address bit 15 or 16 to read low through
   suitable isolation or selection logic.  The corresponding CPU
   address alias preserves the installer while failing the checks on
   both existing images, even when it affects only one byte lane.

Neither needs the CPU to read S1 as a byte; the firmware sees the
result only through its checksum comparisons.

Disabling the whole low bank would also hide the reset code and is
not this mechanism.  A write-protect switch alone would not change
read checksums or prevent the jump to PRIMARY.  A global data-bit fault
would also affect boot instructions; the model does not support it as
a clean recovery method.

Even if a read override gets INSTALL running in RAM, normal flash
access must be restored before programming and verifying PRIMARY.
Whether a switch can be restored while running, whether its state is
latched at reset, and whether it affects write addressing are
unresolved.  No live-switching procedure is established.

#### What would confirm or reject it

On an unpowered board, trace S1 to the flash address pins, read-enable
and chip-enable logic, and the CPU's CSBOOT/CS1/CS6/CS7 signals.
Record the assembly revision and switch numbering.  A connection to a
flash-side address selector or upper-bank read gate supports this
hypothesis; one only to unrelated GPIO or debug signals redirects the
investigation.

The model verifies checksum consequences for known images, not the
electrical circuit, an S1 assignment, an installer session, or recovery
of a real unit.  Those remain open.

### 2. Enable background debugging

The MC68331 has an independent way to redirect execution: background
debug mode (BDM).  MC68331UM sections 5.10.2.1 and 5.10.2.2 describe
enabling it through BKPT at reset and entering it through a hardware
breakpoint.  Sections 5.10.2.5.2 and 5.10.2.6 describe changing the
return program counter (RPC) and resuming with GO.  After the reset
code has configured the memory interfaces, redirecting execution to the
protected installer entry at `0xa58` is a candidate recovery for a
checksum-valid primary that cannot accept commands.  This is an
inference from the CPU manual and firmware, not a tested procedure.
Access to BKPT/DSCLK, IFETCH/DSI and IPIPE/DSO on these boards has not
been mapped.

### 4. S1 is the byte at `0x302000`

The facts are in `firmware/restart.md`, "The switch byte at `0x302000`": the
Oman installer (58503A 3633) reads that byte at every start and, when
bit 0 is clear, takes the host port's baud, framing and pacing from
bits 1 to 4.  That matches the 55300A's S1 B1, "Preset All Serial
Ports at Powerup", in kind.  If S1 drives this byte, it sets the
installer's line settings; it does not force INSTALL, and on the
bench units' Peru and USA installers it does nothing at all.

It can be tested without firmware that uses it.  From the pForth
console, `3153920 c@ .` reads the byte (`0x302000` is 3153920).  Read
it, change one S1 position with the unit powered down, and read it
again; a bit that follows the switch maps that position.  If no bit
changes for any position, the hypothesis is rejected for that board.
This needs `:SYSTem:LANGuage "PFORTH"` and `halt` to return to SCPI
(item 14), and has not been done.  What else a read of `0x302000` does on
the Z3801A and Z3805A boards is unknown.

## 14. Leaving the console with `halt` -- done

*Question:* whether `halt` returns the port from the pForth console to
SCPI as the images say, and what each visit costs (`firmware/console.md`,
"Leaving it").

- With the daemon stopped, at the unit's own line settings, capture a
  transcript of: `*IDN?`, `:SYSTem:LANGuage?` and `:SYSTem:ERRor?`
  until empty; `:SYSTem:LANGuage "PFORTH"`; `mem_rep`; `halt`; then
  `*IDN?`, `:SYSTem:LANGuage?` (expect `"PRIMARY"`), `:SYSTem:ERRor?`
  and a few ordinary queries.
- Repeat once and compare the two `mem_rep` reports for the leak per
  visit.  No more than two visits per unit between power cycles.
- If the port stays silent or at the console prompt, leave it through
  the installer as `read-memory` does, or power cycle.

Done on 2026-10-04, two visits on each of the Z3805A and the 58503A:
`halt` returned to SCPI in PRIMARY each time with an empty error queue,
and each unit's second `mem_rep` matched its first, 18,790 and 16,248
bytes free (`firmware/console.md`, "Leaving it").

*Changes:* `read-memory` and its kin leave the console with `halt`
first on every image, keeping the installer route as the fallback, and
firmware with no known installer exit is no longer refused.

## 15. A receiver swapped under a running daemon -- done

*Question:* whether the slow tier's `*IDN?` check catches a cable
moved between two units on the same line settings (PLAN.md, "Rows
belong to a receiver, not to a file").

Done on 2026-10-05 with the installed daemons stopped and a throwaway
`smartclockd --device /dev/ttyUSB0 --baud 19200 --slow 10` logging to a
scratch file:

- *Z3805A to 58503A, slowly.*  Reads failed long enough to call the
  link dead; the daemon reopened and attached as the 58503A.  The
  Z3805A's rows end at 21:21:31 UTC under its serial, the 58503A's
  begin at 21:21:48 under its own.  The check took no part.
- *58503A to Z3805A, in a few seconds.*  No run of failures; the check
  logged `the receiver was swapped`, dropped the one queued command and
  reattached as the Z3805A.  Five rows from 21:22:51.5 to 21:22:54.5,
  with the Z3805A's EFC of 45.476 %, are filed under the 58503A: the
  window up to one slow pass that PLAN.md states.

## 16. What the Z380x engines do with `@@Ci` format 0 -- not planned

*Open item:* the Z3801A's and Z3805A's init scripts open with `@@Ci`,
switch I/O format, with format 0, which `VPCommands.pdf` does not
define (`firmware/gps.md`, "Requests").  The message has no binary
response, so the firmware cannot say what the engine makes of it.

- [ ] TODO: capture the engine's serial line through a Z380x init
  script, or send `@@Ci` with 0 to a bare VP Oncore, and see whether it
  stays in Motorola binary, answers, or changes format.

*Changes:* closes "What a Z380x engine does with `@@Ci` format 0".
Nothing in this project depends on it.  Not planned: tapping an engine's serial line is not worth it for
this.

## 17. The six LEDs on the 58503A's main board

*Open item:* the bench 58503A's main board (58503-60001 Rev C) carries
six LEDs, which the operator takes to be bicolor, red and green.
Nothing here says what they show.

- [ ] TODO: note each LED's color through power-up, lock, a manual
  holdover (`:SYNChronization:HOLDover:INITiate`), recovery, an alarm,
  and the installer and pForth debug console.
One is a heartbeat (the owner, 2026-10-20): the firmware flips DUART
OP2 about every 1.1 s.  The firmware reads as if the six are the
DUART's OP2 to OP7: heartbeat, installer running, and the CPU, RAM,
DUART and later self-test failures (`firmware/outputs.md`, "The rest
of the bank").

- [ ] TODO: find what drives them in the firmware: a port the start-up
  code sets, the DUART's output port, or a write `:DIAGnostic:OUTPut`
  or the self-test makes.

*Photos:* the Z3805A and Z3801A carry the same row of six
surface-mount LEDs at the board's rear edge, beside the RAM at U8 and
U9 (`z3805a-main-board.jpg`, 100,3300 to 700,3750;
`z3801a-main-board.jpg`, 3150,2700 to 3700,3072), with U1, an SO-14,
beside them; the Z3801A's U1 is a Motorola ACT-family part whose
number is not legible, the Z3805A's not legible at all.  The 58503A's
row is the same (`58503a-interior.jpg`, 2100,3800 to 2600,4080).  An
unpowered photo cannot show a color, and the LEDs' lines leave
through vias, so nothing here is settled.

*Changes:* what can be read off an open board without a serial link.

## 18. Whether loading 10 MHz OUT B disturbs OUT A

*Open item:* both 58503A 10 MHz stages take their signal from the
oscillator through the same network (`58503a-second-10mhz.md`), so
loading OUT B (J15) may move OUT A (J17), the receiver's reference
output.

- [ ] TODO: watch J17's amplitude and phase against a steady
  reference, with the scope's statistics on, while J15 is open,
  shorted and terminated in 50 Ω.  OUT B's stage has the 28.1 Ω pair
  in series ahead of the jack, which limits the current into a short.

*Changes:* whether OUT B can be used without disturbing OUT A.

## 19. The 58503A's second 1 PPS (J4)

*Open item:* the 58503A's J4 (silkscreen OUT A) is fitted but carries
no 1 PPS.  The Z3805A, on the same layout, cables J4 and J5 to its two
rear 1 PPS outputs.  The two boot different FPGA bitstreams, and
no CPU code differs in a way that would drive J4
(`firmware/outputs.md`, "The FPGA bitstreams").  On the component
side, no part near J4 is fitted on the Z3805A and missing on the
58503A.  There is no photo of the Z3805A's back side.  Decoded with
an XC3000 bitstream viewer, the Z3805A's bitstream makes FPGA pin 45
an output that the 58503A's leaves unused, and pin 14 a registered
output where the 58503A's is unregistered (`firmware/outputs.md`).

- [ ] TODO: follow J4's center pin back to its buffer (U17 or U18) and
  scope the buffer's input and output.  A pulse at the input and none
  at J4 means a part between them is missing.  No pulse at the input
  points to the FPGA bitstream.
- [ ] TODO: photograph the back of both boards under J4 and J5 and
  compare the fitted parts (`bench.md`, "Photos still wanted").
- [ ] TODO: with power off, check continuity on the 58503A from U32
  pin 45 to the input of J4's buffer.  Scope U32 pin 14 on the 58503A,
  and pins 14 and 45 on the Z3805A, for 1 PPS.
- [ ] TODO: on the 58503A, check the direction (pin 1) and output
  enable (/OE, pin 19) of the transceiver that drives J4: a disabled
  output blocks J4 whatever pin 45 does.  On the Z3805A, U17 and U18,
  beside J4 and J5, are TI 74ABT245A octal bus transceivers with
  3-state outputs, and U19 and U20 are ACT11245s.

*Photos:* U17 and U18 are both marked TI 66CK05K ABT245A
(`z3805a-pps-transceivers.jpg`; `z3805a-board-right.jpg`, 1350,2300
to 2400,3350); U24 is a third of the same lot; U19, U20 and U38 are
TI 66AL5CT ACT11245.  Data sheets: TI SN74ABT245
(`third_party/ti/sn74abt245b.pdf`, which covers the SN74ABT245B, the
same pinout and function as these SN74ABT245As: DIR pin 1, /OE pin
19) and TI 74ACT11245 (not in `third_party/`).  On the Z3805A J4's and J5's center pins
are under the cable lugs, and the lines from U17 and U18 toward them
leave through vias at the transceivers' pins: not followable.  The
58503A's U17 and U18 are fitted, their markings under a cable
(`58503a-gps-engine.jpg`, 1550,3450 to 2000,4080), so no transceiver
is missing on the component side.  The back sides remain
unphotographed.

*Changes:* whether the 58503A can have a second 1 PPS, and whether
that takes parts or the Z3805A's FPGA bitstream.

## 20. Where the Z3805A's lamp latch goes

*Open item:* the firmware drives five front-panel lamps through the
latch at `0x300000` (in chip select 5's window; `firmware/restart.md`,
"The chip selects"): bit 0 Enabled, 1 Active, 2 Alarm,
4 GPS Lock, 5 Holdover, written whole by `leds_update` (`0x464de`)
about once a second (`firmware/outputs.md`, "The Z3805A's lamps").
Bits 3, 6 and 7 drive nothing the firmware names; the 58503A uses 6
and 7 to select its front-panel device.  The table is the firmware's
and the Z3801A manual's (097-z3801-01, 1-2 and 2-9), not a board's.
The board's own bank, the DUART's OP2 to OP7, is item 17's and
`firmware/outputs.md`'s "The rest of the bank".

- [ ] TODO: on a Z3805A board, find the latch (the part CS5, decoded
  for `0x300000`, strobes) and follow its outputs: which pins reach
  lamps or a connector, and where bits 3, 6 and 7 go.
- [ ] TODO: watch the lamps through power-up (`lamp_test` lights all,
  then each bit in turn, `0x133` ticks apiece), lock, a manual
  holdover (`:SYNChronization:HOLDover:INITiate`) and an alarm, and
  set `:LED:ENABled` and `:LED:ACTive` both ways, to tie each bit to a
  lamp.
- [ ] TODO: note whether the Z3805A fits all six lamps of the
  Z3801A's panel, or a header for them, and what Power is wired to.
- [ ] TODO: note whether the latch has a reset or clear input tied to
  the board's reset, or keeps its outputs through a reset.

*Photos:* no part on the Z3805A's component side can be tied to chip
select 5 by its traces.  The board's lamps are on a separate narrow
board at the front of the chassis, reached by a ribbon cable from a
header at the upper right of the main board (`z3805a-interior.jpg`,
2500,1050 to 4080,2450); that board's back shows a column of two-pad
joints, as for lamps, its parts facing away, and its lamps were not
counted.  At about the same place on the Z3801A is J13, bare, beside U44,
a Philips 74ABT574AD (`z3801a-main-board.jpg`, 1600,0 to 2200,500):
an octal D flip-flop with 3-state outputs, its outputs enabled by
/OE, and no clear or reset input (`third_party/philips/
74ABT574A.pdf`, Table 2; the AD is the 74ABT574A in SO-20).  Its
feature list does claim a power-on reset, so it may come up cleared
from a supply cycle, but no pin clears it afterward.  An 8-bit register the firmware writes whole, beside the
front-panel header, is what the lamp latch looks like, so U44
is the candidate.  On the Z3805A that corner is out of focus in every
photo (`z3805a-main-board-2026-09-27.jpg`, 2350,2250 to 3150,3072):
neither U44's designator nor its marking was read there.  If U44 is
the latch, the part itself keeps its outputs through a reset,
unless its /OE is tied to the reset line.  Suggests; the bench still has to tie CS5's decode to U44's
clock and follow its outputs.

*Which select.*  Settled from the image (`firmware/restart.md`, "The
chip selects"): the boot and primary reset code (`0x5d8`/`0x5e0`,
`0x14542`/`0x1454a`) make CS5 (U33 pin 120) 64 KiB at `0x300000`, an
8-bit port with 12 wait states, holding the latch, the switch byte at
`0x302000`, the FPGA at `0x304000` and the EFC strobe at `0x306000`,
8 KiB apart; chip select 1 is the upper flash bank's read select
(`0x598`/`0x5a0`).  "Chip select 1" for the latch was the Z3816A's
map, where CSBAR1 is `0x3000`.  So the latch's strobe is CS5 decoded
on ADDR13 and ADDR14 (and perhaps ADDR15, which no access sets),
`0x300000` being the window's first 8 KiB; the procedure finds the
decoder, which the image cannot.

*Procedure (power off; "Working on the open Z3805A board" above).*

1. *Find the latch.*  Read U44's marking on the Z3805A, beside S1,
   with a loupe and raking light (`bench.md`, "Photos still wanted").
   If it is a '574, use its pinout above.  If it is a '273, pin 1 is
   /MR, a clear; if a '374 or '573, take its pinout from its data sheet
   before going on.  If U44 is not a register, look for an SO-20 or
   DIP-20 near the ribbon header and read each against step 3.
2. *Its outputs.*  Find the ribbon header's designator.  Read U44's Q0
   to Q7 (pins 19 down to 12 on a '574) to every header pin; a path
   through a resistor or a driver transistor reads as one: note it.
   Then, with the ribbon unplugged only if the owner agrees, put the
   meter's diode test across each lamp-board header pin and its ground
   pin, both ways round: an LED lights faintly, which names the lamp
   without power.  Count the lamps fitted and note which header pin
   reaches none.  Power, if it is a lamp, should go to +5 V or a
   supply pin, not to the latch.
3. *Its strobe.*  Read CP (pin 11) to each of the four 74AC138s' eight
   outputs (pins 7, 9 to 15) and to U33 pin 120.  On the '138 it
   reaches, read A0 to A2 (pins 1 to 3) and the enables (4 to 6) to
   U33 pins 35, 36, 37, 120 and 79.  If CP reaches no '138, read it
   to U32's pins (the FPGA) and record which.
4. *Its data.*  Read D0 to D7 (pins 2 to 9) to U33's DATA8 to DATA15
   (pins 100, 99, 98, 97, 94, 93, 92, 91).  If they read open, the bus
   is buffered: find the ACT11245 or ABT245A (U19, U20, U24 or U38)
   whose B side the D pins reach, and read its A side to U33.  Record
   the bit order: firmware bit n is DATA(n + 8).
5. *Its enable and any clear.*  Read /OE (pin 1), and /MR if the part
   has one, to ground, to +5 V, to U28 pins 6 (RESET) and 5
   (/RESET), and to U33 pin 68 (RESET).

Expected:

| Hypothesis | Reads |
| ---------- | ----- |
| U44 is the latch, strobed by chip select 5 at `0x300000` | CP on a '138 output: /Y0 if its A0 and A1 are ADDR13 and ADDR14 and A2 is a constant or ADDR15; an enable on pin 120; perhaps R/W on another enable |
| The latch keeps its outputs through a reset | /OE 0 Ω to ground, and no /MR |
| A reset blanks the lamps | /OE to U28 pin 6 or to a line that follows it; the latch keeps its data, the lamps go dark during reset and come back at the next write |
| A reset clears the latch | a '273 with /MR on U28 pin 5 or U33 pin 68 |
| Bits 3, 6 and 7 are free | Q3, Q6 and Q7 reach no header pin, or header pins no lamp is on |

Recording: the latch's part, its CP, /OE and any clear with what each
reads to, a table of bit (D pin, DATA line) to Q pin to header pin to
lamp, and the lamps fitted, and the '138 and its A and enable lines
in `firmware/restart.md`, "The chip selects".

*Power needed:* only to watch a reset's effect on the lamps, which
the unpowered reads predict.  It is item 7's external reset: ask
first, since it restarts the receiver, and pulse U28's /RESIN (pin 2)
to ground through a push button, not a probe tip.  Lamp-to-bit by
watching `lamp_test` at power-up needs only the lid off and a power
cycle, and the owner's agreement to the cycle.

*Changes:* whether the firmware's lamp table is the board's, and
whether bits 3, 6 and 7 are free.

## 21. The DUART's interrupt line

*Open item:* the reset code makes chip select 10 the interrupt
acknowledge for level 2 with external DSACK (CSBAR10 `0xfff8`, CSOR10
`0x2bc4`, `0x628`/`0x630`; `firmware/restart.md`, "The chip
selects"), and the DUART answers with the vector its IVR holds, so its
interrupt should be level 2.  Nothing in the firmware shows the wire.

- [ ] TODO: with power off, check continuity from the 68681's /INTR to
  the MC68331's /IRQ2, and from chip select 10's pin to the 68681's
  /IACK.

*Photos:* not followable.  U3, the SCC68692C1A44, is at the board's
bottom edge (`z3805a-cpu-fpga-flash.jpg`, 1950,2800 to 2550,3072) and
U33, the MC68331, at the upper right (2100,500 to 3000,1500), with the
FPGA, the logic and the flash sockets between them; the lines from
U3's pins drop into vias within a few millimeters, and the visible
layer carries no line from one part to the other.  /INTR, the 68681's
IRQ, is an active-low open-drain output (MC68681.pdf, 2.10; INTRN in
`third_party/philips/SCC68692.pdf`, pin descriptions), so it
could share
/IRQ2 with other sources; U29, a DM74ALS09 quad AND with open-collector
outputs beside U33 (3250,1050 to 3550,1350), is the kind of part that
would merge such lines, but nothing ties it to /IRQ2.  Still needs the
continuity check.

*Procedure (power off; "Working on the open Z3805A board" above).*
U3's PLCC pin numbers are the SCC68692 data sheet's (that section's
table); confirm GND 22 and VCC 44 on the board first, and if they do
not read 0 Ω to ground and +5 V, the pin count is off: stop and
recount from pin 1.

1. Read U3 pin 24 (/INTR) to U33 pin 76 (PF2/IRQ2).
2. Read U3 pin 24 to U33 pins 71 to 75 and 77, the other levels.
3. Read U33 pin 76 to +5 V and to ground: an open-drain request needs
   a pull-up, so expect a resistor to +5 V; record its value, and find
   it on the board if it can be seen.
4. Read U33 pin 76 to each of U29's outputs (pins 3, 6, 8, 11).  If
   one reads 0 Ω, read that gate's two inputs to U3 pin 24 and record
   where the other input goes (U32, U3's OP pins, a connector).
5. Read U3 pin 41 (/IACK) to U33 pin 125 (CS10), U3 pin 39 (/CS) to
   U33 pin 123 (CS8), and U3 pin 10 (/DTACK) to U33 pin 89 (DSACK0),
   the firmware's external acknowledge for both selects.

Expected:

| Hypothesis | Reads |
| ---------- | ----- |
| Level 2, alone | pin 24 to pin 76 under 1 Ω; a pull-up on pin 76; no U29 output on it |
| Level 2, shared through U29 | pin 24 on a U29 input, that gate's output on pin 76; the other input names the second source |
| Level 2, wired-OR without a gate | pin 24 to pin 76 direct, and a U29 output or another part's pin on the same net |
| Another level | pin 24 on one of 71 to 75 or 77: the harness and the CS10 decode disagree with the board; record it and stop |
| Not followed | pin 24 open to every IRQ pin: a buffer between them; look for it on the back side (`bench.md`, "Photos still wanted") |

Steps 1 and 5 together are what this item asks; steps 3 and 4 say
what else the line carries.  Record the readings as a table under this
item, the pull-up's value and the shared source, if any.

*Changes:* whether the harness's level-2 DUART interrupt is the
board's.

## 22. Which EEPROM the Z3805A carries -- done

*Open item:* the 8 KiB EEPROM on chip select 9 (`0x400000`;
`firmware/restart.md`, "The chip selects") holds the records and
the log.  The firmware writes it a byte (`eeprom_write_byte`,
`0x31128`) or up to a 64-byte page at a time, the page loaded with
interrupts masked (`eeprom_write_page`, `0x3119e`), and polls the last
byte it wrote until it reads back (`eeprom.c`), the data polling of a
parallel EEPROM such as an X28C64 or AT28C64.  The
harness models that write cycle generically, the polled byte reading
its complement until the write is done, because the part is not
identified (the AT28C64B in `bench.md` is on the Oncore engine).  Now
identified, below.

- [x] Read from photos, 2026-10-23: U4, an Atmel AT28C64B-20JC date
  code 9625, 8 K x 8, 200 ns, in a 32-lead PLCC soldered to the board,
  not socketed (`z3805a-board-right.jpg`, 2700,300 to 3072,700;
  `z3805a-cpu-fpga-flash.jpg`, 2550,2850 to 3100,3072;
  `z3805a-main-board.jpg`, 380,2650 to 680,2950).  The 58503A's U4 is
  an AT28C64B too (`58503a-interior.jpg`, 700,3550 to 2600,4080), and
  the Z3801A's an Atmel part (`z3801a-main-board.jpg`, 2700,2650 to
  3050,3000).  It is the board's only parallel EEPROM, so chip select
  9's by elimination; the select line was not traced.

The part's data sheet (`third_party/microchip/AT28C64B.pdf`,
Microchip DS20006432B, sections 6.2 to 6.5 and Table 6-4) gives byte
or 1- to 64-byte page writes, a page being the 64 bytes that share
A6 to A12; each byte of a page must follow the last within 150 µs
(tBLC), or the part stops taking bytes and starts the write; a write
cycle (tWC) of at most 10 ms; and two ways to see its end: DATA
polling, where a read of the last byte written returns the complement
of its bit 7 until the write is done, and toggle bit, bit 6
alternating on each read.  The data sheet names bits 7 and 6 only;
the harness complements the whole byte, which the firmware's
whole-byte compare ends on the same way.  The firmware's page writes
keep within a page and mask interrupts while loading it, as tBLC
requires.  The part also has software data protection, shipped
disabled; its command addresses, `0x1555` and `0x0aaa` in the part
(`0x401555`, `0x400aaa`), appear nowhere in the Z3805A 3543B
decompiled C, so the firmware is taken not to use it.

*Changes:* the harness's EEPROM model can follow
the part's data sheet: its write time, page mode, toggle or data
polling.

## 23. The hex DAC's supply

*Open item:* which rail feeds U37's VDD, and so whether the
firmware's clock to it is in the part's specification.  U37 is the
MC144110DW hex 6-bit DAC (`firmware/ovens.md`, "What `hdac` is not"),
QSPI device 2, which `qspi_queue_setup` sends with SPCR0's divider 70
in SPI mode 2 (`firmware/structures.md`, "The QSPI's devices"): SCK
is 16.777 MHz over 140, 119.8 kHz, high and low 70 clocks each, so
CLK is low 4.17 µs a bit.  The data sheet (`third_party/motorola/
MC144110.pdf`, Rev. 2, Table 3, Switching Characteristics) gives the
least negative CLK pulse width, twL, as 5 µs at VDD 5 V, 3.5 µs at
10 V and 2 µs at 15 V, and the same 5, 3.5 and 2 µs for the ENB setup
and hold and the Din hold.  So at 5 V the firmware's low time is
0.83 µs short, and from the 10 V point up it is in range; between 5
and 10 V the table gives nothing.  The part runs from 4.5 to 15 V
(page 1), VDD scaling its outputs (4.3), so the board may run it from
the analog +15 V rather than +5 V; nothing in the firmware says
which.

- [ ] TODO: with power off, find the rail U37 pin 20 is on; then, with
  power on, read its voltage and the CLK low time.

*Photos:* U37 is at the upper middle of the CPU board
(`z3805a-board.html`, its marker; `z3805a-cpu-fpga-flash.jpg`), its
pin 20 trace not followable from the component side.

*Procedure, unpowered ("Working on the open Z3805A board" above).*
Confirm the pin count first: pin 9 (VSS) reads 0 Ω to ground; if it
does not, recount from pin 1.

1. Read U37 pin 20 (VDD) to ground, both ways round.
2. Read pin 20 to +5 V (U33 pin 63).
3. Find a point on each of +15 V and -15 V away from U37 (a
   regulator's output or a supply connector's pin; record which) and
   read pin 20 to each.
4. Read pin 12 (CLK), pin 8 (ENB) and pin 1 (Din) to ground and to
   +5 V, for the series parts or pull-ups between them and the QSPI's
   SCK, select and MOSI.

*Procedure, powered (needs the board running; ask first).*  The
daemon may run.  Ground the probe's clip at the J4 or J5 shell; probe
one pin at a time with a sharpened tip on the lead's toe, never
across two leads (1.27 mm pitch).

5. DC volts, U37 pin 20 to ground.
6. A scope on pin 12, triggered on its falling edge, during the
   interpolator's calibration after power-up (`ovens.md`) or an
   `hdac_write` from the console (a spare unit only, item 9): the low
   time, the high time and the edge times, and pin 8's level around
   the burst.

Expected:

| Hypothesis | Reads |
| ---------- | ----- |
| VDD on +5 V | step 2 under 1 Ω; step 5 about 5 V: the low time, 4.17 µs, is under the 5 µs twL, and the part works out of specification, by margin only |
| VDD on +15 V | step 3's +15 V under 1 Ω; step 5 about 15 V: in specification with room |
| VDD on its own rail | none of steps 2 and 3 under 1 Ω, a resistor or regulator on the way: record the value; from about 10 V up in specification by Table 3's points |
| Clock not the QSPI's | step 6's low time not 4.17 µs: something between SCK and pin 12; record it |

*Changes:* whether `quirks.md` records the hex DAC's clock as too
fast for its part.
