//! The configuration messages, `UBX-CFG-*`: what a caller sends a
//! receiver to set it up, each written whole into a frame, and the
//! answers to two of their polls, decoded.
//!
//! Each struct's [`Encode`] writes the fields it holds, as they are: which
//! values to send is the caller's.  A reserved field is written zero
//! (section 32.3.2, p. 169); a reserved bit of a bit field is the
//! caller's, since a bit field is kept as its bits.  The receiver
//! answers each with `UBX-ACK-ACK` or `UBX-ACK-NAK` (section 32.5.1, p.
//! 172), which [`message_id`](crate::frame::message_id) of the frame
//! sent matches to it.

use crate::fields::Fields;
use crate::frame::Encode;
use crate::frame::EncodeError;
use crate::frame::MessageId;
use crate::frame::encode_with;
use crate::message::CFG_ANT;
use crate::message::CFG_CFG;
use crate::message::CFG_DAT;
use crate::message::CFG_GNSS;
use crate::message::CFG_MSG;
use crate::message::CFG_NAV5;
use crate::message::CFG_NAVX5;
use crate::message::CFG_PRT;
use crate::message::CFG_RATE;
use crate::message::CFG_RXM;
use crate::message::CFG_TMODE2;
use crate::message::CFG_TP5;
use crate::units::Centimeters;
use crate::units::CentimetersPerSecond;
use crate::units::DecibelHertz;
use crate::units::Degrees;
use crate::units::F32Bits;
use crate::units::F64Bits;
use crate::units::Meters;
use crate::units::Millimeters;
use crate::units::Milliseconds;
use crate::units::Nanoseconds;
use crate::units::Seconds;
use crate::units::SquareCentimeters;
use crate::units::Tenths;

/// [`CfgPrt`]'s payload length, its section's "Message Structure", as
/// for each length below: the frame is
/// [`OVERHEAD`](crate::frame::OVERHEAD) more.
pub const PRT_LENGTH: usize = 20;
/// [`CfgPrtPoll`]'s.
pub const PRT_POLL_LENGTH: usize = 1;
/// [`CfgMsg`]'s.
pub const MSG_LENGTH: usize = 3;
/// [`CfgMsgPoll`]'s.
pub const MSG_POLL_LENGTH: usize = 2;
/// [`CfgAnt`]'s.
pub const ANT_LENGTH: usize = 4;
/// [`CfgRate`]'s.
pub const RATE_LENGTH: usize = 6;
/// [`CfgRxm`]'s.
pub const RXM_LENGTH: usize = 2;
/// [`CfgNav5`]'s.
pub const NAV5_LENGTH: usize = 36;
/// [`CfgNavx5`]'s.
pub const NAVX5_LENGTH: usize = 40;
/// [`CfgDat`]'s.
pub const DAT_LENGTH: usize = 44;
/// [`CfgCfg`]'s.
pub const CFG_LENGTH: usize = 13;
/// [`CfgTmode2`]'s.
pub const TMODE2_LENGTH: usize = 28;
/// [`CfgTp5`]'s.
pub const TP5_LENGTH: usize = 32;
/// [`CfgTp5Poll`]'s.
pub const TP5_POLL_LENGTH: usize = 1;
/// `CFG-GNSS`'s payload before its first block, and each block's
/// (p. 208).
const GNSS_HEADER: usize = 4;
const GNSS_BLOCK: usize = 8;

/// The `version` this crate writes and decodes of `CFG-TP5` (section
/// 32.10.38.4, p. 272) and `CFG-NAVX5` (section 32.10.20.2, p. 221),
/// and `CFG-GNSS`'s `msgVer` (p. 208).
const TP5_VERSION: u8 = 1;
const NAVX5_VERSION: u16 = 2;
const GNSS_VERSION: u8 = 0;

/// The length of a `CFG-GNSS` payload of `blocks` systems: [`CfgGnss`]'s
/// length, which grows with its blocks.
#[must_use]
pub const fn cfg_gnss_length(blocks: usize) -> usize {
    GNSS_HEADER + GNSS_BLOCK * blocks
}

bit_field_type! {
    /// `CFG-PRT`'s `txReady` (the bitfield graphic, p. 244): the TX
    /// ready pin.
    TxReady(u16)
}

/// `txReady`'s `pin` and `thres` fields: bits 2 to 6 and 7 to 15.
const TX_READY_PIN_SHIFT: u32 = 2;
const TX_READY_PIN_MASK: u16 = 0x1f;
const TX_READY_THRESHOLD_SHIFT: u32 = 7;
const TX_READY_THRESHOLD_MASK: u16 = 0x1ff;

impl TxReady {
    /// `en`: the feature enabled for the port.
    pub const ENABLE: Self = Self(1 << 0);
    /// `pol` set: low-active.  Clear is high-active.
    pub const LOW_ACTIVE: Self = Self(1 << 1);

    /// `pin`: the PIO to use, its low five bits.
    #[must_use]
    pub const fn pin(pin: u8) -> Self {
        Self((pin as u16 & TX_READY_PIN_MASK) << TX_READY_PIN_SHIFT)
    }

    /// `thres`: the pin goes active at this many times 8 bytes pending,
    /// its low nine bits.
    #[must_use]
    pub const fn threshold(eights: u16) -> Self {
        Self((eights & TX_READY_THRESHOLD_MASK) << TX_READY_THRESHOLD_SHIFT)
    }
}

bit_field_type! {
    /// `CFG-PRT`'s `mode` (the bitfield graphic, p. 245): a UART's
    /// character framing, a value of each of its three fields.
    UartMode(u32)
}

/// `mode`'s fields: `charLen` bits 6 and 7, `parity` bits 9 to 11,
/// `nStopBits` bits 12 and 13.
const CHARACTER_SHIFT: u32 = 6;
const PARITY_SHIFT: u32 = 9;
const STOP_SHIFT: u32 = 12;

