//! Motorola Oncore binary framing, and the messages a `SmartClock` uses.
//!
//! A message is `@@`, a two-character ID, binary data, a checksum and
//! CR LF.  The ID implies the length, since the data may hold any byte,
//! `@` and CR LF included; the checksum is the XOR of the ID and the data
//! (M12+ User's Guide, `third_party/motorola/m12plus-users-guide.pdf`,
//! pp. 30-31 and the worked example on p. 34).  The same guide gives the
//! rule a receiver of messages follows: one is valid if it began with
//! `@@`, is the correct length for its type, its checksum validates, and
//! it ends in CR LF; any other is discarded.
//!
//! [`MESSAGES`] lists every message some `SmartClock` image sends or
//! decodes, and no other, with its length in each direction.
//!
//! A page cited here is the one a manual prints.  The VP's command
//! reference, the UT Plus's engineering notes and TRM0003's chapter 6
//! print their PDFs' own page numbers, the chapter's `6.28` being page 28
//! of `ch6.pdf`; the M12+ guide prints its PDF's page number less 8.
//!
//! # Names
//!
//! Each ID has a constant: its prefix is the ID in upper case, and the
//! rest says what the message is, after its manual's title.
//! [`AA_TIME_OF_DAY`] is `@@Aa`.  The prefix is not the ID, since an ID's
//! case matters: `@@Ab` and `@@AB`, `@@Ac` and `@@AC`, `@@Ad` and `@@AD`
//! are three pairs of different messages, and no other two IDs here
//! differ only in case.
//!
//! # Resynchronizing
//!
//! [`Deframer`] finds frames in a stream that may start mid-frame or
//! carry noise.  The guide says only that a bad message is discarded, so
//! where to pick the stream up again follows the firmware's own framer,
//! `gps_receive` (`docs/firmware/gps.md`, "Framing";
//! `third_party/firmware/rewritten/z3805a-3543b/gps_oncore.c`), except as
//! noted:
//!
//! - Outside a frame every byte but `@` is skipped, and a lone `@` too.
//!   No error is reported for them; the firmware counts a frame error for
//!   each.
//! - After `@@`, a further `@` is taken as the start's second, so `@@@Aa`
//!   frames `@@Aa`.
//! - The ID's first character must be an upper-case letter and its second
//!   a letter, as the firmware requires.  Any other byte is
//!   [`FrameError::BadId`], and a new start is looked for, from that byte
//!   if it is an `@`.
//! - An ID without a length is [`FrameError::UnknownId`], and a new `@@`
//!   is looked for from the next byte.  This is the guide's rule.  The
//!   firmware instead stays put and tries each further letter with the
//!   one before it as an ID, which no manual describes.
//! - Inside a frame nothing is looked for: an `@@` there is data.
//! - A bad checksum is [`FrameError::BadChecksum`], reported at the
//!   checksum byte, and the frame's bytes are not examined again.
//! - A wrong byte in place of CR or LF is [`FrameError::BadTerminator`],
//!   and a new start is looked for from that byte if it is an `@`.

use core::fmt;

use thiserror::Error;

use crate::Direction;
use crate::NoRoom;

/// A frame's first two bytes.
const START: [u8; 2] = *b"@@";
/// A frame's last two bytes.
const END: [u8; 2] = *b"\r\n";
/// Bytes in a message's ID.
const ID_BYTES: usize = 2;
/// Bytes before a frame's data: `@@` and the ID.
pub const HEADER: usize = START.len() + ID_BYTES;
/// Bytes after a frame's data: the checksum and CR LF.
pub const TRAILER: usize = size_of::<u8>() + END.len();
/// Bytes in a frame besides its data, so the shortest frame's length.
pub const OVERHEAD: usize = HEADER + TRAILER;

/// A message's two-character ID, the bytes after `@@`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct MessageId([u8; ID_BYTES]);

impl MessageId {
    /// The ID of these two bytes, which need not be one of [`MESSAGES`].
    #[must_use]
    pub const fn new(id: [u8; ID_BYTES]) -> Self {
        Self(id)
    }

    /// Its two bytes.
    #[must_use]
    pub const fn bytes(self) -> [u8; ID_BYTES] {
        self.0
    }

    /// Which form of a per-channel message this ID is, and
    /// [`Channels::Common`] for any other ID, one outside [`MESSAGES`]
    /// included.
    ///
    /// The per-channel IDs are named here and each row of [`MESSAGES`]
    /// takes its form from this, so asking does not read the table: a
    /// build that asks only this does not carry the table.
    #[must_use]
    pub const fn channels(self) -> Channels {
        match self {
            BA_POSITION_STATUS | BC_XDOP_TABLE | BK_EXTENSION | BN_TIME_RAIM | CA_SELF_TEST => {
                Channels::Six
            }
            EA_POSITION_STATUS | EK_EXTENSION | EN_TIME_RAIM | FA_SELF_TEST => Channels::Eight,
            _ => Channels::Common,
        }
    }

    /// Equality, for the table's lookups at compile time.
    const fn is(self, other: Self) -> bool {
        self.0[0] == other.0[0] && self.0[1] == other.0[1]
    }
}

/// As the manuals write it, `@@Aa`.
impl fmt::Display for MessageId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let [first, second] = self.0;
        write!(f, "@@{}{}", first.escape_ascii(), second.escape_ascii())
    }
}

// The IDs of the messages in `MESSAGES`, each named as "Names" in the
// module's documentation says, with its manual's title.

