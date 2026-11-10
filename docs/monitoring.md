# Monitoring

The programs that watch a receiver: a logging daemon and its clients,
a terminal monitor, a browser view and a Prometheus exporter.  Each
line names a program and links the document that describes it; the
tools that take the firmware apart are in [`tooling.md`](tooling.md).
[`running.md`](running.md) covers installing, configuring and running
them; the top [`README.md`](../README.md#running-it) shows them run by
hand.

## Programs

| Program | What it is |
| ------- | ---------- |
| `smartclockd` | holds the serial port, logs to SQLite, serves clients over a Unix socket and, if asked, TCP ([`running.md`](running.md)) |
| `smartclockmon` | terminal monitor: dashboard, history graphs, journal, status screen and stability ([`views.md`](views.md#the-monitor)) |
| `smartclock-web` | browser view: live state, zoomable history, the status screen, stability, receivers compared, measures correlated, and a kiosk screen for an unattended display ([`views.md`](views.md#the-browser)) |
| `smartclock-exporter` | Prometheus metrics for every receiver, from the daemons' readings ([`views.md`](views.md#the-exporter)) |
| `smartclock-sensord` | logs room temperature, humidity and pressure from hwmon, IIO and TEMPer USB sticks ([`sensors.md`](sensors.md)) |
| `smartclock-cli query`, `diagnose` | sends commands and prints the replies; reports oscillator and holdover health in one pass ([`running.md`](running.md)) |
| `smartclock-cli note`, `fact` | writes a note or a fact to the log of a daemon's receiver ([`running.md`](running.md#notes-and-facts)) |
| `smartclock-cli sensors` | lists the host's sensors and their latest readings ([`sensors.md`](sensors.md)) |
| `smartclock-sim` | a simulated receiver, in process for tests and over TCP for the real daemon ([`README.md`](../README.md#running-it)) |

`smartclock-cli`'s commands that read the firmware or program a
receiver are in [`tooling.md`](tooling.md).

## Libraries

| Crate | What it is |
| ----- | ---------- |
| `smartclock` | transports, SCPI framing, the command table, parsers, the status screen scraper, the polling task, the Allan deviation |
| `smartclock-log` | the log's schema and readers |
| `smartclock-http` | the server the exporter and web view share |

## How it works

| Document | What it covers |
| -------- | -------------- |
| [`views.md`](views.md) | what each view shows, and why |
| [`stability.md`](stability.md) | how the stability curves are computed, checked and measured |
| [`protocol.md`](protocol.md) | how the receivers behave on the wire |
| [`commands.md`](commands.md) | the command table the tools choose a dialect from |
| [`PLAN.md`](../PLAN.md) | the design, the decisions behind it, open questions and known defects |
