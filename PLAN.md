# Plan

A Rust library for talking to HP / Symmetricom SmartClock GPS receivers
over serial, a logging daemon that runs as a system service, a TUI, and
browser and Prometheus views.  A GUI is not planned.  This file records
the decisions that are not obvious, each with its reason, and the open
work with no other home.  `README.md` says what the project is,
`docs/running.md` how it is deployed.

## Goals and constraints

- Unattended long-term logging of EFC and holdover state for drift
  analysis, independent of whether anyone is watching.  Read-only
  monitoring first; control commands opt-in.
- The wider SmartClock family, notably the Z3801A; the 58503A is the
  reference.  Other variants' table entries come from their manuals and
  firmware.
- **Single-operator machine**, as a standing assumption.  It justifies
  socket permissions as the whole of authorization, an audit trail with
  no notion of who, and one daemon per device rather than a fleet.
  Revisit these together if it stops being true.
- Review diffs, not only the tree: the worst defects so far each gave a
  wrong value rather than an error while every test passed, because the
  fixtures covered only the cases the code was written from.

## Architecture

The crates are listed in `README.md`.  `smartclock-log` holds the log's
schema and every query that reads it, so the daemon, monitor and web
view cannot drift apart.  The library, bottom up:

1. `transport` -- `Transport`: serial, TCP, replay, tee.
2. `session` -- framing: prompts, echo, timeouts, error queue drain;
   the status screen by its `:SYSTem:STATus:LENGth?` line count.
3. `command` -- `Dialect`, `CommandId` and specs from `commands.toml`.
4. `types`, `parse`, `screen` -- newtypes, parsers, the scraper.
5. `device` -- `Device`: typed queries and poll steps; `control`.
6. `task` -- `DeviceTask`: the schedule, requests, snapshots.
7. `protocol`, `wire`, `client` -- the socket and its client.

## Decisions

### The daemon owns the port

`smartclockd` holds the serial port open, so every other component is
its client and multi-day history does not depend on a TUI being up.
One `DeviceTask` thread owns the `Session` and issues every command: an
interleaved command's half-read prompt desynchronizes every later read.
`Session` moves into that thread at construction, so a second writer is
a compile error.  A command that errors must not leave its reply in
flight, or the next poll reads it as its own (TFOM recorded as FFOM).
Client requests are served between polls, at most `REQUESTS_PER_POLL`
before the schedule's turn, so neither starves the other.  A second
daemon pointed at a live socket refuses to start.

### No client-side SCPI crate

`scpi`/`scpi-contrib` are server side, `scpify` TCP and HiSLIP only,
`scpi-client` and `instrument-core` immature or VISA-oriented.  All
assume a clean write / read-to-terminator cycle; the receiver echoes,
prompts with `scpi > ` / `E-nnn> `, and its richest response is an
ASCII status screen.  The standard parts are shallow (`097-59551-02`).
The simulator skips `scpi` too: prompt, echo and screen are the parts
worth emulating.

### The protocol stays JSON, and has no separate specification

Newline-delimited JSON over a local socket: gRPC would mean `tonic` and
an async runtime the workspace does without, and protobuf a codegen
step and the loss of watching the socket with `socat`, for a message a
second.  Revisit if a non-Rust client appears.  Every client is built
from this tree and the compiler checks the wire type;
`smartclock::wire::Reading` is not `Snapshot`, so an internal rename is
a compile error, not a protocol change.  `smartclock::protocol`
documents what a type cannot (request ids, `VERSION`).  Protocol and
schema each carry a version, since daemon and clients upgrade
independently.

### Transports and readers

- Windows named pipes were tried and dropped: they have no read or
  write timeout, and a daemon that accepts and then wedges must not
  hold its client.  Windows uses TCP on `127.0.0.1:9978`.
- A bare `HOST:PORT` given as `--daemon` is refused rather than looked
  for as a file; TCP is `tcp://HOST:PORT`.
- History is read from the SQLite file, read-only, beside the daemon's
  single writer (WAL).  A read-only reader needs `-shm` and cannot
  create it in `/var/lib/smartclockd`, so the daemon sets persistent
  WAL.  rusqlite has no safe call for it; the workspace denies
  `unsafe_code` rather than forbidding it, and that one call carries the
  `expect`.  A log closed by anything else loses `-shm`; readers then
  open it immutable, safe because no writer holds it.
