//! Every field of each message, from payloads built by hand to the
//! layouts of UBX-13003221 R28: each field a value no other has, so a
//! field read from the wrong offset shows.

use std::vec::Vec;

use smartclock_gps::record::Antenna;
use smartclock_gps::record::Health;
use smartclock_gps::units::DecibelHertz;
use smartclock_gps::units::Decimeters;
use smartclock_gps::units::Degrees;
use smartclock_gps::units::Milliseconds;
use smartclock_gps::units::Nanoseconds;
use smartclock_gps::units::Seconds;

use super::decode;
use super::events;
use super::frame;
use super::payload;
use super::sfrbx;
use crate::ack::Ack;
use crate::frame::FrameError;
use crate::frame::MessageId;
use crate::message::ACK_ACK;
use crate::message::ACK_NAK;
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
use crate::message::NAV_VELECEF;
use crate::message::RXM_RAWX;
use crate::message::RXM_SFRBX;
use crate::message::TIM_TP;
use crate::mon::AntennaPower;
use crate::mon::MonHw;
use crate::nav::FixType;
use crate::nav::NavClock;
use crate::nav::NavDop;
use crate::nav::NavPvt;
use crate::nav::NavTimeGps;
use crate::nav::NavTimeLs;
use crate::nav::NavVelEcef;
use crate::nav::OrbitSource;
use crate::nav::SatelliteInfo;
use crate::rxm::Measurement;
use crate::rxm::RxmRawxHeader;
use crate::subframe::Body;
use crate::subframe::PREAMBLE;
use crate::subframe::Subframe;
use crate::subframe::tests::Bits;
use crate::tim::TimTp;
use crate::units::Centimeters;
use crate::units::CentimetersPerSecond;
use crate::units::F32Bits;
use crate::units::F64Bits;
use crate::units::HundredNanodegrees;
use crate::units::HundredthDegrees;
use crate::units::Hundredths;
use crate::units::Millimeters;
use crate::units::MillimetersPerSecond;
use crate::units::NanosecondsPerSecond;
use crate::units::Picoseconds;
use crate::units::PicosecondsPerSecond;
use crate::units::SubMilliseconds;
use crate::units::TenMicrodegrees;

/// Each message of a fixed length, given one byte more and one less
/// than its own, is dropped at its header.
#[test]
fn a_new_message_off_its_length_is_dropped() {
    for (id, length) in [
        (NAV_CLOCK, 20),
        (NAV_VELECEF, 20),
        (ACK_ACK, 2),
        (ACK_NAK, 2),
    ] {
        for wrong in [length - 1, length + 1] {
            let bytes = frame(id, &std::vec![0; wrong]);
            assert_eq!(
                events::<64>(&bytes),
                [Err(FrameError::BadLength { id, length: wrong })],
                "{id}"
            );
        }
    }
    // Blocks: a header and a part of a block.
    for (id, length) in [(RXM_RAWX, 16 + 31), (RXM_SFRBX, 8 + 3), (NAV_SAT, 7)] {
        let bytes = frame(id, &std::vec![0; length]);
        assert_eq!(
            events::<64>(&bytes),
            [Err(FrameError::BadLength { id, length })],
            "{id}"
        );
    }
}

