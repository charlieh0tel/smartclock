use std::time::Duration;

use serde_json::Value;
use smartclock::http::Method;
use smartclock::http::Request;
use smartclock::http::Response;
use smartclock::http::Status;

use super::CLICKS_QUEUED;
use super::Panel;
use super::address;
use super::console::Console;
use super::console::Port;
use super::console::QUICK_LINES;
use super::record::Applied;
use super::record::Typed;
use super::record::take_typed;
use super::routes::Shared;
use super::state::Controls;
use super::state::Drawing;
use super::state::Snapshot;
use crate::board::front_panel::Display;
use crate::board::front_panel::Key;
use crate::board::time::Cycles;
use crate::control::Control;
use crate::indicators::Change;
use crate::names::Named;
use crate::script::unescape;

/// The page's font and its drawings, as served.
const FONT: &str = include_str!("page/font.json");
const DRAWINGS: &str = include_str!("page/drawings.js");

/// The characters the 3704 firmware puts on the display: its fixed
/// lines and the formats' output (58503a-3704.md, "The front panel":
/// `HP 58503A`, the warm-up's messages, `%d TRACKED`, `%+3.2fm`,
/// `%c%4d:%02d:%06.3f` with E, W, N or S, `%d,%c,%d,%d` with N, E, O or
/// `?`, the masked `LONG: **:**:**.*`, the status lines, `EXITING...`
/// and the display test's `@;`), every letter and digit among them.
const FIRMWARE_GLYPHS: &str = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-*?m@";
/// The marks it puts on the character before.
const FIRMWARE_MARKS: &str = ".,:;";

/// The 53131A's segment words, the same HP display's, for the
/// characters 3704 shows, as `hp_display` decoded them
/// (<https://github.com/ragges/hp_display>, `extras/codes-mapped.list`):
/// each character and the 16 bits of its word that light segments.
const HP53131A_WORDS: &[(char, u16)] = &[
    (' ', 0x0000),
    ('0', 0xc48c),
    ('1', 0x2040),
    ('2', 0xc08b),
    ('3', 0x4489),
    ('4', 0x040f),
    ('5', 0x4487),
    ('6', 0xc487),
    ('7', 0x0488),
    ('8', 0xc48f),
    ('9', 0x448f),
    ('+', 0x2043),
    ('-', 0x0003),
    ('*', 0x3873),
    ('?', 0x208d),
    ('m', 0xa403),
    ('A', 0x848f),
    ('B', 0x64c9),
    ('C', 0xc084),
    ('D', 0x64c8),
    ('E', 0xc087),
    ('F', 0x8087),
    ('G', 0xc485),
    ('H', 0x840f),
    ('I', 0x60c0),
    ('K', 0x8816),
    ('L', 0xc004),
    ('M', 0x843c),
    ('N', 0x8c2c),
    ('P', 0x808f),
    ('Q', 0xcc8c),
    ('R', 0x888f),
    ('S', 0x44a1),
    ('T', 0x20c0),
    ('U', 0xc40c),
    ('V', 0x9014),
    ('W', 0x9c0c),
    ('X', 0x1830),
    ('Y', 0x2030),
    ('Z', 0x5090),
];
/// A segment word's bits, each with the page's segment it lights
/// (`hp_display`'s `extras/charmap.py`, `code2segs`, its letters renamed
/// to the page's).
const WORD_SEGMENTS: [(u16, char); 14] = [
    (0x0001, 'G'),
    (0x0002, 'g'),
    (0x0004, 'f'),
    (0x0008, 'b'),
    (0x0010, 'j'),
    (0x0020, 'h'),
    (0x0040, 'i'),
    (0x0080, 'a'),
    (0x0400, 'c'),
    (0x0800, 'm'),
    (0x1000, 'k'),
    (0x2000, 'l'),
    (0x4000, 'd'),
    (0x8000, 'e'),
];
/// Where the photograph of the bench 58503A showing `UTC 01:34:43`
/// differs from the 53131A: each character and its segments.
const PHOTOGRAPHED: &[(char, &str)] = &[('1', "bc"), ('3', "abcdgG")];
/// The characters 3704 shows that no source settles (docs/emulator.md,
/// "The panel page").
const INFERRED: &str = "JO@";

