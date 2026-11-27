//! The board's serial ports' names, each by the name its line, its tap
//! and its messages give it, which is also the board files' run-time
//! endpoint's: the host port, DUART channel B; the SCI; the GPS
//! engine's, DUART channel A.

pub(crate) const HOST: &str = "host";
pub(crate) const SCI: &str = "sci";
pub(crate) const GPS: &str = "gps";