/// `NAV-PVT` (pp. 375-378), every field.
#[test]
fn nav_pvt_every_field() {
    let bytes: [u8; 92] = payload(&[
        (0, &1_000_u32.to_le_bytes()),
        (4, &2026_u16.to_le_bytes()),
        (6, &[11, 15, 13, 14, 59]),
        (11, &[0b1101]),
        (12, &17_u32.to_le_bytes()),
        (16, &(-18_i32).to_le_bytes()),
        // fixType 4; flags gnssFixOK, diffSoln, psmState 5,
        // headVehValid, carrSoln 2; flags2 confirmedAvai and
        // confirmedTime; numSV 9.
        (20, &[4, 0b1011_0111, 0b1010_0000, 9]),
        (24, &(-1_219_978_333_i32).to_le_bytes()),
        (28, &373_255_833_i32.to_le_bytes()),
        (32, &22_995_i32.to_le_bytes()),
        (36, &55_000_i32.to_le_bytes()),
        (40, &41_u32.to_le_bytes()),
        (44, &42_u32.to_le_bytes()),
        (48, &(-43_i32).to_le_bytes()),
        (52, &44_i32.to_le_bytes()),
        (56, &(-45_i32).to_le_bytes()),
        (60, &46_i32.to_le_bytes()),
        (64, &9_000_000_i32.to_le_bytes()),
        (68, &47_u32.to_le_bytes()),
        (72, &48_u32.to_le_bytes()),
        (76, &149_u16.to_le_bytes()),
        // invalidLlh, lastCorrectionAge 11.
        (78, &0b1_0111_u16.to_le_bytes()),
        (84, &(-49_i32).to_le_bytes()),
        (88, &(-1_234_i16).to_le_bytes()),
        (90, &50_u16.to_le_bytes()),
    ]);
    let Some(Message::NavPvt(pvt)) = decode(NAV_PVT, &bytes) else {
        panic!();
    };
    let NavPvt {
        time_of_week,
        date,
        time_accuracy,
        fix_type,
        fix_ok,
        differential,
        vehicle_heading_valid,
        power_save,
        carrier_solution,
        satellites,
        longitude,
        latitude,
        height,
        mean_sea_level,
        horizontal_accuracy,
        vertical_accuracy,
        north,
        east,
        down,
        ground_speed,
        motion_heading,
        speed_accuracy,
        heading_accuracy,
        position_dop,
        position_invalid,
        correction_age,
        vehicle_heading,
        magnetic_declination,
        magnetic_accuracy,
        magnetic_valid,
    } = pvt;
    assert_eq!(time_of_week, Milliseconds(1_000));
    assert_eq!(
        (
            date.year,
            date.month,
            date.day,
            date.hour,
            date.minute,
            date.second
        ),
        (2026, 11, 15, 13, 14, 59)
    );
    assert_eq!(date.nanoseconds, Nanoseconds(-18));
    assert!(date.date_valid && !date.time_valid && date.fully_resolved);
    assert!(date.confirmation_available && !date.date_confirmed && date.time_confirmed);
    assert_eq!(time_accuracy, Nanoseconds(17));
    assert_eq!(fix_type, FixType::WithDeadReckoning);
    assert!(fix_ok && differential && vehicle_heading_valid);
    assert_eq!((power_save, carrier_solution), (5, 2));
    assert_eq!(satellites, 9);
    assert_eq!(longitude, HundredNanodegrees(-1_219_978_333));
    assert_eq!(latitude, HundredNanodegrees(373_255_833));
    assert_eq!(height, Millimeters(22_995));
    assert_eq!(mean_sea_level, Millimeters(55_000));
    assert_eq!(horizontal_accuracy, Millimeters(41));
    assert_eq!(vertical_accuracy, Millimeters(42));
    assert_eq!(north, MillimetersPerSecond(-43));
    assert_eq!(east, MillimetersPerSecond(44));
    assert_eq!(down, MillimetersPerSecond(-45));
    assert_eq!(ground_speed, MillimetersPerSecond(46));
    assert_eq!(motion_heading, TenMicrodegrees(9_000_000));
    assert_eq!(speed_accuracy, MillimetersPerSecond(47));
    assert_eq!(heading_accuracy, TenMicrodegrees(48));
    assert_eq!(position_dop, Hundredths(149));
    assert!(position_invalid);
    assert_eq!(correction_age, 11);
    assert_eq!(pvt.position(), None);
    assert_eq!(vehicle_heading, TenMicrodegrees(-49));
    assert_eq!(magnetic_declination, HundredthDegrees(-1_234));
    assert_eq!(magnetic_accuracy, HundredthDegrees(50));
    assert!(magnetic_valid);
    // Every fixType, the ones the document does not give kept.
    for (value, kind) in [
        (0, FixType::None),
        (1, FixType::DeadReckoning),
        (2, FixType::TwoD),
        (3, FixType::ThreeD),
        (5, FixType::TimeOnly),
        (6, FixType::Other(6)),
    ] {
        let mut bytes = bytes;
        bytes[20] = value;
        let Some(Message::NavPvt(pvt)) = decode(NAV_PVT, &bytes) else {
            panic!();
        };
        assert_eq!(pvt.fix_type, kind);
    }
}

