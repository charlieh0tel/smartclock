//! The encoders against whole frames a caller sends a receiver today,
//! byte for byte, each built from the field values it carries; the two
//! decoded configurations back through the deframer; and the polls.
//! The position is a made-up one: 10.5 N, 20.5 W.

use std::vec;
use std::vec::Vec;

use super::decode;
use super::frame;
use crate::cfg::AntennaFlags;
use crate::cfg::AntennaPins;
use crate::cfg::AopConfig;
use crate::cfg::CfgAnt;
use crate::cfg::CfgCfg;
use crate::cfg::CfgDat;
use crate::cfg::CfgGnss;
use crate::cfg::CfgMsg;
use crate::cfg::CfgMsgPoll;
use crate::cfg::CfgNav5;
use crate::cfg::CfgNavx5;
use crate::cfg::CfgPrt;
use crate::cfg::CfgPrtPoll;
use crate::cfg::CfgRate;
use crate::cfg::CfgRxm;
use crate::cfg::CfgTmode2;
use crate::cfg::CfgTp5;
use crate::cfg::CfgTp5Poll;
use crate::cfg::ConfigSections;
use crate::cfg::DeviceMask;
use crate::cfg::DynamicModel;
use crate::cfg::FixMode;
use crate::cfg::GnssBlock;
use crate::cfg::GnssFlags;
use crate::cfg::LowPowerMode;
use crate::cfg::Nav5Mask;
use crate::cfg::Navx5Mask1;
use crate::cfg::Navx5Mask2;
use crate::cfg::PortFlags;
use crate::cfg::Protocols;
use crate::cfg::PulseFlags;
use crate::cfg::TimeMode;
use crate::cfg::TimeModeFlags;
use crate::cfg::TimePulse;
use crate::cfg::TimeReference;
use crate::cfg::TxReady;
use crate::cfg::UartMode;
use crate::cfg::UtcStandard;
use crate::cfg::cfg_gnss_length;
use crate::frame::Deframer;
use crate::frame::EncodeError;
use crate::frame::NoRoom;
use crate::frame::OVERHEAD;
use crate::frame::message_id;
use crate::frame::poll;
use crate::message::ACK_ACK;
use crate::message::CFG_ANT;
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
use crate::message::MGA_INI;
use crate::message::MON_HW;
use crate::message::Message;
use crate::message::NAV_CLOCK;
use crate::message::NAV_DOP;
use crate::message::NAV_EOE;
use crate::message::NAV_POSECEF;
use crate::message::NAV_PVT;
use crate::message::NAV_SAT;
use crate::message::NAV_TIMEGPS;
use crate::message::NAV_TIMELS;
use crate::message::RXM_RAWX;
use crate::message::RXM_SFRBX;
use crate::message::SYSTEM_BEIDOU;
use crate::message::SYSTEM_GALILEO;
use crate::message::SYSTEM_GLONASS;
use crate::message::SYSTEM_GPS;
use crate::message::SYSTEM_IMES;
use crate::message::SYSTEM_QZSS;
use crate::message::SYSTEM_SBAS;
use crate::message::TIM_TP;
use crate::mga::MgaIniPosLlh;
use crate::units::Centimeters;
use crate::units::CentimetersPerSecond;
use crate::units::DecibelHertz;
use crate::units::Degrees;
use crate::units::F32Bits;
use crate::units::F64Bits;
use crate::units::HundredNanodegrees;
use crate::units::Meters;
use crate::units::Millimeters;
use crate::units::Milliseconds;
use crate::units::Nanoseconds;
use crate::units::Seconds;
use crate::units::SquareCentimeters;
use crate::units::Tenths;

/// The frames, as hex.
const PRT: &str =
    "b5 62 06 00 14 00 01 00 00 00 c0 08 00 00 00 c2 01 00 01 00 01 00 00 00 00 00 a8 42";
