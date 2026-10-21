# Plan

A Rust library for talking to HP / Symmetricom SmartClock GPS receivers
over serial, a logging daemon that runs as a system service, a TUI, and
browser and Prometheus views.  A GUI is not planned.

## Status

Installed from the package as a service, one daemon per port, logging
to `/var/lib/smartclockd/<model>-<serial>.sqlite`; `docs/running.md` is
the deployment note.  **Next** lists pending work.  `make ci` is the
local check; `make test-hw`, the hardware-only set, never runs in CI.

Review diffs, not only the tree: the defects found in September 2026
each produced a wrong value rather than an error while every test
passed, because fixtures covered only the cases the code was written
from.

## Goals

- Read-only monitoring of a 58503A first; control commands later.
- Unattended long-term logging of EFC and holdover state for drift
  analysis, independent of whether anyone is watching.
- Support the wider SmartClock family, notably the Z3801A.

## Constraints

- **Single-operator machine**, as a standing assumption.  It justifies
  socket permissions as the whole of authorization, an audit trail with
  no notion of who, and one daemon per device rather than a fleet.
  Revisit these together if it stops being true.
- **58503A first.**  Other variants' table entries come from their
  manuals and firmware.  A Z3801A and a Z3805A on the bench answer the
  z3801 dialect.

## Decisions

### Flashing is a direct-port command of the CLI

`smartclock-cli flash` loads firmware with the library's session,
console and line-settings code, so there is one tool to learn.  Like
`read-memory` it is a fixed procedure, outside the forbidden-command
check, run with the port's daemon stopped.

An image must match an audited profile (Z3801A, Z3805A, 58503A,
Z3816A), each with its own protected regions and checksums; filenames
and model strings cannot establish compatibility.  Before erase it
checks the image SHA-256, boot checksums, model, running revision and
expected serial, and refuses anything unknown, with no force override.
It never sends `*CLS`.  Afterwards it reads the whole flash back
through the debug console, compares it with the image, and returns to
SCPI with `halt`, or failing that through the installer, by the same
code as `read-memory` (`smartclock::console`).  Revision changes have
simulator coverage, not hardware validation
(`docs/firmware/restart.md`, "The flasher").

### No client-side SCPI crate

Surveyed: `scpi` + `scpi-contrib` (server side, no_std), `scpify` (TCP
and HiSLIP only), `scpi-client` 0.1.1 (thin, immature),
`instrument-core` 0.1.0 (VISA/GPIB oriented).  None fit: the receiver
echoes, prompts with `scpi > ` / `E-nnn> `, and its richest response is
an ASCII status screen, where generic clients assume a clean write /
read-to-terminator cycle.  The standard parts are shallow and specified
in `097-59551-02`: `:SYSTem:ERRor?` returns `<code>,"<text>"`, and the
status registers are IEEE 488.2.  The simulator skips `scpi` too: it
does not model the prompt, echo and screen, the parts worth emulating.

### No separate document for the socket protocol

All four clients -- monitor, CLI, exporter, browser view -- are built
from this tree, and the compiler checks the wire type.
`smartclock::wire::Reading` is not `Snapshot`, so an internal rename is
a compile error, not a protocol change.  `smartclock::protocol`
documents what a type cannot: newline-delimited JSON over a local
socket; unsolicited snapshots, so every request carries an id its reply
echoes; and a `VERSION` the daemon refuses a mismatch on.  A client
outside this tree would need a specification and a stability
commitment.

### The daemon owns the port

`smartclockd` runs as a systemd system service and holds the serial
port open, so every other component is its client and multi-day EFC
history does not depend on a TUI being up.

One `DeviceTask` thread owns the `Session`, and so the fd, and issues
every command: an interleaved command's half-read prompt desynchronizes
every later read.  `Session` moves into that thread at construction, so
a second writer is a compile error.  A command that errors must not
leave its reply in flight, or the next poll reads it as its own (TFOM
recorded as FFOM).  Client requests are served between polls, at most
`REQUESTS_PER_POLL` before the schedule's turn, so neither starves the
other.

One daemon per socket: a stale socket file is removed before binding
only when nothing answers on it, and a second daemon pointed at a live
socket refuses to start.

### A receiver's line settings are found, not assumed

Rate and framing belong to the unit, not the port, and ports
re-enumerate: the 58503A's are settable, the Z3801A's fixed at 19200
7O1 (`097-z3801-01` 1-8, 2-10).  `--baud` and `--framing` give the
settings to try first; with no identity, 19200 and 9600 at 8N1 and 7O1
are tried in turn (`smartclock::attach`).  The daemon, monitor and CLI
probe; `read-memory` (pForth console) and networked receivers do not.
Per-port framing in a drop-in only saves the probe.

A probe at the wrong framing queues errors on the receiver.  They are
read off at the working settings before anything else and counted in
the journal; `*CLS` would also clear the event registers, which drive
the front-panel alarm.

### The simulator is not PTY-backed

A PTY would make it Unix-only, when systemd is to be the one
Linux-specific piece.
Tests use `SimTransport` in `smartclock-sim`, an in-process
`Transport`; end-to-end runs use the same simulator on a TCP listener,
through the `TcpTransport` ser2net needs anyway.

### The protocol stays JSON

gRPC in Rust means `tonic` and an async runtime the rest of the
workspace does without.  Protobuf would add a codegen step and lose
watching the socket with `socat`, for a message a second between two
ends we control.  Revisit if a non-Rust client appears.  The socket
carries its own type, converted from `Snapshot`, so a field rename does
not change the wire format.

### Two channels to the daemon

- **A socket, via `smartclock::link`**, for the live snapshot stream
  and commands, newline-delimited JSON.  A client gets the current
  snapshot on connect, then updates.  A daemon binds an AF_UNIX
  socket at `--socket PATH`, and listens on TCP at `--listen
  HOST:PORT`, either or both; its sixteen clients are counted across
  both.  A client names the daemon it asks with `--daemon` (or
  `--sensord`): the socket's path, or `tcp://HOST:PORT`.  A bare
  `HOST:PORT` there is refused rather than looked for as a file.  The
  socket is a filesystem path, so systemd's `RuntimeDirectory` owns its
  lifetime and file permissions gate access; over TCP nothing does.
  Where there are no Unix sockets, Windows, TCP is all there is: a
  daemon given neither listens on `127.0.0.1:9978`, and the monitor,
  exporter and web view ask `tcp://127.0.0.1:9978` when told no daemon.
  A second daemon there needs a port of its own and its clients told
  which.  The sensor service listens on `127.0.0.1:9977` there, and
  offers no hwmon or IIO.  `smartclock::defaults` holds where things are
  on each, `smartclock::link`'s `unix.rs` and `windows.rs` how a
  connection is made, `smartclockd::stop` how the daemon is stopped,
  and `smartclock_sensord::platform`'s `linux.rs` and `other.rs` which
  sensors there are beyond TEMPer sticks.
  Windows named pipes were tried and dropped: they have no read or
  write timeout, and a daemon that accepts and then wedges must not
  hold its client.
- **SQLite file, opened read-only**, for history.  `journal_mode=WAL`
  lets readers run beside the daemon's single writer.  A read-only
  reader needs `-shm` and cannot create it in `/var/lib/smartclockd`,
  so the daemon sets persistent WAL and closing a log leaves `-wal` and
  `-shm` in place.  rusqlite has no safe call for it; the workspace
  denies `unsafe_code` rather than forbidding it, and this one call
  carries the `expect`.  A log closed by anything else, such as a hand
  `sqlite3` session, loses `-shm`; the readers then open it immutable,
  which is safe because no writer holds it.

The socket keeps the TUI's live pane off SQLite at 1 Hz and makes
reconnection after a daemon restart trivial.  Protocol and schema each
carry a version, since daemon and clients upgrade independently.

### Commands go through the daemon too

A client sends SCPI text (op `query`; the others are `latest`,
`status`, `info`, `note` and `fact`), which the daemon classifies
against the command table and gates by class.  A reply reports what the
device said, not that the operation finished: a survey takes hours but
acks in milliseconds, and clients watch progress in later snapshots.

| Class     | Examples                                              | Gate                     |
| --------- | ----------------------------------------------------- | ------------------------ |
| Query     | `:GPS:POSition?`, `:SYNC:TINT?`                        | none                     |
| Control   | holdover initiate and recover, survey, antenna delay, elevation mask; reading an event register or `*ESR?`, which clears it; reading `:SYSTem:ERRor?`, which removes the entry | `--allow-control` |
| Dangerous | `:SYSTem:PRESet`, `:SYSTem:COMMunicate:SERial1:*`, `:DIAGnostic:ERASe`, `:SYSTem:LANGuage "INSTALL"` | `--allow-dangerous` |

Each gate is a daemon flag, set outside any client, so a daemon
started without it cannot be talked into the command.  A command
containing `;`, a control character or non-ASCII is refused before
classification, since a query chained to a setter would classify as a
query.  Argument ranges sit in the table beside the class, so the
table, not the typed `Control` API, is the safety boundary.  The TUI's
raw console bypasses the table, so it has its own flag, `--allow-raw`.