impl UartMode {
    /// `charLen` 00: 5 bits, which the receiver does not support.
    pub const FIVE_BITS: Self = Self(0b00 << CHARACTER_SHIFT);
    /// `charLen` 01: 6 bits, which the receiver does not support.
    pub const SIX_BITS: Self = Self(0b01 << CHARACTER_SHIFT);
    /// `charLen` 10: 7 bits, only with parity.
    pub const SEVEN_BITS: Self = Self(0b10 << CHARACTER_SHIFT);
    /// `charLen` 11: 8 bits.
    pub const EIGHT_BITS: Self = Self(0b11 << CHARACTER_SHIFT);
    /// `parity` 000: even.
    pub const EVEN_PARITY: Self = Self(0b000 << PARITY_SHIFT);
    /// `parity` 001: odd.
    pub const ODD_PARITY: Self = Self(0b001 << PARITY_SHIFT);
    /// `parity` 10X: none.
    pub const NO_PARITY: Self = Self(0b100 << PARITY_SHIFT);
    /// `nStopBits` 00: 1 stop bit.
    pub const ONE_STOP_BIT: Self = Self(0b00 << STOP_SHIFT);
    /// `nStopBits` 01: 1.5 stop bits.
    pub const ONE_AND_HALF_STOP_BITS: Self = Self(0b01 << STOP_SHIFT);
    /// `nStopBits` 10: 2 stop bits.
    pub const TWO_STOP_BITS: Self = Self(0b10 << STOP_SHIFT);
    /// `nStopBits` 11: 0.5 stop bit.
    pub const HALF_STOP_BIT: Self = Self(0b11 << STOP_SHIFT);
}

bit_field_type! {
    /// `CFG-PRT`'s `inProtoMask` and `outProtoMask` (the bitfield
    /// graphics, pp. 245-246): the protocols a port takes or sends.
    Protocols(u16)
}

impl Protocols {
    /// `inUbx`, `outUbx`.
    pub const UBX: Self = Self(1 << 0);
    /// `inNmea`, `outNmea`.
    pub const NMEA: Self = Self(1 << 1);
    /// `inRtcm`, RTCM2: input only.
    pub const RTCM: Self = Self(1 << 2);
    /// `inRtcm3`, `outRtcm3`.
    pub const RTCM3: Self = Self(1 << 5);
}

bit_field_type! {
    /// `CFG-PRT`'s `flags` (the bitfield graphic, p. 246).
    PortFlags(u16)
}

impl PortFlags {
    /// `extendedTxTimeout`.
    pub const EXTENDED_TX_TIMEOUT: Self = Self(1 << 1);
}

/// `UBX-CFG-PRT` for a UART (section 32.10.25.2, pp. 243-246): a
/// port's configuration.  The receiver may change its baud rate before
/// its acknowledgment.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CfgPrt {
    /// `portID`: which UART.
    pub port_id: u8,
    /// `txReady`.
    pub tx_ready: TxReady,
    /// `mode`.
    pub mode: UartMode,
    /// `baudRate`, in bits a second.
    pub baud_rate: u32,
    /// `inProtoMask`.
    pub input_protocols: Protocols,
    /// `outProtoMask`.
    pub output_protocols: Protocols,
    /// `flags`.
    pub flags: PortFlags,
}

impl Encode for CfgPrt {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        encode_with(CFG_PRT, PRT_LENGTH, out, |payload| {
            payload.u8(0, self.port_id);
            payload.u16(2, self.tx_ready.0);
            payload.u32(4, self.mode.0);
            payload.u32(8, self.baud_rate);
            payload.u16(12, self.input_protocols.0);
            payload.u16(14, self.output_protocols.0);
            payload.u16(16, self.flags.0);
        })
    }
}

/// `UBX-CFG-PRT`'s poll of one port (section 32.10.25.1, p. 243).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CfgPrtPoll {
    /// `PortID`: the port whose configuration the receiver is to send.
    pub port_id: u8,
}

impl Encode for CfgPrtPoll {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        encode_with(CFG_PRT, PRT_POLL_LENGTH, out, |payload| {
            payload.u8(0, self.port_id);
        })
    }
}

/// `UBX-CFG-MSG` for the current port (section 32.10.18.3, p. 216):
/// how often the port sends a message.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CfgMsg {
    /// `msgClass` and `msgID`.
    pub message: MessageId,
    /// `rate`: once in so many of the events the message is sent on, 0
    /// never.
    pub rate: u8,
}

impl Encode for CfgMsg {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        encode_with(CFG_MSG, MSG_LENGTH, out, |payload| {
            payload.u8(0, self.message.class);
            payload.u8(1, self.message.id);
            payload.u8(2, self.rate);
        })
    }
}

/// `UBX-CFG-MSG`'s poll of a message's rates (section 32.10.18.1, p.
/// 215).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CfgMsgPoll {
    /// `msgClass` and `msgID`.
    pub message: MessageId,
}

impl Encode for CfgMsgPoll {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        encode_with(CFG_MSG, MSG_POLL_LENGTH, out, |payload| {
            payload.u8(0, self.message.class);
            payload.u8(1, self.message.id);
        })
    }
}

bit_field_type! {
    /// `CFG-ANT`'s `flags` (the bitfield graphic, pp. 191-192).
    AntennaFlags(u16)
}