const TP5_UTC: &str = "b5 62 06 31 20 00 00 01 00 00 00 00 00 00 40 42 0f 00 40 42 0f 00 40 0d 03 00 40 0d 03 00 00 00 00 00 77 00 00 00 91 8c";
const MSG_NAV_TIMEGPS: &str = "b5 62 06 01 03 00 01 20 01 2c 83";
const MSG_NAV_PVT: &str = "b5 62 06 01 03 00 01 07 01 13 51";
const MSG_NAV_DOP: &str = "b5 62 06 01 03 00 01 04 01 10 4b";
const MSG_NAV_SAT: &str = "b5 62 06 01 03 00 01 35 01 41 ad";
const MSG_NAV_TIMELS: &str = "b5 62 06 01 03 00 01 26 01 32 8f";
const MSG_NAV_POSECEF: &str = "b5 62 06 01 03 00 01 01 01 0d 45";
const MSG_NAV_CLOCK: &str = "b5 62 06 01 03 00 01 22 01 2e 87";
const MSG_RXM_RAWX: &str = "b5 62 06 01 03 00 02 15 01 22 70";
const MSG_MON_HW: &str = "b5 62 06 01 03 00 0a 09 01 1e 70";
const MSG_TIM_TP: &str = "b5 62 06 01 03 00 0d 01 01 19 69";
const MSG_RXM_SFRBX: &str = "b5 62 06 01 03 00 02 13 01 20 6c";
const MSG_NAV_EOE: &str = "b5 62 06 01 03 00 01 61 01 6d 05";
const ANT: &str = "b5 62 06 13 04 00 1b 00 f0 b9 e1 d2";
const RATE: &str = "b5 62 06 08 06 00 e8 03 01 00 01 00 01 39";
const RXM: &str = "b5 62 06 11 02 00 00 00 19 81";
const GNSS: &str = "b5 62 06 3e 3c 00 00 00 ff 07 00 08 10 00 01 00 01 00 01 00 03 00 00 00 01 00 02 00 08 00 00 00 01 00 03 00 10 00 00 00 01 00 04 00 08 00 00 00 01 00 05 00 03 00 00 00 01 00 06 00 0e 00 00 00 01 00 ef 29";
const NAV5_ALL: &str = "b5 62 06 24 24 00 ff 05 04 03 00 00 00 00 10 27 00 00 0a 00 fa 00 fa 00 64 00 5e 01 00 3c 00 00 00 00 00 00 03 00 00 00 00 00 90 20";
const NAVX5: &str = "b5 62 06 23 28 00 02 00 4c 66 00 00 00 00 00 00 01 20 09 00 00 00 00 00 c3 08 00 00 00 00 00 00 00 00 00 00 64 00 00 00 00 00 00 00 00 00 5e db";
const TMODE2_OFF: &str = "b5 62 06 3d 1c 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 5f 6b";
const DAT: &str = "b5 62 06 06 2c 00 00 00 00 40 a6 54 58 41 88 6d 74 96 1d a4 72 40 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 7d 70";
const CFG: &str = "b5 62 06 09 0d 00 00 00 00 00 ff ff 00 00 00 00 00 00 03 1d ab";
const NAV5_ELEVATION: &str = "b5 62 06 24 24 00 02 00 00 00 00 00 00 00 00 00 00 00 05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 55 84";
const NAV5_AIRBORNE: &str = "b5 62 06 24 24 00 01 00 06 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 55 b4";
const NAV5_PEDESTRIAN: &str = "b5 62 06 24 24 00 01 00 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 52 4e";
const MGA_POSITION: &str =
    "b5 62 13 40 14 00 01 00 00 00 40 2c 42 06 c0 f2 c7 f3 54 1b 00 00 80 96 98 00 a5 d8";