Authorization is socket permissions alone (`RuntimeDirectoryMode`,
group ownership); no peer credentials or tokens.  Every command that is
not a scheduled poll is audited -- time, text, class, outcome, receiver
-- so one log answers "what did I do to it, and what did EFC do
afterwards".  A successful non-query brings every tier's next poll
forward.

### Disconnection is a first-class state

USB serial adapters drop and receivers are power-cycled; the daemon
reconnects rather than exiting, and records the gap.  A failed poll
republishes the last snapshot unchanged, so clients see it go stale,
and the log writes that moment once, so the outage is a gap.

Freshness is per tier: each group of fields carries when it last
succeeded and its own error, over the wire and into the log, and each
pane shows the age of what it displays.  A medium or slow value counts
while its tier's timestamp is within three of its intervals
(`Cadence::current_window`) and is null past that, so a failing tier's
line breaks rather than running flat.  History queries, the monitor and
the exporter share that window, with the cadence the daemon records in
`meta`.

Each opening of the receiver is an attachment (`AttachmentId`), which
starts from an empty snapshot.  A command that reaches a later
attachment than it was sent under is answered `Reattached` and not
sent, since it was meant and audited for the earlier unit.  The daemon
has no default device, since a wrong one sends SCPI at whatever is
there.

### The receiver's own records

The receiver keeps two records that are lost unread: its error queue,
where `:SYSTem:ERRor?` removes the entry it returns and a full queue
replaces its last entry with -350 and discards the newest (097-59551-02
5-31), and its diagnostic log, which holds 222 entries and then stops
recording.  The daemon journals both, each under the receiver that
wrote it; how the log is copied and ordered is in `docs/running.md`.
The journal runs on the thread that owns the database, through the
request queue, since a channel between reading an entry and writing it
is a gap a shutdown can lose it in.  It starts afresh on each
attachment: the unit may have been power cycled, swapped or
reconfigured.

**Condition registers, never event registers.**  Reading an event
register clears it, and with it the front-panel Alarm LED and the BITE
output, which belong to whoever is at the instrument.  The daemon polls
the condition registers and `*STB?`, which reading "does not change"
(5-44); Time Reset is event-only (5-39).  So the logger never writes to
the receiver, `*CLS` included, and a client needs `--allow-control` to
read an event register.  The transition filters, without which events
mean nothing, are recorded once per attachment and never written: they
are non-volatile and only `:SYSTem:PRESet` resets them.  The bench
58503A's negative filters are zero, so a missing clear-event is not
evidence a fault persisted.

**`--adopt-log` erases the receiver's log** once it is copied, so the
log records again.  It is opt-in because erasing is irreversible, and
fires only when the copy is complete and gap-free, the log-almost-full
bit is set, and `*IDN?` confirms the unit is the one copied.  Clearing
unsets the bit, so a flapping link cannot erase twice, and the entry
count goes with the command so the receiver refuses with -222 if an
entry arrived in between.

### Rows belong to a receiver, not to a file

Units are swapped on a bench, and two oscillators' history in one file
reads as one oscillator with a step.  So a `receiver` table has one row
per unit and every per-unit table a `receiver_id`, keyed on the serial
alone; keying on all of `*IDN?` would make a firmware upgrade a new
receiver.  Firmware is recorded as last seen.

A snapshot is filed under the receiver it was read from, not the one
attached when it is written, since the old unit's snapshots are still
queued at a swap.  Journal rows go under the receiver attached at the
time.  Rows from before this keep a NULL id; backfilling would put one
unit's history under another's name.

The slow tier asks `*IDN?` first, and a different model or serial (not
firmware) stops the task, so the daemon reopens under the new serial
even when the link stays up.  Readings between the swap and that check,
at most one slow interval (a minute by default), are filed under the
old unit, so stop the daemon before moving a cable.  Asking more often
would cost a query on a faster tier.  The journal does not wait for
that check: it asks `*IDN?` at the start of every pass and again before
erasing the receiver's log, and skips on a different unit, so the new
unit's log is neither filed under the old one nor erased on the old
one's progress.

### Storage: SQLite

Chosen over JSONL because trending means range queries over time.
`rusqlite` with bundled SQLite, in `StateDirectory=smartclockd`
(`/var/lib/smartclockd`).  Retention is unbounded; the timestamp index
keeps it queryable, and a second on `(receiver_id, at)` serves the
per-receiver range and extent every chart reads.  Without it, the
extent alone scanned the whole table: 4 s on an AMD GX-412TC with
1.7M rows.  JSONL is kept for raw wire transcripts:
append-only, greppable, reusable as parser fixtures.

Every timestamp we record is an integer: nanoseconds since the Unix
epoch, UTC, in 64 bits, which run to 2262.  An integer reads back
exactly as written, compares exactly -- the readers tell a measured row
from a repeated one by `fast_at = at` -- and orders as time does, so
the index on `at` serves every range bound and `ORDER BY at`.  Not
text: until schema 12 they were RFC 3339 strings, which sorted as time
only at a fixed width, cost about 30 bytes each, and wanted a date
function in every comparison.  Not a float: a double resolves about
240 ns today, coarsening as the count grows, and equality on two floats
holds only while both went through the same arithmetic.  The browser
is never sent the integer, which JavaScript cannot hold exactly; the
web view converts at its edge, to seconds for the charts and RFC 3339
for the rest.  The receiver's own times -- its log stamps, its date and
time of day -- are what it said, and stay text.  How a time is stored,
and the SQL that reads it back, is `smartclock_log::timestamp`, and
nowhere else.

The tables are STRICT, so a value of the wrong type is refused rather
than stored.

Opening a schema 11 log converts it: each table rebuilt as STRICT with
its timestamps exact to the nanosecond, the whole of it and the new
stamp in one transaction, so a log is left at one schema or the other
however the process ends.  Every timestamp is checked first, and one in
another form, a column the tables no longer have that still holds
values, or a row pointing at nothing refuses the conversion and changes
nothing.  The file is vacuumed after.  On the bench's logs, of up to
1.4 million rows, it took 1 to 5 s and left them 35 to 44 % smaller.
The readers refuse an unconverted log, naming the service that
converts it, rather than finding nothing in it.

No rollups.  A chart reads every raw row in its span, about 5.7 us
each on an AMD GX-412TC: 1.1 s for 48 h and 4.1 s for a week there.
A per-minute table of count, sum, min and max, kept in the snapshot's
own transaction, would cut a week to a small fraction of that, and an
adversarial review found it workable but not small:
- NULL-safe sums and extrema, with a non-NULL count per column.
- A non-NULL key for rows whose receiver or mode is unknown.
- A row count and timestamp sum, since a point sits at its rows' mean
  time.
- A minute-aligned bucket grid, or raw reads for the partial minutes
  at each end.
- A fingerprint of the rules and cadence that invalidates the table.
- A schema bump, so an older daemon cannot append without it.
- A backfill that does not hold up logging at startup.
- Tests across all of the above.
Not worth it while a week loads in seconds.  ADEV and correlation
would read raw rows regardless.

### Poll scheduling is a link budget

The status screen is about 1.6 KB: 0.94 s of wire time at 19200 8N1
and 0.5 s of the receiver composing it, so it cannot be polled at 1 Hz
beside anything else.  19200 is the ceiling: `097-59551-02` 5-80 lists
four rates up to 19200, and the 58503A answers
`:SYSTem:COMMunicate:SERial1:BAUD 38400` with `+0,"No error"` while
keeping 19200.  `BaudRate` still knows 4800, 38400, 57600 and 115200,
so a receiver another tool moved there can be moved back.

On a 58503A a whole fast pass costs 0.38 s.  So the screen is on no
tier: everything on it but per-satellite elevation, azimuth and signal
strength has its own query.  It is read on request and every `--sky`
seconds (300 by default), half a percent of the link, and never stored
as the latest snapshot, which every poll starts from.

Each tier is a list of steps, one taken per turn.  The fast tier is one
step, because its fields are compared against each other, and a time
interval from one second beside an EFC from the next is a correlation
nobody measured.  A part-done tier yields to any tier come due, and a
refresh moves deadlines, not step cursors, so no tier starves.

| Tier  | Contents                                                    |
| ----- | ----------------------------------------------------------- |
| ~1 s  | `:SYNC:TINT?`, `:SYNC:TFOM?`, `:SYNC:FFOM?`, `:DIAG:ROSC:EFC:REL?`, `:STAT:OPER:HARD:COND?`, `:SYNC:STATE?`, `:SYNC:HOLD:WAIT?`, `:PTIM:TIME?` |
| ~10 s | satellite counts, oven temperature and current, the EFC DAC, `*STB?` and the operation and holdover condition registers, holdover duration and uncertainty |
| ~60 s | `*IDN?`, position, date, diagnostic log count, the oscillator-current constant (`TCOefficient`), the powerup condition register |
|       | `:SYST:STAT?` (satellite table, health line) is on no tier; it is read every `--sky` seconds and on request. |
|       | The receiver's UTC is on the fast tier, not with the date: a clock read once a minute is wrong for the other fifty-nine seconds. |
| ~10 s | the error queue and any new diagnostic log entries, off the schedule; see "The receiver's own records" |