/// `@@Aa`, Time of Day.
pub const AA_TIME_OF_DAY: MessageId = MessageId::new(*b"Aa");
/// `@@Ab`, GMT Correction.
pub const AB_GMT_CORRECTION: MessageId = MessageId::new(*b"Ab");
/// `@@AB`, Application Type Select.
pub const AB_APPLICATION_TYPE: MessageId = MessageId::new(*b"AB");
/// `@@Ac`, Date.
pub const AC_DATE: MessageId = MessageId::new(*b"Ac");
/// `@@AC`, 2-D to 0-D HDOP Threshold.
pub const AC_HDOP_THRESHOLD: MessageId = MessageId::new(*b"AC");
/// `@@Ad`, Latitude.
pub const AD_LATITUDE: MessageId = MessageId::new(*b"Ad");
/// `@@AD`, Correction Thresholds.
pub const AD_CORRECTION_THRESHOLDS: MessageId = MessageId::new(*b"AD");
/// `@@Ae`, Longitude.
pub const AE_LONGITUDE: MessageId = MessageId::new(*b"Ae");
/// `@@Af`, Height.
pub const AF_HEIGHT: MessageId = MessageId::new(*b"Af");
/// `@@Ag`, Satellite Mask Angle.
pub const AG_MASK_ANGLE: MessageId = MessageId::new(*b"Ag");
/// `@@Ah`, Satellite Select Options.
pub const AH_SATELLITE_SELECT: MessageId = MessageId::new(*b"Ah");
/// `@@Ai`, Manual Satellite Select.
pub const AI_MANUAL_SATELLITE: MessageId = MessageId::new(*b"Ai");
/// `@@Aj`, xDOP Type.
pub const AJ_XDOP_TYPE: MessageId = MessageId::new(*b"Aj");
/// `@@Ak`, xDOP Hysteresis.
pub const AK_XDOP_HYSTERESIS: MessageId = MessageId::new(*b"Ak");
/// `@@Al`, 3-D to 2-D xDOP Threshold.
pub const AL_XDOP_THRESHOLD: MessageId = MessageId::new(*b"Al");
/// `@@Am`, Satellite Ignore List.
pub const AM_IGNORE_LIST: MessageId = MessageId::new(*b"Am");
/// `@@An`, Almanac Update Option.
pub const AN_ALMANAC_UPDATE: MessageId = MessageId::new(*b"An");
/// `@@Ao`, Datum Select.
pub const AO_DATUM_SELECT: MessageId = MessageId::new(*b"Ao");
/// `@@Ap`, Set User Defined Datum.
pub const AP_USER_DATUM: MessageId = MessageId::new(*b"Ap");
/// `@@Aq`, Atmospheric Correction Mode in TRM0003 (p. 6.28), which gives
/// it a tropospheric model too; Ionospheric Correction Mode in the VP's
/// reference (p. 50).
pub const AQ_ATMOSPHERIC_CORRECTION: MessageId = MessageId::new(*b"Aq");
/// `@@Ar`, Position Fix Algorithm Type.
pub const AR_FIX_ALGORITHM: MessageId = MessageId::new(*b"Ar");
/// `@@As`, Position-Hold Position.
pub const AS_HOLD_POSITION: MessageId = MessageId::new(*b"As");
/// `@@At`, Position-Hold Mode Select.
pub const AT_POSITION_HOLD: MessageId = MessageId::new(*b"At");
/// `@@Au`, Altitude-Hold Height.
pub const AU_ALTITUDE_HOLD_HEIGHT: MessageId = MessageId::new(*b"Au");
/// `@@Av`, Altitude-Hold Select.
pub const AV_ALTITUDE_HOLD: MessageId = MessageId::new(*b"Av");
/// `@@Aw`, UTC/GPS Time Mode Select.
pub const AW_TIME_MODE: MessageId = MessageId::new(*b"Aw");
/// `@@Ax`, Measurement Epoch Offset Option.
pub const AX_EPOCH_OFFSET: MessageId = MessageId::new(*b"Ax");
/// `@@Ay`, 1PPS Time Offset.
pub const AY_PULSE_OFFSET: MessageId = MessageId::new(*b"Ay");
/// `@@Az`, 1PPS Cable Delay.
pub const AZ_CABLE_DELAY: MessageId = MessageId::new(*b"Az");
/// `@@Ba`, 6 Channel Position/Status/Data Output Message.
pub const BA_POSITION_STATUS: MessageId = MessageId::new(*b"Ba");
/// `@@Bb`, Visible Satellite Status Message.
pub const BB_VISIBLE_SATELLITES: MessageId = MessageId::new(*b"Bb");
/// `@@Bc`, 6 Channel xDOP Table Status Message.
pub const BC_XDOP_TABLE: MessageId = MessageId::new(*b"Bc");
/// `@@Bd`, Almanac Status Message.
pub const BD_ALMANAC_STATUS: MessageId = MessageId::new(*b"Bd");
/// `@@Bh`, Pseudorange Correction Output Message.
pub const BH_CORRECTION_OUTPUT: MessageId = MessageId::new(*b"Bh");
/// `@@Bj`, Leap Second Pending Status.
pub const BJ_LEAP_SECOND_PENDING: MessageId = MessageId::new(*b"Bj");
/// `@@Bk`, 6 Channel Position/Status/Data Extension Message.
pub const BK_EXTENSION: MessageId = MessageId::new(*b"Bk");
/// `@@Bn`, 6 Channel Time RAIM Setup and Status Message.
pub const BN_TIME_RAIM: MessageId = MessageId::new(*b"Bn");
/// `@@Bo`, UTC Offset Status Message.
pub const BO_UTC_OFFSET: MessageId = MessageId::new(*b"Bo");
/// `@@Ca`, 6 Channel Self-Test.
pub const CA_SELF_TEST: MessageId = MessageId::new(*b"Ca");
/// `@@Ce`, Pseudorange Correction Input: input to an engine that applies
/// the corrections, and what one that makes them sends when asked with
/// `@@Bh`.
pub const CE_PSEUDORANGE_CORRECTION: MessageId = MessageId::new(*b"Ce");
/// `@@Cf`, Set-to-Defaults.
pub const CF_SET_TO_DEFAULTS: MessageId = MessageId::new(*b"Cf");
/// `@@Cg`, Position Fix/Idle Mode Select.
pub const CG_FIX_MODE: MessageId = MessageId::new(*b"Cg");
/// `@@Ci`, Switch I/O Format.
pub const CI_IO_FORMAT: MessageId = MessageId::new(*b"Ci");
/// `@@Cj`, Receiver ID Message.
pub const CJ_RECEIVER_ID: MessageId = MessageId::new(*b"Cj");
/// `@@Ea`, 8 Channel Position/Status/Data Message.
pub const EA_POSITION_STATUS: MessageId = MessageId::new(*b"Ea");
/// `@@Ek`, 8 Channel Position/Status/Data Extension Message.
pub const EK_EXTENSION: MessageId = MessageId::new(*b"Ek");
/// `@@En`, 8 Channel Time RAIM Setup and Status Message.
pub const EN_TIME_RAIM: MessageId = MessageId::new(*b"En");
/// `@@Fa`, 8 Channel Self-Test.
pub const FA_SELF_TEST: MessageId = MessageId::new(*b"Fa");
/// `@@Gj`, Leap Second Pending Message.
pub const GJ_LEAP_SECOND_PENDING: MessageId = MessageId::new(*b"Gj");
/// `@@Sz`, System Power-On Failure.
pub const SZ_POWER_ON_FAILURE: MessageId = MessageId::new(*b"Sz");
/// `@@Ta`, in no manual here.  Its three decoders fill the three values
/// `:DIAGnostic:GPSystem:DCXO?` returns
/// (`docs/firmware/oncore/58503b-1.01.04.txt`; `docs/scpi/undocumented.md`).
pub const TA_DCXO: MessageId = MessageId::new(*b"Ta");

/// Which form of a per-channel message an ID is
/// ([`MessageId::channels`]).  The engine reports
/// each channel in turn, so such a message has one ID and length for six
/// channels and another for eight.  No image's table holds a
/// twelve-channel message.
///
/// This describes the message, not what an engine accepts: a UT Plus has
/// eight channels, yet has no `@@Ek`, and has `@@Bo`, `@@Gj` and `@@Sz`,
/// which a VP of eight channels does not
/// (`third_party/motorola/ut-engg-notes.pdf` p. 11;
/// `docs/firmware/gps.md`, "The UT Plus").
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Channels {
    /// Not per-channel: one form, whatever the engine's channels.
    Common,
    /// The six-channel form.
    Six,
    /// The eight-channel form.
    Eight,
}

/// One message: a row of [`MESSAGES`].  Lengths are whole frames, `@@`
/// through LF, as the manuals give them.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Message {
    /// Its ID.
    pub id: MessageId,
    /// The length of the command a receiver sends its engine, `None` if
    /// no image sends one.
    pub command: Option<usize>,
    /// The length of the message the engine sends under this ID, `None`
    /// if it sends none.
    pub response: Option<usize>,
    /// Its per-channel form, if it has one: [`MessageId::channels`].
    pub channels: Channels,
}

impl Message {
    const fn new(id: MessageId, command: Option<usize>, response: Option<usize>) -> Self {
        Self {
            id,
            command,
            response,
            channels: id.channels(),
        }
    }

    /// Its length in one direction, a whole frame.
    #[must_use]
    pub const fn length(&self, direction: Direction) -> Option<usize> {
        match direction {
            Direction::ToEngine => self.command,
            Direction::FromEngine => self.response,
        }
    }

    /// The length of its data in one direction: the frame's, less
    /// [`OVERHEAD`].
    #[must_use]
    pub const fn data_length(&self, direction: Direction) -> Option<usize> {
        match self.length(direction) {
            Some(length) => Some(length - OVERHEAD),
            None => None,
        }
    }
}