const TMODE2_FIXED: &str = "b5 62 06 3d 1c 00 02 00 01 00 40 2c 42 06 c0 f2 c7 f3 54 1b 00 00 64 00 00 00 00 00 00 00 00 00 00 00 55 77";
const TMODE2_FIXED_100M: &str = "b5 62 06 3d 1c 00 02 00 01 00 40 2c 42 06 c0 f2 c7 f3 10 27 00 00 64 00 00 00 00 00 00 00 00 00 00 00 1d eb";
const NAV5_2D: &str = "b5 62 06 24 24 00 04 00 00 01 10 27 00 00 10 27 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 c1 0b";
const NAV5_2D_130M: &str = "b5 62 06 24 24 00 04 00 00 01 cc 32 00 00 10 27 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 88 e0";
const NAV5_AUTO: &str = "b5 62 06 24 24 00 04 00 00 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 55 b7";
const TP5_GPS: &str = "b5 62 06 31 20 00 00 01 00 00 00 00 00 00 40 42 0f 00 40 42 0f 00 40 0d 03 00 40 0d 03 00 00 00 00 00 f7 00 00 00 11 8c";
const TP5_DELAY_500: &str = "b5 62 06 31 20 00 00 01 00 00 00 00 00 00 40 42 0f 00 40 42 0f 00 40 0d 03 00 40 0d 03 00 f4 01 00 00 f7 00 00 00 06 33";
const TP5_DELAY_MINUS_300: &str = "b5 62 06 31 20 00 00 01 00 00 00 00 00 00 40 42 0f 00 40 42 0f 00 40 0d 03 00 40 0d 03 00 d4 fe ff ff f7 00 00 00 e1 13";
const TP5_LOCKED_ONLY: &str = "b5 62 06 31 20 00 00 01 00 00 00 00 00 00 40 42 0f 00 40 42 0f 00 00 00 00 00 40 0d 03 00 d4 fe ff ff f7 00 00 00 91 26";
const TP5_OFF: &str = "b5 62 06 31 20 00 00 01 00 00 00 00 00 00 40 42 0f 00 40 42 0f 00 40 0d 03 00 40 0d 03 00 d4 fe ff ff f6 00 00 00 e0 0f";

/// The position: 10.5 N, 20.5 W, in 1e-7 degree.
const LATITUDE: HundredNanodegrees = HundredNanodegrees(105_000_000);
const LONGITUDE: HundredNanodegrees = HundredNanodegrees(-205_000_000);
/// 100 m, and the same less a geoid separation of -30.04 m and plus
/// it.
const HUNDRED_METERS: Centimeters<i32> = Centimeters(10_000);
const BELOW: Centimeters<i32> = Centimeters(6_996);
const ABOVE: Centimeters<i32> = Centimeters(13_004);

/// A second, and a pulse of 200 ms, in microseconds.
const SECOND: u32 = 1_000_000;
const PULSE: u32 = 200_000;
/// The time pulse's flags, on UTC's grid.
const PULSE_FLAGS: PulseFlags = PulseFlags::ACTIVE
    .union(PulseFlags::LOCK_GNSS_FREQUENCY)
    .union(PulseFlags::LOCKED_OTHER_SET)
    .union(PulseFlags::IS_LENGTH)
    .union(PulseFlags::ALIGN_TO_TOW)
    .union(PulseFlags::RISING)
    .union(PulseFlags::GRID_UTC);

const TP5: CfgTp5 = CfgTp5 {
    time_pulse: TimePulse::First,
    antenna_cable_delay: Nanoseconds(0),
    rf_group_delay: Nanoseconds(0),
    frequency_period: SECOND,
    frequency_period_locked: SECOND,
    pulse_length_ratio: PULSE,
    pulse_length_ratio_locked: PULSE,
    user_config_delay: Nanoseconds(0),
    flags: PULSE_FLAGS,
};

/// A `CFG-NAV5` that applies nothing, every field zero.
const NAV5_NONE: CfgNav5 = CfgNav5 {
    mask: Nav5Mask::NONE,
    dynamic_model: DynamicModel::Portable,
    fix_mode: FixMode::Other(0),
    fixed_altitude: Centimeters(0),
    fixed_altitude_variance: SquareCentimeters(0),
    minimum_elevation: Degrees(0),
    position_dop: Tenths(0),
    time_dop: Tenths(0),
    position_accuracy: Meters(0),
    time_accuracy: Meters(0),
    static_hold_threshold: CentimetersPerSecond(0),
    dgnss_timeout: Seconds(0),
    cno_threshold_satellites: 0,
    cno_threshold: DecibelHertz(0),
    static_hold_maximum_distance: Meters(0),
    utc_standard: UtcStandard::Automatic,
};

