//! The command line, and what it is made into.

use std::fmt;
use std::path::PathBuf;
use std::str::FromStr;

use anyhow::Context as _;
use anyhow::Result;
use anyhow::anyhow;
use anyhow::bail;
use clap::Args;
use clap::Parser;
use clap::ValueEnum;
use jiff::Timestamp;
use smartclock_gps::engine::Options;
use smartclock_gps::engine::furuno::GT_74;
use smartclock_gps::engine::furuno::GT_74_PULSE_EARLY;
use smartclock_gps::engine::furuno::GT_74_WITHOUT_TIME_RAIM;
use smartclock_gps::engine::furuno::Identity as FurunoIdentity;
use smartclock_gps::engine::identity::Identity;
use smartclock_gps::engine::identity::UT_PLUS_2_2;
use smartclock_gps::engine::identity::UT_PLUS_3_1;
use smartclock_gps::engine::identity::VP_EIGHT;
use smartclock_gps::engine::identity::VP_EIGHT_WITHOUT_TIME_RAIM;
use smartclock_gps::engine::identity::VP_SIX;
use smartclock_gps::engine::saved::CONFIGURATION_58503A;
use smartclock_gps::engine::saved::ChannelCount;
use smartclock_gps::engine::saved::SavedConfiguration;
use smartclock_gps::time::LeapChange;
use smartclock_gps::time::LeapEvent;
use smartclock_gps::time::UnixSeconds;
use smartclock_gps::units::Seconds;

use crate::provider::GPSD_PORT;
use crate::provider::UTC_OFFSET;

/// Runs a GPS engine on this host and speaks to a `SmartClock`'s engine
/// port as it: Oncore binary, or a Furuno GT-74's NMEA.
#[derive(Debug, Parser)]
#[command(version)]
pub(crate) struct Cli {
    /// Where each second's record comes from: `gpsd:HOST[:PORT]` (a
    /// u-blox receiver's raw output through gpsd), `ubx:FILE` (a raw
    /// UBX recording, such as `gpspipe -R` writes), `fixed` (a made-up
    /// sky over a fixed place) or `leap:MIDNIGHT[,insert|,remove]` (that
    /// sky, with a leap second ending the UTC day before MIDNIGHT).
    #[arg(value_name = "PROVIDER")]
    pub(crate) provider: ProviderSpec,

    /// The engine: an Oncore, or the Z3815A's Furuno GT-74.
    #[arg(long, value_enum)]
    pub(crate) engine: EngineName,

    /// The eight-channel Oncore a six-channel engine becomes at its
    /// first `@@Ea`.  An Oncore's alone.
    #[arg(long, value_enum, value_name = "ENGINE")]
    pub(crate) eight_on_ea: Option<EngineName>,

    /// The engine takes no commands.  An Oncore's alone.
    #[arg(long)]
    pub(crate) deaf: bool,

    /// The engine powers up configured, as the receiver named leaves it.
    /// An Oncore's alone.
    #[arg(long, value_enum)]
    pub(crate) saved: Option<Saved>,

    /// Relabels the provider's time: its first second is this UTC
    /// second (RFC 3339, such as 2027-12-31T23:58:00Z).
    #[arg(long, value_name = "RFC3339")]
    pub(crate) fake_start: Option<Timestamp>,

    /// The `SmartClock`'s side.
    #[command(flatten)]
    pub(crate) transport: TransportArgs,

    /// Logs every byte each way, with its time, to this file.
    #[arg(long, value_name = "FILE")]
    pub(crate) log: Option<PathBuf>,
}

/// Where the `SmartClock`'s engine port is: one of the three.
#[derive(Debug, Args)]
#[group(required = true, multiple = false)]
pub(crate) struct TransportArgs {
    /// Connects to it over TCP, as a serial-to-TCP bridge such as
    /// ser2net serves it.
    #[arg(long, value_name = "HOST:PORT")]
    pub(crate) connect: Option<String>,

    /// Waits for it to connect over TCP, one connection at a time.
    #[arg(long, value_name = "ADDR")]
    pub(crate) listen: Option<String>,