### Allan deviation is computed over segments, not over a series

The phase reading, `:SYNChronization:TINTerval?`, is already a mean of
ten one-second readings, so the overlapping estimators run over
segments cut at every discontinuity -- a relock, holdover, power cycle,
swap or long absence -- with holes left as holes rather than bridged.
Because of that averaging, MDEV and TDEV are the primary curves; ADEV is
kept, labeled, because data sheets quote it.  Each carries a one-sigma
interval from Greenhall's equivalent degrees of freedom; MTIE is
computed, not inferred.  The method, its checks against NIST SP 1065
and allantools, and the bench measurements are in
`docs/stability.md`.

### One log per receiver, named by its serial

Several receivers on a host means one daemon per port,
`smartclockd@<port>`, each with its own device and socket, so nothing
in a daemon becomes concurrent.  The instance is named by the port, as
`serial-getty@` is, and the unit derives device and socket from it;
anything else per port goes in a drop-in, and shared settings in
`/etc/default/smartclockd`.  The daemon and monitor have no socket
default, which would guess an instance name; `smartclock-web` and the
exporter scan `/run/smartclockd/*/socket` unless given one.

The receiver, not the port, chooses the log: after `*IDN?` the daemon
opens `/var/lib/smartclockd/<model>-<serial>.sqlite`, and nothing
before.  A swap switches files, a unit moved to another port keeps one
history, and two instances cannot collide on a file.  A receiver whose
identity does not parse gets a file named after the device and a loud
log line, not a shared "unknown" file.  `smartclock-web` lists every
`*.sqlite` in the log directory and keys everything by serial
(`?receiver=`).

A schema change is migrated by the daemon on open, in `migrate()`, and
tested: released logs live on hosts other than the bench.

### Exporter metrics keep their names and labels

Exporter metrics are not renamed and their labels are not changed:
`oven_tempco` stays, and a stopped daemon's `up 0` carries only its
instance label.

### Notes and facts live in the receiver's log

Bench events and a unit's internals are invisible to the receiver, and
kept only in `docs/hardware-investigations.md` they cannot be lined up
against the log.  So each log has a `note` table, timestamped free text
such as "ran `master_reset`", and a `fact` table of `key=value` about
the unit, such as `ocxo.serial`; a fact records when it became true, so
a replaced part keeps its old value, and setting one also leaves a
note.  Both are written through the daemon (ops `note`, `fact`) and
never sent to the receiver.  A note is filed under the receiver
attached when it is written, even when `--at` backdates it past a swap;
a bench-wide note, such as a splitter change, is written to each daemon
in turn.  `docs/running.md`, "Notes and facts", says where they show.

### Comparing receivers is a page over the existing API

`/compare` shows every receiver over the same range -- 1 PPS TI, EFC,
temperature, TFOM and FFOM overlaid, and ADEV and MDEV curves on one
plot -- from `/api/history`, `/api/adev` and `/api/notes`, asked once
per receiver and joined in the browser.  The live, status and stability
pages stay one receiver each.

### Correlation is computed in the browser

`/correlation` sets two measures against each other from the same
bucketed `/api/history` and `/api/sensors/history` the other pages
read, and works out r, the fit and the lag curve in the page: nothing
else needs those numbers yet, so they get no endpoint.  The scatter is
uPlot's own mode 2, its dots drawn by the page, so it looks like every
other chart.  The one server change is `locked=1`, because the mode is
text and cannot be averaged into a bucket after the fact.

### The time range follows Grafana

The browser view's range control copies Grafana's dashboards -- half-
window steps, zoom out about the center, the `t` keys, Back undoing a
change of range, a refresh picker with Auto -- because people who read
time series know them.  It departs twice: a range moved to end near now
becomes the moving one rather than sliding into the future, and a fixed
range is not read again once a read has started after its end.  Each
page sets its fastest refresh by what one read costs.  `docs/views.md`
has the details.

### Sensors are the host's

Room temperature and the like are read by `smartclock-sensord`, a
service of its own with its own log and socket, not by smartclockd into
the receivers' logs.  A sensor belongs to the host: in a receiver's log
it would go dark with every outage and swap, a swap would start it
afresh in another file, and two daemons would log it twice.  Reading
it on the device thread would also put sysfs reads, which can block
for seconds on a wedged bus, in the path of the serial link.
`docs/sensors.md` has the design.

### systemd unit

- `Type=exec`: the daemon retries with no receiver attached, so no
  moment honestly counts as ready for `Type=notify`.
- `Restart=on-failure` with a backoff and a start limit, so a bad
  setting lands the unit in `failed` rather than restarting forever.
- No `BindsTo=` / `After=` for the adapter's `.device` unit: the
  daemon reconnects on its own, binding would stop it while an adapter
  is unplugged, and naming a device would mean editing the unit.  A
  drop-in recipe is in `docs/running.md`.
- Every option readable from `SMARTCLOCKD_*`, so operators edit only
  `/etc/default/smartclockd` and drop-ins, and `ExecStart=` names no
  settings.  The unit sets the device and socket from the instance
  name.
- `StateDirectory=smartclockd` for the logs, shared by every instance;
  `RuntimeDirectory=smartclockd/<instance>` for the socket.
- Dedicated user with `SupplementaryGroups=dialout`;
  `RuntimeDirectoryMode` and socket group ownership let the TUI run
  unprivileged.
- Hardening: `ProtectSystem=strict`, `PrivateTmp`, `NoNewPrivileges`.
  `RestrictAddressFamilies=` admits `AF_INET` and `AF_INET6` as well as
  `AF_UNIX`, for the `tcp://host:port` device form.  No
  `PrivateDevices`: it would hide the serial port.

systemd is the only Linux-specific piece; `serialport` and `rusqlite`
are portable, and a daemon listens on TCP alone where there is no
AF_UNIX.  Porting means a launchd plist or a Windows service wrapper,
not touching the protocol.

### The command table is data

Commands live in `crates/smartclock/commands.toml`, from which
`build.rs` generates the `Dialect` and `CommandId` enums and the specs.
Each entry has a stable id and a class, and one block per dialect:

```toml
[[command]]
id = "holdover_waiting"
class = "query"

  [command.dialect.hp58503]
  scpi = ":SYNChronization:HOLDover:WAITing?"
  response = "enum:HoldoverWaitReason"
  models = ["58503A", "58503B", "59551A"]
  cite = "097-59551-02 5-36"
  evidence = "hardware"
```

Tree divergence, per-model availability and citations sit in one table
that can be diffed against the documents, and a typo in an id is a
compile error.  `evidence` is `manual`, `firmware` (every keyword found
in the firmware's own keyword table) or `hardware` (a receiver answered
it), so an unconfirmed command is a known risk.

An entry may declare its argument -- `integer` with `min` and `max`,
`word` with `allowed`, `none` or `free` -- which the daemon checks
before anything reaches the receiver; none declared means none taken.
`free` must be asked for by name, and a test pins the entries that ask:
as a default it let `:SYSTem:LANGuage? "INSTALL"` through a daemon
started with no flags.  `docs/commands.md` is generated from the table,
and a test fails if they disagree.

### The package version is derived

`make deb` builds `<version>-<commits>+g<sha>`, with `+dirty` for an
unclean tree, so different code never shares a version; dpkg treats
reinstalling an identical version as a no-op.  The hand-written
`packaging/debian/changelog` records releases; a derived version is a
build.

### CI is shared; the Makefile is what you run

```
make            build
make windows    cross-build for Windows
make ci         fmt-check clippy test
make fmt        cargo fmt
make clippy     cargo clippy --all-targets -- -D warnings
make test       cargo test
make test-hw    cargo test -- --ignored
make web-deps   install the browser tests' Playwright and Chromium
make test-web   the browser tests
make docs       regenerate docs/commands.md from the command table
make deb        a snapshot package
make release    tag the version Cargo.toml already names
make release-notes  the GitHub release notes the tag will get
```

Pushing a release tag builds the packages, sets the GitHub release's
notes from the newest `packaging/debian/changelog` entry
(`packaging/release-notes.sh`), and rebuilds the apt repository.  The
changelog entry is written once, in `make release`, and is the release
notes everywhere.

CI calls the fleet's reusable `rust-ci.yml`, since one repository doing
it differently costs more than one definition; `make ci` is stricter.
The browser tests need Node and Chromium, so CI runs them in
`.github/workflows/web.yml` and `make ci` leaves them out.

### Dialects

Two branches, not four; `097-59551-02` shows the 58503A tree is
essentially the 58503B's:

| Family                  | Tree                                      |
| ----------------------- | ----------------------------------------- |
| 58503A / 58503B / 59551A| `:GPS:`, `:SYNChronization:`, `:PTIMe:`   |
| Z3801A / Z3816A         | `:PTIME:GPSYSTEM:`, `:ROSCillator:`       |

The Z3805A answers the z3801 dialect.  Availability is per model, and
an unsupported operation returns a typed `Unsupported` error without
reaching the device.  Response formats differ too: `:DIAG:ROSC:EFC:REL?`
returns `+-d.dEe` on the 58503A but is documented as a plain integer on
the Z3801A.

