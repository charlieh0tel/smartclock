//! The parameter keys of the board file's parts that the run gives
//! them, in one place: each part reads its own (`participants`,
//! `registry`, mc683xx-peripherals' AT28C64B), and the mc683xx run's
//! overrides write them over the file's (`mc683xx/overrides.rs`).

/// The indicators' (the lamp board's, the LED bank's and the front
/// panel's): report their changes, as `--indicators` asks.
pub(crate) const REPORT: &str = "report";

/// The EEPROM's, mc683xx-peripherals' AT28C64B (mc683xx's
/// docs/peripherals.md, AT28C64B).
pub(crate) mod eeprom {
    /// Its working file.
    pub(crate) const FILE: &str = "file";
    /// The image a missing working file starts from, or with none the
    /// cells.
    pub(crate) const IMAGE: &str = "image";
    /// Software data protection at power-up, when no working file's
    /// protection is found.
    pub(crate) const PROTECTED: &str = "protected";
}

/// The warm-up's place at power-up, which the oscillator and the
/// temperature sensor take, as a power cycle carries it.
pub(crate) mod warm_up {
    /// Seconds of it behind it.
    pub(crate) const SECONDS: &str = "warm_up_seconds";
    /// Settled.
    pub(crate) const SETTLED: &str = "warm_up_settled";
}

/// The oscillator's.
pub(crate) mod ocxo {
    use crate::board::ocxo::Terms;

    /// `Config`'s terms, fractional frequencies.
    pub(crate) const OFFSET: &str = "offset";
    pub(crate) const AGING: &str = "aging";
    pub(crate) const WARM_UP: &str = "warm_up";
    pub(crate) const EFC_GAIN: &str = "efc_gain";
    pub(crate) const WHITE_FM: &str = "white_fm";
    pub(crate) const RANDOM_WALK_FM: &str = "random_walk_fm";

    /// Its crystal's, as a power cycle carries it: its seconds of aging,
    /// its noise's draws taken, and its random walk's sum.
    pub(crate) const AGING_SECONDS: &str = "aging_seconds";
    pub(crate) const NOISE_DRAWS: &str = "noise_draws";
    pub(crate) const RANDOM_WALK: &str = "random_walk";

    /// The terms `terms` gives, each by its key.
    pub(crate) fn given(terms: &Terms) -> impl Iterator<Item = (&'static str, f64)> {
        [
            (OFFSET, terms.offset),
            (AGING, terms.aging),
            (WARM_UP, terms.warm_up),
            (EFC_GAIN, terms.efc_gain),
            (WHITE_FM, terms.white_fm),
            (RANDOM_WALK_FM, terms.random_walk_fm),
        ]
        .into_iter()
        .filter_map(|(key, value)| Some((key, value?)))
    }
}

/// The FPGA's, the `xc3042`'s (`fpga_part.rs`).
pub(crate) mod fpga {
    /// The design it runs (fpga.rs, `Design::NAMES`).
    pub(crate) const DESIGN: &str = "design";
    /// With the LCA model, the bitstream its configuration must be.
    pub(crate) const BITSTREAM: &str = "bitstream";
    /// The design that is the LCA model, the only one `BITSTREAM` goes
    /// with.
    pub(crate) const LCA: &str = "lca";
}

/// The synthetic GPS engine's.
pub(crate) mod oncore {
    /// Its model by name (`gps::ENGINES`), as `--gps-engine` gives it in
    /// place of the board file's.
    pub(crate) const ENGINE: &str = "engine";
    /// The protocol it speaks, its engine's family's, which the run
    /// gives with `--gps-engine`.
    pub(crate) const PROTOCOL: &str = "protocol";
    /// UTC at power-up, RFC 3339.
    pub(crate) const START: &str = "start";
    /// Its antenna's place: degrees, and meters above mean sea level.
    pub(crate) const LATITUDE: &str = "latitude";
    pub(crate) const LONGITUDE: &str = "longitude";
    pub(crate) const HEIGHT: &str = "height";
    /// GPS time less UTC, seconds.
    pub(crate) const LEAP_SECONDS: &str = "leap_seconds";
    /// Copy its line to standard error, as `--tap`.
    pub(crate) const TAP: &str = "tap";
    /// Whether its antenna is connected, which a power cycle carries.
    pub(crate) const ANTENNA: &str = "antenna";
}
