//! `read-registers`: the DUART's and the SIM's registers that a read
//! through the pForth console can show without side effects, for the
//! bench questions in `docs/hardware-investigations.md`, items 21 and
//! 26 ("Register reads through the console").
//!
//! A dry run by default: it prints what it would read and sends
//! nothing.  With `--read` it checks the unit's EEPROM and flash
//! against `--model` and `--serial`, reads, and returns the port to
//! SCPI with `halt` alone, never through the installer's `execute`.
//! Every fetch is a `c@`, `w@` or `@`; nothing is stored or executed.  Each address is checked against
//! [`refusal`] before anything is sent, so a register whose read
//! changes the device can never be asked for.

use std::fmt::Write as _;

use anyhow::Context as _;
use anyhow::Result;
use clap::ValueEnum;
use smartclock::console;
use smartclock::console::ConsoleError;
use smartclock::console::Fetch;
use smartclock::console::Progress;
use smartclock::console::WayBack;
use smartclock::console::Width;
use smartclock::session::Config;
use smartclock::transport::Transport;

/// What `read-registers` is asked to do.
#[derive(Debug, clap::Args)]
pub(crate) struct ReadRegistersArgs {
    /// The receiver and its firmware, which fix the addresses and what
    /// the reset code writes: `z3805a` for 3543B, `58503a` for 3704.
    #[arg(long, value_enum)]
    model: Model,
    /// Read: enter the console, check the unit against `--model` and
    /// `--serial`, read, and return to SCPI.  Without it, a dry run that
    /// prints what would be read and opens nothing.  Stop the port's
    /// daemon first.
    #[arg(long, requires = "serial")]
    read: bool,
    /// The serial number the unit's EEPROM must hold; required with
    /// `--read`.
    #[arg(long)]
    serial: Option<String>,
    /// Also read the SIM's supervisor-only registers: the chip
    /// selects, PICR, PITR, PEPAR, PFPAR, SIMCR and SYPCR.  The console
    /// runs in user mode, where they are not readable: a bench Z3801A
    /// read them as zero.  Read last, after everything else.
    #[arg(long)]
    supervisor_registers: bool,
}

impl ReadRegistersArgs {
    /// Whether the run reads, rather than printing what it would read.
    pub(crate) const fn reads(&self) -> bool {
        self.read
    }
}

/// The images the reads know.
#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum)]
enum Model {
    /// The Z3805A, 3543B.
    Z3805a,
    /// The 58503A, 3704.
    #[value(name = "58503a")]
    A58503,
}

/// What the run needs to know about one image.
#[derive(Debug)]
struct Profile {
    /// The model, as the EEPROM's identity record holds it.
    model: &'static str,
    /// The primary's revision string.
    revision: &'static str,
    /// Where the revision string is in the flash.
    revision_at: u32,
    /// `duart_shadow`, the code's copy of the DUART's write-only
    /// registers (`docs/firmware/structures.md`, "The DUART's shadows").
    duart_shadow: u32,
    /// CSBAR4 and CSOR4 as the reset code leaves them.
    cs4: (u32, u32),
}

impl Model {
    /// This image's profile.
    const fn profile(self) -> &'static Profile {
        match self {
            Self::Z3805a => &Profile {
                model: "Z3805A",
                revision: "3543B",
                revision_at: 0x12eee,
                duart_shadow: 0x10_2ed0,
                cs4: (0x5000, 0x7830),
            },
            Self::A58503 => &Profile {
                model: "58503A",
                revision: "3704",
                revision_at: 0x1309c,
                duart_shadow: 0x10_31f4,
                cs4: (0, 0),
            },
        }
    }
}

/// The EEPROM's identity record: the model, NUL-padded, and the
/// serial number (the units' dumps under
/// `third_party/firmware/images/eeprom/`).
const EEPROM_MODEL: (u32, usize) = (0x40_0012, 6);
const EEPROM_SERIAL: (u32, usize) = (0x40_0024, 10);