/// 1 m squared.
const ALTITUDE_VARIANCE: SquareCentimeters = SquareCentimeters(10_000);

const TMODE2_DISABLED: CfgTmode2 = CfgTmode2 {
    time_mode: TimeMode::Disabled,
    flags: TimeModeFlags::NONE,
    ecef_x_or_latitude: 0,
    ecef_y_or_longitude: 0,
    ecef_z_or_altitude: Centimeters(0),
    fixed_position_accuracy: Millimeters(0),
    survey_in_minimum_duration: Seconds(0),
    survey_in_accuracy_limit: Millimeters(0),
};

const TMODE2_HOLD: CfgTmode2 = CfgTmode2 {
    time_mode: TimeMode::Fixed,
    flags: TimeModeFlags::LLA,
    ecef_x_or_latitude: LATITUDE.0,
    ecef_y_or_longitude: LONGITUDE.0,
    ecef_z_or_altitude: BELOW,
    fixed_position_accuracy: Millimeters(100),
    ..TMODE2_DISABLED
};

/// The longest frame here: `CFG-GNSS` of seven systems.
const LONGEST: usize = OVERHEAD + cfg_gnss_length(7);

fn hex(text: &str) -> Vec<u8> {
    text.split_whitespace()
        .map(|byte| u8::from_str_radix(byte, 16).unwrap())
        .collect()
}

/// What an encoder wrote, checked against `expected` byte for byte.
fn check(name: &str, encode: impl FnOnce(&mut [u8]) -> Result<usize, EncodeError>, expected: &str) {
    let mut out = [0xaa; LONGEST];
    let length = encode(&mut out).unwrap();
    assert_eq!(out[..length], hex(expected), "{name}");
}

