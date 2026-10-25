# The Time Reset at the end of GPS week 2441

On 2026-10-25 the bench 58503A (firmware 3704) latched Time Reset just
after GPS week 2441 ended, and its time of day ran 1 s slow for 7 to 9
minutes.  This is consistent with a known class of 256-week
leap-parameter bugs in GPS engines; the exact mechanism is unknown.
What Time Reset is and how to clear it are in
[`running.md`](running.md#time-reset-at-a-gps-week-boundary); the
firmware's check, with its addresses, is in
[`firmware/58503a-3704.md`](firmware/58503a-3704.md#time-reset-and-the-oncores-time).

## What was observed

From the daemon's snapshot log: once-a-second `time_utc` against the
host's receive time (`fast_at`), 2026-10-24 23:00 to 2026-10-25 01:00
UTC.  The host clock ran about 12 s slow, steadily; times below are
corrected to UTC, to about 1 s.

- The receiver's time of day repeated 00:00:15: two polls 1 s apart
  both read it.  It later skipped 00:08:16, reading 00:08:15 and then
  00:08:17.  These are the only two steps in the two hours.  Paired
  1 s dips about every 5 minutes (receiver time 23:04:51, 23:09:54, ...,
  00:02:33, 00:12:40) are poll timing: one sample late, then back.
- `*STB?` read 72, the questionable summary (8) and the master summary
  (64) (`097-59551-02` 5-44), at the first medium poll after the first
  step, about 00:00:25 UTC.  `:STATus:QUEStionable:EVENt?` read
  afterward returned `+1`, Time Reset (5-40).
- Lock, mode `Locked` and operation bits 27 were unchanged throughout,
  as were TI, EFC, TFOM and the satellites.  The receiver's diagnostic
  log gained nothing.
- The 2026-10-04, 10-11 and 10-18 week boundaries passed cleanly.

The log holds no raw Oncore traffic: no `@@Ea` time stamps.

## What the 58503A's check bounds

The 58503A compares its clock with the Oncore's `@@Ea` time when its
clock's second is 14.  A mismatch sets a flag, and while the flag is
set it compares every second; a second mismatch in a row sets the
clock from the Oncore's time and raises Time Reset.  A mismatch that
persists is therefore corrected at hh:mm:15 and at no other time
([`firmware/58503a-3704.md`](firmware/58503a-3704.md#time-reset-and-the-oncores-time),
from `clock_check_against_gps_58503a` and its callers in the decompiled
C).  Both steps land there, so the log bounds the Oncore's own changes
only to the minute before each:

- It went 1 s behind between about 23:59:15 and 00:00:15 UTC.  That
  window holds both the GPS week rollover, 2026-10-24 23:59:42 UTC (GPS
  00:00:00, with GPS-UTC 18 s), and UTC midnight.  A phantom leap at
  UTC midnight and a fault at the week rollover cannot be told apart
  from this log.
- It came right between about 00:07:15 and 00:08:15 UTC.
- So its error lasted between about 7 and 9 minutes, not exactly 8.

The check explains when any persisting mismatch was corrected, not why
there was one.  An unchanged 1 PPS phase cannot tell integer-second
labeling errors apart: whatever labeled the second wrong left the edge
where it was.

## Why week 2441

The leap-second week WN_LSF is 8 bits, the GPS week modulo 256
(IS-GPS-200N 20.3.3.5.2.4).  Since the 2016-12-31 leap second it has
been 137, so every 256 weeks the current week matches it again:

| GPS week | mod 256 | Dates (GPS) | |
| -------- | ------- | ----------- | - |
| 1929 | 137 | 2016-12-25 to 12-31 | the last leap second |
| 2185 | 137 | 2021-11-21 to 11-27 | the previous match |
| 2441 | 137 | 2026-10-18 to 10-24 | this event |
| 2697 | 137 | 2031-09-14 to 09-20 | the next match |

A broadcast captured in 2021 shows the fields:
`wnLSF 137 dn 7 ... dtLS 18 dtLSF 18`
([Bert Hubert, 2021](https://berthub.eu/articles/posts/leapseconds-expose-bugs-even-when-they-dont-happen/)).
DN 7 is the last day of the week, so an engine acting on the match
would put a leap at the end of Saturday, which is where this one fell.
There is no capture from the event day.  The receiver's 1024-week
wrapped week, 1417, is also 137 modulo 256, but adds no evidence:
subtracting 1024 leaves the value modulo 256 unchanged.  Nor was week
1673 (2012) a test: WN_LSF was not 137 until the 2016 leap was
announced.

## What a match alone does not explain

A correct decoder inserts nothing here.  With delta t_LS equal to
delta t_LSF, "the determination of an effectivity time of t_LSF, as
indicated by the WN_LSF and the DN, is not necessary"
([IS-GPS-200N](https://archive.gps.gov/technical/icwg/IS-GPS-200N.pdf)
20.3.3.5.2.4, and case a(1)).  So the theory needs a second fault in the
engine: ignoring that equality, or corrupting its cached UTC offset.

## Not the next UTC parameters

An earlier version of this account had the Oncore recover on the next
broadcast of the UTC parameters, subframe 4 page 18.  The timing
contradicts it.  A frame is five 6 s subframes, 30 s; subframes 4 and 5
cycle through 25 pages, 12.5 minutes, and the paging restarts with
page 1 at the end/start of week (IS-GPS-200N 20.3.2, 20.3.4.1).  Page
18's subframe 4 is in frame 18, at time of week 528 to 534 s, which is
00:08:30 to 00:08:36 UTC; the one before was at 23:56:00 UTC.  Neither
falls in the 00:07:15 to 00:08:15 window in which the Oncore came
right.

## Precedents

- **The Oncore VP, 2003-11-27.**  Exactly 256 weeks after the
  1998-12-31 leap second, an Oncore VP sent one `@@Ba` with a wrong
  date and time at the 86401st second of the day, correct again the
  next second; an HP 59551A beside it was unaffected
  ([Tom Van Baak, leapsecond.com](http://www.leapsecond.com/notes/leapsec256.htm),
  the BIPM's appendix
  [figure 33](https://www.bipm.org/documents/d/guest/towards-continuous-utc-appendix-only-news-from-the-press-nov-2022);
  Van Baak recalls it on time-nuts,
  [2019](https://febo.com/pipermail/time-nuts_lists.febo.com/2019-June/096730.html)).
  No leap was pending then, so it too acted on a stale match.  It
  establishes the bug family in Oncore firmware, not this symptom: one
  bad message, not minutes 1 s off.
- **2021-11-28, the previous match.**  Many NTP servers applied a leap
  second nobody scheduled; most recovered within a second, but eight
  stayed 1 s off for 2 to 8 minutes.  At least 42 were LeoNTP units,
  traced to their u-blox engine reporting week 2185 for 1929
  ([weberblog](https://weberblog.net/partial-ntp-pool-the-leap-second-that-wasnt/));
  LeoNTP's firmware 1.24 "Fixed erroneous indication of scheduled leap
  second" ([LeoNTP](https://www.leontp.com/firmware/)).
- **2026-10-25 predicted.**  In November 2025 LeoNTP 1200 units
  announced a leap second for 2026-10-25 00:00:00, 512 weeks after
  1929 ([NTP Pool forum](https://community.ntppool.org/t/leap-second-in-october-2026/4132);
  [time-nuts, Steven Sommars](https://febo.com/pipermail/time-nuts_lists.febo.com/2025-November/110275.html));
  that was a fault in LeoNTP's own server, fixed in its firmware 2.07
  ([time-nuts, Tom Van Baak](https://febo.com/pipermail/time-nuts_lists.febo.com/2025-November/110284.html)).

None of this shows that the 58503A's Oncore behaves the same way.

## Alternatives

- The Oncore's cached UTC offset, or its handling of the week
  transition, went wrong without any leap logic.
- A race in the 58503A: its status message, or the exchange that
  switches the Oncore between GPS and UTC time to measure GPS-UTC
  (`gps_leap_ack`), which a check that has just set the clock requests.
- The duration is the host's view: whole-second readings polled once a
  second, on a host clock 12 s slow, distort it by about a second.

Raw `@@Ea` time stamps, message arrival times, a capture of the GPS and
UTC mode exchanges and the host clock's log would tell these apart.
Narrowing the windows needs a capture of the Oncore's serial line, or
the receiver's time read against a disciplined host clock every second.

## What it is not

The 58503A's firmware decodes none of the leap fields; it acts only on
the Oncore's pending flag `@@Bj`, and books a leap for a quarter's end
([`firmware/58503a-3704.md`](firmware/58503a-3704.md#time-reset-and-the-oncores-time)).
As a narrow negative control, the firmware run in an emulator with
this unit's EEPROM and a synthetic Oncore (`@@Bj` 0, a correct 2026
date, no WN_LSF model), from 2026-10-24 23:35 UTC, stayed at `*STB?` 0
through 00:25: the 58503A's firmware alone does not step.  It says
nothing about the Oncore.

## What to do

Nothing is required.  The latch is harmless; read
`:STATus:QUEStionable:EVENt?` (`097-59551-02` 5-46) or send `*CLS`
(5-40) to clear it ([`running.md`](running.md#time-reset-at-a-gps-week-boundary)).

If the broadcast fields are unchanged, WN_LSF still 137 and no leap
second announced, week 2697 matches again, ending Saturday 2031-09-20
(00:00 UTC on Sunday 2031-09-21).  Anything using the receiver's time
of day, rather than its 1 PPS edge, may then be 1 s off for some
minutes.  A capture of the Oncore's line across that boundary, or the
next at which a unit is logged, would settle the mechanism.
