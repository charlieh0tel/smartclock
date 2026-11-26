//! `--serial`: the receiver a command is for, checked before anything
//! is sent to it.

use std::path::Path;

use anyhow::Result;
use smartclock::client;
use smartclock::client::Daemon;
use smartclock::parse::Identity;

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
    use super::Serial;
    use super::check_direct;
    use smartclock::parse;

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
