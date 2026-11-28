//! `passthrough`: the unit's host port made a byte relay to its GPS
//! engine's port, DUART channel A, for a tool on the host to talk to
//! the engine through a closed unit.
//!
//! A dry run by default: it prints what it would send and opens
//! nothing.  With `--start` it checks the unit's EEPROM against
//! `--serial`, defines `relay` (`relay.s`, assembled into `relay.bin`)
//! in the pForth console's dictionary, reads its code back, and calls
//! it.  The relay never returns: it answers on the host port at its
//! first setting with `!` until a 0x00 latches it, and relays until the
//! unit is reset or power cycled, or until a break from the host with
//! no setting left, which stops servicing the watchdog so that it
//! resets the unit.  A break with a second setting left moves both
//! ports to it.

use std::io::Read;
use std::io::Write;
use std::sync::atomic::AtomicBool;
use std::time::Duration;
use std::time::Instant;

use anyhow::Context as _;
use anyhow::Result;
use anyhow::bail;
use anyhow::ensure;
use clap::ValueEnum;
use smartclock::console;
use smartclock::console::Console;
use smartclock::console::Region;
use smartclock::console::WayBack;
use smartclock::session::Config;
use smartclock::transport::Transport;
use smartclock::transport::serial::DataBits;
use smartclock::transport::serial::SerialTransport;
use smartclock::transport::tee::TeeTransport;
use smartclock::types::BaudRate;

use crate::select::EEPROM_MODEL;
use crate::select::EEPROM_SERIAL;
use crate::select::EepromIdentity;
use crate::select::Serial;

/// What `passthrough` is asked to do.
#[derive(Debug, clap::Args)]
pub(crate) struct PassthroughArgs {
    /// Start the relay: enter the console, check the unit's EEPROM
    /// against `--serial`, define and check the relay, and call it.
    /// Without it, a dry run that prints what would be sent and opens
    /// nothing.  Stop the port's daemon first.
    #[arg(long, requires = "serial")]
    start: bool,
    /// The relay's line rate, on the host port and the engine's alike:
    /// 2400 to 19200.
    #[arg(long, default_value_t = 9600)]
    relay_baud: u32,
    /// The relay's framing, on both ports.
    #[arg(long, value_enum, default_value = "8N1")]
    relay_framing: RelayFraming,
    /// A second line rate, which the host's first break moves both
    /// ports to.
    #[arg(long)]
    then_baud: Option<u32>,
    /// The second setting's framing.
    #[arg(long, value_enum, default_value = "8E1", requires = "then_baud")]
    then_framing: RelayFraming,
    /// The settings an Oncore emulator board's bootloader takes: 9600
    /// 8N1, then 19200 8E1.
    #[arg(long, conflicts_with_all = ["relay_baud", "relay_framing", "then_baud", "then_framing"])]
    oncore_emu_bootloader: bool,
}

impl PassthroughArgs {
    /// Whether the run starts the relay, rather than printing what it
    /// would send.
    pub(crate) const fn starts(&self) -> bool {
        self.start
    }

    /// The relay's first setting and its second, if any.
    fn settings(&self) -> Result<(Setting, Option<Setting>)> {
        if self.oncore_emu_bootloader {
            return Ok((
                Setting::new(BOOTLOADER.0.0, BOOTLOADER.0.1)?,
                Some(Setting::new(BOOTLOADER.1.0, BOOTLOADER.1.1)?),
            ));
        }
        let then = self
            .then_baud
            .map(|baud| Setting::new(baud, self.then_framing))
            .transpose()?;
        Ok((Setting::new(self.relay_baud, self.relay_framing)?, then))
    }
}

/// `--oncore-emu-bootloader`'s settings, first and second.
const BOOTLOADER: ((u32, RelayFraming), (u32, RelayFraming)) = (
    (9600, RelayFraming::EightNone),
    (19200, RelayFraming::EightEven),
);

