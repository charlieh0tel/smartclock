# The Time Reset at the end of GPS week 2441

On 2026-10-25, just after GPS week 2441 ended, the bench 58503A
(firmware 3704-C; Motorola Oncore, model B4121P1115, software
98-P36830P, version 8, revision 8, dated 1996-08-06) latched Time
Reset.  Model B4 is an eight-channel Oncore VP (Synergy's model
table, `third_party/motorola/VPCommands.pdf` p. 2).  The receiver's
time of day ran 1 s slow for about eight minutes.  Under the
Oncore-error interpretation, the engine's error lasted roughly seven
to nine minutes.  This fits a known class of 256-week leap-parameter
bugs in GPS engines; the exact mechanism is unknown.
[`running.md`](running.md#time-reset-at-a-gps-week-boundary) says what
Time Reset is and how to clear it;
[`firmware/58503a-3704.md`](firmware/58503a-3704.md#time-reset-and-the-oncores-time)
gives the firmware's check and its addresses.

## What was observed

From 2026-10-24 23:00 to 10-25 01:00 UTC, the daemon logged receiver
`time_utc` and host receive time `fast_at` once per second.  The host
clock ran 11.84 s slow, undisciplined; times below are corrected, to
about 1 s.  The receiver read a steady 12.1 s ahead of it until the
event.

- Two polls 1 s apart both read 00:00:15.  Later the receiver skipped
  00:08:16, reading 00:08:15 and then 00:08:17.  These are the only two
  steps in the two hours.  Paired 1 s dips about every 5 minutes
  (receiver time 23:04:51, 23:09:54, ..., 00:02:33, 00:12:40) are poll
  timing: one sample late, then back.
- At the first medium poll after the first step, about 00:00:25 UTC,
  `*STB?` read status byte 72: bit 3, questionable summary (8), plus
  bit 6, master summary (64) (`097-59551-02` 5-44).
  `:STATus:QUEStionable:EVENt?`, read afterward, returned `+1`, Time
  Reset (5-40).
- Lock, mode `Locked`, operation bits 27, TI, EFC, TFOM and the
  satellites stayed unchanged throughout.  The receiver's diagnostic log
  gained nothing.
- The 2026-10-04, 10-11 and 10-18 week boundaries passed cleanly.

The log's rows around both steps, host time uncorrected:

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

The log holds no raw Oncore traffic: no `@@Ea` time stamps.

## What the 58503A's check bounds

When its clock's second is 14, the 58503A compares its clock with the
Oncore's `@@Ea` time, provided its reference is good and not
questionable and no leap acknowledgment is pending; it counts no
mismatch when the Oncore's time repeats the reference time.  A mismatch
sets a flag; while the flag is set, the 58503A compares every second,
and a second mismatch in a row sets the clock from the Oncore's time and
raises Time Reset.  With those guards satisfied on consecutive seconds,
a mismatch detected at :14 resets the clock at :15
([`firmware/58503a-3704.md`](firmware/58503a-3704.md#time-reset-and-the-oncores-time),
from `clock_check_against_gps_58503a` and its callers in the decompiled
C).  Both steps land there.

If the resets followed a sustained error in the Oncore's UTC output,
the log bounds each change to the minute before its step.  The bounds
concern the time stamps the 58503A received and used, not necessarily
the engine's internal clock:

- They fell 1 s behind between about 23:59:15 and 00:00:15 UTC.  That
  window holds both UTC midnight and the GPS week rollover, 2026-10-24
  23:59:42 UTC (GPS 00:00:00, with GPS-UTC 18 s); this log cannot tell
  a phantom leap at UTC midnight from a fault at the rollover.
- They came right between about 00:07:15 and 00:08:15 UTC.
- So the error lasted about 7 to 9 minutes, not exactly 8.

The check explains when a persisting mismatch was corrected, not why
one arose.  A steady TI, the 1 PPS phase against GPS, cannot
distinguish integer-second labeling errors: whatever mislabeled the
second left the edge in place.

## Why week 2441

The leap-second week WN_LSF is 8 bits, the GPS week modulo 256
(IS-GPS-200N 20.3.3.5.2.4).  It became 137 with the 2016-12-31 leap
second, so the current week matches it every 256 weeks:

| GPS week | mod 256 | Dates (GPS) | |
| -------- | ------- | ----------- | - |
| 1929 | 137 | 2016-12-25 to 12-31 | the last leap second |
| 2185 | 137 | 2021-11-21 to 11-27 | the previous match |
| 2441 | 137 | 2026-10-18 to 10-24 | this event |
| 2697 | 137 | 2031-09-14 to 09-20 | the next match |

A 2021 capture shows WN_LSF=137, DN=7 and both offsets=18:
`wnLSF 137 dn 7 ... dtLS 18 dtLSF 18`
([Bert Hubert, 2021](https://berthub.eu/articles/posts/leapseconds-expose-bugs-even-when-they-dont-happen/)).
If unchanged, those fields alias week 2441.  DN 7 is the week's last
day, so an engine acting on the match would insert a leap at the end of
Saturday, where this one fell.  No capture exists from the event day.

## What a match alone does not explain

A correct decoder inserts nothing here.  When delta t_LS equals delta
t_LSF, "the determination of an effectivity time of t_LSF, as indicated
by the WN_LSF and the DN, is not necessary"
([IS-GPS-200N](https://archive.gps.gov/technical/icwg/IS-GPS-200N.pdf)
20.3.3.5.2.4, and case a(1)).  The match alone causes no leap.  A false
insertion would require faulty handling of the equal offsets or other
corrupted state, such as a cached UTC offset.

## Not the next UTC parameters

Recovery preceded the next scheduled UTC-parameter transmission,
subframe 4 page 18.  A frame is five 6 s subframes, 30 s; subframes 4
and 5 cycle through 25 pages in 12.5 minutes, and paging restarts with
page 1 at the end/start of week (IS-GPS-200N 20.3.2, 20.3.4.1).  Page
18's subframe 4 is in frame 18, at time of week 528 to 534 s, 00:08:30
to 00:08:36 UTC.  The previous one, at time of week 805 x 750 + 528 =
604278 s of week 2441, came at 23:51:00 to 23:51:06 UTC, 522 s before
the rollover: the restart leaves a 17.5-minute gap between them.
Neither falls in the 00:07:15 to 00:08:15 window in which the Oncore
came right.

## Precedents

- **The Oncore VP, 2003-11-27.**  Exactly 256 weeks after the
  1998-12-31 leap second, an Oncore VP sent one `@@Ba` with a wrong
  date and time at the 86401st second of the day, and was correct again
  the next second; an HP 59551A beside it was unaffected
  ([Tom Van Baak, leapsecond.com](http://www.leapsecond.com/notes/leapsec256.htm),
  the BIPM's appendix
  [figure 33](https://www.bipm.org/documents/d/guest/towards-continuous-utc-appendix-only-news-from-the-press-nov-2022);
  Van Baak recalls it on time-nuts,
  [2019](https://febo.com/pipermail/time-nuts_lists.febo.com/2019-June/096730.html)).
  No leap was pending then, so it too acted on a stale match.  It
  establishes the bug family in Oncore firmware, not this symptom: one
  bad message, not minutes 1 s off.
- **2021-11-28, the previous match.**  Many NTP servers applied an
  unscheduled leap second; most recovered within a second, but eight
  stayed 1 s off for 2 to 8 minutes, and one for 39 min 26 s.  Of the
  servers advertising the false warning, at least 42 were LeoNTP units,
  whose u-blox engine reported week 2185 for 1929; the source does not
  say which models the prolonged errors were
  ([Steven Sommars, weberblog](https://weberblog.net/partial-ntp-pool-the-leap-second-that-wasnt/)).
  LeoNTP's firmware 1.24 "Fixed erroneous indication of scheduled leap
  second" ([LeoNTP](https://www.leontp.com/firmware/)).
- **2026-10-25 predicted.**  In November 2025 LeoNTP 1200 units
  announced a leap second for 2026-10-25 00:00:00, 512 weeks after
  1929 ([NTP Pool forum](https://community.ntppool.org/t/leap-second-in-october-2026/4132);
  [time-nuts, Steven Sommars](https://febo.com/pipermail/time-nuts_lists.febo.com/2025-November/110275.html));
  the fault lay in LeoNTP's own server, and its firmware 2.07 fixed it
  ([time-nuts, Tom Van Baak](https://febo.com/pipermail/time-nuts_lists.febo.com/2025-November/110284.html)).

## Alternatives

- The Oncore's cached UTC offset, or its handling of the week
  transition, went wrong without any leap logic.
- A race in the 58503A: in its status message, or in the exchange that
  switches the Oncore between GPS and UTC time to measure GPS-UTC
  (`gps_leap_ack`), which a check requests just after setting the
  clock.
- Whole-second readings and poll timing add about 1 s of uncertainty to
  the duration.

Raw `@@Ea` time stamps, message arrival times, a capture of the GPS and
UTC mode exchanges and the host clock's log would tell these apart.
Reading the receiver's time every second against a disciplined host
clock would narrow the 58503A's step times; only a capture of the
Oncore's serial line can locate the Oncore's own transition.

## What it is not

The 58503A's firmware decodes none of the leap fields; it acts only on
the Oncore's pending flag `@@Bj`, and books a leap for a quarter's end
([`firmware/58503a-3704.md`](firmware/58503a-3704.md#time-reset-and-the-oncores-time)).
As a narrow negative control, the firmware, run in an emulator with
this unit's EEPROM and a synthetic Oncore (`@@Bj` 0, a correct 2026
date, no WN_LSF model) from 2026-10-24 23:35 UTC, held the status
byte (`*STB?`) at 0 through 00:25: the 58503A's firmware alone does
not step.  This excludes a leap the 58503A scheduled itself, not the
races above, and says nothing about the Oncore.

## What to do

The latched event requires no clock correction.  `*CLS` clears it and
releases the Alarm LED and Alarm BITE output (`097-59551-02` 5-40 to
5-41).  Reading `:STATus:QUEStionable:EVENt?` also clears the event
(5-46); the alarm presumably follows, but the manual does not say so
([`running.md`](running.md#time-reset-at-a-gps-week-boundary)).

If the broadcast fields stay unchanged, with WN_LSF still 137 and no
leap second announced, week 2697 matches again, ending Saturday
2031-09-20 (00:00 UTC on Sunday 2031-09-21).  Anything using the
receiver's time of day, rather than its 1 PPS edge, may then run 1 s
off for some minutes.  A capture of the Oncore's line across that
boundary, or the next one at which a unit is logged, could distinguish
the proposed mechanisms.