#[test]
fn the_set_up_frames_are_reproduced() {
    check(
        "CFG-PRT",
        |out| {
            CfgPrt {
                port_id: 1,
                tx_ready: TxReady::NONE,
                mode: UartMode::EIGHT_BITS
                    .union(UartMode::NO_PARITY)
                    .union(UartMode::ONE_STOP_BIT),
                baud_rate: 115_200,
                input_protocols: Protocols::UBX,
                output_protocols: Protocols::UBX,
                flags: PortFlags::NONE,
            }
            .encode(out)
        },
        PRT,
    );
    check("CFG-TP5", |out| TP5.encode(out), TP5_UTC);
    for (message, expected) in [
        (NAV_TIMEGPS, MSG_NAV_TIMEGPS),
        (NAV_PVT, MSG_NAV_PVT),
        (NAV_DOP, MSG_NAV_DOP),
        (NAV_SAT, MSG_NAV_SAT),
        (NAV_TIMELS, MSG_NAV_TIMELS),
        (NAV_POSECEF, MSG_NAV_POSECEF),
        (NAV_CLOCK, MSG_NAV_CLOCK),
        (RXM_RAWX, MSG_RXM_RAWX),
        (MON_HW, MSG_MON_HW),
        (TIM_TP, MSG_TIM_TP),
        (RXM_SFRBX, MSG_RXM_SFRBX),
        (NAV_EOE, MSG_NAV_EOE),
    ] {
        check(
            "CFG-MSG",
            |out| CfgMsg { message, rate: 1 }.encode(out),
            expected,
        );
    }
    check(
        "CFG-ANT",
        |out| {
            CfgAnt {
                flags: AntennaFlags::SUPPLY_VOLTAGE_CONTROL
                    .union(AntennaFlags::SHORT_CIRCUIT_DETECTION)
                    .union(AntennaFlags::POWER_DOWN_ON_SHORT_CIRCUIT)
                    .union(AntennaFlags::RECOVERY),
                pins: AntennaPins::new(16, 15, 14).union(AntennaPins::RECONFIGURE),
            }
            .encode(out)
        },
        ANT,
    );
    check(
        "CFG-RATE",
        |out| {
            CfgRate {
                measurement_rate: Milliseconds(1000),
                navigation_rate: 1,
                time_reference: TimeReference::Gps,
            }
            .encode(out)
        },
        RATE,
    );
    check(
        "CFG-RXM",
        |out| {
            CfgRxm {
                low_power_mode: LowPowerMode::Continuous,
            }
            .encode(out)
        },
        RXM,
    );
    let block = |system, maximum_tracking_channels, flags| GnssBlock {
        system,
        reserved_tracking_channels: 0,
        maximum_tracking_channels,
        flags,
    };
    let blocks = [
        GnssBlock {
            reserved_tracking_channels: 8,
            ..block(SYSTEM_GPS, 16, GnssFlags::ENABLE.union(GnssFlags::GPS_L1CA))
        },
        block(SYSTEM_SBAS, 3, GnssFlags::SBAS_L1CA),
        block(SYSTEM_GALILEO, 8, GnssFlags::GALILEO_E1),
        block(SYSTEM_BEIDOU, 16, GnssFlags::BEIDOU_B1I),
        block(SYSTEM_IMES, 8, GnssFlags::signals(0x01)),
        block(SYSTEM_QZSS, 3, GnssFlags::QZSS_L1CA),
        block(SYSTEM_GLONASS, 14, GnssFlags::GLONASS_L1),
    ];
    check(
        "CFG-GNSS",
        |out| {
            CfgGnss {
                hardware_tracking_channels: 0,
                tracking_channels: u8::MAX,
                blocks: &blocks,
            }
            .encode(out)
        },
        GNSS,
    );
    assert_eq!(hex(GNSS).len(), LONGEST);
    check(
        "CFG-NAV5",
        |out| {
            CfgNav5 {
                mask: Nav5Mask::DYNAMIC_MODEL
                    .union(Nav5Mask::MINIMUM_ELEVATION)
                    .union(Nav5Mask::POSITION_FIX_MODE)
                    .union(Nav5Mask::DEAD_RECKONING_LIMIT)
                    .union(Nav5Mask::POSITION_MASK)
                    .union(Nav5Mask::TIME_MASK)
                    .union(Nav5Mask::STATIC_HOLD_MASK)
                    .union(Nav5Mask::DGPS_MASK)
                    .union(Nav5Mask::CNO_THRESHOLD)
                    .union(Nav5Mask::UTC),
                dynamic_model: DynamicModel::Automotive,
                fix_mode: FixMode::Auto,
                fixed_altitude_variance: ALTITUDE_VARIANCE,
                minimum_elevation: Degrees(10),
                position_dop: Tenths(250),
                time_dop: Tenths(250),
                position_accuracy: Meters(100),
                time_accuracy: Meters(350),
                dgnss_timeout: Seconds(60),
                utc_standard: UtcStandard::Usno,
                ..NAV5_NONE
            }
            .encode(out)
        },
        NAV5_ALL,
    );
    check(
        "CFG-NAVX5",
        |out| {
            CfgNavx5 {
                mask1: Navx5Mask1::MINIMUM_MAXIMUM
                    .union(Navx5Mask1::MINIMUM_CNO)
                    .union(Navx5Mask1::INITIAL_3D_FIX)
                    .union(Navx5Mask1::WEEK_ROLLOVER)
                    .union(Navx5Mask1::ACK_AIDING)
                    .union(Navx5Mask1::PPP)
                    .union(Navx5Mask1::AOP),
                mask2: Navx5Mask2::NONE,
                minimum_satellites: 1,
                maximum_satellites: 32,
                minimum_cno: DecibelHertz(9),
                initial_fix_3d: false,
                ack_aiding: false,
                week_rollover: 2243,
                signal_attenuation_compensation: 0,
                use_ppp: false,
                aop_config: AopConfig::NONE,
                aop_orbit_maximum_error: Meters(100),
                use_adr: false,
            }
            .encode(out)
        },
        NAVX5,
    );
    check("CFG-TMODE2", |out| TMODE2_DISABLED.encode(out), TMODE2_OFF);
    let zero = F32Bits(0.0_f32.to_bits());
    check(
        "CFG-DAT",
        |out| {
            CfgDat {
                semi_major_axis: F64Bits(6_378_137.0_f64.to_bits()),
                inverse_flattening: F64Bits(298.257_223_563_f64.to_bits()),
                shift_x: zero,
                shift_y: zero,
                shift_z: zero,
                rotation_x: zero,
                rotation_y: zero,
                rotation_z: zero,
                scale: zero,
            }
            .encode(out)
        },
        DAT,
    );
    // The caller's save mask sets the reserved bits too; it is sent as
    // given.
    check(
        "CFG-CFG",
        |out| {
            CfgCfg {
                clear_mask: ConfigSections::NONE,
                save_mask: ConfigSections(0xffff),
                load_mask: ConfigSections::NONE,
                device_mask: DeviceMask::BBR.union(DeviceMask::FLASH),
            }
            .encode(out)
        },
        CFG,
    );
}