    /// Opens it as a serial port, at 9600 8N1.
    #[arg(long, value_name = "PATH")]
    pub(crate) serial: Option<PathBuf>,
}

/// An engine the feed can be, as the command line names it: an Oncore
/// or a Furuno GT-74.
#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum)]
pub(crate) enum EngineName {
    /// A six-channel VP.
    VpSix,
    /// An eight-channel VP.
    VpEight,
    /// An eight-channel VP whose firmware has no Time RAIM.
    VpEightWithoutTimeRaim,
    /// A UT Plus of firmware 2.2.
    #[value(name = "ut-plus-2.2")]
    UtPlus2_2,
    /// A UT Plus of firmware 3.1.
    #[value(name = "ut-plus-3.1")]
    UtPlus3_1,
    /// A Furuno GT-74 with Time RAIM, the Z3815A's engine.
    #[value(name = "gt-74")]
    Gt74,
    /// A GT-74 whose software has no Time RAIM.
    #[value(name = "gt-74-without-time-raim")]
    Gt74WithoutTimeRaim,
    /// A GT-74 whose software puts the 1 PPS 400 ns early, which the
    /// Z3815A corrects in the cable delay it sends.
    #[value(name = "gt-74-pulse-early")]
    Gt74PulseEarly,
}

/// The engine a name is, of its family.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Family {
    Oncore(&'static Identity),
    Furuno(&'static FurunoIdentity),
}

impl EngineName {
    /// What the command line calls it.
    pub(crate) fn name(self) -> String {
        self.to_possible_value()
            .map_or_else(String::new, |value| value.get_name().to_owned())
    }

    const fn family(self) -> Family {
        match self {
            Self::VpSix => Family::Oncore(&VP_SIX),
            Self::VpEight => Family::Oncore(&VP_EIGHT),
            Self::VpEightWithoutTimeRaim => Family::Oncore(&VP_EIGHT_WITHOUT_TIME_RAIM),
            Self::UtPlus2_2 => Family::Oncore(&UT_PLUS_2_2),
            Self::UtPlus3_1 => Family::Oncore(&UT_PLUS_3_1),
            Self::Gt74 => Family::Furuno(&GT_74),
            Self::Gt74WithoutTimeRaim => Family::Furuno(&GT_74_WITHOUT_TIME_RAIM),
            Self::Gt74PulseEarly => Family::Furuno(&GT_74_PULSE_EARLY),
        }
    }
}

/// A configuration an engine can power up in, as the command line
/// names it.
#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum)]
pub(crate) enum Saved {
    /// As a 58503A's power-up script leaves its engine.
    #[value(name = "58503a")]
    Receiver58503A,
}

impl Saved {
    const fn configuration(self) -> &'static SavedConfiguration {
        match self {
            Self::Receiver58503A => &CONFIGURATION_58503A,
        }
    }
}

/// Where each second's record comes from, as the command line names
/// it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) enum ProviderSpec {
    /// gpsd at a host and port, giving a u-blox receiver's raw output.
    Gpsd {
        /// The host.
        host: String,
        /// The port.
        port: u16,
    },
    /// A raw UBX recording.
    Ubx(PathBuf),
    /// The made-up sky.
    Fixed,
    /// The made-up sky with a leap second.
    Leap(LeapEvent),
}

/// What a provider is called on the command line, before its colon.
const GPSD: &str = "gpsd";
const UBX: &str = "ubx";
const FIXED: &str = "fixed";
const LEAP: &str = "leap";
/// What follows a leap second's midnight to say which way it goes.
const INSERT: &str = "insert";
const REMOVE: &str = "remove";

impl FromStr for ProviderSpec {
    type Err = anyhow::Error;

    fn from_str(text: &str) -> Result<Self> {
        let (kind, rest) = match text.split_once(':') {
            Some((kind, rest)) => (kind, Some(rest)),
            None => (text, None),
        };
        match (kind, rest) {
            (FIXED, None) => Ok(Self::Fixed),
            (GPSD, Some(address)) if !address.is_empty() => {
                let (host, port) = host_and_port(address)?;
                Ok(Self::Gpsd { host, port })
            }
            (UBX, Some(path)) if !path.is_empty() => Ok(Self::Ubx(PathBuf::from(path))),
            (LEAP, Some(leap)) => leap_event(leap).map(Self::Leap),
            _ => bail!(
                "no provider \"{text}\": give gpsd:HOST[:PORT], ubx:FILE, fixed or leap:MIDNIGHT[,insert|,remove]"
            ),
        }
    }
}