The bench Z3801A (3543-A) does not echo, and stamps its diagnostic log
in hex seconds of GPS time (`097-z3801-01` 4-13) where the others write
a date; the stamp is stored as written.  Its `-230 Data corrupt or
stale` while tracking no satellites is the manual's answer for an
unavailable value (4-6, 4-7), taken as a state refusal.

### The status screen scraper is mandatory

`:GPS:SATellite:TRACking?` returns PRN numbers only; per-satellite
elevation, azimuth and C/N exist only in the `:SYSTem:STATus?` screen,
so the scraper is a first-class parser.  `097-59551-02` chapter 3 has
five sample screens of distinct states -- survey in progress,
`*nn Acq..` acquiring markers, `nn -- ---` untracked rows, `Predict --`
unavailable, "Locked to GPS: stabilizing frequency" -- and
`097-58503-13` more.  These are scraper goldens.

The scraper reads by label, not column.  An asterisk ends the preceding
cell, and groups carry their heading column so a blank cell differs
from a missing one; otherwise a blank signal cell takes the next
satellite's asterisk, and a short tracked column lets the first group
claim the second's rows.  The tracked count is the `Tracking:` not
preceded by `Not `.

The table is checked against the screen's tracked and untracked
counts.  A disagreeing table is still shown, with the pane saying so,
which makes misreadings visible without predicting their shape.

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

## Next

- **Later: compacting old logs**, perhaps into Parquet.  The logs grow
  without bound.
- **Bench work:** the open items in `docs/hardware-investigations.md`,
  each with its TODO.
