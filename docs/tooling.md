# Tooling

The tools that read the receivers' firmware, FPGAs and boards apart
and check what is written about them.  Each line names a tool and
links the document that describes it; the programs that watch a
receiver are in [`monitoring.md`](monitoring.md).

## Reading a receiver

`smartclock-cli`'s commands for the bench.  A daemon holds its port;
stop it before using one of these directly.

| Tool | What it is |
| ---- | ---------- |
| `smartclock-cli read-memory`, `read-flash`, `read-eeprom` | reads memory, the whole flash or the EEPROM through the pForth debug console ([`firmware/console.md`](firmware/console.md#reading-memory-through-it)) |
| `smartclock-cli flash` | checks a firmware image against the receiver and, with `--write`, programs it through the installer ([`firmware/restart.md`](firmware/restart.md#the-flasher)) |
| `smartclock-cli probe`, `commands` | which of the command table's queries a receiver answers, and the table as Markdown ([`commands.md`](commands.md)) |
| `smartclock-cli sweep` | tries each command in a file, for finding those no manual documents ([`scpi/undocumented.md`](scpi/undocumented.md)) |
| `smartclock-cli --capture` | records a whole exchange as a JSONL transcript ([`sessions/`](sessions/README.md)) |

## Reading an image

`smartclock-cli`'s commands that read a flash image and need no
receiver.

| Tool | What it is |
| ---- | ---------- |
| `smartclock-cli join-chips`, `split-chips` | joins the four flash chips' dumps into an image and splits one back ([`firmware/restart.md`](firmware/restart.md#the-installer)) |
| `smartclock-cli dump-scpi` | every command path an image's SCPI parser knows, and the trees side by side ([`scpi/README.md`](scpi/README.md)) |
| `smartclock-cli dump-pforth` | every word an image's pForth console knows ([`firmware/pforth/README.md`](firmware/pforth/README.md)) |
| `smartclock-cli dump-oncore` | the Oncore messages an image knows and what sends them ([`firmware/oncore/README.md`](firmware/oncore/README.md)) |

## Decompiling and the rewrite

| Tool | What it is |
| ---- | ---------- |
| `tools/ghidra/` | the Ghidra scripts that set up an image and export its C ([`firmware/README.md`](firmware/README.md#decompiling-with-ghidra)) |
| `third_party/firmware/` | the images, Ghidra's exports, the rewritten C and the analysis, with the `Makefile` that checks them ([`README.md`](../third_party/firmware/README.md)) |
| `tools/firmware/` | the firmware checks: function check, link check, layouts, SCPI trees, names, figures, loop traces ([`firmware/rewrite.md`](firmware/rewrite.md#the-verification); in CI, [`firmware/README.md`](firmware/README.md#the-checks-in-ci)) |
| the CPU backend | what the function check runs each image's code on ([`firmware/README.md`](firmware/README.md#the-cpu-backend)) |
| `make -C third_party/firmware help` | lists the firmware's check and generate targets ([`firmware/rewrite.md`](firmware/rewrite.md#reproducing)) |

## FPGAs

| Tool | What it is |
| ---- | ---------- |
| `tools/fpga/` | decodes the bitstreams, simulates and traces the designs, and compares them ([`firmware/fpga-comparison.md`](firmware/fpga-comparison.md#reproduce)) |
| `tools/fpga/xact/` | checks the decoder against XACT's own routed designs ([`README.md`](../tools/fpga/xact/README.md)) |
| `make fpga` | the FPGA explainer, [`fpga.html`](https://charlieh0tel.github.io/smartclock/fpga.html) ([`firmware/fpga-comparison.md`](firmware/fpga-comparison.md#reproduce)) |

## The board and the pages

| Tool | What it is |
| ---- | ---------- |
| `tools/board_atlas.py` | builds the [board atlas](https://charlieh0tel.github.io/smartclock/board-atlas.html) from `board-atlas.json` (`make board-atlas`; [Development](../README.md#development)) |
| `tools/scpi_models.py`, `tools/pforth_models.py` | build the matrix pages, the images' [SCPI trees](https://charlieh0tel.github.io/smartclock/scpi-models.html) and [pForth words](https://charlieh0tel.github.io/smartclock/pforth-models.html) side by side, from what `dump-scpi --models --json` and `tools/firmware/pforth_words.py --json` print, with `tools/pages_matrix.py`'s shared shell (`make scpi-models`, `make pforth-models`) |
| `tools/pages_index.py`, `tools/pages_copy.py` | build the published pages' landing page and copy the hand-written pages (`make pages`; [Development](../README.md#development)) |
| `tools/check-pages.sh`, `tools/check-pages-probe.js` | fails if a published page scrolls sideways (`make check-pages`; [Development](../README.md#development)) |