/// `NAV-TIMEGPS` (p. 397): the raw week and offset, each validity bit
/// on its own.
#[test]
fn nav_timegps_raw_fields() {
    for (valid, flags) in [
        (0b001, (true, false, false)),
        (0b010, (false, true, false)),
        (0b100, (false, false, true)),
    ] {
        let bytes: [u8; 16] = payload(&[
            (0, &5_u32.to_le_bytes()),
            (8, &(-3_i16).to_le_bytes()),
            (10, &(-7_i8).to_le_bytes()),
            (11, &[valid]),
        ]);
        let Some(Message::NavTimeGps(time)) = decode(NAV_TIMEGPS, &bytes) else {
            panic!();
        };
        let NavTimeGps {
            week_number,
            gps_less_utc,
            time_of_week_valid,
            week_valid,
            leap_seconds_valid,
            ..
        } = time;
        assert_eq!((week_number, gps_less_utc), (-3, Seconds(-7)));
        assert_eq!(
            (time_of_week_valid, week_valid, leap_seconds_valid),
            flags,
            "{valid}"
        );
    }
}

/// `NAV-TIMELS` (pp. 398-400): every field as sent, whatever the
/// checks of `count` and `notice` make of them.
#[test]
fn nav_timels_raw_fields() {
    let bytes: [u8; 24] = payload(&[
        (0, &6_u32.to_le_bytes()),
        (8, &[255, 18, 7, (-1_i8).cast_unsigned()]),
        (12, &(-86_400_i32).to_le_bytes()),
        (16, &2_400_u16.to_le_bytes()),
        (18, &9_u16.to_le_bytes()),
        (23, &[0b10]),
    ]);
    let Some(Message::NavTimeLs(leap)) = decode(NAV_TIMELS, &bytes) else {
        panic!();
    };
    assert_eq!(
        leap,
        NavTimeLs {
            time_of_week: Milliseconds(6),
            count_source: 255,
            current: Seconds(18),
            change_source: 7,
            change: -1,
            until: Seconds(-86_400),
            event_week: 2_400,
            event_day: 9,
            count_valid: false,
            event_valid: true,
        }
    );
    assert_eq!(leap.count(), None);
    assert_eq!(leap.notice(), None);
}

/// `NAV-DOP` (p. 361): all seven, northing and easting too.
#[test]
fn nav_dop_every_field() {
    let dops: [u16; 7] = [101, 202, 303, 404, 505, 606, 707];
    let mut bytes = 7_u32.to_le_bytes().to_vec();
    bytes.extend(dops.iter().flat_map(|dop| dop.to_le_bytes()));
    let Some(Message::NavDop(dop)) = decode(NAV_DOP, &bytes) else {
        panic!();
    };
    assert_eq!(
        dop,
        NavDop {
            time_of_week: Milliseconds(7),
            geometric: Hundredths(101),
            position: Hundredths(202),
            time: Hundredths(303),
            vertical: Hundredths(404),
            horizontal: Hundredths(505),
            northing: Hundredths(606),
            easting: Hundredths(707),
        }
    );
}

/// `NAV-POSECEF`'s accuracy (p. 374).
#[test]
fn nav_posecef_accuracy() {
    let bytes: [u8; 20] = payload(&[(16, &1_234_u32.to_le_bytes())]);
    let Some(Message::NavPosEcef(ecef)) = decode(NAV_POSECEF, &bytes) else {
        panic!();
    };
    assert_eq!(ecef.accuracy, Centimeters(1_234));
}

/// `NAV-VELECEF` and `NAV-CLOCK`, both of the layout `iTOW` and four
/// fields of four bytes, and each an epoch's.
#[test]
fn nav_velecef_and_nav_clock() {
    let bytes: [u8; 20] = payload(&[
        (0, &8_u32.to_le_bytes()),
        (4, &(-1_i32).to_le_bytes()),
        (8, &2_i32.to_le_bytes()),
        (12, &(-3_i32).to_le_bytes()),
        (16, &4_u32.to_le_bytes()),
    ]);
    let velocity = decode(NAV_VELECEF, &bytes).unwrap();
    assert_eq!(
        velocity,
        Message::NavVelEcef(NavVelEcef {
            time_of_week: Milliseconds(8),
            x: CentimetersPerSecond(-1),
            y: CentimetersPerSecond(2),
            z: CentimetersPerSecond(-3),
            accuracy: CentimetersPerSecond(4),
        })
    );
    assert_eq!(velocity.epoch(), Some(Milliseconds(8)));
    let clock = decode(NAV_CLOCK, &bytes).unwrap();
    assert_eq!(
        clock,
        Message::NavClock(NavClock {
            time_of_week: Milliseconds(8),
            bias: Nanoseconds(-1),
            drift: NanosecondsPerSecond(2),
            time_accuracy: Nanoseconds((-3_i32).cast_unsigned()),
            frequency_accuracy: PicosecondsPerSecond(4),
        })
    );
    assert_eq!(clock.epoch(), Some(Milliseconds(8)));
}