- **A rebuild that runs, in readable C.**  Decided 2026-10-20: the
  goal is idiomatic C a person can read and change that gcc builds into
  firmware that runs, not a byte-identical rebuild, which would need the
  original compiler, unknown (`docs/firmware/README.md`, "The calling
  convention").  The way there is one function at a time from the
  original image, which runs: recompile one function with gcc, link it
  in with the rest still the image's, and before trusting it run it and
  the original side by side in the emulator the libc and app checks use
  (`tools/firmware/`), comparing results and memory.  A failure then
  points at one function, and every step leaves a working image.

  Steps, in order:

  1. The C compiles.  Done: `make check-decompiled` compiles the whole
     file to an object, every function defined, and confirms its
     assembly is the image's own bytes.  The calls that disagreed with
     their callees were fixed in the names file's prototypes, from each
     callee's body and the bytes its callers push.  What the object
     still needs from a link: a few of gcc's helpers -- 20 float
     comparisons the decompiler wrote as C operators and two 64-bit
     divisions, which the image's own float_compare and 64-bit routines
     can stand in for, or libgcc, which the link uses -- and the made
     symbols `z3805a-3543b.symbols.ld` places.  The decompiler's views
     of the caller's arguments (`stack0xN`) are read from gcc's frame
     pointer; its views into a function's own frame or a register fault
     in the harness until the function is rewritten.
  2. Bridges between gcc's C and the rest.  Started: a routine whose
     arguments and result Setup put in registers, from a convention
     with every item typed, gets a real prototype naming a generated
     wrapper, `c_NAME`, which saves gcc's callee-saved registers, loads
     each argument from gcc's 4-byte slot (gcc promotes a `char` or
     `short` to a whole int there) and calls the routine; 85 routines,
     the soft-float ones among them, so C calls the image's own float
     arithmetic without promoting a float to double.  Left: 10 calls
     into routines a wrapper of this kind cannot reach -- `stack_usage`
     returns two values, `forth_compile_code` is Forth's, the rest an
     exception entry or a kernel's -- and the other direction,
     assembly calling C, where the original passes a `char` or `short`
     in 2 bytes where gcc reads 4.  The kernel internals'
     `undetermined` conventions must be settled first.
  3. A link that keeps the ROM's data -- strings, the SCPI tree,
     descriptors, tables -- at its own addresses, which the code names
     absolutely, and places the rebuilt functions around it.  Done for
     the harness: `tools/firmware/rebuild_link.py` puts every piece of
     assembly at its address, all 418 the image's bytes, and gcc's code
     at `0x600000`, above every chip select; the ROM's data and its original C stay where they
     are, and the assembly calls the original C (`__image_NAME`).  A
     link for the board, gcc's C in the ROM's free space and the
     original C gone, waits on the replaced functions.
  4. The emulator harness: a function's original and rebuilt code on
     the same inputs and memory.  Done, as `tools/firmware/harness.py`.
     `tools/firmware/bridge_check.py` (Unicorn, run with uv) calls each
     wrapper as gcc would and its routine from its registers, from the
     same RAM, and requires the same result, RAM and effects and gcc's
     callee-saved registers kept; 83 of 91 agree, none disagree.
     `tools/firmware/function_check.py` (`make report-decompiled`) does
     the same for each C function, the image's copy against gcc's, the
     image's arguments where the sidecar's `stack` layout says the
     original reads them: of 1249 run, 1238 agree, 1 reaches what the
     harness does not model and is skipped, 10 differ as known, and
     none otherwise.  A known difference is one whose cause is
     found and is not the C's, `function_check.py`'s `KNOWN`: the
     firmware's own reads of what nothing set (`task_stack_info`'s
     locals, `os_report_processes`' D5) and what C cannot say
     (`setjmp`, the 64-bit divides' overflow, `log_erase`'s checksum of
     a pointer); each is counted so only while every failing case
     matches its pattern, so that a new difference in the same function
     still fails.  The harness models what lies
     outside memory at its boundary, the same for both calls
     (`docs/firmware/README.md`): traps #1 and #7 run the image's handlers;
     the kernel's traps are answered by a model of each service, from
     the registers the binding's stubs load and store, and of those
     only the bits the kernel reads (`create`'s D1 and D2 low bytes and
     the block through A0); the devices are
     memory zeroed for each call; and the kernel calls and device
     writes a call makes, in order, are compared as RAM is.
     Unicorn has no CPU32 and the 68020 stands in: the image uses none
     of the CPU32's own instructions (`tbl`, `lpstop`, `bgnd`; a scan of
     its words finds none), and none of the 68020's the CPU32 lacks.
  5. Replace, check, repeat, making the C readable and idiomatic as
     each function is taken: names for locals, real types, loops and
     structures in place of pointer arithmetic, the decompiler's
     idioms (CONCAT, SUB, pieces, unaff_ registers) undone.  Started
     with what the harness found across many functions at once: every
     SCPI handler now has the prototype `scpi_call_handler` calls it
     with, `(parameters, response, call)`, where each showed only the
     arguments it reads, so that its image copy read a third argument
     the harness never gave (92 differing to 78); and the record
     `sprintf` and `vsprintf` give `doprnt`, which the decompiler split
     into two locals gcc laid apart, is typed by a `local` row in the
     names file (78 to 64), as are seven doubles split the same way,
     which the harness found in `sqrt`.  And the C now moves bits where
     the decompiler does: a cast between a floating type and an
     integer, which without an FPU is never a conversion, and the
     implicit conversions gcc finds, are reinterpretations
     (`__ghidra_as`, `__ghidra_bits`), so `sqrt`, `frexp`, `modf` and
     the EFC queries agree (64 to 59); and a shift by a count that is
     not a literal is defined for every count, as the decompiler's is
     (59 to 58).  Then the date and time records `datetime_add` takes,
     split as `sprintf`'s was, the vector names `exception_name` reads
     from 12 bytes into a string whose NUL is the table's first byte,
     and a string result compared by its text, gcc's copy returning its
     own copy of a literal (58 to 53).  Left as they are:
     `int64_divide` and `uint64_divide`, called by nothing, which on a
     quotient too wide for 32 bits return the dividend's low long, as
     `divs.l` leaves it, where the C truncates the quotient.  Then every
     function still on the analyzer's empty signature gets the stack
     parameters its callers push (329 of them), so that its callers'
     C calls it as it is defined, and a piece under a signed cast is
     read signed, the decompiler's sign extension (53 to 49).  Left as
     the harness's own: a function copying a string it is pointed at
     into a small local overruns it on the harness's garbage, and lands
     differently in gcc's frame (`status_screen_height`).  Then the
     harness itself: a pointer left in RAM to the same text, the
     image's in ROM and gcc's in its constants, is the same, and a call
     starts with A6 at a caller's frame, which the image's
     `float_raise` walks where gcc's caller keeps none (41 to 35).
     Five routines the names file marks `convention=other` -- the
     divide's and multiply's error tails, entered by a branch and left
     through their callers' epilogues, and the CPU and RAM self-tests --
     are assembly and no longer run, and `long_array_get_byte` and
     `happening_replay` take pointers, as do the other `long_array_`
     routines (35 to 28).  Then the exporter: a piece is read signed
     where its p-code reads it so, a constant in an unsigned compare
     with arithmetic takes a `U`, and a switch's cases are labeled as the
     table indexes them, from its first index, not 0 (28 to 23).  The
     soft-float routines' copy of their result in A0:A1 is passed on
     (`__ghidra_also`), as is `float_raise`'s and
     `double_truncate_sixteenths`'s in D0:D1 where `pow` calls them
     through a register, and an infinity assigned to an integer variable
     is its bits (23 to 21: `log`, `pow`).  A routine that returns its
     result in the condition codes has its bridge keep them, and the C
     reads its flags after the call from there, not from a compare made
     before it (`clock_init`); and an equality test of a narrow unsigned
     operand with a negative constant compares its unsigned value.  The
     harness has an EEPROM, and gcc's code moved above every chip
     select, off it: twelve functions that agreed reading gcc's code as
     the EEPROM's records agree on an erased one (21 to 20).  The
     Oncore field tables and the installer's records, which the
     decompiler took for short strings, are typed byte arrays (20 to 18:
     `oncore_traim_message`, `boot_unpack_installer`).
     Then
     the harness's models of the kernel, the devices and `trap #7`
     turned 317 of the 541 skipped into comparisons: 107 agree and 210
     differ, nearly all through a few callees whose C is wrong where
     the harness could not see before -- `enter_supervisor`, whose
     `trap #7` and jump back through its popped return address the
     decompiler reads as a call through an unset pointer (61 callers
     jump to 0); `psos_pack_name`, which reads its caller's name
     argument from the caller's frame (12, by `psos_ident` and the
     task and exchange creators); the I/O stubs, whose C drops the
     buffer, length and other registers the binding loads (43); the
     stubs that store a result the kernel returns in A0 through a
     pointer, which the C reads from an unset local (`psos_my_pid`,
     `memory_allocate`, eight in all); and the messages the SCPI
     setters and `pllc_request_*` build on their stacks, split into
     locals gcc lays apart (61).  Five that agreed differ in a case
     now run (`eeprom_write`, `eeprom_write_page`,
     `gps_receiver_status_update`: `enter_supervisor`; `fpga_verify`,
     `hdac_nudge`: the I/O stubs), and `duartb_putc_polled`, which
     agreed by reading what an earlier call left at the DUART, polls
     a zeroed one forever and is skipped (12 to 222).
     `enter_supervisor` is assembly, as it is: its `trap #7` and jump
     back through the address it pops run as the image's own;
     `psos_io_call` takes every register, as `psos_call` does, so the
     I/O stubs pass their buffer and length; and gcc keeps a frame
     pointer, which `psos_pack_name` reads its caller's argument
     through (222 to 154).  The kernel's results in D0 to D3, A0 and
     A1, past the error in D7, reach the C through the bindings'
     bridges, as the condition codes do (154 to 134).  The messages
     the SCPI setters send the loop task through pllc_request_a to _e,
     and the GPS requests, are typed records on their senders' stacks,
     which the decompiler had split into locals gcc lays apart (141 to
     92); three status setters whose first case now runs differ there.
     A message is compared by the 20 bytes from its `+4` the kernel
     copies, not 16, which shows five setters sending a value's low half
     where the image sends all of it (92 to 97).  The self-tests and the
     interpolator's measurements, called through tables with a char in
     a 2-byte slot, take it as `char_slot`, whose first byte lies where
     the slot's does; the pSOS device routines are register routines,
     and `set_timedate`, `console_task_create` and
     `pforth_dictionary_init`, which take traps in line, assembly (97 to
     83).  The EEPROM records, the QSPI's buffer pair, the GPS position's
     three longs and the SCPI step entries the routines that build them
     keep on their stacks are typed; libgcc's soft-float exception
     handler reaches `_fpCCR` through a GOT the link now places and the
     harness loads; and `psos_supervisor`, which pops its return address,
     traps and jumps back, is assembly (83 to 51).  A loop to a stack
     record's end, which the decompiler writes as the frame offset where
     it ends, runs to the record's end (51 to 48).  A pointer to an
     empty text, the image's in ROM and gcc's `""` among its constants,
     is the same: gcc keeps `""` as another literal's end, so nothing
     before it shows (the DUART loopback tests, `sci_puts`).  A rotate is
     a rotate; an argument the kernel returns a result in is read back
     after the call; a copied result reaches a byte piece of it; and
     `si_prefixes` is bytes, not a literal (48 to 38).  Then
     the harness
     turned 92 of the 233 skipped into comparisons (980 agreeing to
     1048, 38 differing to 62): the string devices' write, which sends a
     string and returns its length, where the model had taken an unset
     local as a length (`console_write`); trap #1 through the image's
     handler; `alloc_seg` and `ident` returning memory a caller can
     use; traps #5, #11 and #12, and a write to code, which raises a bus
     error on the board, ending a call; and the status bits the polled
     DUART, SCI and QSPI routines wait for.  The new differences are the
     C's: `psos_io_write` and `sci_port_write` store back the length
     they passed, not the one the device returns; the QSPI self-tests
     pass `&local_c` as the buffer pair gcc lays apart from `local_8`
     (and `selftest_execute`, which agreed, through them); `duart_init`
     returns D0:D1 it never sets; `setjmp` reads registers C cannot;
     and printf's `%e` and `%#x` (`diag_pll_rep`, `diag_pr_gps_debug`).
     Then each frame a `link` makes is filled afresh, as the stack is at
     a call's start, so that the bytes of a message its sender never
     writes match; `scpi_numeric_set` takes its float and integer values,
     two slots StackParameters missed because only their address
     escapes; the two step validators return their results; and
     `gps_init_sequence`'s four arguments are one array (62 to 45).  A
     register a function reads as its caller left it is that value, not
     an unset local (45 to 36).  A call through a pointer loaded from a
     table, the self-tests' and the DAC searches' measurement, is given
     its callee's prototype at the call, which the decompiler took as
     code with no arguments, and a structure it builds from the bytes
     pushed is those bytes (36 to 28).  77 routines a parameter of
     which the harness found dereferenced take a pointer to their record,
     where Ghidra had an integer; 19 that agreed now follow a pointer in
     the buffer the harness gives them and are skipped (1082 agreeing to
     1063).  `doprnt`'s precision, count and exponent text are an int, an
     int and one array, where Ghidra had a pointer that printed 6 as the
     text at address 6; the QSPI tests' buffers are one record; a switch
     is relabeled though the decompiler gives its default a case token
     (`%x`); `HOLD_CAUSE_NONE` names a byte's 0; `duart_init` preserves
     what it returns nothing in; a register a preserving call's
     INDIRECT made up is the value the code last wrote it, back through
     a lone branch (`modf`'s A0, `set_gps_utc_offset`'s D1); and a shift
     inside another's left operand is rewritten where it now is (28 to
     5).  Left: `setjmp`, the 64-bit divides' overflow, `log_erase`'s
     checksum over a pointer and the DIAG query's caller's registers.
     Then the harness turned 59 of the 141 skipped into comparisons
     (1085 agreeing to 1159, 5 differing to 14), each model the same
     for both calls: a branch to itself, `fatal_error`'s spin, ends a
     call halted, a zero divide at its vector, and a task's loop at its
     1000th kernel call, still running (16); RAM starts with the
     kernel's configuration and I/O table and the running task's
     record (5); `request_x` from `spoo` and `pllc` receives a
     well-formed `spoo_request` and `pllc_request` (2); and two cases
     more give each pointer a buffer of pointers, into compared memory
     of the harness's own (36).  The limit cuts short no call that
     returned before it; `selftest_execute`, whose gcc copy outran the
     step limit, now agrees.  The new differences: `program_exit`
     prints its return address, which gcc's copy reads as 0;
     `stack_measure` and `stack_usage_report` on the buffers of
     pointers; `task_stack_info`,
     whose image copy reads an unset local the decompiled C keeps
     (`task_stack_report`, `s_rep_word`,
     `scpi_query_DIAGnostic_OS_STACk`); and `fpga_verify_first`,
     `_odd` and `_even`, which agreed, return 0 where the image's
     return 1 on the buffers of pointers, after the first QSPI read.
     Their causes: the FPGA checks' buffer pair, kept as two locals gcc
     lays apart, and `stack_usage_report`'s four longs, are each one
     local by a `local` row; `stack_usage`, which returns two values in
     D0 and D1, gets one `long long` from D0:D1, as `ldiv`, and so a
     bridge, where `stack_measure` called it with no arguments;
     `program_end_report` reads the long above its argument, which
     `program_exit` does not push, its own return address, and takes it
     as a second argument, which the exporter reads from gcc's frame, N
     + 4 above its frame pointer, for each `in_stack_N` where every
     argument is a long in its slot; `stack_usage` preserves the A0
     `stack_measure` reads after it (1159 agreeing to 1166, 14
     differing to 8).  `task_stack_info`'s two locals, which the image
     reads unset ("no system stack found" at `0x16c28` to `0x16c32`),
     still differ: gcc takes them as 0, where the image faults on the
     filled frame.
     Then the kernel's data area as `kernel_init` makes it, its own
     code run once on the configuration but for the devices' function 0
     and the creates of ROOT and IDLE: the queue of tasks waiting on a
     region, at `+0x64`, points at itself, where a zeroed one led
     `os_report_regions` through address 0 past RAM (77 skipped to 71,
     8 differing to 14).  The six now run differ, every one through two
     reports: `os_report_process` prints a stack share from D5 its
     caller left (`unaff_D5`) when no segment holds the stack, and
     `os_report_regions` a region's use, 7232 kB where the image's
     prints 0 (`ps_word`, `mem_rep_word`, `os_process_report`,
     `os_memory_report` and the DIAG OS queries).  And a parameter that
     points at a routine of a function typedef is given a routine of the
     harness's own, whose call and arguments are an effect, `printf`'s
     text by the image's own `vsprintf`; the names file types the kernel
     reports', `spoo_run`'s, `log_print`'s and the status screen's
     routines, and Setup now gives a prototype's parameter a pointer to
     a function typedef's function, which Ghidra's parser drops and the
     decompiler prints as a byte pointer when it points at the typedef,
     as `os_report_process`'s and the DAC searches' showed (`undefined1
     *`).  Of the 71 skipped, 14 now run: 10 agree and four differ,
     `log_print`, whose C passes its formats' low words, `os_report_regions`, and
     `status_screen` and `status_screen_print`, whose C hands `strcat`
     a text's bytes as its address.  `scpi_parameter_match` and
     `scpi_parameters_finish` take a pointer to a record with a pointer
     at `+0xa`, where Ghidra had an integer, and the first then agrees
     (1166 agreeing to 1176, 71 skipped to 57, 14 differing to 18).
     Then the names file's rows for three of those causes.
     `os_report_regions` keeps its counts, free and used bytes and
     starts in arrays of two the decompiler took as one `int[4]` and
     loose longs, which its C read past: `local` rows.  `log_print`,
     the status screen and `status_screen_position` call `print`, or
     `sprintf` through A0, with arguments at stack offsets that a
     `move.b` or `move.w` push for another call writes, and the
     decompiler sizes a variadic call's arguments by those pieces,
     passing an address's low word or byte: `call` rows give each such
     call its prototype.  `status_screen` walks its mode lines, a table
     at `0x4d032` the decompiler took as the string `"\x03"`: a label,
     `status_screen_mode_lines`.  Its line buffer, 80 bytes after the
     byte `gps_message_supported` fills, and `status_screen_position`'s
     16-byte text were split into short arrays and loose bytes gcc lays
     apart: `local` rows.  Regenerated, 1176 agreeing to 1183, 18
     differing to 11.  The
     stack share `os_report_process` prints from D5 is the firmware's:
     `region_segment_containing` (`0x162b2`) touches only D0, D1, A0
     and A1, and `os_report_processes` holds the address of `"%s"` in
     D5 (`0x16768`) across both of its calls, so the image prints
     332674% for a task whose stack is in no segment; its MEMORY sum
     starts from the caller's D6 the same way.  Like
     `task_stack_info`'s, it is left.
     Then the image's own state where the skipped read it, each model
     the same for both calls.  RAM starts as `status_init` leaves it,
     the image's own run once as `kernel_init` is, its segments kept:
     `status_object`, its registers and queue, where a zeroed pointer had
     led 14 through ROM's first bytes (57 skipped to 46; three
     `:STATus:OPERation:HARDware` queries, which agreed reading ROM
     there, now index the registers by their parameters' garbage past
     RAM and are skipped).  `qsort`'s size and comparator, which its
     swaps read as 0 and looped 2^32 times, are as its one call sets
     them (2).  The second pointer case's fill points each of a record's
     first three longs at the next and the third at a NULL, where every
     long of the first's holds itself, so that a list through them ends
     (7; one fill for both cases gained seven and lost eight, an error
     queue's indexes among them, so each fill has a case).  The pllc
     task's request is in the variables its readers take it from, as the
     image's own `get_setup_vars` stores it (4).  A parameter is given the
     image's object its callers pass: an SCPI formatter's `output` the
     console port (8); the status model, its registers and `aging_curve`
     by type, and a `scpi_step` (8); a header `path` set back to the
     main tree (2).  A trap from a task's user stack takes its frame on
     the supervisor stack, which ran `primary_start` through the boot,
     and `rtr`, which Unicorn lacks, is done in its place, which runs
     `console_init` and finds gcc's `pforth_define_word` calling
     `forth_compile_code` without its dictionary pointer in A4.  Prototypes
     that name `scpi_scan_token`'s and `scpi_skip_to_terminator`'s
     `input`, `scpi_format_text`'s and `scpi_format_choice`'s `output`,
     `scpi_header_step`'s `path` and `error_queue_push`'s `error_queue`
     run four more (1183 agreeing to 1230, 57 skipped to 9, 11 differing
     to 12).  Then an SCPI handler is given the parameters and call
     record `scpi_call_handler` passes it for its path, its nodes' ids
     first (the three HARDware queries, and `:DIAGnostic:QUERy:RESPonse?`,
     which compares the record's input with the console port); the
     parser's routines a record, its items and slot and a parser context
     by name (`scpi_parse_message_unit`, `scpi_call_handler`,
     `scpi_format_response`, by the names file's prototypes); and a `status_snapshot_record *` `status_screen_snapshot`
     (`status_screen_height`, whose `position_source` lay past the
     harness's buffer) (1230 agreeing to 1238, 9 skipped to 2, 12
     differing to 11).  Left
     skipped: an exchange's queue (`os_report_exchange`), whose callers
     take it from the kernel's list, empty in the harness, and
     `scpi_format_resolution_array_record`.  `forth_compile_code` takes
     its code at A0 and leaves A4, the dictionary pointer, past it,
     keeping D0; `pforth_link_word` leaves A4 past the link; their
     conventions now say so (`out=A4`), so that `pforth_define_word`
     appends where the routines leave A4 and counts its loop in D0.
     `console_init` then defines its words as the image does, and differs
     at `console_task_create`'s service 1: the high word of D1, which its
     `clr.w` leaves as its caller had it, and A0, the parameter block on
     its own stack, neither of which C can give.
     Then the kernel's list of exchanges, which RAM now starts with as
     `root_task`'s three creates leave it, `gpsx`, `plla` and `pllc`,
     each made by the kernel's own `kernel_create_x` (`0x1537e`) run on
     the data area, and `os_report_exchange` given one of them, as
     `os_report_exchanges` passes each (1238 agreeing to 1239, 2
     skipped to 1).  The caller's frame a call starts with holds a
     first argument, `"gpsx"`'s address, which `psos_pack_name` reads
     through A6 as its callers' name: its bridge agrees (82 to 83).
     `kernel_dispatch_io`'s bridge is not run: trap #4 enters it with
     an exception frame, which its `rte` returns through, and a
     wrapper's `jsr` leaves only a return address, so no wrapper of
     this kind can call it, and nothing in the C does.  The exporter
     should give an exception entry no bridge, or one that enters by
     its trap.  Left skipped: `scpi_format_resolution_array_record`
     (`0x26816`), which nothing in the image calls.  It reads its
     output at `+4` and its record at `+8` (`move.l (8,fp)` pushed to
     `scpi_format_block`, `movea.l (12,fp),a2`), where the C has only the
     record and reads the output from it: the names file now gives
     the prototype `(void *output, char *record)`.  It ends by calling the routine at the record's `+6`,
     which no caller shows and the harness's buffer does not hold.
     Then the installer's copies, 79 C functions whose prototypes lay
     out and 5 bridges (`installer::` in the sidecar), which run where
     the boot code unpacks them, in RAM from `0x100400` to `0x10aad2`
     (`docs/firmware/restart.md`, "Forced installer entry with an
     unusable primary"), the RAM the harness had started as the
     primary's.  Each module-level constant of `harness.py` that names
     the primary's state is now a field of a profile, one for each
     image (`harness.Profile`): the installer's has RAM the unpacked
     image, with ROM's first `0x400` bytes, the boot vector table, at
     `0x100000`, as the entry at `0xa58` copies them, so that traps #1
     and #7 take its own handlers; its configuration and I/O table as
     `installer_psos_config_init` (`0x1083b6`) copies them, its data
     area from `installer_kernel_init` (`0x1026de`), `kernel_init`'s
     bytes, and its status object from `0x107a00`, as its root task
     (`0x100d40`) calls it; its spool's exchange (`0x10afbe`) and the
     message the root task sends it, its caller's name (`"spoo"`), its
     one string device and its own `vsprintf`; the arguments' buffers at
     `0x900000`, outside RAM, compared as RAM is; and the unpacked image
     read-only and a place literals lie, as ROM is (docs/firmware/README.md).
     The primary's results are unchanged (1239 agreeing, 1 skipped, 11
     differing; bridges 83).  The installer's: 43 agree, 5 are skipped
     and 31 differ; 4 bridges agree, and `installer_kernel_dispatch_io`'s,
     an exception entry, is not run (91 bridges: 87 agreeing, 4 not run).
     What differs is the installer's rows, not the harness: their
     primary twins have conventions the installer's copies lack, so
     that the exporter wrote them as C, and gcc's copies of their
     callers call that C.  `installer_psos_call` (`psos_call`,
     `register`, the service in D7) runs every binding's trap with no
     service (`installer_psos_my_pid` to `_service_29`, `_mutex_acquire`
     and `_release`, `_periodic_interrupt`, `_drtb_getc`, `_gets` and
     `_putc`); `installer_enter_supervisor` (`other`, a `jmp (a0)`
     through its popped return address) jumps nowhere, which every
     polled print reaches (`installer_set_interrupt_mask`,
     `_enter_supervisor_mode`, `_console_write_polled`, `_fatal_error`,
     `_print_exception`, `_float_raise`, `_float_exception_report`,
     `_program_exit`, `_program_end_report`); `installer_psos_pack_name`
     (`other`, its caller's A6) reads no frame; `installer_trap_1`,
     `_trap_7`, `_default_exception` and `installer_return`, an `rte`,
     are exception entries (`exception`) run as functions.  And two
     prototypes are not their twins': `installer_identification_record_read`
     returns a short where `identification_record_read` returns a
     byte, and `installer_duarta_receive` a `uint` it never sets.  The 5
     skipped are the status system's (`installer_status_propagate`,
     `_set_bit`, `_apply_condition`, `_read_event`,
     `installer_error_queue_push`), whose prototypes take `int` where
     their twins take a `status_model *` and `status_register *`, which
     the harness gives by type, and `installer_status_set_bit` two
     parameters where `status_set_bit` has three: their models are
     garbage.  The fix is the Ghidra project's, giving each installer
     copy its twin's convention and prototype; with them, the installer's
     profile would need the primary's objects by type, its status object
     at `0x10ad06`.  So the names file now gives each installer copy
     whose instructions match its twin's, mnemonic for mnemonic, the
     twin's convention and prototype: 29 of them, `installer_psos_call`,
     `_enter_supervisor`, `_psos_pack_name`, the mutexes, the status
     system's and `installer_program_end_report` among them.  The
     installer's: 63 agree, 3 are skipped and 8 differ; 9 bridges agree.
     Left: the four exception entries run as functions,
     `installer_duarta_receive`, `_print_exception`, `_program_exit`
     and `_program_end_report`, and the status system's three, which
     wait for the installer's status object.  The status object is now
     a profile's (`Profile.status_object`), the primary's `0x102eb8`
     and the installer's `0x10ad06`, where `0x107a00` has
     `status_create`'s copy (`0x105d98`) keep the model; every profile
     gives its model, registers and error queue by type and name, and
     only the primary its other objects.  The three agree: installer
     66 agree, 0 skipped, 8 differ.  The rest are the names file's.
     `installer_trap_1`, `_trap_7`, `_default_exception` and
     `installer_spurious_interrupt` (`installer_return` was named for
     its `rte` before the boot vector table's vector 24 was seen to
     enter it) are the vectors' targets, byte for byte their primary
     twins, and get their `exception` convention.
     `installer_duarta_receive` returns nothing: its D0 is what is left
     of its overrun test, and its one caller does not read it.
     `installer_print_exception` calls `installer_printf_polled`
     through A2, so Ghidra guessed the variadic arguments: a `short` for
     the name's pointer and the address's low word, where the code
     pushes longs (`0x1005f0`, `0x10061c`); two `call` rows give them.
     And `installer_program_end_report`, and with it `_program_exit`:
     the label `installer_program_end_digits_end` (`0x10ab1f`), typed
     `uint8_t`, inside `installer_program_end_message`, had Ghidra clear
     the message's array, so the C wrote the digits down from a lone
     byte, out of its object, and gcc dropped the store of its `0xff`;
     the label is gone, as the primary has none at `0x100c21`.  The
     exception entries, and the periodic and DUART interrupts, vector
     targets as the primary's are, are written as assembly and left
     out: the installer's 68 agree and none is skipped or differs (63
     agreeing, 3 skipped and 8 differing before).  The
     primary's are unchanged.
     Then the idioms themselves, now that the harness checks every
     function after each change.  The C that compiles held 1546 in 224
     functions (`__ghidra_piece` 521, `CONCAT` 416, `__ghidra_bits`
     210, `__ghidra_as` 154; the assembly routines' `#if 0` readings
     aside).  A value pushed and popped around a call, which the
     decompiler took a byte at a time where the slot is also written at
     other widths, is a variable of its own, `pushed_n`, where the
     p-code shows the pieces are its own (1546 to 1263, most of
     `holdover`'s, `startup_pll`'s and `fine`'s); a character constant
     that is no text, `'\0'` and `'\x01'` for a byte's 0 and 1, is an
     integer; and every function that is C has Ghidra's default
     convention, so no longer reads "Unknown calling convention" (219
     to 8, the rest not C).  The counts are unchanged.  Left: the
     byte and word pieces of variables the decompiler types too wide
     (`__ghidra_piece`, `CONCAT22`, `CONCAT31` on `local_8`), the soft
     float library's own bit work, and then by hand.  Named, too: the
     signals the drivers send their tasks, by the firmware's messages
     (`SIG_SIQ`, `SIG_SOQA`; `types/psos-signals.h`), and a device
     register's bits where the decompiler reads it into a local first
     (`SCSR_TDRE`, `SR_RXRDY`).  Then types, in the shared headers
     where they can be, so the other images take them: a
     `time_of_day` the clock routines carry byte by byte
     (`datetime_add`, `time_next_second`), the GPT's TMSK and TFLG
     pairs as the words the firmware moves, and `error_queue_push`'s
     code an `int` whose low word it reads (1263 to 1157; the counts
     unchanged).  The clock's own times, the Oncore status's date and
     time, the reset record's, and the getters' and setters' are
     `time_of_day` too, and a structure the decompiler reads as an
     integer -- cast, compared, computed with -- reads its bytes
     (`__ghidra_aggregate_bits`), the tokens saying which operands are
     structures (1157 to 1098).  A conversion routine that shifts a
     time's bytes out of the whole (`hms_to_seconds`) keeps it an
     integer: the decompiler does not map those shifts to fields.
     Then four agents, one a subsystem, typed what they could read from
     the code, each change checked here in the harness: the clock's,
     NV records' and drivers' (`calendar_date`; byte and word results;
     a date or time local where the code works on its bytes; 1098 to
     1053), and the GPS side's (the snapshot's `satellite_sets`, the
     command log's time, the GPS time query's; 1053 to 1043, and
     `satellites_in_use`, whose prototype it now has, runs and agrees:
     1240), and the loop's (`format_si`'s double and char arguments,
     `efc_clamp`'s float result, the snapshot's time and interval
     status, byte and word results; 1043 to 1009), and the SCPI
     engine's (`scpi_number`, `scpi_token` and `scpi_port` for the
     lexer, its number and its port; 1009 to 1000).  A short its
     callers pass in a long slot is an `int`, not an `int16_t`, which
     Setup lays out at the slot's start: so typed, 106 more functions
     differed.  And a TFOM the code compares by its bits stays a
     `uint32_t`: as a `float` the decompiler writes `ABS(tfom)`, a
     conversion.
     Then the exporter: a double moved as its two longs, read, stored a
     constant or another double, is moved whole (1000 to 898), and a
     sign extension built of `CONCAT`s is a cast (898 to 859), and a
     call's double read back from a register pair, its copy in A0:A1
     or D0:D1 the decompiler took to be elsewhere, is kept in a
     variable (859 to 788).
     And `console_init`, which differed at `console_task_create`'s
     `create`: the kernel reads only D1's and D2's low bytes and the
     block A0 points at (`kernel_create`, `0x14dac`), so the high word
     the `clr.w` leaves and the block's address on each build's stack are
     no difference, and the model records what the kernel reads.  What
     then differed was the dictionary, 0x32 bytes short: gcc's
     `pforth_define_word` compiled one argument push where `hdac_write`
     takes two, its `dbf` loop around `forth_compile_code` reading the
     count back from a register the decompiler takes the call to change.
     That routine keeps D0, and what the `dbf` leaves there is the count
     less one in its low word, which the exporter now assigns after the
     call (1240 agreeing to 1241, 11 differing to 10).
     And a call's result has a name, `names/results.tsv`, which the
     variable it is assigned to takes where every value the variable
     holds is so named: `uptime = get_uptime()` at every call.
     And a function can be written by hand, in `third_party/firmware/rewritten/IMAGE/`, a file
     to a subsystem: the exported C keeps the decompiler's reading inside
     `#if 0` and includes the file, and the harness checks the hand's
     version as it did the decompiler's (`clock.c`'s time conversions
     first).
     Then the harness's own blind spots, found by an agent: cases a
     function's callers make that the generated ones never did
     (`function_check.py`'s `CALLED_FIRST`, `GIVEN_CASES`, several
     `SET_UP_REQUESTS`, and the stack around the declared arguments
     filled so that one the prototype leaves out shows).  They found
     `pforth_define_word`'s bug again on the C before its fix, and six
     more: `robin_store`, `validate_or_doaction` and
     `scpi_long_resolve_bound` read arguments their prototypes left out
     (now in them); `sqrt` and `log` on a subnormal loop forever under
     gcc, the decompiler's doubling loop overflowing a signed int
     (`-fwrapv`); `modf` on a NaN stores through an A0 two branches
     reach (the walk back now takes every path); and `frexp` on a
     subnormal calls `double_normalize`, which takes and returns its
     double in its own stack slots, as plain C, to be written by hand.
     The scan also found the exporter missing a pointer-typed `unaff_`
     register and A6, now read, and byte results with garbage above
     (`drtb_getc`, `sci_getc`, `coarse`, now `uint8_t`).
     And every integer type named by its size on the machine, the
     decompiler's `undefined4` and `uint` and C's `int` and `long`, is
     written as the `<stdint.h>` type it is here, `uint32_t` and
     `int32_t`; `char` stays for text.
     Then the rewrite, by agents a subsystem each: `records.c`, the
     EEPROM, the NV register, the parameter, identification, lifetime and
     calibration records and the two logs, 47 functions written by hand,
     each agreeing (`log_page_read` and `log_page_write` wait on their
     prototypes, which should take a `log_page *`).
     `libc.c`, the C library, 29 functions: `frexp`, the last that
     differed, agrees, normalizing a subnormal in C where the decompiled
     call of `double_normalize` lost the shift; and `pow` on a negative
     base, which the decompiled C got wrong (`ABS` of a `uint64_t`) and
     no case tried, does too, now a given case (`POWERS`).  Of 1252 run,
     1241 agree, 1 is skipped, 10 differ as known and none otherwise.
     Left: `doprnt`, `sprintf_put` and `vsprintf` on their prototypes,
     `call_atexit_functions` on the type of `atexit_functions`.
     `drivers.c`, 104 functions: the DUART's channels, their rings and
     self-tests, the SCI, the QSPI, the spool and deferred messages, the
     ADC and DACs, the health monitor and the PPS monitors; and the
     clock's five files, 84 more, the calendar and time arithmetic,
     leap seconds, the klok task, the time code, the start-up and the
     small arithmetic.  264 functions are written by hand, every one
     agreeing.  Found on the way: the decompiled C drops every write of
     the status register, the interrupt mask a driver raises around a
     DUART, SCI or QSPI access and restores, which the harness does not
     compare (the hand-written drivers write them with `move` to and
     from SR in a line of assembly); and the image's `duart_op0_set` and
     its kin pass 6 to `set_interrupt_mask`, which keeps only bits 10 to
     8, so the mask they set is level 0.
     Those six files, split by address range, are regrouped by subject,
     the bodies unchanged but for their file-scope names' prefixes:
     `clock.c`, the calendar, leap seconds and the clock record;
     `arithmetic.c`; `startup.c`; `os.c`, the mutexes, task reports,
     waits and exceptions; `klok.c`; `timecode.c`; and the drivers by
     device, `duart.c`, `sci.c`, `qspi.c`, `adc.c` (with the DACs and
     the EFC), `monitors.c` and `spool.c`; then `fpga.c` from
     `startup.c`, and `cpu.h`, the status register's helpers, interrupt
     levels, exception frame masks and watchdog service values, each
     constant that two files held merged into one.
     `scpi.c`, the SCPI engine, 122 more: the converters and parsers,
     the formatters, the lexer, keyword and suffix matching, the status
     system and error queue, and the call path, checked beyond the
     harness's cases on real command lines and numbers; 386 written by
     hand in all.  Its structures are its own for now and belong in
     `smartclock.h`; seven of its routines wait on prototypes that skip
     an argument's slot.
     Then the prototypes the rewriters found wrong, those whose callers
     are still the decompiler's: `doprnt` and `sprintf_put` take a
     `sprintf_output` and a `sprintf_put_function`, `atexit_functions`
     is 32 `atexit_function` pointers (Setup now parses an array of a
     named function pointer), the SCPI parsers and port methods take
     the slot they skipped, `duarta_isr` returns a byte, three print
     routines are variadic, and the harness, which does not run a
     variadic function, runs three fewer (1241 to 1238).
     Then the shared values named: `status_condition_code`, the
     conditions `status_happening` raises, by the Z3801A guide's status
     bits; `fpga_configuration_index` and `fpga_chip`, which the FPGA's
     loader and checks take, the ROM table `fpga_configurations` and
     `pll.fpga_configuration`; a `mutex`; the klok lists as
     `klok_entry **`, the log pages as `log_page *` and `qspi_transfer`'s
     `qspi_buffers *`, the hand-written callers' casts gone.  The C is in
     Google's style with braces everywhere (`make format-c`).
     And two more subjects by hand: `fpga.c`, the FPGA's loading,
     readback checks, registers and self-tests (19 functions), and
     `ti.c`, the time-interval counter and interpolator (25); 430 in
     all, every one agreeing.  `interpolator_verify` stays the
     decompiler's, returning on its failure paths a register its caller
     left.

  Costs: the wrappers, the harness and the link; then the long tail of
  decompiled functions that are wrong in ways only running them shows.
  A whole image is proven only on a unit or a model of the board.