- The simulator is not PTY-backed, which would make it Unix-only:
  tests use the in-process `SimTransport`, end-to-end runs the same
  simulator over the `TcpTransport` ser2net needs anyway.

### A receiver's line settings are found, not assumed

Rate and framing belong to the unit, not the port, and ports
re-enumerate.  With no identity, 19200 and 9600 at 8N1 and 7O1 are tried
in turn (`smartclock::attach`).  A probe at the wrong framing queues
errors on the receiver; they are read off at the working settings and
counted, never cleared with `*CLS`, which would also clear the event
registers that drive the front-panel alarm.

### Commands are gated by class, at the daemon

A client sends SCPI text, which the daemon classifies against the
command table: Query (no gate), Control (`--allow-control`: setters,
and reads that clear state -- an event register, `*ESR?`,
`:SYSTem:ERRor?`), Dangerous (`--allow-dangerous`).  Each gate is a
daemon flag, set outside any client, so a daemon started without it
cannot be talked into the command.  A command containing `;`, a control
character or non-ASCII is refused before classification, since a query
chained to a setter would classify as a query.  Argument ranges sit in
the table beside the class, so the table, not the typed `Control` API,
is the safety boundary.  The TUI's raw console bypasses the table and
has its own flag, `--allow-raw`.  A reply reports what the device said,
not that the operation finished (a survey acks in milliseconds).  Every
non-poll command is audited in the log, so one file answers "what did I
do to it, and what did EFC do afterwards".

### The command table is data

`crates/smartclock/commands.toml` generates the `Dialect` and
`CommandId` enums and the specs, so divergence between trees,
per-model availability and citations sit in one table diffable against
the manuals, and a typo in an id is a compile error.  `evidence` is
`manual`, `firmware` or `hardware`, so an unconfirmed command is a
known risk.  An argument kind of `free` must be asked for by name, and a
test pins the entries that ask: as a default it let
`:SYSTem:LANGuage? "INSTALL"` through a daemon started with no flags.