/// The 53131A's separator words for the marks (`hp_display`'s
/// `hp_msg_parse.cpp`): each mark and its three bits.
const HP53131A_SEPARATORS: [(char, u8); 4] =
    [('.', 0b010), (',', 0b110), (':', 0b011), (';', 0b111)];
/// A separator word's bits, each with the page's mark it lights: the
/// colon's upper dot, the point, the comma's tail.
const SEPARATOR_MARKS: [(u8, &str); 3] = [(0b001, "colon"), (0b010, "point"), (0b100, "comma")];

/// The console's quick lines the owner chose, in the page's order.
const CHOSEN_QUICK_LINES: [&str; 5] = [
    "*IDN?",
    ":SYSTem:STATus?",
    ":SYSTem:ERRor?",
    ":SYSTem:LANGuage \"PFORTH\"",
    "halt",
];
/// What no quick line may send, in SCPI's short form: the commands that
/// discard the receiver's learned state, strand its serial link or
/// erase its flash (AGENTS.md, "Hardware").
const NEVER_SENT: [&str; 5] = [
    ":SYST:PRES",
    ":SYST:PON",
    ":SYST:COMM",
    ":DIAG:ERAS",
    ":SYST:LANG \"INSTALL\"",
];

/// A snapshot of a 58503A with Option 001.
fn snapshot() -> Snapshot {
    Snapshot {
        drawing: Drawing::Hp58503AOption001,
        cycle: Cycles(88_152_596),
        seconds: 5.25,
        indicators: vec![
            Change {
                at: Cycles(88_152_596),
                bank: "lamp",
                name: "gps_lock",
                lit: true,
            },
            Change {
                at: Cycles(88_152_596),
                bank: "led",
                name: "heartbeat",
                lit: false,
            },
        ],
        display: Some(Display {
            lamps: 0x20,
            text: b"HP 58503A".to_vec(),
        }),
        controls: Controls {
            antenna: Some(true),
            warming: Some(false),
        },
    }
}

/// The answer to `method` `target` with `body`.
fn ask(shared: &Shared, method: Method, target: &str, body: &[u8]) -> Response {
    shared.respond(
        &Request {
            method,
            target: target.to_owned(),
            body: body.to_vec(),
        },
        Duration::ZERO,
    )
}

/// The answer to a GET of `target`.
fn get(shared: &Shared, target: &str) -> Response {
    ask(shared, Method::Get, target, b"")
}

/// The answer to a POST to `target`, with no body.
fn post(shared: &Shared, target: &str) -> Response {
    ask(shared, Method::Post, target, b"")
}

/// The font's table `table`, `glyphs` or `marks`.
fn font(table: &str) -> serde_json::Map<String, Value> {
    let font: Value = serde_json::from_str(FONT).expect("the font is JSON");
    font[table]
        .as_object()
        .unwrap_or_else(|| panic!("the font's {table}"))
        .clone()
}

/// The font's glyph for `character`, its segments sorted.
fn page_glyph(character: char) -> Option<String> {
    font("glyphs")
        .get(&character.to_string())
        .and_then(Value::as_str)
        .map(sorted)
}

fn sorted(segments: &str) -> String {
    let mut characters: Vec<char> = segments.chars().collect();
    characters.sort_unstable();
    characters.into_iter().collect()
}

#[test]
fn a_snapshot_is_one_json_object() {
    assert_eq!(
        snapshot().json(),
        concat!(
            "{\"drawing\":\"58503a-option-001\",\"cycle\":88152596,\"seconds\":5.25,",
            "\"indicators\":[{\"bank\":\"lamp\",\"name\":\"gps_lock\",\"lit\":true},",
            "{\"bank\":\"led\",\"name\":\"heartbeat\",\"lit\":false}],",
            "\"display\":{\"lamps\":32,\"text\":\"HP 58503A\"},",
            "\"keys\":[\"time\",\"long\",\"lat\",\"alt\",\"sat\",\"status\",\"serial\",\"shift\"],",
            "\"controls\":{\"names\":[\"antenna-off\",\"antenna-on\",\"skip-warm-up\",",
            "\"power-cycle\",\"reset\"],\"antenna\":true,\"warming\":false}}"
        )
    );
}