/// A framing the relay sets on both ports.
#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum)]
enum RelayFraming {
    /// Eight data bits, no parity, one stop bit.
    #[value(name = "8N1")]
    EightNone,
    /// Eight data bits, even parity, one stop bit.
    #[value(name = "8E1")]
    EightEven,
}

impl RelayFraming {
    /// MR1 for it: eight bits, with even parity or none (MC68681, MR1).
    const fn duart_mode(self) -> u8 {
        match self {
            Self::EightNone => 0x13,
            Self::EightEven => 0x03,
        }
    }

    /// SCCR1's high byte for it: PE and M for parity, nine bits a
    /// character (MC68331UM, SCCR1).
    const fn sci_control(self) -> u8 {
        match self {
            Self::EightNone => 0x00,
            Self::EightEven => 0x06,
        }
    }

    /// Whether a parity bit is sent, and if so whether odd, as
    /// [`smartclock::types::Framing::odd_parity`] says it.
    const fn odd_parity(self) -> Option<bool> {
        match self {
            Self::EightNone => None,
            Self::EightEven => Some(false),
        }
    }
}

impl std::fmt::Display for RelayFraming {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Self::EightNone => "8N1",
            Self::EightEven => "8E1",
        })
    }
}

/// The DUART's clock selects in baud-rate set 2, which the firmware's
/// ACR picks: exact from its 3.6864 MHz crystal (MC68681 Table 4-5).
const DUART_CLOCK_SELECTS: [(BaudRate, u8); 5] = [
    (BaudRate::B1200, 0x66),
    (BaudRate::B2400, 0x88),
    (BaudRate::B4800, 0x99),
    (BaudRate::B9600, 0xbb),
    (BaudRate::B19200, 0xcc),
];

/// The MC68331's system clock, which the SCI divides.
const SYSTEM_CLOCK_HZ: u32 = 16_777_216;
/// The SCI's rate is the system clock over 32 times SCBR (MC68331UM,
/// SCCR0).
const SCI_CLOCKS_PER_DIVISOR: u32 = 32;
/// The largest SCBR the setting's byte holds.
const SCI_DIVISOR_MAX: u32 = 0xff;
/// How far the SCI's rate may be from the one asked for.
const MAX_RATE_ERROR: f64 = 0.02;

/// One of the relay's settings, as the host port opens at it and as
/// the relay's setting cell holds it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Setting {
    /// The line rate.
    baud: BaudRate,
    /// The framing.
    framing: RelayFraming,
    /// MR1 << 24 | CSR << 16 | SCCR1-high << 8 | SCBR (`relay.s`).
    cell: u32,
}

impl Setting {
    /// `baud` and `framing`, refused where the DUART has no such rate
    /// or the SCI would miss it by more than [`MAX_RATE_ERROR`].
    fn new(baud: u32, framing: RelayFraming) -> Result<Self> {
        let Some(&(rate, clock_select)) = DUART_CLOCK_SELECTS
            .iter()
            .find(|(rate, _)| rate.get() == baud)
        else {
            bail!("{baud} baud: the DUART has no such rate in baud-rate set 2");
        };
        let sci_clock = SYSTEM_CLOCK_HZ / SCI_CLOCKS_PER_DIVISOR;
        let divisor = (sci_clock + baud / 2) / baud;
        ensure!(
            divisor <= SCI_DIVISOR_MAX,
            "{baud} baud: the SCI's divisor, {divisor}, does not fit the relay's setting"
        );
        let error = (f64::from(sci_clock) / f64::from(divisor) - f64::from(baud)) / f64::from(baud);
        ensure!(
            error.abs() <= MAX_RATE_ERROR,
            "{baud} baud: the SCI would be {:+.2}% off",
            error * 100.0
        );
        Ok(Self {
            baud: rate,
            framing,
            cell: u32::from(framing.duart_mode()) << 24
                | u32::from(clock_select) << 16
                | u32::from(framing.sci_control()) << 8
                | divisor,
        })
    }
}

