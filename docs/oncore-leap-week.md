# The Time Reset at the end of GPS week 2441

Just after GPS week 2441 ended, on 2026-10-25, the bench 58503A latched
Time Reset; its time of day ran 1 s slow for about eight minutes.  This
fits a known class of 256-week leap-parameter bugs in GPS engines,
mechanism unknown.  Time Reset and clearing it:
[`running.md`](running.md#time-reset-at-a-gps-week-boundary).  The
check and its addresses:
[`firmware/58503a-3704.md`](firmware/58503a-3704.md#time-reset-and-the-oncores-time).

Unit: firmware 3704-C; Motorola Oncore B4121P1115, an eight-channel
Oncore VP (Synergy's model table, `third_party/motorola/VPCommands.pdf`
p. 2), software 98-P36830P, version 8, revision 8, 1996-08-06.

## Observed

The daemon logged receiver `time_utc` and host receive time `fast_at`
each second, 2026-10-24 23:00 to 10-25 01:00 UTC.  The undisciplined
host clock ran 11.84 s slow; times here are corrected, to about 1 s.
The receiver read a steady 12.1 s ahead of the host until the event.
No raw Oncore traffic (`@@Ea` time stamps) was logged, and no outputs
were measured.

- 00:00:15 read twice; later 00:08:16 was skipped.  These are the only
  steps in two hours.  Paired 1 s dips about every 5 minutes (receiver time
  23:04:51, 23:09:54, ..., 00:02:33, 00:12:40) are poll timing.
- About 00:00:25 UTC, at the next medium poll, `*STB?` read 72:
  questionable summary, bit 3 (8), plus master summary, bit 6 (64)
  (`097-59551-02` 5-44).  `:STATus:QUEStionable:EVENt?` then returned
  `+1`, Time Reset (5-40).
- Lock, mode `Locked`, operation bits 27, TI, EFC, TFOM, satellites
  and the receiver's diagnostic log were unchanged.
- The 2026-10-04, 10-11 and 10-18 week boundaries passed cleanly.

Host time uncorrected:

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

At second 14, with its reference good, not questionable, and no leap
acknowledgment pending, the 58503A compares its clock with the
Oncore's `@@Ea` time, ignoring an Oncore time that repeats the
reference time.  After a mismatch it compares every second; a second
in a row sets the clock from the Oncore and raises Time Reset.  So,
when the guards allow, the reset comes at :15
([`firmware/58503a-3704.md`](firmware/58503a-3704.md#time-reset-and-the-oncores-time),
from `clock_check_against_gps_58503a` and its callers in the decompiled
C).  Both steps did.

If each reset followed a sustained error in the time stamps the
58503A received (not necessarily the engine's clock), each change lies
in the minute before its step.  The windows are inferred from the
reset timing, not observed:

| Oncore time | Window (UTC) | Notes |
| ----------- | ------------ | ----- |
| fell 1 s behind | 23:59:15 to 00:00:15 | holds UTC midnight and the rollover, 2026-10-24 23:59:42 UTC (GPS 00:00:00, GPS-UTC 18 s), which the log cannot separate |
| came right | 00:07:15 to 00:08:15 | before the next UTC parameters |

So the error lasted 7 to 9 minutes, not exactly 8; whole-second
readings and poll timing add about 1 s.  The check shows when a mismatch was corrected,
not why it arose.  A steady TI, the 1 PPS phase against GPS, cannot
show a mislabeled second: the edge stayed put.

Recovery preceded the next scheduled UTC parameters, subframe 4 page
18.  A frame is five 6 s subframes, 30 s; subframes 4 and 5 cycle 25
pages in 12.5 minutes, restarting at page 1 at the week boundary
(IS-GPS-200N 20.3.2, 20.3.4.1).  Page 18 is in frame 18, time of week
528 to 534 s, 00:08:30 to 00:08:36 UTC.  The previous one, at time of
week 805 x 750 + 528 = 604278 s of week 2441, 23:51:00 to 23:51:06 UTC,
came 522 s before the rollover, 17.5 minutes earlier.

## Why week 2441

The 8-bit leap-second week WN_LSF is the GPS week modulo 256
(IS-GPS-200N 20.3.3.5.2.4).  The 2016-12-31 leap second set it to 137:

| GPS week | mod 256 | Dates (GPS) | |
| -------- | ------- | ----------- | - |
| 1929 | 137 | 2016-12-25 to 12-31 | the last leap second |
| 2185 | 137 | 2021-11-21 to 11-27 | the previous match |
| 2441 | 137 | 2026-10-18 to 10-24 | this event |
| 2697 | 137 | 2031-09-14 to 09-20 | the next match |

A 2021 capture shows `wnLSF 137 dn 7 ... dtLS 18 dtLSF 18`
([Bert Hubert, 2021](https://berthub.eu/articles/posts/leapseconds-expose-bugs-even-when-they-dont-happen/));
none exists from the event day.  If unchanged, they alias week 2441,
and DN 7 puts a leap at the end of Saturday, where this one fell.  A
correct decoder inserts nothing: with delta t_LS equal to delta t_LSF,
"the determination of an effectivity time of t_LSF, as indicated by the
WN_LSF and the DN, is not necessary"
([IS-GPS-200N](https://archive.gps.gov/technical/icwg/IS-GPS-200N.pdf)
20.3.3.5.2.4, and case a(1)).  A false leap needs mishandled equal
offsets or other corrupt state, such as a cached UTC offset.

## Precedents

- **2003-11-27.**  256 weeks after the 1998-12-31 leap second, none
  pending, an Oncore VP sent one bad `@@Ba` date and time at the day's
  86401st second; an HP 59551A beside it was unaffected
  ([Tom Van Baak, leapsecond.com](http://www.leapsecond.com/notes/leapsec256.htm);
  BIPM appendix
  [figure 33](https://www.bipm.org/documents/d/guest/towards-continuous-utc-appendix-only-news-from-the-press-nov-2022);
  time-nuts,
  [2019](https://febo.com/pipermail/time-nuts_lists.febo.com/2019-June/096730.html)).
  The same family in Oncore firmware, but one message, not minutes.
- **2021-11-28, the previous match.**  Many NTP servers applied an
  unscheduled leap; most recovered within a second, but eight stayed
  1 s off for 2 to 8 minutes, one for 39 min 26 s.  At least 42 advertising it were LeoNTP units, whose
  u-blox engine reported week 2185 as 1929; the source does not name
  the prolonged ones' models
  ([Steven Sommars, weberblog](https://weberblog.net/partial-ntp-pool-the-leap-second-that-wasnt/)).
  LeoNTP firmware 1.24 "Fixed erroneous indication of scheduled leap
  second" ([LeoNTP](https://www.leontp.com/firmware/)).
- **2026-10-25 predicted.**  In November 2025 LeoNTP 1200 units, 512
  weeks after 1929, announced a leap for 2026-10-25 00:00:00
  ([NTP Pool forum](https://community.ntppool.org/t/leap-second-in-october-2026/4132);
  [time-nuts, Steven Sommars](https://febo.com/pipermail/time-nuts_lists.febo.com/2025-November/110275.html));
  LeoNTP firmware 2.07 fixed its server
  ([time-nuts, Tom Van Baak](https://febo.com/pipermail/time-nuts_lists.febo.com/2025-November/110284.html)).

## Alternatives

- The Oncore's cached UTC offset or week transition failed, without
  leap logic.
- A race in the 58503A's status message or in `gps_leap_ack`, which
  switches the Oncore between GPS and UTC to measure GPS-UTC, and which
  a check requests just after setting the clock.

The 58503A's own leap logic is excluded: it decodes no leap fields,
acts only on the Oncore's pending flag `@@Bj`, and books leaps for a
quarter's end
([`firmware/58503a-3704.md`](firmware/58503a-3704.md#time-reset-and-the-oncores-time)).
Emulated with this unit's EEPROM and a synthetic Oncore (`@@Bj` 0, a
correct 2026 date, no WN_LSF model) from 2026-10-24 23:35 UTC, it held
`*STB?` at 0 through 00:25.  That narrow control says nothing about
the races or the Oncore.

Raw `@@Ea` time stamps, arrival times, the GPS and UTC mode exchanges
and the host clock's log would separate these.  Per-second reads
against a disciplined host clock would narrow the 58503A's steps; only
the Oncore's serial line shows its own transition.

## What to do

No clock correction is needed.  `*CLS` clears the event and releases
the Alarm LED and Alarm BITE output (`097-59551-02` 5-40 to 5-41).
Reading `:STATus:QUEStionable:EVENt?` also clears it (5-46); the alarm
presumably follows, but the manual does not say
([`running.md`](running.md#time-reset-at-a-gps-week-boundary)).

If the fields stay unchanged, week 2697 matches again, ending Saturday
2031-09-20 (00:00 UTC on Sunday 2031-09-21).  The receiver's time of
day, not its 1 PPS, may then run 1 s off for minutes.  Capturing the
Oncore's line across that boundary, or any later logged one, could
settle the mechanism.