#[test]
fn a_board_without_a_panel_has_no_display_and_no_keys() {
    let json = Snapshot {
        drawing: Drawing::Z3805A,
        indicators: Vec::new(),
        display: None,
        controls: Controls {
            antenna: None,
            warming: Some(true),
        },
        ..snapshot()
    }
    .json();
    assert!(
        json.ends_with(concat!(
            "\"indicators\":[],\"display\":null,\"keys\":[],",
            "\"controls\":{\"names\":[\"skip-warm-up\",\"power-cycle\",\"reset\"],",
            "\"antenna\":null,\"warming\":true}}"
        )),
        "{json}"
    );
}

#[test]
fn a_clicked_key_is_traced_at_a_cycle_a_script_replays() {
    let pressed = Applied {
        cycle: Cycles(53_608_448),
        at: Cycles(53_608_448),
        what: Key::Sat,
    };
    let record = pressed.to_string();
    assert_eq!(record, "key cycle=53608448 at=3.1953125 name=sat");
    // the `at` field, read back as a script's `at` reads its seconds,
    // and the key named, are the same press
    let fields: Vec<(&str, &str)> = record
        .split(' ')
        .skip(1)
        .filter_map(|field| field.split_once('='))
        .collect();
    let at = fields[1].1.parse::<f64>().expect("seconds");
    assert_eq!(Cycles::from_seconds(at), Some(pressed.at));
    assert_eq!(Key::from_name(fields[2].1), Some(pressed.what));
    // every cycle of a long run reads back exactly
    for cycle in [1, 16_777_215, 16_777_217, 1 << 40, (1 << 53) - 1] {
        let text = Applied {
            cycle: Cycles(cycle),
            at: Cycles(cycle),
            what: Key::Time,
        }
        .to_string();
        let at = text
            .split_once("at=")
            .and_then(|(_, rest)| rest.split_once(' '))
            .map(|(at, _)| at.parse::<f64>().expect("seconds"))
            .expect("an at");
        assert_eq!(Cycles::from_seconds(at), Some(Cycles(cycle)), "{text}");
    }
}

#[test]
fn a_click_reaches_the_run_loop_as_the_key_a_script_names() {
    let (panel, shared) = Panel::new(snapshot(), Port::Script, jiff::Timestamp::UNIX_EPOCH);
    for name in ["sat", "serial", "shift"] {
        let response = post(&shared, &format!("/key?name={name}"));
        assert_eq!(response.status, Status::Ok, "{name}");
    }
    assert_eq!(
        panel.take_keys(Cycles(7), Cycles(7)),
        ["sat", "serial", "shift"].map(|name| Key::from_name(name).expect("a key"))
    );
    assert!(
        panel.take_keys(Cycles(8), Cycles(8)).is_empty(),
        "each once"
    );
}

#[test]
fn a_click_on_no_key_or_past_the_queue_is_refused() {
    let (panel, shared) = Panel::new(snapshot(), Port::Script, jiff::Timestamp::UNIX_EPOCH);
    assert_eq!(post(&shared, "/key?name=enter").status, Status::BadRequest);
    assert_eq!(post(&shared, "/key").status, Status::BadRequest);
    for _ in 0..CLICKS_QUEUED {
        assert_eq!(post(&shared, "/key?name=time").status, Status::Ok);
    }
    assert_eq!(post(&shared, "/key?name=time").status, Status::Unavailable);
    assert_eq!(panel.take_keys(Cycles(0), Cycles(0)).len(), CLICKS_QUEUED);
    drop(panel);
    assert_eq!(post(&shared, "/key?name=time").status, Status::Unavailable);
}

#[test]
fn a_path_answers_only_its_methods() {
    let (_panel, shared) = Panel::new(snapshot(), Port::Page, jiff::Timestamp::UNIX_EPOCH);
    for (method, target) in [
        (Method::Get, "/key?name=time"),
        (Method::Get, "/event?name=reset"),
        (Method::Other, "/key?name=time"),
        (Method::Post, "/"),
        (Method::Post, "/state"),
        (Method::Post, "/constants.json"),
        (Method::Other, "/console"),
    ] {
        let response = ask(&shared, method, target, b"");
        assert_eq!(
            response.status,
            Status::MethodNotAllowed,
            "{method:?} {target}"
        );
    }
    assert_eq!(get(&shared, "/console?since=0").status, Status::Ok);
    assert_eq!(post(&shared, "/key?name=time").status, Status::Ok);
    assert_eq!(get(&shared, "/nothing").status, Status::NotFound);
}

