//! The models' snapshots for the mc683xx board's (`mc683xx_cpu32::state::Save`):
//! each model's state in mc683xx-cpu32's snapshot encoding, under one format
//! version for smartclock's models.  What a snapshot leaves out is a
//! run's choice, not state: whether a model reports its indicators, and
//! the lines' far ends.

use mc683xx_cpu32::state::StateError;
use mc683xx_cpu32::state::decode;
use mc683xx_cpu32::state::encode;
use serde::Serialize;
use serde::de::DeserializeOwned;

/// The format of smartclock's models' snapshots.
const VERSION: u32 = 1;

/// `state` as a snapshot.
pub(crate) fn save<T: Serialize>(state: &T) -> Vec<u8> {
    encode(VERSION, state)
}

/// The state `snapshot` holds.
pub(crate) fn load<T: DeserializeOwned>(snapshot: &[u8]) -> Result<T, StateError> {
    decode(VERSION, snapshot)
}
