//! The assistance messages, `UBX-MGA-*`: what a caller tells a
//! receiver to speed its first fix.

use crate::frame::Encode;
use crate::frame::EncodeError;
use crate::frame::encode_with;
use crate::message::MGA_INI;
use crate::units::Centimeters;
use crate::units::HundredNanodegrees;

/// [`MgaIniPosLlh`]'s payload length (p. 329): the frame is
/// [`OVERHEAD`](crate::frame::OVERHEAD) more.
pub const INI_POS_LLH_LENGTH: usize = 20;
/// Its `type` and `version`.
const INI_POS_LLH_TYPE: u8 = 0x01;
const INI_POS_LLH_VERSION: u8 = 0x00;

/// `UBX-MGA-INI-POS_LLH` (section 32.15.9.2, p. 329): an initial
/// position, in WGS84 coordinates.  The receiver acknowledges it only
/// as `CFG-NAVX5`'s `ackAiding` says, and then with `UBX-MGA-ACK`
/// (section 32.15.1, p. 307), not `UBX-ACK-ACK`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MgaIniPosLlh {
    /// `lat`.
    pub latitude: HundredNanodegrees,
    /// `lon`.
    pub longitude: HundredNanodegrees,
    /// `alt`: WGS84's, so above the ellipsoid.
    pub altitude: Centimeters<i32>,
    /// `posAcc`: the position's accuracy, a standard deviation.
    pub position_accuracy: Centimeters<u32>,
}

impl Encode for MgaIniPosLlh {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        encode_with(MGA_INI, INI_POS_LLH_LENGTH, out, |payload| {
            payload.u8(0, INI_POS_LLH_TYPE);
            payload.u8(1, INI_POS_LLH_VERSION);
            payload.i32(4, self.latitude.0);
            payload.i32(8, self.longitude.0);
            payload.i32(12, self.altitude.0);
            payload.u32(16, self.position_accuracy.0);
        })
    }
}
