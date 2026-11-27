# The emulator

`smartclock-emu` boots a receiver's own flash image on an emulated
CPU32, among models of the board's devices, with its host port on
standard input and output or served over TCP the way `smartclock-sim`
serves its simulated receiver, so that the daemon and the CLI connect
to it by `tcp://HOST:PORT`.  The decisions are PLAN.md's, "The
emulator".  Each image runs on its model's board file ("The boards"):
the Z3805A 3543B and 1.01.03, the 58503A 3704, the Z3801A 3543 and the
Z3817A 3752 on hand-written FPGA models, and the 58503B 1.01.04, the
Z3815A 4010 and the Z3816A 4001 on their bitstreams' netlists executed
(`firmware/fpga-codegen.md`).  The 58503A also runs in a browser ("In a
browser").

A file this document names alone is found so:

- Docs: `efc.md`, `ocxo.md`, `protocol.md`, `bench.md`,
  `hardware-investigations.md` and `board-atlas.html` are in `docs/`;
  `scpi/NAME` is in `docs/scpi/`; the rest (`restart.md`,
  `structures.md`, `outputs.md`, `console.md`, `counter.md`,
  `interval.md`, `chip-selects.md`, `gps.md`, `58503a-3704.md`,
  `z3801a-3543.md`, `z3817a-3752.md`), with or without a leading
  `firmware/`, are in
  `docs/firmware/`.
- The firmware's C (`adc.c`, `duart.h`, `smartclock.h`) is the
  hand-written C in `third_party/firmware/rewritten/`, under the image's
  directory, the Z3805A 3543B's unless the text names another, or
  `common/`; `rewritten/NAME` is under `third_party/firmware/rewritten/`.
- The emulator's own files are under `crates/smartclock-emu/`: a path
  (`board/ocxo.rs`, `gps/`, `mc683xx/`) under its `src/`, but
  `tests/NAME` under its `tests/`; a Rust file named alone under
  `src/`, `src/board/` or `src/mc683xx/`; the panel page's files
  under `src/panel/page/`.
- Manuals by number (097-59551-02, 097-z3801-01, 097-58503-08, -12 and
  -13) are in `third_party/hp/`; VPCommands.pdf, the VP Oncore
  Engineering Notes (`vponcoreengnotesarchive.pdf`), MC68331UM, the
  MC68681 data sheet and MC144110/D in `third_party/motorola/`;
  `xc3000.pdf` in `third_party/xilinx/`.  SNAS531B is TI's ADC0838
  data sheet, not in the repository.
- mc683xx's docs are
  [mc683xx's repository](https://github.com/charlieh0tel/mc683xx)'s
  `docs/`.

## Quick start

Build it, boot a 58503A with its front panel drawn in a browser, and
ask it what it is:

    cargo build --release -p smartclock-emu
    target/release/smartclock-emu third_party/firmware/images/58503a-3704.bin \
        --eeprom target/smartclock-emu/58503a.bin --panel 8080
    smartclock-emu: panel on http://127.0.0.1:8080/

Open the page.  Its console, under the control strip, has the host
port: once the firmware's `scpi > ` prompt shows, type `*IDN?` and
Enter; the answer is in "What the boot shows".  Without `--panel` the
host port is the terminal's, where `*IDN?` and Enter do the same, and
^C stops the emulator ("Running it").  Or open
<https://charlieh0tel.github.io/smartclock/emulator.html>, the same
58503A in the browser, nothing to build.

## The cores

The CPU core is [mc683xx](https://github.com/charlieh0tel/mc683xx)'s
MC68331, under MIT or Apache-2.0, its 0.9.0 release from crates.io,
pinned in `crates/smartclock-emu/Cargo.toml`
and bumped only on purpose.  It is behind the `mc683xx` feature, on by default, so a plain
`cargo build` has it, and it runs the receiver's board file,
`boards/*.toml` ("The mc683xx backend").  A build with it alone can be
distributed under this project's license.

Unicorn is a second core, `--core unicorn`, a development option that
is frozen and never distributed ("The Unicorn core").

    make emulator            # clippy and every test, on mc683xx
    make emulator-slow       # the tests ignored as too slow unoptimized
    make emulator-wasm       # compiled for wasm32-unknown-unknown, not run

`make emulator` and then `make
emulator-slow` are what firmware.yml's
`emulator` job runs, beside the firmware's checks, and `make
emulator-wasm` its `emulator-wasm` job: the emulator checked for the
browser's target, after `rustup target add wasm32-unknown-unknown`, so
that nothing it comes to need leaves it unable to build there.  The
tests build in
the dev profile, in which mc683xx's crates are optimized all the same
(Cargo.toml); `EMULATOR_PROFILE=checked`, which CI gives, builds them
in Cargo.toml's `checked` profile, optimized, with the dev profile's
debug assertions and overflow checks kept: the mc683xx crates
`debug_assert!`, and an overflow in a device model is a bug.

### The library and its host

The crate is a library, `smartclock_emu`, the board's side, and the
binary `smartclock-emu`, the command line's host (`main.rs`).  The
library has the run, checked (`config.rs`), the board's models, both
cores' runs and the panel page's answers, a request to a response
(`panel/routes.rs`); it reads no command line.  A run starts from the
command line's defaults (`RunConfig::new`), takes the choices its host
gives, and is checked as a whole (`RunConfig::check`).  The options
every host's command line takes alike, the model, the board file, the
synthetic engine's, the oscillator's, the stop and the tracing, are the
library's (`options::RunOptions`, clap's), which a host flattens into
its own beside its ports' and files'.  What the host
gives the run beyond that is `host::Ports` (`host.rs`): a line served
at an address, the host port on its own console, the SCI's capture
and the panel page's server; `files::Files` (`files.rs`), the files the
run reads (at its start the board file, its images and a script, and at
each power-up the EEPROM's dump: a part's file parameters are read
through it, by mc683xx's `Board::load_with`) and the working directory
paths are taken from, the file system's or, for a host with none, the
bytes it was given (`files::InMemory`); and its clock's readings, which
a paced run is stepped to ("The
mc683xx backend").  The binary's are the command line's (`cli.rs`),
TCP, standard input and output, a file and `smartclock::http`
(`serve.rs`, `stdio.rs`), the file system (`files::Disk`) and
`Instant`; it reads the image itself, and gives the run the synthetic
engine's start, the host's clock's UTC unless `--gps-start`.  The
trace and the emulator's own lines go to standard error unless the
host gives them a sink of its own (`trace::set_sink`); a script's
transcript, what the host port sends, goes to standard output.  The library compiles for
wasm32, the browser's target (`make emulator-wasm`); the Unicorn core
is native alone.

## Running it

From a checkout: the board files are found in the checkout's
`boards/`, and elsewhere `--board` names one.

    make emulate      # the Z3805A 3543B, host port on this terminal
    make emulate HOST=127.0.0.1:5025   # the same, host port over TCP

The host port, DUART channel B, where SCPI and the pForth console are
spoken, is on standard input and output unless `--host`, `--script` or
`--panel` says otherwise ("The ports"); standard output carries the
port's bytes and nothing else, everything the emulator says itself
going to standard error:

- *A terminal*: typed at, raw and not echoing, since the firmware
  echoes; Enter goes as CR LF, as `smartclock::session` sends it (the
  firmware's line reader, `duart_gets` in `duart.h`, ends a line at CR
  or LF and skips the other half of a pair, so CR, LF and CR LF all
  end one).  ^C and SIGTERM stop the emulator; the terminal is put back
  on every way out, a signal, an error or a panic among them.  ^Z puts
  it back while the emulator is stopped, and `fg` makes it raw again.
- *A pipe or a file*: its bytes go to the port as they are, back to
  back at the line's rate, from the firmware's first output on (a byte
  before its receiver is enabled is lost, as on the board, and the
  power-up prompt says it is), each when the line has taken the one
  before, so they reach the board at the same board times however the
  pipe delivers them and a deterministic run repeats.  At the input's
  end the run goes on until the port has been idle for 2 s of board
  time since its last output or the last byte given, past the second
  `:PTIMe:TCODe?` can take to answer, then stops with the stopped
  record; `--stop-after` still bounds it.

      printf '*IDN?\n' | cargo run --release -p smartclock-emu -- \
          third_party/firmware/images/z3805a-3543b.bin \
          --eeprom target/smartclock-emu/z3805a.bin --deterministic

`--host ADDR` serves the port over TCP instead, one connection at a
time, for the daemon and the CLI:

    smartclock-emu IMAGE --eeprom FILE --host 127.0.0.1:5025
    smartclock-cli --device tcp://127.0.0.1:5025 query '*IDN?'
    smartclockd    --device tcp://127.0.0.1:5025 ...

`make emulate` copies a Z3805A's EEPROM dump
(`third_party/firmware/images/eeprom/z3805a-3625A01487-eeprom.bin`) to a working
file of the same name under `target/smartclock-emu/` (`EMULATOR_EEPROM`
names another) the first time; every write the
firmware makes goes through to it, so settings carry from one run to the
next, and the dump is never written ("The EEPROM").

The 58503A and the Z3801A boot the same way, each on its own image and
EEPROM; the image picks the board ("The boards"):

    smartclock-emu third_party/firmware/images/58503a-3704.bin \
        --eeprom target/smartclock-emu/58503a.bin \
        --eeprom-dump third_party/firmware/images/eeprom/58503a-3710A01056-eeprom.bin
    smartclock-emu third_party/firmware/images/z3801a-3543.bin \
        --eeprom target/smartclock-emu/z3801a.bin \
        --eeprom-dump third_party/firmware/images/eeprom/z3801a-3542A01548-eeprom.bin

### The options

| Option | What it does | Where |
| ------ | ------------ | ----- |
| `IMAGE` | the flash image to boot; it picks the board | "The boards" |
| `--eeprom FILE`, `--eeprom-dump FILE` | the EEPROM's working file, and the dump it is made from when it does not exist | "The EEPROM" |
| `--core mc683xx`, `--core unicorn` | picks the core, mc683xx by default | "The cores" |
| `--board FILE` | the board file mc683xx runs in place of the model's in `boards/` | "The mc683xx backend" |
| `--fpga-design DESIGN` | on mc683xx, the FPGA's design in place of the board file's, a `bitstream` it asserts dropped but for `lca`: `lca` runs the netlist of the bitstream the firmware sends, exact and far slower | "The boards" |
| `--gps-engine MODEL` | on mc683xx, the synthetic engine's Oncore in place of the board file's: `b1121`, `b4121` or `r5122`, the UT Plus; `b4121` puts a VP on the 58503B | "The synthetic GPS engine" |
| `--model z3805a`, `58503a`, `z3801a`, `z3816a`, `58503b` | models that board whatever the image names | "The boards" |
| `--host ADDR` | serves the host port over TCP in place of standard input and output | "The ports" |
| `--script FILE` | runs the host port from a script in place of standard input; refuses `--host` | "Scripts" |
| `--sci ADDR` | where to serve the SCI, 127.0.0.1:5026 by default; refused on mc683xx, as `--sci-capture` is, where the board file has no `sci` endpoint (the 58503A's and the Z3801A's), rather than ignored | "The ports" |
| `--sci-capture FILE` | writes every byte the SCI sends to a file, raw, as it is sent | "The ports" |
| `--gps ADDR` | serves DUART channel A, the GPS engine's port, at an address, in place of the synthetic engine, for an external engine or a recording | "The ports" |
| `--gps-latitude`, `--gps-longitude`, `--gps-height` | place the synthetic engine's antenna | "The synthetic GPS engine" |
| `--gps-start` | UTC at power-up, RFC 3339, else the host's clock, to the second | "The synthetic GPS engine" |
| `--gps-leap-seconds N` | GPS time less UTC, 0 to 127, 18 by default, as since 2017 | "The synthetic GPS engine" |
| `--deterministic` | runs flat out instead of keeping the board's time to the wall clock's | "Time" |
| `--stop-after SECONDS` | stops after that much of the board's time | "Time" |
| `--seed N` | seeds the board's modeled noise, the ADC's readings and the oscillator's, from a fixed default, so that a deterministic run repeats | "Time" |
| `--ocxo-offset`, `--ocxo-aging`, `--ocxo-warm-up`, `--ocxo-efc-gain`, `--ocxo-white-fm`, `--ocxo-random-walk-fm` | set the oscillator's terms, each over the board file's; one not given keeps the board file's, or with none the model's, the offset with none the EEPROM's | "The oscillator", "The oscillator the EEPROM describes" |
| `--skip-warm-up` | starts every power-up, the run's and each power cycle's, at the warm-up's settled end, as the `skip-warm-up` control puts it; mc683xx's, refused on Unicorn | "The controls" |
| `--time-code-off` | sends `:PTIMe:TCODe:CONTinuous OFF` on the host port at each power-up's first time code frame, as a client does first, so that the port answers; the Z3816A's, refused on a model whose image does not start the time code at power-up | "The boards" |
| `--tap` | copies what each port sends to standard error, a line at a time (`smartclock-emu: sci < ...`, escaped, CR left out, a line broken after 160 bytes with none lost; on mc683xx the synthetic engine's line both ways too, `gps <` what reaches its `rx`) | "The ports" |
| `--indicators` | reports the lamps and the LEDs | "The lamps", "The LEDs" |
| `--panel ADDR` | serves a page that draws the front panel live and takes its keys; without `--host` or `--script` its console has the host port in place of standard input and output | "The panel page", "The console" |
| `--trace`, `--count-instructions`, `--watch ADDR`, `--watch-code ADDR` | trace what the CPU does; `--count-instructions` is Unicorn's, refused on mc683xx, which counts the instructions always | "Tracing" |

## The ports

The host port, where SCPI is spoken, is DUART channel B, not the SCI:
`scpi_port_open` opens I/O device 0, channel B (`0x1dee6`;
`scpi_port.c`), as on the Z3801A and the 58503A.  The pForth console is the same port after
`:SYSTem:LANGuage "PFORTH"`.  The SCI is a second port: `sci_init` sets
its asynchronous flag (`0x10329c`, at `0x1ffc8`) and nothing clears it,
and while it is set `sci_write` (`0x20152`) drops text, so the SCI
carries only the ES message, sent every other second (`es_schedule`,
`timecode.c`; structures.md, "The clock record").  DUART channel A is the
GPS engine's link, with the synthetic engine on it, or served with
`--gps`.  The Z3816A's host port is its SCI, its one port
(firmware/console.md, "Which port"), quiet but for its time code until
that is turned off ("The boards").

Who has the host port, standard input and output, `--host`'s client,
a script or the panel page's console, is in "Running it" and "The
console".  On mc683xx the board file's run-time endpoints are bound to
the command line's ports: `host` to standard input and output, a
script, `--host` or the panel page's console; `sci` to `--sci`; `gps`
to `--gps`; `gps_pps` to the timed 1 PPS of `--gps`; `alarm`,
the 58503A's PF5, to the indicators' report (`bank=output`).

What connects to `--gps` must speak the protocol of the engine part
it takes the place of, as the firmware does: an Oncore's binary
protocol for an `oncore` part, Furuno's NMEA for a `furuno-gt74` one,
the part's `protocol`.  It can be a receiver's own
stream through a serial-to-TCP bridge, a recording of one, or
`smartclock-gps-feed`, which runs either family on a host.  A bridge
fed by gpsd takes gpsd's raw stream, not its JSON.  An external engine
sends no 1 PPS over TCP, and the board's GPS 1 PPS is timed, never
taken from the engine's messages: it rises on each whole second of the
emulator's own clock, the run's time, except when the run is paced to
the wall clock, where it rises on each whole UTC second of the host's
clock, on which `smartclock-gps-feed` paces its seconds too.  It is
high for 200 ms, as the VP Oncore's.  A pulse taken from the messages
would carry their arrival jitter over TCP, tens to hundreds of
milliseconds, start at a reply to a command as readily as at the
continuous output, and move later by hundreds of milliseconds, which
stalls the firmware's fine frequency adjustment.  The second each
rise is due on is fixed against board time when the pulse starts, so
a run that falls behind the wall clock under load takes its rises late,
still a second apart, and never moves them.  On mc683xx the cable
delay and the pulse offset the firmware sends the engine, an Oncore's
`@@Az` and `@@Ay` or a GT-74's `set,t`, read from the line by a model of
each family, move each rise from the next on by them: a cable delay of
500 ns puts the pulse 500 ns before each second, a step of 500 ns and no
more, as an engine's own pulse would step.  The pulse mode, whether the
engine puts a pulse out at all, is the external engine's to know, and
the timed pulse always rises.  The built-in engine, which the emulator
holds, applies all three (`gps/external.rs`, whose `Rises` schedules
the pulse for both cores; `mc683xx/pps.rs`).

## The EEPROM

`--eeprom` names the working file and `--eeprom-dump` the dump it is
made from when it does not exist; its directory, and any above it, is
made first if it is not there, on both cores.  Without a dump, on mc683xx, it
starts from the board file's EEPROM `image` where the file has one
(`z3805a.toml`, `58503a.toml` and `z3801a.toml`, each the bench unit's
dump; `58503b.toml`, a 58503B's dump
scrubbed, and `z3816a.toml`, `z3815a.toml` and `z3805a-10103.toml`,
synthetic images, below); otherwise, and on Unicorn, it starts erased,
every byte `0xff`.
A dump shorter than the part is padded erased, so `--eeprom-dump` of
an empty file, `/dev/null` among them, starts it erased on any board.

The 58503B's board file starts from a 58503B's EEPROM, read with
pForth, `third_party/firmware/images/eeprom/58503b-L00000-eeprom.bin`,
every byte as read but two values replaced (*board*, the rest):

- the serial number, the identification record's at `+0x14`, by
  `L00000`, a 58503B serial's shape, a letter and five digits, that no
  unit has;
- the position in the live NV slot, slot 1, `:PTIMe:GPSystem:POSition`'s three
  values (descriptor `0x4438a`, packed from the slot's bit 128:
  latitude and longitude, 32 bits each, in milliarcseconds, then the
  height, 24 bits signed, in centimeters, at the slot's `+0x20`,
  `+0x24` and `+0x28`), by HP Santa Clara Division's, the synthetic
  engine's default antenna ("The synthetic GPS engine"), its height
  the ellipsoid height, 55 m MSL plus the engine's geoid separation,
  since the 58503B keeps that one (firmware/gps.md, "The 58503B's
  height datum");

and the identification record's and slot 1's checksums made again.
Slot 0, invalidated, holds the defaults' position, zeros, which place
nothing, and is left as read.
Nothing else in the part holds a position or the serial number: the
lifetime record, the calibration block, the reset records and the
event log were read for them, the log's formats and its entries'
arguments too.  `tools/firmware/scrubbed_eeprom.py` writes the image
from a dump, which the repository does not keep, so the image is
checked in, and `make check-boards` holds it to its checksums, tags,
revision, serial number and position (`scrubbed_eeprom.py --check`).

The dump confirms the layout the synthetic images are built to
(structures.md, "EEPROM records") on a real unit: the identification
record's model, serial number at `+0x14`, suffix at `+0x26` (`D` on
this unit) and tag `CQB` at `+0x27`; the NV slots' tag `CQ` and the
checksum over `+8` to the slot's end; the checksum convention of every
other record, the lifetime record, the calibration block, the reset
records and the log pages, each good; slot 0 holding the defaults,
byte for byte the synthetic image's slot 0 but for its first byte and
its sequence number, with the 58503B's erase mark, `0xf1`, `0xf0` with
the setting's value 1, in its first byte, as the firmware writes it,
and slot 1 the live one.  Its parameter record differs from the ROM's
defaults at `+8` and `+0xc`, the channel B and SCI echo bytes, both 0
where the defaults have 1: the unit's own settings, echo off, which
the 58503B takes, so its port does not echo ("What the boot shows").  `scrubbed_eeprom.py --check` holds slot 0 to the
synthetic image's.

No Z3816A's or Z3815A's EEPROM has been dumped, and no Z3805A
1.01.03's is the repository's, so their board files start from
synthetic images, named so that they are not taken for dumps,
`third_party/firmware/images/eeprom/z3816a-synthetic-eeprom.bin`,
`z3815a-synthetic-eeprom.bin` and `z3805a-1.01.03-synthetic-eeprom.bin`,
the last built from the 1.01.03's own NV layout, its descriptor tables
and defaults, and no dump; the
58503B keeps one beside its dump, `58503b-synthetic-eeprom.bin`, which
no board file starts from but which `tests/eeprom.rs` and
`scrubbed_eeprom.py --check` hold the firmware and the dump to.  All
are what `make synthetic-eeprom`
writes from the images (`tools/firmware/synthetic_eeprom.py`) and
`make check-boards` holds to them.  Each is the records the image's
own power-up reads, in the 58503A 3704's layout, which every one of
the images keeps (structures.md, "EEPROM records"), the Z3815A 4010's
at its own addresses (its `nv_save`, `0x4701e`, packs 24 numeric
descriptors and 19 settings into the same 160-byte slot), every value
marked as the board files mark theirs (`boards/README.md`, "Synthetic
EEPROMs"):

- the parameter record (`0x400000`), *read*: the SCI's defaults, 9600
  baud, eight bits, no parity, no pacing and echo on, as
  `save_parameter_record` lays them out;
- the identification record (`0x400010`): the model, the tag (`CQB`,
  `S2` or `S3`) and the suffix `A` *read*, what each image takes for a bad
  record, and the serial number `0000A00000`, *inferred*, made up: an
  HP serial's shape with a date code no unit has;
- the NV register's slot 0 (`0x4000c0`), *read*: `nv_save`'s record of
  the settings at power-up, `nv_settings_defaults` and the image's
  initialized data, with sequence number 0, as the firmware's first
  save to an erased part writes it;
- the reset records (`0x400200`) and the event log's pages
  (`0x400d80`), *read*: what the same first power-up writes next, its
  settings defaulted, `log_erase` and `log_clear` in the root task
  (each image's rewritten `main.c`): every page cleared, its write count the erased `0xffff`
  plus one, and "Log cleared" in page 0's first entry and in the first
  reset record, each at 0:00:00 on 1994-01-01, the clock's start on an
  erased lifetime record;
- every other byte erased, *inferred*: slot 1, the lifetime record and
  the calibration block, each of which the firmware reads as absent,
  taking its defaults, and none of which its power-up self-test reads.

The log is not left erased, since the firmware does not read an
erased page as an empty one.  Its first entry on such a page reads the
page in, keeps the other entries' `0xffffffff` formats, and moves the
oldest entry past the one it writes, the entry under it not free
(`log_append`); `:DIAGnostic:LOG:READ? 1` then prints an entry whose
format is `0xffffffff`, and the bus error stops the receiver until its
watchdog resets it: at every read on the Z3815A 4010, which reads the
page again before each entry, and at the first on the Z3805A 1.01.03.

So the firmware boots on it as on a unit already powered up once: its
settings are its defaults, `*IDN?` gives the serial number, and the
power-up self-test's EEPROM test passes, which on an erased part fails
(`nv_restore` finds neither slot valid).  `tests/eeprom.rs` holds the
read records to the firmware, starting each from an erased part: the
slot its first power-up saves, the parameter record the 58503B's
and the Z3815A's
write back (`parameter_record_read`, `0x40f64` and `0x4528e`; the
Z3816A's only takes the defaults), and the reset records and event log
it clears, but for the entries it logs past "Log cleared", are the
image's bytes; the Z3815A's and the Z3805A 1.01.03's boot tests read
the log back with no reset.
The bytes of each of the part's write cycles are
written to the file as the cycle ends, each by itself, so the settings
the SCPI setters keep in the NV register (structures.md, "The NV
register") are there the next time it starts.  The file cannot be left
part written:

- It is made whole: a new one is written in a sibling and linked into
  place, never renamed over an existing one, and a short one is padded
  where it is (on Unicorn, "The Unicorn core").  A dump or a working
  file larger than 8 KiB is refused.
- Each byte of a finished write cycle goes to the file in one `write` of
  one byte, with no buffer in the emulator: a kill, even `kill -9`,
  loses nothing a finished cycle wrote, and a cycle still running loses
  its bytes, as a power cut does on the part.  It is not synced a byte
  at a time, so a crash of the host may lose the last bytes, as a power
  cut mid-write loses them on the real part; the firmware checksums its
  records and keeps the NV register in two slots against that.  A
  record the firmware was writing when it was killed is torn as it
  would be on the board, never the file.
- The file is locked while the emulator runs; a second one on it is
  refused.

That is mc683xx-peripherals' stock `at28c64b`
(mc683xx's [`docs/peripherals.md`](https://github.com/charlieh0tel/mc683xx/blob/main/docs/peripherals.md)),
which locks the working file itself while the board is loaded.  It
keeps the part's software data protection, should the firmware ever
turn it on, in `<file>.sdp` beside it.  On Unicorn the EEPROM is
smartclock's own model, the same part but for how it makes and locks
the file ("The Unicorn core").

A host with no files, a browser, keeps the EEPROM in memory instead
(`Eeprom::Memory`, the run's `eeprom`; the mc683xx core only, and not on
the command line).  The board file is the same: the part keeps its
`image`, which it reads through the host's files (mc683xx's
`Board::load_with`, `Context::contents`), but any `file` is taken off
it and the run gives it no work directory, so it opens no file.  A
power cycle carries the part's cells (`At28c64b::cells`) and its
software data protection (`At28c64b::protected`) to the board it
builds: the cells are what the host's files answer for the image's
path, in place of the dump, and the protection is the part's
`protected` parameter.  The host keeps the same two (`Run::eeprom`,
`mc683xx::KeptEeprom`) and gives a later run the cells as the image's
bytes and the protection as `Eeprom::Memory`'s `protected`.  Nothing is
written anywhere, and a write cycle a power cycle cuts off is lost, its
bytes never reaching the cells, where the file mode keeps what a power
cut does to the part.  The unit test
`an_eeprom_in_memory_boots_the_58503a_from_its_dump` boots the 58503A
so, every file from memory, the dump among them, and reads the dump's
serial number from `*IDN?` after a power cycle;
`a_power_cycle_carries_the_eeproms_cells_and_protection` checks the
carry.

The part is U4, an Atmel AT28C64B (`hardware-investigations.md`, item
22), and its writes are the data sheet's
(`third_party/microchip/AT28C64B.pdf`, DS20006432B, 6.2 to 6.5, Table
6-4):

- A write starts a write cycle; each byte after it within tBLC, 150 µs,
  of the one before is loaded into the same 64-byte page (A6 to A12),
  by A0 to A5, a byte loaded twice keeping the second.  Once tBLC passes
  with none, the part programs the bytes loaded, and only those, for
  tWC; a byte written then is not taken, and one of another page during
  the load, which the data sheet forbids without saying what it does,
  is dropped.  The bytes reach the cells, and the working file, as tWC
  ends.
- From the first byte to the end of tWC every read is a polling read:
  bit 7 the complement of the last byte written's (DATA polling), bit 6
  the opposite of the read before's (toggle bit), the other six the last
  byte's own, which the data sheet does not give.
- Software data protection is off, as the part ships, and stays off:
  the firmware never writes its command sequence.
- tWC is 5 ms, a pick: Table 6-4 gives only its most, 10 ms.  The
  firmware polls for a write's end 5000 times (`eeprom_write_byte`,
  `eeprom_write_page`), about 6 ms and 7 ms of board time at Unicorn's
  two cycles a byte, and about as long on the CPU32 with the part's
  three wait states; at 10 ms every write would run out of polls and be
  tried again.  So the pick is the most both polls outlast.

tBLC is shorter than a slice of the run, so the part takes each access
at its own time: on mc683xx at its own bus cycle's time, and on
Unicorn as "Its board and its time" ("The Unicorn core") has it.

`tests/eeprom.rs` sets the holdover duration threshold
(`:ROSC:HOLD:DUR:STAT:THR`, 86400 in the dump) to 172800, waits for the
NV register's bytes in the working file to change, kills the emulator,
starts it again on the same file and reads 172800 back with no error.
Its sessions are over TCP, so each waits, at each start, for the
power-up self-test to be past its checks of the host port ("What the
boot shows"), which a command sent at the first prompt would land in
or not by how fast the host runs the board.

## The lamps

The latch at `0x300000` drives the Z3805A's front-panel lamps
(outputs.md, "The Z3805A's lamps"): bit 0 Enabled, 1 Active, 2 Alarm,
4 GPS Lock, 5 Holdover.  With `--indicators` each lamp is reported on
standard error as it lights or goes out, a trace record
(`indicators.rs`, shared with the board's LEDs); without it, none is:

    smartclock-emu: indicator cycle=N bank=lamp name=enabled lit=1

The lamps start dark, so the first write reports only what it lights.
At power-up `lamp_test` lights all five, puts them out, and lights each
in turn; after it, `leds_update` writes the latch about once a second,
and only a change is reported.

The 58503A's latch has three lamps, `gps_lock`, `holdover` and `alarm`
on bits 0 to 2 (58503a-3704.md, "The front panel"),
and its front panel's two control lines on bits 6 and 7 ("The
boards"); its `lamp_test` lights the three, puts them out and lights
each in turn, and `hmon_task` writes them after.

## The LEDs

The DUART's output port, OP2 to OP7, drives the board's six LEDs
(outputs.md, "The rest of the bank"), named as `smartclock.h`'s
`board_led` names them: `heartbeat`, `installer`, `cpu`, `ram`,
`duart` and `selftest`.  Each is reported as it changes, in the same
record as the lamps, bank `led`:

    smartclock-emu: indicator cycle=N bank=led name=selftest lit=1

The output port register holds its pins' complement (MC68681, 4.2.12),
and the code reads as if an LED lights on a high pin, so an LED is lit
while its register bit is clear.  The reset clears the register
(MC68681, 2.4), so all six are lit from power-up, reported at cycle 0,
until the primary's reset code sets every bit (`0x144ca`; restart.md),
after the boot code's flash checksums; the installer's path does it at
`0x746`.  `hmon_task` flips `heartbeat` about once a second, a record
each time; the power-up
self-test lights `selftest` for the tests the emulator fails ("What the
boot shows").  A watchdog reset does not reset the DUART's model, so
it lights nothing: the DUART's RESET would follow the CPU's only if the
board ties the two, and that is not known (`board-atlas.html` names
U3, the SCC68692, and a TL7705A supervisor whose reset lines were not
traced; `hardware-investigations.md` has no item on it), so the
behavior stays as it was.

## The boards

The board is the Z3805A's, the 58503A's, the Z3801A's, the Z3816A's
or the 58503B's, a `Model` and its `Profile` (`board/profile.rs`),
chosen by the image: the one model whose profile's `identity`, the
model name `*IDN?` gives NUL-terminated (`Z3805A`, `58503A`, `Z3801A`,
`Z3816A`, `58503B`), the image holds.  `--model` overrides it, and an
image naming none of them is refused without it.  Every image names
its own model so, the Z3815A's `Z3815A` too, NUL-terminated just past
an `rts`, as the modeled images' are: its is refused only because no
profile has its name yet.  A test
(`each_board_files_images_pick_its_model`) holds every board file's
images to the model its `[board]` name names, or to none where there
is no such model.

A model's profile can also name a board file of its own for the
images of one revision, a `Variant`, picked by the revision string the
image holds, NUL-terminated: the Z3805A's names `z3805a-10103.toml`
for the 1.01.03 (`1.01.03` at `0x13048`), which the image picks unless
`--board` gives another.  It is `z3805a.toml` but for the lines marked
`1.01.03`, which a test holds it to
(`each_variant_is_its_models_board_but_for_its_marked_lines`): its
name, title and images, the GPS engine a `furuno-gt74`, `gt-74` ("The
synthetic GPS engine"), and its EEPROM the synthetic image ("The
EEPROM"): the one unit's dump is not the repository's
(firmware/z3805a-1.01.03.md, "Provenance"), and the bench Z3805A's is
a 3543B's.  The image keeps
the 3543B's boot code, chip selects, I/O table and FPGA bitstream
(firmware/z3805a-1.01.03.md, "Layout and chip selects", "What
differs"), so the rest of the board is the Z3805A's; the Unicorn core,
whose board is the model's with its Oncore, refuses it.  From the
synthetic EEPROM it boots configured to its SCPI prompt on DUART
channel B, answers `*IDN?` `SYMMETRICOM,Z3805A,0000A00000,1.01.03-A`, the
synthetic serial number, and passes every power-up test
(`:DIAGnostic:TEST:RESult?` `+0`); from an erased part
(`--eeprom-dump /dev/null`) the EEPROM's fails (`+8`) until the boot
has written it.  Its engine's init is the Z3815A's `$PFEC` script, the
intervals, `rrs`, `rrq`, `set,M`, `set,t`, `set,U` and `set,Z` each
with an `srq`, and two requests of its own, `int,acc00` at power-up and
`int,anc00,acc00` a minute: `acc`, satellite accuracy (GT p. 28),
which the engine sends, a URA index or `X` for each satellite, and
whose commit marks a satellite above 6 in its channel's status bit 3
(firmware/z3805a-1.01.03.md, "anc and acc").  The engine sends a
second's sentences half a second after the pulse before it, as a
GT-8031 does (`engine::furuno`, "Timing").  It takes the synthetic GT-74 for a GT-74
(`:DIAGnostic:IDENtification:GPSystem?` "MODEL # FURUNO GT-74"), tracks
six, surveys and locks: `tests/boot_z3805a_10103.rs` checks the boot,
and, ignored but when asked, `LOCK` at 300 s.  The Z3805A's, the 58503A's and the Z3801A's boards
share the memory map, the MC68331's modules, the DUART, the EEPROM, the ADC, the
DACs and the FPGA's model; the Z3816A's and the 58503B's are below.  That model is the Z3805A's design's
(`firmware/counter.md`).  The board file's `design` names the design
the part runs (`board/fpga.rs`, `Design::NAMES`), not a file: the
emulator reads no bitstream, the firmware sending its own at boot, of
which the model takes only the length count and the readback.
`z3805a` is the Z3805A's design, the one `z3805a.bit` configures; the
58503A and the Z3801A say `58503a-z3801a`, the design of their own
bitstream, one
file, `58503a-z3801a.bit`, which differs from `z3805a.bit` but sets the
same I/O blocks on every pin but P14 and P45 (outputs.md, "The FPGA
bitstreams"), and whose netlist reads as the Z3805A's at the port, the
count, the divider and the sync, differing besides those pins in the
internal 1 PPS's width (`firmware/58503a-z3801a-trace.md`), none of which
the model drives, so it runs as the Z3805A's, not checked against its
own netlist ("Not yet").  The Z3817A's says `z3817a`, its own design,
a model of its own ("The interval counter").  `design = "unmodeled"`,
which `--fpga-design` may still ask for, configures and reads back,
but no design runs, its port reading 0 and no internal 1 PPS coming,
so the firmware finds the FPGA dead; any other name is refused.

The Z3815A's board file, `z3815a.toml`, has no model of its own and
runs as `--model z3816a --board boards/z3815a.toml`, the host port on
the SCI, its GPS engine `furuno-gt74`, its FPGA on the LCA model
(`design = "lca"`, `bitstream = "z3815a"`, below), its EEPROM the
synthetic image ("The EEPROM").  Its ADC gives the eight channels the
health monitor's table, `hmon_channels` at `0x33c9c`, names, each a
steady `dc-source` at the level the entry's convert routine turns into
its nominal (12A, 5V, 12C, -12A, -12C, -12E, the oscillator current,
the antenna current; *inferred*, the dividers open), which the QSPI's
ADC test and the supplies test, POWER, read.  Its OCXO is a module the
code calls the puck, on a bit-serial link the health monitor clocks a
bit a pass (PE7), sampling PE5 while PE2 is high
(firmware/z3815a-4010.md, "The oscillator and references"): a frame of
the Z3817A's oscillator's report, `0x55`, a 24-bit reading of the EFC,
a status byte whose bit 0 says warm, and a sum, which
`clocked-efc-monitor` sends ("The mc683xx device layer").  With the
switch word's bit 12 set, as the board's levels give it, power-up's
OCXO warm-up waits for that bit, a good frame within 10 s and 30 s of
uptime (`ocxo_warm`, `0x33c3a`; `puck_warm`, `0x36054`); the file's
puck says warm at once (*inferred*) and reads the EFC as the
firmware's own default calibration expects, 16 counts a unit of its
EFC (`efc_calibration_defaults`, `0x45038`; *inferred*).  The file
gives its oscillator the loop's own G, the calibration's a with bit 12
set, +1.2e-12 a count (`0x45046`; firmware/loop.md, "In every
image"): without it the `ocxo` part takes `--model z3816a`'s G,
-1.25e-12, of the other sign, under which the loop locks and then runs
away (*inferred*: the part's gain is open).  The 4010
boots, answers `*IDN?` `HEWLETT-PACKARD,Z3815A,0000A00000,U-4010.0-A`,
runs its `$PFEC` init with the GT-74, names it ("MODEL # FURUNO
GT-74"), tracks six, passes every power-up test (`+0`, by the 4010's
table at `0x28e54`, PROC to POWER from bit 1; *inferred*: bit 0 the
Z3816A's DISP, which it lacks), leaves its warm-up and locks:
`tests/boot_z3815a.rs` checks the boot and, ignored but when asked,
`+0` and `LOCK` at 300 s, `:ALARm:HARDware?` `+0` and a status screen
not headed `[ ALARM ]`.  The switch word's bit 3 reads low, the low
byte `0xf7`: its one reader, `alarm_hardware_update`, logs "PAL Fail"
with it high while the loop runs and the clocks are good (`0x34258`),
and the output goes to the hardware fault, which it never leaves, the
status screen headed `[ ALARM ]` with every status register clear; a
sound board must give it low, though what drives it is open.  The
word's other bits read high (*inferred*): bit 0 makes the output's
status active, so the screen is headed `[ Active ]` (bit 1 sets its
standby bit too, which active outranks, `output_state_get`,
`0x340fa`), and bit 10 names the card GPSR 2, the redundant pair's
arbitration a lone card's backplane decides, also open.  With
`--fpga-design unmodeled` its result is `+448`, the FPGA's, the
interpolator's and the internal reference's tests (bits 6 to 8), and
it stays in power-up (`POW`), which the test checks on every run.
The design's inputs only the Z3815A has, P23, P27, P35, P50, P78 and
P84, are held Low as its bench holds them (`board/fpga/pins.rs`,
`ACLK_Z3815A`; *inferred*, their sources open): P78 and P84 are the
start sources register 2 selects but the GPS 1 PPS, which the
firmware selects only in the standby card's stage, `n3 lock pll`,
never entered with the switch word's bits 4 and 5 both set
(firmware/z3815a-trace.md, "What the firmware takes from
P35 and P84").  A test, `each_board_file_names_its_images_design`
(`board/fpga_part.rs`), keeps the names honest: each board's images
carry the bitstream of the design it names,
`third_party/firmware/images/fpga/NAME.bit`, and an `unmodeled`
board's carry no modeled design's.  `design = "lca"` runs the
bitstream itself instead, the LCA model (`firmware/fpga-codegen.md`), as
the Z3816A's and the 58503B's boards do:
the configuration the firmware sends picks the netlist at DONE by its
hash (`board/fpga/netlists.rs`), a trace line saying which, and is
refused, DONE Low and the run stopped with a line saying why, if the
model does not know it, the board does not wire its design's clocks
and held inputs (`board/fpga/pins.rs`, `Wiring`: not yet the
Z3817A's, whose internal 1 PPS is on P78, not P83,
beside a second 1 PPS in on P84 and no interval; the Z3816A's and
the Z3815A's their ACLK, P53, driven ideally from the OCXO,
the PLL not modeled, `firmware/fpga-codegen.md`), or it is not
the one an optional `bitstream = "z3805a"` asserts.  Every bitstream of the repository's
has its netlist; fast-forwarded between events, the model is still
slower than the hand model (`make bench-lca`), so only the board
files with no hand model of their design say `lca`, the Z3816A's, the
Z3815A's and the 58503B's (`firmware/fpga-comparison.md`), with
`bitstream = "z3816a"`, `"z3815a"` and `"58503b"`, and the hand models stay the
default elsewhere: `--fpga-design lca` asks for it in a run, on
mc683xx, and `--fpga-design` with another design on a board file that
says `lca` drops its `bitstream`, which only the LCA model takes
(`mc683xx/load.rs`, `fpga_design`).  Its reads take mc683xx's
read-time horizon: sampled by the netlist itself where nothing can
reach the part before their bus cycle ends, else by a copy of it, the
same answer either way unless an input lands within the cycle
(`firmware/fpga-codegen.md`, "A bus access is a bus cycle").
`tests/boot_lca.rs` checks that the Z3805A's configuration picks its
netlist and the 58503A's the one it shares with the Z3801A, and, ignored but when asked, that
the Z3805A boots on it and answers `*IDN?` as on the hand model;
`make bench-lca` prints how much slower the board runs on it, natively
and in the browser's module, both with the synthetic GPS engine.  The 58503A's reset
code sets up no chip select 4 (58503a-3704.md, "Chip select 4";
restart.md, "The chip selects"), but the Z3805A's addresses nothing there either, so the map
needs no change.  What differs is the 58503A's (58503a-3704.md,
"Hardware"):

- *The latch's lamps*, above ("The lamps").
- *The front panel*, Option 001's display and keys
  (`board/front_panel.rs`), on QSPI device 0, all four PCS lines low
  (58503a-3704.md, "The front panel"): the latch's bit 7 low holds it
  in reset, bit 6 low frames a command, the first byte the command,
  the rest its data: `0x01` the key lamps and a line of text, `0x02`
  the key lamps, `0x04` text, `0x00` a key poll, `0x08` and `0x10` the
  self-test.  The byte shifted back with a command is a key, 1 to 8 in
  the panel's order, 0 for none.  Which command a reply answers the
  code does not settle; *inferred*, the model answers each with what
  the command before loaded: `0x55` after `0x08`, `0xaa` after `0x10`,
  else the key pressed since, once.  So `0x537ea`'s self-test (`0x00`,
  `0x08`, `0x10`, `0x00`, wanting `0x55` then `0xaa`) passes, and
  `root_task` starts the `fpnl` task.  The panel's controller is not
  traced; this is the least that passes, not its protocol.
  `qspi_write` returns once its transfer has started, and `0x53796`
  raises bit 6 after the last byte's: on the board that byte, 2 µs at
  the panel's 4.19 MHz, is through first, but the QSPI's model takes a
  fixed span a queue, so a frame that closes with a transfer to the
  panel in flight ends when the transfer comes.  How mc683xx clocks it
  is in "The mc683xx backend".
- *The alarm output*, PF5, low while the alarm is on (58503a-3704.md,
  "The alarm outputs"), a pin not driven reading high.

With `--indicators` the alarm output is reported as the LEDs are, bank
`output`, and the display as a record of its own each time a command
changes it, the key lamps (key n's on bit n - 1) and the text, a
space, a backslash or anything not printable ASCII as `\xHH`:

    smartclock-emu: indicator cycle=... bank=output name=alarm lit=1
    smartclock-emu: display cycle=N lamps=0x00 text=HP\x2058503A

A script presses a key with `key NAME` ("Scripts").  The panel is a
board of its own, held in reset by the latch: a watchdog reset leaves
it as it was.  The other differences the code shows need nothing of
the board: 3704 drives no SCI and none of the GPT's time-code
registers; it writes the DUART's OPCR, which the model ignores; the FPGA's register 3 bit 2 follows the alarm and
`fpga_set_bit` sets bits there before the interpolator's calibration,
written to the model and unused; and the routines that read ports
until steady have no caller (58503a-3704.md, "Reads until steady").

The Z3801A's board (`boards/z3801a.toml`) is the Z3805A's: its image is
the Z3805A's less the SCI driver and the ES message
(firmware/z3801a-3543.md), with the same chip selects and port setup,
and every routine that drives a part relocated from the Z3805A's, so
the file attaches what z3805a.toml does, and marks each part not seen
on this board as followed from the code (boards/README.md).  What
differs:

- *No SCI.*  The image's one reference to the SCI clears SCCR1
  (console.md, "The Z3801A image"), so its board file has no `sci`
  endpoint, and `--sci` and `--sci-capture` are refused on mc683xx.
- *The host port, 7O1, without echo.*  The port is fixed at seven data
  bits, odd parity (097-z3801-01 1-8, 2-10), as the NV record sets
  channel B (MR1B `0x06`; the defaults `duartb_settings_defaults`,
  firmware/z3801a-3543.md, "How it differs from the Z3805A"), and with the EEPROM dump's
  record its echo is off.  mc683xx's stock DUART puts the parity bit on the
  line, so the host port's far end frames each byte as a 7O1 terminal
  does (`Model::host_framing`, `mc683xx/endpoints.rs`'s `frame_bits`): the
  parity bit above the seven data bits going in, taken off coming out.
  `smartclock-cli` reaches it over `--host` as any other.
- *The FPGA's bitstream*, the 58503A's, and the four flags in its
  register 2 that only the Z3801A's `fpga_bit_table` names
  (firmware/z3801a-3543.md, "How it differs from the Z3805A"): written to the model,
  which keeps no register 2 ("The interval counter").
- *The GPS engine*, a six-channel B1121, as the Z3805A's: the image
  knows only the six-channel `@@Bn` (console.md, "The Z3801A image").

Its lamps are the Z3805A's, the same `leds_update` relocated, and it
has neither the 58503A's front panel nor its alarm output.

The Z3816A's board (`boards/z3816a.toml`) is its own, on mc683xx
alone, as the 58503B's is: the second
family's map (`firmware/chip-selects.md`), the host port on the SCI, no
board seen, every part read from the 4001 image or inferred as the
board file marks it.  What the boot needs of it:

- *The FPGA*, `design = "lca"`, `bitstream = "z3816a"`: no hand model
  is its design, so its netlist runs, on ACLK, P53, which the board
  drives ideally at the OCXO's rate times 12,288 / 6250; the PLL is not
  modeled at all, P7 held Low, so its P7 divider and its phase
  detector, P38 and P40, are constant, and its sync steps are
  *inferred* from the firmware's writes of register 3 (`pins::Steps`;
  `firmware/fpga-codegen.md`, "The Z3816A's clocks on the board").
- *The supplies' monitors.*  The health monitor's table (`0x32596`)
  reads eight ADC channels, `adc_read` (`0x2e3b0`) the 58503A's, so
  channel n is the ADC's CHn: 12B, 5V, 12C, -12B, -12C and -12D, each
  converted by its own scale and checked against its limits, the
  oscillator's current and the antenna's.  Without them every rail
  reads out of tolerance and the power-up self-test fails.  Each rail
  is mc683xx-board's built-in `dc-source` (`volts` on its analog
  output `out`) at the level whose count the conversion makes the
  table's nominal; the current is the oscillator model's, read at the
  Z3816A's scale, 4.489 a count, so cold it is past the table's 650
  until the oven settles.
- *The EFC.*  The image writes the AD569's word on QSPI device 7 about
  a hundred times a second, dithered around its value, with no strobe
  after it, so the board file ties every latch of the DAC transparent
  (*inferred*).  Its gain is G with bit 8 of the switch word at
  `0x302000` set, as the board file's `switch_high` reads,
  −1.25 × 10⁻¹² (firmware/loop.md, "τ and G"), so the run's oscillator
  takes it (`Profile`'s `efc_gain`).
- *The GPS engine*, eight channels: the image takes `@@Ea` and `@@En`
  and asks `@@Ea` first (firmware/gps.md, "Six or eight channels").
- *The EEPROM*, the synthetic image ("The EEPROM"): no dump of a
  Z3816A's exists.  `*IDN?`'s serial number is its made-up
  `0000A00000`, and the power-up self-test's EEPROM test passes; from
  an erased part (`--eeprom-dump /dev/null`) the firmware writes its
  defaults and the log, the serial number is 0 and the test fails.
- *The port's time code.*  At power-up the SCI sends the continuous
  time code (`:PTIMe:TCODe:CONTinuous`, its cell `0x102532`), a binary
  frame each second, and while it is on the SCI's write (`0x2f10e`)
  drops everything else: the SCPI task reads, echoes and answers, and
  its answers and prompts go nowhere.  `:PTIMe:TCODe:CONTinuous OFF`
  stops the frames, and the prompt and every answer after come.  A
  client of the Z3816A sends it first, or the emulator does with
  `--time-code-off`: once the host port has sent the power-up's first
  frame, its CR, the command goes on the port as a client's, its echo
  and the prompt following, and again after each reset or power cycle,
  which start the time code again (`mc683xx/time_code.rs`, a trace
  line `time-code-off cycle=N step=send`).  The default is a real
  unit's, the time code on; the option is refused on the other models,
  whose images do not start it at power-up (`Model::starts_time_code`).
  A TCP client that connects between the command and the firmware's
  taking it drops it, as it drops what an earlier client left.  The
  EEPROM does not set it:
  the cell is the last of the initialized data `system_init` copies
  from the image (`0x22cc8`, `0x95` bytes from `0x400de` to
  `0x10249e`), its byte `0x01`, and `nv_save` keeps no descriptor of
  it, so the time code is on at every power-up whatever the EEPROM
  holds (firmware/console.md, "Which port").

On its board's LCA model every power-up test passes
(`:DIAGnostic:TEST:RESult?`, a bit a test of the table at `0x28888`),
the FPGA's, the interpolator's and its reference's among them, but
the EEPROM's (bit 3) when the part starts erased.  The QSPI's (bit 5)
passes on the EFC chain clocked on SCK's fall, the stock `74x164`'s
`clock = "falling"` ("The mc683xx device layer"): its EFC test (`0x28b38`, bit 7 of the QSPI's
result, `:DIAGnostic:TEST? QSPI`) wants the word read back a bit
early, and the stock 74x164 gives it as it was.  With
`--fpga-design unmodeled` the FPGA's and the interpolator's fail.
With the warm-up skipped the engine tracks six and the loop goes
through its stages to `LOCK` on the LCA model.  Its phase alignment
steps the internal 1 PPS by the sync field's kinds 1 and 2, a write
every two seconds, which step the PLL's P7 divider rather than the
1 PPS divider.  The PLL is not modeled, so the board makes the step
itself, *inferred* from those writes (`pins::Steps`), moving ACLK and
so the 1 PPS a tick of the 10 MHz each (`firmware/fpga-codegen.md`, "The Z3816A's
clocks on the board").  With no design the FPGA's tests fail and the
loop never leaves its power-up's GPS acquisition.

It has no lamps the emulator names: the latch's eight bits are lit in
turn by the lamp test at `0x2871e`, and what each means is not traced;
the heartbeat is DUART OP6 (outputs.md, "The heartbeat").
`tests/boot_z3816a.rs` boots it with the time code turned off to its
prompt, `*IDN?`, `:SYSTem:ERRor?` and the self-test's result, on
every `cargo test` with `--fpga-design unmodeled`, the session
sending the command or `--time-code-off` sending it, the transcripts
the same, and without either finds no prompt; ignored but when
asked, on its own board's LCA model, and runs it there with the
warm-up skipped to `LOCK` and the self-test's result
(`cargo test --release -p smartclock-emu --test
boot_z3816a -- --ignored` shows how long they take).

The 58503B's board (`boards/58503b.toml`) is its file's alone, on
mc683xx; Unicorn's board in code is the DUART boards', so `--core
unicorn` is refused for it.  What the file has that the others do not,
each from its image (`boards/58503b.toml` gives the addresses):

- *Its own map*, the Z3816A's family less a DSACK pin: the latch on
  CS1, an 8-bit port, the FPGA on CS5, the switch byte on CS7.
- *An ACIA for the GPS engine*, an MC6850 on CS8's E-clock cycles,
  mc683xx-peripherals' stock `mc6850` ("The mc683xx device layer"), at
  the level 2 autovector; the host port, SCPI and the console, is the SCI, which
  the file binds to `external.host`.
- *The FPGA's own design*, run by the LCA model, the board's 10 MHz on
  P7 (`Wiring`, above).  Its boot is slow on it ("What the boot
  shows").
- *The synthetic engine as a UT Plus Oncore*, the R5122U1152 a
  shipped 58503B reports: the image takes the `R` of its `@@Cj` model
  for a UT Plus, counts eight channels, sets it up with `@@Ea` and
  `@@En` and sends it only commands the UT Plus lists, `@@Bo` among
  them, none of the VP's (firmware/gps.md, "The UT Plus").  With
  `--gps-engine b4121` it is the eight-channel VP B4121 instead: the
  image's self-test script finds it by its `@@Ea` and sets it up with
  `@@Ea` and `@@En`, and its init scripts ask `@@Ta` of it, which the
  engine answers ("The synthetic GPS engine").  The second `@@Ar`,
  argument 65, goes to software 8.4 alone (firmware/gps.md, "Six or
  eight channels"), so not to the B4121's 8.8.
- *The 58503A's lamps, front panel and alarm*: GPS Lock, Holdover and
  Alarm on the latch's bits 0 to 2, Option 001's panel on bits 6 and 7
  and QSPI device 0 with the 58503A's command framing and self-test,
  the alarm on PF5, each read from the image; the heartbeat on the
  latch's bit 3.
- *The oscillator the dump describes*, so that the board's is the
  unit's.  Its calibration block keeps the unit's own G, the slope its
  EFC calibration measured (`:DIAGnostic:ROSCillator:EFControl:ASLOPe`;
  firmware/loop.md, "In every image"), about −6.684 × 10⁻¹³, in the
  float at `+0x10`, which the firmware takes in place of oscillator
  type 0's −1.25 × 10⁻¹² because the block's type, `+0x1c`, is the one
  in force (`0x40baa` to `0x40c1e`; `:DIAGnostic:ROSCillator:TYPE?`
  +0); and its lifetime record the EFC the loop last locked at,
  636777.5625 counts, the float at `+0xa` (`0x408fe`), which `pll_init`
  starts the loop from.  So the run's oscillator takes that G
  (`Profile`'s `efc_gain`; `board/profile.rs` reads it from the bytes,
  and its test holds it to the board file's image) and, as every
  board's, the offset at the EFC's middle that puts it on frequency at
  that EFC, about 7.52 × 10⁻⁸ ("The oscillator the EEPROM
  describes").  On type 0's G instead, the board's
  oscillator's gain not the firmware's, the loop stayed in its power-up
  stage, fine frequency adjustment, its TI drifting, well past the
  lock test's time; erasing either record also let it lock.  The synthetic
  image's oscillator is type 0's (`--ocxo-efc-gain -1.25e-12`), its
  offset 0, the image's lifetime record being erased.
- *A 58503B's EEPROM*: the run's working file starts from its dump,
  scrubbed ("The EEPROM"), its serial number `L00000`; from an erased
  part the firmware writes its defaults and its parameter record, its
  serial number 0.

The ADC's channels, the temperature, the supplies, the interpolator
and its trims, and the EFC's word are the 58503A's wiring, marked
inferred; the switch byte reads `0xff`, and the latch's bit 4 ends at
nothing.

The Z3817A's board (`boards/z3817a.toml`) runs on mc683xx alone, with
`--board` and another model's `--model`: there is no Z3817A model.  Its parts are the image's (z3817a-3752.md, "The
board as the image drives it"): the second family's map without the
switch word, the EFC's strobe on CS7, the QSPI's codes 5, `0xa` and
`0xc`, an ADC of supplies alone, no hex DAC, the host port on the SCI.
Three parts are its own:

- *The time references*, `timeref-selector`: TIMEREF1 and TIMEREF2,
  each a pulse (none, 1 PPS or PP2S, changing to another at a given
  second if the board file says) and a VALID level, and the
  selector PE7 drives, which passes the selected reference's pulse to
  PAI and the FPGA's P82 and its VALID to PE5.  The board file's are
  the bench's of 2026-11-09, a 1 PPS and VALID on TIMEREF2 and PE7 high
  selecting TIMEREF1.
- *The oscillator's link*, `efc-monitor` (the Z3815A's puck clocks the
  same report on port E, `clocked-efc-monitor`, below): the oscillator's six-byte
  report of the EFC it reads, each second on DUART channel B, without
  which the firmware fails the unit in ten seconds, its status byte's
  ready bit set once `ready_seconds` of the board's warm-up are behind
  it, without which power-up never leaves its OCXO warm-up: 784 s in
  the board file, which puts the log's "GPS lock started" where the
  bench unit's log has it (z3817a-3752.md, "The warm-up").  The
  `skip-warm-up` control and `--skip-warm-up` set it at once, as they
  settle the oven ("The controls").  The oscillator's own warm-up and
  noise are the board file's `ocxo` terms, none in this one, or the
  command line's (`--ocxo-*`, "The oscillator"), which take their
  place when given.
- *The EFC's shift register*, `efc-shift-register`: sixteen stages
  shifting on SCK's fall, which the image's EFC self-test needs.

The synthetic engine on channel A is the eight-channel one without
Time RAIM (`engine = "b4121-no-time-raim"`), deaf (`answers = false`),
so that a script's `expect-gps` sees what the module sends while
nothing answers, and saved as a 58503A leaves its engine
(`saved = "58503a"`), so that it streams `@@Ea`, `@@En` and `@@Bb`
unasked from power-up ("The synthetic GPS engine").
The FPGA runs the Z3817A's own design, `z3817a` ("The interval
counter"): the counter's and the internal reference's self-tests pass,
and the loop measures the interval, leaves power-up and locks
(z3817a-3752.md, "Emulated").  `tests/boot_z3817a.rs` runs the board
file as it is, its unasked stream, to the GPS record's qualifying; the
bench's feed of 2026-11-09, nothing on RXD (the engine without
`saved`), to the reference the hunt finds, and a variant of it with
PE7 on TIMEREF2 and no warm-up to lock; and checks that nothing goes
out on channel A; runs too long for every `cargo test`, ignored, take
the bench's feed and a PP2S variant on to lock.

### Adding a board

A model is a `Model` variant, with its `--model` name, and a
`Profile`, a constant in `board/profile.rs` that `Model::profile`
returns: its name, which is its board file's, `boards/NAME.toml`, and
that file's `[board]` name; its `identity`; its lamps, its Unicorn board's GPS engine, its
front panel and alarm output, its host port's framing, whether the
Unicorn board is its board, its firmware's EFC gain, and its
variants, the board files of its own for one revision's images
("The boards").  The
compiler then asks for its drawing (`panel/state.rs`, `Drawing::of`,
and a face in `drawings.js`, "The drawings").  The board file is the
rest, and the tests hold it to the profile: its name, its images'
`identity`, its engine's name and its FPGA's `design`
(`each_board_file_names_its_images_design`).  The hand models are the
Z3805A's design and the Z3817A's alone, so a board whose bitstream is another's says
`design = "lca"` and `bitstream = "NAME"`, its netlist's, which the
board must wire (`Wiring`, above).

## Time

The board's time is its own, never read from the wall clock: on
mc683xx board time in picoseconds, on which the chip's system clocks
run at 16.777216 MHz once the reset code sets SYNCR, and at the
reset's slower default before ("Tracing"); on Unicorn the core's
estimate in CPU cycles ("Its board and its time").  The firmware's
periodic timer ticks 1024 times a second of it.  The run keeps it in
one of two modes:

- *Paced*, the default: on mc683xx the board runs a frame at a time to
  the board time the wall clock has come to (mc683xx's
  `Board::run_frame_with`), never ahead of it, the binary waiting 5 ms
  between frames once the board has caught up; on Unicorn it runs no
  more than 20 ms ahead of the wall clock, waiting when it would.  A
  host too slow to keep up (a heavy trace) falls behind; past a second
  behind the emulator says so, once, and the firmware's clock runs
  slow by as much.  The wall clock is the host's, read by the binary
  and handed to the run each frame, never read by the board.
- *Deterministic*, `--deterministic`: flat out, never waiting.  A run
  is the same each time, to the cycle and the instruction, given the
  same image, the same working EEPROM, the same `--seed` and the same
  input at the same board times; with no client connected, a boot is,
  and so is a session piped to standard input.  What a client
  sends arrives when the wall clock has it there, so a session over
  TCP is not repeatable; a script's is ("Scripts").  `tests/tooling.rs`
  boots twice and compares the traces.

`--stop-after SECONDS` ends the run once the board's clock reaches it,
with a last record, on mc683xx mc683xx-board's:

    mc68331: stopped cycle=N instructions=N reason=budget

A CPU that halts (a double bus fault, or on Unicorn an access the core
cannot take an exception for) ends the run with an error, the emulator
exiting nonzero, in one form on either core, the address in its 32
bits:

    Error: the CPU halted at 0xNNNNNNNN: a double bus fault

## Scripts

`--script FILE` puts a script on the host port in place of a client,
its input timed by the board's clock, so that a session replays the
same each time: with `--deterministic`, the same image, EEPROM and
`--seed`, and `--gps` or a fixed `--gps-start` (the synthetic engine's
UTC is otherwise the host's clock), to the cycle and the instruction.
The host port is then not served, and `--host` is refused.  A script
is a file of steps, one a line, taken in order, each once what it waits
for has come; a blank line or one starting `#` is nothing:

    at SECONDS      board time since the run's start (across a power
                    cycle, "The controls")
    after SECONDS   board time since the step before
    expect TEXT     the port to have sent TEXT since the last expect
    expect TEXT within SECONDS
                    the same, failing the run if it has not within
                    SECONDS of board time since the step before
    expect-gps TEXT [within SECONDS]
                    the same of what the firmware sent the synthetic
                    GPS engine, on DUART channel A, since the last
                    expect-gps
    send TEXT       TEXT and CR LF, as the library's session sends a command
    write TEXT      TEXT alone
    write-gps TEXT  TEXT to the firmware on the GPS engine's line, as
                    though the synthetic engine had sent it, after what
                    it has already sent
    key NAME        a front-panel key pressed: time, long, lat, alt,
                    sat, status, serial or shift (the 58503A's; "The
                    boards"); with `at`, a key clicked on the panel
                    page replayed ("The panel page")
    event NAME      a simulation control: antenna-off, antenna-on,
                    skip-warm-up, power-cycle or reset ("The
                    controls"); with `at`, one clicked on the panel
                    page replayed
    stop            the end of the run, with its stop record

TEXT is the rest of the line, spaces and all, with `\r`, `\n`, `\t`,
`\\` and `\xHH` for what a line cannot hold (`\x20` for a trailing
space); an `expect`'s TEXT ends before its last ` within ` when what
follows is a number of seconds.  A step is looked at after each of the
machine's slices, and a step's time, an `at`'s, an `after`'s or an
`expect`'s limit, ends a slice, as a device's event does, so it is
taken at its time, or past it by the block of code the core was then
running, never the wall clock's; what it sends starts on the line
then, its first character's start bit at that instant.  An `expect`
without `within` waits for good, or until `--stop-after`; one not met
within its time ends the run with an error naming its line, and the
emulator exits nonzero.  An `expect-gps` waits on the firmware's
progress rather than the board's time, the GPS init script's last
exchange, say, so that two runs whose timing differs, on the two
cores, cover the same work: it sees
the bytes as the engine reads them, on either core, and with `--gps`,
no synthetic engine, never matches.  A `write-gps`'s
bytes go out whole at the step's time, the engine run then, after
the messages then due, never inside one;
with `--gps` they go nowhere.  What the port sends goes to standard output as
it comes, the transcript, and each step taken is a record on standard
error:

    smartclock-emu: script cycle=N line=3 step=send

A line a script gives before the firmware enables the port's receiver
is lost, as on the board ("What is modeled", the DUART), so a script
waits for the boot first:

    at 5
    send
    expect scpi >\x20
    send *IDN?
    expect scpi >\x20
    stop

`tests/script.rs` runs a session twice and compares the transcripts
and the traces.  `tests/gps_framer.rs` gives the 58503A an `@@Bh`
with `write-gps`, whose table length of 0 overruns the firmware's
framer into a bus error and the watchdog's restart (firmware/gps.md,
"Framing"); on mc683xx alone, since Unicorn halts at the stray store.

## The panel page

`--panel ADDR` serves a page, on the mc683xx core, that draws the
receiver's front panel and lights it as the run does, and presses the
keys clicked on it.  ADDR is an address, or a port alone, which binds
127.0.0.1 and nothing else; standard error says where:

    smartclock-emu third_party/firmware/images/58503a-3704.bin \
        --eeprom target/smartclock-emu/58503a.bin \
        --eeprom-dump third_party/firmware/images/eeprom/58503a-3710A01056-eeprom.bin \
        --panel 8080
    smartclock-emu: panel on http://127.0.0.1:8080/

It serves with `smartclock::http`, the exporter's and the web view's
server, which checks neither `Host` nor `Origin` (PLAN.md, "Maybe: a
Host allowlist for the web view"): any page the browser opens could
press a key, so bind it to a public address only on a network of your
own.

The page is a few files in `src/panel/page/`, every one built into
the binary and served by the run, nothing fetched from elsewhere:
`panel.html`, its style `panel.css`, its drawings `drawings.js`, the
display's font `font.json`, and its script `panel.js`.  What the page
shares with the run it takes from the run rather than keeping a copy:
`/constants.json` gives how often it polls (`poll_ms`), the console's
longest line (`line_limit`) and how much of the port's output is kept
(`kept`), the controls' names, the typists' (below), and the
console's quick lines.  Every 150 ms the page asks `/state` for a JSON
object, which the run publishes 64 times a board second:

    {"drawing":"58503a-option-001","cycle":N,"seconds":S,
     "indicators":[{"bank":"lamp","name":"gps_lock","lit":false},...,
                   {"bank":"led","name":"heartbeat","lit":true},...],
     "display":{"lamps":32,"text":"HP 58503A"},
     "keys":["time","long","lat","alt","sat","status","serial","shift"],
     "controls":{"names":["antenna-off","antenna-on","skip-warm-up",
                          "power-cycle","reset"],
                 "antenna":true,"warming":true}}

- `drawing`: which panel to draw, by the board's model (below).
- `cycle`: the chip's system clocks, as the trace counts them;
  `seconds`, board time since the run's start, which a power cycle
  does not set back ("The controls").
- `indicators`: each lamp-board lamp and LED-bank LED whose net the
  board has driven, by its bank and name as `--indicators` gives them
  ("The lamps", "The LEDs").  The page lights the panel's lamps from
  bank `lamp` and lists the board's LEDs, bank `led`, below it.
- `display`: the front panel's key lamps, key n's on bit n - 1, and its
  text, as the `display` record gives them, a byte not printable ASCII
  as `\u00HH`; `null` without a front panel.  `keys` names the keys in
  the lamp byte's order, empty without one.
- `controls`: the simulation's controls ("The controls"): `names`,
  those that apply to the run; `antenna`, whether the antenna is
  connected, and `warming`, whether the warm-up curve is still short of
  its end, each `null` where its control does not apply.  The page's
  control strip, under the status line and apart from the drawing,
  shows those and posts them.

Power is not the firmware's (58503a-3704.md, "The front panel"), so the
page lights it while `/state` answers and puts the panel out when it
stops, saying "not answering" where it said "running".  An answer not
back within 2 s (`ANSWER_MS` in `panel.js`) counts as none, so a run
that hangs, its server taking connections and answering nothing, is
shown as one that has ended is, not as the last state it answered.

The run owns the board.  It publishes a copy into a mutex, and the
server's threads read that copy and never the board.  A click posts
`/key?name=NAME`, a name as a script's `key` step takes it, the one
the state's `keys` gives the key, and nothing while the run is not
answering or for a key the state does not name; it goes into a queue
of 16; the run takes the queue without waiting at the end
of each slice, or of a paced run's quantum, where a script's keys are pressed, presses each through
the panel's `press`, as a script's key, and traces it:

    smartclock-emu: key cycle=N at=S name=sat

`cycle` is the chip's, as the trace counts it; `at` is the run's
clock as a script's `at` step reads seconds, cycles over 16777216
written out exactly, so the record replays.  The two are the same
until a power cycle starts the chip's again ("The controls").

### Replaying a run

The page's `key` records, its controls' `event` records ("The
controls") and its console's `console` records ("The console") are
each a script's `at` and a step: `key`, `event` or, for a console
line, `send`.  With the same image, the same working EEPROM, `--seed`,
a fixed `--gps-start` and nothing else sent on the host port, a script
of the records, paced or `--deterministic`, presses the same keys,
applies the same controls and types the same lines at the same cycles.
A paced run takes what the page gives at the end of one of mc683xx's
5 ms quanta of board time, not of a slice ("The mc683xx backend");
each record names the cycle it was taken at, so its replay takes it
there all the same (`tests/panel.rs` replays a paced run's records
deterministically and compares the displays):

    grep -oE '(key|event|console) cycle=.*' run.err | while read -r kind _ at what; do
        echo "at ${at#at=}"; echo "${kind/console/send} ${what#*=}"; done > replay.script

### The page's requests

Each path takes its own methods: the page's files, `/constants.json`
and `/state` a GET; `/key` and `/event` a POST; `/console` either.
Any other is refused 405, with `Allow` naming those it takes.  A
request's body past 64 KiB is refused 413, as by the exporter and the
web view, which share the server.

The page asks with `ask(path, options)` (`panel.js`), `fetch` with a
timeout; a host that answers the page's requests itself rather than
serving them, the browser's build ("In a browser"), sets
`smartclockAsk(path, options)` before the script loads, and `ask` calls
it instead, a `Response` back as from `fetch`.

`tests/panel.rs` clicks Sat on a paced run's page while the 58503A's
banner scrolls, replays the record under `--deterministic`, and
compares the two runs' `display` and `indicator` records; and does the
same with two controls, the antenna pulled and the power cycled.

### The drawings

The drawings are our own line drawings from photographs of the bench
units, not HP's artwork, picked by the model; where each one's
proportions, layout and the display's font come from is "The panel's
drawings":

| `drawing` | Model | Panel |
| --------- | ----- | ----- |
| `58503a-option-001` | 58503A, a board with the front panel | the dark window with the title and the display, STATUS's four lamps, MODE's eight keys each with its lamp |
| `58503a` | 58503A, a board without one | the standard panel: the title and the four lamps over a blank window |
| `z3805a` | Z3805A | the black band with the title and six LEDs over a light window |
| `z3801a` | Z3801A | a light plate with six LEDs, two thumbscrews and a handle |
| `z3816a` | Z3816A | the Z3805A's band and window with the Z3816A's heading and the power LED alone: no photograph or manual of one is here |

The drawing follows the model rather than a key of the board file,
which says what is wired where, not how the panel looks; a board run
with another model's `--model` is drawn as that model's.  The Z38xx LEDs are green but Alarm, red, and Holdover,
amber, as the bench Z3805A's.

The display is twelve characters of 14 segments, a point, a comma's
tail and a colon's upper dot, drawn with the unlit segments dim as a
vacuum fluorescent display shows them.

## In a browser

The 58503A runs in a browser too, the board and its firmware in the
page, on WebAssembly, with no server: `crates/smartclock-emu-web`, the
emulator's library built for wasm32-unknown-unknown and bound by
wasm-bindgen.  The pages workflow publishes it at <https://charlieh0tel.github.io/smartclock/emulator.html>.  To build
and open it locally:

    make emulator-web          # target/emulator-web, a static directory
    cd target/emulator-web && python3 -m http.server 8000 --bind 127.0.0.1
    # then open http://127.0.0.1:8000/

It needs the wasm32 target (`rustup target add wasm32-unknown-unknown`)
and a wasm-bindgen CLI of the crate's exact wasm-bindgen version,
`WASM_BINDGEN=PATH` if it is not `wasm-bindgen` on the path;
`tools/install-wasm-bindgen.sh DIR` puts the release's into a
directory, as CI does.  A page opened as a file, not served, cannot
load its module.

`make emulator-web` builds the crate `--locked`, in release, binds it
`--target web` without wasm-opt into `pkg/`, and copies the panel
page's files, the board file, the image and the EEPROM's dump beside
it, the last three at their paths in the
repository, which the board file names its own by.  Its `index.html` is the panel page's with `host.js` before the
page's scripts: it sets the `smartclockAsk` the page's `ask` calls ("The
page's requests"), which `loader.mjs` answers from the run.

The loader fetches the files and starts the run with command-line
arguments, the image and, from a kept EEPROM, `--eeprom-protected`,
parsed by the native
command line's rules for the options every host takes
(`options::RunOptions`: `--model`, `--board`, `--fpga-design`, `--gps-*`, `--seed`,
`--ocxo-*`, `--skip-warm-up`, `--time-code-off`, `--stop-after`, `--indicators`, `--trace`, `--watch`,
`--watch-code`); the synthetic engine's start, without `--gps-start`,
is the browser's clock, `Date.now()`.  It steps the run a frame at a
time from `requestAnimationFrame`, to the page's clock, a frame running
no more than 50 ms of board time, so that a tab hidden a while drops
the time it missed rather than run it all at once.  The host port is
the page's console; no port is served over TCP, which a browser has
none of, and none is asked for but by a board file's own (the
Z3805A's SCI), which is then not served, and said so.  The trace's
lines go to the browser's console.

The EEPROM is in memory ("The EEPROM"), from the bench unit's dump, and
the loader keeps its cells, 8 KiB, and its protection as they stand in
the browser's `localStorage` every ten seconds and when the page is
hidden or left, so the next visit starts from them, the cells given as
the board file's EEPROM image and the protection as
`--eeprom-protected`, settings and logs as the firmware left them; what
is kept but is not an EEPROM's, or that the part refuses, is dropped,
and the dump started.  The page's own strip, below the simulation's, says
which it started from, and forgets the kept one, keeping none again
until the next visit.  `use my location`,
which asks the browser for its position only when clicked, starts the
run again from the EEPROM as it stands with the synthetic engine's
antenna there (`--gps-latitude`, `--gps-longitude`; the height stays the
default, a browser's being the ellipsoid's), as a power cycle with the
antenna moved, and keeps the place for the next visit.  A panic is
written to the console and stops the run, said on the strip.

The JavaScript API (`src/bindings.rs`) is small: `new Emulator(args,
files, now)`, its files a `Map` or an object of `Uint8Array`s by path;
`frame(wall)`, the page's clock in milliseconds, which gives whether the
run goes on and how far it is behind; `respond(method, target, body)`,
an answer's `status`, `kind`, `body` and `allow`; `eeprom()`, the
EEPROM's `cells` and whether it is `protected`; and
`onPanic(report)`.  Its Rust API (`src/emulator.rs`) is the one its
native tests call (`tests/boot.rs`, the 58503A booted from files in
memory to `*IDN?`, and a kept EEPROM starting the next run).  `make
test-emulator-web` builds the directory and runs it in Node
(`tests/node.test.mjs`, the boot and the kept EEPROM too, through
`pkg/node.mjs`, which instantiates the module a browser loads) and in
the browser tests' Chromium (`emulator-web.spec.mjs`, the panel booting
to `HP 58503A`, its console answering `*IDN?`, the EEPROM kept for
the next visit and forgotten, and the antenna moved to a location given
the page); `node --test`'s
output gives the board time the Node test ran and how long it took.
web.yml's `emulator-web` job runs it, and `make emulator-wasm` lints the
crate for wasm32.

## The controls

The simulation's controls do to the receiver what happens to it from
outside, not on its panel.  A script gives one with an `event` step
("Scripts"), the panel page's control strip with a POST to
`/event?name=NAME`, beside `/key`: a name that is no control is
refused 400, and a full queue or a run that has ended 503, as `/key`
refuses a key.  Either way it goes into a queue of 16 of its own, as the page's
keys do, and the run takes it at the end of a slice, after the
keys, in order; one clicked on the page is traced as a key is, and
replays as a key's record does ("Replaying a run"):

    smartclock-emu: event cycle=N at=S name=antenna-off

They are the mc683xx core's: on Unicorn an `event` step applies
nothing, and standard error says so.  A control nothing on the board
takes (the antenna's with `--gps`, which has no synthetic engine) is
not applied, and standard error says so; `/state`'s `controls` names
only those that apply, and the page shows only those.  Each of the
boards the emulator boots, the 58503A, the Z3805A, the Z3801A and the
58503B, takes all five without `--gps`; the Z3816A's is not checked
with them.

- `antenna-off`, `antenna-on`: the synthetic engine's antenna pulled
  and put back (`gps/`, `Synthetic::set_antenna`).  Disconnected, it
  tracks nothing from its next message on: `@@Ba`'s and `@@Ea`'s
  tracked count 0, every channel empty, no fix and the receiver status
  0, `@@Bn`'s and `@@En`'s channels empty and Time RAIM's solution
  unknown; reconnected, the fixed sky's tracked satellites come back.  The
  satellites in view, `@@Bb` and the visible count, are the almanac's
  and stay.  Its 1 PPS goes on, as the engine's own clock keeps it.  No
  antenna-sense bit changes: the VP's `@@Ba` has none (VPCommands.pdf,
  6 Channel Position/Status/Data Output Message, its receiver status
  byte); the UT+ reports an open antenna in `@@Ea` and `@@Fa`
  (`third_party/motorola/ut-engg-notes.pdf`, 12, "Antenna Sense
  Circuit Description") but the layout is in no source here, and no
  firmware reads one: the Z380x's and the 58503A's test only the
  receiver status's 3D-fix bit (`gps_oncore.c`, after `@@Ba` or
  `@@Ea`; `screen.c`).  The 58503A reads the antenna's current on the
  ADC's channel 7, `ADC_ANTENNA_CURRENT`
  (`firmware/58503a-3704.md`, "The enums"), but its check,
  `hmon_check_antenna_current` (`0x222f2`), has no caller
  (`scpi/undocumented.md`, `:DIAGnostic:GPSystem:ACURrent`), and the
  model wires channel 7 to nothing;
  the Z380x's "Antenna current" is the oscillator's channel 3 under
  another name (`rewritten/z3801a-3543/adc.c`, `read_adc_ant_curr`).
  So the firmware sees the loss as the engine's: the receiver loses GPS
  Lock and goes to holdover, and locks again after the antenna is back.
  How long each takes is the firmware's; a deterministic script shows
  it, the lamps' records giving the times:

      printf 'at 10\nevent skip-warm-up\nat 600\nevent antenna-off\nat 900\nevent antenna-on\n' \
          > antenna.script
      smartclock-emu third_party/firmware/images/58503a-3704.bin \
          --eeprom target/smartclock-emu/58503a.bin \
          --eeprom-dump third_party/firmware/images/eeprom/58503a-3710A01056-eeprom.bin \
          --deterministic --gps-start 2026-10-22T12:00:00Z \
          --script antenna.script --indicators --stop-after 1200 \
          2>&1 >/dev/null | grep 'bank=lamp'

- `skip-warm-up`: the board's warm-up curve jumped to its settled end
  (`board/analog.rs`, `Warmth::Settled`): the oven's monitor and the
  oscillator's current at their warm levels, the oscillator's warm-up
  offset (`--ocxo-warm-up`) at nothing, the temperature inside at
  its risen level, the same curve from power-up ("The oscillator"), and
  the Z3817A's and Z3815A's oscillator reports' ready bit set.
  The firmware still decides for itself: `oven_warmed_up` reads the
  oven channel past its -2.0 at its next readings, and the 58503A's
  `OCXO WARM UP` gives way as it would eight minutes in.  `warming` in
  `/state` is whether any of the curve's levels is still half an ADC
  count or more from its end, past which the ADC reads the end; the
  page offers the control while it is.  `--skip-warm-up` takes it at
  the first instant of every power-up, the run's start and each
  `power-cycle`'s, though not a `reset`'s, which does not power down:
  `warming` is false from the start, and the page shows the control
  disabled, "warmed up".  The firmware still decides for itself, as
  with the control.  A unit test in `mc683xx/runner.rs` finds a
  deterministic run with the option ending in the state of one whose
  script takes the control at 0.
- `power-cycle`: power off and on, a cold restart.  The board is built
  again from its file as the run read it at its start, every part at
  its power-up, the CPU at its power-on reset, as at the run's start:
  a record `reset cycle=0 cause=PowerOn`, the chip's clock, and the
  trace's `cycle`, from 0 again.  The board file and the images are
  not read from the disk again, so that one
  changed or removed since does not change the board; the EEPROM's part
  opens its working file again, and reads the dump a missing one starts
  from, as at the start, or in memory is given the cells it had ("The
  EEPROM").  What a power cycle does not take
  is carried (`mc683xx/carried.rs`):
  - the EEPROM, its working file, which the old board's part lets go of
    first, a write cycle in flight lost as a power cut loses it ("The
    EEPROM");
  - the antenna;
  - the warm-up, where the curve had come to: a power cycle cools the
    ovens only if it lasts, and this one takes no board time, so the
    ovens start as warm as they were.  Its place is carried on the
    board's own clock, as the oscillator and the temperature count it,
    not the chip's, which runs slower until the reset code sets SYNCR;
  - the oscillator's crystal (`ocxo.rs`, `Crystal`), whose aging and
    noise are its own, not the power's: the aging goes on from the
    seconds it had come to, and the noise's generator from the draws it
    had taken, with the random walk's sum, so that a seeded run's draws
    go on rather than repeat ("The oscillator").  So across a power
    cycle the frequency goes on but for the EFC, which the DAC holds at
    its middle until the firmware's first word, the warm-up's place, and
    the white term, drawn again at power-up as each second, the next
    draw in the seed's sequence, not the first again.  A unit test in
    `mc683xx/tests.rs` power-cycles a fast-aging oscillator 2 s in and
    finds its frequency at the new power-up the old one's just before.

  The synthetic engine starts again idle, its UTC at the
  whole seconds of board time the run has come to, not of the chip's
  clocks, which run slow from each power-up until the reset code sets
  SYNCR.  The run's clock goes on: a script's `at` steps and its
  `script` records' `cycle`, the `key` and `event` records' `at`,
  `--stop-after`, the pacing and `/state`'s `seconds` count from the
  run's start, not the power-up.  The host port's, the SCI's and
  the GPS port's lines, the script and the page are the run's and stay.
  With `--gps` the external engine's 1 PPS starts again, on the next
  whole second of its clock.
- `reset`: a warm restart, as the board's own watchdog gives one.  No
  receiver has a reset button, and RESET from outside, RSR EXT, is a
  cold start to the firmware (restart.md, "Restarting": the reset code
  restores the preserved region only when `RSR & 0xc0` is zero); a
  software watchdog's reset, RSR SW, is warm.  So the control resets
  the chip as its software watchdog's time-out does, through
  mc683xx-board's `Board::reset_for` with `ResetCause::Watchdog`: RSR
  SW, the modules a time-out resets reset, and RESET driven on the
  board's nets for 512 clocks.  It comes at once, not a time-out
  later, and at any point, the reset code's first cycles among them.
  mc683xx's SIM records the reset, `reset cycle=N cause=Watchdog`, with
  the models as a watchdog leaves them ("The LEDs", "Across a warm
  restart").

## The console

The panel page has a console on the host port under its control strip:
what the receiver sends on the port, and a line to type back.  One
person or thing types at the port at a time:

| Command line | Who has the port |
| ------------ | ---------------- |
| `--panel` | the page: standard input is not the port's and standard output carries none of its bytes, so the terminal shows the emulator's own lines on standard error alone |
| `--panel --host ADDR` | a TCP client while one is connected, else the page; the port served over TCP one connection at a time as without `--panel`, the console beside it, not in its place |
| `--panel --script FILE` | the script, its transcript on standard output as without `--panel` |
| no `--panel` | standard input and output, or `--host`'s client, or the script, as "Running it" has them |

With `--panel` alone and no page open, nothing types at the port, and
the run goes on without it.

- *Output.*  The run keeps the last 64 KiB the port sent
  (`panel/console.rs`, `KEPT`), counted by an offset from the run's
  start.  `GET /console?since=N&client=ID` answers a JSON object:
  `from`, the offset the text starts at, N or the oldest kept if N is
  older; `offset`, the offset after it, the next poll's N; `text`, the
  bytes, a byte not printable ASCII as `\u00HH`; `typist`, `writable`
  and `reason`, below; and `run`, the run's name, which no other run
  shares, so that a page reading by offset sees the emulator started
  again, its offsets from 0 again, and reads from the new run's start,
  marking the pane where it did.  The page polls it as it polls `/state` and
  shows the bytes with CR dropped, a backspace taking the character
  before, and any other byte not printable ASCII as a dimmed `\xHH`,
  keeping as many characters as the run keeps bytes, the newest: past
  that it drops the oldest, from the front of one long answer if need
  be.
- *Input, one typist at a time.*  While a TCP client is connected to
  `--host`, it types, and every console is read-only.  With none, or
  with no `--host` at all, one page may type: the first to poll takes
  the writer lease, its polls renew it, and it lapses 5 s after its last
  (`LEASE`); other pages read until it lapses.  A TCP client connecting
  takes the port over and ends the lease; a line typed before it
  connected that the run has not yet put on the port is dropped, with
  a note on standard error and no `console` record, rather than put
  into the client's session.  With `--script`, the script
  types, and the console is read-only.  `typist` names who types,
  `tcp`, `page`, `script` or `nobody`; `writable` says whether the page
  asking, by its `client`, may type; `reason` why not, `null` when it
  may.
- *A line.*  `POST /console?client=ID`, the body one line, a trailing
  LF or CR LF taken off, at most 256 bytes (`LINE_LIMIT`): queued whole,
  with the CR LF a script's `send` ends a line with, for the run, which
  puts it on the port at the end of a slice, where keys are pressed,
  and the next slice sends it to the board as a client's bytes.  A
  page that may not type is refused 409 with the reason; a body that is
  more than one line or too long 400; a full queue of 16 lines or a
  run that has ended 503.  The page shows a refusal where it says who
  has the port, for 4 s (`REFUSAL_MS` in `panel.js`) over the polls that
  would say so again at once, and keeps the line in the input.  It sets
  the input no length, which would cut a pasted line short and send
  the rest, and refuses a line past 256 bytes itself, as the run would.
  Each line put on the port is traced:

      smartclock-emu: console cycle=N at=S line=*IDN?

  `cycle` and `at` as a key's record has them ("The panel page");
  `line` as a script's `send` takes its TEXT, a space, a backslash and
  any byte not printable ASCII as `\x20`, `\\` and `\xHH`, so that
  `at S` and `send LINE` replay it at the same cycle, given the same
  run otherwise ("Replaying a run").
- *Quick lines.*  A row of buttons above the input sends a line each,
  posted as a typed line is, so it is refused as typing is and traced
  and replayed as a typed line: `*IDN?`, `:SYSTem:STATus?`,
  `:SYSTem:ERRor?`, pForth, `:SYSTem:LANGuage "PFORTH"`, into the
  pForth console as `crates/smartclock/src/console.rs` enters it for
  `smartclock-cli read-memory`, and halt, `halt`, the console's own word
  for leaving it.  They are disabled while the input is.  The run
  lists them in one constant, `QUICK_LINES` (`panel/console.rs`), and
  serves them to the page in `/constants.json`; none discards the
  receiver's learned state, strands its link or erases its flash
  (`:SYSTem:PRESet`, `:SYSTem:PON`, `:SYSTem:COMMunicate:*`,
  `:DIAGnostic:ERASe`, `:SYSTem:LANGuage "INSTALL"`), and a unit test
  in `panel/tests.rs` holds the list to that, knowing each of those in
  any case and in short form or long, and reads each quick line's
  record back as a script's `at` and `send` steps read theirs.

The page itself is tested in a browser, `make test-web` (after `make
web-deps` once; `.github/workflows/web.yml` in CI), by
`crates/smartclock-web/tests/browser/panel.spec.mjs`: against a run
faked per test, its keys and controls post what they name, the
console is writable or read-only as its typist says and its quick
lines post theirs, the status line shows the state, and a run that
stops answering puts the panel out; against a running 58503A, the
panel is drawn and goes on, a key reaches the run, and a typed
`*IDN?` is answered.

`tests/panel.rs` types `*IDN?` on a run's console and reads its answer
back in the output, with `--host` and with `--panel` alone, where it
also sends a quick line and finds its record; refuses a second page
the port while the first holds it; and makes the console read-only
while a TCP client is connected.  The unit tests in
`panel/console/tests.rs` hold the lease to one page and to its time.

## Tracing

The trace goes to standard error, a record a line: a prefix, the
record's kind, then `key=value` fields, ASCII, separated by single
spaces; addresses and registers are hexadecimal with `0x`.  Other lines
on standard error (the ports' addresses, the board file's, `--tap`,
warnings) are not of this form.  The records of the emulator itself,
`indicator`, `display`, `script`, `key`, `event`, `console` and `reset`, are `smartclock-emu: `
on either core, a reset's the same on both, its cause named as
mc683xx's are, the power-on reset's at cycle 0 among them:

    smartclock-emu: reset cycle=0 cause=PowerOn
    smartclock-emu: reset cycle=N cause=Watchdog

The CPU's records are each core's own.  On mc683xx, the default, they
are mc683xx-board's, `mc68331: ` and the kind, after a
`# mc683xx trace 4 chip=mc68331` header line (mc683xx's
[`docs/tracing.md`](https://github.com/charlieh0tel/mc683xx/blob/main/docs/tracing.md)),
and `cycle` is the chip's system clocks since power-on.  On Unicorn
they are smartclock-emu's own ("The Unicorn core").

`--trace` takes `position`, `devices` and `exceptions`, comma-separated
or given more than once (on mc683xx `qspi` and `pins` too):

- `position`: where the CPU is, ten times a second of the board's time.
- `devices`: every access to a device region, read or write; on
  mc683xx `bus-cycle` records, the cycles a board part answers, never
  the modules' registers.  `--watch ADDR` traces only the accesses that
  touch an address, keeping on mc683xx the `bus-cycle` records at its
  addresses, and may be given more than once.
- `exceptions`: every exception the core takes.

`--watch-code ADDR` traces each time the instruction at an address
runs, on mc683xx the board's code watch.  `--count-instructions`
counts the instructions on Unicorn ("Its tracing"); on mc683xx it is
refused, the board's position and `stopped` records counting them
always.  On mc683xx the run ends with a `stopped`
record.  Its cycles are the chip's system clocks since power-on, which
run at the reset's 8.4 MHz until SYNCR is written, and so are the
indicators'.  A script's records count the same clocks from the run's
start instead, across a power cycle, as the `key`, `event` and
`console` records' `at` does ("The controls").
The chip's resets and its DSACK faults (`Event::Dsack`, the board
file's `dsack` checked against PEPAR) are reported a line each.

## The synthetic GPS engine

The engine on the far end of channel A is `smartclock-gps`'s, the one
engine the project's tools share (`smartclock_gps::engine::Engine`;
firmware/gps.md, the engine and its messages): the emulator holds it
on the board's clock and tells it each second what the sky shows.  It
is the Oncore each model carries (firmware/gps.md, "The engines on the
bench"): on the Z3805A and the Z3801A the six-channel VP B1121P1114,
software 8.4 (`VP_SIX`); on the 58503A the eight-channel VP
B4121P1115, software 8.8 (`VP_EIGHT`); and on the 58503B the
eight-channel UT Plus R5122U1152, software 2.2, which a shipped 58503B
reports (`UT_PLUS_2_2`; firmware/gps.md, "The UT Plus"), by the board
file's `oncore` `engine` on mc683xx and by the model on Unicorn, which
a test holds to the same.  `--gps-engine` names another on mc683xx,
`b1121`, `b4121`, `b4121-no-time-raim` or `r5122`: `--gps-engine b4121`
puts the VP on the 58503B, as the first units had (Synergy's TN484-D
p. 6).  `b4121-no-time-raim` is the B4121 with options `B`, no Time
RAIM (`VP_EIGHT_WITHOUT_TIME_RAIM`), for the Z3817A's references, none
of which has been seen.  Each name is an identity of the shared
engine's (`gps::Engine`, `src/gps/mod.rs`): its model, six-channel VP,
eight-channel VP or UT Plus, which decides the commands it takes and
the channels it has, and its `@@Cj`, the preset's lines.

The engine can also be the other family the images speak to, a Furuno
GT-74 on NMEA and Furuno's `$PFEC` sentences, the shared engine's model
of one (`smartclock_gps::engine::furuno`): the Z3815A's and the Z3805A
1.01.03's.  A board file names it with a part of type `furuno-gt74`,
`protocol = "nmea"`, its `engine` one of `gt-74` (`GT_74`, Time RAIM,
its software made up, the default), `gt-74-without-time-raim` (4850113
version 2, which the images know to have none) or `gt-74-pulse-early`
(4850102 version 8, whose pulse the images take to come 400 ns early
and correct), the names smartclock-gps-feed's `--engine` gives them;
an `oncore` part takes the Oncores, `protocol = "motorola-binary"`.
Both are the one part (`board/participants/gps.rs`), on channel A at
the VP's and the images' 9600 baud 8N1 (`duart_init`;
firmware/z3805a-1.01.03.md, "The GPS engine"), and either takes any
engine's name, a protocol not its engine's refused: `--gps-engine`
names an engine of either family on any board, the run giving the
part its protocol with it, so `--gps-engine gt-74` puts a GT-74 on the
Z3805A's board, whose 3543B then hears nothing it decodes.  A GT-74
has no options, neither `answers = false` nor `saved`.  It is told the
same fake receiver's sky as an Oncore.  It holds nothing back, so has no
deadline, and counts seconds, not the board's milliseconds.  Its
sentences for a second go out at the second's start, as an Oncore's
do, where a GT-8031 sends them about 500 ms after its pulse
(`third_party/furuno/Furuno_GT8031_Hardware_Specification_2005.04.28.pdf`,
Fig. 2.2.2.4), which the two images' boots do not show they mind.  Its
1 PPS goes out as its own `tps` reports it available
(`furuno::Engine::pulses`): never in `rrs`'s control mode 0, always in
mode 1, and in modes 2, the default, and 3 once the record holds the
almanac and the offset to UTC, with a satellite used and, in estimated
observation point mode, a first fix of four satellites under a PDOP of
6 made (GT p. 47), mode 3 dropping it at a Time RAIM alarm; `set,t`'s
correction moves it, a negative one earlier as a cable delay does,
less the 400 ns `gt-74-pulse-early`'s software is taken to put it
early.  A pulse that falls before its second's start, an early one,
is decided on the second before's record, so the first such pulse
after power-up is second 2's.

On mc683xx a board file can also make it deaf (`answers = false`: it
hears the firmware, for `expect-gps`, and answers nothing,
`Options::deaf`) and have it power up configured, as an engine a
receiver configured before it was last turned off, fixing and sending
unasked (`saved`, `Options::saved`): `saved = "58503a"` is the shared
engine's `CONFIGURATION_58503A`, how a 58503A leaves its engine,
`@@Ea` and `@@En` every second, `@@Bb` at each change, Time RAIM on,
which needs an eight-channel engine.  The Z3817A's board has both, as
its image cannot command its engine (firmware/gps.md, "Which engines
each image accepts"); its tests of the bench's feed of 2026-11-09,
nothing on RXD, take `saved` away, which leaves the engine idle and
silent.

What it answers, and how, is the shared engine's and its own
documentation's (`crates/smartclock-gps/src/engine.rs`; firmware/gps.md,
the messages' table): every message the images send, implemented
whole, field by field, from VPCommands.pdf and TRM0003, with
`smartclock-gps`'s tests and recorded vectors holding it.  Among what
the boot turns on: `@@Ao` answered with `@@Ap`, as the firmware's table
matches it; `@@Ta`, the 58503B's and the Z3816A's DCXO query, which no
manual defines, kept and answered with what it was given (class gap,
`stored`), so their init scripts go on; a self-test, `@@Ca` or `@@Fa`,
answered six seconds after its command, within the VP's "up to ten
seconds" and between a real VP's 5.2 and 7.1 s, which the emulator
holds back and sends when due (`Engine::deadline`, `Engine::poll`); a
six-channel engine leaving the eight-channel messages unanswered, so
the 58503A on the B1121 waits out three `@@Ea`; the UT Plus taking only
the commands it lists, with no idle mode.

`--gps-latitude`,
`--gps-longitude` (degrees, north and east positive) and `--gps-height`
(meters above mean sea level) place the synthetic engine's antenna, by
default at the position the Z380x's `master_reset` script, not its
power-up one, sends (37.3255833° N, 121.9978333° W, 55 m MSL;
firmware/gps.md, "Requests"), HP's Santa Clara Division on Stevens
Creek Boulevard, where these receivers were made, which is the shared
fake receiver's place (`fake_receiver::PLACE`);
`--gps-start` gives UTC at power-up, RFC 3339, else the host's clock,
to the second; `--gps-leap-seconds N` gives GPS time less UTC, 0 to
127, 18 by default, as since 2017.  Each must be one the Oncore's
messages can carry, or the command line is refused, naming the option
and the range (`gps/range.rs`): a latitude of -90° to 90° (`@@Ad`), a
longitude of -180° to 180° (`@@Ae`), an MSL height whose ellipsoid
height, 28.41 m lower, is also within -1000 m to 18000 m (`@@Af`), and
a start from the GPS epoch, 1980-01-06, to the end of 2077, the last
year of `@@Ac`'s date (VPCommands.pdf).  A board file's `oncore`
parameters are held to the same.

What it is told each second is the emulator's provider of records
(`gps/sky.rs`, `Sky`): `smartclock-gps`'s made-up sky (`source::fake_receiver`)
at the antenna's place, at the power-up time plus the seconds since
(`scenario::Fixed`), GPS time ahead of UTC by the leap seconds given.
Eight satellites at fixed elevations, azimuths and signals, a 3D fix
at the place, its DOPs, an almanac of the eight, a self-test that
passes and Time RAIM's result for such a sky; the ellipsoid height is
the MSL height less 28.41 m everywhere, a bench Z3801A's engine's
separation (`fake_receiver::SEPARATION`).  The engine tracks those at or above
its mask angle and not on its ignore list, as many as it has
channels, so under the default 10° mask the two lowest are neither
visible nor tracked.  The provider acts on what the fake receiver acts on,
position hold (`fake_receiver::hold`), and works out the position in
Earth-centered coordinates, which the fake receiver leaves to its holder, as
it leaves where in its second the epoch lies, which the emulator puts on the
second; the
`antenna-off` control unplugs the antenna (`fake_receiver::unplug`), and the
engine hears nothing until `antenna-on` ("The controls").  It fills
what the fake receiver's `Declaration` declares, and a test holds it to that,
to the fields the images use and to the settings they set, the
exceptions tables' `fake receiver` rows being its own
(firmware/gps-field-exceptions.tsv, firmware/gps-setting-exceptions.tsv).
Nothing models acquisition or moving satellites.

The machine advances the engine to the board's time after each slice
(`Machine::run_alongside`), so it keeps to `--deterministic`,
`--stop-after` and the trace's stop record as the board does, and a
deterministic run with it is repeatable.  Second *n* of the engine's
time scale starts *n* seconds of board time after power-up: its record
is given to the engine then (`Engine::second`), and the messages it
sends for that second go out then; what the firmware sends is given to
it as it is read (`Engine::receive`), with that second's record, and
its answers go out at once.  The engine's own clock, which only its
held-back answers are timed by, is the board's in milliseconds.
The 1 PPS is the emulator's, not the shared engine's, and whoever
makes the pulse acts on the engine's pulse settings
(`Engine::settings`): second *n*'s pulse is at its start less the
cable delay (`@@Az`, VP p. 68) plus the pulse offset (`@@Ay`, VP
p. 66), to the picosecond on mc683xx's board time and to the cycle
on Unicorn's, from the next pulse after a change, none put out
before power-up; and the pulse mode (`@@Bn`, `@@En`, VP p. 104) says
whether it goes out at all: never, always (the default), while a
satellite is tracked (the engine fixing, its antenna heard, one at or
above the mask and not ignored), or while Time RAIM also finds the
solution within its limit; a GT-74's as above.  `Synthetic::on_pulse` gives each pulse put
out to a hook: on mc683xx the engine's part drives its `pps` port
high there for 200 ms, the VP's width, a GT-74's too, for which the
GT-8031's hardware specification gives none (*inferred*), and
`unicorn.rs` gives it to the board as the
engine's 1 PPS (`Board::gps_pps`), which the GPT's pulse accumulator
counts and which starts the FPGA's interval count ("The interval
counter").  Each pulse ends a slice, so it reaches the board at its
own time.

## The interval counter

The FPGA's design is the Z3805A bitstream's, as
`docs/firmware/counter.md`, "What the netlist shows", reads the netlist
decoded from it, simulated in Icarus Verilog: not checked on a board.
`board/fpga/z3805a.rs` models it, behind the XC3042's own
model in `board/fpga.rs`, the configuration and the readback, and the
one interface every model of a design gives it, `board/fpga/logic.rs`'s
`Logic`, at the
level of events, not clock edges: each
pulse, tap, start and stop is a value of the oscillator's phase, whole
10 MHz periods where the netlist acts on a clock edge:

- The port: registers 0, 1 and 3, and only the bits the netlist keeps;
  register 0's mode as whether it is 3, so modes 1 and 2 are mode 0.  A
  read drives the seconds in bits 4 to 7 at every address, the status's
  start and stop bits in bits 0 and 1 below `0x10`, and otherwise the
  nibble the address's low four bits pick among `0x10` to `0x17`, the
  interpolator's byte complemented and the count's six nibbles, `0x18`
  up reading 0; so the status's bits 2 and 3 are the interpolator's P5
  and P6, complemented.  The port answers anywhere in its 8 KiB, only
  ADDR0 to ADDR2 reaching the FPGA.
- The count: the preset (register 0's `0x40`) clears it and holds
  `0x555555`.  Without it, outside mode 3, the GPS 1 PPS's rising edge
  starts the count, setting status bit 1; the gate opens on the 10 MHz
  clock's first edge after it with register 1's reference bit Low, its
  second with the bit High, and the count runs to the fourth edge after
  the divider's tap, 4,000,000 edges after the edge the internal 1 PPS
  rises on, the first tap after the start, which sets status bit 0.  A
  GPS 1 PPS after the start does nothing until the next preset.  So
  for four ticks after the stop bit the count still runs, and the
  port takes each access at its own time: at the slice's start, a
  slice beginning within those four ticks would read the status stopped
  and every nibble after it short, `counter_read` 400 ns low.
- Mode 3: the start bit's fall with the preset off starts the
  references' measurement on the write's strobe, a clock edge, setting
  both status bits at once and counting six edges; AH.Y lasts one clock
  with the reference bit Low, two with it High.  The design's reset
  leaves it in mode 3 and makes one such start.
- The sync field: its change from 0 to a kind marks a sync pending,
  which the next internal 1 PPS clears, making that pulse an edge early
  for kind 1 and an edge late for kind 2.  With kind 3 a GPS 1 PPS
  restarts the divider on its second clock edge, the next internal
  1 PPS 10,000,000 edges on; if the internal 1 PPS comes first, the
  divider holds there, the sync still pending, until the GPS 1 PPS
  restarts it.  A field left at its kind does nothing more.
- The divider: the internal 1 PPS every 10,000,000 edges, the first
  10,000,000 after the design's reset, which `fpga_program`'s PROGRAM
  pulse gives, and the tap of the second before it; the seconds count
  it, from that reset.
- The interpolator, outside the FPGA: it measures the start's interval,
  from the GPS 1 PPS, or mode 3's strobe, to the edge the gate opens on
  (AH.Y, out on P81), reading its low reference, one 10 MHz period, and
  its high, two, under the hex DAC's trims, an interval between in
  proportion, clamped to its byte.

The design's clock is the signal on its P7, the OCXO's ("The
oscillator"), which the board gives it as a phase (`board/clock.rs`'s
`Clock`: the oscillator's phase at a board time, its last rising edge,
a whole tick, and the part of a period since, and the first board cycle
at which it reaches a rising edge): it knows no period of its own, so
the internal 1 PPS and the count follow the EFC.  Every pulse, tap and
edge is a whole tick, an integer, and the cycle the clock names for an
edge is exact, on the clock net's own integer phase: the phase there has
reached the edge and the cycle before has not, so the design's event
comes due on the cycle it names.  When the phase was seconds in an
`f64`, a round trip through it could name the cycle before a pulse
with a steered oscillator, hours to days in; the event never came due,
and mc683xx-board's run stopped as stalled.  `fpga_part.rs`'s tests
check that case and edges over a month at frequencies across 1e−7 of
10 MHz.  Only the part of a period, the GPS 1 PPS's place between two
10 MHz edges, the interpolator's fraction, is an `f64`, apart from the
whole ticks, so it keeps its precision however long the board runs;
the GPS 1 PPS itself comes on the board's 59.6 ns cycle.

The model is checked against the netlist itself: `tools/fpga/differential.py`
drives the netlist (`tools/fpga/z3805a_stimulus.v`) with cases of
register writes and reads, GPS 1 PPS edges and interpolator bytes,
and records what it reads, its internal 1 PPS's edges and AH.Y's
widths in `crates/smartclock-emu/src/board/fpga/z3805a.fixture`, which
`make -f tools/fpga/Makefile check-differential-z3805a` regenerates and diffs;
a unit
test drives `z3805a.rs` with the same stimulus and wants the same reads,
a count within a tick or, still running, as it stood somewhere in the
read's bus cycle, the same pulses to the tick and AH.Y within 3 ns.
Those tolerances (17 tick positions, a count ±1, 3 ns, the preset's
pulses of about 100 ns left unmatched), each read, a nibble, matched
alone, mean a pass cannot exclude a systematic one-count error or fix
the pipeline's constant to the tick.  A
simulated second takes Icarus minutes, so the fixture is what `cargo
test` reads, and an ignored test runs the netlist afresh where Icarus
and the generated netlist are.  The fixture was first made from a
netlist with one input repaired by hand (`netlist.py --reconnect
EF.D=DB.X`); since the decoder was checked against XACT, the netlist
reads that from the bitstream alone, Yosys proved the two the same (a
one-off proof: the option is gone, so it cannot be rerun), and
the fixture made again from it is the same byte for byte
(`docs/firmware/outputs.md`, "Netlists").

The Z3817A's design (`design = "z3817a"`) is a second model at the
same level, `board/fpga/z3817a.rs`, as `docs/firmware/z3817a-trace.md`
reads its netlist: the divider, its tap, the seconds, the sync field
and the stop are the Z3805A's to the edge, but for a full sync's
restart, and the two models share them (`board/fpga/timebase.rs`).  What differs:

- No interpolator and no mode: register 0 keeps the preset alone,
  register 1 the read address alone, and nothing like mode 3 starts a
  count, at the design's reset or after.
- The start: register 4's bit 0 (ADDR2 High) selects P82, clear, or
  P84, set, whose 1 PPS starts the count; the gate opens on the first
  edge after it, as the Z3805A's with the reference bit Low, so the
  count less its preset is `4,000,004 + (t_internal - t_1PPS) / 100 ns`
  less the start's fraction of a tick.  The firmware writes the bit 0.
- The count: 32 bits, preset `0x55555555`, its eight nibbles at `0x12`
  to `0x19`; `0x10` and `0x11` read `0xf`.
- The P84 counter: four bits of P84's rises, in a read's high nibble at
  odd addresses, the seconds at even ones.  The board file drives no
  P84.
- The status's bits 2 and 3, flags of the 1 PPS chain the firmware
  never tests: bit 2, FI.X, High the millisecond after each internal
  1 PPS; bit 3, GJ.X, the first millisecond of every ten, counted from
  the edge the divider started on; neither on the reset's own edge.
- The outputs, which nothing on the board model takes: P72 High four
  clocks each millisecond, P19 the clock divided by 65, High 32 clocks
  of each 65 from 32 after the reset, P16 P82 passed through.  The
  model gives P72's and P19's levels to its tests alone.
- The sync field, as the netlist's runs give it
  (`check-readings-z3817a.sync1` to `.sync3`): kinds 1 and 2 step the
  internal 1 PPS a clock, as the Z3805A's; kind 3 restarts the divider
  on the 1 PPS and the restart is itself the internal 1 PPS, counted
  in the seconds, the count after it 4,000,005, where the Z3805A's
  gives none until a second on.  A kind 3 whose 1 PPS comes after an
  internal 1 PPS, which no run makes, holds the divider as the
  Z3805A's does, and the flags and P72 count from the divider's new
  start: both inferred.  A unit test replays the three runs.

It is checked against its netlist as the Z3805A's is:
`differential.py --image z3817a` drives the netlist through
`tools/fpga/z3817a_stimulus.v` and records its reads, its internal
1 PPS's edges and the clock edges after which the status's bits 2 and
3 and the outputs change, with each P84 pulse's width and the
harness options a case ran with (`+edges` and `+p19` print the
outputs), in
`crates/smartclock-emu/src/board/fpga/z3817a.fixture`, which
`make -f tools/fpga/Makefile check-differential-z3817a` regenerates
and diffs, and a unit test replays it, as the LCA model's tests replay
it on the Z3817A's netlist.  Its cases: the read map at
either select, the P84 counter through its wrap, counts P82 starts
before and after the internal 1 PPS, in the first second and a few
clocks before a tap, a count P84 starts with P82 ignored, and P16's,
P19's and P72's edges.  Each read must be the netlist's somewhere in
its bus cycle, and each pulse on the netlist's tick.  No case writes
the sync field.

A pulse of the internal 1 PPS that a GPS 1 PPS, or a read or write of
the port, runs the design past before the pulse's own event is kept
and driven then (`Fpga`'s `unreported`), so that with the two 1 PPS a
fraction of a period apart, as once locked, no internal 1 PPS is lost:
the firmware would call the FPGA's 1 PPS dead and go to holdover, "HW
error".

### Across a warm restart

The firmware takes the FPGA as configured and its 1 PPS as in phase
across a warm restart: `system_init` pulses PROGRAM only on a cold
start (`0x12cfa`), `pllp_task` reloads the FPGA (`counter_reset`) only
from power-up, and power-up recovery, `pll_reference_check`
(`0x46388`), sets the clock from the FPGA's count of seconds and
reloads the FPGA only when its first reading is invalid
(firmware/counter.md, "The ready bits and the wait").  The reset code
drives PROGRAM high before it makes PE6 an output (PORTE0 `0x40`, then
DDRE `0x40`, in the boot code's `0x550` and the primary's `0x14534` in
3704), and while the reset leaves PE6 an input the line reads high, as
an undriven pin does on mc683xx and port F's do on Unicorn; nothing
else of the CPU's reset reaches the part, whose RESET is PROGRAM, so
the emulator keeps it through a watchdog reset on both cores.
*Inferred*: what holds PE6 high on the board is not traced.

Were the reset to clear the FPGA, as at power-on, power-up
recovery's first reading would be invalid, `counter_reset` would load
it again, and its divider would start a fraction of a second off GPS's.
Recovery would then pass to holdover on the TI limit, and holdover
waits for the TI within the holdover limit, its threshold and
tolerance, before recovery (`holdover_within_limit`, `0x43912`): it
would never come, and `:SYNChronization:HOLDover:WAITing?` would say
`LIM` for good.  A restart while the loop is still in power-up reloads
the FPGA anyway (`pllp_task`), so a restart there locks either way;
one from lock locks again through holdover recovery ("Re-boot", then
"GPS lock started" in the event log), as the GPS engine counts the
board's own clock ("The mc683xx backend").

## The oscillator

`board/ocxo.rs` is the OCXO, which knows nothing of what it clocks: its
input is the EFC's DAC word, its output a clock signal, its 10 MHz
phase on the board's clock (`board/clock.rs`'s `Clock`: the phase at a
board time, and the cycle at which it reaches a rising edge).  The board alone
joins the two, as a trace on the board does: the OCXO's output to the
FPGA's clock input, whose ÷10,000,000 divider makes the internal 1 PPS
and whose ticks the interval count counts (`firmware/counter.md`,
`firmware/interval.md`).  The CPU's clock is not the OCXO's: it is the
MC68331's synthesizer from the 32.768 kHz reference (`board/time.rs`),
so the OCXO's error shows as the internal 1 PPS drifting against the
GPS engine's, which is true time, and the firmware reads it as TI.

Its fractional frequency offset is

    y = offset + gain × 16 × (word − 0x8000)
          + aging × days + warm-up × heating + noise

- *The EFC.*  `efc_write_dithered` (`dac.c`) sends the loop's 20-bit
  EFC, dithered, as its top 16 bits to the AD569 on QSPI device 7, and
  `efc_write` then writes 0 at `0x306000` (restart.md, "The chip selects").
  *Inferred:* that write loads the word into the DAC's output, since
  `qspi_efc_test` writes its patterns without it, so a word moves the
  oscillator only with its strobe.  The gain is the firmware's G,
  6.25e−13 an EFC count (structures.md, "The EFC's gain"), 1e−11 a DAC
  step, the frequency rising with the count (counter.md, "The sign, and
  the loop", inferred there; efc.md has it rising on a 58503A).  Real
  units differ (efc.md: 3.94e−13 on a 58503A; ocxo.md: 6.0e−13 on the
  bench Z3801A), so `--ocxo-efc-gain` sets it.  Without it the gain is
  the model's firmware's G (`Profile`, `efc_gain`).  The EFC adds nothing at
  its middle, 524288 counts (structures.md, "The lifetime record"), the
  DAC's `0x8000`, which the DAC holds before its first strobe, a pick.
  On `--core mc683xx` the DAC is mc683xx-peripherals' AD569 wired as
  the board files infer ("The mc683xx backend"): a word's high
  byte reaches the oscillator as it is shifted in, the self-test's
  patterns among them, the strobe its low byte, and its output starts
  at mid-scale, the board files' `initial` `0x8000`, until the
  firmware's first word.
- *Offset,* `--ocxo-offset Y`: the frequency at the EFC's middle; by
  default, on mc683xx, the board file's if it gives one, as
  `z3817a.toml` alone does, else the one that puts the oscillator on
  frequency at the EFC its EEPROM's lifetime record holds ("The
  oscillator the EEPROM describes"); 0 on Unicorn.
- *Aging,* `--ocxo-aging Y`, a day, linear from the run's first
  power-up, 0 by default; the 10811-60159's is under 2.5e−10 a day
  (ocxo.md).
- *Warm-up,* `--ocxo-warm-up Y`: the offset at power-up, falling as the
  ADC's oven model settles (full power four minutes, then a time
  constant of 150 s), 0 by default.  The 10811 is within 5e−9 of its
  final frequency ten minutes after turn-on (ocxo.md, "Power"), which a
  warm-up of 5.5e−8 or less meets.
- *Noise,* drawn each second of board time from `--seed`:
  `--ocxo-white-fm`, the spread of each second's frequency (white
  frequency noise, a random walk of phase), and
  `--ocxo-random-walk-fm`, the spread of each second's step of a
  random walk of frequency; both 0, off, by default.

On `--core mc683xx` each term is also the board file's `ocxo` part's
parameter of the same name (`offset`, `aging`, `warm_up`, `efc_gain`,
`white_fm`, `random_walk_fm`), and an `--ocxo-*` option takes its place
only when given: a board file can carry its oscillator, the command
line still moving it.  Where the file gives no `efc_gain` the model's
is its, and where it gives no `offset` the EEPROM's
(`mc683xx/overrides.rs`).  On Unicorn, with no board file, the options
over the model's set it (`Model::ocxo`), its offset 0.

A term that is not a finite number, NaN or an infinity, is refused
(`ocxo.rs`, `OcxoError`), the board file's part or the Unicorn board
not built, rather than run at the rate its cast gives.

The phase is integrated exactly: the frequency is held between changes,
each strobe and each slice of board time when aging, warm-up or noise
move it, quantized to 1e−18, and the phase kept as whole parts and a
remainder, so that no rounding accumulates and the edges over any span
are the integral's, though the board's cycle, 59.6 ns, is coarser than
the oscillator's 100 ns.  `ocxo.rs`'s tests count 1e10 + 100 edges in
1000 s at 1e−8, however finely the history is cut.  A time past the
last change is projected at the present frequency; the history is kept
for 64 s of board time, and a time older than that is extrapolated,
not exact.  A reset of the CPU does not touch the oscillator.  The
`skip-warm-up` control puts its warm-up at its end, with the oven's,
`--skip-warm-up` starts each power-up there, and a power cycle carries the warm-up's place and the crystal to the
board it builds ("The controls").

`tests/ocxo.rs` boots the Z3805A 1e−8 fast with the synthetic engine
and checks at 1500 s of board time that the loop is `LOCK` and its EFC
(`:DIAGnostic:ROSCillator:EFControl:ABSolute?`) leaves under 1e−10 of
the offset, 1e−8 + 6.25e−13 × (EFC − 524288), the FPGA clocked by the
OCXO.  It is slow, so it is ignored unless asked for:

    cargo test --profile checked -p smartclock-emu --test ocxo -- --ignored

### The oscillator the EEPROM describes

An EEPROM is one unit's, and holds where that unit's oscillator was on
frequency: the lifetime record's EFC, the loop's EFC as it was last
saved in lock (structures.md, "The lifetime record").  Every image the
emulator runs reads the record with the same `lifetime_record_read`
(`rewritten/common/records.c`), at `0x400060`, its EFC the float at
`+0xa` (`common/types/identification_record.h`), taking the EFC's
middle, 524288 counts, where the record's checksum fails or its EFC
has the bits `0xe8000000` all set, as an erased record has; and every
image's `pll_init` starts its loop from that EFC, clamped (`pllp.c`,
`efc_clamp(lifetime.efc)`).  The Z3801A 3543 and the Z3805A 3543B use
it once more: they take it as the aging fit's `a` at the first history
update (`pll_aging.c`, `PLL_AGING_START`), where it stays until an
entry of 2700 samples has been fitted, and in holdover set the EFC
that fit predicts (`pll_stages.c`, `pll_stages_predicted_efc`): until
then, a holdover puts the EFC back at the EEPROM's.

So on those two an oscillator on frequency anywhere else leaves a
holdover that never ends.  The loop locks, having pulled the EFC to
where this oscillator is on frequency; any holdover, the temporary one
an antenna delay set starts among them (firmware/gps.md, "The cable
delay"), then moves the EFC to the EEPROM's, the oscillator runs off
at G times the difference, the TI soon passes the hold threshold, and
`holdover_within_limit` (`pll_stages.c`) keeps the loop in holdover
from then on: the status screen's "1PPS TI exceeds hold threshold".
That is the firmware's doing, as on a unit whose oscillator had been
changed for another without the EEPROM's knowing.  The other images
take the EFC they have as `a` when it is still zero, at the holdover's
start (`pll_stages.c`, `pll_is_zero`): the 58503A 3704, the Z3815A
4010 and the Z3816A 4001 the EFC less its temperature correction, the
58503B 1.01.04, the Z3805A 1.01.03 and the Z3817A 3752 the mean of the
history's entry being summed, or the EFC.  For them the EEPROM's EFC
is where the loop starts.

On mc683xx the board's oscillator is therefore the one the EEPROM
describes, unless told otherwise.  At the run's start the emulator
reads the lifetime record as the firmware does (`board/lifetime.rs`)
from the EEPROM the run starts with, whatever gives it: the working file a run before
left, else `--eeprom-dump`, else the board file's image, a unit's dump
or a synthetic image, or in memory the image the host hands over
(`mc683xx/eeprom.rs`, `starting_cells`).  The oscillator's offset is
the one that puts it on frequency at that EFC with the gain it runs
at, `--ocxo-efc-gain` or the board file's or the model's G
(`mc683xx/overrides.rs`): 0 for an erased part or a synthetic image,
whose record fails the checksum.  An `offset` in the board file or
`--ocxo-offset` takes its place; no board file gives one.  The oscillator is the
same through a power cycle, so its offset is worked out once, and a
lifetime record the firmware writes later does not move it.  Unicorn
reads no board file and keeps offset 0, so its Z3801A and Z3805A on a
dump keep the mismatch.

`mc683xx/tests.rs` holds every board file's oscillator to its image's
record, and the offset to the EEPROM a run starts with and to a power
cycle; `tests/temporary_holdover.rs` sets a 500 ns antenna delay on
the Z3801A, the Z3805A and the 58503A, locked, and has the holdover
end and the TI pulled in.  Those are 520 s of board time each, ignored
but in `make emulator-slow`:

    make emulator-slow EMULATOR_PROFILE=checked

## What is modeled

Each model's evidence is the file it cites: the MC68331 User's Manual
(`third_party/motorola/MC68331UM.pdf`), the MC68681 data sheet, the
register headers in `third_party/firmware/include/`, and the
hand-written C in `third_party/firmware/rewritten/z3805a-3543b/`.

The table's "Modeled" and "Not modeled" columns are smartclock's own
board, the one Unicorn runs.  Its last column says what mc683xx runs in
each one's place: the chip's own modules and mc683xx-peripherals'
stock chips ("The mc683xx backend"; mc683xx's `docs/peripherals.md`),
or, where it says the same, smartclock's model itself.

| Device | Where | Modeled | Not modeled | On mc683xx |
| ------ | ----- | ------- | ----------- | ---------- |
| CPU32 | | Unicorn's 68020 runs the instructions; the core takes every exception itself, as the CPU32 does: `trap #n`, illegal instruction, privilege violation, line A and F through format-0 frames, CHK and zero divide through format 2, their return address the faulting instruction's own, not the next one's as the CPU32 stacks it, `rte` popping either (`TRAPcc` never reaches the core: Unicorn's QEMU, 5.0, decodes no m68k `TRAPcc`, which takes the illegal-instruction exception); interrupts at their level and vector when the mask admits them; the vector base followed from each `movec` to VBR; `rtr`, `move (sp)+,sr` and `stop`, which QEMU under Unicorn gets wrong, done in its place, as the harness does; the status register read without spoiling its condition codes ("The condition codes") | bus and address errors, which halt the emulator; TBL and LPSTOP (the image has no LPSTOP; its two TBL words were not seen to run); code in RAM gets none of the corrections | mc683xx's CPU32 |
| SIM | `0xfffa00` | SYNCR always locked; RSR from the reset (power-up at start, the watchdog's after a time-out); ports E and F, DONE on port F; the software watchdog, its time-out from SYPCR and its 0x55/0xaa service, resetting the CPU and the SIM, GPT and QSM with it, the QSPI RAM kept (MC68331UM 4.6.2, 6.3.2); the periodic timer, from PICR and PITR, 1024 ticks a second, its request cleared by its acknowledge | the chip selects, kept as written and not enforced; privilege: the supervisor-only registers read and write from user mode as from supervisor, which on mc683xx are a bus error (`hardware-investigations.md`, "Register reads through the console"); PORTC's reset value, `0x7f`, which reads zero here; port E's input pins, which read their latch; the clock's frequency, fixed at 16.777216 MHz | the MC68331's SIM |
| GPT | `0xfff900` | the free-running counter through the prescaler; the four output compares, their flags and interrupts through ICR's level and vectors (the time code's and the ES message's, vectors 86 and 87); PACTL's PCLKS showing the FPGA's INIT; input capture 1, TIC1 and IC1F, from the FPGA's internal 1 PPS when TCTL2's EDGE1 takes a rising edge, its interrupt through vector 81 (`pps.c`); the pulse accumulator, PACNT counting the engine's 1 PPS on PAI's rising edges while PACTL has PAEN and PEDGE, as `gps_pps_check` sets it (`monitors.c`) | the accumulator's gated mode and overflow; the other captures, which see nothing; PWM | the MC68331's GPT |
| SCI | `0xfffc08` | the QSM's registers from their reset values (MC68331UM D.4: QIVR `$0F`, SCCR0 `$0004`, SPCR0 `$0104`, SPCR1 `$0404`); the baud rate from SCCR0, a character a character time, TDRE, TC, RDRF and OR, the interrupt from SCCR1's enables at QILR's level and QIVR's vector | parity, framing and noise errors, idle, wake-up | the MC68331's QSM |
| QSPI | `0xfffc18` | the queue from NEWQP to ENDQP when SPE is set, each entry to the device its chip selects name, SPE cleared and SPIF set a fixed span later, the end-of-queue interrupt | wraparound, halting, mode faults; transfer time is fixed, not from the dividers | the MC68331's QSM |
| ADC | QSPI select 1 | an ADC0838 answering each channel with a level chosen so the firmware's scalings (`adc.c`) give 5.01 V, +15.03 V and -14.98 V, inside the health monitor's bounds (`monitors.c`), with noise of about a third of a count, never more than one, from `--seed`; the board warming from power-up: the oven at full power for four minutes, then settling with a time constant of 150 s, the shape from `ocxo.md`'s "Power", so the oscillator current falls from 230 to 100 and the oven channel from 5.0 to -3.8, past `oven_warmed_up`'s -2.0, when `powerup` turns the outer oven on (`pll_stages.c`); the temperature rising from 25 to about 35 degrees over half an hour; the trailing bits `qspi_adc_test` checks.  Every level, time and spread is a pick, not a measurement | the double oven's reading after the outer oven is on, steady at 5.0; any tie between the readings and the EFC or the loop | `adc0838`, converting the same levels (`analog.rs`) |
| DACs | QSPI selects 2 and 7 | the hex DAC's 36 bits shifted back a bit early and the EFC's AD569 its word as it was, as the self-test reads them back (`selftest.c`); the hex DAC's channels 2 and 3 trimming the interpolator's references (`ti.c`); the EFC's word loaded into the oscillator by the strobe at `0x306000` (*inferred*, "The oscillator") | the DAC's settling and output filter | `mc144110`, and `ad569` behind a `74x164` chain; the strobe and the interpolator the same |
| OCXO | | 10 MHz from the EFC, the offset, aging, warm-up and noise, its phase integrated exactly ("The oscillator") | temperature beyond the warm-up; phase noise faster than a second | the same |
| FPGA | QSPI select 6, `0x304000` | the XC3042 (U32) as the XC3000 data sheet gives it (`third_party/xilinx/xc3000.pdf`), driven as `fpga_program` drives it (`fpga.c`), port E's PROGRAM its RESET and port F's DONE its DONE/PROG: a Low on both clears the configuration (Figure 20), as does DONE/PROG falling on the configured part and held Low alone more than 6 us (7-20, 7-23; TPGW, 7-34); INIT follows RESET before configuration; DONE rises when the start-up's clocks after the header's length count end on a byte (Figure 21): `0x786c`, 30828, for the Z3805A's bitstream, DONE at 30832, the XC3042's 285 frames of 108 bits, its 40-bit header and postamble and a bit for the one device; nothing without the preamble; RESET after configuration holds the design reset while Low (7-8, 7-9), no 1 PPS, write or GPS start, and keeps the configuration, the divider counting from its release; the readback `fpga_verify_chip` compares, started by M0/RTRIG, port F's bit 6, rising and shifted out inverted, a transfer's bits at a time: three dummy bits, the third the first frame's start bit, then each frame with one start and one stop bit, 106 of its 108 (7-23), so that the compare passes under `fpga_readback_mask`; the Z3805A's design as its decoded netlist has it ("The interval counter"), for the Z3805A's design and the 58503A's and Z3801A's, which the board file's `design` names ("The boards"), no bitstream read, the bits the firmware sends setting only the length count and the readback, so a boot shows the firmware works with this model, not that a bitstream implements it: the register port, registers 0, 1 and 3 and the bits each keeps, the read multiplexer; the counter presetting to `0x555555`, which `counter_selftest` reads back, started by the GPS 1 PPS and stopped by the divider's tap, its count readable while it runs; mode 3's start; the sync field's steps and restart; the internal 1 PPS from its 10 MHz clock, every 10,000,000 periods, into the GPT's input capture 1, its pin driven Low at DONE, where the part's pull-up held it High while it configured (p. 21), so the first pulse's rise is an edge, and its count of seconds in the status register; the interpolator beside it measuring the start's interval, its references one and two periods, reading 31 and 222 with its trims at the middle, 1.5 counts lower a step of trim, picks, not measurements, so that `interpolator_calibrate` searches and settles | the design's state, its flip-flops, latches and pads, which the part reads back where the configuration has unused bits and the mask leaves out (7-23): the model returns the configuration there; what the dummy bits hold; the memory's clearing time; the second chip's readback line; the start bit outside mode 3, which holds the gate open in the netlist and which the firmware never sets there; the interpolator's control lines (P76, P77, P79) and its timing; a write's own clock edges but for mode 3's start; noise in the interpolator; the bitstream's own logic, which the LCA model executes when the board file says `design = "lca"`, fast-forwarded between events to what clocking every edge gives ("The boards", "Not yet") | the same, behind the `xc3042` part |
| DUART | `0x200000` | both channels: mode registers, clock select in either baud-rate set, commands, a three-deep receive FIFO and the shift register behind it; the far end sending back to back at the line's rate, as a line with no flow control does, each character in the FIFO a character time after its start bit, the first's at the instant it is put on the line (the slice's end before); a character arriving with the receiver disabled lost, and one arriving with the FIFO full held in the shift register until a read makes room, lost to the next's start bit with the overrun error set, cleared by a reset error status or a receiver reset (3.2, 4.2.9.4); `TxRDY` and `TxEMT`, local loopback as the self-tests use it, the receiver taking the transmitter's characters enabled or not (4.2.7.4); ISR, IMR and the interrupt at level 2 through the IVR; the counter, counting down at 3.6864 MHz over 16 (`duart_counter_test`); the input port reading high; the output port, OP2 to OP7 the LEDs ("The LEDs") | the counter's interrupt and timer mode; parity and framing errors and break; RTS and CTS, and so any flow control | `scc68692` |
| EEPROM | `0x400000` | the AT28C64B (`hardware-investigations.md`, item 22; `third_party/microchip/AT28C64B.pdf`): 8 KiB from the dump, each write cycle's bytes written through to the working file as it ends, the file made whole and atomically, and locked; byte and 64-byte page writes, tBLC 150 µs, tWC 5 ms, DATA polling and toggle bit ("The EEPROM") | software data protection, off as shipped; write failures and endurance; the 5 ms power-on write inhibit | `at28c64b` ("The EEPROM") |
| Latch, switch | `0x300000`, `0x302000` | in CS5's 64 KiB window with the FPGA's port and the EFC strobe (`0x306000`, "The oscillator"), 8 KiB apart, ADDR13 and ADDR14 picking one (restart.md, "The chip selects"): the latch, each lamp reported as it changes ("The lamps"); the switch byte reads `0xff` | Power, which is not the firmware's | `74x374`, `74x244` |
| Front panel | QSPI select 0, 58503A only | Option 001's display and keys, framed and reset by the latch's bits 6 and 7, its commands' lamps and text shown, keys from a script, the self-test's `0x55` and `0xaa` ("The boards"; *inferred* where the code does not settle it) | the controller's own protocol, timing and display segments; a key held | the same |
| Alarm output | PF5, 58503A only | low while the alarm is on, reported ("The boards") | what the pin drives; FPGA register 3 bit 2, the alarm's other output | the same, on the chip's PF5 |
| Flash | `0x000000` | read-only | writing (the installer's flashing) | the board file's `[[memory]]` |
| RAM | `0x100000` | 64 KiB | | the board file's `[[memory]]` |

## The mc683xx backend

`--core mc683xx`, the default, runs the image on mc683xx's MC68331
("Running it" has the commands).  It loads the receiver's board file,
`boards/z3805a.toml`,
`boards/58503a.toml`, `boards/z3801a.toml` or `boards/58503b.toml` as the image's model
string or `--model` picks it ("The boards"), or the file `--board` names, through mc683xx-board
with every smartclock type registered ("The mc683xx device layer"),
and runs it with the board crate's own run loop, `Board::run_until`, on
board time in picoseconds (`src/mc683xx/`).  smartclock's map of
the board, `machine.rs` and its SIM, QSM and GPT are not on this
path: the chip's are.  The image replaces the file's `[board]
images`; every option the file declares is enabled (the 58503A's
front panel).

The command line works as on Unicorn.  What is the run's, not the
board's, is the parameters of the parts it belongs to, merged over the
file's by the loader (`overrides`, `Options::parameters`): `--eeprom` and
`--eeprom-dump` the EEPROM's `file` and `image`, without which a new
working file starts from the board file's image ("The EEPROM");
`--gps-start`, the position and
`--gps-leap-seconds` the engine's; the `--ocxo-*` options the
oscillator's, each only when given, so that a board file's term holds
without it; `--indicators` the lamp board's, the LED bank's and the
front panel's `report`; `--tap` the engine's.  `--gps` takes the synthetic
engine off the board: its serial nets end at the endpoint `gps`, and
its 1 PPS's net starts at the endpoint `gps_pps` instead; it rewires
the engine's nets in the board file's text before it is loaded, a
change of the board, not of a part's parameters.  The file's run-time
endpoints are bound to the command line's ports ("The ports").
A script's `key` steps press the front panel's keys as each step is
taken, at the end of the slice its time ends, as on Unicorn.  The
host steps the run (`mc683xx::Run`).  With `--deterministic` it runs a
slice of 4096 CPU cycles at most at a time, each ending at the next of
the run's own times: the script's next step, the run's end and the
external 1 PPS's next edge (`Run::step_until`).  Paced, it runs a frame
at a time to the board time due by the host's clock
(`Run::step_frame`), in mc683xx's quanta of 5 ms of board time, and a
frame ends at the next of the run's own times as a slice does: the
frame is given the host's clock's reading at which that time is due,
if it has come, so the time is taken at its time and none is dropped
from the pacing, to within the host's clock's nanosecond (a paced run
of a script ends in the deterministic run's state, the unit test
`a_paced_run_takes_a_scripts_steps_at_their_times`).  Between the
board's runs, a slice or a quantum, bytes go in and out of the
endpoints and the trace is written.  What the page gives (a key, a
control, a console line) and a TCP client's bytes are taken between
them too, so paced, at a quantum's end, not a slice's: each is traced
at the cycle it was taken at, and a deterministic replay takes it at
that cycle ("Replaying a run").

The run is two sides.  The board side (`mc683xx/powered.rs`) is one
power-up's board, built from the file (`load.rs`) with the run's
parameters (`overrides.rs`) and what a power cycle carries
(`carried.rs`); a power cycle drops it and builds the next.  The run
side (`mc683xx/runner.rs`) is what outlives it: the lines bound to the
endpoints, what runs alongside (a script, standard input), the keys
and controls given and not yet applied, the pacing and the panel page.
Each slice's end, or quantum's, takes its steps in one order, each
seeing what those before it did (`Side::slice_end`): what the slice sent, onto the
lines and the page's console, and the alarm's changes; what the
firmware sent the synthetic engine, for `expect-gps`; the trace and
the chip's events; what runs alongside, advanced to the run's clock, a
script's steps due by then taken; the bytes its `write-gps` steps gave,
onto the engine's line, the engine run then; the keys pressed, the
script's and the page's;
the lines typed on the page's console; the controls, in order, each
power cycle building the board again before those after it; and the
page, published when due.  A CPU halted in the slice ends the run after the
chip's events, with an error saying where it stopped, before what runs
alongside is advanced: a power cycle a script gives at that slice's end
is not applied, as it would not be a slice later.  The command line is checked once, before
either core runs, into the run it asks for (`config.rs`, "The library
and its host").

The chip sizes an externally terminated cycle from its device's DSACK
(MC68331UM 4.4.2, Table 4-12): the DUART's DSACK1 alone makes its select a 16-bit
port, and the board files attach the DUART on its lower lane, its
registers at the odd addresses.  A part whose `advance` leaves its next
event at its time ends the run with an error naming it (`RunError::Stalled`).
The boots need no flash status or autoselect reads (the boot check only
sums the flash, restart.md), so the flash has no command logic; the
installer's erase and download would.

The integration test `tests/boot_mc683xx.rs` boots the Z3805A to its
prompt and asks `*IDN?` at 2 s of board time.

### The mc683xx device layer

With the `mc683xx` feature, the default (mc683xx-cpu32,
mc683xx-modules, mc683xx-mc68331, mc683xx-board and
mc683xx-peripherals from crates.io, pinned to their 0.9.0 release
and bumped only on purpose, with mc683xx-target's bus types at the
same version), the board files
(`boards/*.toml`, `boards/README.md`) name the board's standard chips
by mc683xx-peripherals' stock types, each modeled from its datasheet
there (mc683xx's `docs/peripherals.md`), and
smartclock registers only what is the board's own: the EFC's strobe,
the QSPI's front panel, the FPGA and the models off
the bus, and the sinks the nets end at (`board/registry.rs`,
`register_all`, the stock types after ours).  A board file records each part's marking with
mc683xx-board's `marking` key, which no type reads.

| Chip | Stock type | The board's parameters | Ports the nets use |
| ---- | ---------- | ---------------------- | ------------------ |
| the DUART | `scc68692` (the Z3805A's and 58503A's U3, and the Z3801A's inferred), `mc68681` | `x1_hz` 3686400 | `RxDA`, `TxDA` the GPS link, `RxDB`, `TxDB` the host port, `OP2`-`OP7` the LEDs |
| the ACIA | `mc6850` (the 58503B's) | `tx_clk_hz`, `rx_clk_hz` 153600 | `RxData`, `TxData` the GPS link |
| the EEPROM | `at28c64b` | `image`, the receiver's EEPROM dump; `write_cycle_us` 5000, within Table 6-4's 10 ms, which both of the firmware's polls outlast; `file`, the run's (`--eeprom`) | -- |
| the lamp latch | `74x374` | -- | `Q0`-`Q7`: the lamps; the 58503A's panel frame on `Q6`, its reset on `Q7` |
| the switch byte | `74x244` | `levels` 0xff, open; the Z3815A's low byte 0xf7, bit 3 low ("The boards") | -- |
| the ADC | `adc0838` | `vref_volts` 5, `noise_volts` 0.0068, 0.35 of a count | `CH0`-`CH7` |
| the hex DAC | `mc144110` | `vdd_volts` 5 | `R1Out`-`R6Out` |
| the EFC DAC | `ad569` fed by a `74x164` chain, `cascade` 2, its QH on MISO; on the 58503B, the Z3815A and the Z3816A the chain's `clock` `"falling"` | `cs`, `hbe`, `ldac` tied low, and `lbe` on the Z3815A and the Z3816A; `vref_negative_volts` -5, `vref_positive_volts` 5; `initial` 0x8000 | `DB` from the chain's `Q`; `LBE` from the strobe, a `logic` net; `VOUT` |

What the stock chips need of the board, each stated in its file:

- The DUART on the lower lane of the 16-bit port its DSACK1 sizes,
  RS1 to RS4 on ADDR1 to ADDR4 (`lines = [1, 2, 3, 4]`), and its
  acknowledge on the lower lane too.
- The ADC's address bits are ODD, SELECT 1 and SELECT 0 (SNAS531B,
  Table 2), so `adc_read`'s channel n is the pin 2 x (n & 3) + (n >>
  2): ADC_DOUBLE_OVEN 1 on `CH2`, ADC_5V 4 on `CH1`.
- The hex DAC shifts its 36 bits in as DAC 6 first, so `hdac_set`'s
  slot 2, the low trim, is `R4Out`, and slot 3 is `R3Out` (MC144110/D
  4.1, Din).
- The EFC's word reaches the AD569 through two 74HC164s (efc.md,
  Tom Van Baak's Z3801A traces; hardware-investigations.md, item
  27): with CS, HBE and LDAC tied low the 8 MSBs follow DB as the
  chain shifts, and the strobe's LBE pulse completes the word.  The
  self-test's 0x5555 and 0xaaaa therefore reach VOUT's upper byte
  without a strobe, which the Unicorn board's DAC does not do.  The
  board files give the AD569 and the 74x164 chain `initial` 0x8000,
  mid-scale, as the Unicorn board's DAC starts (*inferred*: neither
  part's power-on state is known): with HBE tied low the MSB latch
  follows DB, the chain's Q, from power-on, so VOUT starts at 0 V, the
  EFC's middle, until the firmware's first word.
- The chain's edges follow the firmware's modes.  The Z3805A's and
  the 58503A's images write QSPI device 7 in SPI mode 3 and read it in
  mode 2 (SPCR0 `0x8308` and `0x8208`; `0x207e6` in the Z3805A 3543B,
  `0x1f5e4` in the 58503A 3704), and `qspi_efc_test` compares the word
  read back as it is (`selftest.c`): a stock 74x164, shifting on SCK's
  rise.  The 58503B's, the Z3815A's and the Z3816A's write it in
  mode 2 and read it in mode 3 (`0x8204` and `0x8304`; `0x2ee76`,
  `0x30f80` and `0x2f886`), as every image does the hex DAC, and their
  EFC tests (`0x28bb4`, `0x290fa`, `0x28b38`) shift the word read back
  right a bit before comparing it with 0x5555 and then 0x2aaa: a
  register shifting on SCK's fall, as the MC144110 does.  Their board
  files so give the chain `clock = "falling"`, SCK inverted to its
  CLK (*inferred*: whether
  the board inverts SCK to the 74HC164s or fits another part is open;
  the firmware settles only the edges).  The Z3815A's is read from its
  image; first checked on a copy of its file with the Z3816A's Oncore
  in place of its Furuno, before the Furuno was written, and now on its
  own file, each run with `--model z3816a --board` ("The boards"), its
  QSPI test (`0x29196`) passes the FPGA
  check (bit 6, PF6 pulsed at `0x35bfc` to `0x35c0c` before the
  readback, the `fpga-readback` net), the EFC test (bit 7) and the
  ADC's (bit 1), which reads channel 1, the 5 V rail.
- Analog nets carry volts.  The sources off the bus drive the
  voltage the ADC converts to the count the Unicorn board's ADC gives
  (count x VREF / 256, `adc_volts`); the OCXO takes the AD569's volts
  back to the 16-bit word (`efc_word`), and the interpolator the hex
  DAC's back to its 6-bit code (`trim_code`).  Each takes its
  converter's span from its own parameters, which the board files give
  as the converter's (`adc_vref_volts`, `efc_vref_negative` and
  `efc_vref_positive`, `trim_vss` and `trim_vdd`; a test holds every
  file's to its converter's).
- MISO no chip drives reads 1s; the ADC's reply in mode 3 has its
  reading in bits 9 to 2, as `adc_read` takes it.

The lamps and the LEDs are reported, as `--indicators` gives them, by
the sinks their nets end at, `lamp-board` and `led-bank`, from the
levels the stock latch and DUART drive on their pins (PLAN.md, "The
emulator's indicators stay observable from outside"), in the Unicorn
board's record format.  `led-bank`'s ports are the LEDs' names,
`heartbeat`, `installer`, `cpu`, `ram`, `duart`, `selftest`, OP2 to
OP7 as `board_led` (smartclock.h) names them.

| Type | Model | Parameters | Ports |
| ---- | ----- | ---------- | ----- |
| `efc-strobe` | the EFC's strobe | -- | `out` logic out, low for three clocks a write (MC68331UM 4.5.1) |
| `lamp-board`, `led-bank`, `pps-buffers` | none: sinks the nets end at | `report` (false), the indicators' records; `led-bank`'s `count`, at most 6, the LEDs fitted, the first that many, the rest's nets ending at nothing | `enabled`, `active`, `alarm`, `gps_lock`, `holdover`; the LEDs' names; `in`, all in |

The 58503B's ACIA is mc683xx-peripherals' stock `mc6850`, modeled
from the MC6850 data sheet (Motorola DS9493R4, in mc683xx's
`third_party/datasheets/MC6850.pdf`; mc683xx's `docs/peripherals.md`,
MC6850), which the 58503B's driver (`0x2d26a` to `0x2d59c`) uses as
`boards/58503b.toml` states: RS on ADDR1, `lines = [1]`, picks the
status and control registers or the data registers; Tx CLK and Rx CLK
at 153.6 kHz (`tx_clk_hz`, `rx_clk_hz`), the control word's divide by
16 making the Oncore's 9600 baud; its receive line `RxData` and its
transmit line `TxData`; the level 2 autovector, CS4's acknowledge
asking AVEC itself, the part giving no vector.  Its E is the select's
cycle, so no net carries ECLK.  A frame on `RxData` starts no earlier
than the frame before it ends, so an external engine's bytes on
`--gps`, which come onto the line together, arrive at the line's
rate.  FE and PE stay with their character through its read until the
next transfer, and an overrun shows once the character before it is
read, as the data sheet sequences it.  DCD and CTS are undriven,
read low (*inferred*: the driver reads neither bit).

The flash and the RAM are no peripherals: a board file's `[[memory]]`
entries are them, the flash's image read-only on CSBOOT and CS1, the
RAM on CS0, CS2 and CS3.

The strobe implements mc683xx-cpu32's
`peripheral::MappedPeripheral` and mc683xx-modules' `Timed`, which the
Unicorn board reaches too (`board/select.rs` builds a byte cycle for
each access on its select), and the board reaches through
mc683xx-board's `adapter::MappedAdapter`.  The models count time in the chip's
system clocks, which their rates assume run at 16.777216 MHz, as they
do once the reset code sets SYNCR; a peripheral converts board time,
picoseconds, through the board's system clock, each call's own
(`Drive::system_clock`), and the one last told it
(`Peripheral::system_clock`) for `next_event` and `sample`
(`board/system_clock.rs`).  The models the chip does not clock, the
OCXO and its ovens, the temperature and the GPS engine, count the
board's own cycles instead, 16.777216 MHz from power-on: each reset
puts the synthesizer back to its slower default until the reset code
sets SYNCR, so on the chip's clocks the GPS engine's 1 PPS would fall
behind the oscillator's at a watchdog reset, which the 58503A's warm
start would read as a TI past its holdover limit, and wait in holdover
for good.

Every peripheral on the board saves its state for the board's
snapshot but the GPS engine, the OCXO and the FPGA: the stock chips
their own (mc683xx's `docs/peripherals.md`); ours through
`mc683xx_cpu32::cpu::state::Save` (`board/snapshot.rs`): the strobe,
the front panel, the interpolator, the sinks' levels, the temperature,
its warm-up's place, which the `skip-warm-up` control moves, and the
supplies, which hold nothing.  What a run chooses, whether a sink
reports its indicators, stays out.  So a whole board's `save` still
refuses.

### The QSPI's front panel and the models off the bus

With the `mc683xx` feature the OCXO and the synthetic GPS engine
implement mc683xx's `Timed`, and each, with the 58503A's front panel
and the analog sources, is an mc683xx-board peripheral registered by type name from its parameter table
(`board/participants/`, `register`), with the ports below; the
Unicorn board runs the same models unchanged.  Each model's net-facing
side is a small interface of its own, which the peripheral maps onto
its ports: the engine's `Port`, the OCXO's `set_efc` and `Clock`, the
panel's `control` and `press`.  The ADC's levels are `analog.rs`'s
sources, wired to its channels as the board wires them (`Wiring`).
The models keep the CPU's cycles at 16.777216 MHz ("Time"); a
peripheral converts board time to them and back.

The front panel and the FPGA's QSPI side are clocked through
mc683xx-board's SPI edges (`mc683xx_board::spi`, `board/spi.rs`), as
the stock chips are: each model's reply is shifted out, and its word
sampled, on the SCK edges its part acts on, in the mode SPCR0's CPOL
and CPHA give the transfer, so that in the firmware's mode both are
whole and in another both come skewed.  The front panel is SPI mode
3's own part, sampling on the rise and changing on the fall, its first
bit out at the first fall (*inferred*: its controller is not traced;
mode 3 is the mode `qspi_queue_setup` sends it in, 58503a-3704.md,
"The front panel").  The XC3042 takes DIN on CCLK's rise (Slave Serial
mode, `xc3000.pdf` 7-31) and shifts its readback out on each rise
(Readback, 7-23), the stream's first bit out from the readback's start
(*inferred*: what RDATA holds before the first rise); in mode 2, the
firmware's for it (structures.md, "The QSPI's devices"), every bit is
whole.  The panel's self-test (0x55, then 0xaa) and the FPGA's
configuration and readback are as before ("The boards").

The FPGA is `fpga.rs`'s event-level model behind the XC3042's
ports (`board/fpga_part.rs`).  Its P7 is the `ten-megahertz` clock
net from the OCXO, which carries the oscillator's frequency in
nanohertz from each change with its phase carried across; the part
keeps the net's history and gives the model its phase, seconds of
the oscillator's own time, and the board cycle a phase is reached
at, through the system clock.  The part's oscillator retunes at its
changes and each second of drift.  The 1 PPS is 10 us wide (the width
is ours); the design starts at the configuration's last transfer; the
interpolator carries the hex DAC's trims to it on the `bus` net, the
low trim in bits 0 to 7 and the high in 8 to 15, since the model
computes the interpolator's reading itself.  The CPU's reset does not
reach it ("Across a warm restart").

The synthetic engine reads a frame at its stop bit's end, and its
answer goes out from there, each frame at its start bit.

| Type | Parameters | Ports | Does |
| ---- | ---------- | ----- | ---- |
| `hp58503a-front-panel` | `report` (false) | `run`, `frame`, `key-time` to `key-shift` logic in | one 8-bit transfer a byte; `run` low holds it in reset, `frame` low frames a command (the latch's bits 7 and 6; "The boards"); a key pressed as its input goes low; the display traced with `report`, no port; a byte in flight while it is selected and has not had it |
| `oncore` | `start` (RFC 3339), `protocol` (`motorola-binary`), `engine` (`b1121`, the six-channel VP B1121, `b4121`, the eight-channel VP B4121, `b4121-no-time-raim`, the B4121 with no `I` in its options, or `r5122`, the eight-channel UT Plus; `b1121`), which `--gps-engine` gives in place of the file's, `baud` (9600), `frame_bits` (10), `latitude`, `longitude`, `height` (within the Oncore's ranges, as `--gps-latitude` and the rest), `leap_seconds` (18), `tap` (false), `antenna` (true), whether its antenna is connected, which a power cycle carries; `answers` (true); `saved` (none), `58503a`, the configuration it powers up in | `rx` serial in, `tx` serial out, `pps` edge out | the synthetic engine: frames on `rx` answered on `tx` back to back at the line's rate; `pps` high 200 ms from each second (VP Oncore Engineering Notes, 6) |
| `ocxo` | `hz` (10 MHz, the model's), `offset`, `aging`, `warm_up`, `efc_gain`, `white_fm`, `random_walk_fm` (where the file gives none, the model's firmware's G, the offset that puts it on frequency at the EFC the EEPROM's lifetime record holds ("The oscillator the EEPROM describes"), the others 0; the `--ocxo-*` given take their place), `warm_up_seconds` (0) and `warm_up_settled` (false), the warm-up's place at power-up, which a power cycle carries, `aging_seconds`, `noise_draws`, `random_walk` (each 0, the crystal a power cycle carries), `efc_vref_negative` (-5 V), `efc_vref_positive` (5 V), `adc_vref_volts` (5 V) | `efc` analog in, volts, `out` clock out, `outer_oven` logic in, `oven_current`, `oven_monitor`, `outer_oven_monitor` analog out, volts | "The oscillator"; `out` a clock net, the frequency in nanohertz from power-on and at each change, the EFC's, a noise draw, and each second while aging or warm-up move it; `outer_oven` not acted on; the ovens' monitors sampled as they warm |
| `temperature-sensor` | `adc_vref_volts` (5 V), `warm_up_seconds` (0) and `warm_up_settled` (false), the warm-up's place at power-up, which a power cycle carries | `out` analog out, volts | the temperature inside |
| `supply-monitor` | `adc_vref_volts` (5 V) | `plus_15v`, `plus_5v`, `minus_15v`, and the Z3817A's `plus_12v_b` and `minus_12v_b`, analog out, volts | the supplies' monitors |
| `timeref-selector` | `timeref1`, `timeref2` (`none`, `1pps` or `pp2s`; `none`), `timeref1_valid`, `timeref2_valid` (false), `timeref1_change`, `timeref2_change` (a pulse the reference changes to; none) with `timeref1_change_second`, `timeref2_change_second` (the second from power-up it changes at; both or neither), `select_high` (1 or 2; 1) | `select` logic in, `pulse` edge out, `valid` logic out | the Z3817A's two references and the selector between them: the selected one's pulse, 200 ms high from each second of board time, or each even one for PP2S, and its VALID; the selector's input high until driven |
| `efc-monitor` | `counts_per_efc` (required), `efc_vref_negative` (-5 V), `efc_vref_positive` (5 V), `baud` (19200), `frame_bits` (10), `period_seconds` (1), `ready_seconds` (0) of warm-up, from which the status byte's ready bit is set, `warm_up_seconds` (0) and `warm_up_settled` (false), the warm-up's place at power-up, which a power cycle carries | `efc` analog in, volts, `tx` serial out | the Z3817A's oscillator's report: `0x55`, the EFC the volts give back, 20 bits, times `counts_per_efc` in 24, the status byte, its bit 0 the ready bit, the sum |
| `clocked-efc-monitor` | `efc-monitor`'s `counts_per_efc`, `efc_vref_negative`, `efc_vref_positive`, `period_seconds`, `ready_seconds`, `warm_up_seconds` and `warm_up_settled` | `efc` analog in, volts, `clock` logic in, `framed` and `data` logic out | the Z3815A's puck: `efc-monitor`'s report clocked out, `framed` raised each period with the first bit, most significant first, on `data`, the next at each rise of `clock`, `framed` lowered after the last; a period begun while a report goes out passes |
| `efc-shift-register` | `initial` (0) | `Q` bus out | sixteen stages shifting MOSI in on SCK's fall, the last on MISO, the stages on `Q`; saved and restored |
| `xc3042` | `design`, the design the model runs: `z3805a`, `58503a-z3801a`, `z3817a`, `unmodeled` or `lca`, the bitstream's netlist; with `lca`, `bitstream`, the one its configuration must be, `z3805a` ("The boards") | `P82` edge in, `P7` clock in, `pps` edge out, `INIT` logic out, `DONE` logic in and out, `RESET`, `M0` logic in, `P3-P6,P8-P11` bus in, `P84` edge in (the Z3817A's hand model alone) | `fpga.rs`'s event-level model ("The interval counter"), its 10 MHz the clock net on `P7` (`board/fpga_part.rs`), above |
| `smartclock-interpolator` | `trim_vss` (0 V), `trim_vdd` (5 V) | `trim_low`, `trim_high` analog in, volts, `byte` bus out | carries the hex DAC's trims, as codes, to the FPGA's model |

### The board files' additions

The board files' additions for mc683xx: the EFC strobe's `efc-strobe`
type and its `efc-load` net to the AD569's LBE; `fpga.DONE` reading the
`fpga-done` net back; and on the 58503A the ADC's channels and the
interpolator's trims, wired as the Z3805A's (each `# inferred`): its
ADC self-test wants readings neither 0 nor 0xff, and its
interpolator calibration the trims' effect, so without them it fails
the QSPI self-test and calibrates forever.

## What the boot shows

What each test below checks is its own assertions; a figure the boot
gives beyond them (when the prompt comes, when the loop locks, its TI)
is what a run gives, on its core, not quoted here: a script gives it
("Scripts"), its records timed by board time.

The Z3805A 3543B reaches its prompt; `*IDN?` answers
`HEWLETT-PACKARD,Z3805A,3625A01487,3543B-A`, the image's revision and
the dump's serial number; `:SYSTem:ERRor?` `+0,"No error"`; the status
screen shows the loop in power-up, with no satellites.  The power-up
self-test's result, `:DIAGnostic:TEST:RESult?`, once the GPS engine's
test has timed out, has only the GPS engine failing: the CPU, RAM,
EEPROM, UART, QSPI, FPGA counter, interpolator, internal reference and
power tests pass.  That is the boot with channel A served and nobody
connected (`--gps`), as the boot test runs it.  The prompt comes
before the self-test, whose UART check disables the host port's
receiver, resets it and runs it in local loopback
(`duart_loopback_test_b`), and whose QSPI check masks the interrupts
about its transfers, so what a client sends then is lost from where the
receiver went deaf, its end of line too, as on the board's DUART;
`--watch-code` on the check gives when.  `tests/boot.rs` sends a query
into the UART check and finds its echo cut short and the same query
answered after.  `tests/boot.rs` checks
the answers, the status screen and the self-test's result, asked for
once the GPS engine's test has timed out, only GPS failing, from scripts under `--deterministic`, each query sent at the prompt
after the last's answer, so a loaded host cannot move them; and with
the synthetic engine, the status screen tracking six by 60 s of board
time.  The loop leaves OCXO warm-up and turns the outer oven on
(`0x45f46` setting PGP5) when the oven channel has fallen past -2.0,
and then waits for GPS.

With the synthetic engine the firmware runs its GPS script: `@@Ca`,
`@@Cg`, `@@Ci`, `@@Cj`, then the init script's settings from `@@Ab` on,
each answered, and goes on to track six satellites, the sky's two
lowest under the mask, as the status screen shows.  The engine's 1
PPS, each second's start by default (`Synthetic::on_pulse`), goes to the GPT's pulse
accumulator: once `gps_pps_check` (`monitors.c`) sets PACTL to `0x50`,
PACNT counts the pulses, the check passes, and the status screen has
the GPS 1PPS valid and the clock synchronized to it.  With the engine
every power-up test passes.

The 1 PPS starts the FPGA's interval count ("The interval counter"),
so the loop has its readings, no "Gate failure", and goes on: the
full syncs the power-up asks for hold the divider at its pulse until
the GPS 1 PPS restarts it ("The interval counter"), the loop locks and
steers the EFC.  `tests/ocxo.rs` checks the lock and the EFC from an
oscillator 1e-8 fast ("The oscillator"); the peripheral checks' `loop`
check, the lock on both cores.

The 58503A 3704 boots on the same models, with its own board ("The
boards") and nothing else changed, with its EEPROM dump
(`third_party/firmware/images/eeprom/58503a-3710A01056-eeprom.bin`):
`*IDN?` answers `HEWLETT-PACKARD,58503A,3710A01056,3704-C` and
`:SYSTem:ERRor?` `+0,"No error"`.  Every power-up test passes with the
synthetic engine, and all but the GPS engine's with none (`--gps`), as
on the Z3805A; the front panel's self-test, which the result does not
count, passes, and `fpnl` runs: the display blanks, `HP` scrolls in
with the key lamps sweeping to `HP 58503A`, then the Status key's lamp
blinks over the banner and `OCXO WARM UP` follows.  With the synthetic
engine the status screen tracks six satellites; the loop leaves
warm-up, the display going through `LEAPSEC CALC` to `OUTPUT VALID`,
and a key gives its mode's line (Time `TIME: **:**:**` before the time
is known, Lat, Shift then Alt `NO ALARM`, Sat `6 TRACKED`).  Nothing in
the boot raises the alarm, so PF5 stays high.  `tests/boot_58503a.rs`
checks the answers, the result but for GPS, and the banner, from a
script whose queries go at 10 s of board time, past the self-test and
the banner, so a loaded host cannot move them; and, from a
deterministic run with the synthetic engine whose keys are pressed in
warm-up and which then skips it, each display text quoted here, in its
turn (`the_58503a_display_shows_what_the_docs_quote`).

The Z3801A 3543 boots on its own board ("The boards") with its EEPROM
dump (`third_party/firmware/images/eeprom/z3801a-3542A01548-eeprom.bin`)
to the steady state the Z3805A's reaches: its prompt, its GPS init
script to `@@Bk` and the `@@Cg` after it (script `0x48`,
firmware/oncore/z3801a-3543.txt), then `*IDN?`, answering
`HEWLETT-PACKARD,Z3801A,3542A01548,3543-A`, `:SYSTem:ERRor?`
`+0,"No error"` and, with the synthetic engine, the self-test's result
`+0,ALL`.  `tests/boot_z3801a.rs` checks the transcript, which has no
echo, on each core the build has, from a script that waits for each
step.

The 58503B 1.01.04 boots on its own board ("The boards"), its EEPROM
a 58503B's dump, its serial number and position replaced ("The
EEPROM"), its FPGA's bitstream run on the LCA model: its prompt, then
`*IDN?` answering `HEWLETT-PACKARD,58503B,L00000,1.01.04-D`, the
dump's suffix and the replaced serial number, `:SYSTem:ERRor?`
`+0,"No error"`, the power-up self-test's result `+0,ALL` and, once
the engine has reported, `:PTIMe:GPSystem:POSition?` HP Santa Clara
Division's, its height the ellipsoid one.  The port does not echo: the
dump's parameter record has echo off, and the 58503B takes it.  The
self-test passes every test, the QSPI's (bit 5 of the Z3816A's table)
on two things of its board file's.  Its FPGA check (`0x28b6e`, bit 6
of the QSPI's result) reads the bitstream back, started by PF6
(`0x3284e` to `0x3285e`), which the `fpga-readback` net carries to the
part's M0, as on the other boards; and its EFC test (`0x28bb4`, bit 7)
wants the chain shifting on SCK's fall, as the Z3816A's.  The status
screen's health monitor shows `Self Test: OK`; the synthetic image
gives the same result.  From an erased part the EEPROM's fails,
bit 3, and the result comes later in the boot, *inferred* held up by
the firmware's first writes of the part, so that a query early in the
boot finds it still clear.  With the synthetic engine the front panel
shows `HP` scrolling in and the key lamps sweeping, as the 58503A's;
the engine, a UT Plus or with `--gps-engine b4121` a VP, is found as
eight channels, set up, and tracks.  With the
warm-up skipped the loop goes through its stages to `LOCK`, the GPS
Lock lamp lit, its TI pulled in, its EFC near the one the dump's
lifetime record holds, on the oscillator the dump describes ("The
boards"); from the synthetic image too, on type 0's oscillator.
`tests/boot_58503b.rs` checks the answers from the dump and the
netlist the configuration picks, on the UT Plus and on the VP, that
the boot's second load of the FPGA, its loader driving DONE Low on the
net the part shares with PF3, clears the configuration and configures
it again, and,
in four more runs, one from each image on each engine, ignored but
when asked, in release, the LCA model being slow once the counter
runs, the lock, the lamp and the TI.

## The Unicorn core

Unicorn, through the `unicorn-engine` crate, is a second core for
development, `--core unicorn`, on smartclock's own map of the board:
it was the emulator's first core, and the peripheral checks hold
mc683xx to it ("The peripheral checks").  It is frozen: kept as it is,
not reworked to share mc683xx's run loop (PLAN.md, "The emulator").
It is GPL-2.0-only, and this
project is GPL-3.0-only, so a binary that combines the two cannot be
distributed.  So it sits behind the `unicorn` feature, off by default:
`make ci`, ci.yml, `make windows`, `make deb` and `make release` never
build it, and a binary built with it is for local use only.  It builds
QEMU's m68k target from C, which needs cmake and libclang (with its
headers, or `BINDGEN_EXTRA_CLANG_ARGS` pointing at gcc's, which the
Makefile sets); its build uses ninja when it finds it and cmake's
Makefiles when not, so ninja is optional.

    make emulator-unicorn    # the same as make emulator with both cores, and the checks across the two

`make emulator-unicorn` checks for cmake and libclang first, libclang
under `LIBCLANG_PATH`, an LLVM's `lib`, `/usr/lib/*-linux-gnu` or
`/usr/lib64`, any one of them, and
covers the tests of either core alone and the peripheral checks
across the two; CI does not run it.

### Its board and its time

On Unicorn, time is the board's: each block of code the core runs counts two
cycles a byte, an estimate, and every device's timing is counted in
those cycles, 16777216 a second, the periodic timer ticking every
16384 of them, so a boot is the same however fast the host is.  A block
is counted as it starts, and a run ends at the first block to start
with its budget spent, which neither runs nor is counted; a run's first
block always runs, whatever it costs, so the CPU makes headway on a
budget smaller than its block.  The
machine runs the core in slices of at most 4096 cycles, a quarter of a
periodic tick, ending at the board's next event or the next event of
what runs alongside it, a script's step or the GPS engine's second;
interrupts are delivered between slices, and while the CPU is stopped
time jumps to the next event.

tBLC is shorter than the machine's slice, so on Unicorn the EEPROM
takes each access at its own time, the core's estimate within the
slice running, not the slice's start; so does the FPGA ("The interval
counter"), alone of the other devices.

The Unicorn board keeps smartclock's own models of the DUART, the
EEPROM, the latch, the switch byte, the ADC and the DACs, which no
longer run on mc683xx ("What is modeled").  Its EEPROM is smartclock's
own model (`board/eeprom.rs`), the part as "The EEPROM" gives it, but
for the file:

- It is made, from the dump or erased, or padded erased when it is
  short, by writing a sibling (`NAME.partial`), syncing it and renaming
  it over the working file: it is the old file or the new one.
- The lock is a sidecar's, `NAME.lock`, taken
  before the working file is looked for, so two emulators starting on
  a missing file cannot both make it; the sidecar stays.

The Z3801A's SCI is modeled and stays silent, and Unicorn's DUART
models no parity, so the Z3801A's 7O1 bytes pass as they are ("The
boards").

### Its tracing

Unicorn's CPU records are smartclock-emu's own; `cycle` is the board's
clock in CPU cycles, 16777216 a second:

    smartclock-emu: position cycle=N pc=0x0000069a sr=0x2700 vbr=0x00000000 a7=0x0010fffe instructions=N
    smartclock-emu: device cycle=N pc=0x000145ea access=write address=0x0020001d size=1 value=0xff
    smartclock-emu: exception cycle=N vector=39 pc=0x00013424 sr=0x2714
    smartclock-emu: exception cycle=N vector=66 pc=0x00028086 sr=0x0000 level=5

- `devices`: every access to a device region (the DUART, chip select 5,
  the EEPROM, chip select 4, the MC68331's modules), read or write,
  with its size in bytes and its value.
- `exceptions`: every exception the core takes, `trap #n`, faults and
  interrupts, its vector number, the status register before it and the
  program counter it was taken at: the instruction's for a trap or a
  fault, the next one's for an interrupt, which has a `level` too.
  `rte` is not traced.

A device access's or a trap's cycle is the end of the block of code
making it, since the core counts time a block at a time; an
interrupt's is between blocks, exact.

`--count-instructions` counts every instruction, through a hook on each
that slows the core, and adds `instructions=` to the position records
and the stop.  It is Unicorn's: mc683xx's records count them always,
and on mc683xx the option is refused.  `--watch-code ADDR` traces each
time the instruction at an address runs, with the registers and the
top eight longs of the stack, comma-separated:

    smartclock-emu: code cycle=N pc=0x0001dee6 sr=0x0000 d=00102b4e,0010bf00,00000000,00000000,00000000,00000000,00000000,00000000 a=0010a95e,0010bf38,00000000,00000000,00000000,00000000,0010bf3c,0010bf28 stack=00029b6c,0002a0a8,00000000,00000000,0010a95e,00000000,00000000,00000000

### What differs from mc683xx

- Time.  mc683xx charges the CPU32's instruction timings, Unicorn about
  two cycles a byte of code, so the boot takes longer in board time on
  mc683xx.  Whatever is timed
  (the tasks' interleaving, how often a loop polls, how many EFC
  writes a span holds) differs; what the firmware decides does not.
- Unicorn (QEMU) traces a device access its size does not align as
  the aligned accesses covering it (`move.l (2,a2)` at the EEPROM's
  `0x400060` as longs at `0x400060` and `0x400064`); mc683xx traces
  the byte cycles the CPU32 makes on the 8-bit port.
- The FPGA: Unicorn's OCXO retunes each slice, where mc683xx's part
  retunes at its changes and each second of drift; on Unicorn the
  design starts at the slice's end after the configuration's last
  transfer.  The CPU's reset reaches it on neither ("Across a warm
  restart").
- The board files' additions, which Unicorn's board needs none of
  ("The board files' additions").

### The peripheral checks

`tools/peripheral_check.py` exercises each of the board's peripherals
through the firmware on both cores, Unicorn the oracle, and prints a
table: check, Unicorn's result, mc683xx's, agree.  Every run is a
script at fixed board times, `--deterministic`, the synthetic engine
from a fixed `--gps-start`, a fresh working EEPROM from the receiver's
dump.  What is compared is what the firmware gives, never when:

    cargo build --release -p smartclock-emu --features unicorn
    tools/peripheral_check.py [--slow] [--receiver z3805a] [--check gps] [--details]

| Check | What | Receivers |
| ----- | ---- | --------- |
| `host` | `*IDN?`, `:SYSTem:ERRor?`; the console by `:SYSTem:LANGuage "PFORTH"`, `1 2 + .`, `halt` back to `"PRIMARY"` (console.md) | all |
| `gps` | both ways off `--tap`, the firmware's commands (`gps <`) and the engine's answers (`gps >`), by message, in order, and the distinct commands' count; `:GPS:SATellite:TRACking?`, its count (six), the predicted, `:GPS:POSition?`, the status screen's | all |
| `sci` | the ES message off `--sci-capture`, raw: 16 bytes, digits and CR (`es_build`, `ES_DIGITS_LENGTH`), 2 s apart (`timecode_update` sends it on even seconds of the firmware's clock; rewritten/z3805a-3543b/timecode.c), its offset, status and summary | the Z3805A; the 58503A's board file has no SCI endpoint, and the Z3801A no SCI |
| `eeprom` | `:GPS:REFerence:ADELay 150E-9` (097-59551-02 5-17), read back after a second boot from the same working file | all |
| `time` | `:PTIMe:TCODe?` at 30, 40 and 70 s: 10 and 30 s apart; its flags; the leap second's state | all |
| `adc` | the console's `pr_adc_avg`, `:DIAGnostic:TEMPerature?`, `:DIAGnostic:ROSCillator:CURRent?`, in the health monitor's bounds, compared within a tolerance | all |
| `dacs` | the EFC (`:DIAGnostic:ROSCillator:EFControl:ABSolute?`, `pr_efc`); the hex DAC's `hdac_shadow` read with `c@` | all |
| `fpga` | `:DIAGnostic:TEST:RESult?` at 10 s of the session, with the synthetic engine: `+0`, the counter's and interpolator's tests among it (with no engine it becomes `+512`, the GPS bit, once the GPS engine's test times out, as `tests/boot.rs` checks); `interpolator_calibration_state` | all |
| `indicators` | the lamps' and LEDs' changes to 60 s, in order, and the heartbeat's period | all |
| `keys` | each key's first display record and its texts (`6 TRACKED`, `TIME: **:**:**`, ... `NO ALARM`) | the 58503A |
| `alarm` | `*ESE 32`, `*SRE 32`, a command error: `:LED:ALARm?` 1 and PF5 low; `*CLS`: both off (58503a-3704.md, "The alarm outputs") | the 58503A |
| `watchdog` | `4 crash` in the console (restart.md, "The watchdog"): one watchdog reset, the boot again, the restart logged | all |
| `loop`, `--slow` | `--ocxo-offset 1e-8`, 2000 s, read each 60 s: `LOCK` within a read, the power-up stages ending on whole seconds the CPU's speed moves; the TI pulled in below half its peak, the cores' last within 5 ns; the EFC within 100 counts of on frequency and the cores' within 32; the interpolator's calibration record, its 9 bytes read with `rb`, the same on both cores | all |

What each check gives, on each core, and whether they agree is the
script's own table: `tools/peripheral_check.py`, with `--slow` for the
loop and `--details` for the reads.

The ADC's readings are compared within a tolerance (0.3 degrees,
0.05 V on 5 V, 0.1 V on the 15 V supplies and the ovens, 2 mA): the
noise's seeds differ by design, `--seed` itself on Unicorn and
`Environment::seed_for(name)` for each part on mc683xx, and the same
seed would not give the same readings anyway, since the cores sample
at different times while the board warms.  The firmware's own clock,
before GPS time is valid, runs behind on mc683xx, as the event log's
times do: its first internal 1 PPS comes later in board time, the
FPGA's configuration ending later under the CPU32's timings; only the
advance is compared.  The loop's TI is compared within a tolerance too:
the firmware's loop acts at different board times on the two cores,
and with the oscillator 1e-8 fast a second is 10 ns of phase, modulo
the 100 ns tick, so the two pull in from different phases along the
same curve.

The Z3801A's port does not echo ("The boards"), so where the others'
answers follow their echoed commands, its are paired with the lines
the script sent, in order, each the text after the prompt before it;
its event log gives times as GPS seconds in hex (firmware/z3801a-3543.md,
"How it differs from the Z3805A"), which the watchdog check reads as the time of day.

`tests/peripherals.rs` runs the fast checks as integration tests on
both cores (`make emulator-unicorn`, by hand): the host port and console, the
GPS link both ways, the EEPROM across a reboot and the watchdog's
reboot on each receiver, the Z3805A's ES message, the 58503A's keys and
PF5; the loop's is ignored, being slow.

`tools/trace_diff.py` compares a trace of either backend with one of
the other, or with a reference, by the order and values of the board
devices' accesses as bytes, cycles dropped: `--writes` compares writes
only, `--reads-as-sets` the reads between two writes as a set,
`--by-region` each device's window alone, so that the tasks'
interleaving does not end the comparison at the first task switch,
`--exceptions` adds the exceptions' vectors.  It reports the first
divergence of each.

So a boot on one core is compared with one on the other by its device
accesses, `--writes --by-region`, its transcript and its EEPROM after:
the firmware's decisions agree, and the order of what is timed (the
tasks' interleaving) may not.

### The condition codes

A fault of Unicorn's, the development core.  Unicorn cannot read the m68k's status register without spoiling it.
QEMU keeps the condition codes lazily, as the last flag-setting
instruction's operands and the kind of operation, and computes them
when an instruction needs them; Unicorn's register read
(`qemu/target/m68k/unicorn.c`, `UC_M68K_REG_SR`) marks them computed
before computing them, so the codes it returns are the operands taken
for flags, and the CPU goes on with those.  Each time the machine asked
whether an interrupt's level was admitted, and each time the core took
one or a trap, the codes between a compare and its branch could flip.

The firmware showed it in its kernel.  `kernel_check_task` (`0x15b3a`)
returns Z set for a good task; with Z lost before `signal_v`'s `bne`
(`0x157f6`), the service returned with D7 still its table offset,
`0x64`, the error no check gives: `fatal_error("DUARTA isr signaling
SIG_SIQ")` on channel A, and the same in channel B's interrupts.  With
Z lost after the trap's `andi.w #0x700` (`0x14b54`), a service called
from an interrupt switched tasks as if from a task, leaving the
interrupted context's D6 and D7 to another: the host port's tasks
waited for good on events and exchange tokens their interrupts had
sent, and the port went silent part way through a command's echo
(`smartclock-cli diagnose`, at `:PTIMe:TFO` or wherever the tick fell).
Neither the DUART model nor the firmware was at fault.

The core reads the system byte from a copy of the CPU's state, which
the read spoils in place of the CPU's own, and the condition codes with
a probe of its own in a page where the board has nothing, `0xf00000`:
`move.w ccr,d0`, which QEMU computes rightly, D0 saved and put back,
run between the machine's slices and not counted in their cycles or
instructions.  The page is executable only, and a hook on it knows
when the probe runs, so the firmware's reading or writing it, or
running it but for the probe, halts the emulator as any invalid access
does.  A
hook cannot run the CPU, so an exception Unicorn raises stops the run
and is taken after, its frame holding the true codes; a watched
instruction's record, made in a hook, leaves the codes out.  The
core's tests (`cpu/unicorn.rs`) take a compare's codes through a read,
a trap and an interrupt, and fail with Unicorn's read; the boot test
`the_host_port_answers_diagnose_again_and_again` puts the host port,
on each core the build has, through `diagnose`'s queries ten times over, from a script that sends
each at the prompt, `scpi > ` or `E-nnn> `, after the last's answer.

## The panel's drawings

Each drawing is data, an entry of `drawings.js`'s `DRAWINGS`: its
face's size and its parts in the order drawn, the bezel, the heading,
a row of lamps or LEDs, the display, the keys.  Each is a face of its
real proportions, and what is on it sits where it does on the real
face:

- `58503a-option-001` and `58503a`: 212.6 mm by 88.5 mm, 2.40:1, the
  58503B's and 59551A's "Half-Rack Module" (097-58503-12 and -13,
  Specifications, "Dimensions"; 097-59551-02 gives none of the
  58503A's own), 2U by HP's half-rack width.  Option 001's layout,
  the dark window, the display, STATUS, MODE and the blank area to
  their right, and its text sizes are measured on the owner's unit,
  photographed 2026-11-06 with a ruler along the face's top edge: the
  ruler gives the dark window's width, the face's 88.5 mm its height,
  and the window's edges where they meet the face's top and bottom map
  the photograph to the face.  Each STATUS lamp sits in the column of
  the two key lamps below it.  Where it and an earlier photograph
  disagree, it wins (`drawings.js` says where).  The plain one's is the
  58503B's face as 097-59551-02 draws it ("58503A Front Panel at a
  Glance", page 1-4), with Option 001's heading and lamps.
- `58503b-option-001` and `58503b`: the 58503A's two, the same
  half-rack module, lamps and keys (097-58503-12, "58503B/Option 001
  Front-Panel Display/Keypad at a Glance"), the heading's model
  `58503B` (`renamed`); no photograph of one is here.
- `z3805a`: the same 2U half-rack face.  No manual is here; a
  photograph of the bench unit, corrected for the camera's tilt from
  where its sides converge, measures about 2.5:1, the 2U face within
  the measurement's error, and places the band, the LEDs and the
  window.
- `z3816a`: the Z3805A's face and band, unmeasured: no photograph or
  manual of a Z3816A is here.
- `z3801a`: 301.5 mm by 127 mm, 2.37:1, as 097-z3801-01 gives it
  (Specifications, "General Information").  127 mm is 3U, but 301.5 mm
  is wider than HP's half-rack module, and the manual's own drawing of
  the face (page 1-2) is 2.7:1, so the figure is odd; it is used as
  given.  The thumbscrews sit where that drawing has them, the LEDs and
  the handle where a photograph of a Nortel-badged unit has them.

The run knows the display's text, not its segments: 3704 sends the
panel's controller ASCII, so the font is the
controller's and not in the image.  The page maps a character to its
segments (`font.json`) as HP's display of the era does, the one the 53131A
drives a segment word at a time.  Its table of which segments light
for each character comes from
[hp_display](https://github.com/ragges/hp_display) (GPL-3.0; the table
is re-expressed, not copied), which decodes the 53131A's words:
`extras/codes-mapped.list` gives each character's word, and
`extras/charmap.py`'s `code2segs` gives each bit's segment.  A
photograph of the bench 58503A showing `UTC 01:34:43` agrees with it
for U, T, C, 0 and 4, and differs for two characters, where the
photograph wins: the 58503A's 1 is the two right-hand verticals, where
the 53131A's is the two center bars, and its 3 has the whole middle
bar, where the 53131A's has the right half.  So the 58503A's
controller has a font of its own, and the 53131A's glyphs elsewhere
are the best evidence, not proof.  The other HP sources say nothing
about glyphs:
[HP34401a-OLED-FW](https://github.com/openscopeproject/HP34401a-OLED-FW)'s
`protocol.md` shows the 34401A's panel also takes ASCII, and the
58503A manuals (097-58503-08, "VFD DSP TEST") say only that the
display test lights every segment and mark.

The slant (6 degrees), the stroke, and where the marks sit are measured
on the photograph.  The 53131A's three separator bits
(`hp_msg_parse.cpp`) light the point for `.`, the point and the
comma's tail for `,`, the point and the upper dot for `:`, and all
three for `;`, so a colon's lower dot is the point, as the photograph
shows.  A `.`, `,`, `:` or `;` is a mark on the character before it,
not a character of its own: `LONG: **:**:**.*` and `LAT: ***:**:**.*`
fill twelve characters only so, as `E 139:01:23.456` does, and the
display test steps `@;` across the display in lines of thirteen bytes
(`0x52b1c`), so `@`, every segment, with `;`, every mark, is one
character (*inferred*: the panel's controller is not traced).  A mark
after a character that has one, or at the start, takes a character of
its own.

*Inferred* glyphs, in no source: J, O and `@` among the characters
3704 shows, and the punctuation it never shows.  The 53131A's glyphs
the photograph does not show (every other letter, 2 and 5 to 9, `+`,
`-`, `*`, `?` and `m`) are the 53131A's, unconfirmed on the 58503A;
the display test's boot scroll is the photograph that would settle
them.  Unit tests hold the font to every character 3704's display lines
use, and to the 53131A's table but where the photograph differs.

## Not yet

PLAN.md, "Next", has the open work: the daemon and the CLI logging
from the emulated receiver, and the other images.  For the 58503A and
the Z3801A, their FPGA design: the 58503A and Z3801A bitstream differs
from the Z3805A's (outputs.md, "The FPGA bitstreams"), and the model
runs it as the Z3805A's design, which the 58503A's boot and self-tests
pass on but which nothing yet checks against that design's own
netlist.  The Z3817A's design is modeled by hand from its trace
("The interval counter"), checked against its testbench's references
and its differential fixture.  The Z3815A's, the Z3816A's and the
58503B's designs run on the LCA model alone ("The boards").  The bitstream itself is for a model
that executes it, the LCA model, the counter fast-forwarded, in place
of the hand-written one (`firmware/fpga-codegen.md`): then the board
file says `design = "lca"`, and the model runs the netlist of what the
firmware sends.  Its evaluator, `board/fpga/lca.rs`, runs each of
the six designs' netlists (`board/fpga/<image>.netlist`), agreeing
with each netlist's Icarus run exactly, and the board runs it ("The boards"),
fast-forwarded between events (`board/fpga/lca/fast_forward.rs`),
which agrees with it clocked edge by edge; it is opt-in until it is as
cheap as the hand model is in the browser, and the board does not
wire the Z3817A's design's pins for it yet.