/// Every message some `SmartClock` image sends or decodes.
///
/// Each row cites the manual page that gives its layout and length, then
/// the images whose message table holds it.  The manuals, all under
/// `third_party/motorola/`: VP, `VPCommands.pdf`, the VP Oncore's command
/// reference; UT, `oncore-ut-users-guide/ch6.pdf`, TRM0003 chapter 6;
/// M12+, `m12plus-users-guide.pdf`.  The images' tables are
/// `docs/firmware/oncore/<image>.txt`, set side by side in
/// `docs/firmware/oncore/models.md`; "every image" is every image with an
/// Oncore.  A test checks the response lengths against those tables.
pub const MESSAGES: &[Message] = &[
    // VP p. 6.  Every image.
    Message::new(AA_TIME_OF_DAY, Some(10), Some(10)),
    // VP p. 10.  Every image.
    Message::new(AB_GMT_CORRECTION, Some(10), Some(10)),
    // VP p. 12.  Every image.
    Message::new(AB_APPLICATION_TYPE, Some(8), Some(8)),
    // VP p. 14.  Every image.
    Message::new(AC_DATE, Some(11), Some(11)),
    // VP p. 16.  Every image.
    Message::new(AC_HDOP_THRESHOLD, Some(9), Some(9)),
    // VP p. 18.  Every image.
    Message::new(AD_LATITUDE, Some(11), Some(11)),
    // VP p. 20.  Every image.
    Message::new(AD_CORRECTION_THRESHOLDS, Some(9), Some(9)),
    // VP p. 22.  Every image.
    Message::new(AE_LONGITUDE, Some(11), Some(11)),
    // VP p. 26: the command sets one height, the response gives two.
    // Every image.
    Message::new(AF_HEIGHT, Some(12), Some(15)),
    // VP p. 28.  Every image.
    Message::new(AG_MASK_ANGLE, Some(8), Some(8)),
    // VP p. 30.  Every image.
    Message::new(AH_SATELLITE_SELECT, Some(8), Some(8)),
    // VP p. 32.  Every image.
    Message::new(AI_MANUAL_SATELLITE, Some(9), Some(9)),
    // VP p. 34.  Every image.
    Message::new(AJ_XDOP_TYPE, Some(8), Some(8)),
    // VP p. 38.  Every image.
    Message::new(AK_XDOP_HYSTERESIS, Some(9), Some(9)),
    // VP p. 40.  Every image.
    Message::new(AL_XDOP_THRESHOLD, Some(9), Some(9)),
    // VP p. 42.  Every image.
    Message::new(AM_IGNORE_LIST, Some(12), Some(12)),
    // VP p. 44.  Every image.
    Message::new(AN_ALMANAC_UPDATE, Some(8), Some(8)),
    // VP p. 46 and M12+ p. 83 print the response as `@@Ao`, UT p. 6.33
    // as `@@Ap`, 25 bytes either way.  Every image, and each takes only
    // `@@Ap`: its table gives `@@Ao` no length (`docs/firmware/gps.md`,
    // "Answer matching").
    Message::new(AO_DATUM_SELECT, Some(8), None),
    // VP pp. 48-49.  Every image, decoded only: none sends the command.
    Message::new(AP_USER_DATUM, None, Some(25)),
    // VP p. 51.  Every image.
    Message::new(AQ_ATMOSPHERIC_CORRECTION, Some(8), Some(8)),
    // VP p. 53.  Every image.
    Message::new(AR_FIX_ALGORITHM, Some(8), Some(8)),
    // VP p. 55.  Every image.
    Message::new(AS_HOLD_POSITION, Some(20), Some(20)),
    // VP p. 57.  Every image.
    Message::new(AT_POSITION_HOLD, Some(8), Some(8)),
    // VP p. 59.  Every image but the Z3801A's and Z3805A's.
    Message::new(AU_ALTITUDE_HOLD_HEIGHT, Some(12), Some(12)),
    // VP p. 61.  Every image.
    Message::new(AV_ALTITUDE_HOLD, Some(8), Some(8)),
    // VP p. 63.  Every image.
    Message::new(AW_TIME_MODE, Some(8), Some(8)),
    // VP p. 65.  Every image.
    Message::new(AX_EPOCH_OFFSET, Some(9), Some(9)),
    // VP p. 67.  Every image.
    Message::new(AY_PULSE_OFFSET, Some(11), Some(11)),
    // VP p. 69.  Every image.
    Message::new(AZ_CABLE_DELAY, Some(11), Some(11)),
    // VP pp. 71-73.  Every image.
    Message::new(BA_POSITION_STATUS, Some(8), Some(68)),
    // VP p. 75.  Every image.
    Message::new(BB_VISIBLE_SATELLITES, Some(8), Some(92)),
    // VP p. 77.  Every image.
    Message::new(BC_XDOP_TABLE, Some(8), Some(82)),
    // VP p. 79.  Every image.
    Message::new(BD_ALMANAC_STATUS, Some(8), Some(23)),
    // VP p. 90: answered by `@@Ce`, not by its own ID.  Every image, with
    // no length in its table.
    Message::new(BH_CORRECTION_OUTPUT, Some(8), None),
    // VP p. 94.  Every image.
    Message::new(BJ_LEAP_SECOND_PENDING, Some(8), Some(8)),
    // VP pp. 96-98.  Every image.
    Message::new(BK_EXTENSION, Some(8), Some(69)),
    // VP pp. 104-106.  Every image.
    Message::new(BN_TIME_RAIM, Some(22), Some(59)),
    // UT p. 6.81.  The 58503B's and Z3816A's.
    Message::new(BO_UTC_OFFSET, Some(8), Some(8)),
    // VP p. 108.  Every image.
    Message::new(CA_SELF_TEST, Some(7), Some(9)),
    // VP p. 115, and p. 90 for it as `@@Bh`'s answer.  Every image,
    // decoded only: none sends the command.
    Message::new(CE_PSEUDORANGE_CORRECTION, None, Some(52)),
    // VP p. 117.  Every image.
    Message::new(CF_SET_TO_DEFAULTS, Some(7), Some(7)),
    // VP p. 119, which says 7 bytes under a format, `@@CgmC<CR><LF>`, of
    // 8.  Every image, with 8 in its table.
    Message::new(CG_FIX_MODE, Some(8), Some(8)),
    // VP p. 121: no response.  Every image.
    Message::new(CI_IO_FORMAT, Some(8), None),
    // VP p. 123.  Every image.
    Message::new(CJ_RECEIVER_ID, Some(7), Some(294)),
    // VP pp. 125-126.  Every image but the Z3801A's and Z3805A's.
    Message::new(EA_POSITION_STATUS, Some(8), Some(76)),
    // VP pp. 135-137.  Every image but the Z3801A's and Z3805A's.
    Message::new(EK_EXTENSION, Some(8), Some(71)),
    // VP pp. 140-142.  Every image but the Z3801A's and Z3805A's.
    Message::new(EN_TIME_RAIM, Some(22), Some(69)),
    // VP p. 144.  Every image but the Z3801A's and Z3805A's.
    Message::new(FA_SELF_TEST, Some(7), Some(9)),
    // M12+ p. 153.  The 58503B's and Z3816A's.
    Message::new(GJ_LEAP_SECOND_PENDING, Some(7), Some(21)),
    // UT p. 6.39 and M12+ p. 177: 8 bytes, sent unasked.  The 58503B's,
    // Z3816A's and Z3817A's, whose tables give it 7.
    Message::new(SZ_POWER_ON_FAILURE, None, Some(8)),
    // In no manual here.  The 58503B's and Z3816A's: the response's
    // length is their tables', and the command's is the entry's query
    // form in the image, five bytes of data
    // (`docs/firmware/oncore/README.md`, "The message table", `+0x16`).
    Message::new(TA_DCXO, Some(12), Some(12)),
];

/// The row of [`MESSAGES`] for an ID.
#[must_use]
pub const fn message(id: MessageId) -> Option<&'static Message> {
    let mut index = 0;
    while index < MESSAGES.len() {
        if MESSAGES[index].id.is(id) {
            return Some(&MESSAGES[index]);
        }
        index += 1;
    }
    None
}

/// The length of a message in one direction, a whole frame.
///
/// [`MESSAGES`] is every engine's messages together, so this is a length
/// for any ID some engine has.  One engine has fewer: what it does with
/// an ID it lacks is decided after framing, by whoever reads the frames.
#[must_use]
pub const fn length(id: MessageId, direction: Direction) -> Option<usize> {
    match message(id) {
        Some(message) => message.length(direction),
        None => None,
    }
}

/// The length of a message's data in one direction: the frame's, less
/// [`OVERHEAD`].
#[must_use]
pub const fn data_length(id: MessageId, direction: Direction) -> Option<usize> {
    match message(id) {
        Some(message) => message.data_length(direction),
        None => None,
    }
}

