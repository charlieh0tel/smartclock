# Bench notes

What is on this bench, and what it has been seen doing.

This goes stale when hardware is swapped, so it is not in `README.md`,
`PLAN.md` or `AGENTS.md`, and no code should depend on it.  A running
daemon is the better source: `smartclock-cli --daemon ... diagnose`
heads its report with the model, serial and firmware of whatever is
attached, and every log row names its receiver.

## Antenna feed

As of 2026-10-10 the 20 dB gain block sits ahead of the distribution
amp, feeding every bench receiver and the u-blox.  It once fed the
Z3805A alone, as the dated results below say; when it moved is not
recorded.

## HP Z3801A, serial 3542A01548

Connected; its EFC gain was measured on 2026-10-10
(`docs/hardware-investigations.md`, item 2).  Its history is in
`docs/hardware-investigations.md`, items 11 and 12.

Power supply: BSC CV90-15357-2, for 38 to 60 V DC in, substantially
the same board as the Z3805A's (by inspection), though for a different
input range.  It has RIFA
capacitors too, not yet replaced.  Its flash is labeled Z3801-8007
Rev ZB.

## HP 58503A, serial 3710A01056

    HEWLETT-PACKARD,58503A,3710A01056,3704-C

Option 001: front-panel display and keypad.  19200 8N1, its fastest:
asked for 38400 it replies `+0,"No error"` and stays at 19200.
Oscillator: HP 10811-60159, serial 3505A15217; see `docs/ocxo.md` for the specification
and `docs/efc.md` for the EFC measurements taken on it.  Time zone
offset `+0,+0` (`:PTIMe:TZONe?`, read 2026-09-23), so its dates and
times are UTC.

GPS receiver, as `:DIAGnostic:IDENtification:GPSystem?` reports it:

    COPYRIGHT 1991-1996 MOTOROLA INC.
    SFTW P/N # 98-P36830P
    SOFTWARE VER # 8
    SOFTWARE REV # 8
    SOFTWARE DATE  06 Aug 1996
    MODEL #    B4121P1115
    HDWR P/N # _
    SERIAL #   SSG0220999
    MANUFACTUR DATE 7D01
    OPTIONS LIST    IB

Main board: 58503-60001 Rev C, marked hp.  Its output jacks, as the
operator found them on 2026-10-11:

| Jack | Label | State |
| ---- | ----- | ----- |
| J17 | 10 MHz A | 10 MHz OUT A, cabled to the rear 10 MHz BNC |
| J15 | 10 MHz B | 10 MHz OUT B; its output stage lacked three resistors (below), fitted 2026-10-11 (`58503a-second-10mhz.md`) |
| J5 | OUT B | cabled to the 1 PPS BNC |
| J4 | OUT A | not connected |
| J6 | none | cabled to the ALARM BNC |
| J7 | none | not connected; function not identified |
| J8 | none | not connected; function not identified |

No 58503A firmware drives a second pulse channel or a time-tag input
(`firmware/outputs.md`).

The two 10 MHz outputs each have their own output stage: on the
component side, Q12 and Q13 (Q13 marked 2907A) and the 78M12
regulator U46 behind J17, Q8 and Q9 and the 78M12 U45 behind J15,
all fitted.  From the board's other side, J17's stage on the left and
J15's on the right, the two match but for three empty footprints on
J15's side, where J17's side holds three resistors marked `56R2`,
56.2 Ω, both colors about 2.3 mm by 1.23 mm, so 0805: two side by side beside a
capacitor, black, and one at the end of the row of three `147R`,
147 Ω, blue; on J15's side that row holds the three `147R` and an
empty fourth footprint, checked by eye.  No 0 Ω link was found missing.
Each stage's black pair is in parallel, 28.1 Ω, and the two stages'
pairs share no node (continuity, 2026-10-11).

![The component side: J17 (10 MHz OUT A, cabled) and J15 (10 MHz OUT B), with their output stages](images/58503A-60001_RevC/58503a-j17-j15-front.jpg)

![The other side: J17's output stage on the left, J15's on the right](images/58503A-60001_RevC/58503a-j17-j15-back.jpg)

