# Monitoring

The programs that watch a receiver: a logging daemon and its clients,
a browser view and a Prometheus exporter.  Each line names a program
and links the document that describes it; the tools that take the
firmware apart are in [`tooling.md`](tooling.md).
[`running.md`](running.md) covers installing, configuring and running
them; the top [`README.md`](../README.md#running-it) shows them run by
hand.

## Programs

| Program | What it is |
| ------- | ---------- |
| `smartclockd` | holds the serial port, logs to SQLite, serves clients over a Unix socket and, if asked, TCP ([`running.md`](running.md)) |
| `smartclock-web` | browser view: live state, zoomable history, the status screen, stability, receivers compared, measures correlated, and a kiosk screen for an unattended display ([`views.md`](views.md#the-browser)) |
| `smartclock-exporter` | Prometheus metrics for every receiver, from the daemons' readings ([`views.md`](views.md#the-exporter)) |
| `smartclock-sensord` | logs room temperature, humidity and pressure from hwmon, IIO and TEMPer USB sticks ([`sensors.md`](sensors.md)) |
| `smartclock-cli query`, `diagnose` | sends commands and prints the replies; reports oscillator and holdover health in one pass ([`running.md`](running.md)) |
| `smartclock-cli note`, `fact` | writes a note or a fact to the log of a daemon's receiver ([`running.md`](running.md#notes-and-facts)) |
| `smartclock-cli sensors` | lists the host's sensors and their latest readings ([`sensors.md`](sensors.md)) |
| `smartclock-cli daemons` | lists the host's receiver daemons, each with the socket to give `--daemon`, the receiver it is attached to and the port it holds ([`running.md`](running.md#finding-a-daemon)) |
| `smartclock-cli devices` | lists the host's serial ports, each with the daemon holding it and its receiver, and with `--probe` asks the free ones `*IDN?` ([`running.md`](running.md#listing-the-ports)) |
| `smartclock-sim` | a simulated receiver, in process for tests and over TCP for the real daemon ([`README.md`](../README.md#running-it)) |
| `smartclock-gps-feed` | runs a `smartclock-gps` engine on a host and speaks Oncore binary, or the Z3815A's Furuno NMEA, to a `SmartClock`'s engine port, from gpsd, a UBX recording or a made-up sky (`smartclock-gps-feed --help`) |

`smartclock-cli`'s commands that read the firmware or program a
receiver are in [`tooling.md`](tooling.md).

## Libraries

| Crate | What it is |
| ----- | ---------- |
| `smartclock` | transports, SCPI framing, the command table, parsers, the status screen scraper, the polling task, the Allan deviation, and behind its `http` feature the small HTTP server on tiny_http that the exporter, the web view and the emulator's panel page share |
| `smartclock-log` | the log's schema and readers |
| `smartclock-gps` | the wire formats of the GPS engines inside the receivers, Oncore binary and NMEA with Furuno's `$PFEC` sentences: IDs, lengths, checksums, encoders and deframers, a record of what an engine knows in one second and sources of it, and models that stand in for an Oncore and for the Z3815A's Furuno, `no_std` ([`firmware/gps.md`](firmware/gps.md)) |
| `smartclock-ubx` | a u-blox receiver's UBX framing and messages both ways, every field decoded, the messages that configure it encoded, and a GPS satellite's navigation subframes too, for a microcontroller and for hosts alike: a codec alone, depending on no other crate here, `no_std` |
| `smartclock-gps-ublox` | `smartclock-ubx`'s messages made into `smartclock-gps` records, a source of them from a receiver no `SmartClock` holds, `no_std` |

## How it works

| Document | What it covers |
| -------- | -------------- |
| [`views.md`](views.md) | what each view shows, and why |
| [`stability.md`](stability.md) | how the stability curves are computed, checked and measured |
| [`protocol.md`](protocol.md) | how the receivers behave on the wire |
| [`commands.md`](commands.md) | the command table the tools choose a dialect from |
| [`PLAN.md`](../PLAN.md) | the design, the decisions behind it, open questions and known defects |