#[test]
fn a_clicked_control_reaches_the_run_loop_and_a_bad_one_is_refused() {
    let (panel, shared) = Panel::new(snapshot(), Port::Script, jiff::Timestamp::UNIX_EPOCH);
    for name in ["antenna-off", "skip-warm-up", "power-cycle"] {
        let response = post(&shared, &format!("/event?name={name}"));
        assert_eq!(response.status, Status::Ok, "{name}");
    }
    assert_eq!(
        post(&shared, "/event?name=antenna").status,
        Status::BadRequest
    );
    assert_eq!(
        panel.take_controls(Cycles(7), Cycles(9)),
        [
            Control::AntennaOff,
            Control::SkipWarmUp,
            Control::PowerCycle
        ]
    );
    assert!(panel.take_keys(Cycles(7), Cycles(9)).is_empty(), "not keys");
    let record = Applied {
        cycle: Cycles(7),
        at: Cycles(16_777_216 + 8_388_608),
        what: Control::PowerCycle,
    };
    assert_eq!(record.to_string(), "event cycle=7 at=1.5 name=power-cycle");
    for _ in 0..CLICKS_QUEUED {
        assert_eq!(post(&shared, "/event?name=reset").status, Status::Ok);
    }
    assert_eq!(
        post(&shared, "/event?name=reset").status,
        Status::Unavailable
    );
    drop(panel);
    assert_eq!(
        post(&shared, "/event?name=reset").status,
        Status::Unavailable
    );
}

#[test]
fn the_page_and_the_state_are_served_and_the_state_is_the_last_published() {
    let (panel, shared) = Panel::new(snapshot(), Port::Script, jiff::Timestamp::UNIX_EPOCH);
    let page = get(&shared, "/");
    assert!(page.kind.starts_with("text/html"));
    for file in ["panel.css", "drawings.js", "panel.js"] {
        assert!(page.body.contains(&format!("\"{file}\"")), "{file}");
        assert_eq!(get(&shared, &format!("/{file}")).status, Status::Ok);
    }
    assert!(get(&shared, "/drawings.js").body.contains("SVG"));
    assert_eq!(get(&shared, "/font.json").body, FONT);
    let later = Snapshot {
        cycle: Cycles(99),
        ..snapshot()
    };
    panel.publish(later.clone());
    let state = get(&shared, "/state?t=1");
    assert_eq!(state.kind, "application/json");
    assert_eq!(state.body, later.json());
    assert_eq!(get(&shared, "/nothing").status, Status::NotFound);
}

#[test]
fn the_font_has_every_character_the_firmware_shows() {
    let glyphs = font("glyphs");
    let marks = font("marks");
    for character in FIRMWARE_GLYPHS.chars() {
        assert!(
            glyphs.contains_key(&character.to_string()),
            "no glyph for {character:?}"
        );
    }
    for character in FIRMWARE_MARKS.chars() {
        assert!(
            marks.contains_key(&character.to_string()),
            "no mark for {character:?}"
        );
    }
}

#[test]
fn the_font_is_the_cited_table() {
    for character in FIRMWARE_GLYPHS.chars() {
        let glyph = page_glyph(character).expect("a glyph");
        let photographed = PHOTOGRAPHED.iter().find(|(shown, _)| *shown == character);
        let word = HP53131A_WORDS.iter().find(|(shown, _)| *shown == character);
        let expected = match (photographed, word) {
            (Some((_, segments)), _) => sorted(segments),
            (None, Some((_, word))) => sorted(
                &WORD_SEGMENTS
                    .iter()
                    .filter(|(bit, _)| word & bit != 0)
                    .map(|(_, segment)| *segment)
                    .collect::<String>(),
            ),
            (None, None) => {
                assert!(INFERRED.contains(character), "no source for {character:?}");
                continue;
            }
        };
        assert_eq!(glyph, expected, "{character:?}");
    }
}

#[test]
fn the_marks_are_the_cited_separators() {
    let marks = font("marks");
    for (character, word) in HP53131A_SEPARATORS {
        let value = marks.get(&character.to_string()).expect("a mark");
        for (bit, mark) in SEPARATOR_MARKS {
            assert_eq!(
                value[mark] == Value::Bool(true),
                word & bit != 0,
                "{character:?} {mark}"
            );
        }
    }
}