Two dialects, not four (`097-59551-02` shows the 58503A tree is
essentially the 58503B's): 58503A/B and 59551A, and Z3801A/Z3816A; the
Z3805A answers the z3801 one.  An operation a model lacks returns
`Unsupported` without reaching the device.

### Condition registers, never event registers

Reading an event register clears it, and with it the front-panel Alarm
LED and the BITE output, which belong to whoever is at the instrument.
The daemon polls the condition registers and `*STB?`, which reading
"does not change" (`097-59551-02` 5-44), so the logger never writes to
the receiver, `*CLS` included.  The transition filters are recorded once
per attachment and never written: they are non-volatile and only
`:SYSTem:PRESet` resets them.  Time Reset is event-only (5-40), so it is
not seen.

### The receiver's own records

The error queue (`:SYSTem:ERRor?` removes what it returns; a full queue
discards the newest, 5-31) and the diagnostic log (222 entries, then it
stops recording) are lost unread, so the daemon journals both, under
the receiver that wrote them.  The journal runs on the thread that owns
the database, since a channel between reading an entry and writing it
is a gap a shutdown can lose it in, and starts afresh on each
attachment.  `--adopt-log` erases the receiver's log once copied, so it
records again: opt-in because irreversible, and only when the copy is
complete and gap-free, the log-almost-full bit is set and `*IDN?`
confirms the unit; the entry count goes with the command so the
receiver refuses (-222) if an entry arrived in between.

### Disconnection is a first-class state

The daemon reconnects rather than exiting, and an outage is a gap, not
a flat line.  Freshness is per tier: a medium or slow value counts while
its tier's timestamp is within three of its intervals
(`Cadence::current_window`) and is null past that, so a failing tier's
line breaks.  Each opening of the receiver is an attachment
(`AttachmentId`); a command that reaches a later attachment than it was
sent under is answered `Reattached` and not sent, since it was meant and
audited for the earlier unit.  The daemon has no default device, since
a wrong one sends SCPI at whatever is there.

### Rows belong to a receiver, not to a file

Two oscillators' history in one file reads as one oscillator with a
step.  A `receiver` table keys every per-unit row, on the serial alone:
keying on all of `*IDN?` would make a firmware upgrade a new receiver.
A snapshot is filed under the receiver it was read from, not the one
attached when written, since the old unit's snapshots are still queued
at a swap.  Old rows keep a NULL id; backfilling would put one unit's
history under another's name.

The slow tier asks `*IDN?` first, and a different model or serial stops
the task.  Readings between a swap and that check, at most one slow
interval (a minute by default), are filed under the old unit, so stop
the daemon before moving a cable; asking more often would cost a query
on a faster tier.  The journal asks `*IDN?` at every pass and before
erasing, so it never files or erases across a swap.

### One daemon per port, one log per receiver

`smartclockd@<port>`, so nothing in a daemon becomes concurrent.  The
receiver, not the port, chooses the log: after `*IDN?` the daemon opens
`<model>-<serial>.sqlite`, and nothing before, so a swap switches files,
a unit moved to another port keeps one history, and two instances cannot
collide.  A receiver whose identity does not parse gets a file named
after the device, not a shared "unknown" file.  The daemon and monitor
have no socket default, which would guess an instance name.

### Storage: SQLite, integer nanoseconds, no rollups

SQLite over JSONL because trending means range queries; JSONL stays for
raw wire transcripts, which double as parser fixtures.  Retention is
unbounded; an index on `(receiver_id, at)` serves the per-receiver range
and extent every chart reads (without it the extent alone took 4 s on an
AMD GX-412TC).  Tables are STRICT.

Every timestamp we record is a 64-bit integer of nanoseconds since the
Unix epoch, UTC: it reads back exactly, compares exactly -- readers tell
a measured row from a repeated one by `fast_at = at` -- and orders as
time does.  Was RFC 3339 text until schema 12, which sorted only at a
fixed width, cost about 30 bytes and wanted a date function in every
comparison; not a double, which resolves about 240 ns today and makes
equality fragile.  The browser is never sent the integer, which
JavaScript cannot hold exactly.  The receiver's own times stay text, as
it said them.  `smartclock_log::timestamp` is the only place that knows
the encoding.

No rollups while a week loads in seconds.  A per-minute table was
reviewed and found workable but not small: NULL-safe aggregates with
per-column counts, a key for unknown receiver or mode, a timestamp sum
for each point's mean time, a minute-aligned grid or raw reads at the
ends, a fingerprint of the rules and cadence, a schema bump and a
non-blocking backfill.  ADEV and correlation would read raw rows
regardless.

Every schema change is migrated by the daemon on open (`migrate()`) and
tested: released logs live on hosts other than the bench.

### Poll scheduling is a link budget

The status screen is about 1.6 KB: 0.94 s of wire at 19200 8N1 plus
0.5 s of composing, so it is on no tier.  Everything on it but
per-satellite elevation, azimuth and C/N has its own query; it is read
on request and every `--sky` seconds (300 by default), and never stored
as the latest snapshot.  19200 is the ceiling: the 58503A answers
`:SYSTem:COMMunicate:SERial1:BAUD 38400` with `+0,"No error"` and keeps
19200; `BaudRate` still knows faster rates so a receiver moved there
can be moved back.

The fast tier is one step, because its fields are compared against each
other, and a time interval from one second beside an EFC from the next
is a correlation nobody measured.  The receiver's UTC is on the fast
tier: a clock read once a minute is wrong for the other fifty-nine
seconds.  A part-done tier yields to any tier come due, and a refresh
moves deadlines, not step cursors, so no tier starves.

### The status screen scraper is mandatory

`:GPS:SATellite:TRACking?` returns PRNs only; per-satellite elevation,
azimuth and C/N exist only in `:SYSTem:STATus?`.  The scraper reads by
label, not column: an asterisk ends the preceding cell, and groups
carry their heading column so a blank cell differs from a missing one.
The table is checked against the screen's tracked and untracked counts,
and a disagreeing table is still shown, flagged, which makes
misreadings visible without predicting their shape.  Goldens come from
`097-59551-02` chapter 3 and `097-58503-13`.

### Allan deviation is computed over segments, not over a series

`:SYNChronization:TINTerval?` is already a mean of ten one-second
readings, so the overlapping estimators run over segments cut at every
discontinuity, holes left as holes.  Because of that averaging MDEV and
TDEV are the primary curves; ADEV is kept, labeled, because data sheets
quote it.  The first 3 h after each start of locked operation is left
out: the loop runs wider then, and no query reports its stage, so the
span is the firmware's timing.  Method and checks: `docs/stability.md`.

### Notes, facts and sensors

- Notes (timestamped text) and facts (`key=value` about a unit, with
  when it became true) live in the receiver's log, so bench events line
  up against the data.  Written through the daemon, never sent to the
  receiver; a note is filed under the receiver attached when written.
- Sensors are the host's: `smartclock-sensord` has its own log and
  socket.  In a receiver's log a room temperature would go dark with
  every outage and swap and be logged twice by two daemons, and sysfs
  reads, which can block for seconds, would sit in the serial link's
  path.  `docs/sensors.md`.

### The web views

- Correlation is computed in the browser: nothing else needs r, the fit
  or the lag curve, so they get no endpoint.  The one server change is
  `locked=1`, because the mode is text and cannot be averaged into a
  bucket after the fact.
- The range control copies Grafana's, because people who read time
  series know it, and departs twice: a range moved to end near now
  becomes the moving one, and a fixed range is not read again once a
  read has started after its end.  `docs/views.md`.
- `/kiosk` is a page of its own over the shared drawing code, not a flag
  on the live page.  It never reads `/api/status`, because a display
  left on for days must not cost the receiver a status screen every few
  seconds (hence no sky plot; a test holds it).  Its clock counts on
  between polls by the display's monotonic clock, since a clock that
  stalls and jumps looks broken across a room.  It never writes its
  address, which a signage player may take as a cue to reload, and
  inlines its CSS and scripts and keeps its last data in localStorage,
  to paint at once on a reload.  Its mode word is a fixed 70 kiosk
  pixels, small enough for "RECOVERING" on one line in a wide default
  font, rather than fitted to each display's font.
- Every page refreshes in place and never blanks a panel: on a
  low-power signage player every replaced element or rebuilt chart is a
  visible repaint.  A chart is rebuilt only when its lines or the kind
  of its y axis change.
- Exporter metrics are not renamed and their labels not changed.

### systemd and packaging

- `Type=exec`: the daemon retries with no receiver attached, so no
  moment honestly counts as ready for `Type=notify`.
- No `BindsTo=` the adapter's `.device`: the daemon reconnects itself,
  binding would stop it while an adapter is unplugged, and naming a
  device would mean editing the unit.  No `PrivateDevices`: it hides the
  serial port.  `RestrictAddressFamilies=` admits IP for `tcp://`
  devices.
- systemd is the only Linux-specific piece; porting means a launchd
  plist or a Windows service wrapper, not touching the protocol.
- `make deb` derives `<version>-0~git<commits>.g<sha>[+dirty]`, so
  different code never shares a version (dpkg treats reinstalling one
  as a no-op).
- Two packages: `smartclock-utils` (CLI, simulator) and `smartclockmon`
  (daemon, monitors, exporter, sensor service), because the CLI is
  wanted on machines that will never log a receiver.  They depend on
  each other's exact version, not `>=`: they share the socket protocol
  and the command table, and nothing tests one version against another.
  Breaks/Replaces track the package's own version, since a fixed
  `<< 1.7.0-1` would also break the snapshots that sort below it.
  cargo-deb has no variable for this, so `make release` moves the
  relations and a test (`crates/smartclockd/tests/packages.rs`) fails if
  they and the workspace version part.  Cost: `apt-get upgrade` holds
  `smartclockmon` back at the first upgrade needing the new package, and
  relative links between the two packages' documents break in
  `/usr/share/doc`.
- CI calls the fleet's shared `rust-ci.yml`, since one repository doing
  it differently costs more than one definition; `make ci` is stricter
  and is what you run.  The browser and firmware checks need Node,
  Chromium, an m68k gcc and uv, so they are jobs of their own.

### Flashing is a direct-port command of the CLI

`smartclock-cli flash` reuses the library's session, console and line
settings code, so there is one tool.  An image must match an audited
profile, with its own protected regions and checksums: filenames and
model strings cannot establish compatibility, and there is no force
override.  It never sends `*CLS`.  It reads the whole flash back and
compares it before returning to SCPI.  `docs/firmware/restart.md`, "The
flasher".

### The firmware in C

The goal is idiomatic C a person can read, proven function by function
against the original image in an emulator, not a byte-identical rebuild,
which would need the unknown original compiler.  Method, scope and
figures: `docs/firmware/rewrite.md`; the toolchain:
`docs/firmware/README.md`.

- **A rebuild that runs, in readable C**, narrowed: the hand-written C
  is a proven reading, not a working image.  It compiles as one
  translation unit with the export, and the installer's copies share
  static bodies with the primary's through headers, which the real
  installer, alone in RAM, could not.  Splitting them is not planned
  unless a runnable image is the aim again.
- The function check fails (and CI with it) on any failed function.
  Was a measure that exited zero; changed so that a regression cannot
  pass unnoticed.  A known difference is one whose cause is found and
  is not the C's (`function_check.py`'s `KNOWN`), and counts only while
  every difference of the case matches its pattern, so it hides no
  other.