/// The DUART on chip select 8: register n at `DUART + 2n + 1`, its
/// lower byte lane (`docs/firmware/README.md`; MC68681 Table 4-1).
const DUART: u32 = 0x20_0000;
/// Chip select 8's window.
const DUART_END: u32 = 0x20_0800;
/// The SIM's register block (MC68331UM Table D-3).
const SIM: u32 = 0xff_fa00;
const SIM_END: u32 = 0xff_fa80;

/// A DUART register's read address.
const fn duart(register: u32) -> u32 {
    DUART + 2 * register + 1
}

/// Which reads a run makes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Group {
    /// Readable from the console's user mode: the DUART, the code's
    /// RAM and the SIM's "S/U" ports, which SIMCR's SUPV 0 opens.
    User,
    /// The SIM's "S" registers, opt-in (`--supervisor-registers`).
    Supervisor,
}

/// One register to read and how to read its value.
#[derive(Debug)]
struct Register {
    /// The hardware-investigations items it bears on.
    items: &'static str,
    /// Its name, as the data sheet has it.
    name: &'static str,
    /// Where.
    address: u32,
    /// How wide.
    width: Width,
    /// Which run reads it.
    group: Group,
    /// What the reset code writes or leaves there, if fixed, and
    /// where that is said.
    image: Option<(u32, &'static str)>,
    /// Its fields, named.
    decode: fn(u32) -> String,
}

/// A register in the user group.
const fn user(
    items: &'static str,
    name: &'static str,
    address: u32,
    image: Option<(u32, &'static str)>,
    decode: fn(u32) -> String,
) -> Register {
    Register {
        items,
        name,
        address,
        width: Width::Byte,
        group: Group::User,
        image,
        decode,
    }
}

/// A register in the supervisor group.
const fn supervisor(
    items: &'static str,
    name: &'static str,
    address: u32,
    width: Width,
    image: (u32, &'static str),
    decode: fn(u32) -> String,
) -> Register {
    Register {
        items,
        name,
        address,
        width,
        group: Group::Supervisor,
        image: Some(image),
        decode,
    }
}

/// Chip selects in register order, with CSBAR and CSOR as the
/// Z3805A's and 58503A's reset code writes them
/// (`docs/firmware/chip-selects.md`), CS4 aside.
const SELECTS: [(&str, u32, u32); 12] = [
    ("BT", 0x0005, 0x6870),
    ("0", 0x1003, 0x6830),
    ("1", 0x0405, 0x6870),
    ("2", 0x1003, 0x5030),
    ("3", 0x1003, 0x3030),
    ("4", 0, 0),
    ("5", 0x3003, 0x7b30),
    ("6", 0x0005, 0x7070),
    ("7", 0x0405, 0x7070),
    ("8", 0x2000, 0x3bf0),
    ("9", 0x4001, 0x78f0),
    ("10", 0xfff8, 0x2bc4),
];
/// CSBARBT's address; each select's pair follows the last.
const CSBARBT: u32 = 0xff_fa48;

/// The registers a run reads, in order: the user group first, so a
/// supervisor-only read that restarts the unit comes after them.
fn registers(profile: &Profile, with_supervisor: bool) -> Vec<Register> {
    const RESET: &str = "MC68331UM D.3.22, reset; no image writes it";
    let mut all = vec![
        user(
            "21",
            "DUART IVR",
            duart(0xc),
            Some((
                0x43,
                "boot_duart_early_init 0x500; duart_init 0x1da1a, 3704 0x1d86c",
            )),
            interrupt_vector,
        ),
        user("21", "DUART ISR", duart(0x5), None, duart_interrupts),
        user(
            "21",
            "duart_shadow IMR",
            profile.duart_shadow + 4,
            None,
            duart_interrupts,
        ),
        user("21", "PORTE0", SIM + 0x11, None, port_e),
        user(
            "21",
            "DDRE",
            SIM + 0x15,
            Some((0x40, "0x145ca, 3704 0x14624")),
            outputs,
        ),
        user("21", "PORTF0", SIM + 0x19, None, port_f),
        user(
            "21",
            "DDRF",
            SIM + 0x1d,
            Some((0xf7, "0x145e2, 3704 0x1463c")),
            outputs,
        ),
        user("26", "PORTC", SIM + 0x41, Some((0x7f, RESET)), port_c),
    ];
    if !with_supervisor {
        return all;
    }
    const PRIMARY: &str = "the reset code, docs/firmware/chip-selects.md";
    all.extend([
        supervisor(
            "21",
            "SIMCR",
            SIM,
            Width::Word,
            (0x014a, "0x145a2, 3704 0x145fc"),
            simcr,
        ),
        supervisor(
            "21",
            "PEPAR",
            SIM + 0x17,
            Width::Byte,
            (0x32, "0x145ba, 3704 0x14614"),
            pepar,
        ),
        supervisor(
            "21",
            "PFPAR",
            SIM + 0x1f,
            Width::Byte,
            (0x04, "0x145d2, 3704 0x1462c"),
            pfpar,
        ),
        supervisor(
            "21",
            "SYPCR",
            SIM + 0x21,
            Width::Byte,
            (0xcc, "0x144ca, 3704 0x14534"),
            sypcr,
        ),
        supervisor(
            "21",
            "PICR",
            SIM + 0x22,
            Width::Word,
            (0x0542, "0x12016, 3704 0x1215c: 0x0542 while the PIT runs"),
            picr,
        ),
        supervisor(
            "21",
            "PITR",
            SIM + 0x24,
            Width::Word,
            (0x0008, "0x145b2, 3704 0x1460c"),
            pitr,
        ),
        supervisor(
            "21, 26",
            "CSPAR0",
            SIM + 0x44,
            Width::Word,
            (0x2bff, PRIMARY),
            cspar0,
        ),
        supervisor(
            "21, 26",
            "CSPAR1",
            SIM + 0x46,
            Width::Word,
            (0x03af, PRIMARY),
            cspar1,
        ),
    ]);
    for (n, &(select, bar, option)) in SELECTS.iter().enumerate() {
        let (bar, option) = if select == "4" {
            profile.cs4
        } else {
            (bar, option)
        };
        let at = CSBARBT + 4 * n as u32;
        let items = match select {
            "4" => "26",
            "8" | "10" => "21",
            _ => "map",
        };
        all.push(supervisor(
            items,
            CS_BASE_NAMES[n],
            at,
            Width::Word,
            (bar, PRIMARY),
            chip_select_base,
        ));
        all.push(supervisor(
            items,
            CS_OPTION_NAMES[n],
            at + 2,
            Width::Word,
            (option, PRIMARY),
            chip_select_option,
        ));
    }
    all
}

/// The base and option registers' names, in [`SELECTS`]' order.
const CS_BASE_NAMES: [&str; 12] = [
    "CSBARBT", "CSBAR0", "CSBAR1", "CSBAR2", "CSBAR3", "CSBAR4", "CSBAR5", "CSBAR6", "CSBAR7",
    "CSBAR8", "CSBAR9", "CSBAR10",
];
const CS_OPTION_NAMES: [&str; 12] = [
    "CSORBT", "CSOR0", "CSOR1", "CSOR2", "CSOR3", "CSOR4", "CSOR5", "CSOR6", "CSOR7", "CSOR8",
    "CSOR9", "CSOR10",
];

/// Why a fetch must not be made, or `None` if it may: the DUART's
/// registers whose read changes it, the selects whose decode a read
/// strobes, and anything no select maps, which a bus error answers.
fn refusal(address: u32, width: Width) -> Option<&'static str> {
    let in_range = |from: u32, to: u32| (from..to).contains(&address);
    if in_range(DUART, DUART_END) {
        if width != Width::Byte {
            return Some("the DUART is read a byte at a time");
        }
        return match (address - DUART) % 0x20 {
            0x0b | 0x19 | 0x03 | 0x13 | 0x1b => None,
            0x01 | 0x11 => Some("MR1/MR2: a read moves the mode register pointer (MC68681 4.2.1)"),
            0x05 | 0x15 => {
                Some("do not access: factory test, a read can change registers (MC68681 Table 4-1)")
            }
            0x07 | 0x17 => {
                Some("RHR: a read takes a character from the receive FIFO (MC68681 Table 4-1)")
            }
            0x09 => Some("IPCR: a read clears its change bits and ISR[7] (SCC68692, IPCR)"),
            0x0d | 0x0f => {
                Some("CUR/CLR: valid only with the counter stopped (MC68681 3.5, 4.2.17)")
            }
            0x1d => Some("start counter command: a read restarts the counter/timer (MC68681 3.5)"),
            0x1f => Some(
                "stop counter command: a read stops the counter and clears ISR[3] (MC68681 3.5)",
            ),
            _ => Some("an even address: chip select 8 is the lower byte lane only"),
        };
    }
    if in_range(0x30_0000, 0x31_0000) {
        return Some(
            "chip select 5: the lamp latch, S1, the FPGA and the EFC strobe; a read may strobe the decode",
        );
    }
    if in_range(0x50_0000, 0x50_0800) {
        return Some(
            "chip select 4: no known part; on the 58503A no select, so a bus error and a restart",
        );
    }
    if in_range(SIM, SIM_END) {
        return match address - SIM {
            0x26 | 0x27 => Some("SWSR: write-only, the watchdog's service"),
            0x30..=0x3b => Some("the factory test registers"),
            _ => None,
        };
    }
    let mapped =
        in_range(0, 0x8_0000) || in_range(0x10_0000, 0x11_0000) || in_range(0x40_0000, 0x40_2000);
    (!mapped).then_some("no chip select maps it: a bus error, which restarts the unit")
}

/// The DUART's addresses the run never reads, each with why, for the
/// dry run's listing.
fn refused_duart() -> Vec<(u32, &'static str)> {
    (0..0x10)
        .map(duart)
        .filter_map(|address| refusal(address, Width::Byte).map(|why| (address, why)))
        .collect()
}

/// The names of the bits set in `value`, `names` from bit 7 down.
fn bits(value: u32, names: [&str; 8]) -> String {
    let set: Vec<&str> = names
        .iter()
        .enumerate()
        .filter(|(n, _)| value & (0x80 >> n) != 0)
        .map(|(_, name)| *name)
        .collect();
    if set.is_empty() {
        "none".to_owned()
    } else {
        set.join(" ")
    }
}

fn interrupt_vector(value: u32) -> String {
    format!(
        "vector {value}, its entry at VBR + {:#x}; the reset value is 0x0f",
        value * 4
    )
}

fn duart_interrupts(value: u32) -> String {
    bits(
        value,
        [
            "input-change",
            "B-break",
            "RxRDYB",
            "TxRDYB",
            "counter",
            "A-break",
            "RxRDYA",
            "TxRDYA",
        ],
    )
}

/// The level of bit `n`.
const fn bit(value: u32, n: u32) -> u32 {
    (value >> n) & 1
}

fn port_e(value: u32) -> String {
    format!(
        "PE0 {}: DSACK0's pin, a port input under PEPAR 0x32, so its level; PE6 {}, the FPGA's PROGRAM",
        bit(value, 0),
        bit(value, 6)
    )
}

fn port_f(value: u32) -> String {
    format!(
        "PF3 {}: the FPGA's DONE, an input, so its pin; the rest read their latch, \
         PF2 as IRQ2 included (MC68331UM 4.9.3)",
        bit(value, 3)
    )
}

fn port_c(value: u32) -> String {
    format!(
        "PC1 {}: CS4's pin when CSPAR0 makes it a discrete output; no DDRC, port C is output only",
        bit(value, 1)
    )
}

fn outputs(value: u32) -> String {
    format!(
        "outputs: {}",
        bits(value, ["7", "6", "5", "4", "3", "2", "1", "0"])
    )
}

fn simcr(value: u32) -> String {
    format!(
        "SUPV {}, MM {}, SHEN {}, IARB {}",
        bit(value, 7),
        bit(value, 6),
        (value >> 8) & 3,
        value & 0xf
    )
}

fn pepar(value: u32) -> String {
    format!(
        "bus pins: {}",
        bits(
            value,
            [
                "SIZ1", "SIZ0", "AS", "DS", "RMC", "AVEC", "DSACK1", "DSACK0"
            ]
        )
    )
}

fn pfpar(value: u32) -> String {
    format!(
        "IRQ pins: {}",
        bits(
            value,
            [
                "IRQ7", "IRQ6", "IRQ5", "IRQ4", "IRQ3", "IRQ2", "IRQ1", "MODCLK"
            ]
        )
    )
}

fn sypcr(value: u32) -> String {
    format!(
        "SWE {}, SWP {}, SWT {}, HME {}, BME {}, BMT {}",
        bit(value, 7),
        bit(value, 6),
        (value >> 4) & 3,
        bit(value, 3),
        bit(value, 2),
        value & 3
    )
}

fn picr(value: u32) -> String {
    format!(
        "PIRQL {} (the PIT's level), PIV {:#04x}",
        (value >> 8) & 7,
        value & 0xff
    )
}

fn pitr(value: u32) -> String {
    format!("PTP {}, PITM {}", bit(value, 8), value & 0xff)
}

/// A CSPAR field's meaning (MC68331UM Table D-10).
const fn pin_field(field: u32) -> &'static str {
    match field {
        0 => "discrete",
        1 => "alternate",
        2 => "8-bit",
        _ => "16-bit",
    }
}

/// CSPAR fields, two bits each from bit 0 up, named.
fn pin_fields(value: u32, names: &[&str]) -> String {
    names
        .iter()
        .enumerate()
        .map(|(n, name)| format!("{name} {}", pin_field((value >> (2 * n)) & 3)))
        .collect::<Vec<_>>()
        .join(", ")
}

fn cspar0(value: u32) -> String {
    pin_fields(value, &["CSBOOT", "CS0", "CS1", "CS2", "CS3", "CS4", "CS5"])
}

fn cspar1(value: u32) -> String {
    pin_fields(value, &["CS6", "CS7", "CS8", "CS9", "CS10"])
}

fn chip_select_base(value: u32) -> String {
    const SIZES: [&str; 8] = [
        "2 KiB", "8 KiB", "16 KiB", "64 KiB", "128 KiB", "256 KiB", "512 KiB", "1 MiB",
    ];
    format!(
        "base {:#08x}, {}",
        (value & !7) << 8,
        SIZES[(value & 7) as usize]
    )
}

fn chip_select_option(value: u32) -> String {
    const BYTE: [&str; 4] = ["disabled", "lower", "upper", "both"];
    const READ_WRITE: [&str; 4] = ["reserved", "read", "write", "both"];
    const SPACE: [&str; 4] = ["CPU", "user", "supervisor", "S/U"];
    let dsack = match (value >> 6) & 0xf {
        0xe => "fast termination".to_owned(),
        0xf => "external DSACK".to_owned(),
        waits => format!("internal DSACK, {waits} wait"),
    };
    format!(
        "{}, BYTE {}, R/W {}, {}, {dsack}, SPACE {}, IPL {}, AVEC {}",
        if bit(value, 15) == 0 { "async" } else { "sync" },
        BYTE[((value >> 13) & 3) as usize],
        READ_WRITE[((value >> 11) & 3) as usize],
        if bit(value, 10) == 0 { "AS" } else { "DS" },
        SPACE[((value >> 4) & 3) as usize],
        (value >> 1) & 7,
        bit(value, 0)
    )
}

/// What a run reads, after checking every address.
fn plan(args: &ReadRegistersArgs) -> Result<(Vec<Register>, Vec<Fetch>)> {
    let registers = registers(args.model.profile(), args.supervisor_registers);
    let fetches = registers
        .iter()
        .map(|register| {
            if let Some(why) = refusal(register.address, register.width) {
                anyhow::bail!(
                    "{} at {:#x} is refused: {why}; nothing was sent",
                    register.name,
                    register.address
                );
            }
            Ok(Fetch::new(register.address, register.width)?)
        })
        .collect::<Result<Vec<_>>>()?;
    Ok((registers, fetches))
}

/// The identity reads: the EEPROM's model and serial, the flash's
/// revision string, a byte each.
fn identity_fetches(profile: &Profile) -> Result<Vec<Fetch>> {
    [
        EEPROM_MODEL,
        EEPROM_SERIAL,
        (profile.revision_at, profile.revision.len()),
    ]
    .iter()
    .flat_map(|&(at, n)| at..at + n as u32)
    .map(|address| Ok(Fetch::new(address, Width::Byte)?))
    .collect()
}

/// The width's console word and its digits.
const fn digits(width: Width) -> usize {
    2 * width.bytes() as usize
}

/// The table: one row per register, with its value when read.
fn table(registers: &[Register], values: Option<&[u32]>) -> String {
    let mut out = String::from(
        "| Item | Register | Address | Read with | Image | Value | Fields |\n\
         | ---- | -------- | ------- | --------- | ----- | ----- | ------ |\n",
    );
    for (n, register) in registers.iter().enumerate() {
        let width = digits(register.width);
        let image = register.image.map_or("varies".to_owned(), |(value, _)| {
            format!("{value:0width$x}")
        });
        let (value, fields) = match values.and_then(|values| values.get(n)) {
            None => ("-".to_owned(), String::new()),
            Some(&value) => {
                let note = match register.image {
                    Some((expected, _)) if expected == value => "; as the image sets it",
                    _ if register.group == Group::Supervisor && value == 0 => {
                        "; zero: supervisor-only, read from user mode"
                    }
                    Some(_) => "; DIFFERS from the image",
                    None => "",
                };
                (
                    format!("{value:0width$x}"),
                    format!("{}{note}", (register.decode)(value)),
                )
            }
        };
        let _ = writeln!(
            out,
            "| {} | {} | {:#08x} | `{:X} {} u.` | {image} | {value} | {fields} |",
            register.items,
            register.name,
            register.address,
            register.address,
            register.width.word()
        );
    }
    out
}

/// The sources of the image column.
fn sources(registers: &[Register]) -> String {
    let mut out = String::new();
    for register in registers {
        if let Some((_, source)) = register.image {
            let _ = writeln!(out, "- {}: {source}", register.name);
        }
    }
    out
}

/// Print what a run would read and send nothing.
pub(crate) fn dry_run(args: &ReadRegistersArgs) -> Result<()> {
    let profile = args.model.profile();
    let (registers, _) = plan(args)?;
    println!(
        "Dry run: nothing is opened or sent.  With --read and --serial, on {} {}:\n",
        profile.model, profile.revision
    );
    println!(
        "1. An empty line; if no `p4th` prompt answers, :SYSTem:LANGuage \"PFORTH\".  Then `hex`."
    );
    println!(
        "2. The identity, a `c@ u.` a byte: the EEPROM's model at {:#x} and serial at {:#x}, \
         and {:?} at {:#x} in the flash.  A mismatch stops the run before any register is read.",
        EEPROM_MODEL.0, EEPROM_SERIAL.0, profile.revision, profile.revision_at
    );
    println!("3. The registers, in this order:\n");
    print!("{}", table(&registers, None));
    println!("\nImage values from:\n");
    print!("{}", sources(&registers));
    if !args.supervisor_registers {
        println!(
            "\nThe SIM's supervisor-only registers (CSPARx, CSBARx, CSORx, PICR, PITR, PEPAR, \
             PFPAR, SIMCR, SYPCR) are left out; --supervisor-registers adds them, last."
        );
    }
    println!(
        "\n4. `decimal` and `halt`, checked with :SYSTem:LANGuage?.  If `halt` fails the port \
         stays at the console: `halt` by hand, or power cycle.\n"
    );
    println!("Never read, and refused if asked for:\n");
    for (address, why) in refused_duart() {
        println!("- {address:#08x}: {why}");
    }
    for (address, width) in [(0x30_0000, Width::Byte), (0x50_0000, Width::Byte)] {
        if let Some(why) = refusal(address, width) {
            println!("- {address:#08x}: {why}");
        }
    }
    Ok(())
}

/// The text of bytes read one a fetch, up to the first NUL.
fn text(bytes: &[u32]) -> String {
    bytes
        .iter()
        .map(|&byte| byte as u8)
        .take_while(|&byte| byte != 0)
        .map(char::from)
        .collect()
}

/// Why the unit is not the one asked for, or `None` if it is.
fn mismatch(profile: &Profile, serial: &str, identity: &[u32]) -> Option<String> {
    let (model, rest) = identity.split_at(EEPROM_MODEL.1);
    let (found_serial, revision) = rest.split_at(EEPROM_SERIAL.1);
    let (model, found_serial, revision) = (text(model), text(found_serial), text(revision));
    (model != profile.model || found_serial != serial || revision != profile.revision).then(|| {
        format!(
            "the unit's EEPROM says {model} {found_serial} and its flash {revision:?}, \
             not {} {serial} {:?}; no register was read",
            profile.model, profile.revision
        )
    })
}

/// The value read for the register named `name`.
fn value_of(registers: &[Register], values: &[u32], name: &str) -> Option<u32> {
    let n = registers
        .iter()
        .position(|register| register.name == name)?;
    values.get(n).copied()
}

/// What ISR and the IMR's shadow say together about the DUART's /INTR
/// at the moment of the reads: a source both pending and enabled drives
/// it low.  One look, so a low /INTR is normally its handler's to clear
/// a moment later.
fn interrupt_line(registers: &[Register], values: &[u32]) -> String {
    let (Some(isr), Some(imr)) = (
        value_of(registers, values, "DUART ISR"),
        value_of(registers, values, "duart_shadow IMR"),
    ) else {
        return String::new();
    };
    let pending = isr & imr;
    format!(
        "\nISR AND IMR {pending:02x} ({}): /INTR {} at the reads.\n",
        duart_interrupts(pending),
        if pending == 0 { "released" } else { "asserted" }
    )
}

/// Print a console run's progress.
fn report(progress: Progress) {
    if let Progress::Entering = progress {
        eprintln!("not at the pForth prompt; sending :SYSTem:LANGuage \"PFORTH\"");
    }
}

/// Read the registers on the port and return it to SCPI.  The table is
/// printed as soon as the reads are in, before the way back.
pub(crate) fn run<T: Transport>(port: T, args: &ReadRegistersArgs, config: Config) -> Result<()> {
    let profile = args.model.profile();
    let serial = args
        .serial
        .as_deref()
        .context("--read needs --serial: the unit's serial number")?;
    let (registers, fetches) = plan(args)?;
    let identity = identity_fetches(profile)?;
    let split = fetches
        .iter()
        .zip(&registers)
        .position(|(_, register)| register.group == Group::Supervisor)
        .unwrap_or(fetches.len());
    let (user_fetches, supervisor_fetches) = fetches.split_at(split);
    // `halt` alone: this run never executes anything, the installer
    // exit included.
    let (outcome, back) = console::and_return(port, config, WayBack::Halt, |port| {
        let found = console::fetch_each(&mut *port, &identity, report)?;
        if let Some(why) = mismatch(profile, serial, &found) {
            return Ok(Err(why));
        }
        let mut values = console::fetch_each(&mut *port, user_fetches, report)?;
        // Kept apart, so the user group's values are printed whatever
        // the supervisor-only reads do.
        let supervisor = if supervisor_fetches.is_empty() {
            None
        } else {
            Some(console::fetch_each(&mut *port, supervisor_fetches, report))
        };
        let failed = match supervisor {
            Some(Ok(more)) => {
                values.extend(more);
                None
            }
            Some(Err(e)) => Some(e),
            None => None,
        };
        println!(
            "{} {serial}, {}: the registers as read through the console.\n",
            profile.model, profile.revision
        );
        print!("{}", table(&registers, Some(&values)));
        print!("{}", interrupt_line(&registers, &values));
        if let Some(e) = &failed {
            println!(
                "\nThe supervisor-only reads failed: {e}.  If the unit restarted (scpi > \
                 prompt, a Re-boot in its log), they are answered by a bus error."
            );
        }
        Ok(Ok(failed.map(|e: ConsoleError| e.to_string())))
    })?;
    eprintln!("back at SCPI: {back}");
    match outcome {
        Err(why) => anyhow::bail!(why),
        Ok(Some(failed)) => anyhow::bail!("supervisor-only reads failed: {failed}"),
        Ok(None) => Ok(()),
    }
}

#[cfg(test)]
mod tests {
    use smartclock::console::Width;