- **Editing notes from the web view**, through the daemon socket.
  Needs POST bodies in `smartclock-http`, `/api/notes` returning each
  note's `id`, and edit requests keyed by it.

## Open questions

1. **What the firmware leaves open.**  The mode suffixes are now read
   from the firmware: the loop's stage byte and its sub-states choose
   them (`docs/screen-format-strings.md`, "Mode suffixes").  What
   remains is in `docs/firmware/README.md`, "What is not established".

2. **How far the z3801 dialect is confirmed.**  Of the z3801 entries,
   62 are `evidence = "hardware"`, 19 `firmware` and one `manual`;
   those 20 stay unconfirmed until a receiver answers them.  All but
   one of the 20 change settings, which no probe sends; the query left,
   `:PTIMe:GPSystem:POSition:SURVey:PROGress?`, answers only while a
   survey runs, and the bench Z3801A refused it in position hold
   (-221).  The
   keyword table they were checked against is `docs/scpi/z3801-keywords.md`.

3. **Managing the receiver's position.**  Not now.  A receiver told
   where it is goes straight to position hold and serves time; a survey
   leaves the 1 PPS invalid while it runs.  A Z3805A on an antenna a
   58503A had already surveyed spent an hour unable to serve time;
   asserting the position by hand took two commands.  The receiver
   already stores a held position, so the work is in controlling it:

   - set a fixed position, from the configuration or by hand;
   - control the startup mode -- survey on power-up or hold;
   - start the receiver's own survey;
   - run a 24-hour survey in the daemon, averaging the receiver's fixes
     far longer than its own survey does, and assert the result;
   - take a position from gpsd.

   Constraints, whatever form it takes:

   - Each is a control command, so it needs the same opt-in and must
     not fire under a daemon started read-only.
   - The position is non-volatile, so a stale value outlives the
     configuration that set it.  Holding it means turning off
     survey-on-powerup, or the next power cycle discards it; with both
     set, a receiver whose antenna has moved never notices.  Whatever
     asserts a position must be able to release it.
   - Writing the antenna delay forces a holdover, and so does writing
     the position, through a different cause: on the bench 58503A,
     rewriting its held `:GPS:POSition` unchanged took over 5 s to
     answer, then `Holdover: GPS 1PPS invalid` for about a minute, and
     two more to lock (`docs/screen-format-strings.md`).  In the
     firmware the setter only stores the position and leaves a request
     the power-up machine reads (Z3816A `FUN_00049da2`, `0x4a774`).
   - Datum.  Every image selects WGS-84 (`@@Ao` 49), so latitude and
     longitude need no conversion.  Heights do: the 58503B surveys,
     holds and reports the Oncore's ellipsoid height (`@@As` type 0),
     the Z380x and 58503A its MSL height (type 1), and MSL is each
     device's own geoid -- on the bench the Oncore's separation was
     −28.4 m, the u-blox's −30.0 m and gpsd's −32.3 m
     (`docs/firmware/gps.md`, "Requests").  So a position should be
     carried as ellipsoid height and converted to an MSL receiver with
     its engine's own separation, which its decoded `@@Ba` record holds
     (the two heights' difference) -- readable from RAM with
     `smartclock-cli read-memory`, not over SCPI on the Z380x.

4. **Whether acknowledging from the monitor is wanted.**  Reading an
   event register says which bit latched rather than which group, and
   clears the alarm: right for a deliberate acknowledgment, wrong on a
   timer.  Nothing needs it yet.

5. **Maybe: a Host allowlist for the web view.**  `smartclock-http`
   checks neither `Host` nor `Origin`, so a page in the operator's
   browser can rebind its own name to the web view's address and read
   `/api/status`, position included, or force screen reads at 1.5 s of
   link each.  Likely shape: `--allow-host NAME`, repeatable, loopback
   always allowed, a non-loopback bind refused without one.  Deferred:
   no access-control work for now.

## Known defects

Known and unfixed, each because the fix is not yet worth its cost.  The
threat model is a careless operator on a single-operator machine, not
an attacker, so two things are not defended against:

- Socket permissions are the whole of the authorization, and over TCP
  there is none: anyone who can reach the port may connect.  On
  Windows, where TCP is all there is, a program that takes
  `127.0.0.1:9978` before the daemon does is the daemon to every
  client asking there by default.
  A connection ends at its first line that is not a request, so a web
  page posting to the port does not have the request in its body
  answered.
- A client that connects and only reads holds one of the sixteen slots
  while connected; a watcher does exactly that, so it is not refused.
  One that stops reading is cut off once a write has waited
  `client::DEADLINE`, and when either half of a connection ends the
  socket is shut both ways.  A client half-closing its sending side releases its slot
  and stops the push thread, and is tested.
