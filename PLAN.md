# Plan

HP / Symmetricom SmartClock GPS receivers taken apart -- their firmware,
disciplining loop and boards -- and the tools that monitor them: a Rust
library for talking to them over serial, a logging daemon that runs as
a system service, a TUI, and browser and Prometheus views.  A GUI is
not planned.  This file records the decisions that are not obvious,
each with its reason, and the open work with no other home.
`README.md` says what the project is, `docs/running.md` how it is
deployed.

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

The crates are listed in `docs/monitoring.md`.  `smartclock-log`
holds the log's schema and every query that reads it, so the daemon,
monitor and web view cannot drift apart.  The library, bottom up:

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
Z3805A and Z3817A answer the z3801 one.  An operation a dialect lacks
returns `Unsupported` without reaching the device.

An entry's `models` list binds only the models in the table's
`complete` list, whose lists have been audited against a receiver: one
of those is polled only for what names it, and a typed read of
anything else returns `NotListed` unsent.  The Z3817A is the first:
its 3752 tree lists keywords whose handlers are stubs answering -113
(`:DIAGnostic:ROSCillator:CURRent?` among them), so a tree alone does
not say what it answers.  Every other model is asked everything its
tree has, as before the lists bound anyone.  The lists were written as
provenance -- the manual's models, the unit a discovery was swept on
-- and binding them everywhere would have dropped the Z3805A's FFOM,
EFC and registers, which it answers but no list names; a model joins
`complete` once its lists are audited.  An entry naming none but
`complete` models is theirs alone, so a command only they have draws
no -113 from the rest.  Only the polls and the `Device` reads
are gated: the journal, the typed control handle and a client's own
command go by dialect, as the operator's choice.

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
the receiver that wrote them.  A poll's refusal never reaches the
queue's drain, since the session reads its entry to explain it, so the
log thread writes each code a receiver gives each query to
`receiver_error` once, with the query: once, because a stub refused
every pass would otherwise bury the queue's own entries, and a
restart does not repeat it.  The journal runs on the thread that owns
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

### One refused query costs one field

A step reads each field on its own.  What the tree lacks (`Unsupported`)
and what the receiver declines on state (-221, -230) are absent.  Any
other refusal, -113 for a keyword whose handler is a stub or one whose
code a full queue lost, is absent too, and the query is named in the
tier's `failed` list, which `last_error` falls back to; the step goes
on.  A refusal used to fail the step: a Z3805A's missing TFOM froze
the fast tier, and a Z3817A's stubbed oven current kept the medium tier
from ever completing.  A refused field does not make the snapshot
stale, since nothing in it is old.  An unreadable reply, a link failure
or a swap still fails the step, which publishes nothing it read: the
first is a defect to see, the others make what follows untrustworthy.

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
  and is what you run.  The shared workflow does not build the API
  documentation, so `ci.yml` runs `make doc` as a job beside it, with a
  rustdoc warning an error; `make ci` runs it too.  The browser and
  firmware checks need Node, Chromium, an m68k gcc and uv, so they are
  jobs of their own.
- The firmware job runs on pull requests and pushes to `main`, not on
  a branch's own pushes, which ran it a second time, at once, beside
  the pull request's; a branch without a pull request runs it by hand
  (`workflow_dispatch`).  On a pull request its longest step,
  `report-decompiled`, runs only where anything it reads changed
  against the base (`tools/ci/report-decompiled-needed`, the one list
  of its inputs).  A push to `main` and a run by hand always run it:
  `main` stays checked in full as the backstop, so a skip never trusts
  a commit whose own run may have failed, and a change outside the
  list that the step does read, or drift outside the repository (the
  runner image, the toolchain, the Python dependencies), is caught at
  the next merge.  Guarding `main` too, against the commit before the
  push, was weighed and declined.  The job still runs and passes, so a
  required check is never left pending.  Cost: `main`'s run mostly
  re-proves what the rebase-merged pull request tested; the checkout
  fetches the whole history; and a branch is not checked until it has
  a pull request or is run by hand.  The four
  images' function checks run at once (`make -j`), each one's output
  kept whole in a log of its own.
- HTML pages are published on GitHub Pages by
  `.github/workflows/pages.yml`, which `make pages` builds.  Generated
  ones (the board atlas, the block diagram, the SCPI trees, the
  pForth words and the coverage by model)
  are built from their sources and not committed; hand-written ones (`loop.html` and the
  bench records) are copied from their sources in `docs/pages/`.  A
  generated page's committed copy needed a drift check and a
  regeneration commit for every change to its data, and could be viewed only through htmlpreview.github.io;
  a Pages URL is the repository's own and permanent.  The build runs
  on pull requests too, so a source that breaks a page fails before it
  lands.  Each page that was once committed leaves a forwarding page
  at its old path.  The hand-written ones moved too, so that every
  page has one kind of URL and none needs htmlpreview's workarounds.
  Generated Markdown stays committed: GitHub renders it in place.