![Close-up of the other side: the empty footprints at the top right, on J15's side](images/58503A-60001_RevC/58503a-j17-j15-back-closeup.jpg)

![Close-up of J17's side: the two black and one blue 56R2 that J15's side lacks](images/58503A-60001_RevC/58503a-j17-back-closeup.jpg)

![J17's output stage from the other side, CR21 at the right: the two black `56R2` at the top, then a row of three `147R` and a blue `56R2`](images/58503A-60001_RevC/58503a-j17-back-detail.jpg)

![The other side, wider: the OCXO's 10 MHz jack at the lower left, a resistor and capacitor network above it, and the two output stages at the bottom](images/58503A-60001_RevC/58503a-ocxo-feed-back.jpg)

Its diagnostic log had been full at 222 entries, and so not
recording, since March 2025; the entries were copied out and the log
cleared, so it records again.  It showed nineteen holdover and relock
cycles over two days in March 2025 and nothing since -- GPS reception,
not a failing crystal.

## HP Z3805A, serial 3625A01487

    HEWLETT-PACKARD,Z3805A,3625A01487,3543B-A

Oscillator serial 4505A05096.  Main board Z3805-68001, no revision
marked, serial 6E9008G, date code 3627.  Its four flash parts are
labeled Z3805-80001 to -80004, Rev C, at U11 (2L), U12 (1L), U13 (2M)
and U14 (1M).  Its jacks
carry the same designators as the 58503A's: J4 (OUT A) and J5 (OUT B)
go to the two rear 1 PPS outputs, J17 (10 MHz A) and J15 (10 MHz B) to
the two rear 10 MHz outputs, and J6, J7 and J8 are not fitted.

Its BTS power supply had three RIFA PME271 Y 10 nF paper capacitors
(marked 40/100/56/C) on its DC input, a type known to crack and fail
with age.  All three were replaced on 2026-10-11 with Vishay
VY2472M49Y5US6TV7, 4.7 nF X1/Y2 ceramic.
The supply is Nortel's BTS supply A0696033, for 19.5 to 30 V DC in,
built on an HP board, 58503-60002 Rev D, serial 5DC01CC, date code
3513, carrying a Datel UWR-5/4000-D48 DC/DC converter and an AT&T
DC-DC power module.

![The main board in place, the GPS engine module at the center](images/Z3805-68001/z3805a-main-board.jpg)

![The MC68331, the XC3042 FPGA and the four flash parts, U11 to U14](images/Z3805-68001/z3805a-cpu-fpga-flash.jpg)

![The board's serial, 6E9008G, and date code, 3627](images/Z3805-68001/z3805a-board-labels.jpg)

![J17 (10 MHz A) and J15 (10 MHz B), both cabled](images/Z3805-68001/z3805a-10mhz-outputs.jpg)

![The GPS engine module, top](images/Z3805-68001/z3805a-gps-engine-top.jpg)

![The GPS engine module, bottom](images/Z3805-68001/z3805a-gps-engine-bottom.jpg)

![The GPS engine module, top, its shield off](images/Z3805-68001/z3805a-gps-engine-top-unshielded.jpg)

![The GPS engine module, bottom, its shield off](images/Z3805-68001/z3805a-gps-engine-bottom-unshielded.jpg)

![The three RIFA PME271 Y capacitors removed from the power supply](images/Z3805-68001/z3805a-rifa-pme271-front.jpg)

![The same, reverse](images/Z3805-68001/z3805a-rifa-pme271-back.jpg)

![The power supply, Nortel A0696033 on HP board 58503-60002 Rev D, with the RIFA capacitors replaced](images/Z3805-68001/z3805a-power-supply-top.jpg)

![The power supply, reverse: its part number and labels](images/Z3805-68001/z3805a-power-supply-bottom.jpg)

Found at 9600 8N1, moved to 19200 to match the other unit.  No
front-panel display -- the Z3801A and Z3805A never had one -- so the
serial status screen is the only status output.

Takes the Z3801A command tree, selected by the `Z38` prefix.  Every
read-only command in that tree answered; the sixteen entries added to
it in September 2026 were confirmed here.

GPS receiver, as `:DIAGnostic:IDENtification:GPSystem?` reports it:

    COPYRIGHT 1991-1995 MOTOROLA INC.
    SFTW P/N # 98-P39972M
    SOFTWARE VER # 8   REV # 4   DATE 13 JUL 1995
    MODEL #    B1121P1114
    SERIAL #   SSG0163878
    MANUFACTUR DATE 6G09
    OPTIONS LIST    IB

A different engine from the 58503A's B4121P1115: a six-channel B1
(`bench-sky-2026-10-03.html`, from the VP Oncore Command Reference)
with software a year older (`firmware/gps.md`, "The engines on the
bench").  Both report the same 1024-week offset and, on the same day,
the same date.  The engine is a module on the main board: a Motorola
MC68331 with HM628128 RAM, two TN28F010 flash parts, an AT28C64B
EEPROM, an MC68HC68T1 clock and an XCT38140FD04, its RF section under
a shield marked 326/0816 2643686MAB.  Under the shields: on one side
a part marked 24M01 19.09 M 9601; on the other a part marked 58M01
9623 and a can marked 95001 624.

Its diagnostic log held 225 entries, not the 222 the 58503A stops at.
Copied out and cleared.

Position asserted by hand rather than surveyed, and survey-on-powerup
turned off so it survives a power cycle.  **Both settings are
non-volatile.**  If that antenna moves, this receiver will not notice
and will serve time from a wrong position.

## The Z3805A's receive path is degraded

It knows where every satellite is and cannot hear them.  Its predicted
elevations and azimuths match what the 58503A tracks, PRN for PRN, on
the same antenna minutes apart:

    PRN     Z3805A predicted      58503A tracking, with signal
              1     22  314         23  314    74
              2     45  310         46  310   152
              8     29  256         29  254    79
             10     47   60         46   61   137
             23     18   81         17   83    62
             27     23  214         22  214   105
             28     32  148         33  148   103
             32     78   35         76   35   250

So the almanac is sound and it points correctly.  On the same cable
and adapter the 58503A reads 62 to 250; the Z3805A never exceeded 29
in a day of watching, on either of two antennas.  Even if the two
engines scale signal differently, it hears roughly an order of
magnitude less.

Everything else is eliminated, each by measurement against the 58503A
on the same cables: both antennas, the distribution amp
drop, the cable, the adapter, the antenna bias (4.8 V at the N
connector, "a little less than +5 Volts" as `097-z3801-01` 2-12 says
it should read), the supply (28 V at 3 A, no rail fault bit in ten
hours), the asserted position (reads back correct, agrees with the
58503A's survey to within a meter), the elevation mask (10 degrees,
nothing ignored, all 32 included), and the health monitor (six of six
OK throughout).

It is not dead: over about five hours it acquired enough to download
an almanac, validate its time and reach `Locked` with TFOM 3.  An
almanac download needs sustained data lock, which a deaf receiver
cannot manage.  It is degraded, not broken, matching a time-nuts report
of another Z3805A whose Oncore lost sensitivity over time.

Later results (`hardware-investigations.md`, item 11):

- 2026-09-28, overnight after `master_reset`, a power cycle and 20 dB
  of low-noise gain ahead of it: lock about 98% of nine hours on 2 to
  4 satellites.
- 2026-10-03, 24 hours on one splitter beside the 58503A and a u-blox
  NEO-M8T, with that gain ahead of it alone: 2.9 satellites on average, none held below 39 dB-Hz where
  the 58503A held 88% at 36 to 39
  ([bench-sky-2026-10-03.html](https://htmlpreview.github.io/?https://github.com/charlieh0tel/smartclockmon/blob/main/docs/bench-sky-2026-10-03.html)).
- The 24 hours to 21:00 UTC on 2026-10-05, with the 20 dB LNA still in
  place: lock 88.3% of the time, on about 2.7 satellites on average
  while locked.

In position hold at the 58503A's surveyed antenna position, which it
read back on 2026-09-28, with survey-on-powerup off (read 0 on
2026-10-05), so a power cycle keeps the position.