#[test]
fn the_navigation_settings_are_reproduced() {
    for (nav5, expected) in [
        (
            CfgNav5 {
                mask: Nav5Mask::MINIMUM_ELEVATION,
                minimum_elevation: Degrees(5),
                ..NAV5_NONE
            },
            NAV5_ELEVATION,
        ),
        (
            CfgNav5 {
                mask: Nav5Mask::DYNAMIC_MODEL,
                dynamic_model: DynamicModel::Airborne1G,
                ..NAV5_NONE
            },
            NAV5_AIRBORNE,
        ),
        (
            CfgNav5 {
                mask: Nav5Mask::DYNAMIC_MODEL,
                dynamic_model: DynamicModel::Pedestrian,
                ..NAV5_NONE
            },
            NAV5_PEDESTRIAN,
        ),
        (
            CfgNav5 {
                mask: Nav5Mask::POSITION_FIX_MODE,
                fix_mode: FixMode::TwoD,
                fixed_altitude: HUNDRED_METERS,
                fixed_altitude_variance: ALTITUDE_VARIANCE,
                ..NAV5_NONE
            },
            NAV5_2D,
        ),
        (
            CfgNav5 {
                mask: Nav5Mask::POSITION_FIX_MODE,
                fix_mode: FixMode::TwoD,
                fixed_altitude: ABOVE,
                fixed_altitude_variance: ALTITUDE_VARIANCE,
                ..NAV5_NONE
            },
            NAV5_2D_130M,
        ),
        (
            CfgNav5 {
                mask: Nav5Mask::POSITION_FIX_MODE,
                fix_mode: FixMode::Auto,
                ..NAV5_NONE
            },
            NAV5_AUTO,
        ),
    ] {
        check("CFG-NAV5", |out| nav5.encode(out), expected);
    }
}

#[test]
fn the_position_frames_are_reproduced() {
    check(
        "MGA-INI-POS_LLH",
        |out| {
            MgaIniPosLlh {
                latitude: LATITUDE,
                longitude: LONGITUDE,
                altitude: BELOW,
                position_accuracy: Centimeters(10_000_000),
            }
            .encode(out)
        },
        MGA_POSITION,
    );
    check("CFG-TMODE2", |out| TMODE2_HOLD.encode(out), TMODE2_FIXED);
    let hundred = CfgTmode2 {
        ecef_z_or_altitude: HUNDRED_METERS,
        ..TMODE2_HOLD
    };
    check("CFG-TMODE2", |out| hundred.encode(out), TMODE2_FIXED_100M);
    check("CFG-TMODE2", |out| TMODE2_DISABLED.encode(out), TMODE2_OFF);
}