impl fmt::Display for ProviderSpec {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Gpsd { host, port } => write!(formatter, "{GPSD}:{host}:{port}"),
            Self::Ubx(path) => write!(formatter, "{UBX}:{}", path.display()),
            Self::Fixed => write!(formatter, "{FIXED}"),
            Self::Leap(event) => write!(formatter, "{LEAP} ({:?})", event.change),
        }
    }
}

/// `HOST`, `HOST:PORT`, `[V6]` or `[V6]:PORT`, gpsd's port if none.
fn host_and_port(address: &str) -> Result<(String, u16)> {
    let port = |text: &str| {
        text.parse::<u16>()
            .with_context(|| format!("no port \"{text}\" in \"{address}\""))
    };
    if let Some(bracketed) = address.strip_prefix('[') {
        let (host, rest) = bracketed
            .split_once(']')
            .ok_or_else(|| anyhow!("no closing bracket in \"{address}\""))?;
        return match rest.strip_prefix(':') {
            Some(text) => Ok((host.to_owned(), port(text)?)),
            None if rest.is_empty() => Ok((host.to_owned(), GPSD_PORT)),
            None => bail!("no host and port in \"{address}\""),
        };
    }
    match address.rsplit_once(':') {
        // A bare IPv6 address has more than one colon and no port.
        Some((host, _)) if host.contains(':') => Ok((address.to_owned(), GPSD_PORT)),
        Some((host, text)) => Ok((host.to_owned(), port(text)?)),
        None => Ok((address.to_owned(), GPSD_PORT)),
    }
}

/// `MIDNIGHT[,insert|,remove]`: the leap second at the end of the UTC
/// day before `MIDNIGHT`, under the made-up sky's offset.
fn leap_event(text: &str) -> Result<LeapEvent> {
    let (midnight, change) = match text.split_once(',') {
        None => (text, LeapChange::Insert),
        Some((midnight, INSERT)) => (midnight, LeapChange::Insert),
        Some((midnight, REMOVE)) => (midnight, LeapChange::Remove),
        Some((_, other)) => bail!("no leap second change \"{other}\": give {INSERT} or {REMOVE}"),
    };
    let utc = utc(&midnight.parse::<Timestamp>()?)?;
    LeapEvent::at_day_end(utc, UTC_OFFSET, change)
        .ok_or_else(|| anyhow!("{midnight} is not a UTC midnight, where a leap second ends a day"))
}

/// A whole UTC second.
pub(crate) fn utc(timestamp: &Timestamp) -> Result<UnixSeconds> {
    if timestamp.subsec_nanosecond() != 0 {
        bail!("{timestamp} is not a whole second");
    }
    Ok(UnixSeconds::from_epoch(Seconds(timestamp.as_second())))
}

/// Where the `SmartClock`'s engine port is.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) enum TransportSpec {
    /// A TCP server to connect to.
    Connect(String),
    /// A TCP address to listen on.
    Listen(String),
    /// A serial port.
    Serial(PathBuf),
}

/// The engine to make, of its family.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum EngineSpec {
    /// An Oncore, and how it departs from the one its identity names.
    Oncore {
        identity: &'static Identity,
        options: Options,
    },
    /// A GT-74, which has no options.
    Furuno(&'static FurunoIdentity),
}

/// What the command line asks for, checked.
#[derive(Debug)]
pub(crate) struct Config {
    pub(crate) provider: ProviderSpec,
    /// The engine as the command line names it.
    pub(crate) engine: EngineName,
    /// The engine to make.
    pub(crate) spec: EngineSpec,
    pub(crate) fake_start: Option<UnixSeconds>,
    pub(crate) transport: TransportSpec,
    pub(crate) log: Option<PathBuf>,
}