- Unicorn has no CPU32; its 68020 stands in, since the image uses none
  of the instructions either has alone.  Because the 68020 runs what a
  CPU32 traps on, the link takes a libgcc built for the CPU32 with soft
  float from GCC's source (`tools/firmware/cpu32_libgcc.py`, vendored in
  `third_party/gcc/`), not the toolchain's 68020/68881 one, and the
  build fails on any instruction the CPU32 lacks.
- Unicorn's m68k status register bugs are checked by behavior, not by
  version (`make check-unicorn`), since `dev` still calls itself 2.1.4;
  a Unicorn that differs fails, so the workarounds are revisited rather
  than kept by habit (`docs/firmware/README.md`).
- A routine that returns in the condition codes or in registers beside
  its result is called through one register-bound asm statement, not a
  generated bridge: the bridges kept those results in globals, which
  another task's kernel call could overwrite between a call and its
  caller's reads.  The exporter now writes no bridge at all.
- A register the image uses as its caller left it is read by one helper
  (`cpu.h`'s `CPU_CALLERS_REGISTER`, volatile, since gcc merges such asm
  statements otherwise), and listed in `docs/firmware/quirks.md`.  A
  firmware fault the C can reproduce, it does.
- Stack headroom is a difference, not a figure: a case differs where
  gcc's copy goes more than 192 bytes deeper than the image's, or past
  the floor where another use's memory lies.  A figure alone let
  `libc_doprnt`'s stack sit 4 bytes above a console data stack
  unremarked until gcc 13 crossed it.