impl AntennaFlags {
    /// `svcs`: the antenna supply voltage control signal.
    pub const SUPPLY_VOLTAGE_CONTROL: Self = Self(1 << 0);
    /// `scd`: short circuit detection.
    pub const SHORT_CIRCUIT_DETECTION: Self = Self(1 << 1);
    /// `ocd`: open circuit detection.
    pub const OPEN_CIRCUIT_DETECTION: Self = Self(1 << 2);
    /// `pdwnOnSCD`: the supply powered down on a short, with
    /// [`SHORT_CIRCUIT_DETECTION`](Self::SHORT_CIRCUIT_DETECTION).
    pub const POWER_DOWN_ON_SHORT_CIRCUIT: Self = Self(1 << 3);
    /// `recovery`: automatic recovery from a short.
    pub const RECOVERY: Self = Self(1 << 4);
}

bit_field_type! {
    /// `CFG-ANT`'s `pins` (the bitfield graphic, p. 192): the PIOs the
    /// antenna supervisor uses.
    AntennaPins(u16)
}

/// `pins`' fields: `pinSwitch` bits 0 to 4, `pinSCD` 5 to 9, `pinOCD`
/// 10 to 14.
const PIN_MASK: u16 = 0x1f;
const PIN_SWITCH_SHIFT: u32 = 0;
const PIN_SHORT_SHIFT: u32 = 5;
const PIN_OPEN_SHIFT: u32 = 10;

impl AntennaPins {
    /// `reconfig`: the receiver takes the pins given.
    pub const RECONFIGURE: Self = Self(1 << 15);

    /// `pinSwitch`, `pinSCD` and `pinOCD`: the PIOs switching the
    /// supply, detecting a short and detecting an open antenna, each
    /// its low five bits.
    #[must_use]
    pub const fn new(switch: u8, short_circuit: u8, open_circuit: u8) -> Self {
        Self(
            (switch as u16 & PIN_MASK) << PIN_SWITCH_SHIFT
                | (short_circuit as u16 & PIN_MASK) << PIN_SHORT_SHIFT
                | (open_circuit as u16 & PIN_MASK) << PIN_OPEN_SHIFT,
        )
    }
}

/// `UBX-CFG-ANT` (section 32.10.1, pp. 191-192): the antenna
/// supervisor.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CfgAnt {
    /// `flags`.
    pub flags: AntennaFlags,
    /// `pins`.
    pub pins: AntennaPins,
}

impl Encode for CfgAnt {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        encode_with(CFG_ANT, ANT_LENGTH, out, |payload| {
            payload.u16(0, self.flags.0);
            payload.u16(2, self.pins.0);
        })
    }
}

value_type! {
    /// `CFG-RATE`'s `timeRef` (p. 255): the time measurements are
    /// aligned to.
    TimeReference(u16) {
        /// 0.
        Utc = 0,
        /// 1.
        Gps = 1,
        /// 2.
        Glonass = 2,
        /// 3.
        BeiDou = 3,
        /// 4.
        Galileo = 4,
        /// 5.
        NavIc = 5,
    }
}

/// `UBX-CFG-RATE` (section 32.10.27, pp. 254-255): the measurement and
/// navigation rates.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CfgRate {
    /// `measRate`: the time between measurements.
    pub measurement_rate: Milliseconds<u16>,
    /// `navRate`: measurements to a navigation solution.
    pub navigation_rate: u16,
    /// `timeRef`.
    pub time_reference: TimeReference,
}

impl Encode for CfgRate {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        encode_with(CFG_RATE, RATE_LENGTH, out, |payload| {
            payload.u16(0, self.measurement_rate.0);
            payload.u16(2, self.navigation_rate);
            payload.u16(4, self.time_reference.value());
        })
    }
}

value_type! {
    /// `CFG-RXM`'s `lpMode` (p. 258).  4 is continuous mode too.
    LowPowerMode(u8) {
        /// 0: continuous.
        Continuous = 0,
        /// 1: power save.
        PowerSave = 1,
    }
}

/// `UBX-CFG-RXM` (section 32.10.30.2, p. 258): the power mode.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CfgRxm {
    /// `lpMode`.
    pub low_power_mode: LowPowerMode,
}

impl Encode for CfgRxm {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        encode_with(CFG_RXM, RXM_LENGTH, out, |payload| {
            payload.u8(1, self.low_power_mode.value());
        })
    }
}

bit_field_type! {
    /// A `CFG-GNSS` block's `flags` (the bitfield graphic, p. 209).
    GnssFlags(u32)
}

/// `flags`' `sigCfgMask`: bits 16 to 23.
const SIGNAL_SHIFT: u32 = 16;

impl GnssFlags {
    /// `enable`: the system enabled.
    pub const ENABLE: Self = Self(1 << 0);
    /// `sigCfgMask` 0x01 of GPS: L1C/A.
    pub const GPS_L1CA: Self = Self::signals(0x01);
    /// `sigCfgMask` 0x10 of GPS: L2C.
    pub const GPS_L2C: Self = Self::signals(0x10);
    /// `sigCfgMask` 0x20 of GPS: L5.
    pub const GPS_L5: Self = Self::signals(0x20);
    /// `sigCfgMask` 0x01 of SBAS: L1C/A.
    pub const SBAS_L1CA: Self = Self::signals(0x01);
    /// `sigCfgMask` 0x01 of Galileo: E1.
    pub const GALILEO_E1: Self = Self::signals(0x01);
    /// `sigCfgMask` 0x10 of Galileo: E5a.
    pub const GALILEO_E5A: Self = Self::signals(0x10);
    /// `sigCfgMask` 0x20 of Galileo: E5b.
    pub const GALILEO_E5B: Self = Self::signals(0x20);
    /// `sigCfgMask` 0x01 of `BeiDou`: B1I.
    pub const BEIDOU_B1I: Self = Self::signals(0x01);
    /// `sigCfgMask` 0x10 of `BeiDou`: B2I.
    pub const BEIDOU_B2I: Self = Self::signals(0x10);
    /// `sigCfgMask` 0x80 of `BeiDou`: B2A.
    pub const BEIDOU_B2A: Self = Self::signals(0x80);
    /// `sigCfgMask` 0x01 of QZSS: L1C/A.
    pub const QZSS_L1CA: Self = Self::signals(0x01);
    /// `sigCfgMask` 0x04 of QZSS: L1S.
    pub const QZSS_L1S: Self = Self::signals(0x04);
    /// `sigCfgMask` 0x10 of QZSS: L2C.
    pub const QZSS_L2C: Self = Self::signals(0x10);
    /// `sigCfgMask` 0x20 of QZSS: L5.
    pub const QZSS_L5: Self = Self::signals(0x20);
    /// `sigCfgMask` 0x01 of GLONASS: L1.
    pub const GLONASS_L1: Self = Self::signals(0x01);
    /// `sigCfgMask` 0x10 of GLONASS: L2.
    pub const GLONASS_L2: Self = Self::signals(0x10);