/// One `NAV-SAT` block with `flags`.
fn satellite(flags: u32) -> SatelliteInfo {
    let mut bytes = payload::<8>(&[(4, &[1, 1])]).to_vec();
    bytes.extend(payload::<12>(&[
        (0, &[0, 7, 45, (-5_i8).cast_unsigned()]),
        (4, &(-17_i16).to_le_bytes()),
        (6, &(-123_i16).to_le_bytes()),
        (8, &flags.to_le_bytes()),
    ]));
    let Some(Message::NavSat(sat)) = decode(NAV_SAT, &bytes) else {
        panic!();
    };
    let [info] = sat.satellites().collect::<Vec<_>>()[..] else {
        panic!();
    };
    info
}

/// `NAV-SAT`'s block (pp. 381-383): each flag on its own, and the
/// fields of more than one bit at their ends.
#[test]
fn nav_sat_every_flag() {
    let none = satellite(0);
    assert_eq!(
        (
            none.system,
            none.id,
            none.signal,
            none.elevation,
            none.azimuth
        ),
        (0, 7, DecibelHertz(45), Degrees(-5), Degrees(-17))
    );
    assert_eq!(none.residual, Decimeters(-123));
    assert_eq!(none.orbit_source, OrbitSource::None);
    assert_eq!(none.health, Health::Unknown);
    type Read = fn(&SatelliteInfo) -> bool;
    let bits: [(u32, Read); 18] = [
        (3, |info| info.used),
        (6, |info| info.differential),
        (7, |info| info.smoothed),
        (11, |info| info.ephemeris),
        (12, |info| info.almanac),
        (13, |info| info.assist_now_offline),
        (14, |info| info.assist_now_autonomous),
        (16, |info| info.sbas_corrections),
        (17, |info| info.rtcm_corrections),
        (18, |info| info.slas_corrections),
        (19, |info| info.spartn_corrections),
        (20, |info| info.pseudorange_corrections),
        (21, |info| info.carrier_corrections),
        (22, |info| info.doppler_corrections),
        (23, |info| info.clas_corrections),
        (0, |info| info.quality == 1),
        (4, |info| info.health == Health::Healthy),
        (8, |info| info.orbit_source == OrbitSource::Ephemeris),
    ];
    for (bit, read) in bits {
        assert!(read(&satellite(1 << bit)), "bit {bit}");
        for (other, read) in bits {
            if other != bit {
                assert!(!read(&satellite(1 << bit)), "bit {bit} read as {other}");
            }
        }
    }
    assert_eq!(satellite(0b111).quality, 7);
    assert_eq!(satellite(2 << 4).health, Health::Unhealthy);
    for (value, source) in [
        (2, OrbitSource::Almanac),
        (3, OrbitSource::AssistNowOffline),
        (4, OrbitSource::AssistNowAutonomous),
        (7, OrbitSource::Other(7)),
    ] {
        assert_eq!(satellite(value << 8).orbit_source, source);
    }
}

/// A `NAV-SAT` whose `numSvs` is not its length's count of blocks, or
/// of another version, is not decoded.
#[test]
fn nav_sat_count_must_be_its_lengths() {
    let mut bytes = payload::<8>(&[(4, &[1, 2])]).to_vec();
    bytes.extend([0; 12]);
    assert_eq!(decode(NAV_SAT, &bytes), None);
    bytes[5] = 1;
    assert!(decode(NAV_SAT, &bytes).is_some());
    bytes[4] = 2;
    assert_eq!(decode(NAV_SAT, &bytes), None);
}

/// An `RXM-RAWX` header (section 32.18.4).
fn rawx_header(measurements: u8) -> [u8; 16] {
    payload(&[
        (0, &123_456.5_f64.to_bits().to_le_bytes()),
        (8, &2_400_u16.to_le_bytes()),
        (10, &[18, measurements, 0b11, 1]),
    ])
}

