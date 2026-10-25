# The Time Reset at the end of GPS week 2441

Just after GPS week 2441 ended, on 2026-10-25, the bench 58503A latched
Time Reset; its time of day ran 1 s slow for about eight minutes.  The
trigger matches the Oncore's known 256-week leap-week alias at UTC
midnight, but the eight minutes need more than that alias's documented
one-second anomaly.
[`running.md`](running.md#time-reset-at-a-gps-week-boundary) explains
Time Reset and how to clear it;
[`firmware/58503a-3704.md`](firmware/58503a-3704.md#time-reset-and-the-oncores-time)
gives the firmware's check and its addresses.

The unit has firmware 3704-C and a Motorola Oncore B4121P1115 engine, an
eight-channel Oncore VP by Synergy's model table
(`third_party/motorola/VPCommands.pdf` p. 2), with software 98-P36830P,
version 8, revision 8, 1996-08-06.

## Observed

From 2026-10-24 23:00 to 10-25 01:00 UTC, the daemon logged receiver
`time_utc` and host receive time `fast_at` each second.  The
undisciplined host clock ran 11.84 s slow; the times here are corrected,
to about 1 s.  Until the event, the receiver read a steady 12.1 s ahead
of the host.  The log holds no raw Oncore traffic (`@@Ea` time stamps),
and the receiver's outputs were not measured.

- The receiver read 00:00:15 twice and later skipped 00:08:16.  These
  were the only steps in two hours.  Paired 1 s dips about every 5
  minutes (receiver time 23:04:51, 23:09:54, ..., 00:02:33, 00:12:40)
  are poll timing.
- At the next medium poll, about 00:00:25 UTC, `*STB?` read 72: the
  questionable summary, bit 3 (8), plus the master summary, bit 6 (64)
  (`097-59551-02` 5-44).  `:STATus:QUEStionable:EVENt?` then returned
  `+1`, Time Reset (5-40).
- Lock, mode `Locked`, operation bits 27, TI, EFC, TFOM, the
  satellites and the receiver's diagnostic log did not change.
- The 2026-10-04, 10-11 and 10-18 week boundaries passed cleanly.

The log around both steps, host time uncorrected:

| Host `fast_at` | Receiver `time_utc` | |
| -------------- | ------------------- | - |
| 00:00:01.928 | 00:00:14 | |
| 00:00:02.917 | 00:00:15 | |
| 00:00:03.917 | 00:00:15 | repeated |
| 00:00:04.917 | 00:00:16 | |
| 00:00:12.554 | | `receiver_event`: alarm, 72 |
| 00:08:02.905 | 00:08:14 | |
| 00:08:03.905 | 00:08:15 | |
| 00:08:04.905 | 00:08:17 | 00:08:16 skipped |
| 00:08:05.905 | 00:08:18 | |

## What the check bounds

At second 14, if its reference is good and not questionable and no
leap acknowledgment is pending, the 58503A compares its clock with the
Oncore's `@@Ea` time.  It ignores an Oncore time that repeats the
reference time.  After a mismatch, it compares
every second.  A second mismatch in a row sets the clock from the
Oncore's time and raises Time Reset.  When the guards allow, the
reset comes at :15
([`firmware/58503a-3704.md`](firmware/58503a-3704.md#time-reset-and-the-oncores-time),
from `clock_check_against_gps_58503a` and its callers in the decompiled
C).  Both steps came there.

Suppose each reset followed a sustained error in the time stamps the
58503A received, not necessarily in the engine's own clock.  Then each
change lies in the minute before its step.  These windows are inferred
from the reset timing; they were not observed:

| Oncore time | Window (UTC) | Notes |
| ----------- | ------------ | ----- |
| fell 1 s behind | 23:59:15 to 00:00:15 | holds UTC midnight and the week rollover, 2026-10-24 23:59:42 UTC (GPS 00:00:00, GPS-UTC 18 s); the log cannot separate them |
| came right | 00:07:15 to 00:08:15 | before the next UTC parameters |

The error lasted 7 to 9 minutes, not exactly 8; whole-second readings
and poll timing add about 1 s.  The check shows when a mismatch was
corrected, not why it arose.  A steady TI cannot distinguish time
labels a whole second apart.

Recovery preceded the next scheduled UTC parameters (subframe 4 page
18) at 00:08:30 (IS-GPS-200N 20.3.2, 20.3.4.1).

## Why week 2441

The leap-second week WN_LSF has 8 bits: it is the GPS week modulo 256
(IS-GPS-200N 20.3.3.5.2.4).  The 2016-12-31 leap second set it to 137:

| GPS week | mod 256 | Dates (GPS) | |
| -------- | ------- | ----------- | - |
| 1929 | 137 | 2016-12-25 to 12-31 | the last leap second |
| 2185 | 137 | 2021-11-21 to 11-27 | the previous match |
| 2441 | 137 | 2026-10-18 to 10-24 | this event |
| 2697 | 137 | 2031-09-14 to 09-20 | the next match |

A 2021 capture shows `wnLSF 137 dn 7 ... dtLS 18 dtLSF 18`
([Bert Hubert, 2021](https://berthub.eu/articles/posts/leapseconds-expose-bugs-even-when-they-dont-happen/));
no capture exists from the event day.  If unchanged, those fields
alias week 2441.  DN 7 is the week's last day, so a leap would
come at the end of Saturday, where this one fell.  A correct decoder
inserts nothing.  When delta t_LS equals delta t_LSF, "the
determination of an effectivity time of t_LSF, as indicated by the
WN_LSF and the DN, is not necessary"
([IS-GPS-200N](https://archive.gps.gov/technical/icwg/IS-GPS-200N.pdf)
20.3.3.5.2.4, and case a(1)).  A false leap needs mishandled
equal offsets or other corrupt state, such as a cached UTC offset.

If the fields stay unchanged, week 2697 matches again, ending
Saturday 2031-09-20 (00:00 UTC on Sunday 2031-09-21).  Anything using
the receiver's time of day, rather than its 1 PPS edge, may then run
1 s off for minutes.  Capturing the Oncore's line across that
boundary, or any later logged one, could settle the mechanism.

## Precedents

- **2003-11-27.**  256 weeks after the 1998-12-31 leap second, an
  Oncore VP sent one garbled `@@Ba` (11/29/2003 62:28:15) at UTC
  midnight, then correct ones.  Motorola's July 2003 application note,
  on the same page, blames the 8-bit leap week: VP, UT, GT and M12 Oncores, not the
  M12+, report the date a day ahead for one second, with the time and
  1PPS unaffected
  ([Tom Van Baak, leapsecond.com](http://www.leapsecond.com/notes/leapsec256.htm);
  BIPM appendix
  [figure 33](https://www.bipm.org/documents/d/guest/towards-continuous-utc-appendix-only-news-from-the-press-nov-2022);
  time-nuts,
  [2019](https://febo.com/pipermail/time-nuts_lists.febo.com/2019-June/096730.html)).
- **2021-11-28, the previous match.**  NTP servers applied an
  unscheduled leap, and some stayed 1 s off for minutes.  Many were
  LeoNTP units on u-blox engines
  ([Steven Sommars, weberblog](https://weberblog.net/partial-ntp-pool-the-leap-second-that-wasnt/);
  [LeoNTP firmware 1.24](https://www.leontp.com/firmware/)).

## Alternatives

A one-second anomaly like 2003's cannot cause this through the 58503A's
firmware, by a trace of its decompiled C.  Each `@@Ea` overwrites
`gps.status` unchecked, and midnight rollover (`clock_tick`,
`0x137f6`) never reads it.  The check installs the Oncore's time as
received, through `clock_set` (`0x10b00`), whose setter rejects
out-of-range fields.  No timer delays a correction; only the check's
guards could.  So the Oncore's UTC labels stayed 1 s slow for minutes,
or something else failed:

- The Oncore's cached UTC offset, or its handling of the week
  transition, failed without any leap logic.
- A race in the 58503A's status message or in `gps_leap_ack`.  That
  exchange switches the Oncore between GPS and UTC to measure GPS-UTC,
  and a check requests it just after setting the clock.  Its offset
  cannot step the UTC clock, but while pending it blocks the check.

The 58503A's own leap scheduling cannot explain this event.  It decodes
no leap fields, acts only on the Oncore's pending flag `@@Bj`, and
books leaps for a quarter's end
([`firmware/58503a-3704.md`](firmware/58503a-3704.md#time-reset-and-the-oncores-time)).
An emulator ran it with this unit's EEPROM and a synthetic Oncore
(`@@Bj` 0, a correct 2026 date, no WN_LSF model) from 2026-10-24 23:35
UTC, and `*STB?` stayed 0 through 00:25.  This narrow control says
nothing about the races or the Oncore.

A 1PPS-timestamped capture of the Oncore's line, both directions, across
both steps would decide.  Per-second reads against a disciplined host
clock would narrow the 58503A's step times.
