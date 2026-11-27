# Board files

One board description per SmartClock model, for the emulator's
default core, mc683xx, whose runner takes a `board.toml`.
Each file says what is wired where and how the MC68331 is configured,
never what a peripheral does; the models stay in smartclock, behind
the core's peripheral registry.  The format is mc683xx-board's version
1 (mc683xx's
[`docs/board-files.md`](https://github.com/charlieh0tel/mc683xx/blob/main/docs/board-files.md))
where it reaches: `[chip] type =
"mc68331"`, the board's parts as `[[peripheral]]`.  What it could not
say when written is marked `# schema: draft`; mc683xx-board's loader
now takes every file as a description (mc683xx's `make
smartclock-boards`).  `smartclock-emu` runs `z3805a.toml`,
`58503a.toml`, `z3801a.toml`, `z3816a.toml` and `58503b.toml`, with
smartclock's peripheral types and mc683xx-peripherals' stock chips
(docs/emulator.md, "The mc683xx device layer", "The mc683xx backend").  `z3805a-10103.toml` is the Z3805A
1.01.03's, which its image's revision picks: `z3805a.toml` with the
Furuno GT-74 the image speaks to (`furuno-gt74`) in place of the
Oncore and no EEPROM image, a test holding it to `z3805a.toml` but for
its lines marked `1.01.03` (docs/emulator.md, "The boards").
`z3815a.toml` builds, its GPS engine `furuno-gt74` written, and runs
with `--model z3816a --board boards/z3815a.toml`, since the emulator
has no Z3815A model, as far as its unmodeled FPGA and its ADC with no
inputs let it: power-up, never locked (docs/emulator.md, "The
boards").  `z3816a.toml` is the Z3816A model's, its FPGA the
LCA model (`design = "lca"`), its supplies' monitors mc683xx-board's built-in `dc-source`s,
levels the board file gives (docs/emulator.md, "The boards").
`z3817a.toml` builds and its image runs, with `--board
boards/z3817a.toml` and another model's `--model`, since the emulator
has no Z3817A model, and `tests/boot_z3817a.rs` checks what it does
with its references (docs/emulator.md, "The boards").  Every file names its standard chips by
their stock types (the DUART, the EEPROM, the latch, the switch byte's
buffer, the ADC, the hex DAC, the EFC's AD569 and the 74HC164s that
feed it, on the 58503B, the Z3815A and the Z3816A clocked on SCK's
fall, below) and a part's marking by the `marking` key.  A test loads each file and builds
its stock chips from their parameters
(`every_board_files_stock_chips_take_their_parameters`), and another
checks every net's ends against the ports their devices' types declare,
skipping only the types not yet written
(`every_board_files_nets_end_at_ports_their_devices_have`).

| File | Model | Images |
| ---- | ----- | ------ |
| `z3801a.toml` | Z3801A, Z3801-68001 | `z3801a-3543.bin` |
| `z3805a.toml` | Z3805A, Z3805-68001 | `z3805a-3543b.bin` |
| `z3805a-10103.toml` | Z3805A, Z3805-68001: `z3805a.toml` with a Furuno GT-74 and no EEPROM image | `z3805a-1.01.03.bin` |
| `58503a.toml` | 58503A, 58503-60001 Rev C | `58503a-3704.bin`, `58503a-3633.bin` |
| `58503b.toml` | 58503B | `58503b-1.01.04.bin` |
| `z3815a.toml` | Z3815A | `z3815a-4010.bin` |
| `z3816a.toml` | Z3816A | `z3816a-4001.bin` |
| `z3817a.toml` | Z3817A | `z3817a-3752.bin` |

`z3805a.toml` is the fullest and the one the others refer to.  Where a
model's map, port setup or code is documented as the Z3805A's (the
Z3801A's map and loader, say), its file says so and takes the
Z3805A's wiring.  Elsewhere an unknown is `# open`, not a copy of
another model's.

## Confidence

Every fact ends in a comment giving how it is known and where from: a
document and section, or an address in the image.

| Mark | Meaning |
| ---- | ------- |
| `# read` | From the image's code: a register write, an instruction, a string. |
| `# board` | From a photo or the bench: a part's marking, a fitted connector, a dump. |
| `# inferred` | Follows from what is read or seen, not observed itself; the line says from what. |
| `# open` | Not known.  The line says what would settle it, often a `docs/hardware-investigations.md` item. |

## Synthetic EEPROMs

A board with no dump of its EEPROM starts from a synthetic image
instead, its `image` marked `# inferred`: `z3816a.toml`, from
`third_party/firmware/images/eeprom/z3816a-synthetic-eeprom.bin`,
`z3815a.toml`, from `z3815a-synthetic-eeprom.bin`, and
`z3805a-10103.toml`, from `z3805a-1.01.03-synthetic-eeprom.bin`.
`58503b.toml` starts from a 58503B's dump, `58503b-L00000-eeprom.bin`,
marked `# board`, its serial number and position replaced
(`tools/firmware/scrubbed_eeprom.py`; `docs/emulator.md`, "The
EEPROM"), and keeps its synthetic image,
`58503b-synthetic-eeprom.bin`, as the check the dump confirmed the
layout against.  `tools/firmware/synthetic_eeprom.py`
builds the synthetic ones from the images (`make synthetic-eeprom`; `make
check-boards` fails if a checked-in one differs), and its docstring
marks each record's values the same way:

| Record | Values | Mark |
| ------ | ------ | ---- |
| Parameter record, `0x400000` | the SCI's defaults, as `save_parameter_record` lays them out | `read` |
| Identification record, `0x400010` | the model, the tag and the suffix `A`, the image's own for a bad record | `read` |
| | the serial number, `0000A00000`, made up | `inferred` |
| NV register slot 0, `0x4000c0` | the settings at power-up as `nv_save` packs them, sequence 0 | `read` |
| Reset records, `0x400200`; event log, `0x400d80` | cleared, "Log cleared" first in each, as `log_erase` and `log_clear` leave them at an erased part's first power-up | `read` |
| Every other byte | erased: slot 1, lifetime, calibration | `inferred` |

The `read` records are also what the firmware itself writes on an
erased part's first power-up (`tests/eeprom.rs`).  `docs/emulator.md`,
"The EEPROM", gives the rest.

## Checking

`make check-boards` (`tools/firmware/check_boards.py`) checks every
chip-select attachment against the maps `tools/firmware/chip_selects.py`
reads from each image's reset code (`docs/firmware/chip-selects.md`):
a `read`, `write` or `read-write` attachment needs that R/W field, a
`lane` of `upper` or `lower` that BYTE field or both, an `iack` one an
interrupt acknowledge, and every select a map enables must be attached or
listed under `[[unconnected]]`.  The primary's map must agree; a boot
code's map is warned about, since the second family's boot code makes
CS7 a write select at `0x306000` where the primary makes it the
switch word's read select at `0x302000`.  The check does not cover
the QSPI codes, the decoders' address bits or the nets.  It also
rebuilds the synthetic EEPROMs ("Synthetic EEPROMs") and fails if a
checked-in one differs, and fails if the 58503B's dump is not good
records of serial number `L00000` and HP Santa Clara Division's
position (`scrubbed_eeprom.py --check`).

## The emulator today

On mc683xx, its default core, the emulator runs these files.  On
Unicorn, its development core, it wires its boards in code, the
Z3805A's so:

| Board file | `crates/smartclock-emu/src/board/` |
| ---------- | ---------------------------------- |
| `[[memory]] flash_low`, `flash_high` | `FLASH`, `FLASH_SIZE` (one 512 KiB read-only block) |
| `[[memory]] ram` | `RAM`, `RAM_SIZE` |
| `duart`, `irq = 2`, CS10 `iack` | `Duart`, `duart.rs` (`LEVEL`) |
| `cs5_decoder` and its four outputs | `CS5`, `latch::OFFSET`, `switch::OFFSET`, `fpga::PORT`, `EFC_STROBE` (the OCXO's load) |
| `eeprom` | `Eeprom` (`eeprom.rs`) |
| `[[unconnected]] CS4` | `CS4`, a page that reads 0 |
| QSPI codes 1, 2, 6, 7 | `SELECT_ADC`, `SELECT_HEX_DAC`, `SELECT_FPGA`, `SELECT_EFC_DAC` in `QspiDevices` |
| `fpga-pps` to `chip.PGP0`; `gps-pps` to `chip.PAI` | `Gpt::capture_1`, `Gpt::pulse_accumulator_rising` in `Board::advance`, `Board::gps_pps` |
| `fpga-init` to `chip.PCLK` | `Gpt::read(.., fpga.init())` |
| `fpga-done`, `fpga-program`, `fpga-readback` | `Board::fpga_lines`, `PORTF_FPGA_DONE`, `PORTE_FPGA_PROGRAM`, `PORTF_FPGA_READBACK_FIRST` (`sim.rs`) |
| `host-in`, `host-out`; `sci-in`, `sci-out`; `gps-*` | `Board::new(model, sci, gps, host, eeprom)`'s three `Line`s |
| the ADC's nets | the channel levels in `adc.rs` |
| `ten-megahertz`, `efc` | `Ocxo` (`ocxo.rs`), its EFC loaded by the strobe; its clock the FPGA's (`docs/emulator.md`, "The oscillator") |

Each model the emulator boots is a `Model` and a `Profile`
(`profile.rs`), named as its file is, `NAME.toml`, and picked by the
model name its image holds (docs/emulator.md, "The boards", "Adding a
board"); a revision with a board of its own, the Z3805A 1.01.03, is
the profile's `Variant`, its file `z3805a-10103.toml`, picked by the
revision string its image holds.  The 58503A's board (`profile.rs`, `front_panel.rs`, the `--model`
option) is a profile that picks the latch's lamps, the front panel on QSPI code 0 and the alarm
on PF5, and shares the rest with the Z3805A, the FPGA model included:
its `fpga`'s `design`, `58503a-z3801a`, the design of the 58503A's
own bitstream (`58503a-z3801a.bit`), runs as the Z3805A's design,
unchecked against the 58503A's.  The Z3817A's says `z3817a`, its own
design, hand-modeled from its trace (`docs/emulator.md`, "The interval
counter"); the LCA model runs its netlist against its fixture, but the
board's LCA wiring does not fit its pins (its internal 1 PPS on P78,
no interval, a second 1 PPS on P84), so its board runs the hand model.
The Z3815A's says
`design = "unmodeled"`: the part configures but runs no design
(`docs/emulator.md`, "The boards", "Not yet").  The Z3816A's and the
58503B's say `design = "lca"`, with `bitstream = "z3816a"` and
`bitstream = "58503b"`, their designs being neither hand model's.  `design` names a
design, one of `z3805a`, `58503a-z3801a`, `z3817a` and `unmodeled`, not a file:
the hand models read no bitstream, the firmware sending its own at
boot, and the emulator refuses any other name but `lca`, the LCA model,
which runs the netlist of the bitstream the firmware sends, picked by
its hash, and may assert which with `bitstream = "z3805a"`
(`docs/firmware/fpga-codegen.md`), as `z3816a.toml` and `58503b.toml`
do.  A test
(`each_board_file_names_its_images_design`, `fpga_part.rs`) checks that
each board's images carry the bitstream of the design it names,
`third_party/firmware/images/fpga/NAME.bit`, and an `unmodeled` board's
neither.
`58503a.toml` states those three as the latch's `Q0` to `Q2` and `Q6`,
`Q7` nets, `front_panel` on `{ qspi = 0 }`, and `alarm-out` from
`chip.PF5`.  The Z3801A's is a profile with the Z3805A's lamps and
engine and no panel or alarm; its file has no `sci` endpoint, as the
58503A's has none, so the emulator refuses `--sci` and `--sci-capture`
on either, and the
host port's far end is framed 7O1 on mc683xx (`Model::host_framing`;
docs/emulator.md, "The boards").  The 58503B's is a profile with the
58503A's lamps, panel and alarm, the UT Plus Oncore a shipped 58503B
reports (`--gps-engine b4121` for a VP; docs/firmware/gps.md, "The UT
Plus"), the
EFC gain of the oscillator its EEPROM's calibration block describes,
and no Unicorn board;
`58503b.toml` is the rest: its ACIA (the stock `mc6850`) on `gps-to-acia` and
`acia-to-gps`, the host on the SCI, and the nets as the 58503A's file
states them, each read from its image, the analog side and the
interpolator inferred from the 58503A's.

The 58503B's, the Z3815A's and the Z3816A's images write the EFC
chain, QSPI device 7, in SPI mode 2 and read it back in mode 3, as
every image does the hex DAC, and their EFC tests want the word read
back a bit early; the Z3801A's, Z3805A's and 58503A's write it in mode
3, read it in mode 2 and want it as it was (docs/emulator.md, "The
mc683xx device layer").  So those three files give the chain as
`type = "74x164"` with `clock = "falling"`: the stock chain with
SCK inverted to its clock, shifting on SCK's fall (*inferred*: an
inverter or another part is open).  The Z3815A's FPGA check reads the
bitstream back after a pulse on PF6, as the others' do, so its file
has the `fpga-readback` net too.  Both are read from the image and
were checked on a copy of the file with the Z3816A's Oncore in place
of the Furuno, before `furuno-gt74` was written, and are now on the
file itself, run as `--model z3816a --board boards/z3815a.toml`: the
QSPI's FPGA, EFC and ADC tests pass on it (docs/emulator.md, "The
mc683xx device layer").

## What differs between the models

Read from the images unless marked.  Addresses and sources are in the
files.

| | Z3801A | Z3805A | 58503A | Z3816A | Z3815A | 58503B |
| --- | --- | --- | --- | --- | --- | --- |
| Map | first family | the same | the same but CS4 | second family | the Z3816A's; boot sums first | the Z3816A's but CS1, CS8, CS10 |
| Flash | 2 banks x 2 parts, CSBOOT/CS1 read, CS6/CS7 write | the same | the same | 512 KiB on CSBOOT, CS6 write; Intel-style 16-bit | the same (inferred) | the same (inferred) |
| CS4 | 0x500000, unused | the same | not set up; PC1 high under 3633's boot | DUART IACK | the same | serial IACK, AVEC |
| CS10 | DUART IACK | the same | the same | 0x500000, 8-bit, unknown | the same | the ECLK pin |
| Latch 0x300000 | CS5 decode | the same | the same | CS1, 16-bit | CS1, 16-bit | CS1, 8-bit |
| Switch 0x302000 | CS5 decode, unread | the same | 3633's installer only | CS7, bit 8 picks G | CS7, bits 7, 12 to 14 | CS7, read at each EFC write |
| FPGA 0x304000 | CS5 decode | the same | the same | CS5 | CS5 | CS5 |
| EFC strobe | 0x306000 | the same | the same | none found: the DAC's latches tied transparent (inferred) | DUART OP0 pulsed after the word; the latches tied transparent (inferred) | PE7 |
| Serial part on CS8 | SCC68692 (inferred: U3 unread) | SCC68692 (board) | SCC68692 (board) | 68681-class | 68681-class | an ACIA on ECLK (inferred) |
| DSACK pins (PEPAR) | DSACK1 | DSACK1 | DSACK1 | DSACK1 | both | none |
| Host port | DUART B, 7O1 | DUART B | DUART B | SCI | open: SCI or DUART B | SCI (inferred) |
| SCI | off | the ES message | none | host | open | host |
| GPS engine | Oncore, 6 channels | Oncore (3543B); Furuno GT-74, NMEA (1.01.03) | Oncore, up to 8 | Oncore, up to 8 | Furuno GT-74, NMEA | Oncore, 8 channels: a UT Plus, told from a VP by its `@@Cj` model; a VP asked `@@Ta` |
| FPGA bitstream | `58503a-z3801a.bit` | `z3805a.bit` | `58503a-z3801a.bit` | `z3816a.bit` | `z3815a.bit` | `58503b.bit` |
| `fpga`'s `design` | `58503a-z3801a` | `z3805a` | `58503a-z3801a` | `lca` | `lca`, `bitstream = "z3815a"` | `lca`, `bitstream = "58503b"` |
| Heartbeat | DUART OP2 | OP2 | OP2 | OP6 | OP6 | latch bit 3 |
| Outer oven, PGP5 | power-up machine | the same | 3633 yes, 3704 doven only | power-up machine | port GP all inputs | doven only |
| Front panel, QSPI 0 | none | none | Option 001 | none | its own device 0 | Option 001, the 58503A's protocol |
| EFC chain, QSPI 7 | written in mode 3, read in mode 2: `74x164` | the same | the same | written in mode 2, read in mode 3: `74x164`, `clock = "falling"` | the Z3816A's | the Z3816A's |
| Alarm output | open | open (a rear TTL output) | PF5 and FPGA reg 3 bit 2 (3704) | open | open | PF5 and FPGA field 7 |
| ADC channels | 0 temperature, 1 double oven, 2 +15 V, 3 current, 4 5 V, 5 oven, 6 -15 V | the same | the Z3805A's inputs, numbered otherwise (`adc_channel`, 58503a-3704.md) | 0 12B, 1 5V, 2 12C, 3 to 5 -12B, -12C, -12D, 6 current, 7 antenna current; no temperature | 0 12A, 1 5V, 2 12C, 3 to 5 -12A, -12C, -12E, 6 oscillator current, 7 antenna current, steady at the table's nominals (inferred) | the 58503A's (inferred) |
| Lamps on the latch | 0-2, 4, 5 (inferred) | 0-2, 4, 5 | 0-2; 6, 7 the panel | open | open | 0-2; 3 the heartbeat; 6, 7 the panel; 4 open |
| OCXO link | none | none | none | none | the puck, bit-serial on PE2, PE5 and PE7: `clocked-efc-monitor` | none |
| EEPROM image | bench dump | bench dump (3543B); synthetic (inferred) (1.01.03) | bench dump | synthetic (inferred) | synthetic (inferred) | synthetic (inferred) |
| Boards seen | photo, bench (`docs/bench.md`) | photo, bench | photo, bench | none | none | none |

## Schema gaps

What the drafts need that mc683xx's `docs/board-files.md` does not say, each a
`# schema: draft` in the files:

- `[board]`: the images a board takes (the 58503A takes two) and its
  options (`front-panel`).
- `image_offset` on a memory: a bank's half of an image.  And two
  8-bit parts behind one select, one a byte lane, and flash command
  cycles, which a `[[memory]]` cannot say.
- `[[unconnected]]`: a select the firmware programs with nothing known
  on it (the Z3805A's CS4, the Z3816A's CS10).
- `dsack` on a peripheral: which DSACK pin an externally terminated part
  drives, which sets the cycle's port size.
- A `wired-and` net with several drivers: DONE, which the FPGA and
  port F both drive.
- A parallel `bus` net: the interpolator's byte into the FPGA.
- `chip.PE7`'s use as a strobe.
- A decoder's unknown third input (ADDR15 or tied).