- `loop.html` and `docs/firmware/loop.md` are split by job: the page
  explains the loop (the law, the symbols, its poles and damping, the
  aging feed-forward, start-up as a story), `loop.md` holds the
  reference (the addresses in every image, how the images differ, the
  soft-float constants, how it is checked) and points to the page's
  sections rather than restating them.  The loop's constants by model
  are one file, `docs/firmware/loop-constants.json`, from which
  `tools/firmware/loop_constants.py` writes a table into each,
  between markers, the rest of both written by hand.  The page's
  table is committed with a drift check, unlike a generated page,
  because the page is hand-written around it and `loop.md`'s copy
  must be committed anyway; one source and one check cover both.
- The site's landing page, `index.html`, is generated by
  `tools/pages_index.py` from the pages' own head tags (title,
  description, `smartclock-group`) and the `Makefile`'s `PAGES` order
  and its `NEW_PAGE` and `FEATURED_PAGE`, each in a box of its own, so a page joins it, or is set
  apart as new or featured, without the
  landing page being edited by hand.  A page set apart is listed in its
  group too, so a group shows all its pages whichever is set apart:
  listed above only, the featured loop page once left its group with only the pForth page in
  it.  The firmware group, "Firmware", holds the loop
  page, the images' pForth words and SCPI trees and the coverage by
  model: SCPI is the
  firmware's, and had a group of its own only for its one page.  Its hero is the 58503A's front
  panel drawn as SVG to scale from photographs, not a photograph: a
  drawing is a few kilobytes, follows the page's fonts, shows UTC live,
  and carries no bench clutter or stickers.  Its intro names the
  FPGAs' decoding since the FPGA write-up's page joined, set apart as
  new beside the featured loop page.
- The landing page's last box, "In the repository", points to the
  README and then to each of the project's tracks by its hub
  document (`DOCUMENTS` in `tools/pages_index.py`): the firmware
  notes, `docs/tooling.md` and `docs/monitoring.md`.  Reverse
  engineering, its tools and the monitoring programs share one
  repository, and a reader arriving from the pages needs to see
  which is which before the README's detail.  A hub lists one line
  per program or tool and links its document, rather than repeating
  it; a new track is one more hub and one more entry, with nothing
  reordered.  `PLAN.md` is not in the box: it is a record of
  decisions, not a way in, and the README links it.
- Every other page gets a breadcrumb back to the landing page
  (SmartClock, its group, its title), put in by `tools/pages_index.py`
  as it writes the landing page, from the same head tags, rather than
  by each page's generator or source: one place to change it, and the
  hand-written sources in `docs/pages/` stay as written.  It goes at
  the top of the first element in the page's `<body>`, its content's
  column, so it lines up with the content whatever the page's width
  and padding; the build fails on a page not built that way.  Its
  styles are inline and inherit the page's colors and font, so it
  follows each page's theme without knowing it.  A rerun replaces it.
- The site's icon, `docs/pages/favicon.svg`, is the 58503A's GPS Lock
  lamp lit on the front panel's dark gray, in the hero drawing's
  colors: a big lamp and nothing else, so it reads at 16 px.  It is
  SVG, one small file every current browser takes, with no
  `favicon.ico` beside it.  `tools/pages_index.py` copies it into the
  site and links it from every page's `<head>` in the same pass as the
  breadcrumb, for the same reason; a page that declares an icon of its
  own keeps it, and a rerun adds no second link.
- A page can be published but unlisted: named in the `Makefile`'s
  `UNLISTED_PAGES`, it is built and served at its URL, with its
  breadcrumb, and left off the landing page, so a page of only the
  bench's interest stays reachable by its links without a slot on the
  landing page.  It is chosen in the `Makefile`, as `NEW_PAGE` and
  `FEATURED_PAGE` are, rather than by a tag in the page, so that which
  pages the landing page shows is decided in one place; the page keeps
  its group, which its breadcrumb names.  The three-receiver sky
  comparison is unlisted.
- A page can be marked as a work in progress: named in the
  `Makefile`'s `WIP_PAGES`, it gets a "Work in progress" ribbon across
  its top right corner and a WIP tag on its landing page entry, so a
  page can be published while it is still being checked without
  reading as finished.  `tools/pages_index.py` inserts the ribbon in
  the same pass as the breadcrumb, for the same reason, and a rerun
  replaces it rather than doubling it.  It is an amber band with dark
  ink, which reads on every page's light and dark themes without
  knowing their tokens.  In a window narrower than the landing page's
  wide breakpoint it is a banner under the breadcrumb instead, so it
  never covers a title or makes the page scroll sideways.  It is chosen
  in the `Makefile`, as `UNLISTED_PAGES` is.  The block diagram is a
  work in progress.