    use super::Group;
    use super::Model;
    use super::duart;
    use super::mismatch;
    use super::refusal;
    use super::registers;

    #[test]
    fn every_register_read_is_allowed_and_on_its_own_lane() {
        for model in [Model::Z3805a, Model::A58503] {
            for register in registers(model.profile(), true) {
                assert_eq!(
                    refusal(register.address, register.width),
                    None,
                    "{}",
                    register.name
                );
            }
        }
    }

    #[test]
    fn the_duart_registers_a_read_changes_are_refused() {
        // MR, the factory test addresses, the receive FIFOs, IPCR,
        // the counter and its two commands; and an even address.
        for register in [0x0, 0x2, 0x3, 0x4, 0x6, 0x7, 0x8, 0xa, 0xb, 0xe, 0xf] {
            assert!(
                refusal(duart(register), Width::Byte).is_some(),
                "{register}"
            );
        }
        for register in [0x1, 0x5, 0x9, 0xc, 0xd] {
            assert_eq!(refusal(duart(register), Width::Byte), None, "{register}");
        }
        assert!(refusal(0x20_0018, Width::Byte).is_some());
        assert!(refusal(duart(0xc), Width::Word).is_some());
    }

    #[test]
    fn the_strobed_selects_and_unmapped_space_are_refused() {
        for address in [
            0x30_0000, 0x30_2000, 0x30_4003, 0x30_6000, 0x50_0000, 0x70_0000,
        ] {
            assert!(refusal(address, Width::Byte).is_some(), "{address:#x}");
        }
        assert!(refusal(0xff_fa27, Width::Byte).is_some());
        assert_eq!(refusal(0x40_0012, Width::Byte), None);
    }

