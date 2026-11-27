//! The SmartClock emulator in a browser (docs/emulator.md, "In a
//! browser"): the emulator's library, the board's side, built for
//! wasm32-unknown-unknown and bound by wasm-bindgen, its host the
//! browser's page.  The page gives the run its files as bytes, by path
//! (`files::InMemory`), and its clock's readings, a frame at a time from
//! `requestAnimationFrame`; the panel page's requests are answered in
//! the page, with no server (`panel::routes::Shared::respond`); the host
//! port is the page's console; the EEPROM is in memory, from the
//! bench unit's dump or the cells and protection the page keeps.
//!
//! `emulator` is the host, its Rust API the one the tests call natively;
//! `bindings` is its JavaScript API; `host` the ports a browser has;
//! `values` the checks on what JavaScript passes; `error` what a call
//! refuses.

pub mod bindings;
pub mod emulator;
pub mod error;
mod host;
mod values;
