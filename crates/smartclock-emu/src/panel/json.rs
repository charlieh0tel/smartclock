//! The page's answers as JSON, with `serde_json`, ASCII throughout: a
//! character of a string that is not printable ASCII goes as `\u00HH`,
//! so that a byte the firmware sends reaches the page as the same code
//! and the answer reads the same in a terminal (docs/emulator.md, "The
//! panel page").

use std::io;

use serde::Serialize;
use serde::Serializer;
use serde_json::ser::CharEscape;
use serde_json::ser::Formatter;

/// Bytes as a JSON string: each byte as the character of its code, so
/// that the page reads the firmware's bytes back with `charCodeAt`.
#[derive(Debug, Clone, Copy)]
pub(super) struct Bytes<'a>(pub(super) &'a [u8]);

impl Serialize for Bytes<'_> {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        let text: String = self.0.iter().map(|&byte| char::from(byte)).collect();
        serializer.serialize_str(&text)
    }
}

/// `serde_json`'s compact format, but with every character of a string
/// outside printable ASCII escaped as `\uXXXX`, a control character's
/// too, rather than as itself or a short escape.
#[derive(Debug, Clone, Copy)]
struct Ascii;

/// `code` as a JSON `\uXXXX` escape.
fn escape<W: ?Sized + io::Write>(writer: &mut W, code: u16) -> io::Result<()> {
    write!(writer, "\\u{code:04x}")
}

impl Formatter for Ascii {
    fn write_string_fragment<W: ?Sized + io::Write>(
        &mut self,
        writer: &mut W,
        fragment: &str,
    ) -> io::Result<()> {
        for character in fragment.chars() {
            if character.is_ascii_graphic() || character == ' ' {
                writer.write_all(&[character as u8])?;
            } else {
                let mut units = [0; 2];
                for &unit in character.encode_utf16(&mut units).iter() {
                    escape(writer, unit)?;
                }
            }
        }
        Ok(())
    }

    fn write_char_escape<W: ?Sized + io::Write>(
        &mut self,
        writer: &mut W,
        char_escape: CharEscape,
    ) -> io::Result<()> {
        let code = match char_escape {
            CharEscape::Quote => return writer.write_all(b"\\\""),
            CharEscape::ReverseSolidus => return writer.write_all(b"\\\\"),
            CharEscape::Solidus => b'/',
            CharEscape::Backspace => 0x08,
            CharEscape::FormFeed => 0x0c,
            CharEscape::LineFeed => b'\n',
            CharEscape::CarriageReturn => b'\r',
            CharEscape::Tab => b'\t',
            CharEscape::AsciiControl(code) => code,
        };
        escape(writer, u16::from(code))
    }
}

/// `value` as compact JSON, ASCII throughout.
pub(super) fn to_string<T: Serialize>(value: &T) -> String {
    let mut out = Vec::new();
    let mut serializer = serde_json::Serializer::with_formatter(&mut out, Ascii);
    // The answers are structs and string-keyed maps of plain values,
    // which serialize into memory without fail.
    value
        .serialize(&mut serializer)
        .expect("an answer serializes");
    // ASCII, by the formatter
    String::from_utf8_lossy(&out).into_owned()
}

#[cfg(test)]
mod tests {
    use super::Bytes;
    use super::to_string;

    #[test]
    fn the_texts_bytes_reach_the_page_as_their_codes() {
        assert_eq!(to_string(&Bytes(b"A \"B\" \\")), "\"A \\\"B\\\" \\\\\"");
        assert_eq!(
            to_string(&Bytes(b"\x00\r\n\x7f\xb0")),
            "\"\\u0000\\u000d\\u000a\\u007f\\u00b0\""
        );
        assert_eq!(to_string(&"\u{1d11e}"), "\"\\ud834\\udd1e\"");
    }
}