    #[test]
    fn the_supervisor_registers_come_last_and_only_when_asked() {
        let profile = Model::Z3805a.profile();
        assert!(
            registers(profile, false)
                .iter()
                .all(|register| register.group == Group::User)
        );
        let all = registers(profile, true);
        let first = all
            .iter()
            .position(|register| register.group == Group::Supervisor)
            .expect("some");
        assert!(
            all[first..]
                .iter()
                .all(|register| register.group == Group::Supervisor)
        );
    }

    #[test]
    fn the_image_values_are_the_images() {
        for (model, name) in [
            (Model::Z3805a, "z3805a-3543b.bin"),
            (Model::A58503, "58503a-3704.bin"),
        ] {
            let path = format!(
                "{}/../../third_party/firmware/images/{name}",
                env!("CARGO_MANIFEST_DIR")
            );
            let image = std::fs::read(&path).expect(&path);
            let profile = model.profile();
            let at = profile.revision_at as usize;
            assert_eq!(
                &image[at..at + profile.revision.len()],
                profile.revision.as_bytes()
            );
            // Each register's image value is an immediate the image
            // writes to it: `move.w #v,addr.l` or `move.b`.  PORTC's is
            // its reset value, which no image writes, and PICR is
            // written through `(a2)`.
            for register in registers(profile, true) {
                let Some((value, _)) = register.image else {
                    continue;
                };
                if ["PORTC", "PICR"].contains(&register.name) {
                    continue;
                }
                let mut pattern = match register.width {
                    Width::Byte => vec![0x13, 0xfc, 0, value as u8],
                    _ => vec![0x33, 0xfc, (value >> 8) as u8, value as u8],
                };
                pattern.extend_from_slice(&register.address.to_be_bytes());
                let written = image.windows(pattern.len()).any(|w| w == pattern);
                // CS4 on the 58503A is left at its reset value.
                assert!(written || value == 0, "{name} {} {value:#x}", register.name);
            }
        }
    }

    #[test]
    fn the_identity_is_the_eeproms_and_the_flashs() {
        let profile = Model::Z3805a.profile();
        let bytes = |text: &str, n: usize| {
            let mut out: Vec<u32> = text.bytes().map(u32::from).collect();
            out.resize(n, 0);
            out
        };
        let identity = [
            bytes("Z3805A", 6),
            bytes("3625A0XXXX", 10),
            bytes("3543B", 5),
        ]
        .concat();
        assert_eq!(mismatch(profile, "3625A0XXXX", &identity), None);
        assert!(mismatch(profile, "3625A0XXXY", &identity).is_some());
        assert!(mismatch(Model::A58503.profile(), "3625A0XXXX", &identity).is_some());
    }
}