/// The length of the command a receiver sends its engine, whichever
/// engine that is ([`length`]): the [`Lengths`] of a [`Deframer`] that
/// reads what a receiver sends.
#[must_use]
pub const fn command_length(id: MessageId) -> Option<usize> {
    length(id, Direction::ToEngine)
}

/// The length of the message an engine sends, whichever engine that is
/// ([`length`]): the [`Lengths`] of a [`Deframer`] that reads what an
/// engine sends.
#[must_use]
pub const fn response_length(id: MessageId) -> Option<usize> {
    length(id, Direction::FromEngine)
}

/// The longest message of [`MESSAGES`] in one direction.
const fn longest(direction: Direction) -> usize {
    let mut longest = 0;
    let mut index = 0;
    while index < MESSAGES.len() {
        if let Some(length) = MESSAGES[index].length(direction)
            && length > longest
        {
            longest = length;
        }
        index += 1;
    }
    longest
}

/// The longest command: room for a [`Deframer`] of commands.
pub const MAX_COMMAND: usize = longest(Direction::ToEngine);
/// The longest message an engine sends: room for a [`Deframer`] of them.
pub const MAX_RESPONSE: usize = longest(Direction::FromEngine);

/// The ID the engine answers a command under, `None` for a command it
/// does not answer or an ID that is no command.  It is the command's own
/// but for `@@Ao`, answered by `@@Ap`, and `@@Bh`, answered by `@@Ce`.
#[must_use]
pub const fn answer_id(id: MessageId) -> Option<MessageId> {
    let Some(message) = message(id) else {
        return None;
    };
    if message.command.is_none() {
        None
    } else if id.is(AO_DATUM_SELECT) {
        Some(AP_USER_DATUM)
    } else if id.is(BH_CORRECTION_OUTPUT) {
        Some(CE_PSEUDORANGE_CORRECTION)
    } else if message.response.is_some() {
        Some(id)
    } else {
        None
    }
}

/// A frame's checksum: the XOR of its ID and data.
#[must_use]
pub fn checksum(id: MessageId, data: &[u8]) -> u8 {
    id.bytes()
        .iter()
        .chain(data)
        .fold(0, |sum, byte| sum ^ byte)
}

/// Writes a frame into `out` and returns its length.  The data's length
/// is the caller's to get right: [`MESSAGES`] is not consulted, so an ID
/// outside it can be framed too.
///
/// # Errors
///
/// [`NoRoom`] if `out` is shorter than the frame.
pub fn encode(id: MessageId, data: &[u8], out: &mut [u8]) -> Result<usize, NoRoom> {
    let needed = OVERHEAD + data.len();
    let room = out.len();
    let frame = out.get_mut(..needed).ok_or(NoRoom { needed, room })?;
    let (header, rest) = frame.split_at_mut(HEADER);
    let (body, trailer) = rest.split_at_mut(data.len());
    let (start, id_bytes) = header.split_at_mut(START.len());
    let (sum, end) = trailer.split_at_mut(size_of::<u8>());
    start.copy_from_slice(&START);
    id_bytes.copy_from_slice(&id.bytes());
    body.copy_from_slice(data);
    sum.fill(checksum(id, data));
    end.copy_from_slice(&END);
    Ok(needed)
}

/// A whole frame with a good checksum, lent by a [`Deframer`] until its
/// next byte.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Frame<'a> {
    /// The frame, `@@` through LF.
    bytes: &'a [u8],
}

impl<'a> Frame<'a> {
    /// Its ID.
    #[must_use]
    pub fn id(&self) -> MessageId {
        MessageId([self.bytes[START.len()], self.bytes[START.len() + 1]])
    }

    /// Its data, between the ID and the checksum.
    #[must_use]
    pub fn data(&self) -> &'a [u8] {
        &self.bytes[HEADER..self.bytes.len() - TRAILER]
    }

    /// Its checksum.
    #[must_use]
    pub fn checksum(&self) -> u8 {
        self.bytes[self.bytes.len() - TRAILER]
    }

    /// The whole frame, `@@` through LF.
    #[must_use]
    pub fn bytes(&self) -> &'a [u8] {
        self.bytes
    }
}

/// Why a [`Deframer`] discarded what it was reading.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Error)]
pub enum FrameError {
    /// After `@@`, a byte that cannot be an ID's: the first not an
    /// upper-case letter, or the second not a letter.
    #[error("byte {0:#04x} after @@ is not part of a message ID")]
    BadId(u8),
    /// An ID the deframer's lengths do not hold, or hold with a length
    /// under [`OVERHEAD`].
    #[error("no length is known for {0}")]
    UnknownId(MessageId),
    /// A message longer than the deframer's buffer.
    #[error("{id} is {length} bytes, more than the buffer's {capacity}")]
    Overflow {
        /// The message.
        id: MessageId,
        /// Its length.
        length: usize,
        /// The buffer's, the deframer's `N`: both count the whole frame.
        capacity: usize,
    },
    /// A frame whose checksum is not the XOR of its ID and data.
    #[error("{id} has checksum {found:#04x}, not {expected:#04x}")]
    BadChecksum {
        /// The message.
        id: MessageId,
        /// The XOR of its ID and data.
        expected: u8,
        /// The checksum byte it carried.
        found: u8,
    },
    /// A frame with another byte where its CR or LF belongs.
    #[error("{id} has {found:#04x} in place of its CR LF")]
    BadTerminator {
        /// The message.
        id: MessageId,
        /// The byte in the CR's or LF's place.
        found: u8,
    },
}

/// Where a [`Deframer`] is in a frame.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum State {
    /// Outside a frame.
    Idle,
    /// After one `@`.
    Start,
    /// After `@@`: the ID's first character is next.
    IdFirst,
    /// After the ID's first character, held here.
    IdSecond(u8),
    /// In the data, or at the checksum, of a frame of this length.
    Body { length: usize },
    /// After a good checksum: CR is next.
    CarriageReturn { length: usize },
    /// After CR: LF is next.
    LineFeed { length: usize },
}

/// What one byte did to a [`Deframer`]: nothing yet, ended a frame of
/// some length, or an error.  [`Frame`] borrows the buffer, so the step
/// is taken first and the frame made from it after.
type Step = Option<Result<usize, FrameError>>;

/// A [`Deframer`]'s lengths: a message's whole length by its ID, `None`
/// for an ID it does not frame.  [`command_length`] reads what a receiver
/// sends and [`response_length`] what an engine sends; a caller may give
/// its own, a function or a closure that captures nothing.
pub type Lengths = fn(MessageId) -> Option<usize>;

/// Finds frames in a stream of bytes, however it is cut into chunks
/// ("Resynchronizing" in the module's documentation).
///
/// `N` is the buffer's size in bytes, and the buffer holds a whole frame,
/// not its data alone: [`HEADER`], the data and [`TRAILER`], `@@` through
/// LF.  So `N` is the length of the longest message to be framed, as
/// [`MESSAGES`] and the manuals give lengths: [`MAX_COMMAND`] or
/// [`MAX_RESPONSE`] for this module's two [`Lengths`].  The longest of
/// all is `@@Cj`'s response, 287 bytes of data:
///
/// ```
/// use smartclock_gps::oncore::CJ_RECEIVER_ID;
/// use smartclock_gps::oncore::HEADER;
/// use smartclock_gps::oncore::MAX_RESPONSE;
/// use smartclock_gps::oncore::TRAILER;
/// use smartclock_gps::oncore::response_length;
///
/// assert_eq!(HEADER + 287 + TRAILER, 4 + 287 + 3);
/// assert_eq!(MAX_RESPONSE, 294);
/// assert_eq!(response_length(CJ_RECEIVER_ID), Some(MAX_RESPONSE));
/// ```
///
/// A message longer than `N` is not framed: it is
/// [`FrameError::Overflow`].
///
/// The type has no parameter but `N` and [`Deframer::new`] is const, so
/// one can be a `static`'s field.
pub struct Deframer<const N: usize> {
    /// A message's whole length, by its ID.
    length: Lengths,
    state: State,
    /// The frame so far, from its `@@`.
    buffer: [u8; N],
    /// The bytes of `buffer` in use.
    filled: usize,
}