#[test]
fn the_page_draws_every_drawing() {
    for name in Drawing::all_names() {
        assert!(
            DRAWINGS.contains(&format!("\"{name}\": {{")),
            "the page has no drawing {name}"
        );
    }
}

/// Whether `line` is one of `NEVER_SENT`'s, in any case, in short form
/// or long: each of its header's keywords, from the first, begins with
/// the forbidden one's keyword, its short form, and its arguments begin
/// with the forbidden one's, so that `:system:communicate:serial:baud`
/// is `:SYST:COMM`'s.
fn harmful(line: &str) -> bool {
    let line = line.to_ascii_uppercase();
    let (header, arguments) = line.split_once(' ').unwrap_or((&line, ""));
    let keywords: Vec<&str> = header.trim_start_matches(':').split(':').collect();
    NEVER_SENT.iter().any(|never| {
        let (never_header, never_arguments) = never.split_once(' ').unwrap_or((never, ""));
        let never_keywords: Vec<&str> = never_header.trim_start_matches(':').split(':').collect();
        keywords.len() >= never_keywords.len()
            && keywords
                .iter()
                .zip(&never_keywords)
                .all(|(keyword, never)| keyword.starts_with(never))
            && arguments.trim().starts_with(never_arguments)
    })
}

#[test]
fn the_harm_check_knows_a_command_in_any_case_and_form() {
    for line in [
        ":SYSTem:PRESet",
        ":syst:pres",
        ":SYSTEM:PRESET",
        "SYST:PON",
        ":system:communicate:serial:baud 19200",
        ":DIAGnostic:ERASe",
        ":syst:lang \"install\"",
        ":SYSTem:LANGuage \"INSTALL\"",
    ] {
        assert!(harmful(line), "{line}");
    }
    for line in [":SYSTem:LANGuage \"PFORTH\"", "*IDN?", ":SYST:ERR?", "halt"] {
        assert!(!harmful(line), "{line}");
    }
}

#[test]
fn the_quick_lines_are_the_chosen_five_and_none_harmful() {
    let lines: Vec<&str> = QUICK_LINES.iter().map(|quick| quick.line).collect();
    assert_eq!(lines, CHOSEN_QUICK_LINES);
    for line in lines {
        assert!(!harmful(line), "{line}");
    }
}

/// Each quick line, and lines with what a record escapes, posted as its
/// button posts it: put on the port whole with the terminator, and its
/// record read back as a script's `at` and `send` steps read theirs is
/// the same line at the same time.
#[test]
fn a_quick_line_is_queued_and_traced_as_a_typed_one_that_replays() {
    let escaped = ["a b\\c", "\u{7f}\u{e9}"];
    for line in CHOSEN_QUICK_LINES.into_iter().chain(escaped) {
        let (console, typed) = Console::new(Port::Page, jiff::Timestamp::UNIX_EPOCH);
        let now = Duration::ZERO;
        console.read(0, Some("a"), now);
        assert_eq!(
            console.write(line.as_bytes(), Some("a"), now).status,
            Status::Ok
        );
        let at = Cycles(16_777_216 + 8_388_608);
        let mut put: Vec<u8> = Vec::new();
        let taken = take_typed(&typed, Cycles(5), at, |bytes| {
            put.extend(bytes);
            true
        });
        assert_eq!(taken, 1);
        assert_eq!(put, [line.as_bytes(), b"\r\n"].concat());
        let record = Typed {
            cycle: Cycles(5),
            at,
            line: line.as_bytes().to_vec(),
        }
        .to_string();
        let field = |name: &str| {
            record
                .split(' ')
                .find_map(|field| field.strip_prefix(&format!("{name}=")))
                .unwrap_or_else(|| panic!("{name} in {record}"))
        };
        let seconds = field("at").parse::<f64>().expect("seconds");
        assert_eq!(Cycles::from_seconds(seconds), Some(at), "{record}");
        assert_eq!(
            unescape(field("line")).as_deref(),
            Ok(line.as_bytes()),
            "{record}"
        );
    }
}

#[test]
fn a_bare_port_is_on_the_loopback_interface_alone() {
    assert_eq!(address("8080"), "127.0.0.1:8080");
    assert_eq!(address("0.0.0.0:8080"), "0.0.0.0:8080");
    assert_eq!(address("[::1]:8080"), "[::1]:8080");
}