impl std::fmt::Display for Setting {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{} {}", self.baud, self.framing)
    }
}

/// A port whose line settings the run changes once the relay starts: a
/// local serial port, recorded or not.
pub(crate) trait Line: Transport {
    /// Move the port to `setting` and drop what it has received.
    fn set_line(&mut self, setting: Setting) -> smartclock::error::Result<()>;
    /// Hold the line in a break for `length`.
    fn send_break(&mut self, length: Duration) -> smartclock::error::Result<()>;
}

impl Line for SerialTransport {
    fn set_line(&mut self, setting: Setting) -> smartclock::error::Result<()> {
        SerialTransport::set_line(
            self,
            setting.baud,
            DataBits::Eight,
            setting.framing.odd_parity(),
        )
    }

    fn send_break(&mut self, length: Duration) -> smartclock::error::Result<()> {
        SerialTransport::send_break(self, length)
    }
}

impl<W: Write> Line for TeeTransport<SerialTransport, W> {
    fn set_line(&mut self, setting: Setting) -> smartclock::error::Result<()> {
        Line::set_line(self.inner_mut(), setting)
    }

    fn send_break(&mut self, length: Duration) -> smartclock::error::Result<()> {
        Line::send_break(self.inner_mut(), length)
    }
}

impl<L: Line + ?Sized> Line for &mut L {
    fn set_line(&mut self, setting: Setting) -> smartclock::error::Result<()> {
        (**self).set_line(setting)
    }

    fn send_break(&mut self, length: Duration) -> smartclock::error::Result<()> {
        (**self).send_break(length)
    }
}

/// The relay's machine code.
const CODE: &[u8] = include_bytes!("relay.bin");
const _: () = assert!(
    CODE.len().is_multiple_of(4),
    "the definition compiles the code a long word at a time"
);

/// The relay's two rings, 32 bytes each, which the definition allots
/// after its code (`relay.s`'s RING).
const RINGS: usize = 2 * 32;
/// Dictionary bytes the relay's entry takes besides its code and rings,
/// as the emulator measured them.
const ENTRY_BYTES: usize = 20;
/// Dictionary bytes left for `read-memory`'s `rd`, which reads the
/// code back.
const READ_WORD_BYTES: usize = 80;
/// Dictionary bytes a run needs free.
const NEEDED: usize = CODE.len() + RINGS + ENTRY_BYTES + READ_WORD_BYTES;

/// The console's dictionary segment, from its first RAM entry.
const SEGMENT: u32 = 0x1000;
/// Dictionary links below this are the ROM kernel's.
const ROM_END: u32 = 0x10_0000;
/// How many links the walk follows before giving up.
const WALK_LIMIT: usize = 400;

/// Long words a definition line holds, which keeps it within the line
/// length the relay was proven with.
const LONGS_PER_LINE: usize = 9;

/// The console's dictionary pointer, its latest entry's address, and
/// the latest entry's link and code: `here` is a variable there, with
/// the latest entry's address in the cell below it.
const HERE: &str = "hex here @ u.";
const LATEST: &str = "here 4 - @ u.";
const LATEST_IN_HEX: &str = "hex here 4 - @ u.";
/// Prints the long word at the address before it.
const FETCH: &str = "@ u.";
/// Switches the console to hex, as every number sent is.
const HEX: &str = "hex";
/// The word the relay is defined as.
const RELAY: &str = "relay";
/// How its definition opens, compiling what follows, and closes, after
/// the rings' length.
const DEFINITION_START: &str = "hex : relay [";
const DEFINITION_END: &str = "allot ] ;";
/// The models, as the EEPROM names them, the relay is proven on; any
/// other is refused.
const PROVEN: [&str; 4] = ["58503A", "Z3801A", "Z3805A", "Z3816A"];
/// Where an entry keeps its code's address.
const CODE_SLOT: u32 = 4;