impl Cli {
    /// Checks what the command line asks for.  The engine's options are
    /// checked when it is made.
    pub(crate) fn configure(self) -> Result<Config> {
        let spec = match self.engine.family() {
            Family::Oncore(identity) => EngineSpec::Oncore {
                identity,
                options: self.oncore_options(identity)?,
            },
            Family::Furuno(identity) => {
                self.refuse_oncore_options()?;
                EngineSpec::Furuno(identity)
            }
        };
        let fake_start = self.fake_start.as_ref().map(utc).transpose()?;
        let transport = match self.transport {
            TransportArgs {
                connect: Some(address),
                ..
            } => TransportSpec::Connect(address),
            TransportArgs {
                listen: Some(address),
                ..
            } => TransportSpec::Listen(address),
            TransportArgs {
                serial: Some(path), ..
            } => TransportSpec::Serial(path),
            TransportArgs { .. } => bail!("give --connect, --listen or --serial"),
        };
        Ok(Config {
            provider: self.provider,
            engine: self.engine,
            spec,
            fake_start,
            transport,
            log: self.log,
        })
    }

    /// How the Oncore `identity` names is to depart from it.
    fn oncore_options(&self, identity: &'static Identity) -> Result<Options> {
        let mut options = Options::DEFAULT;
        options.deaf = self.deaf;
        options.saved = self.saved.map(Saved::configuration);
        if let Some(eight) = self.eight_on_ea {
            if ChannelCount::of(identity.model()) != ChannelCount::Six {
                bail!(
                    "--eight-on-ea is for a six-channel engine, and --engine names an eight-channel one"
                );
            }
            let target = match eight.family() {
                Family::Oncore(target) => target,
                Family::Furuno(_) => {
                    bail!("--eight-on-ea names a Furuno engine; give an eight-channel Oncore")
                }
            };
            if ChannelCount::of(target.model()) != ChannelCount::Eight {
                bail!("--eight-on-ea names a six-channel engine; give an eight-channel one");
            }
            options.eight_on_ea = Some(target);
        }
        Ok(options)
    }

