# Emulation

The receivers' own firmware, booted on a modeled board: `smartclock-emu`
on the command line, and the 58503A in a browser.

- [The 58503A in a browser](https://charlieh0tel.github.io/smartclock/emulator.html):
  its front panel, console and simulation controls, nothing to install.
- [`emulator.md`](emulator.md): building and running it, its options
  and ports, each model and its evidence.
- [`../boards/`](../boards/): the board files, each fact marked with
  how it is known.
- [`firmware/fpga-codegen.md`](firmware/fpga-codegen.md): the FPGA
  bitstream's netlist executed, for the 58503B, Z3815A and Z3816A.

Quick start: `make emulate` boots the Z3805A 3543B on the terminal;
`make emulate HOST=127.0.0.1:5025` serves it to the daemon and the CLI
over TCP.

Its checks: `make emulator`, clippy and every test, the boots of each
model among them; `make emulator-slow`, the tests too slow
unoptimized; `make test-web`, the panel page; `make test-emulator-web`,
the browser build in Node and Chromium.  CI runs them
(`.github/workflows/firmware.yml`, `web.yml`); the lock runs, ignored
as long, run by hand (`emulator.md`, "The boards").