    /// `sigCfgMask`: the signals configured, whose bits mean a signal
    /// of the block's system.
    #[must_use]
    pub const fn signals(mask: u8) -> Self {
        Self((mask as u32) << SIGNAL_SHIFT)
    }
}

/// One system's block of `CFG-GNSS` (p. 208).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct GnssBlock {
    /// `gnssId`: [`SYSTEM_GPS`](crate::message::SYSTEM_GPS) and the
    /// others.
    pub system: u8,
    /// `resTrkCh`: the tracking channels reserved for it.
    pub reserved_tracking_channels: u8,
    /// `maxTrkCh`: the most it uses.
    pub maximum_tracking_channels: u8,
    /// `flags`.
    pub flags: GnssFlags,
}

/// `UBX-CFG-GNSS` (section 32.10.13, pp. 207-209): the systems the
/// receiver tracks, and their channels.  Its frame is
/// [`OVERHEAD`](crate::frame::OVERHEAD) and [`cfg_gnss_length`] of its
/// blocks long.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CfgGnss<'a> {
    /// `numTrkChHw`: the tracking channels in hardware, which the
    /// receiver does not take.
    pub hardware_tracking_channels: u8,
    /// `numTrkChUse`: those to use, 0xFF all.
    pub tracking_channels: u8,
    /// One block a system, its count `numConfigBlocks`.
    pub blocks: &'a [GnssBlock],
}

impl Encode for CfgGnss<'_> {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        let count = self.blocks.len();
        let blocks = u8::try_from(count).map_err(|_| EncodeError::TooManyBlocks(count))?;
        encode_with(CFG_GNSS, cfg_gnss_length(count), out, |payload| {
            payload.u8(0, GNSS_VERSION);
            payload.u8(1, self.hardware_tracking_channels);
            payload.u8(2, self.tracking_channels);
            payload.u8(3, blocks);
            for (index, block) in self.blocks.iter().enumerate() {
                // A block starts where a payload of the blocks before it
                // ends.
                let at = cfg_gnss_length(index);
                payload.u8(at, block.system);
                payload.u8(at + 1, block.reserved_tracking_channels);
                payload.u8(at + 2, block.maximum_tracking_channels);
                payload.u32(at + 4, block.flags.0);
            }
        })
    }
}

bit_field_type! {
    /// `CFG-NAV5`'s `mask` (the bitfield graphic, pp. 218-219): the
    /// settings the receiver applies.
    Nav5Mask(u16)
}

impl Nav5Mask {
    /// `dyn`: `dynModel`.
    pub const DYNAMIC_MODEL: Self = Self(1 << 0);
    /// `minEl`: `minElev`.
    pub const MINIMUM_ELEVATION: Self = Self(1 << 1);
    /// `posFixMode`: `fixMode`, `fixedAlt` and `fixedAltVar`.
    pub const POSITION_FIX_MODE: Self = Self(1 << 2);
    /// `drLim`, reserved.
    pub const DEAD_RECKONING_LIMIT: Self = Self(1 << 3);
    /// `posMask`: `pDop` and `pAcc`.
    pub const POSITION_MASK: Self = Self(1 << 4);
    /// `timeMask`: `tDop` and `tAcc`.
    pub const TIME_MASK: Self = Self(1 << 5);
    /// `staticHoldMask`: `staticHoldThresh` and `staticHoldMaxDist`.
    pub const STATIC_HOLD_MASK: Self = Self(1 << 6);
    /// `dgpsMask`: `dgnssTimeout`.
    pub const DGPS_MASK: Self = Self(1 << 7);
    /// `cnoThreshold`: `cnoThresh` and `cnoThreshNumSVs`.
    pub const CNO_THRESHOLD: Self = Self(1 << 8);
    /// `utc`: `utcStandard`.
    pub const UTC: Self = Self(1 << 10);
}

value_type! {
    /// `CFG-NAV5`'s `dynModel` (p. 217): the dynamic platform model.
    DynamicModel(u8) {
        /// 0: portable.
        Portable = 0,
        /// 2: stationary.
        Stationary = 2,
        /// 3: pedestrian.
        Pedestrian = 3,
        /// 4: automotive.
        Automotive = 4,
        /// 5: sea.
        Sea = 5,
        /// 6: airborne with less than 1 g acceleration.
        Airborne1G = 6,
        /// 7: airborne with less than 2 g.
        Airborne2G = 7,
        /// 8: airborne with less than 4 g.
        Airborne4G = 8,
        /// 9: wrist-worn watch.
        Wrist = 9,
        /// 10: motorbike.
        Motorbike = 10,
        /// 11: robotic lawn mower.
        LawnMower = 11,
        /// 12: electric kick scooter.
        KickScooter = 12,
    }
}