    /// Refuses the options only an Oncore has, for a GT-74.
    fn refuse_oncore_options(&self) -> Result<()> {
        let engine = self.engine.name();
        if self.eight_on_ea.is_some() {
            bail!(
                "--eight-on-ea is for a six-channel Oncore, which becomes an eight-channel one at its first @@Ea; {engine} is a Furuno engine, which has no @@ messages"
            );
        }
        if self.saved.is_some() {
            bail!(
                "--saved powers an Oncore up as the receiver named leaves it, and a 58503A's engine is an Oncore; {engine} always powers up with no backup"
            );
        }
        if self.deaf {
            bail!("--deaf is for an Oncore; the {engine} model has no deaf mode");
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::Cli;
    use super::Config;
    use super::EngineSpec;
    use super::ProviderSpec;
    use super::TransportSpec;
    use crate::provider::GPSD_PORT;
    use clap::Parser;
    use smartclock_gps::engine::Options;
    use smartclock_gps::engine::furuno::GT_74;
    use smartclock_gps::engine::furuno::GT_74_PULSE_EARLY;
    use smartclock_gps::engine::furuno::GT_74_WITHOUT_TIME_RAIM;
    use smartclock_gps::engine::identity::Identity;
    use smartclock_gps::engine::identity::UT_PLUS_2_2;
    use smartclock_gps::engine::identity::VP_EIGHT;
    use smartclock_gps::engine::identity::VP_SIX;
    use smartclock_gps::engine::saved::CONFIGURATION_58503A;
    use smartclock_gps::time::DateTime;
    use smartclock_gps::time::GpsSeconds;
    use smartclock_gps::time::LeapChange;
    use smartclock_gps::time::LeapEvent;
    use smartclock_gps::units::Seconds;
    use std::path::PathBuf;

    fn parse(arguments: &[&str]) -> anyhow::Result<Config> {
        let cli = Cli::try_parse_from(
            ["smartclock-gps-feed"]
                .iter()
                .chain(arguments)
                .copied()
                .collect::<Vec<_>>(),
        )?;
        cli.configure()
    }

    /// The Oncore a configuration makes.
    fn oncore(config: &Config) -> (&'static Identity, Options) {
        match config.spec {
            EngineSpec::Oncore { identity, options } => (identity, options),
            EngineSpec::Furuno(_) => panic!("a Furuno engine"),
        }
    }

    /// Each GT-74 is made from its name, with any provider and
    /// transport.
    #[test]
    fn the_command_line_names_a_furuno_engine() {
        for (name, identity) in [
            ("gt-74", &GT_74),
            ("gt-74-without-time-raim", &GT_74_WITHOUT_TIME_RAIM),
            ("gt-74-pulse-early", &GT_74_PULSE_EARLY),
        ] {
            let config = parse(&[
                "gpsd:apu-0",
                "--engine",
                name,
                "--fake-start",
                "2027-12-31T23:58:00Z",
                "--serial",
                "/dev/ttyS0",
                "--log",
                "wire.log",
            ])
            .unwrap();
            assert_eq!(config.spec, EngineSpec::Furuno(identity));
            assert_eq!(config.engine.name(), name);
            assert_eq!(
                config.transport,
                TransportSpec::Serial(PathBuf::from("/dev/ttyS0"))
            );
            assert!(config.fake_start.is_some());
        }
        let config = parse(&[
            "leap:2028-01-01T00:00:00Z",
            "--engine",
            "gt-74",
            "--connect",
            "a:1",
        ])
        .unwrap();
        assert_eq!(config.spec, EngineSpec::Furuno(&GT_74));
    }

    /// An Oncore's options are refused with a GT-74, each saying why,
    /// and a GT-74 is no eight-channel Oncore to become.
    #[test]
    fn a_furuno_engine_refuses_an_oncores_options() {
        for (option, why) in [
            (&["--eight-on-ea", "vp-eight"][..], "no @@ messages"),
            (&["--saved", "58503a"], "engine is an Oncore"),
            (&["--deaf"], "no deaf mode"),
        ] {
            let arguments: Vec<&str> = ["fixed", "--engine", "gt-74", "--connect", "a:1"]
                .iter()
                .chain(option)
                .copied()
                .collect();
            let refusal = parse(&arguments).unwrap_err().to_string();
            assert!(refusal.contains(why), "{refusal}");
        }
        let refusal = parse(&[
            "fixed",
            "--engine",
            "vp-six",
            "--eight-on-ea",
            "gt-74",
            "--connect",
            "a:1",
        ])
        .unwrap_err()
        .to_string();
        assert!(refusal.contains("Furuno"), "{refusal}");
    }

    fn provider(text: &str) -> anyhow::Result<ProviderSpec> {
        text.parse()
    }

    #[test]
    fn providers_parse() {
        let gpsd = |host: &str, port| ProviderSpec::Gpsd {
            host: host.to_owned(),
            port,
        };
        assert_eq!(provider("gpsd:apu-0").unwrap(), gpsd("apu-0", GPSD_PORT));
        assert_eq!(provider("gpsd:apu-0:3000").unwrap(), gpsd("apu-0", 3000));
        assert_eq!(provider("gpsd:[::1]").unwrap(), gpsd("::1", GPSD_PORT));
        assert_eq!(provider("gpsd:[::1]:3000").unwrap(), gpsd("::1", 3000));
        assert_eq!(
            provider("gpsd:fe80::1").unwrap(),
            gpsd("fe80::1", GPSD_PORT)
        );
        assert_eq!(
            provider("ubx:capture.ubx").unwrap(),
            ProviderSpec::Ubx(PathBuf::from("capture.ubx"))
        );
        assert_eq!(provider("fixed").unwrap(), ProviderSpec::Fixed);

        let midnight = DateTime::new(2028, 1, 1, 0, 0, 0).unwrap().to_unix();
        let inserted = GpsSeconds::from_utc(midnight, Seconds(18)).unwrap();
        assert_eq!(
            provider("leap:2028-01-01T00:00:00Z").unwrap(),
            ProviderSpec::Leap(LeapEvent {
                at: inserted,
                change: LeapChange::Insert,
            })
        );
        assert_eq!(
            provider("leap:2028-01-01T00:00:00Z,remove").unwrap(),
            ProviderSpec::Leap(LeapEvent {
                at: inserted - Seconds(1),
                change: LeapChange::Remove,
            })
        );

        for bad in [
            "gpsd:",
            "gpsd:host:port",
            "gpsd:[::1",
            "ubx:",
            "fixed:x",
            "leap:2028-01-01T00:00:01Z",
            "leap:2028-01-01T00:00:00Z,sideways",
            "leap:tomorrow",
            "sky",
        ] {
            assert!(provider(bad).is_err(), "{bad}");
        }
    }

    #[test]
    fn the_command_line_configures_the_engine_and_transport() {
        let config = parse(&[
            "fixed",
            "--engine",
            "vp-six",
            "--eight-on-ea",
            "vp-eight",
            "--connect",
            "shack:2001",
        ])
        .unwrap();
        assert_eq!(oncore(&config).0, &VP_SIX);
        assert_eq!(oncore(&config).1.eight_on_ea.unwrap(), &VP_EIGHT);
        assert!(!oncore(&config).1.deaf);
        assert_eq!(oncore(&config).1.saved, None);
        assert_eq!(
            config.transport,
            TransportSpec::Connect("shack:2001".to_owned())
        );
        assert_eq!(config.fake_start, None);
        assert_eq!(config.log, None);

        let config = parse(&[
            "gpsd:apu-0",
            "--engine",
            "vp-eight",
            "--deaf",
            "--saved",
            "58503a",
            "--listen",
            "127.0.0.1:4000",
            "--fake-start",
            "2027-12-31T23:58:00Z",
            "--log",
            "wire.log",
        ])
        .unwrap();
        assert_eq!(oncore(&config).0, &VP_EIGHT);
        assert!(oncore(&config).1.deaf);
        assert_eq!(oncore(&config).1.saved, Some(&CONFIGURATION_58503A));
        assert_eq!(
            config.transport,
            TransportSpec::Listen("127.0.0.1:4000".to_owned())
        );
        assert_eq!(
            config.fake_start,
            Some(DateTime::new(2027, 12, 31, 23, 58, 0).unwrap().to_unix())
        );
        assert_eq!(config.log, Some(PathBuf::from("wire.log")));

        let config = parse(&[
            "ubx:a.ubx",
            "--engine",
            "ut-plus-2.2",
            "--serial",
            "/dev/ttyS0",
        ])
        .unwrap();
        assert_eq!(oncore(&config).0, &UT_PLUS_2_2);
        assert_eq!(
            config.transport,
            TransportSpec::Serial(PathBuf::from("/dev/ttyS0"))
        );
    }

    #[test]
    fn the_command_line_refuses_what_cannot_be() {
        for bad in [
            // No transport, and two.
            &["fixed", "--engine", "vp-six"][..],
            &[
                "fixed",
                "--engine",
                "vp-six",
                "--connect",
                "a:1",
                "--listen",
                "b:2",
            ],
            // No engine, and one there is not.
            &["fixed", "--connect", "a:1"],
            &["fixed", "--engine", "vp-nine", "--connect", "a:1"],
            // Eight channels on @@Ea from eight, or to six.
            &[
                "fixed",
                "--engine",
                "vp-eight",
                "--eight-on-ea",
                "vp-eight",
                "--connect",
                "a:1",
            ],
            &[
                "fixed",
                "--engine",
                "vp-six",
                "--eight-on-ea",
                "vp-six",
                "--connect",
                "a:1",
            ],
            // A start that is not a whole second.
            &[
                "fixed",
                "--engine",
                "vp-six",
                "--fake-start",
                "2027-12-31T23:58:00.5Z",
                "--connect",
                "a:1",
            ],
            &[
                "fixed",
                "--engine",
                "vp-six",
                "--saved",
                "z3801a",
                "--connect",
                "a:1",
            ],
        ] {
            assert!(parse(bad).is_err(), "{bad:?}");
        }
    }
}
