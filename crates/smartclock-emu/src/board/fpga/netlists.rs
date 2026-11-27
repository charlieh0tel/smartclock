//! The bitstreams the LCA model knows, and the netlists it runs for
//! them (docs/firmware/fpga-codegen.md, "The board file names the LCA,
//! and the configuration picks the netlist"): each of the repository's
//! `.bit` files (outputs.md, "The FPGA bitstreams") by its name, the
//! SHA-256 of its configuration as the part takes it, the bits to DONE,
//! and its netlist as tools/fpga/lcanet.py writes it, where there is
//! one.  The part's configuration picks the netlist by that hash, not
//! by anything the board file says; a board file may assert the name.
//!
//! The hash is of the bits to DONE because the part keeps no more: a
//! `.bit` runs a few bytes past DONE, which the firmware sends after it
//! (fpga.rs).  The test below holds each hash to its `.bit`, and each
//! netlist's `bitstream` line, the whole `.bit`'s SHA-256, to the same
//! file.

use std::sync::LazyLock;

use sha2::Digest;
use sha2::Sha256;

use super::lca::Netlist;
use super::pins::ACLK_ALONE;
use super::pins::ACLK_Z3815A;
use super::pins::TEN_MHZ_ALONE;
use super::pins::Wiring;

/// A SHA-256, as its bytes.
type Hash = [u8; 32];

/// A bitstream the LCA model knows.
#[derive(Debug)]
struct Bitstream {
    /// Its name, `third_party/firmware/images/fpga/NAME.bit`'s.
    name: &'static str,
    /// The SHA-256 of its configuration's bits to DONE, as the part
    /// packs them, the first bit highest, hex.
    configuration: &'static str,
    /// Its netlist, lcanet.py's text, if the model has one.
    netlist: Option<&'static str>,
    /// How the board wires its design's own pins, if it does yet.
    wiring: Option<&'static Wiring>,
}

/// Every bitstream of the repository's.
const BITSTREAMS: [Bitstream; 6] = [
    Bitstream {
        name: "z3805a",
        configuration: "fba734d0040d7e82a171551f6a6cc3a3f99210bdba9450236092e26ed039d2aa",
        netlist: Some(include_str!("z3805a.netlist")),
        wiring: Some(&TEN_MHZ_ALONE),
    },
    Bitstream {
        name: "58503a-z3801a",
        configuration: "557a91b91e7856e899d3996ecf84af7bcde82c3cd83b6788e47aff1ccbeaf754",
        netlist: Some(include_str!("58503a-z3801a.netlist")),
        wiring: Some(&TEN_MHZ_ALONE),
    },
    Bitstream {
        name: "58503b",
        configuration: "851e60168d49249fab918c1752ef97d2644e1883c9d1781765770d88f8b6d838",
        netlist: Some(include_str!("58503b.netlist")),
        wiring: Some(&TEN_MHZ_ALONE),
    },
    Bitstream {
        name: "z3815a",
        configuration: "42e5d782d1d1ae468d7669b0ccf6be430f45cbe33f794b673548d3bc4cc3f6f5",
        netlist: Some(include_str!("z3815a.netlist")),
        wiring: Some(&ACLK_Z3815A),
    },
    Bitstream {
        name: "z3816a",
        configuration: "f8c6e3e3e2190d69ec0172bcff8201a186384ccb068bf7c535234cc8e3cc8c20",
        netlist: Some(include_str!("z3816a.netlist")),
        wiring: Some(&ACLK_ALONE),
    },
    // the board's LCA model takes the internal 1 PPS on P83 and the
    // interval on P81 (pins.rs), where the Z3817A's design has P78 and
    // no interval, and drives no P84: its board runs the hand model
    Bitstream {
        name: "z3817a",
        configuration: "392000dc3b202015b5feb3f98782ee5b10ec1f60d8ec593ce66bba4c42fb7c10",
        netlist: Some(include_str!("z3817a.netlist")),
        wiring: None,
    },
];

/// Each bitstream's netlist parsed, at its first use, by `BITSTREAMS`'
/// index.  The netlists are the repository's, which the tests parse:
/// one that does not parse is a build's fault, not a run's.
static NETLISTS: LazyLock<Vec<Option<Netlist>>> = LazyLock::new(|| {
    BITSTREAMS
        .iter()
        .map(|bitstream| {
            bitstream.netlist.map(|text| {
                Netlist::parse(text)
                    .unwrap_or_else(|error| panic!("{}'s netlist: {error}", bitstream.name))
            })
        })
        .collect()
});

