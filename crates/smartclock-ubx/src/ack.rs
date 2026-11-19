//! The acknowledgments, `UBX-ACK-*`: a receiver's answer to a
//! configuration message sent it.

use crate::fields::Fields;
use crate::frame::MessageId;

/// `UBX-ACK-ACK` (section 32.8.1, p. 179) or `UBX-ACK-NAK` (section
/// 32.8.2, p. 179): the message acknowledged or refused.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Ack {
    /// `clsID` and `msgID`.
    pub message: MessageId,
}

impl Ack {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        Self {
            message: MessageId::new(fields.u8(0), fields.u8(1)),
        }
    }
}