value_type! {
    /// `CFG-NAV5`'s `fixMode` (p. 217): the position fixing mode.
    FixMode(u8) {
        /// 1: 2D only.
        TwoD = 1,
        /// 2: 3D only.
        ThreeD = 2,
        /// 3: 2D or 3D.
        Auto = 3,
    }
}

value_type! {
    /// `CFG-NAV5`'s `utcStandard` (p. 218): the UTC standard used.
    UtcStandard(u8) {
        /// 0: the receiver's choice, by the systems configured.
        Automatic = 0,
        /// 3: the U.S. Naval Observatory's, from GPS time.
        Usno = 3,
        /// 5: Europe's laboratories', from Galileo time.
        European = 5,
        /// 6: the former Soviet Union's, from GLONASS time.
        SovietUnion = 6,
        /// 7: China's National Time Service Center's, from `BeiDou` time.
        Ntsc = 7,
        /// 8: India's National Physics Laboratory's, from `NavIC` time.
        Npli = 8,
    }
}

/// `UBX-CFG-NAV5` (section 32.10.19, pp. 216-219): the navigation
/// engine's settings, of which the receiver applies those its mask
/// names.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CfgNav5 {
    /// `mask`.
    pub mask: Nav5Mask,
    /// `dynModel`.
    pub dynamic_model: DynamicModel,
    /// `fixMode`.
    pub fix_mode: FixMode,
    /// `fixedAlt`: the altitude above mean sea level a 2D fix holds.
    pub fixed_altitude: Centimeters<i32>,
    /// `fixedAltVar`: its variance.
    pub fixed_altitude_variance: SquareCentimeters,
    /// `minElev`: the lowest elevation of a satellite used.
    pub minimum_elevation: Degrees<i8>,
    /// `pDop`: the position DOP mask.
    pub position_dop: Tenths,
    /// `tDop`: the time DOP mask.
    pub time_dop: Tenths,
    /// `pAcc`: the position accuracy mask.
    pub position_accuracy: Meters<u16>,
    /// `tAcc`: the time accuracy mask.
    pub time_accuracy: Meters<u16>,
    /// `staticHoldThresh`: the static hold threshold.
    pub static_hold_threshold: CentimetersPerSecond<u8>,
    /// `dgnssTimeout`.
    pub dgnss_timeout: Seconds<u8>,
    /// `cnoThreshNumSVs`: the satellites that must be above
    /// `cnoThresh` for a fix to be tried.
    pub cno_threshold_satellites: u8,
    /// `cnoThresh`.
    pub cno_threshold: DecibelHertz,
    /// `staticHoldMaxDist`: the distance that ends static hold.
    pub static_hold_maximum_distance: Meters<u16>,
    /// `utcStandard`.
    pub utc_standard: UtcStandard,
}

impl Encode for CfgNav5 {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        encode_with(CFG_NAV5, NAV5_LENGTH, out, |payload| {
            payload.u16(0, self.mask.0);
            payload.u8(2, self.dynamic_model.value());
            payload.u8(3, self.fix_mode.value());
            payload.i32(4, self.fixed_altitude.0);
            payload.u32(8, self.fixed_altitude_variance.0);
            payload.i8(12, self.minimum_elevation.0);
            payload.u16(14, self.position_dop.0);
            payload.u16(16, self.time_dop.0);
            payload.u16(18, self.position_accuracy.0);
            payload.u16(20, self.time_accuracy.0);
            payload.u8(22, self.static_hold_threshold.0);
            payload.u8(23, self.dgnss_timeout.0);
            payload.u8(24, self.cno_threshold_satellites);
            payload.u8(25, self.cno_threshold.0);
            payload.u16(28, self.static_hold_maximum_distance.0);
            payload.u8(30, self.utc_standard.value());
        })
    }
}

bit_field_type! {
    /// `CFG-NAVX5`'s `mask1` (the bitfield graphic, pp. 222-223): the
    /// settings the receiver applies.
    Navx5Mask1(u16)
}

impl Navx5Mask1 {
    /// `minMax`: `minSVs` and `maxSVs`.
    pub const MINIMUM_MAXIMUM: Self = Self(1 << 2);
    /// `minCno`: `minCNO`.
    pub const MINIMUM_CNO: Self = Self(1 << 3);
    /// `initial3dfix`: `iniFix3D`.
    pub const INITIAL_3D_FIX: Self = Self(1 << 6);
    /// `wknRoll`: `wknRollover`.
    pub const WEEK_ROLLOVER: Self = Self(1 << 9);
    /// `ackAid`: `ackAiding`.
    pub const ACK_AIDING: Self = Self(1 << 10);
    /// `ppp`: `usePPP`.
    pub const PPP: Self = Self(1 << 13);
    /// `aop`: `aopCfg` and `aopOrbMaxErr`.
    pub const AOP: Self = Self(1 << 14);
}

bit_field_type! {
    /// `CFG-NAVX5`'s `mask2` (the bitfield graphic, p. 223).
    Navx5Mask2(u32)
}

impl Navx5Mask2 {
    /// `adr`: `useAdr`.
    pub const ADR: Self = Self(1 << 6);
    /// `sigAttenComp`: `sigAttenCompMode`.
    pub const SIGNAL_ATTENUATION_COMPENSATION: Self = Self(1 << 7);
}

bit_field_type! {
    /// `CFG-NAVX5`'s `aopCfg` (the bitfield graphic, p. 223).
    AopConfig(u8)
}

impl AopConfig {
    /// `useAOP`: `AssistNow` Autonomous enabled.
    pub const USE_AOP: Self = Self(1 << 0);
}

