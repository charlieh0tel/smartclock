//! `--serial`: the receiver a command is for, checked before anything
//! is sent to it.

use std::path::Path;

use anyhow::Result;
use smartclock::client;
use smartclock::client::Daemon;
use smartclock::console::Fetch;
use smartclock::console::Width;
use smartclock::parse::Identity;

/// The EEPROM's identity record: the model, NUL-padded, and the
/// serial number, each as (address, bytes) (the units' dumps under
/// `third_party/firmware/images/eeprom/`).
pub(crate) const EEPROM_MODEL: (u32, usize) = (0x40_0012, 6);
pub(crate) const EEPROM_SERIAL: (u32, usize) = (0x40_0024, 10);

/// What a unit's EEPROM says it is, read through the console, where
/// `*IDN?` cannot be asked.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct EepromIdentity {
    /// The model, such as `Z3805A`.
    pub(crate) model: String,
    /// The serial number.
    pub(crate) serial: String,
}

impl EepromIdentity {
    /// The console fetches that read the record, a byte each: the
    /// model's, then the serial's.
    pub(crate) fn fetches() -> Result<Vec<Fetch>> {
        [EEPROM_MODEL, EEPROM_SERIAL]
            .iter()
            .flat_map(|&(at, n)| at..at + n as u32)
            .map(|address| Ok(Fetch::new(address, Width::Byte)?))
            .collect()
    }

    /// The identity in the values [`Self::fetches`] gave, which `values`
    /// starts with, and the values after them.
    pub(crate) fn split(values: &[u32]) -> (Self, &[u32]) {
        let (model, rest) = values.split_at(EEPROM_MODEL.1);
        let (serial, rest) = rest.split_at(EEPROM_SERIAL.1);
        (
            Self {
                model: text(model),
                serial: text(serial),
            },
            rest,
        )
    }
}

/// The text of bytes read one a fetch, up to the first NUL.
pub(crate) fn text(bytes: &[u32]) -> String {
    bytes
        .iter()
        .map(|&byte| byte as u8)
        .take_while(|&byte| byte != 0)
        .map(char::from)
        .collect()
}

/// A receiver's serial number as `*IDN?` gives it, trimmed.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct Serial(String);

impl Serial {
    /// Parse `--serial`.
    pub(crate) fn parse(text: &str) -> std::result::Result<Self, String> {
        let text = text.trim();
        if text.is_empty() {
            return Err("a serial number cannot be empty".to_owned());
        }
        Ok(Self(text.to_owned()))
    }

    pub(crate) fn as_str(&self) -> &str {
        &self.0
    }

    /// Whether `identity` is this receiver's.
    pub(crate) fn matches(&self, identity: &Identity) -> bool {
        identity.has_serial(&self.0)
    }
}

impl std::fmt::Display for Serial {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

/// A receiver as an error names it.
pub(crate) fn describe(identity: &Identity) -> String {
    format!(
        "{} serial {} firmware {}",
        identity.model, identity.serial, identity.firmware
    )
}

/// Refuse unless the receiver on `device` is `serial`'s.
pub(crate) fn check_direct(serial: &Serial, identity: &Identity, device: &str) -> Result<()> {
    anyhow::ensure!(
        serial.matches(identity),
        "the receiver on {device} is {}, not serial {serial}; nothing else was sent",
        describe(identity)
    );
    Ok(())
}

/// Refuse unless the daemon at `socket`, by its `info` reply, is
/// attached to `serial`'s receiver and checks the serial itself; then
/// have every request on `daemon` carry it.
pub(crate) fn check_daemon(
    serial: &Serial,
    daemon: &mut Daemon,
    info: &serde_json::Value,
    socket: &Path,
) -> Result<()> {
    let at = socket.display();
    anyhow::ensure!(
        client::checks_receiver_serial(info),
        "the daemon at {at} is too old to check the serial; nothing was sent.  \
         Upgrade it, or leave out --serial"
    );
    let identity = client::identity(info).ok_or_else(|| {
        anyhow::anyhow!(
            "the daemon at {at} has not identified a receiver yet, so cannot be checked \
             for serial {serial}; nothing was sent"
        )
    })?;
    anyhow::ensure!(
        serial.matches(&identity),
        "the daemon at {at} is attached to {}, not serial {serial}; nothing was sent.  \
         Give --serial {serial} without --daemon to find the daemon serving it \
         (`smartclock-cli daemons` lists them)",
        describe(&identity)
    );
    daemon.for_receiver(serial.as_str());
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::EEPROM_MODEL;
    use super::EEPROM_SERIAL;
    use super::EepromIdentity;
    use super::Serial;
    use super::check_direct;
    use smartclock::parse;

    /// Where chip select 9 puts the EEPROM.
    const EEPROM_BASE: u32 = 0x40_0000;

    #[test]
    fn the_eeprom_identity_is_read_from_each_units_dump() {
        for (name, model, serial) in [
            ("z3801a-3542A01548", "Z3801A", "3542A01548"),
            ("58503b-L00000", "58503B", "L00000"),
        ] {
            let path = format!(
                "{}/../../third_party/firmware/images/eeprom/{name}-eeprom.bin",
                env!("CARGO_MANIFEST_DIR")
            );
            let eeprom = std::fs::read(&path).expect(&path);
            let values: Vec<u32> = [EEPROM_MODEL, EEPROM_SERIAL]
                .iter()
                .flat_map(|&(at, n)| {
                    let at = (at - EEPROM_BASE) as usize;
                    eeprom[at..at + n].iter().map(|&byte| u32::from(byte))
                })
                .collect();
            assert_eq!(
                EepromIdentity::fetches().expect("fetches").len(),
                values.len()
            );
            let (identity, rest) = EepromIdentity::split(&values);
            assert_eq!(identity.model, model, "{name}");
            assert_eq!(identity.serial, serial, "{name}");
            assert!(rest.is_empty());
        }
    }

    #[test]
    fn a_serial_is_trimmed_and_not_empty() {
        assert_eq!(
            Serial::parse(" 3700A00001\t").map(|s| s.to_string()),
            Ok("3700A00001".to_owned())
        );
        assert!(Serial::parse("  ").is_err());
    }

    #[test]
    fn a_serial_matches_exactly_as_the_receiver_reports_it() {
        let id = parse::identity("HEWLETT-PACKARD,Z3817A, 3800A00002 ,3752-A").expect("identity");
        let serial = |text: &str| Serial::parse(text).expect("serial");
        assert!(serial("3800A00002").matches(&id));
        assert!(!serial("3832a03064").matches(&id));
        assert!(!serial("3700A00001").matches(&id));
        assert!(!serial("3832A0306").matches(&id));
        let blank = parse::identity("HEWLETT-PACKARD,Z3817A,,3752-A").expect("identity");
        assert!(!serial("3800A00002").matches(&blank));
    }

    #[test]
    fn a_mismatch_names_both_serials_and_the_device() {
        let id = parse::identity("HEWLETT-PACKARD,Z3817A,3800A00002,3752-A").expect("identity");
        let why = check_direct(
            &Serial::parse("3700A00001").expect("serial"),
            &id,
            "/dev/ttyUSB0",
        )
        .expect_err("refused")
        .to_string();
        for part in ["3700A00001", "3800A00002", "/dev/ttyUSB0"] {
            assert!(why.contains(part), "{why}");
        }
    }
}