#[test]
fn the_time_pulse_frames_are_reproduced() {
    let gps = CfgTp5 {
        flags: PULSE_FLAGS.union(PulseFlags::GRID_GPS),
        ..TP5
    };
    let late = CfgTp5 {
        user_config_delay: Nanoseconds(500),
        ..gps
    };
    let early = CfgTp5 {
        user_config_delay: Nanoseconds(-300),
        ..gps
    };
    let locked_only = CfgTp5 {
        pulse_length_ratio: 0,
        ..early
    };
    let off = CfgTp5 {
        flags: PulseFlags(early.flags.0 & !PulseFlags::ACTIVE.0),
        ..early
    };
    for (pulse, expected) in [
        (gps, TP5_GPS),
        (late, TP5_DELAY_500),
        (early, TP5_DELAY_MINUS_300),
        (locked_only, TP5_LOCKED_ONLY),
        (off, TP5_OFF),
    ] {
        check("CFG-TP5", |out| pulse.encode(out), expected);
    }
    assert_eq!(gps.flags.grid(), PulseFlags::GRID_GPS);
    assert_eq!(TP5.flags.grid(), PulseFlags::GRID_UTC);
    assert_eq!(gps.flags.sync_mode(), PulseFlags::SYNC_KEEP_LOCKED);
    assert!(!off.flags.contains(PulseFlags::ACTIVE));
}

/// The two configurations decoded are what was sent, through a
/// deframer and from the payload alone.
#[test]
fn a_polled_configuration_decodes_to_what_was_encoded() {
    let mut out = [0; LONGEST];
    for tmode2 in [TMODE2_DISABLED, TMODE2_HOLD] {
        let length = tmode2.encode(&mut out).unwrap();
        assert_eq!(
            decode(CFG_TMODE2, &out[OVERHEAD - 2..length - 2]),
            Some(Message::CfgTmode2(tmode2))
        );
    }
    let survey = CfgTmode2 {
        time_mode: TimeMode::SurveyIn,
        flags: TimeModeFlags::ALTITUDE_INVALID,
        ecef_x_or_latitude: -1,
        ecef_y_or_longitude: i32::MAX,
        ecef_z_or_altitude: Centimeters(i32::MIN),
        fixed_position_accuracy: Millimeters(1),
        survey_in_minimum_duration: Seconds(2),
        survey_in_accuracy_limit: Millimeters(3),
    };
    let unknown = CfgTmode2 {
        time_mode: TimeMode::Other(7),
        flags: TimeModeFlags(0x8000),
        ..survey
    };
    for tmode2 in [survey, unknown] {
        let length = tmode2.encode(&mut out).unwrap();
        assert_eq!(
            decode(CFG_TMODE2, &out[OVERHEAD - 2..length - 2]),
            Some(Message::CfgTmode2(tmode2))
        );
    }
    let second = CfgTp5 {
        time_pulse: TimePulse::Second,
        antenna_cable_delay: Nanoseconds(-50),
        rf_group_delay: Nanoseconds(20),
        frequency_period: 10_000_000,
        frequency_period_locked: 10,
        pulse_length_ratio: u32::MAX / 2,
        pulse_length_ratio_locked: 7,
        user_config_delay: Nanoseconds(i32::MIN),
        flags: PulseFlags::IS_FREQUENCY
            .union(PulseFlags::GRID_GALILEO)
            .union(PulseFlags::SYNC_SWITCH_BACK),
    };
    for tp5 in [TP5, second] {
        let length = tp5.encode(&mut out).unwrap();
        assert_eq!(
            decode(CFG_TP5, &out[OVERHEAD - 2..length - 2]),
            Some(Message::CfgTp5(tp5))
        );
    }
    assert_eq!(second.flags.grid(), PulseFlags::GRID_GALILEO);
    assert_eq!(second.flags.sync_mode(), PulseFlags::SYNC_SWITCH_BACK);
}

/// Version 0 of `CFG-TP5` (section 32.10.38.3, p. 271) has its own
/// flags, so it is not decoded as version 1.
#[test]
fn a_time_pulse_of_another_version_is_not_decoded() {
    let mut out = [0; LONGEST];
    let length = TP5.encode(&mut out).unwrap();
    let mut payload = out[OVERHEAD - 2..length - 2].to_vec();
    payload[1] = 0;
    assert_eq!(decode(CFG_TP5, &payload), None);
}