/// `UBX-CFG-NAVX5` version 2 (section 32.10.20.2, pp. 221-223): the
/// navigation engine's expert settings, of which the receiver applies
/// those its masks name.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CfgNavx5 {
    /// `mask1`.
    pub mask1: Navx5Mask1,
    /// `mask2`.
    pub mask2: Navx5Mask2,
    /// `minSVs`: the fewest satellites for navigation.
    pub minimum_satellites: u8,
    /// `maxSVs`: the most.
    pub maximum_satellites: u8,
    /// `minCNO`: the weakest signal for navigation.
    pub minimum_cno: DecibelHertz,
    /// `iniFix3D`: the first fix must be 3D.
    pub initial_fix_3d: bool,
    /// `ackAiding`: assistance messages acknowledged.
    pub ack_aiding: bool,
    /// `wknRollover`: the GPS week from which week numbers are taken,
    /// for 1024 weeks; 0 the firmware's own.
    pub week_rollover: u16,
    /// `sigAttenCompMode`: 0 disabled, 255 automatic, 1 to 63 the
    /// strongest C/N0 expected.
    pub signal_attenuation_compensation: u8,
    /// `usePPP`: Precise Point Positioning.
    pub use_ppp: bool,
    /// `aopCfg`.
    pub aop_config: AopConfig,
    /// `aopOrbMaxErr`: the largest `AssistNow` Autonomous orbit error
    /// taken; 0 the firmware's own.
    pub aop_orbit_maximum_error: Meters<u16>,
    /// `useAdr`: ADR/UDR sensor fusion.
    pub use_adr: bool,
}

impl Encode for CfgNavx5 {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        encode_with(CFG_NAVX5, NAVX5_LENGTH, out, |payload| {
            payload.u16(0, NAVX5_VERSION);
            payload.u16(2, self.mask1.0);
            payload.u32(4, self.mask2.0);
            payload.u8(10, self.minimum_satellites);
            payload.u8(11, self.maximum_satellites);
            payload.u8(12, self.minimum_cno.0);
            payload.u8(14, u8::from(self.initial_fix_3d));
            payload.u8(17, u8::from(self.ack_aiding));
            payload.u16(18, self.week_rollover);
            payload.u8(20, self.signal_attenuation_compensation);
            payload.u8(26, u8::from(self.use_ppp));
            payload.u8(27, self.aop_config.0);
            payload.u16(30, self.aop_orbit_maximum_error.0);
            payload.u8(39, u8::from(self.use_adr));
        })
    }
}

value_type! {
    /// `CFG-TMODE2`'s `timeMode` (p. 267).
    TimeMode(u8) {
        /// 0: disabled.
        Disabled = 0,
        /// 1: surveying in.
        SurveyIn = 1,
        /// 2: fixed at the position given.
        Fixed = 2,
    }
}

bit_field_type! {
    /// `CFG-TMODE2`'s `flags` (the bitfield graphic, p. 267).
    TimeModeFlags(u16)
}

impl TimeModeFlags {
    /// `lla`: the position is latitude, longitude and altitude, not
    /// ECEF.
    pub const LLA: Self = Self(1 << 0);
    /// `altInv`: with [`LLA`](Self::LLA), the altitude is not valid.
    pub const ALTITUDE_INVALID: Self = Self(1 << 1);
}

/// `UBX-CFG-TMODE2` (section 32.10.36, pp. 266-267), every field: the
/// time mode of a timing receiver.  Sent, and decoded from the answer
/// to its poll.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CfgTmode2 {
    /// `timeMode`.
    pub time_mode: TimeMode,
    /// `flags`.
    pub flags: TimeModeFlags,
    /// `ecefXOrLat`: ECEF X in centimeters, or with
    /// [`TimeModeFlags::LLA`] the latitude in 1e-7 degree.
    pub ecef_x_or_latitude: i32,
    /// `ecefYOrLon`: ECEF Y in centimeters, or the longitude in 1e-7
    /// degree.
    pub ecef_y_or_longitude: i32,
    /// `ecefZOrAlt`: ECEF Z, or the altitude, in centimeters.  The
    /// document gives the altitude as WGS84's, so above the ellipsoid.
    pub ecef_z_or_altitude: Centimeters<i32>,
    /// `fixedPosAcc`: the fixed position's 3D accuracy.
    pub fixed_position_accuracy: Millimeters<u32>,
    /// `svinMinDur`: the survey-in's shortest duration.
    pub survey_in_minimum_duration: Seconds<u32>,
    /// `svinAccLimit`: the survey-in's position accuracy limit.
    pub survey_in_accuracy_limit: Millimeters<u32>,
}

impl Encode for CfgTmode2 {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        encode_with(CFG_TMODE2, TMODE2_LENGTH, out, |payload| {
            payload.u8(0, self.time_mode.value());
            payload.u16(2, self.flags.0);
            payload.i32(4, self.ecef_x_or_latitude);
            payload.i32(8, self.ecef_y_or_longitude);
            payload.i32(12, self.ecef_z_or_altitude.0);
            payload.u32(16, self.fixed_position_accuracy.0);
            payload.u32(20, self.survey_in_minimum_duration.0);
            payload.u32(24, self.survey_in_accuracy_limit.0);
        })
    }
}

impl CfgTmode2 {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        Self {
            time_mode: TimeMode::new(fields.u8(0)),
            flags: TimeModeFlags(fields.u16(2)),
            ecef_x_or_latitude: fields.i32(4),
            ecef_y_or_longitude: fields.i32(8),
            ecef_z_or_altitude: Centimeters(fields.i32(12)),
            fixed_position_accuracy: Millimeters(fields.u32(16)),
            survey_in_minimum_duration: Seconds(fields.u32(20)),
            survey_in_accuracy_limit: Millimeters(fields.u32(24)),
        }
    }
}

value_type! {
    /// `CFG-TP5`'s `tpIdx` (p. 272): which time pulse.
    TimePulse(u8) {
        /// 0: `TIMEPULSE`.
        First = 0,
        /// 1: `TIMEPULSE2`.
        Second = 1,
    }
}