- `ctype_table` and the lexer's character classes stay the image's:
  `atoi` and the lexer index them by a sign-extended character, which
  reads the bytes before each.  Calls into image code use the image's
  2-byte slots, not gcc's 4.
- The SCPI tree is generated C data (`smartclock-cli dump-scpi --c`,
  `--names`), not hand-written, and the C reads it in place of ROM's.
  It is for every image, so it refuses a record it cannot check rather
  than guess; `cargo test` pins its output.  The harness maps an
  address in the C's tree to its twin in ROM's, so the parser is proven
  on it.  Left: Setup taking the generated rows, so that Ghidra labels
  each record.
- The pForth kernel's words stay the image's assembly, exact by
  construction (one branches into another's body; A6 is the data
  stack); the C diagnostic words are proven by console sessions in the
  harness.
- The export leaves out the decompiled reading of a hand-written
  function, so that names inside a reading cannot change the file;
  three bare imports of an image give byte-identical exports.
- A field's or global's coded values are an enum in the image's
  `smartclock.h` typing it, never a local enum.
- Hand-written C must not use a bare literal where an enumerator or
  named constant exists (`make check-magic-numbers`, in CI); whether a
  literal passed as a parameter means the same each time is a reader's
  call, so that is reported, never checked.
- **Firmware layout**: `third_party/firmware/` is split by kind, not
  by image, so that what is the vendors', what Ghidra made and what we
  wrote can be told apart at a glance: `images/` what we were given
  (the ROMs, the console log one was rebuilt from, EEPROM dumps, FPGA
  bitstreams), `decompiled/IMAGE/` Ghidra's exports, `rewritten/` our
  C (`common/`, what several images share, and `IMAGE/`, with its
  `types/smartclock.h` and `types/check-layout.c`), and
  `analysis/IMAGE/` what we learned about each ROM (names files,
  modules, handlers, allowlists).  Image-independent headers stay in
  `include/`, the Ghidra archive in `ghidra/`, and its own `Makefile`,
  which the top Makefile delegates to.  `third_party/firmware/README.md`
  maps each image to its places.  It replaced one folder per image
  holding everything about it, where vendor, generated and hand-written
  files sat side by side.  Keeping `decompiled/`, `rewritten/` and
  `analysis/` under `third_party/`, though they are derived from the
  vendor images rather than vendor material, is deliberate: they are
  about those images and inseparable from them.
