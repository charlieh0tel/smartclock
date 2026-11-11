//! The FPGA bitstreams in `third_party/firmware/images/fpga/` are byte-for-byte
//! copies of the ones the firmware images load, at the offsets
//! `docs/firmware/outputs.md` gives, with the hashes and the byte
//! differences it and `docs/firmware/fpga-comparison.md` quote.

use std::fs;
use std::path::PathBuf;

use sha2::Digest;
use sha2::Sha256;

/// Bytes in each XC3042 bitstream, header to stop bits.
const LENGTH: usize = 3860;

/// The XC3000 preamble and the start of the length count.
const HEADER: [u8; 4] = [0xff, 0x20, 0x07, 0x86];

/// Each image, where its bitstream starts, and the file that copies it.
const BITSTREAMS: [(&str, usize, &str); 8] = [
    ("58503a-3633.bin", 0x1ff84, "58503a-z3801a.bit"),
    ("58503a-3704.bin", 0x1fd16, "58503a-z3801a.bit"),
    ("z3801a-3543.bin", 0x1fddc, "58503a-z3801a.bit"),
    ("z3805a-3543b.bin", 0x20f18, "z3805a.bit"),
    ("58503b-1.01.04.bin", 0x2f5a8, "58503b.bit"),
    ("z3815a-4010.bin", 0x316a6, "z3815a.bit"),
    ("z3816a-4001.bin", 0x2ffb8, "z3816a.bit"),
    ("z3817a-3752.bin", 0x2ffe4, "z3817a.bit"),
];

/// The block after each image's bitstream, the readback mask, where it
/// starts in each image, as `BITSTREAMS` orders them.
const SECOND_BLOCKS: [usize; 8] = [
    0x20ea2, 0x20c34, 0x20cfa, 0x21e36, 0x304c6, 0x325c4, 0x30ed6, 0x30f02,
];

/// Each bitstream file's SHA-256, its first four bytes in hex, as the
/// docs quote it.
const HASHES: [(&str, &str); 6] = [
    ("58503a-z3801a.bit", "b9fcb082"),
    ("z3805a.bit", "aa4750d3"),
    ("58503b.bit", "f095d6b5"),
    ("z3815a.bit", "3402456a"),
    ("z3816a.bit", "0bb50333"),
    ("z3817a.bit", "4dc3aa57"),
];

/// The second block's SHA-256 prefix, the same in every image.
const SECOND_BLOCK_HASH: &str = "d64b97e1";

/// Bytes in which two bitstream files differ.
const DIFFERENCES: [(&str, &str, usize); 7] = [
    ("58503a-z3801a.bit", "z3805a.bit", 1868),
    ("58503a-z3801a.bit", "58503b.bit", 2347),
    ("58503a-z3801a.bit", "z3816a.bit", 3315),
    ("58503a-z3801a.bit", "z3815a.bit", 3392),
    ("z3815a.bit", "z3816a.bit", 1686),
    ("58503a-z3801a.bit", "z3817a.bit", 3310),
    ("58503b.bit", "z3817a.bit", 3292),
];

/// The first four bytes of `data`'s SHA-256, in hex.
fn hash_prefix(data: &[u8]) -> String {
    Sha256::digest(data)
        .iter()
        .take(4)
        .map(|byte| format!("{byte:02x}"))
        .collect()
}

fn firmware(name: &str) -> Vec<u8> {
    let path: PathBuf = [
        env!("CARGO_MANIFEST_DIR"),
        "../../third_party/firmware/images",
        name,
    ]
    .iter()
    .collect();
    fs::read(&path).unwrap_or_else(|e| panic!("{}: {e}", path.display()))
}

#[test]
fn each_bitstream_file_is_the_one_its_images_load() {
    for (image, offset, file) in BITSTREAMS {
        let image_bytes = firmware(image);
        let loaded = &image_bytes[offset..offset + LENGTH];
        assert_eq!(loaded[..HEADER.len()], HEADER, "{image} at {offset:#x}");
        assert_eq!(
            loaded,
            firmware(&format!("fpga/{file}")),
            "{file} differs from {image} at {offset:#x}"
        );
    }
}

#[test]
fn the_bitstreams_hash_as_quoted() {
    for (file, prefix) in HASHES {
        assert_eq!(
            hash_prefix(&firmware(&format!("fpga/{file}"))),
            prefix,
            "{file}"
        );
    }
}

#[test]
fn every_image_carries_the_same_second_block() {
    for ((image, _, _), offset) in BITSTREAMS.iter().zip(SECOND_BLOCKS) {
        let image_bytes = firmware(image);
        let block = &image_bytes[offset..offset + LENGTH];
        assert_eq!(block[..HEADER.len()], HEADER, "{image} at {offset:#x}");
        assert_eq!(
            hash_prefix(block),
            SECOND_BLOCK_HASH,
            "{image} at {offset:#x}"
        );
    }
}

#[test]
fn the_bitstreams_differ_as_quoted() {
    for (first, second, bytes) in DIFFERENCES {
        let first_bytes = firmware(&format!("fpga/{first}"));
        let second_bytes = firmware(&format!("fpga/{second}"));
        let differing = first_bytes
            .iter()
            .zip(&second_bytes)
            .filter(|(a, b)| a != b)
            .count();
        assert_eq!(differing, bytes, "{first} against {second}");
    }
}