- A page that sets a table side by side across the firmware images, a
  row per path or word and a column per image, filtered as the reader
  types, is a matrix page: its generator prints the table's data as
  JSON, kept out of the site, and a Python renderer builds the page
  from it with `tools/pages_matrix.py`'s shell, style and filtering
  script.  The SCPI trees by model are one, their data from
  `smartclock-cli dump-scpi --models --json`; the pForth words by
  model another, from `tools/firmware/pforth_words.py --json`, the
  reading that writes each image's `IMAGE-words.md`, not the CLI's
  `dump-pforth`, which reads names and code addresses only and no
  flags or stack effects.  It needs uv in the Pages build for the
  script's Capstone.  Both pages are in the firmware group.  A table scrolls
  inside its own box and, in a narrow window, holds its first column,
  so the page never scrolls sideways.  The CLI once wrote the
  page itself from a template of its own, which a second such page,
  its data from a Python tool, could not share; one shell keeps the
  pages alike and fixes each once.
- The coverage by model is a table of how far each
  firmware image is covered, a row per image and a column per kind of
  work, from `docs/coverage.json` by `tools/coverage.py`, in the
  firmware group.  A hand-kept table of what is done goes stale
  silently, so each column names the evidence a Yes rests on (a
  generated document, a heading, a Makefile list entry such as
  `REPORTED_IMAGES`, a data file's entry, `dialect_for()`'s rule, the
  command table's `complete` list), and
  the build fails on a Yes without it and on a cell that is not a Yes
  with it.  Work that exists only off main shows as WIP and names
  nothing of it; a model with no image, the 59551A, has a row that
  says so across every column, Daemon support included: its daemon
  support has never been tried, and the build fails if an image of it
  turns up.  It quotes no counts.  It takes the matrix
  pages' style, but is a static table: a row per image needs no filter.

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
- **Completeness is checked by a link**, built: `make check-link`
  (`tools/firmware/link_check.py`, in CI) links each image from the
  export's declarations, `rewritten/` and the assembly pieces alone,
  the decompiled C left out, requiring every function of the export's
  `.conventions.jsonl` (`symbols.ld` holds only addresses the assembly
  names), unreferenced ones too, through `ld --require-defined`, so a
  missing one fails the build, named.  The link compares no bytes; it
  shows that nothing is missing, and the function check that what is
  there agrees.  Its pieces lie at their addresses, `rebuild_link.py`'s
  layout, since their short branches reach only there.  The docs state
  completeness only from its summary line, whose figures
  `check-report-figures` checks.  Was a count of the export's markers,
  which once read an image as all hand-written with some 400 functions
  still the decompiler's.
- The function check fails (and CI with it) on any failed function.
  Was a measure that exited zero; changed so that a regression cannot
  pass unnoticed.  A known difference is one whose cause is found and
  is not the C's (`function_check.py`'s `KNOWN`), and counts only while
  every difference of the case matches its pattern, so it hides no
  other.