/// Keeps the firmware from commanding the engine while the console has
/// the port, and gives that back.
const GPS_COMMANDS_OFF: &str = "1 disable_gps_cmds";
const GPS_COMMANDS_ON: &str = "0 disable_gps_cmds";

/// How many times the relay is defined and read back before the run
/// gives up.
const ATTEMPTS: usize = 2;

/// How long after the call the host port changes: a USB adapter's FIFO
/// can still be sending the call when the write returns, and a change
/// sooner garbles it.  The relay repeats its `!`, so waiting costs
/// nothing.
const CALL_DRAIN: Duration = Duration::from_millis(100);
/// How long the relay is given to send its `!`.
const READY_WAIT: Duration = Duration::from_secs(3);
/// What the relay sends until it is latched.
const READY: u8 = b'!';
/// What latches it.
const LATCH: u8 = 0;
/// How long the latch is given to leave an adapter's FIFO before the
/// port closes.
const LATCH_DRAIN: Duration = Duration::from_millis(50);
/// A break's length, and the gap after each, ending a relay that did
/// not answer.
const BREAK: Duration = Duration::from_millis(20);
const BREAK_GAP: Duration = Duration::from_secs(1);

/// What a run sends: the relay's definition and its call.
#[derive(Debug)]
struct Plan {
    /// The first setting.
    first: Setting,
    /// The second, applied by the host's first break.
    second: Option<Setting>,
    /// The definition, line by line.
    definition: Vec<String>,
    /// The call, which never returns.
    call: String,
}

impl Plan {
    fn new(args: &PassthroughArgs) -> Result<Self> {
        let (first, second) = args.settings()?;
        Ok(Self {
            first,
            second,
            definition: definition(CODE),
            call: format!(
                "{HEX} {:X} {:X} {RELAY}",
                first.cell,
                second.map_or(0, |second| second.cell)
            ),
        })
    }

    /// How many breaks end the relay.
    fn breaks(&self) -> usize {
        1 + usize::from(self.second.is_some())
    }
}

/// The definition of `relay` for `code`: compiled a long word at a time
/// between `[` and `]`, then the rings allotted after it.
fn definition(code: &[u8]) -> Vec<String> {
    let longs: Vec<String> = code
        .as_chunks::<4>()
        .0
        .iter()
        .map(|&long| format!("{:08x} ,", u32::from_be_bytes(long)))
        .collect();
    let mut lines = vec![DEFINITION_START.to_owned()];
    lines.extend(longs.chunks(LONGS_PER_LINE).map(|line| line.join(" ")));
    lines.push(format!("{RINGS:x} {DEFINITION_END}"));
    lines
}

/// Why the unit must not be started, or `None` if it may.
fn refusal(identity: &EepromIdentity, serial: &Serial) -> Option<String> {
    if identity.serial != serial.as_str() {
        return Some(format!(
            "the unit's EEPROM says {} {}, not serial {serial}; nothing was defined",
            identity.model, identity.serial
        ));
    }
    (!PROVEN.contains(&identity.model.as_str())).then(|| {
        format!(
            "the unit's EEPROM says {}, and the relay is proven only on the {}; \
             nothing was defined",
            identity.model,
            PROVEN.join(", ")
        )
    })
}

/// Fails unless `reply` to `line` is its echo, if any, and nothing else.
fn prompt_alone(line: &str, reply: &str) -> Result<()> {
    let rest = reply.trim_start().strip_prefix(line).unwrap_or(reply);
    ensure!(
        rest.trim().is_empty(),
        "the console answered {line:?} with {reply:?}"
    );
    Ok(())
}

/// Fails unless `found`, read back from `address`, is the relay's code.
fn compare(address: u32, found: &[u8]) -> Result<()> {
    if let Some(at) = (0..CODE.len()).find(|&at| found.get(at) != CODE.get(at)) {
        let at = at & !3;
        let long = |bytes: &[u8]| bytes.get(at..at + 4).map_or("nothing".to_owned(), hex);
        bail!(
            "read-back differs at {:#08x}: expected {}, found {}",
            address as usize + at,
            long(CODE),
            long(found)
        );
    }
    Ok(())
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}