impl<const N: usize> fmt::Debug for Deframer<N> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Deframer")
            .field("state", &self.state)
            .field("buffer", &&self.buffer[..self.filled])
            .finish_non_exhaustive()
    }
}

impl<const N: usize> Deframer<N> {
    /// A deframer outside any frame.
    #[must_use]
    pub const fn new(length: Lengths) -> Self {
        Self {
            length,
            state: State::Idle,
            buffer: [0; N],
            filled: 0,
        }
    }

    /// Takes one byte, and returns a frame or an error if the byte ended
    /// one.
    pub fn push(&mut self, byte: u8) -> Option<Result<Frame<'_>, FrameError>> {
        let step = self.step(byte)?;
        Some(step.map(|length| self.frame(length)))
    }

    /// Takes bytes from the front of `chunk` up to the first that ends a
    /// frame or is an error, and returns how many it took and what that
    /// one ended.  With bytes left over, call again with the rest:
    ///
    /// ```
    /// use smartclock_gps::oncore::Deframer;
    /// use smartclock_gps::oncore::MAX_RESPONSE;
    /// use smartclock_gps::oncore::response_length;
    ///
    /// let mut deframer = Deframer::<MAX_RESPONSE>::new(response_length);
    /// let mut rest: &[u8] = b"noise@@Cf%\r\n@@Bj\x01)\r\n";
    /// let mut frames = 0;
    /// while !rest.is_empty() {
    ///     let (taken, event) = deframer.feed(rest);
    ///     rest = &rest[taken..];
    ///     if let Some(Ok(_frame)) = event {
    ///         frames += 1;
    ///     }
    /// }
    /// assert_eq!(frames, 2);
    /// ```
    pub fn feed(&mut self, chunk: &[u8]) -> (usize, Option<Result<Frame<'_>, FrameError>>) {
        let ended = chunk
            .iter()
            .enumerate()
            .find_map(|(index, &byte)| self.step(byte).map(|step| (index + 1, step)));
        match ended {
            Some((taken, step)) => (taken, Some(step.map(|length| self.frame(length)))),
            None => (chunk.len(), None),
        }
    }

    /// Forgets any frame in progress.
    pub fn reset(&mut self) {
        self.state = State::Idle;
    }

    fn frame(&self, length: usize) -> Frame<'_> {
        Frame {
            bytes: &self.buffer[..length],
        }
    }

    /// The ID of the frame in the buffer.
    fn id(&self) -> MessageId {
        MessageId([self.buffer[START.len()], self.buffer[START.len() + 1]])
    }

    fn store(&mut self, byte: u8) {
        self.buffer[self.filled] = byte;
        self.filled += 1;
    }

    /// Looks for a frame's start again, from this byte if it is an `@`.
    fn restart(&mut self, byte: u8) {
        self.state = if byte == START[0] {
            State::Start
        } else {
            State::Idle
        };
    }

    /// Starts a frame's body, its ID known.
    fn begin(&mut self, id: MessageId) -> Step {
        self.state = State::Idle;
        match (self.length)(id) {
            Some(length) if length > N => Some(Err(FrameError::Overflow {
                id,
                length,
                capacity: N,
            })),
            Some(length) if length >= OVERHEAD => {
                self.buffer[..START.len()].copy_from_slice(&START);
                self.buffer[START.len()..HEADER].copy_from_slice(&id.bytes());
                self.filled = HEADER;
                self.state = State::Body { length };
                None
            }
            _ => Some(Err(FrameError::UnknownId(id))),
        }
    }

    fn step(&mut self, byte: u8) -> Step {
        match self.state {
            State::Idle => {
                self.restart(byte);
                None
            }
            State::Start => {
                self.state = if byte == START[1] {
                    State::IdFirst
                } else {
                    State::Idle
                };
                None
            }
            State::IdFirst if byte == START[1] => None,
            State::IdFirst if byte.is_ascii_uppercase() => {
                self.state = State::IdSecond(byte);
                None
            }
            State::IdSecond(first) if byte.is_ascii_alphabetic() => {
                self.begin(MessageId([first, byte]))
            }
            State::IdFirst | State::IdSecond(_) => {
                self.restart(byte);
                Some(Err(FrameError::BadId(byte)))
            }
            State::Body { length } => {
                self.store(byte);
                if self.filled < length - END.len() {
                    return None;
                }
                let id = self.id();
                let expected = checksum(id, &self.buffer[HEADER..self.filled - size_of::<u8>()]);
                if byte == expected {
                    self.state = State::CarriageReturn { length };
                    None
                } else {
                    self.state = State::Idle;
                    Some(Err(FrameError::BadChecksum {
                        id,
                        expected,
                        found: byte,
                    }))
                }
            }
            State::CarriageReturn { length } if byte == END[0] => {
                self.store(byte);
                self.state = State::LineFeed { length };
                None
            }
            State::LineFeed { length } if byte == END[1] => {
                self.store(byte);
                self.state = State::Idle;
                Some(Ok(length))
            }
            State::CarriageReturn { .. } | State::LineFeed { .. } => {
                let id = self.id();
                self.restart(byte);
                Some(Err(FrameError::BadTerminator { id, found: byte }))
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use std::collections::BTreeMap;
    use std::collections::BTreeSet;
    use std::fs;
    use std::path::Path;
    use std::string::String;
    use std::string::ToString;
    use std::sync::Mutex;
    use std::vec::Vec;

    use super::AA_TIME_OF_DAY;
    use super::AB_APPLICATION_TYPE;
    use super::AB_GMT_CORRECTION;
    use super::AC_DATE;
    use super::AC_HDOP_THRESHOLD;
    use super::AD_CORRECTION_THRESHOLDS;
    use super::AD_LATITUDE;
    use super::AG_MASK_ANGLE;
    use super::AO_DATUM_SELECT;
    use super::AP_USER_DATUM;
    use super::AQ_ATMOSPHERIC_CORRECTION;
    use super::AS_HOLD_POSITION;
    use super::AW_TIME_MODE;
    use super::AY_PULSE_OFFSET;
    use super::AZ_CABLE_DELAY;
    use super::BA_POSITION_STATUS;
    use super::BC_XDOP_TABLE;
    use super::BH_CORRECTION_OUTPUT;
    use super::BJ_LEAP_SECOND_PENDING;
    use super::BK_EXTENSION;
    use super::BN_TIME_RAIM;
    use super::CA_SELF_TEST;
    use super::CE_PSEUDORANGE_CORRECTION;
    use super::CF_SET_TO_DEFAULTS;
    use super::CI_IO_FORMAT;
    use super::CJ_RECEIVER_ID;
    use super::Channels;
    use super::Deframer;
    use super::EA_POSITION_STATUS;
    use super::EK_EXTENSION;
    use super::EN_TIME_RAIM;
    use super::FA_SELF_TEST;
    use super::FrameError;
    use super::GJ_LEAP_SECOND_PENDING;
    use super::HEADER;
    use super::Lengths;
    use super::MAX_COMMAND;
    use super::MAX_RESPONSE;
    use super::MESSAGES;
    use super::MessageId;
    use super::OVERHEAD;
    use super::SZ_POWER_ON_FAILURE;
    use super::answer_id;
    use super::checksum;
    use super::command_length;
    use super::data_length;
    use super::encode;
    use super::length;
    use super::message;
    use super::response_length;
    use crate::Direction;
    use crate::NoRoom;

    /// What a deframer found, owned: a frame's ID and data, or an error.
    type Event = Result<(MessageId, Vec<u8>), FrameError>;

    /// Both directions.
    const DIRECTIONS: [Direction; 2] = [Direction::ToEngine, Direction::FromEngine];

    /// Bytes a frame's data must carry untouched: starts, ends, both
    /// extremes.
    const AWKWARD: &[u8] = b"@@\r\n@@Cf%\r\n\x00\xff";

    /// An ID in no table.
    const UNLISTED: MessageId = MessageId::new(*b"Zz");

    /// The Oncore images' message tables, from the crate's directory.
    const TABLES: &str = "../../docs/firmware/oncore";

    /// The length the images' tables give `@@Sz`, where both manuals
    /// give 8.
    const SZ_TABLE_LENGTH: usize = 7;

    /// A frame of `data`.
    fn framed(id: MessageId, data: &[u8]) -> Vec<u8> {
        let mut out = [0; MAX_RESPONSE];
        let length = encode(id, data, &mut out).expect("room");
        out[..length].to_vec()
    }

    /// `length` bytes of [`AWKWARD`], repeated.
    fn awkward(length: usize) -> Vec<u8> {
        AWKWARD.iter().copied().cycle().take(length).collect()
    }

    /// Everything a deframer of `N` bytes finds in the chunks, in order.
    fn deframe<const N: usize>(lengths: Lengths, chunks: &[&[u8]]) -> Vec<Event> {
        let mut deframer = Deframer::<N>::new(lengths);
        let mut events = Vec::new();
        for chunk in chunks {
            let mut rest = *chunk;
            while !rest.is_empty() {
                let (taken, event) = deframer.feed(rest);
                events.extend(event.map(|event| event.map(|f| (f.id(), f.data().to_vec()))));
                rest = &rest[taken..];
            }
        }
        events
    }

    /// The same, of an engine's messages in one chunk.
    fn responses(stream: &[u8]) -> Vec<Event> {
        deframe::<MAX_RESPONSE>(response_length, &[stream])
    }

    fn frame(id: MessageId, data: &[u8]) -> Event {
        Ok((id, data.to_vec()))
    }

    /// M12+ User's Guide p. 34: `@@Ha` with data 0x01 has checksum 0x28.
    #[test]
    fn checksum_is_the_guides_worked_example() {
        assert_eq!(checksum(MessageId::new(*b"Ha"), &[0x01]), 0x28);
    }

    /// The commands the M12+ User's Guide prints in hexadecimal, by its
    /// page: each encodes to those bytes, is the table's length, and
    /// deframes back.
    #[test]
    fn encodes_the_guides_examples() {
        let examples: [(MessageId, &[u8], &[u8]); 12] = [
            // p. 75.
            (
                AG_MASK_ANGLE,
                &[0xff],
                &[0x40, 0x40, 0x41, 0x67, 0xff, 0xd9, 0x0d, 0x0a],
            ),
            // p. 83.
            (
                AO_DATUM_SELECT,
                &[0xff],
                &[0x40, 0x40, 0x41, 0x6f, 0xff, 0xd1, 0x0d, 0x0a],
            ),
            // p. 91.
            (
                AQ_ATMOSPHERIC_CORRECTION,
                &[0xff],
                &[0x40, 0x40, 0x41, 0x71, 0xff, 0xcf, 0x0d, 0x0a],
            ),
            // p. 95.
            (
                AS_HOLD_POSITION,
                &[0xff; 13],
                &[
                    0x40, 0x40, 0x41, 0x73, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                    0xff, 0xff, 0xff, 0xff, 0xcd, 0x0d, 0x0a,
                ],
            ),
            // p. 99.
            (
                AW_TIME_MODE,
                &[0xff],
                &[0x40, 0x40, 0x41, 0x77, 0xff, 0xc9, 0x0d, 0x0a],
            ),
            // p. 101.
            (
                AY_PULSE_OFFSET,
                &[0xff; 4],
                &[
                    0x40, 0x40, 0x41, 0x79, 0xff, 0xff, 0xff, 0xff, 0x38, 0x0d, 0x0a,
                ],
            ),
            // p. 103.
            (
                AZ_CABLE_DELAY,
                &[0xff; 4],
                &[
                    0x40, 0x40, 0x41, 0x7a, 0xff, 0xff, 0xff, 0xff, 0x3b, 0x0d, 0x0a,
                ],
            ),
            // p. 115.
            (
                BJ_LEAP_SECOND_PENDING,
                &[0x00],
                &[0x40, 0x40, 0x42, 0x6a, 0x00, 0x28, 0x0d, 0x0a],
            ),
            // p. 125.
            (
                CF_SET_TO_DEFAULTS,
                &[],
                &[0x40, 0x40, 0x43, 0x66, 0x25, 0x0d, 0x0a],
            ),
            // p. 127.
            (
                CI_IO_FORMAT,
                &[0x01],
                &[0x40, 0x40, 0x43, 0x69, 0x01, 0x2b, 0x0d, 0x0a],
            ),
            // p. 129.
            (
                CJ_RECEIVER_ID,
                &[],
                &[0x40, 0x40, 0x43, 0x6a, 0x29, 0x0d, 0x0a],
            ),
            // p. 153.
            (
                GJ_LEAP_SECOND_PENDING,
                &[],
                &[0x40, 0x40, 0x47, 0x6a, 0x2d, 0x0d, 0x0a],
            ),
        ];
        for (id, data, bytes) in examples {
            assert_eq!(framed(id, data), bytes, "{id}");
            assert_eq!(command_length(id), Some(bytes.len()), "{id}");
            assert_eq!(
                deframe::<MAX_COMMAND>(command_length, &[bytes]),
                [frame(id, data)],
                "{id}"
            );
        }
    }

    #[test]
    fn encode_reports_a_short_buffer() {
        let mut out = [0; OVERHEAD];
        assert_eq!(
            encode(AA_TIME_OF_DAY, &[1, 2, 3], &mut out),
            Err(NoRoom {
                needed: 10,
                room: OVERHEAD
            })
        );
        assert_eq!(encode(CF_SET_TO_DEFAULTS, &[], &mut out), Ok(OVERHEAD));
    }

    /// Every message, in each direction it travels, encodes to its
    /// table's length and deframes to the same ID, data and bytes, with
    /// starts and ends in its data.
    #[test]
    fn every_message_round_trips() {
        for row in MESSAGES {
            for direction in DIRECTIONS {
                let (Some(whole), Some(data)) = (row.length(direction), row.data_length(direction))
                else {
                    continue;
                };
                let lengths: Lengths = match direction {
                    Direction::ToEngine => command_length,
                    Direction::FromEngine => response_length,
                };
                let data = awkward(data);
                let bytes = framed(row.id, &data);
                assert_eq!(bytes.len(), whole, "{}", row.id);

                let mut deframer = Deframer::<MAX_RESPONSE>::new(lengths);
                let (last, body) = bytes.split_last().expect("a frame");
                assert!(
                    body.iter().all(|&byte| deframer.push(byte).is_none()),
                    "{}",
                    row.id
                );
                let found = deframer
                    .push(*last)
                    .expect("a frame's end")
                    .expect("a good frame");
                assert_eq!(found.id(), row.id);
                assert_eq!(found.data(), data);
                assert_eq!(found.checksum(), checksum(row.id, &data));
                assert_eq!(found.bytes(), bytes);
            }
        }
    }

    /// A stream of frames, noise and errors gives the same events cut in
    /// two at any byte, and fed a byte at a time.
    #[test]
    fn any_split_gives_the_same_events() {
        let spoiled = {
            let mut bytes = framed(BJ_LEAP_SECOND_PENDING, &[1]);
            bytes[HEADER] ^= 0xff;
            bytes
        };
        let stream = [
            b"\r\n@noise@".to_vec(),
            framed(EN_TIME_RAIM, &awkward(62)),
            framed(CF_SET_TO_DEFAULTS, &[]),
            b"@@@".to_vec(),
            framed(AA_TIME_OF_DAY, &[23, 59, 59]),
            spoiled,
            b"@@Zz@@1".to_vec(),
            framed(CJ_RECEIVER_ID, &awkward(287)),
            b"@@Cf%\r".to_vec(),
            framed(AP_USER_DATUM, &[0x40; 18]),
        ]
        .concat();
        let whole = responses(&stream);
        assert_eq!(
            whole,
            [
                frame(EN_TIME_RAIM, &awkward(62)),
                frame(CF_SET_TO_DEFAULTS, &[]),
                frame(AA_TIME_OF_DAY, &[23, 59, 59]),
                Err(FrameError::BadChecksum {
                    id: BJ_LEAP_SECOND_PENDING,
                    expected: 0xd6,
                    found: 0x29
                }),
                Err(FrameError::UnknownId(UNLISTED)),
                Err(FrameError::BadId(b'1')),
                frame(CJ_RECEIVER_ID, &awkward(287)),
                Err(FrameError::BadTerminator {
                    id: CF_SET_TO_DEFAULTS,
                    found: b'@'
                }),
                frame(AP_USER_DATUM, &[0x40; 18]),
            ]
        );
        for cut in 0..=stream.len() {
            let (head, tail) = stream.split_at(cut);
            assert_eq!(
                deframe::<MAX_RESPONSE>(response_length, &[head, tail]),
                whole,
                "cut {cut}"
            );
        }
        let singly: Vec<&[u8]> = stream.chunks(1).collect();
        assert_eq!(deframe::<MAX_RESPONSE>(response_length, &singly), whole);
    }

    #[test]
    fn noise_and_lone_ats_are_skipped_silently() {
        let stream = [
            b"\x00\xffAa@ @\r\n".to_vec(),
            framed(CF_SET_TO_DEFAULTS, &[]),
        ]
        .concat();
        assert_eq!(responses(&stream), [frame(CF_SET_TO_DEFAULTS, &[])]);
        assert_eq!(responses(b"no frame here @ all"), []);
    }

    #[test]
    fn a_third_at_slides_the_start() {
        let stream = [b"@@@@".to_vec(), framed(CF_SET_TO_DEFAULTS, &[])].concat();
        assert_eq!(responses(&stream), [frame(CF_SET_TO_DEFAULTS, &[])]);
    }

    /// A frame whose data holds a whole frame is one frame.
    #[test]
    fn a_frame_inside_data_is_data() {
        let inner = framed(CF_SET_TO_DEFAULTS, &[]);
        let data = [inner.as_slice(), &[0; 52 - 7]].concat();
        let stream = framed(BN_TIME_RAIM, &data);
        assert_eq!(responses(&stream), [frame(BN_TIME_RAIM, &data)]);
    }

    /// A byte that cannot be an ID's is an error, and a frame that starts
    /// at it is found.
    #[test]
    fn a_bad_id_restarts_from_an_at() {
        let good = framed(CF_SET_TO_DEFAULTS, &[]);
        let after_first = [b"@@1".as_slice(), &good].concat();
        assert_eq!(
            responses(&after_first),
            [Err(FrameError::BadId(b'1')), frame(CF_SET_TO_DEFAULTS, &[])]
        );
        let lower_case = [b"@@cf".as_slice(), &good].concat();
        assert_eq!(
            responses(&lower_case),
            [Err(FrameError::BadId(b'c')), frame(CF_SET_TO_DEFAULTS, &[])]
        );
        let after_second = [b"@@C".as_slice(), &good].concat();
        assert_eq!(
            responses(&after_second),
            [Err(FrameError::BadId(b'@')), frame(CF_SET_TO_DEFAULTS, &[])]
        );
    }

    /// An ID without a length is an error, and so is one whose length is
    /// under a frame's overhead; the next frame is found.
    #[test]
    fn an_unknown_id_waits_for_the_next_start() {
        let good = framed(CF_SET_TO_DEFAULTS, &[]);
        let stream = [b"@@Zz".as_slice(), &good].concat();
        assert_eq!(
            responses(&stream),
            [
                Err(FrameError::UnknownId(UNLISTED)),
                frame(CF_SET_TO_DEFAULTS, &[])
            ]
        );
        // `@@Ao` has no response.
        let unanswered = [framed(AO_DATUM_SELECT, &[1]), good].concat();
        assert_eq!(
            responses(&unanswered),
            [
                Err(FrameError::UnknownId(AO_DATUM_SELECT)),
                frame(CF_SET_TO_DEFAULTS, &[])
            ]
        );
        assert_eq!(
            deframe::<MAX_COMMAND>(|_| Some(OVERHEAD - 1), &[b"@@Cf"]),
            [Err(FrameError::UnknownId(CF_SET_TO_DEFAULTS))]
        );
    }

    #[test]
    fn a_bad_checksum_is_reported_once() {
        let stream = [b"@@Cf\x00\r\n".to_vec(), framed(CF_SET_TO_DEFAULTS, &[])].concat();
        assert_eq!(
            responses(&stream),
            [
                Err(FrameError::BadChecksum {
                    id: CF_SET_TO_DEFAULTS,
                    expected: 0x25,
                    found: 0
                }),
                frame(CF_SET_TO_DEFAULTS, &[])
            ]
        );
    }

    /// A wrong byte for CR or LF is an error, and a frame that starts at
    /// it is found.
    #[test]
    fn a_bad_terminator_restarts_from_an_at() {
        let good = framed(CF_SET_TO_DEFAULTS, &[]);
        for cut in [b"@@Cf%".as_slice(), b"@@Cf%\r"] {
            let stream = [cut, &good].concat();
            assert_eq!(
                responses(&stream),
                [
                    Err(FrameError::BadTerminator {
                        id: CF_SET_TO_DEFAULTS,
                        found: b'@'
                    }),
                    frame(CF_SET_TO_DEFAULTS, &[])
                ]
            );
        }
        assert_eq!(
            responses(b"@@Cf%\n\r"),
            [Err(FrameError::BadTerminator {
                id: CF_SET_TO_DEFAULTS,
                found: b'\n'
            })]
        );
    }

    /// A message longer than the buffer is an error, and one that just
    /// fits is framed.
    #[test]
    fn a_message_longer_than_the_buffer_overflows() {
        const ROOM: usize = 10;
        let stream = [
            framed(CJ_RECEIVER_ID, &[0; 287]),
            framed(AA_TIME_OF_DAY, &[1, 2, 3]),
            framed(CF_SET_TO_DEFAULTS, &[]),
        ]
        .concat();
        assert_eq!(
            deframe::<ROOM>(response_length, &[&stream]),
            [
                Err(FrameError::Overflow {
                    id: CJ_RECEIVER_ID,
                    length: 294,
                    capacity: ROOM
                }),
                frame(AA_TIME_OF_DAY, &[1, 2, 3]),
                frame(CF_SET_TO_DEFAULTS, &[])
            ]
        );
    }

    #[test]
    fn reset_forgets_a_frame_in_progress() {
        let mut deframer = Deframer::<MAX_RESPONSE>::new(response_length);
        let good = framed(CF_SET_TO_DEFAULTS, &[]);
        assert_eq!(deframer.feed(b"@@Cj"), (4, None));
        deframer.reset();
        let (taken, event) = deframer.feed(&good);
        assert_eq!(taken, good.len());
        assert_eq!(
            event.expect("an event").expect("a frame").id(),
            CF_SET_TO_DEFAULTS
        );
    }

    #[test]
    fn errors_and_ids_display() {
        assert_eq!(AA_TIME_OF_DAY.to_string(), "@@Aa");
        assert_eq!(MessageId::new([b'A', 0]).to_string(), "@@A\\x00");
        assert_eq!(
            FrameError::BadChecksum {
                id: CF_SET_TO_DEFAULTS,
                expected: 0x25,
                found: 0
            }
            .to_string(),
            "@@Cf has checksum 0x00, not 0x25"
        );
        assert_eq!(
            NoRoom {
                needed: 10,
                room: 7
            }
            .to_string(),
            "a frame of 10 bytes does not fit in 7"
        );
    }

    /// The table is well formed: IDs the deframer can frame, each once,
    /// each traveling some way at a frame's length or more; and the
    /// buffer sizes are its longest messages'.
    #[test]
    fn the_table_is_consistent() {
        let ids: BTreeSet<[u8; 2]> = MESSAGES.iter().map(|row| row.id.bytes()).collect();
        assert_eq!(ids.len(), MESSAGES.len(), "an ID twice");
        for row in MESSAGES {
            let [first, second] = row.id.bytes();
            assert!(
                first.is_ascii_uppercase() && second.is_ascii_alphabetic(),
                "{}",
                row.id
            );
            assert!(
                row.command.is_some() || row.response.is_some(),
                "{}",
                row.id
            );
            for direction in DIRECTIONS {
                assert!(
                    row.length(direction).is_none_or(|l| l >= OVERHEAD),
                    "{}",
                    row.id
                );
                assert_eq!(
                    length(row.id, direction),
                    row.length(direction),
                    "{}",
                    row.id
                );
                assert_eq!(
                    data_length(row.id, direction).map(|data| data + OVERHEAD),
                    row.length(direction),
                    "{}",
                    row.id
                );
            }
            assert_eq!(message(row.id), Some(row));
        }
        assert_eq!(message(UNLISTED), None);
        assert_eq!(command_length(UNLISTED), None);
        assert_eq!(data_length(UNLISTED, Direction::ToEngine), None);
        assert_eq!(
            data_length(CF_SET_TO_DEFAULTS, Direction::ToEngine),
            Some(0)
        );
        assert_eq!(Some(MAX_COMMAND), command_length(EN_TIME_RAIM));
        assert_eq!(Some(MAX_RESPONSE), response_length(CJ_RECEIVER_ID));
    }

    /// The IDs that differ only in case are the three pairs the module's
    /// documentation names, so a constant's prefix tells every other two
    /// apart.
    #[test]
    fn three_pairs_of_ids_differ_only_in_case() {
        let upper: Vec<[u8; 2]> = MESSAGES
            .iter()
            .map(|row| row.id.bytes().map(|byte| byte.to_ascii_uppercase()))
            .collect();
        let shared: BTreeSet<[u8; 2]> = upper
            .iter()
            .filter(|id| upper.iter().filter(|other| other == id).count() > 1)
            .copied()
            .collect();
        assert_eq!(shared, BTreeSet::from([*b"AB", *b"AC", *b"AD"]));
        assert_ne!(AB_GMT_CORRECTION, AB_APPLICATION_TYPE);
        assert_ne!(AC_DATE, AC_HDOP_THRESHOLD);
        assert_ne!(AD_LATITUDE, AD_CORRECTION_THRESHOLDS);
    }

    /// A deframer as a `static`: its type can be written out and its
    /// constructor is const.
    static COMMANDS: Mutex<Deframer<MAX_COMMAND>> = Mutex::new(Deframer::new(command_length));

    #[test]
    fn a_deframer_can_be_a_static() {
        let bytes = framed(CF_SET_TO_DEFAULTS, &[]);
        let mut deframer = COMMANDS.lock().expect("the lock");
        let (taken, event) = deframer.feed(&bytes);
        assert_eq!(taken, bytes.len());
        assert_eq!(
            event.map(|event| event.map(|frame| frame.id())),
            Some(Ok(CF_SET_TO_DEFAULTS))
        );
    }

    /// Each per-channel message's eight-channel form is its six-channel
    /// one with the first letter three on, the same command and a longer
    /// or equal response; `@@Bc` alone has no eight-channel form here.
    #[test]
    fn six_and_eight_channel_forms_pair() {
        const LETTERS_APART: u8 = b'E' - b'B';
        let eights: Vec<_> = MESSAGES
            .iter()
            .filter(|r| r.channels == Channels::Eight)
            .collect();
        assert!(!eights.is_empty());
        for eight in &eights {
            let [first, second] = eight.id.bytes();
            let six = message(MessageId::new([first - LETTERS_APART, second])).expect("a pair");
            assert_eq!(six.channels, Channels::Six, "{}", six.id);
            assert_eq!(six.command, eight.command, "{}", six.id);
            assert!(six.response <= eight.response, "{}", six.id);
        }
        let sixes = MESSAGES
            .iter()
            .filter(|r| r.channels == Channels::Six)
            .count();
        assert_eq!(sixes, eights.len() + 1);
    }

    /// An ID's form is its row's, for every row, without the table: the
    /// per-channel IDs are the nine whose manual titles say six or eight
    /// channels, and every other pair of bytes is common, in the table or
    /// not.
    #[test]
    fn an_ids_channels_are_its_rows() {
        let of_form = |form: Channels| -> Vec<MessageId> {
            MESSAGES
                .iter()
                .filter(|row| row.channels == form)
                .map(|row| row.id)
                .collect()
        };
        assert_eq!(
            of_form(Channels::Six),
            [
                BA_POSITION_STATUS,
                BC_XDOP_TABLE,
                BK_EXTENSION,
                BN_TIME_RAIM,
                CA_SELF_TEST
            ]
        );
        assert_eq!(
            of_form(Channels::Eight),
            [EA_POSITION_STATUS, EK_EXTENSION, EN_TIME_RAIM, FA_SELF_TEST]
        );
        for first in u8::MIN..=u8::MAX {
            for second in u8::MIN..=u8::MAX {
                let id = MessageId::new([first, second]);
                let row = message(id).map_or(Channels::Common, |row| row.channels);
                assert_eq!(id.channels(), row, "{id}");
            }
        }
        const { assert!(matches!(EA_POSITION_STATUS.channels(), Channels::Eight)) };
    }

    /// A command's answer is a message the engine sends.
    #[test]
    fn answers_are_messages_the_engine_sends() {
        for row in MESSAGES {
            match answer_id(row.id) {
                Some(id) => {
                    assert!(row.command.is_some(), "{}", row.id);
                    assert!(response_length(id).is_some(), "{}", row.id);
                }
                None => assert!(
                    row.command.is_none() || row.response.is_none(),
                    "{}",
                    row.id
                ),
            }
        }
        assert_eq!(answer_id(AO_DATUM_SELECT), Some(AP_USER_DATUM));
        assert_eq!(
            answer_id(BH_CORRECTION_OUTPUT),
            Some(CE_PSEUDORANGE_CORRECTION)
        );
        assert_eq!(answer_id(CI_IO_FORMAT), None);
        assert_eq!(answer_id(AA_TIME_OF_DAY), Some(AA_TIME_OF_DAY));
        assert_eq!(answer_id(AP_USER_DATUM), None);
        assert_eq!(answer_id(UNLISTED), None);
    }

    /// Each ID's lengths in the images' tables, by image, from
    /// `docs/firmware/oncore/<image>.txt`: lines of
    /// `message <index> <ID> kind <kind> length <length> ...`.
    fn table_lengths() -> BTreeMap<String, BTreeSet<usize>> {
        let directory = Path::new(env!("CARGO_MANIFEST_DIR")).join(TABLES);
        let mut lengths: BTreeMap<String, BTreeSet<usize>> = BTreeMap::new();
        let tables = fs::read_dir(&directory)
            .expect("the tables' directory")
            .map(|entry| entry.expect("a directory entry").path())
            .filter(|path| path.extension().is_some_and(|extension| extension == "txt"));
        for table in tables {
            let text = fs::read_to_string(&table).expect("a table");
            for line in text.lines() {
                let words: Vec<&str> = line.split_whitespace().collect();
                if let ["message", _, id, "kind", _, "length", length, ..] = words.as_slice() {
                    lengths
                        .entry((*id).to_string())
                        .or_default()
                        .insert(length.parse().expect("a length"));
                }
            }
        }
        lengths
    }

    /// The table holds the IDs of the images' message tables and no
    /// other, and each response is the length the tables frame it at: all
    /// images agree on each, a message they give no length has no
    /// response, and `@@Sz` is the one they frame short of the manuals'.
    #[test]
    fn the_table_matches_the_images_tables() {
        let tables = table_lengths();
        let ours: BTreeSet<String> = MESSAGES
            .iter()
            .map(|row| String::from_utf8_lossy(&row.id.bytes()).into())
            .collect();
        let theirs: BTreeSet<String> = tables.keys().cloned().collect();
        assert_eq!(ours, theirs);
        for row in MESSAGES {
            let name: String = String::from_utf8_lossy(&row.id.bytes()).into();
            let framed: Vec<usize> = tables[&name].iter().copied().collect();
            let expected = if row.id == SZ_POWER_ON_FAILURE {
                assert_eq!(row.response, Some(SZ_TABLE_LENGTH + 1));
                SZ_TABLE_LENGTH
            } else {
                row.response.unwrap_or(0)
            };
            assert_eq!(framed, [expected], "{}", row.id);
        }
    }
}