/// An `RXM-RAWX` block of satellite `id`.
fn rawx_block(id: u8, track: u8) -> [u8; 32] {
    payload(&[
        (0, &21_000_000.25_f64.to_bits().to_le_bytes()),
        (8, &(-1.5_f64).to_bits().to_le_bytes()),
        (16, &(-2_500.5_f32).to_bits().to_le_bytes()),
        (20, &[0, id, 0, 9]),
        (24, &64_500_u16.to_le_bytes()),
        (26, &[41, 0xf3, 0xf4, 0xf5, track]),
    ])
}

/// `RXM-RAWX`: the header and every field of a block, the
/// floating-point ones as their bits.
#[test]
fn rxm_rawx_every_field() {
    let mut bytes = rawx_header(2).to_vec();
    bytes.extend(rawx_block(5, 0b0001));
    bytes.extend(rawx_block(6, 0b1110));
    let Some(Message::RxmRawx(rawx)) = decode(RXM_RAWX, &bytes) else {
        panic!();
    };
    assert_eq!(
        rawx.header,
        RxmRawxHeader {
            receiver_time_of_week: F64Bits(123_456.5_f64.to_bits()),
            week: 2_400,
            gps_less_utc: Seconds(18),
            measurements: 2,
            leap_seconds_known: true,
            clock_reset: true,
        }
    );
    let [first, second] = rawx.measurements().collect::<Vec<_>>()[..] else {
        panic!();
    };
    assert_eq!(
        first,
        Measurement {
            pseudorange: F64Bits(21_000_000.25_f64.to_bits()),
            carrier_phase: F64Bits((-1.5_f64).to_bits()),
            doppler: F32Bits((-2_500.5_f32).to_bits()),
            system: 0,
            id: 5,
            frequency: 9,
            lock_time: Milliseconds(64_500),
            signal: DecibelHertz(41),
            pseudorange_deviation: 3,
            carrier_phase_deviation: 4,
            doppler_deviation: 5,
            pseudorange_valid: true,
            carrier_phase_valid: false,
            half_cycle_valid: false,
            half_cycle_subtracted: false,
        }
    );
    assert_eq!(f64::from_bits(first.pseudorange.0), 21_000_000.25);
    assert_eq!(f32::from_bits(first.doppler.0), -2_500.5);
    assert_eq!(second.id, 6);
    assert!(!second.pseudorange_valid);
    assert!(second.carrier_phase_valid && second.half_cycle_valid && second.half_cycle_subtracted);
    assert_eq!(Message::RxmRawx(rawx).epoch(), None);
    // numMeas must be the length's count.
    let mut wrong = bytes.clone();
    wrong[11] = 3;
    assert_eq!(decode(RXM_RAWX, &wrong), None);
}

/// `RXM-SFRBX` (section 32.18.7): the header and the words as sent.
#[test]
fn rxm_sfrbx_every_field() {
    let words: [u32; 10] = core::array::from_fn(|index| 0xc000_0000 | (index as u32) << 8);
    let mut bytes = std::vec![0, 17, 5, 3, 10, 6, 2, 0];
    bytes.extend(words.iter().flat_map(|word| word.to_le_bytes()));
    let Some(Message::RxmSfrbx(subframe)) = decode(RXM_SFRBX, &bytes) else {
        panic!();
    };
    assert_eq!(
        (
            subframe.system,
            subframe.id,
            subframe.signal,
            subframe.frequency,
            subframe.count,
            subframe.channel,
            subframe.version
        ),
        (0, 17, 5, 3, 10, 6, 2)
    );
    assert_eq!(subframe.words().collect::<Vec<_>>(), words);
    assert_eq!(Message::RxmSfrbx(subframe).epoch(), None);
    // numWords must be the length's count.
    bytes[4] = 9;
    assert_eq!(decode(RXM_SFRBX, &bytes), None);
}

/// A GPS subframe from `RXM-SFRBX` to its fields: subframe 1 of week
/// 905, each word with padding above its 30 bits.
#[test]
fn rxm_sfrbx_gives_a_gps_subframe() {
    let words = Bits::new().head(1).set(61, 10, 905).words();
    let Some(Message::RxmSfrbx(message)) = decode(RXM_SFRBX, &sfrbx(0, &words)) else {
        panic!();
    };
    let subframe = Subframe::decode(&message.gps_words().unwrap()).unwrap();
    assert_eq!(subframe.telemetry.preamble, PREAMBLE);
    let Body::Subframe1(clock) = subframe.body else {
        panic!("{subframe:?}");
    };
    assert_eq!(clock.week, 905);
    // Another system's, or other than ten words, is no GPS subframe.
    for (system, count) in [(2, 10), (0, 8)] {
        let Some(Message::RxmSfrbx(message)) = decode(RXM_SFRBX, &sfrbx(system, &words[..count]))
        else {
            panic!();
        };
        assert_eq!(message.gps_words(), None, "{system} {count}");
    }
}