/// The dictionary bytes free: from its first RAM entry, found by
/// walking the latest entry's links down to the ROM kernel's, to the
/// segment's end.
fn free_bytes<T: Read + Write>(console: &mut Console<T>) -> Result<u32> {
    let here = console.number(HERE)?;
    let mut entry = console.number(LATEST)?;
    for _ in 0..WALK_LIMIT {
        let link = console.number(&format!("{entry:X} {FETCH}"))?;
        if link < ROM_END {
            return Ok((entry + SEGMENT).saturating_sub(here));
        }
        entry = link;
    }
    bail!("the dictionary's links did not reach the ROM in {WALK_LIMIT} steps")
}

/// Enter the console, define the relay and read its code back.
fn define<T: Transport>(port: &mut T, plan: &Plan) -> Result<()> {
    let address = {
        let mut console = Console::reach(&mut *port, &mut crate::report)?;
        console.send(GPS_COMMANDS_OFF)?;
        let free = free_bytes(&mut console)?;
        ensure!(
            free as usize >= NEEDED,
            "only about {free} dictionary bytes are free; the relay needs {NEEDED}"
        );
        for line in &plan.definition {
            prompt_alone(line, &console.send(line)?)?;
        }
        let entry = console.number(LATEST_IN_HEX)?;
        console.number(&format!("{entry:X} {CODE_SLOT} + {FETCH}"))?
    };
    let length = u32::try_from(CODE.len()).context("the relay's length")?;
    let mut found = Vec::new();
    console::read_memory(
        &mut *port,
        Region::new(address, length)?,
        &mut found,
        None,
        crate::report,
        &AtomicBool::new(false),
    )?;
    compare(address, &found)?;
    eprintln!("relay defined at {address:#x} and read back");
    Ok(())
}

/// Give the firmware back its engine commands and return to SCPI with
/// `halt` alone.
fn quit_console<T: Transport>(port: &mut T, config: &Config) -> Result<()> {
    Console::reach(&mut *port, &mut crate::report)?.send(GPS_COMMANDS_ON)?;
    let back = console::leave(&mut *port, config.clone(), WayBack::Halt)
        .context("returning to SCPI; halt by hand, or power cycle")?;
    eprintln!("back at SCPI: {back}");
    Ok(())
}

/// Whether the relay's `!` arrives within [`READY_WAIT`].
fn ready<T: Read>(port: &mut T) -> Result<bool> {
    let deadline = Instant::now() + READY_WAIT;
    let mut chunk = [0u8; 256];
    while Instant::now() < deadline {
        let n = port.read(&mut chunk)?;
        if chunk[..n].contains(&READY) {
            return Ok(true);
        }
    }
    Ok(false)
}

/// Print what a run would send and open nothing.
pub(crate) fn dry_run(args: &PassthroughArgs) -> Result<()> {
    print!("{}", dry_run_text(&Plan::new(args)?));
    Ok(())
}