bit_field_type! {
    /// `CFG-TP5`'s `flags` (the bitfield graphic, pp. 273-274).
    PulseFlags(u32)
}

/// `flags`' fields: `gridUtcGnss` bits 7 to 10, `syncMode` bits 11 to
/// 13.
const GRID_SHIFT: u32 = 7;
const GRID_MASK: u32 = 0xf << GRID_SHIFT;
const SYNC_SHIFT: u32 = 11;
const SYNC_MASK: u32 = 0x7 << SYNC_SHIFT;

impl PulseFlags {
    /// `active`: the pulse enabled.
    pub const ACTIVE: Self = Self(1 << 0);
    /// `lockGnssFreq`: the pulse synchronized to GNSS time once it is
    /// valid.
    pub const LOCK_GNSS_FREQUENCY: Self = Self(1 << 1);
    /// `lockedOtherSet`: the locked period and length used once locked.
    pub const LOCKED_OTHER_SET: Self = Self(1 << 2);
    /// `isFreq`: the periods are frequencies.
    pub const IS_FREQUENCY: Self = Self(1 << 3);
    /// `isLength`: the lengths are lengths, not duty cycles.
    pub const IS_LENGTH: Self = Self(1 << 4);
    /// `alignToTow`: the pulse aligned to the top of a second.
    pub const ALIGN_TO_TOW: Self = Self(1 << 5);
    /// `polarity` set: the rising edge at the top of a second.  Clear
    /// is the falling edge.
    pub const RISING: Self = Self(1 << 6);
    /// `gridUtcGnss` 0: UTC's time grid.
    pub const GRID_UTC: Self = Self(0 << GRID_SHIFT);
    /// `gridUtcGnss` 1: GPS's.
    pub const GRID_GPS: Self = Self(1 << GRID_SHIFT);
    /// `gridUtcGnss` 2: GLONASS's.
    pub const GRID_GLONASS: Self = Self(2 << GRID_SHIFT);
    /// `gridUtcGnss` 3: `BeiDou`'s.
    pub const GRID_BEIDOU: Self = Self(3 << GRID_SHIFT);
    /// `gridUtcGnss` 4: Galileo's.
    pub const GRID_GALILEO: Self = Self(4 << GRID_SHIFT);
    /// `syncMode` 0: the locked settings kept once taken.
    pub const SYNC_KEEP_LOCKED: Self = Self(0 << SYNC_SHIFT);
    /// `syncMode` 1: the unlocked settings taken back when time is no
    /// longer accurate.
    pub const SYNC_SWITCH_BACK: Self = Self(1 << SYNC_SHIFT);

    /// `gridUtcGnss` alone: one of the `GRID_` constants, or another
    /// value.
    #[must_use]
    pub const fn grid(self) -> Self {
        Self(self.0 & GRID_MASK)
    }

    /// `syncMode` alone: one of the `SYNC_` constants, or another
    /// value.
    #[must_use]
    pub const fn sync_mode(self) -> Self {
        Self(self.0 & SYNC_MASK)
    }
}

/// `UBX-CFG-TP5` version 1 (section 32.10.38.4, pp. 272-274), every
/// field: a time pulse's settings.  Sent, and decoded from the answer
/// to its poll.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CfgTp5 {
    /// `tpIdx`.
    pub time_pulse: TimePulse,
    /// `antCableDelay`.
    pub antenna_cable_delay: Nanoseconds<i16>,
    /// `rfGroupDelay`.
    pub rf_group_delay: Nanoseconds<i16>,
    /// `freqPeriod`: the period in microseconds, or with
    /// [`PulseFlags::IS_FREQUENCY`] the frequency in hertz.
    pub frequency_period: u32,
    /// `freqPeriodLock`: the same once locked, with
    /// [`PulseFlags::LOCKED_OTHER_SET`].
    pub frequency_period_locked: u32,
    /// `pulseLenRatio`: the length in microseconds, with
    /// [`PulseFlags::IS_LENGTH`], or else the duty cycle in 2^-32.
    pub pulse_length_ratio: u32,
    /// `pulseLenRatioLock`: the same once locked.
    pub pulse_length_ratio_locked: u32,
    /// `userConfigDelay`: the user's delay of the pulse.
    pub user_config_delay: Nanoseconds<i32>,
    /// `flags`.
    pub flags: PulseFlags,
}

impl Encode for CfgTp5 {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        encode_with(CFG_TP5, TP5_LENGTH, out, |payload| {
            payload.u8(0, self.time_pulse.value());
            payload.u8(1, TP5_VERSION);
            payload.i16(4, self.antenna_cable_delay.0);
            payload.i16(6, self.rf_group_delay.0);
            payload.u32(8, self.frequency_period);
            payload.u32(12, self.frequency_period_locked);
            payload.u32(16, self.pulse_length_ratio);
            payload.u32(20, self.pulse_length_ratio_locked);
            payload.i32(24, self.user_config_delay.0);
            payload.u32(28, self.flags.0);
        })
    }
}

impl CfgTp5 {
    /// `None` for a version other than 1.
    pub(crate) fn parse(fields: Fields<'_>) -> Option<Self> {
        (fields.u8(1) == TP5_VERSION).then(|| Self {
            time_pulse: TimePulse::new(fields.u8(0)),
            antenna_cable_delay: Nanoseconds(fields.i16(4)),
            rf_group_delay: Nanoseconds(fields.i16(6)),
            frequency_period: fields.u32(8),
            frequency_period_locked: fields.u32(12),
            pulse_length_ratio: fields.u32(16),
            pulse_length_ratio_locked: fields.u32(20),
            user_config_delay: Nanoseconds(fields.i32(24)),
            flags: PulseFlags(fields.u32(28)),
        })
    }
}