/// A poll request is framed, and a deframer passes it on without a
/// length error, but it is not decoded.
#[test]
fn a_poll_is_an_empty_frame_or_one_parameter() {
    let mut out = [0; LONGEST];
    for id in [
        CFG_PRT, CFG_MSG, CFG_DAT, CFG_RATE, CFG_RXM, CFG_ANT, CFG_NAVX5, CFG_NAV5, CFG_TP5,
        CFG_TMODE2, CFG_GNSS,
    ] {
        let length = poll(id, &mut out).unwrap();
        assert_eq!(out[..length], frame(id, &[]), "{id}");
        assert_eq!(decode(id, &[]), None, "{id}");
    }
    let length = CfgTp5Poll {
        time_pulse: TimePulse::Second,
    }
    .encode(&mut out)
    .unwrap();
    assert_eq!(out[..length], frame(CFG_TP5, &[1]));
    assert_eq!(decode(CFG_TP5, &[1]), None);
    let length = CfgPrtPoll { port_id: 1 }.encode(&mut out).unwrap();
    assert_eq!(out[..length], frame(CFG_PRT, &[1]));
    let length = CfgMsgPoll { message: NAV_PVT }.encode(&mut out).unwrap();
    assert_eq!(out[..length], frame(CFG_MSG, &[NAV_PVT.class, NAV_PVT.id]));
}

/// A configuration message of a length neither its own nor a poll's is
/// a length error.
#[test]
fn a_configuration_off_its_length_is_refused() {
    for (id, length) in [(CFG_TMODE2, 1), (CFG_TP5, 2), (CFG_TP5, 31)] {
        let bytes = frame(id, &vec![0; length]);
        let mut deframer = Deframer::<64>::new();
        let (_, event) = deframer.feed(&bytes);
        assert!(event.unwrap().is_err(), "{id} of {length}");
    }
}

#[test]
fn a_frame_names_its_message() {
    let mut out = [0; LONGEST];
    let length = TP5.encode(&mut out).unwrap();
    assert_eq!(message_id(&out[..length]), Some(CFG_TP5));
    assert_eq!(message_id(&hex(MGA_POSITION)), Some(MGA_INI));
    assert_eq!(message_id(&out[..3]), None);
    assert_eq!(message_id(&[0xb5, 0x63, 0x06, 0x31]), None);
    match decode(ACK_ACK, &[CFG_TP5.class, CFG_TP5.id]) {
        Some(Message::AckAck(ack)) => assert_eq!(Some(ack.message), message_id(&out)),
        other => panic!("{other:?}"),
    }
}

#[test]
fn an_encoder_needs_room_and_a_countable_number_of_blocks() {
    let mut out = [0; OVERHEAD + 31];
    assert_eq!(
        TP5.encode(&mut out),
        Err(EncodeError::NoRoom(NoRoom {
            needed: OVERHEAD + 32,
            room: OVERHEAD + 31,
        }))
    );
    let blocks = vec![
        GnssBlock {
            system: SYSTEM_GPS,
            reserved_tracking_channels: 0,
            maximum_tracking_channels: 0,
            flags: GnssFlags::NONE,
        };
        usize::from(u8::MAX) + 1
    ];
    let gnss = CfgGnss {
        hardware_tracking_channels: 0,
        tracking_channels: 0,
        blocks: &blocks,
    };
    let mut out = vec![0; OVERHEAD + cfg_gnss_length(blocks.len())];
    assert_eq!(gnss.encode(&mut out), Err(EncodeError::TooManyBlocks(256)));
    let gnss = CfgGnss {
        blocks: &blocks[1..],
        ..gnss
    };
    assert_eq!(gnss.encode(&mut out), Ok(out.len() - 8));
    assert_eq!(out[OVERHEAD - 2 + 3], u8::MAX);
}

/// The sections of the bitfield graphic, p. 194: bits 0 to 4 and 8 to
/// 12.
#[test]
fn every_configuration_section_is_the_named_ones() {
    assert_eq!(ConfigSections::ALL, ConfigSections(0x1f1f));
}

#[test]
fn a_listed_value_comes_back_as_it_went() {
    for value in 0..=u8::MAX {
        assert_eq!(DynamicModel::new(value).value(), value);
        assert_eq!(TimeMode::new(value).value(), value);
    }
    assert_eq!(FixMode::new(3), FixMode::Auto);
    assert_eq!(UtcStandard::new(4), UtcStandard::Other(4));
}