fn dry_run_text(plan: &Plan) -> String {
    let then = plan.second.map_or_else(
        || "a break from the host resets the unit".to_owned(),
        |second| {
            format!("the host's first break moves both to {second}, and a second resets the unit")
        },
    );
    format!(
        "Dry run: nothing is opened or sent.  With --start and --serial, on the port:\n\n\
         1. An empty line; if no `p4th` prompt answers, :SYSTem:LANGuage \"PFORTH\".\n\
         2. The EEPROM's model and serial, a `c@ u.` a byte from {:#x} and {:#x}.  Another \
         serial, or a model but the {}, stops the run, back at SCPI by `halt`.\n\
         3. `{GPS_COMMANDS_OFF}`, then `{HERE}`, `{LATEST}` and `<link> {FETCH}` down the \
         dictionary to its first RAM entry.  Fewer than {NEEDED} bytes free stops the run.\n\
         4. The relay's definition, each line answered by the prompt alone:\n\n{}\n\n\
         5. `{LATEST_IN_HEX}` and `<entry> {CODE_SLOT} + {FETCH}` for its code, read back with \
         `rd` as read-memory reads.  A bad reply or a difference: `{GPS_COMMANDS_ON}`, `halt`, \
         and once more from 1; a second failure stops the run.\n\
         6. `{}`, which never returns.  The port then moves to {}, and at the relay's `!` a \
         0x00 latches it, `ready` is printed and the command exits.  With no `!`, a break for \
         each setting ends the relay, the last resetting the unit.\n\n\
         The relay runs at {} on the GPS engine's port and the host's; {then}.  Otherwise only \
         a reset or a power cycle ends it.\n",
        EEPROM_MODEL.0,
        EEPROM_SERIAL.0,
        PROVEN.join(", "),
        plan.definition
            .iter()
            .map(|line| format!("    {line}"))
            .collect::<Vec<_>>()
            .join("\n"),
        plan.call,
        plan.first,
        plan.first,
    )
}

/// Check the unit, define the relay, start it and latch the port.  The
/// relay keeps running after the command exits.
pub(crate) fn run<P: Line>(
    mut port: P,
    args: &PassthroughArgs,
    serial: &Serial,
    config: Config,
) -> Result<()> {
    let plan = Plan::new(args)?;
    let values = console::fetch_each(&mut port, &EepromIdentity::fetches()?, crate::report)?;
    let (identity, _) = EepromIdentity::split(&values);
    if let Some(why) = refusal(&identity, serial) {
        match console::leave(&mut port, config, WayBack::Halt) {
            Ok(back) => eprintln!("back at SCPI: {back}"),
            Err(e) => eprintln!("returning to SCPI failed: {e}; halt by hand, or power cycle"),
        }
        bail!(why);
    }
    for attempt in 1..=ATTEMPTS {
        let Err(e) = define(&mut port, &plan) else {
            break;
        };
        eprintln!("attempt {attempt} failed: {e:#}");
        quit_console(&mut port, &config)
            .with_context(|| format!("after attempt {attempt} failed: {e:#}"))?;
        if attempt == ATTEMPTS {
            bail!("the relay was not defined: {e:#}; nothing was started");
        }
    }
    port.write_all(plan.call.as_bytes())?;
    port.write_all(b"\r")?;
    port.flush()?;
    std::thread::sleep(CALL_DRAIN);
    // Once the call is out the relay may be running, so any failure from
    // here ends it.
    let latched = latch(&mut port, plan.first).and_then(|answered| {
        ensure!(
            answered,
            "no {:?} from the relay at {}",
            char::from(READY),
            plan.first
        );
        Ok(())
    });
    if let Err(e) = latched {
        let ended = end_relay(&mut port, plan.breaks()).map_or_else(
            |broke| format!("ending it with breaks failed too: {broke}; power cycle the unit"),
            |()| {
                format!(
                    "sent {} break(s) to end it, the last resetting the unit",
                    plan.breaks()
                )
            },
        );
        bail!("{e:#}; {ended}");
    }
    println!("ready");
    Ok(())
}

/// Move the port to the relay's first setting and, at its `!`, latch
/// it; whether the `!` came.
fn latch<P: Line>(port: &mut P, first: Setting) -> Result<bool> {
    port.set_line(first)?;
    if !ready(port)? {
        return Ok(false);
    }
    port.write_all(&[LATCH])?;
    port.flush()?;
    std::thread::sleep(LATCH_DRAIN);
    Ok(true)
}

/// End a relay with `breaks` breaks, the last of which resets the unit.
fn end_relay<P: Line>(port: &mut P, breaks: usize) -> Result<()> {
    for _ in 0..breaks {
        port.send_break(BREAK)?;
        std::thread::sleep(BREAK_GAP);
    }
    Ok(())
}

#[cfg(test)]
mod tests;