- **A second image is ported by copying the first's work**, not by
  sharing it from the start: the 58503A's `rewritten/` and
  `smartclock.h` began as copies of the Z3805A's, seeded from
  `tools/firmware/image_diff.py`, and the Z3805A's tree is not touched.
  A third image is the trigger to share: C that several images run
  moves to `common/`, but only by the rules in `rewrite.md`, "Shared
  C": identical or relocated code, identical data layouts, and every
  sharing image's checks unchanged after each move.  What differs, even
  slightly, stays per image.  A new image that shares `common/` takes
  all of it, so a function of it that differs there goes back to every
  image's own C first (the Z3801A 3543, the third to share, sent back
  `counter_set_sync`, the `:LED:ALARm:USER` handlers, and the FPGA bit
  setters with each image's own table).
- **Per-image tool tables**: what the function check gives an image
  other than the Z3805A 3543B is kept in tables keyed by image or held
  by its profile (`harness.IMAGE_PROFILES`, `IMAGE_KNOWN`,
  `IMAGE_GIVEN_CASES`, `IMAGE_STATES`, ...), each
  entry cited in that image's code, never a base entry widened, so a
  port's quirks show as that port's and the Z3805A's figures cannot
  move.  `make check-comment-addresses` makes a copied address show.

## Next

- **Compacting old logs**, perhaps into Parquet.  The logs grow without
  bound.
- **Bench work:** the open items in `docs/hardware-investigations.md`.
- **Editing notes from the web view**, through the daemon socket.
  Needs POST bodies in `smartclock-http`, `/api/notes` returning each
  note's `id`, and edit requests keyed by it.
- **Firmware C:** when a Unicorn release has #2420 (bug 1), bump the
  pin, drop the probe and set `BUG_1.expected` to fixed; likewise bug 2.
  Give the firmware routines their file's prefix, as the hand-written
  helpers have, through the names files.  The exporter should give an
  exception entry no bridge.  The primary's own writes to ROM's range
  are not modeled.

## Open questions

1. **What the firmware leaves open:** `docs/firmware/README.md`, "What
   is not established".

2. **How far the z3801 dialect is confirmed.**  The `firmware` and
   `manual` entries stay unconfirmed until a receiver answers them; all
   but one change settings, which no probe sends.  The query left,
   `:PTIMe:GPSystem:POSition:SURVey:PROGress?`, answers only while a
   survey runs.

3. **Managing the receiver's position.**  Not now.  A receiver told
   where it is goes straight to position hold; a survey leaves the
   1 PPS invalid while it runs.  Candidates: a fixed position from the
   configuration, control of survey-on-powerup, starting the receiver's
   survey, a 24-hour survey in the daemon, a position from gpsd.
   Constraints:
   - each is a control command, so needs the opt-in;
   - the position is non-volatile, so a stale value outlives the
     configuration that set it, and holding it means turning off
     survey-on-powerup; whatever asserts a position must be able to
     release it;
   - writing the position, like the antenna delay, forces a holdover
     (`docs/screen-format-strings.md`);
   - heights: the 58503B holds ellipsoid height, the Z380x and 58503A
     MSL by each engine's own geoid, so a position should be carried as
     ellipsoid height and converted with the engine's own separation
     (`docs/firmware/gps.md`, "Requests").

4. **Whether acknowledging from the monitor is wanted.**  Reading an
   event register clears the alarm: right for a deliberate
   acknowledgment, wrong on a timer.  Nothing needs it yet.

5. **Maybe: a Host allowlist for the web view.**  `smartclock-http`
   checks neither `Host` nor `Origin`, so a DNS-rebinding page could read
   `/api/status` or force screen reads.  Likely shape: `--allow-host
   NAME`, loopback always allowed, a non-loopback bind refused without
   one.  Deferred.

## Known defects

Known and unfixed, each because the fix is not yet worth its cost.  The
threat model is a careless operator on a single-operator machine, not
an attacker:

- Socket permissions are the whole of the authorization, and over TCP
  there is none.  On Windows a program that takes `127.0.0.1:9978`
  before the daemon is the daemon to every default client.  A
  connection ends at its first line that is not a request, so a web page
  posting to the port does not have its body answered.
- A client that connects and only reads holds one of the sixteen slots
  while connected; a watcher does exactly that, so it is not refused.
  One that stops reading is cut off once a write has waited
  `client::DEADLINE`.