- The function check compares what zeroed RAM and the effects alone
  let pass: the text pointers a function's own calls pass, by what they
  name; a creator's create and start, its name and entry, finding no
  task in half its cases, creators found by their calls; and a cell
  read or written in place of another, in each case run again with
  the cells one copy alone read, or either wrote and neither read,
  poisoned.  Was blind to all three: the Z3815A's first link run's C
  passed with the 58503A's log texts, its flag cell and a task's name
  and entry, found by hand.  Only pointers are compared, those both
  copies pass in the order first passed; gcc's copy is compiled for
  the check with its inlining of small and called-once callees off and
  a static helper's calls are its caller's, since a hook missed what
  gcc put in line (2026-11-05: a log text through a static helper,
  two texts logged the other way round, agreed until then); and a
  preset, hook or record naming what the image's C lacks fails the
  function rather than leaving its cases unset; no cell both
  copies read is poisoned, since a poisoned path reaches addresses
  that differ between the copies (a frame's, a message's).  Poisoning
  was of the first case alone, which reaches only the path zeroed RAM
  takes: the Z3815A's `counter_read` and `counter_calibrate` read and
  wrote the 58503A's cells of a configuration laid out otherwise, on a
  path the first case never took, and agreed.  Every case is now
  poisoned, but one in which the copies touched the same cells, at
  the cost of a second run of each case that differs.
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
  gcc's copy goes more than 256 bytes deeper than the image's (192
  until the check's link stopped inlining callees), or past
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
  setters with each image's own table; the Z3817A 3752, the fifth,
  sent back, before it joined, each function it lacks, the hex DAC,
  the interpolator and its self-tests, the :OUTPut:PIN1 handlers and
  the root's unsupported entries among them, and each its code
  changes, the FPGA's loader and readback check, the QSPI's FPGA
  writes, holdover's TI statistics and the self-tests' run over
  their list, one entry shorter, among them, one copy in each
  of the four images' own C, since `common/` is all or nothing).
  Each seeded image's
  `check-layout.c`, which rule 2 compares once the image shares
  `common/`, the Z3815A 4010's too, asserts its layouts at its
  own addresses, derived from its code and export
  (`tools/firmware/layout_addresses.py`, run by `make check-common`),
  and every field shared C reaches through a global is asserted: the
  58503A's began as the Z3805A's addresses, copied, which let the
  shared `gps_receive` count errors at the Z3805A's offsets unseen.
  A check's record must also lie where its address puts it, in a
  global the export types as that record or as one holding it, so that
  a check left at a base's addresses, there inside another global, no
  longer passes for the image's own (the Z3815A's `gps_statistics`, the
  clock record and others had); nor one inside a label the names leave
  untyped, but for one named with why (`UNTYPED_HOLDERS`), as the base's
  checks inside the Z3815A's status snapshots went unseen until they
  were typed.  An aligned instruction is evidence of an address only where it
  names one inside the image's global of the base's global's name: the
  Z3817A's SCI routines, aligned with the Z3815A's for DUART channel B,
  spoke against three checks of records they do not touch.  A rule in
  the derivation, not a table of excused checks, which was tried and
  dropped (2026-11-13).
  A record that differs in any image is entirely per image, with every
  shared function that names it: `common/` holds no shared head of a
  per-image record (decided 2026-11-06, backing out the splits of the
  boot code's self-test results and the GPS request;
  `docs/firmware/rewrite.md`, "Shared C").  The split enums' heads
  (`status_scpi.h` and the rest) stay shared, by decision, as shared
  code names their members; `qspi_device.h` and `fpga_bit_index.h`
  went back to each image's `smartclock.h` when the Z3817A 3752's
  split left no shared code naming their members, the Z3817A's own
  enums sharing none of them.
  A more aggressive commonizing pass, which revisits them, is planned
  for later.
  A function the diffs class identical or relocated in every image
  is not thereby shared: it moves only if gcc gives the same code for
  it in each, which most such functions miss for a field offset, a
  static helper or a callee of the image's own.  The owner declined
  relaxing the rule to one C text over per-image types (2026-11-03),
  so those stay per image.
  Rule 1's masking hides a callee too, so `check-common` compares the
  routines each image's code of a shared function calls with those its
  C names, each exception a named entry with its reason: the Z3815A's
  five that went back first (`docs/firmware/rewrite.md`, "Shared C").
  The export is one translation unit, in which a file could name what
  a file before it defines; the rule is that none does, and `make
  check-alone` compiles each file alone after the export's
  declarations and headers so that one that does fails (decided
  2026-11-03, after loop run 3 found files naming others' statics).
  An unfinished image (`UNFINISHED_IMAGES`) is one whose C is not yet
  all hand-written, so that it has no link check; its function check
  may fail none (the Z3815A 4010's since its rest wave, 2026-11-04,
  but for the two the widened function check found, written in its
  second link run),
  and it joins `REPORTED_IMAGES` once its link check passes, where
  before a report with none failed had forced the move.  The Z3815A
  4010 joined it in its fifth link run (2026-11-05), leaving
  `UNFINISHED_IMAGES` empty, the mechanism kept for the next image;
  the Z3817A 3752 was one from its first export (2026-11-11).  Until
  it shared `common/` its export took common/'s headers and bindings
  alone (`SmartClockExportC.java`'s `headers=`) and the C it needed to
  compile, and the C its function check found the decompiler's wrong
  for, was copied into its own folder, as the Z3815A's first export
  copied its base's; such an image does not count as sharing
  `common/` for `make check-common`.  Its waves wrote the rest of its
  functions, each from the closest base's copy or the decompiler,
  read beside the image's instructions, its own types, headers and
  names where the bases' did not fit, never bent to the Z3815A's,
  until `make report-link IMAGE=z3817a-3752` listed only what
  `common/` defines and fits.  The join (2026-11-13), a wave of its
  own, split out of `common/`, into the four reported images' own C,
  every function the Z3817A's diff against the Z3805A, the base
  `check-common` compares by, finds not identical or relocated, and
  those whose data differs there (holdover's TI statistic, the
  self-test list, the counter's preset, the region report's
  heading, the DUART's overrun message); dropped the image's copies of what `common/` defines; and
  named in its names file what `common/`'s C names, where the seeding
  had left it unnamed or named for the Z3815A's twins
  (`docs/firmware/z3817a-3752.md`, "Joining `common/`").  Its link
  check then passed, and it joined `REPORTED_IMAGES` the same day,
  leaving `UNFINISHED_IMAGES` empty again.
- **Per-image tool tables**: what the function check gives an image
  other than the Z3805A 3543B is kept in tables keyed by image or held
  by its profile (`harness.IMAGE_PROFILES`, `IMAGE_KNOWN`,
  `IMAGE_GIVEN_CASES`, `IMAGE_STATES`, `IMAGE_COUNTED_LOOPS`, ...), each
  entry cited in that image's code, never a base entry widened, so a
  port's quirks show as that port's and the Z3805A's figures cannot
  move.  `make check-comment-addresses` makes a copied address show.
- **A copied ROM pointer is checked by what it points at**: the function
  check compares a ROM pointer by its target or masked, so C ported
  from another image's can keep that image's address or text and still
  agree.  `make check-rom-pointers` (`tools/firmware/rom_pointers.py`,
  in CI for every image with an export) holds each pointer literal of
  the hand-written C to a string's or a label's start, and each operand
  where an image's copy of a function both write by hand differs from
  its base's to the same kind of object and the same text, unless the
  image's C gives its own object by text, constant or label.  It began
  as a report-only audit of ported C, which caught such addresses; a
  check keeps them from coming back.  Its exceptions are rows of
  `analysis/IMAGE/rom-pointer-allow.tsv` with their reasons, as the
  magic numbers' are.
- **Seeding a port's names**: until a port's own naming starts, its
  names and modules files are `tools/firmware/seed_names.py`'s output,
  and `make check-image-diff` fails when they fall behind the diff.
  Once edited by hand, `check-seed-names` checks only rows still marked
  seeded; a hand row drops the mark.  A RAM label the rules leave
  unplaced is placed by a hand row citing the port's references, never
  by a new exception in the rules.  A single reference from a relocated
  function was tried as enough to place a label and dropped: relocated
  code may load another cell of the same record (`gps_receive`,
  `0x4c124`), so it placed labels wrong.  A port's C carries its own
  image's constants (model, tag, revision).  A lead, a pairing changed
  below the carrying threshold, is seeded under its base's name, its
  note saying it is a lead and at what similarity.  It was seeded with
  the port's suffix, `gps_init_58503a`, and that was dropped: the
  suffix recorded lineage, not what the function does, and read as the
  58503A's inside the Z3815A, which carried it.  No function is named
  for an image: the rows added by hand were renamed for what they do,
  a difference from another image's function of the name going into
  the note, and `make check-plain-names` fails on a suffixed name in
  the hand-written C or a names file.
  A port whose base lacks code another image has takes a second base
  (`Port.second_base_folder`): the functions the base's matches find
  new are seeded from the second base's matches where those carry
  them, with its ROM tables at addresses and names free, each row
  noted as the second base's (the Z3817A's Oncore code from the
  58503A, 2026-11-11); the Z3815A had named its SCI routines from the
  Z3805A by hand rows instead.  A primary function matched inside the
  installer's records, the installer's copy's bytes, has no row.

### The FPGA designs as netlists

`tools/fpga/` decodes the XC3042 bitstreams through the XC3000 viewer
(<https://github.com/Arisotura/xc3000>), in node or headless Chrome, and
writes structural Verilog over our own models of the data sheet
(`docs/firmware/outputs.md`, "Netlists").  The viewer has no license,
so it is never copied into the project; the tools load it from the
clone `tools/fpga/Makefile` makes at the pinned commit
(`tools/fpga/decoder.mk`); `netdump.js` corrects the viewer's faults.  The netlist
stays generated, in `target/`, since it needs the viewer to make.

The decoder is held to XACT itself, not to what the firmware expects:
XACT 6.0.1 runs headless in DOSBox-X (`tools/fpga/xact/`), and each
test design's decode must match the routed LCA XACT built it from.
Our test designs, their LCAs and bitstreams are committed, so CI checks
the decoder with no XACT (`make check-fpga-decoder`); XACT and its
XAPP designs are not.  The SmartClock bitstreams look built with
MakeBits's tie-down (inferred, `docs/firmware/outputs.md`), which a
bit-identical rebuild must reproduce.

Every figure the FPGA docs quote from a simulation is checked:
`make -f tools/fpga/Makefile check` covers the fast path (sizes, read
ports, Icarus and Yosys diagnostics, pads, the 58503A/Z3801A's blocks
against the Z3805A's, the 58503B's paired blocks as functions) and runs
in firmware CI, and
`check-readings` the long runs, against
`tools/fpga/ref/<image>.<run>.readings`, with `tools/fpga/probe.v`
measuring the internal nets the docs quote, model hazards under 5 ns
removed.  The long runs take minutes a simulated second, so `check-readings` is outside
`check` and CI; it is run by hand when a testbench, the models, the
netlist generator or the decoder pin change.

`check-decoder-upstream` decodes every design again at the viewer's
upstream pull request #3 and requires the same netlists; it needs the
network, so it too is run by hand, when `netdump.js` or the pin
changes.

The illustrated explainer is generated, not hand-written, and not
committed, like the board atlas: `tools/fpga/explainer.py` takes every
figure from those checked references and inlines `vcd_wave.py`'s
waveforms.  The top Makefile's `fpga` writes it into `$(PAGES_DIR)`
for GitHub Pages, https://charlieh0tel.github.io/smartclock/fpga.html,
and `check-explainer`, part of `check`, fails if it cannot be
rendered.  A committed copy would only repeat what its sources and
the build already check.

The detailed diagrams, FPGA detail, are generated the same
way, by one generator for all six designs (`tools/fpga/detail.py`,
`detail_draw.py`), never drawn by hand per design, so the six share
one grammar and their differences show: the whole chip by function
blocks, and the time-interval counter gate by gate.  Where a block
sits is layout, one table a family, since positions are not facts; what
it says, its counters' moduli, its equations and the nets drawn between
blocks come from the netlist and the checked references, and a wire
the netlist lacks fails the build.  The drawings are kept legible by a
check, not by eye: wires run in channels of their own, and the build
fails on a wire through a block, two wires along one line, or text on
text, on a box, on another wire or out of its box (`tools/fpga/sheet.py`).
A click opens a drawing full screen at its own scale, to pan and zoom,
through the block diagram's zoom, shared as `tools/zoom.py`; without
JavaScript the page is unchanged.  What an element is and what its trace says
of it are tables (`tools/fpga/detail_designs.py`), each element with
its trace's confidence word and section; which pins, clocks and
other elements it connects to is read from the decoded netlist, so a
table cannot draw a wire the netlist lacks, and a pin in use without
a role or an output no element drives fails the build.  The top
Makefile's `fpga-detail` writes it, https://charlieh0tel.github.io/smartclock/fpga-detail.html,
and `check-detail`, part of `check`, fails if it cannot be rendered.
It shares the explainer's references, styles and page shell through
`tools/fpga/page.py`.

### The GPS engines' wire formats are one crate

`smartclock-gps` holds the wire formats of the GPS engines inside the
receivers: Motorola's Oncore binary `@@` messages and the Z3815A's
NMEA with Furuno's `$PFEC` sentences, a module each
(`docs/firmware/gps.md`).  The monitoring tools reach a receiver by
SCPI and never see that link, so nothing else in the workspace speaks
either format; the crate is for a tool that talks to an engine or
stands in for one.

- **Scoped to what the receivers use.**  A message or sentence is in
  its tables only if some image's firmware sends or decodes it; it is
  not a general Oncore or Furuno library.  Each row cites the manual
  page that gives its layout, and tests hold the tables to the images'
  own: the Oncore lengths to `docs/firmware/oncore/*.txt`, the NMEA
  sentences to the two tables in the Z3815A's image.
- **`no_std`, and no allocation**, so one copy of it runs on a
  microcontroller, in a browser and on a host.  An encoder writes into
  the caller's buffer; a deframer owns a buffer of fixed size and lends
  each frame out of it, an Oncore one's size a constant derived from
  the longest message in the table: the whole frame, header through
  trailer, as the table and the manuals count a length, not the data
  alone.  A host build proves neither,
  having `std` to fall back on, so `make check-no-std`, which CI runs,
  builds the crate for targets of each kind, the `Makefile`'s
  `NO_STD_TARGETS`, the least of them a Cortex-M0.
- **A small build does not pay for the table to ask one thing.**  The
  Oncore table is every message with its lengths, and a build that
  looks a row up carries all of it in flash.  Whether an ID is a
  six-channel or an eight-channel message is a `const` function of the
  ID alone (`MessageId::channels`), which names the per-channel IDs;
  the table's rows take their form from it, so the two cannot differ.
- **An Oncore deframer's lengths are a type parameter, a function
  unless named.**  A frame's ID is all it says of its length, so a
  deframer is given the lengths, and which messages an engine has may
  be known only at run time.  A function pointer captures nothing, so
  lengths that depend on that needed a function for each case.  They
  are now a trait (`MessageLengths`) every function and closure
  implements and a caller's own type can, and the deframer's second
  parameter, which defaults to the function pointer: `Deframer<N>` and
  `Deframer::new` are what they were.  A trait object was not used:
  it would be a borrow in the deframer's type and an indirect call
  where a caller's type costs neither.  A constructor that takes any
  lengths is const too, so a deframer of a caller's type is still a
  `static`'s field, built before the engine is known and given its
  lengths after (`set_lengths`); a capturing closure's type cannot be
  written out, so it serves only a deframer that is not a `static`.
  The cost is a copy of the deframer's code for each lengths type a
  build uses.
- **Framing first:** IDs, lengths, checksums, encoders and streaming
  deframers.  What turns an engine's knowledge into a message's bytes
  is a module beside these: `engine`, for the Oncores.  The Furuno's
  is later work.
- **A model stands in for an Oncore** (`engine`): a six-channel VP, an
  eight-channel VP or a UT Plus, chosen when it is made by an identity,
  a `static` holding the model and the `@@Cj` text, so one build serves
  them all and a tool picks by configuration.  It takes the receiver's
  bytes and a record each second and hands whole frames to the caller's
  output.  It has no clock, no link and no source: the caller calls it
  at each 1 PPS with the record its source gave, and it counts those
  calls for the rates of its continuous messages, so a record whose
  time jumps neither doubles a message nor skips one.  The one answer
  that comes late, a self-test's, it holds until the caller's time,
  passed in with every call, reaches a deadline it reports.
- **The model computes nothing physical.**  Dilutions of precision, a
  position in Earth-centered coordinates and magnetic variation need
  floating point or tables a Cortex-M0 should not carry, so a record
  has a block of derived values a caller with the means fills in, and
  the model sends zeros for what is absent.
- **The model's command framer is its own**, not the deframer run the
  other way: an engine resynchronizes a byte at a time, so a whole
  command straight after the start of a broken one is taken, where the
  receiver's firmware, which the deframer follows, would lose it.
- **The model is held to recorded exchanges**:
  `crates/smartclock-gps/tests/vectors/engine/` holds scripted
  sessions, the bytes sent to an engine and the bytes it must send
  back, second by second, which `make check-engine-vectors` and the
  crate's tests replay against the model byte for byte.
  `make engine-vectors` rewrites the answers from the model, for
  review as a diff, when a change to them is meant.  Their sky is made
  up, and their position is the one the firmware's own script sets
  (`docs/firmware/gps.md`, "Requests") or one a command of theirs
  sets.
- **A VP's identity text is made up; a UT Plus's is a published one**,
  a `@@Cj` answer posted whole in a public bug report and matched in
  form by another on the time-nuts list, both cited where the text is.
  A second UT Plus identity, of a firmware late enough to be sent
  `@@Gj`, is made up.
- **One record of a second, whatever the engine** (`record`): the
  time, the leap second, the fix, the sky, the almanac and the antenna,
  naming no message.  A field is in it only if a message some image
  decodes carries it, and it is held in the integer unit that message
  sends (`units`), so nothing needs floating point.  It holds twelve
  satellites, the slots of `@@Bb` and what three `GSV` sentences carry;
  more in view are dropped lowest first.  Its size is bounded by a
  constant a compile-time assertion checks on every target, and it is
  built in a `static` and filled in place, never passed by value.
- **Time is a count of GPS seconds** (`time`) with no rollover and no
  leap seconds; a week number, a time of week, a UTC date and 23:59:60
  are derived from it where a message is written, with the offset and
  the leap second the record carries.  All of it is `const`.  The count
  is signed: a clock set behind UTC reads a date before the start of
  GPS time in the hours after it, where an engine with no time starts,
  and the difference of two times is as often negative as not.  Its
  number, the UTC count's, a week's and a time of week's are private:
  a time is made from a date, a week and time of week or a typed count
  of seconds, and two times differ by a length of time, so a GPS second
  is not taken for a UTC one nor a time for a length.  The operators
  stop at the count's ends, where the `checked_` functions say `None`.
- **A source gives each second's record, and the tool's clock drives
  it** (`source`).  A tool that stands in for an engine asks its source
  for the record of the tick it has reached; no source keeps a clock,
  and none carries a pulse: the 1 PPS is the tool's own.  A fixed sky
  and a scenario, a table of changes to one that can insert a leap
  second, are worked out from the tick alone, so any second can be
  asked for in any order.  A replay reads a recording through an
  assembler the caller supplies, there being no recording format among
  what the receivers use.  Wrappers relabel a source's time, announce a
  leap second or silence it for a while, and nest by value.
- **A record takes a leap second once.**  Its UTC offset is the one in
  effect, with a past leap second's step already in it, so a leap
  second announced over another would step it twice.  Where both are
  in tables a `const` function reads -- a scenario's rows, and the sky
  under them -- the scenario is refused when it is built, with a typed
  error, so a `static` of one fails the build.  A wrapper cannot know
  what the source it wraps will say, a recording's least of all, and a
  wrapper that could fail to build could not be the fixed nest a tool
  switches on by configuration; so there the source's own leap second
  is kept, the wrapper's is withheld, and the wrapper counts the
  records for the tool to report.  A scenario's leap second is worked
  out after its other rows, so their order does not matter and the
  offset is always the one for the record's time.  A scenario's time
  step, an engine's clock gone wrong, leaves the leap second where it
  is.  The wrapper that relabels time, a sky moved whole to another
  date, drops the source's leap second, the event and its step, and
  freezes the offset at the one in effect at the fake start: UTC has
  a leap second only where a day ends, and a real one moved by an
  arbitrary whole number of seconds lands in the middle of a day of
  the UTC the new time reads.  A leap second under a fake time is one
  scheduled in that time, by the announcing wrapper around the
  relabeling one; one inside it is the source's, and is dropped and
  counted for the tool to report.
- **A source that follows real time is refused where real time is not
  kept.**  Each source says how it takes ticks -- any, ascending only,
  or not at all, giving whatever it heard last -- and `admit` refuses
  the pairs that cannot work, before the first tick and in words fixed
  by test: a live source under a driver that runs fast, one resuming
  saved state, or one with no network or serial port, as in a browser;
  and a forward-only stream asked to resume.
- **The live source stamps each record with the tick it arrived in**,
  which whoever delivers it passes in, so the two things a driver must
  watch such a source for are measured in the crate and not in each
  driver: how long ago the newest record arrived, and how far its time
  moved against the ticks since the one before, which is other than
  zero when the source's time jumped or records were lost.  It has no
  lock and no atomics, a Cortex-M0 having no compare-and-swap to build
  one from: where delivery can interrupt the driver, the caller puts
  each call in a critical section.
- **Sources do not allocate either.**  Each is built by `const`
  functions and borrows its tables, so a nest of them is one `static`
  with its tables in flash, and each fills the caller's record rather
  than return one.  Only the live source holds records of its own, two,
  so that a record half filled is never read.
- **One crate, a module a format**, not a crate a format: the formats
  share a pattern and little else, and another engine's is another
  module.
- **A deframer follows the firmware's own framer** wherever the manual
  leaves the choice open -- where to pick a stream up after a bad
  frame, what a sentence may hold -- since what the firmware would
  refuse is not worth accepting.  Each module's documentation says
  which rule comes from where, and where it departs.
- **`thiserror`, without its default features, is the one
  dependency**: it needs neither `std` nor an allocator, and the
  other library crates write their errors with it.

## Next

- **Compacting old logs**, perhaps into Parquet.  The logs grow without
  bound.
- **Bench work:** the open items in `docs/hardware-investigations.md`.
- **A Z3817A's reference beyond the monitor:** the daemon logs
  `:DIAGnostic:REFerence:STATus?` and the monitor shows it, but the
  browser's status strip and the exporter, which names each metric it
  exports, do not.  Its lists leave out the predicted visible count,
  which the bench unit declined on every pass, until the reason is
  known; asked again, it would give the browser's not-tracking series.
- **Editing notes from the web view**, through the daemon socket.
  Needs POST bodies in `smartclock-http`, `/api/notes` returning each
  note's `id`, and edit requests keyed by it.
- **Tracking log decoder:** a 58503A's `Track lost` and `Track resumed`
  event log entries carry packed channel words
  (`docs/firmware/gps.md`, "Channel mode and signal strength");
  `smartclock-cli` could unpack them when it reads the log.  It needs
  each image's PRN 32 alias (10 in the 58503A 3704, 12 in the later
  images) and must report the alias's PRN as ambiguous, since both
  PRNs now fly.
- **Firmware C:** when a Unicorn release has #2420 (bug 1), bump the
  pin, drop the probe and set `BUG_1.expected` to fixed; likewise bug 2.
  Give the firmware routines their file's prefix, as the hand-written
  helpers have, through the names files.  The exporter should give an
  exception entry no bridge.  The primary's own writes to ROM's range
  are not modeled.
- **The rest of the firmware work** is in progress and will follow:
  the emulator that boots these images and booting the rebuilt C in
  it.  The FPGA designs decoded as netlists are here
  (`docs/firmware/fpga-comparison.md`).
- **FPGA netlists:** where DL.Y goes; what each trace lists as open
  (`docs/firmware/z3805a-trace.md` and its siblings, "Confidence"),
  among them the other four designs' write maps and pins; all of it on
  a board.  The Z3816A 4001's `counter_set_sync` is read later, with
  the Z3816A rewrite; until then the P7-reach finding names only the
  images whose sync writes are read.

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

6. **What the Z3815A's "19.8 MHz Fail" alarm watches.**  The 4010
   image names a hardware alarm "19.8 MHz Fail" beside "10 MHz Fail"
   and "19.6608 MHz Fail" (`strings -t x`, `0x34e7a` to `0x34e99`;
   `alarm_hardware_update`, `0x341b8`), and no 19.8 MHz clock appears
   in the FPGA design.  What it watches is not traced, so the FPGA
   docs leave it out until it is.

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
