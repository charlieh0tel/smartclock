//! The FPGA bitstreams in `third_party/firmware/images/fpga/` are byte-for-byte
//! copies of the ones the firmware images load, at the offsets
//! `docs/firmware/outputs.md` gives.

use std::fs;
use std::path::PathBuf;

/// Bytes in each XC3042 bitstream, header to stop bits.
const LENGTH: usize = 3860;

/// The XC3000 preamble and the start of the length count.
const HEADER: [u8; 4] = [0xff, 0x20, 0x07, 0x86];

/// Each image, where its bitstream starts, and the file that copies it.
const BITSTREAMS: [(&str, usize, &str); 7] = [
    ("58503a-3633.bin", 0x1ff84, "58503a-z3801a.bit"),
    ("58503a-3704.bin", 0x1fd16, "58503a-z3801a.bit"),
    ("z3801a-3543.bin", 0x1fddc, "58503a-z3801a.bit"),
    ("z3805a-3543b.bin", 0x20f18, "z3805a.bit"),
    ("58503b-1.01.04.bin", 0x2f5a8, "58503b.bit"),
    ("z3815a-4010.bin", 0x316a6, "z3815a.bit"),
    ("z3816a-4001.bin", 0x2ffb8, "z3816a.bit"),
];

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