/// A configuration the model has matched to a bitstream it knows.
#[derive(Debug, Clone, Copy)]
pub(super) struct Known {
    /// The bitstream's name.
    pub(super) name: &'static str,
    /// Its netlist, if the model has one.
    pub(super) netlist: Option<&'static Netlist>,
    /// How the board wires its design's own pins, if it does yet.
    pub(super) wiring: Option<&'static Wiring>,
}

/// The names of the bitstreams the model has a netlist for, which a
/// board file may assert.
pub(super) fn named() -> impl Iterator<Item = &'static str> {
    BITSTREAMS
        .iter()
        .filter(|bitstream| bitstream.netlist.is_some())
        .map(|bitstream| bitstream.name)
}

/// The bitstream whose configuration's bits to DONE are `bits`, the
/// first first, if the model knows it.
pub(super) fn configured(bits: &[bool]) -> Result<Known, String> {
    let packed: Vec<u8> = bits
        .chunks(u8::BITS as usize)
        .map(|byte| {
            byte.iter().enumerate().fold(0, |packed, (bit, high)| {
                packed | u8::from(*high) << (u8::BITS as usize - 1 - bit)
            })
        })
        .collect();
    let hash: Hash = Sha256::digest(&packed).into();
    let hex = hex(&hash);
    BITSTREAMS
        .iter()
        .zip(NETLISTS.iter())
        .find(|(bitstream, _)| bitstream.configuration == hex)
        .map(|(bitstream, netlist)| Known {
            name: bitstream.name,
            netlist: netlist.as_ref(),
            wiring: bitstream.wiring,
        })
        .ok_or(hex)
}

/// `bytes` as lowercase hex.
fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}

#[cfg(test)]
mod tests {
    use sha2::Digest;
    use sha2::Sha256;

    use super::super::executed::Wired;
    use super::BITSTREAMS;
    use super::NETLISTS;
    use super::configured;
    use super::hex;

    /// The repository's bitstreams.
    const BITSTREAM_DIRECTORY: &str = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../third_party/firmware/images/fpga"
    );
    /// An XC3042's bits to DONE (fpga.rs): the 30828 of the length
    /// count and the start-up's, to the byte.
    const DONE_BITS: usize = 30832;

    /// The bits of `bytes`, the first highest.
    fn bits(bytes: &[u8]) -> Vec<bool> {
        bytes
            .iter()
            .flat_map(|byte| (0..u8::BITS).rev().map(move |bit| byte >> bit & 1 != 0))
            .collect()
    }

    /// Each hash is its `.bit`'s to DONE, picks that bitstream, and the
    /// netlist, where there is one, was decoded from that `.bit`.
    #[test]
    fn each_bitstream_is_picked_by_its_configuration() {
        for (bitstream, netlist) in BITSTREAMS.iter().zip(NETLISTS.iter()) {
            let file = std::fs::read(format!("{BITSTREAM_DIRECTORY}/{}.bit", bitstream.name))
                .expect("the bitstream");
            let known = configured(&bits(&file)[..DONE_BITS]).expect("known");
            assert_eq!(known.name, bitstream.name);
            assert_eq!(known.netlist.is_some(), bitstream.netlist.is_some());
            if let Some(netlist) = netlist {
                assert_eq!(
                    netlist.bitstream(),
                    hex(&Sha256::digest(&file)),
                    "{}",
                    bitstream.name
                );
            }
        }
    }

    /// Every netlist the board wires bonds every pin its wiring and the
    /// shared pins name.
    #[test]
    fn each_wired_netlist_bonds_its_pins() {
        for (bitstream, netlist) in BITSTREAMS.iter().zip(NETLISTS.iter()) {
            if let (Some(netlist), Some(wiring)) = (netlist, bitstream.wiring) {
                let wired = Wired { netlist, wiring };
                assert_eq!(wired.unwired(), None, "{}", bitstream.name);
            }
        }
    }

    #[test]
    fn noise_picks_none() {
        assert!(configured(&[true; DONE_BITS]).is_err());
    }
}