/// `UBX-CFG-TP5`'s poll of one time pulse (section 32.10.38.2, p. 270).
/// An empty poll, [`poll`](crate::frame::poll), asks for `TIMEPULSE`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CfgTp5Poll {
    /// `tpIdx`: the pulse whose settings the receiver is to send.
    pub time_pulse: TimePulse,
}

impl Encode for CfgTp5Poll {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        encode_with(CFG_TP5, TP5_POLL_LENGTH, out, |payload| {
            payload.u8(0, self.time_pulse.value());
        })
    }
}

/// `UBX-CFG-DAT`'s user-defined datum (section 32.10.4.1, p. 195),
/// each field an `R8` or `R4`'s bits.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CfgDat {
    /// `majA`: the semi-major axis, in meters.
    pub semi_major_axis: F64Bits,
    /// `flat`: 1 over the flattening.
    pub inverse_flattening: F64Bits,
    /// `dX`: the X shift at the origin, in meters.
    pub shift_x: F32Bits,
    /// `dY`.
    pub shift_y: F32Bits,
    /// `dZ`.
    pub shift_z: F32Bits,
    /// `rotX`: the rotation about the X axis; its accepted range is in
    /// milli-arc seconds.
    pub rotation_x: F32Bits,
    /// `rotY`.
    pub rotation_y: F32Bits,
    /// `rotZ`.
    pub rotation_z: F32Bits,
    /// `scale`: the scale change, in parts per million.
    pub scale: F32Bits,
}

impl Encode for CfgDat {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        encode_with(CFG_DAT, DAT_LENGTH, out, |payload| {
            payload.u64(0, self.semi_major_axis.0);
            payload.u64(8, self.inverse_flattening.0);
            payload.u32(16, self.shift_x.0);
            payload.u32(20, self.shift_y.0);
            payload.u32(24, self.shift_z.0);
            payload.u32(28, self.rotation_x.0);
            payload.u32(32, self.rotation_y.0);
            payload.u32(36, self.rotation_z.0);
            payload.u32(40, self.scale.0);
        })
    }
}

bit_field_type! {
    /// `CFG-CFG`'s `clearMask`, `saveMask` and `loadMask` (the bitfield
    /// graphic, p. 194): sections of the configuration.  The others
    /// are reserved, and the document has them zero.
    ConfigSections(u32)
}

impl ConfigSections {
    /// `ioPort`: the ports' settings.
    pub const IO_PORT: Self = Self(1 << 0);
    /// `msgConf`: the messages'.
    pub const MESSAGE_CONFIGURATION: Self = Self(1 << 1);
    /// `infMsg`: the INF messages'.
    pub const INF_MESSAGES: Self = Self(1 << 2);
    /// `navConf`: navigation's.
    pub const NAVIGATION_CONFIGURATION: Self = Self(1 << 3);
    /// `rxmConf`: the receiver manager's.
    pub const RXM_CONFIGURATION: Self = Self(1 << 4);
    /// `senConf`: the sensor interface's.
    pub const SENSOR_CONFIGURATION: Self = Self(1 << 8);
    /// `rinvConf`: the remote inventory's.
    pub const REMOTE_INVENTORY_CONFIGURATION: Self = Self(1 << 9);
    /// `antConf`: the antenna's.
    pub const ANTENNA_CONFIGURATION: Self = Self(1 << 10);
    /// `logConf`: logging's.
    pub const LOG_CONFIGURATION: Self = Self(1 << 11);
    /// `ftsConf`: the FTS variant's.
    pub const FTS_CONFIGURATION: Self = Self(1 << 12);
    /// Every section the document names, and no reserved bit.
    pub const ALL: Self = Self::IO_PORT
        .union(Self::MESSAGE_CONFIGURATION)
        .union(Self::INF_MESSAGES)
        .union(Self::NAVIGATION_CONFIGURATION)
        .union(Self::RXM_CONFIGURATION)
        .union(Self::SENSOR_CONFIGURATION)
        .union(Self::REMOTE_INVENTORY_CONFIGURATION)
        .union(Self::ANTENNA_CONFIGURATION)
        .union(Self::LOG_CONFIGURATION)
        .union(Self::FTS_CONFIGURATION);
}

bit_field_type! {
    /// `CFG-CFG`'s `deviceMask` (the bitfield graphic, p. 195): the
    /// memories acted on.
    DeviceMask(u8)
}

impl DeviceMask {
    /// `devBBR`: battery-backed RAM.
    pub const BBR: Self = Self(1 << 0);
    /// `devFlash`.
    pub const FLASH: Self = Self(1 << 1);
    /// `devEEPROM`.
    pub const EEPROM: Self = Self(1 << 2);
    /// `devSpiFlash`.
    pub const SPI_FLASH: Self = Self(1 << 4);
}

/// `UBX-CFG-CFG` with `deviceMask` (section 32.10.3, pp. 193-195):
/// configuration sections cleared, saved and loaded, in that order.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CfgCfg {
    /// `clearMask`: those set to their defaults in permanent memory.
    pub clear_mask: ConfigSections,
    /// `saveMask`: those saved from the current configuration.
    pub save_mask: ConfigSections,
    /// `loadMask`: those loaded from permanent memory.
    pub load_mask: ConfigSections,
    /// `deviceMask`.
    pub device_mask: DeviceMask,
}

impl Encode for CfgCfg {
    fn encode(&self, out: &mut [u8]) -> Result<usize, EncodeError> {
        encode_with(CFG_CFG, CFG_LENGTH, out, |payload| {
            payload.u32(0, self.clear_mask.0);
            payload.u32(4, self.save_mask.0);
            payload.u32(8, self.load_mask.0);
            payload.u8(12, self.device_mask.0);
        })
    }
}