#[test]
fn ack_and_nak_name_the_message() {
    let message = MessageId::new(0x06, 0x31);
    assert_eq!(
        decode(ACK_ACK, &[0x06, 0x31]),
        Some(Message::AckAck(Ack { message }))
    );
    assert_eq!(
        decode(ACK_NAK, &[0x06, 0x31]),
        Some(Message::AckNak(Ack { message }))
    );
}

/// `MON-HW` (pp. 342-343), every field.
#[test]
fn mon_hw_every_field() {
    let pins: [u8; 17] = core::array::from_fn(|index| index as u8 + 100);
    let bytes: [u8; 60] = payload(&[
        (0, &1_u32.to_le_bytes()),
        (4, &2_u32.to_le_bytes()),
        (8, &3_u32.to_le_bytes()),
        (12, &4_u32.to_le_bytes()),
        (16, &5_u16.to_le_bytes()),
        (18, &6_u16.to_le_bytes()),
        // aStatus SHORT, aPower ON, flags rtcCalib, jammingState 2,
        // xtalAbsent.
        (20, &[3, 1, 0b1_1001]),
        (24, &7_u32.to_le_bytes()),
        (28, &pins),
        (45, &[8]),
        (48, &9_u32.to_le_bytes()),
        (52, &10_u32.to_le_bytes()),
        (56, &11_u32.to_le_bytes()),
    ]);
    let Some(Message::MonHw(hardware)) = decode(MON_HW, &bytes) else {
        panic!();
    };
    assert_eq!(
        hardware,
        MonHw {
            pin_select: 1,
            pin_bank: 2,
            pin_direction: 3,
            pin_value: 4,
            noise: 5,
            agc_count: 6,
            antenna: Antenna::Shorted,
            antenna_power: AntennaPower::On,
            rtc_calibrated: true,
            safe_boot: false,
            jamming_state: 2,
            crystal_absent: true,
            used_mask: 7,
            virtual_pins: pins,
            cw_suppression: 8,
            pin_irq: 9,
            pull_high: 10,
            pull_low: 11,
        }
    );
    for (power, decoded) in [
        (0, AntennaPower::Off),
        (2, AntennaPower::Unknown),
        (9, AntennaPower::Unknown),
    ] {
        let mut bytes = bytes;
        bytes[21] = power;
        bytes[22] = 0b10;
        let Some(Message::MonHw(hardware)) = decode(MON_HW, &bytes) else {
            panic!();
        };
        assert_eq!(hardware.antenna_power, decoded);
        assert!(hardware.safe_boot && !hardware.rtc_calibrated);
    }
}

/// `TIM-TP`'s fields beyond those a record uses (p. 433).
#[test]
fn tim_tp_every_field() {
    let bytes: [u8; 16] = payload(&[
        (0, &1_u32.to_le_bytes()),
        (4, &0x8000_0000_u32.to_le_bytes()),
        (8, &(-2_i32).to_le_bytes()),
        (12, &3_u16.to_le_bytes()),
        (14, &[0b10, 0x3f]),
    ]);
    let Some(Message::TimTp(pulse)) = decode(TIM_TP, &bytes) else {
        panic!();
    };
    assert_eq!(
        pulse,
        TimTp {
            time_of_week: Milliseconds(1),
            sub_millisecond: SubMilliseconds(0x8000_0000),
            week: 3,
            quantization_error: Some(Picoseconds(-2)),
            utc_base: false,
            utc_available: true,
            locked: true,
            raim: None,
            time_reference: 15,
            utc_standard: 3,
        }
    );
}

/// `NAV-EOE` is an epoch's, and decodes to its `iTOW`.
#[test]
fn nav_eoe_is_its_epochs() {
    assert_eq!(
        decode(NAV_EOE, &9_u32.to_le_bytes()).unwrap().epoch(),
        Some(Milliseconds(9))
    );
}
