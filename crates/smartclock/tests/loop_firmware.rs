//! `docs/firmware/loop.md` and `docs/loop.html` checked against the
//! firmware itself, for the Z3805A 3543B and the 58503A 3704: each
//! constant the evidence cites read from the image at the address it
//! gives, or found as an immediate operand in the routine it names, by
//! receiver and by meaning; and the update law the evidence states
//! replayed against what the images' own `startup_pll` and `pll_normal`
//! did, second by second, in Unicorn (`tools/firmware/loop_trace.py`,
//! whose output is `fixtures/loop/IMAGE.tsv`; `make -C
//! third_party/firmware check-loop-trace` fails when it is stale).
//! The page's by-model rows, poles and step response are then
//! recomputed from that law, which the firmware has been shown to run.

use std::collections::HashMap;
use std::fs;
use std::ops::Range;

/// The repository's root, from this crate's directory.
const ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");

/// A receiver whose loop is checked, and what its firmware does that
/// the other's does not.
struct Receiver {
    /// Its image's name: `third_party/firmware/IMAGE.bin` and `IMAGE/`.
    image: &'static str,
    /// Its row's first cell in loop.md's "In every image" table.
    evidence_row: &'static str,
    /// The words its row's first cell in loop.html's by-model table
    /// holds.
    page_row: &'static [&'static str],
    /// Whether a block in which no reading was summed is still steered
    /// on, its mean taken as 0, rather than skipped.
    empty_block_steers: bool,
    /// Whether a reading is summed only while the GPS reference is good
    /// and not questionable.
    reference_gates_readings: bool,
}

const RECEIVERS: [Receiver; 2] = [
    Receiver {
        image: "z3805a-3543b",
        evidence_row: "Z3805A 3543B",
        page_row: &["Z3805A 3543B"],
        empty_block_steers: true,
        reference_gates_readings: false,
    },
    Receiver {
        image: "58503a-3704",
        evidence_row: "58503A 3704",
        page_row: &["58503A", "3704"],
        empty_block_steers: false,
        reference_gates_readings: true,
    },
];

/// The offset of τ in the ROM's 25-byte block of defaults (loop.md,
/// "τ and G": "τ is at offset 0x14 of a 25-byte block").
const TAU_OFFSET: usize = 0x14;

/// smartclock.h's `pll_startup_state` values the replay steps through.
const STARTUP_SETTLING: u8 = 1;
const STARTUP_WAITING_FOR_HISTORY: u8 = 2;
const STARTUP_LENGTHENING: u8 = 3;

/// smartclock.h's `pll_stage_result` for a stage that is done.
const RESULT_DONE: u8 = 1;

/// smartclock.h's `pll_hold_cause` bits a block with no reading summed
/// adds: a reading failed, or every one was past the limit.
const HOLD_CAUSE_TI_ERROR: u16 = 0x04;
const HOLD_CAUSE_TI_LIMIT_EXCEEDED: u16 = 0x08;

fn read(path: &str) -> String {
    let path = format!("{ROOT}/{path}");
    fs::read_to_string(&path).unwrap_or_else(|error| panic!("{path}: {error}"))
}

/// `text` with every run of whitespace one space, so that a phrase can
/// be found across the lines it was wrapped over.
fn flat(text: &str) -> String {
    text.split_whitespace().collect::<Vec<_>>().join(" ")
}

/// The decimal number at the start of `text`.
fn leading_number(text: &str) -> f64 {
    let digits: String = text
        .chars()
        .take_while(|letter| letter.is_ascii_digit() || *letter == '.')
        .collect();
    digits
        .parse()
        .unwrap_or_else(|_| panic!("no number at the start of {text:?}"))
}

/// The number right after the first `prefix` in `text`.
fn number_after(text: &str, prefix: &str) -> f64 {
    let at = text
        .find(prefix)
        .unwrap_or_else(|| panic!("the text no longer says {prefix:?}"));
    leading_number(&text[at + prefix.len()..])
}

/// A value written `±N × 10ⁿ` at the start of `text`.
fn power(text: &str) -> f64 {
    let (sign, text) = match text.chars().next() {
        Some('+') => (1.0, &text[1..]),
        Some('−') => (-1.0, &text['−'.len_utf8()..]),
        _ => (1.0, text),
    };
    let mantissa = leading_number(text);
    let rest = text
        .split_once(" × 10")
        .unwrap_or_else(|| panic!("{text:?} is not N × 10ⁿ"))
        .1;
    let mut exponent = 0;
    let mut negative = false;
    for letter in rest.chars() {
        match letter {
            '⁻' => negative = true,
            _ => match "⁰¹²³⁴⁵⁶⁷⁸⁹".chars().position(|digit| digit == letter) {
                Some(digit) => exponent = exponent * 10 + i32::try_from(digit).expect("a digit"),
                None => break,
            },
        }
    }
    sign * mantissa * 10f64.powi(if negative { -exponent } else { exponent })
}

/// The addresses written `` `0x...` `` in `text`, in order.
fn addresses(text: &str) -> Vec<usize> {
    text.split("`0x")
        .skip(1)
        .map(|rest| {
            let digits = rest.split('`').next().unwrap_or_default();
            usize::from_str_radix(digits, 16)
                .unwrap_or_else(|_| panic!("{digits:?} is not an address"))
        })
        .collect()
}

/// The rows of the Markdown table whose header row starts with
/// `header`, each cell by its column's heading.
fn markdown_table(text: &str, header: &str) -> Vec<HashMap<String, String>> {
    let cells = |line: &str| -> Vec<String> {
        line.replace("\\|", "\u{1}")
            .trim()
            .trim_matches('|')
            .split('|')
            .map(|cell| cell.trim().replace('\u{1}', "|"))
            .collect()
    };
    let mut lines = text
        .lines()
        .skip_while(|line| !line.starts_with(header))
        .take_while(|line| line.starts_with('|'));
    let headings = cells(
        lines
            .next()
            .unwrap_or_else(|| panic!("no table {header:?}")),
    );
    lines
        .skip(1)
        .map(|line| headings.iter().cloned().zip(cells(line)).collect())
        .collect()
}

/// The row of loop.md's "In every image" table for `receiver`.
fn evidence_row(evidence: &str, receiver: &Receiver) -> HashMap<String, String> {
    markdown_table(evidence, "| Image | τ, as the ROM block sets it")
        .into_iter()
        .find(|row| row["Image"] == receiver.evidence_row)
        .unwrap_or_else(|| panic!("loop.md has no row for {}", receiver.evidence_row))
}

/// The text of each `<td>` of loop.html's by-model table, by row.
fn page_rows(page: &str) -> Vec<Vec<String>> {
    let table = &page[page.find("<h3>By model</h3>").expect("the by-model table")..];
    let table = &table[..table.find("</table>").expect("its end")];
    table
        .split("<tr>")
        .skip(1)
        .map(|row| {
            row.split("<td")
                .skip(1)
                .map(|cell| {
                    let cell = cell.split_once('>').map_or(cell, |(_, rest)| rest);
                    cell.split("</td>").next().unwrap_or_default().to_owned()
                })
                .collect::<Vec<_>>()
        })
        .filter(|row: &Vec<String>| !row.is_empty())
        .collect()
}

/// An image and its functions' addresses.
struct Firmware {
    image: &'static str,
    rom: Vec<u8>,
    /// Each function's start and name, by address (names.tsv's primary
    /// rows).
    functions: Vec<(usize, String)>,
}

impl Firmware {
    fn load(image: &'static str) -> Self {
        let path = format!("{ROOT}/third_party/firmware/{image}.bin");
        let rom = fs::read(&path).unwrap_or_else(|error| panic!("{path}: {error}"));
        let mut functions: Vec<(usize, String)> =
            read(&format!("third_party/firmware/{image}/names.tsv"))
                .lines()
                .filter(|line| !line.starts_with('#'))
                .filter_map(|line| {
                    let cells: Vec<&str> = line.split('\t').collect();
                    let address = cells.first()?.strip_prefix("0x")?;
                    if cells.get(1) != Some(&"function") {
                        return None;
                    }
                    let address = usize::from_str_radix(address, 16).ok()?;
                    Some((address, (*cells.get(2)?).to_owned()))
                })
                .collect();
        functions.sort();
        Self {
            image,
            rom,
            functions,
        }
    }

    /// The bytes of the function named `name`, to the next function.
    fn function(&self, name: &str) -> Range<usize> {
        let index = self
            .functions
            .iter()
            .position(|(_, function)| function == name)
            .unwrap_or_else(|| panic!("{} has no function {name}", self.image));
        let start = self.functions[index].0;
        let end = self.functions[index + 1..]
            .iter()
            .map(|(address, _)| *address)
            .find(|address| *address > start)
            .expect("a function after it");
        start..end
    }

    /// The name of the function `address` is in.
    fn containing(&self, address: usize) -> &str {
        &self
            .functions
            .iter()
            .rev()
            .find(|(start, _)| *start <= address)
            .expect("a function before it")
            .1
    }

    fn word(&self, at: usize) -> u16 {
        u16::from_be_bytes([self.rom[at], self.rom[at + 1]])
    }

    fn long(&self, at: usize) -> u32 {
        u32::from_be_bytes(self.rom[at..at + 4].try_into().expect("four bytes"))
    }

    fn float(&self, at: usize) -> f32 {
        f32::from_bits(self.long(at))
    }

    /// Whether the long at `at` is the operand of a `move.l #imm`, its
    /// opcode the word before it: size bits 10, source mode 7 register
    /// 4 (M68000PRM, MOVE).
    fn is_immediate(&self, at: usize) -> bool {
        const MOVE_LONG: u16 = 0x2000;
        const SIZE: u16 = 0xf000;
        const SOURCE: u16 = 0x003f;
        const IMMEDIATE: u16 = 0x003c;
        let opcode = self.word(at - 2);
        opcode & SIZE == MOVE_LONG && opcode & SOURCE == IMMEDIATE
    }

    /// The operands of the `move.l #imm` instructions in the function
    /// named `name`.
    fn immediates(&self, name: &str) -> Vec<u32> {
        self.function(name)
            .step_by(2)
            .filter(|at| self.is_immediate(*at + 2))
            .map(|at| self.long(at + 2))
            .collect()
    }

    /// Whether the function named `name` has a `moveq #value,Dn`
    /// (M68000PRM, MOVEQ: 0111 rrr0 and the byte).
    fn has_moveq(&self, name: &str, value: u8) -> bool {
        self.function(name)
            .step_by(2)
            .any(|at| self.rom[at] & 0xf1 == 0x70 && self.rom[at + 1] == value)
    }
}

/// Asserts the function named `name` holds `value` as a single float
/// immediate.
fn assert_float_immediate(firmware: &Firmware, name: &str, value: f64, meaning: &str) {
    #[expect(
        clippy::cast_possible_truncation,
        reason = "the firmware's floats are single"
    )]
    let bits = (value as f32).to_bits();
    assert!(
        firmware.immediates(name).contains(&bits),
        "{}'s {name} has no move.l #{value} ({bits:#010x}), {meaning}",
        firmware.image
    );
}

#[test]
fn the_evidence_s_time_constants_and_gains_are_the_roms() {
    let evidence = read("docs/firmware/loop.md");
    for receiver in &RECEIVERS {
        let firmware = Firmware::load(receiver.image);
        let row = evidence_row(&evidence, receiver);

        let tau = &row["τ, as the ROM block sets it"];
        let block = addresses(tau)[0];
        assert_eq!(
            f64::from(firmware.float(block + TAU_OFFSET)),
            leading_number(tau),
            "{}: loop.md's τ, {tau}",
            receiver.image
        );

        let startup = &row["Startup's constant"];
        let setters: Vec<&str> = addresses(startup)
            .into_iter()
            .map(|at| {
                assert!(
                    firmware.is_immediate(at),
                    "{}: {at:#x}, loop.md's startup constant, is no immediate",
                    receiver.image
                );
                assert_eq!(
                    f64::from(firmware.float(at)),
                    leading_number(startup),
                    "{}: loop.md's startup constant at {at:#x}",
                    receiver.image
                );
                firmware.containing(at)
            })
            .collect();
        assert_eq!(
            setters,
            ["pll_init", "holdover_recovery"],
            "{}: loop.md's startup constant is set by pll_init and holdover_recovery",
            receiver.image
        );

        let gain = &row["G"];
        let at = addresses(gain)[0];
        assert!(
            firmware.is_immediate(at),
            "{}: G at {at:#x}",
            receiver.image
        );
        assert_eq!(firmware.containing(at), "pllp_task", "{}", receiver.image);
        let rom = f64::from(firmware.float(at));
        assert!(
            (rom / power(gain) - 1.0).abs() < 1e-6,
            "{}: loop.md's G {gain}, the ROM's {rom:e}",
            receiver.image
        );
    }
}

#[test]
fn the_evidence_s_update_constants_are_in_each_receiver_s_code() {
    let evidence = read("docs/firmware/loop.md");
    let law = flat(
        &evidence[evidence.find("The update:").expect("the update law")
            ..evidence.find("and `FUN_00033798(u)`").expect("its end")],
    );
    let prose = flat(&evidence);
    let symbols = markdown_table(&evidence, "| Symbol | Address");
    let clamp = &symbols
        .iter()
        .find(|row| row["Symbol"] == "M")
        .expect("M's row")["What it is"];
    let clamp = power(clamp.split_once("set to ").expect("M's value").1);
    for receiver in &RECEIVERS {
        let firmware = Firmware::load(receiver.image);
        for stage in ["startup_pll", "pll_normal"] {
            for (value, meaning) in [
                (number_after(&law, "a = "), "a's numerator"),
                (number_after(&law, "k = 1 / ("), "k's factor"),
                (number_after(&law, "I ← I + "), "the integrator's factor"),
                (number_after(&law, "d = clamp(p·"), "the fit's unit"),
            ] {
                assert_float_immediate(&firmware, stage, value, meaning);
            }
        }
        assert_float_immediate(&firmware, "efc_gain_set", clamp, "M's numerator");
        assert_float_immediate(
            &firmware,
            "efc_clamp",
            number_after(&prose, "The loop clamps u to 0 to "),
            "the EFC's top",
        );
        let step = number_after(&prose, "lengthens its constant by ");
        assert_float_immediate(&firmware, "startup_pll", step, "the ramp's step");
        assert_float_immediate(&firmware, "startup_pll", -step, "the ramp's step");

        let settled = power(
            prose
                .split_once("the double ")
                .expect("the settling threshold")
                .1,
        );
        let bits = settled.to_bits();
        let immediates = firmware.immediates("startup_pll");
        #[expect(clippy::cast_possible_truncation, reason = "a double's halves")]
        let halves = [(bits >> 32) as u32, bits as u32];
        assert!(
            halves.iter().all(|half| immediates.contains(half)),
            "{}'s startup_pll has no double {settled:e}",
            receiver.image
        );
        let count = number_after(&prose, "whose mean is within 150 ns, to ");
        #[expect(
            clippy::cast_possible_truncation,
            clippy::cast_sign_loss,
            reason = "a small count"
        )]
        let count = count as u8;
        assert!(
            firmware.has_moveq("startup_pll", count),
            "{}'s startup_pll has no moveq #{count}",
            receiver.image
        );
    }
}

#[test]
fn the_evidence_s_holdover_recovery_thresholds_are_the_roms() {
    let prose = flat(&read("docs/firmware/loop.md"));
    let sentence = prose
        .split_once("when the holdover lasted at most ")
        .expect("the recovery's threshold")
        .1;
    let sentence = &sentence[..sentence.find(')').expect("its end")];
    let (z3805a, a3704) = sentence.split_once("; ").expect("two receivers' values");
    for (receiver, text, marker) in [
        (&RECEIVERS[0], z3805a, "(Z3805A, "),
        (&RECEIVERS[1], a3704, " s in 3704, "),
    ] {
        assert!(text.contains(marker), "{text:?} names no {marker:?}");
        let firmware = Firmware::load(receiver.image);
        let at = addresses(text)[0];
        assert!(firmware.is_immediate(at), "{}: {at:#x}", receiver.image);
        assert_eq!(firmware.containing(at), "holdover_recovery");
        assert_eq!(
            f64::from(firmware.float(at)),
            leading_number(text),
            "{}: loop.md's recovery threshold at {at:#x}",
            receiver.image
        );
    }
}

/// One row of a trace: a stage's call and what the firmware's state
/// was after it.
struct Row {
    scenario: String,
    stage: String,
    same_stage: bool,
    valid: bool,
    reading: f32,
    uptime: u32,
    current: f32,
    reference_good: bool,
    questionable: bool,
    fit_b: f32,
    fit_c: f32,
    collecting: bool,
    history_ready: bool,
    result: u8,
    hold_causes: u16,
    ti_filtered: f32,
    integral: f32,
    efc_base: f32,
    efc: f32,
    loop_time_constant: f32,
    startup_state: u8,
    large_offset_blocks: i32,
    proportional_gain: f32,
    integral_gain: f32,
    filter_weight: f32,
}

/// A receiver's trace: its scenarios' constants and its rows.
struct Trace {
    constants: HashMap<(String, String), f32>,
    rows: Vec<Row>,
}

impl Trace {
    fn load(image: &str) -> Self {
        let text = read(&format!(
            "crates/smartclock/tests/fixtures/loop/{image}.tsv"
        ));
        let mut lines = text.lines();
        let header: Vec<&str> = lines.next().expect("a header").split('\t').collect();
        let mut constants = HashMap::new();
        let mut rows = Vec::new();
        for line in lines {
            if let Some(comment) = line.strip_prefix("# ") {
                let words: Vec<&str> = comment.split(' ').collect();
                if let [scenario, name, bits] = words[..]
                    && let Ok(bits) = u32::from_str_radix(bits, 16)
                {
                    constants.insert((scenario.to_owned(), name.to_owned()), f32::from_bits(bits));
                }
                continue;
            }
            let cells: HashMap<&str, &str> = header.iter().copied().zip(line.split('\t')).collect();
            let float = |name: &str| {
                f32::from_bits(u32::from_str_radix(cells[name], 16).expect("float bits"))
            };
            let flag = |name: &str| cells[name] != "0";
            let integer = |name: &str| cells[name].parse::<i64>().expect("an integer");
            rows.push(Row {
                scenario: cells["scenario"].to_owned(),
                stage: cells["stage"].to_owned(),
                same_stage: flag("same_stage"),
                valid: flag("valid"),
                reading: float("reading"),
                uptime: u32::try_from(integer("uptime")).expect("an uptime"),
                current: float("current"),
                reference_good: flag("reference_good"),
                questionable: flag("questionable"),
                fit_b: float("fit_b"),
                fit_c: float("fit_c"),
                collecting: flag("collecting"),
                history_ready: flag("history_ready"),
                result: u8::try_from(integer("result")).expect("a result"),
                hold_causes: u16::try_from(integer("hold_causes")).expect("causes"),
                ti_filtered: float("ti_filtered"),
                integral: float("integral"),
                efc_base: float("efc_base"),
                efc: float("efc"),
                loop_time_constant: float("loop_time_constant"),
                startup_state: u8::try_from(integer("startup_state")).expect("a state"),
                large_offset_blocks: i32::try_from(integer("large_offset_blocks"))
                    .expect("a count"),
                proportional_gain: float("proportional_gain"),
                integral_gain: float("integral_gain"),
                filter_weight: float("filter_weight"),
            });
        }
        Self { constants, rows }
    }

    fn constant(&self, scenario: &str, name: &str) -> f32 {
        self.constants[&(scenario.to_owned(), name.to_owned())]
    }

    fn scenarios(&self) -> Vec<String> {
        let mut names: Vec<String> = Vec::new();
        for row in &self.rows {
            if !names.contains(&row.scenario) {
                names.push(row.scenario.clone());
            }
        }
        names
    }
}

/// The update law's constants as loop.md states them, for a receiver.
#[derive(Clone, Copy)]
struct Law {
    /// a's numerator: a = `filter` / τ.
    filter: f32,
    /// k's factor: k = 1 / (`integral_scale`·G·τ²).
    integral_scale: f32,
    /// The integrator's factor, the seconds between updates.
    block: f32,
    /// The aging fit's unit, s.
    entry: f32,
    /// M's numerator: M = `step_numerator` / |G|.
    step_numerator: f32,
    /// The EFC's top.
    efc_top: f32,
    /// The receiver's G and τ, from the "In every image" table.
    gain: f32,
    tau: f32,
    /// `startup_pll`'s settling: blocks, their bound and the ramp's step.
    settled_blocks: i32,
    settled: f64,
    ramp_step: f32,
}

impl Law {
    #[expect(
        clippy::cast_possible_truncation,
        reason = "the firmware's floats are single"
    )]
    fn from_evidence(evidence: &str, receiver: &Receiver) -> Self {
        let law = flat(
            &evidence[evidence.find("The update:").expect("the update law")
                ..evidence.find("and `FUN_00033798(u)`").expect("its end")],
        );
        let prose = flat(evidence);
        let row = evidence_row(evidence, receiver);
        let symbols = markdown_table(evidence, "| Symbol | Address");
        let clamp = &symbols
            .iter()
            .find(|row| row["Symbol"] == "M")
            .expect("M's row")["What it is"];
        Self {
            filter: number_after(&law, "a = ") as f32,
            integral_scale: number_after(&law, "k = 1 / (") as f32,
            block: number_after(&law, "I ← I + ") as f32,
            entry: number_after(&law, "d = clamp(p·") as f32,
            step_numerator: power(clamp.split_once("set to ").expect("M's value").1) as f32,
            efc_top: number_after(&prose, "The loop clamps u to 0 to ") as f32,
            gain: power(&row["G"]) as f32,
            tau: leading_number(&row["τ, as the ROM block sets it"]) as f32,
            settled_blocks: number_after(&prose, "whose mean is within 150 ns, to ") as i32,
            settled: power(prose.split_once("the double ").expect("the bound").1),
            ramp_step: number_after(&prose, "lengthens its constant by ") as f32,
        }
    }

    /// 1/G, as both of the firmware's reciprocals hold it.
    fn reciprocal(self) -> f32 {
        1.0 / self.gain
    }

    fn gains(self, tau: f32) -> Gains {
        Gains {
            proportional: self.reciprocal() / tau,
            integral: self.reciprocal() / tau / tau / self.integral_scale,
            filter_weight: self.filter / tau,
        }
    }

    fn step_limit(self) -> f32 {
        self.step_numerator / self.gain.abs()
    }
}

/// K, k and a for a time constant.
#[derive(Clone, Copy)]
struct Gains {
    proportional: f32,
    integral: f32,
    filter_weight: f32,
}

/// What a replay leaves out or changes, to show the firmware does what
/// the law says and not the simpler thing.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Variant {
    /// The law as loop.md states it, with startup's τ steps.
    Firmware,
    /// The error the block's last summed reading, not its mean.
    LastReading,
    /// Startup's τ steps without the integrator's adjustment.
    NoRescale,
}

/// The loop's state as the replay keeps it.
struct Loop {
    law: Law,
    receiver: &'static Receiver,
    variant: Variant,
    setpoint: f32,
    temperature_coefficient: f32,
    threshold: f32,
    filtered: f32,
    integral: f32,
    base: f32,
    efc: f32,
    sum: f32,
    last: f32,
    readings: u8,
    seconds: u8,
    questionable_seen: bool,
    failed: bool,
    causes: u16,
    tau: f32,
    state: u8,
    blocks: i32,
    normal_gains: Gains,
    result: u8,
}

impl Loop {
    fn new(
        law: Law,
        receiver: &'static Receiver,
        variant: Variant,
        trace: &Trace,
        scenario: &str,
    ) -> Self {
        Self {
            law,
            receiver,
            variant,
            setpoint: trace.constant(scenario, "ti_setpoint"),
            temperature_coefficient: trace.constant(scenario, "tc_coefficient"),
            threshold: trace.constant(scenario, "threshold"),
            filtered: 0.0,
            integral: 0.0,
            base: 0.0,
            efc: trace.constant(scenario, "first_efc"),
            sum: 0.0,
            last: 0.0,
            readings: 0,
            seconds: 0,
            questionable_seen: false,
            failed: false,
            causes: 0,
            tau: trace.constant(scenario, "first_time_constant"),
            state: 0,
            blocks: 0,
            normal_gains: law.gains(law.tau),
            result: 0,
        }
    }

    /// A second's reading into the block, if it is summed.
    fn reading(&mut self, row: &Row) {
        if self.receiver.reference_gates_readings && row.questionable {
            self.questionable_seen = true;
        }
        if !row.valid {
            self.failed = true;
            return;
        }
        if self.receiver.reference_gates_readings && (!row.reference_good || row.questionable) {
            return;
        }
        if row.reading.abs() < self.threshold {
            self.sum += row.reading;
            self.last = row.reading;
            self.readings += 1;
        }
    }

    /// The block's mean into `sum`; whether any reading was summed.
    /// A block with none adds a hold cause, unless a second in it was
    /// questionable.
    fn mean(&mut self) -> bool {
        if self.readings == 0 {
            self.sum = 0.0;
            if !self.questionable_seen {
                self.causes |= if self.failed {
                    HOLD_CAUSE_TI_ERROR
                } else {
                    HOLD_CAUSE_TI_LIMIT_EXCEEDED
                };
            }
            return false;
        }
        self.sum = if self.variant == Variant::LastReading {
            self.last
        } else {
            self.sum / f32::from(self.readings)
        };
        true
    }

    fn clear_block(&mut self) {
        self.sum = 0.0;
        self.readings = 0;
        self.seconds = 0;
        self.questionable_seen = false;
        self.failed = false;
    }

    /// The update: loop.md's "The update", then the clamp.
    #[expect(
        clippy::cast_precision_loss,
        reason = "the firmware converts the uptime to a float"
    )]
    fn steer(&mut self, gains: Gains, row: &Row) {
        let law = self.law;
        let error = self.sum - self.setpoint;
        self.filtered = (1.0 - gains.filter_weight) * self.filtered + gains.filter_weight * error;
        let limit = law.step_limit();
        let slope = (row.fit_c * law.entry / row.uptime as f32 + row.fit_b).clamp(-limit, limit);
        self.integral +=
            (slope / gains.integral / law.entry + self.filtered) * gains.integral * law.block;
        let efc = gains.proportional * self.filtered + self.base + self.integral;
        self.efc = (efc + self.temperature_coefficient * row.current).clamp(0.0, law.efc_top);
    }

    /// The integrator's adjustment for a step of startup's τ from
    /// `previous`: I + (1/G)·f·(1/previous − 1/τ).
    fn rescale(&mut self, previous: f32) {
        if self.variant != Variant::NoRescale {
            self.integral +=
                self.law.reciprocal() * self.filtered * (1.0 / previous + -1.0 / self.tau);
        }
    }

    fn entry(&mut self, row: &Row) {
        self.integral = 0.0;
        self.base = self.efc - self.temperature_coefficient * row.current;
        self.clear_block();
    }

    /// The block's end, at its tenth second; whether it was.
    fn second(&mut self, row: &Row) -> bool {
        self.reading(row);
        self.seconds += 1;
        self.seconds >= 10
    }

    fn startup(&mut self, row: &Row) {
        let law = self.law;
        if !row.same_stage {
            self.filtered = 0.0;
            self.entry(row);
            self.blocks = 0;
            self.state = STARTUP_SETTLING;
            return;
        }
        if !self.second(row) {
            return;
        }
        let summed = self.mean();
        match self.state {
            STARTUP_SETTLING => {
                if self.blocks < law.settled_blocks {
                    if f64::from(self.sum.abs()) < law.settled {
                        self.blocks += 1;
                    } else {
                        self.blocks = 0;
                    }
                } else {
                    self.state = if row.collecting {
                        STARTUP_LENGTHENING
                    } else {
                        STARTUP_WAITING_FOR_HISTORY
                    };
                }
            }
            STARTUP_WAITING_FOR_HISTORY => {
                if row.history_ready {
                    self.state = STARTUP_LENGTHENING;
                }
            }
            STARTUP_LENGTHENING => {
                if law.tau - law.ramp_step <= self.tau {
                    if summed {
                        self.tau = law.tau;
                        self.rescale(law.tau - law.ramp_step);
                        self.result = RESULT_DONE;
                    }
                } else {
                    self.tau += law.ramp_step;
                    self.rescale(self.tau - law.ramp_step);
                }
            }
            _ => {}
        }
        if summed || self.receiver.empty_block_steers {
            self.steer(law.gains(self.tau), row);
        }
        self.clear_block();
    }

    fn normal(&mut self, row: &Row) {
        if !row.same_stage {
            self.normal_gains = self.law.gains(self.law.tau);
            self.entry(row);
            return;
        }
        if !self.second(row) {
            return;
        }
        if self.mean() || self.receiver.empty_block_steers {
            self.steer(self.normal_gains, row);
        }
        self.clear_block();
    }

    fn step(&mut self, row: &Row) {
        self.result = 0;
        self.causes = 0;
        if row.stage == "startup_pll" {
            self.startup(row);
        } else {
            self.normal(row);
        }
    }
}

/// Whether two of the firmware's floats agree to within its rounding.
fn close(a: f32, b: f32, floor: f32) -> bool {
    (a - b).abs() <= floor.max(1e-6 * a.abs().max(b.abs()))
}

/// The first row where the replay and the firmware part, described.
fn replay(receiver: &'static Receiver, variant: Variant) -> Result<usize, String> {
    let evidence = read("docs/firmware/loop.md");
    let law = Law::from_evidence(&evidence, receiver);
    let trace = Trace::load(receiver.image);
    let mut checked = 0;
    for scenario in trace.scenarios() {
        let mut state = Loop::new(law, receiver, variant, &trace, &scenario);
        for (index, row) in trace
            .rows
            .iter()
            .filter(|row| row.scenario == scenario)
            .enumerate()
        {
            state.step(row);
            let pairs = [
                ("f", state.filtered, row.ti_filtered, 1e-16),
                ("I", state.integral, row.integral, 1e-3),
                ("B", state.base, row.efc_base, 1e-2),
                ("u", state.efc, row.efc, 1e-2),
                ("startup's τ", state.tau, row.loop_time_constant, 0.0),
            ];
            for (name, replayed, firmware, floor) in pairs {
                if !close(replayed, firmware, floor) {
                    return Err(format!(
                        "{} {scenario} row {index} ({} at uptime {}): {name} replayed {replayed:e}, firmware {firmware:e}",
                        receiver.image, row.stage, row.uptime
                    ));
                }
            }
            if row.stage == "startup_pll"
                && (state.state != row.startup_state || state.blocks != row.large_offset_blocks)
            {
                return Err(format!(
                    "{} {scenario} row {index}: startup state {} after {} blocks replayed, firmware {} after {}",
                    receiver.image,
                    state.state,
                    state.blocks,
                    row.startup_state,
                    row.large_offset_blocks
                ));
            }
            if row.stage == "startup_pll"
                && (state.result == RESULT_DONE) != (row.result == RESULT_DONE)
            {
                return Err(format!(
                    "{} {scenario} row {index}: done replayed {}, firmware {}",
                    receiver.image, state.result, row.result
                ));
            }
            if state.causes != row.hold_causes {
                return Err(format!(
                    "{} {scenario} row {index}: hold causes replayed {:#x}, firmware {:#x}",
                    receiver.image, state.causes, row.hold_causes
                ));
            }
            checked += 1;
        }
    }
    Ok(checked)
}

#[test]
fn the_firmware_runs_the_evidence_s_update_law() {
    for receiver in &RECEIVERS {
        let rows = replay(receiver, Variant::Firmware).unwrap_or_else(|error| panic!("{error}"));
        assert!(rows > 1000, "{}: {rows} rows", receiver.image);
    }
}

#[test]
fn a_block_with_no_reading_updates_on_the_z3805a_and_not_on_the_3704() {
    let evidence = flat(&read("docs/firmware/loop.md"));
    assert!(evidence.contains("and still updates, with the mean taken as 0: e = −x₀"));
    assert!(evidence.contains("a ten seconds with none skips the update"));
    for receiver in &RECEIVERS {
        let trace = Trace::load(receiver.image);
        let normal: Vec<&Row> = trace
            .rows
            .iter()
            .filter(|row| row.scenario == "normal")
            .collect();
        let mut seen = Vec::new();
        for pair in normal.windows(2) {
            let (before, after) = (pair[0], pair[1]);
            if after.hold_causes == 0 {
                continue;
            }
            assert_eq!(
                after.ti_filtered != before.ti_filtered,
                receiver.empty_block_steers,
                "{}: a block with none summed at uptime {}",
                receiver.image,
                after.uptime
            );
            seen.push(after.hold_causes);
        }
        assert_eq!(
            seen,
            [HOLD_CAUSE_TI_ERROR, HOLD_CAUSE_TI_LIMIT_EXCEEDED],
            "{}: the trace's empty blocks",
            receiver.image
        );
    }
}

#[test]
fn the_firmware_averages_the_block_and_rescales_the_integral() {
    for receiver in &RECEIVERS {
        for variant in [Variant::LastReading, Variant::NoRescale] {
            assert!(
                replay(receiver, variant).is_err(),
                "{}: the firmware is told apart from a simpler loop",
                receiver.image
            );
        }
    }
}

#[test]
fn the_firmware_s_gains_are_the_evidence_s() {
    let evidence = read("docs/firmware/loop.md");
    for receiver in &RECEIVERS {
        let law = Law::from_evidence(&evidence, receiver);
        let trace = Trace::load(receiver.image);
        let firmware = Firmware::load(receiver.image);
        let row = evidence_row(&evidence, receiver);
        assert_eq!(
            trace.constant("normal", "gain"),
            firmware.float(addresses(&row["G"])[0]),
            "{}: the trace ran with the G loop.md cites",
            receiver.image
        );
        assert_eq!(trace.constant("normal", "max_loop_time"), law.tau);
        assert!(close(
            trace.constant("normal", "reciprocal_ti"),
            law.reciprocal(),
            0.0
        ));
        assert!(close(
            trace.constant("normal", "reciprocal_tau"),
            law.reciprocal(),
            0.0
        ));
        assert!(
            close(
                trace.constant("normal", "step_limit"),
                law.step_limit(),
                0.0
            ),
            "{}: M",
            receiver.image
        );
        let gains = law.gains(law.tau);
        let entry = &trace.rows[0];
        for (name, firmware, documented) in [
            ("K", entry.proportional_gain, gains.proportional),
            ("k", entry.integral_gain, gains.integral),
            ("a", entry.filter_weight, gains.filter_weight),
        ] {
            assert!(
                close(firmware, documented, 0.0),
                "{}: {name} is {firmware:e} in the firmware, {documented:e} by loop.md",
                receiver.image
            );
        }
    }
}

/// A complex number, enough of one for a cubic's roots.
#[derive(Clone, Copy)]
struct Complex {
    re: f64,
    im: f64,
}

impl Complex {
    const fn new(re: f64, im: f64) -> Self {
        Self { re, im }
    }
    fn add(self, other: Self) -> Self {
        Self::new(self.re + other.re, self.im + other.im)
    }
    fn sub(self, other: Self) -> Self {
        Self::new(self.re - other.re, self.im - other.im)
    }
    fn mul(self, other: Self) -> Self {
        Self::new(
            self.re * other.re - self.im * other.im,
            self.re * other.im + self.im * other.re,
        )
    }
    fn div(self, other: Self) -> Self {
        let norm = other.re * other.re + other.im * other.im;
        Self::new(
            (self.re * other.re + self.im * other.im) / norm,
            (self.im * other.re - self.re * other.im) / norm,
        )
    }
    fn abs(self) -> f64 {
        self.re.hypot(self.im)
    }
}

/// The update the firmware runs, closed around an oscillator that
/// turns an EFC of u into a phase change of −G·u a second, applied
/// from the second after the update: a matrix on (phase at the update,
/// f, I), with G taken as 1 since it cancels.  The block's readings are
/// X − j·u for j from 1 to 10, so their mean is X − 5.5·u.
fn update(law: Law, tau: f64) -> [[f64; 3]; 3] {
    let block = f64::from(law.block);
    let a = f64::from(law.filter) / tau;
    let proportional = 1.0 / tau;
    let integral = 1.0 / (f64::from(law.integral_scale) * tau * tau);
    let held = (block + 1.0) / 2.0;
    let u = [0.0, proportional, 1.0];
    let mean = [1.0 - held * u[0], -held * u[1], -held * u[2]];
    let filter = [a * mean[0], (1.0 - a) + a * mean[1], a * mean[2]];
    let integrator = [
        block * integral * filter[0],
        block * integral * filter[1],
        1.0 + block * integral * filter[2],
    ];
    let phase = [1.0 - block * u[0], -block * u[1], -block * u[2]];
    [phase, filter, integrator]
}

/// The update's poles in the s-plane, scaled by τ, slowest first.
fn poles(law: Law, tau: f64) -> [Complex; 3] {
    let m = update(law, tau);
    let trace = m[0][0] + m[1][1] + m[2][2];
    let minors = m[0][0] * m[1][1] - m[0][1] * m[1][0] + m[0][0] * m[2][2] - m[0][2] * m[2][0]
        + m[1][1] * m[2][2]
        - m[1][2] * m[2][1];
    let determinant = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
        - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    let value = |z: Complex| {
        let real = |x: f64| Complex::new(x, 0.0);
        z.mul(z)
            .mul(z)
            .sub(real(trace).mul(z).mul(z))
            .add(real(minors).mul(z))
            .sub(real(determinant))
    };
    let seed = Complex::new(0.4, 0.9);
    let mut roots = [Complex::new(1.0, 0.0), seed, seed.mul(seed)];
    for _ in 0..500 {
        for i in 0..3 {
            let denominator = (0..3)
                .filter(|j| *j != i)
                .fold(Complex::new(1.0, 0.0), |product, j| {
                    product.mul(roots[i].sub(roots[j]))
                });
            roots[i] = roots[i].sub(value(roots[i]).div(denominator));
        }
    }
    let scale = tau / f64::from(law.block);
    let mut poles =
        roots.map(|z| Complex::new(z.abs().ln() * scale, z.im.atan2(z.re).abs() * scale));
    poles.sort_by(|x, y| y.re.total_cmp(&x.re));
    poles
}

/// The phase after each update, from a unit phase error at rest.
fn step_response(law: Law, tau: f64, updates: usize) -> Vec<f64> {
    let m = update(law, tau);
    let mut state = [1.0, 0.0, 0.0];
    (0..updates)
        .map(|_| {
            state = [0, 1, 2].map(|row| (0..3).map(|col| m[row][col] * state[col]).sum());
            state[0]
        })
        .collect()
}

/// The seconds `startup_pll`'s ramp took in the trace, from its first
/// lengthening update to the one that was done.
fn ramp_seconds(trace: &Trace) -> u32 {
    let startup = || {
        trace
            .rows
            .iter()
            .filter(|row| row.scenario == "startup" && row.stage == "startup_pll")
    };
    let first = startup()
        .find(|row| row.startup_state == STARTUP_LENGTHENING)
        .expect("the ramp");
    let done = startup()
        .find(|row| row.result == RESULT_DONE)
        .expect("its end");
    done.uptime - first.uptime
}

#[test]
fn the_page_s_by_model_rows_are_the_firmware_s() {
    let evidence = read("docs/firmware/loop.md");
    let page = read("docs/loop.html");
    let rows = page_rows(&page);
    for receiver in &RECEIVERS {
        let law = Law::from_evidence(&evidence, receiver);
        let trace = Trace::load(receiver.image);
        let row = rows
            .iter()
            .find(|row| receiver.page_row.iter().all(|word| row[0].contains(word)))
            .unwrap_or_else(|| panic!("loop.html has no row for {}", receiver.image));
        let tau = trace.constant("normal", "max_loop_time");
        assert_eq!(
            leading_number(&row[1]),
            f64::from(tau),
            "{}: τ",
            receiver.image
        );
        let gain = trace.constant("normal", "gain");
        assert!(
            (power(&row[2]) / f64::from(gain) - 1.0).abs() < 1e-6,
            "{}: G",
            receiver.image
        );
        let weight = trace.rows[0].filter_weight;
        assert!(
            (leading_number(&row[3]) / f64::from(weight) - 1.0).abs() < 1e-6,
            "{}: a is {weight} in the firmware, {} on the page",
            receiver.image,
            row[3]
        );
        let slowest = f64::from(tau) / -poles(law, f64::from(tau))[0].re;
        assert_eq!(
            leading_number(&row[4]),
            (slowest / 10.0).round() * 10.0,
            "{}: the slowest pole, {slowest:.0} s",
            receiver.image
        );
        let ramp = f64::from(ramp_seconds(&trace));
        let stated = leading_number(row[5].trim_start_matches("≈ "));
        assert_eq!(
            stated,
            (ramp / 100.0).round() * 100.0,
            "{}: the startup ramp, {ramp} s",
            receiver.image
        );
    }
}

#[test]
fn the_page_s_step_response_is_the_firmware_s() {
    const UPDATES: usize = 3000;
    let evidence = read("docs/firmware/loop.md");
    let page = flat(&read("docs/loop.html"));
    let overshoot = number_after(&page, "overshoots by about ");
    let removed = number_after(&page, "the loop removes ");
    assert!(page.contains(&format!("removes {removed} % of it in about τ")));
    for receiver in &RECEIVERS {
        let law = Law::from_evidence(&evidence, receiver);
        let tau = f64::from(law.tau);
        let response = step_response(law, tau, UPDATES);
        let peak = -response.iter().copied().fold(f64::INFINITY, f64::min) * 100.0;
        assert!(
            (peak - overshoot).abs() < 2.5,
            "{}: the firmware's loop overshoots by {peak:.1} %",
            receiver.image
        );
        let left = 1.0 - removed / 100.0;
        let reached = response
            .iter()
            .position(|x| x.abs() < left)
            .expect("the error falls");
        #[expect(clippy::cast_precision_loss, reason = "a few thousand updates")]
        let seconds = (reached + 1) as f64 * f64::from(law.block);
        assert!(
            (seconds / tau - 1.0).abs() < 0.1,
            "{}: {removed} % is removed after {seconds} s",
            receiver.image
        );
    }
}

#[test]
fn the_page_states_the_firmware_s_poles() {
    /// How far the page's two-decimal poles may be from the firmware's
    /// at either receiver's τ, as a fraction of their magnitude.
    const TOLERANCE: f64 = 0.02;
    let evidence = read("docs/firmware/loop.md");
    let page = flat(&read("docs/loop.html"));
    let sentence = page
        .split_once("For the 10 s update itself its roots are a real pole at ")
        .expect("the update's poles")
        .1;
    let real = -number_after(sentence, "−");
    let pair = sentence.split_once("a pair at (−").expect("the pair").1;
    let pair_re = -leading_number(pair);
    let pair_im = number_after(pair, "± ");
    let damping = number_after(sentence, "damping ratio ");
    for receiver in &RECEIVERS {
        let law = Law::from_evidence(&evidence, receiver);
        let firmware = poles(law, f64::from(law.tau));
        let stated = [Complex::new(real, 0.0), Complex::new(pair_re, pair_im)];
        for (stated, firmware) in stated.iter().zip(firmware) {
            assert!(
                stated.sub(firmware).abs() / firmware.abs() < TOLERANCE,
                "{}: the page's pole ({:.2}, {:.2}j)/τ, the firmware's ({:.3}, {:.3}j)/τ",
                receiver.image,
                stated.re,
                stated.im,
                firmware.re,
                firmware.im
            );
        }
        let firmware_damping = -firmware[1].re / firmware[1].abs();
        assert!(
            (damping - firmware_damping).abs() < 0.01,
            "{}: the page's damping ratio {damping}, the firmware's {firmware_damping:.3}",
            receiver.image
        );
    }
}

#[test]
fn the_hand_over_to_pll_normal_moves_u_by_about_k_f() {
    /// How near K·f the first update's step must be, as a fraction of
    /// it: the rest is that update's own, a ten-second step of a loop
    /// that has settled.
    const NEAR: f32 = 0.1;
    let evidence = flat(&read("docs/firmware/loop.md"));
    assert!(
        evidence.contains("leaves f, so its first update moves u by about K·f"),
        "loop.md no longer says the hand-over moves u by about K·f"
    );
    for receiver in &RECEIVERS {
        let trace = Trace::load(receiver.image);
        let rows: Vec<&Row> = trace
            .rows
            .iter()
            .filter(|row| row.scenario == "startup")
            .collect();
        let entry = rows
            .iter()
            .position(|row| row.stage == "pll_normal" && !row.same_stage)
            .expect("the hand-over");
        let handed = rows[entry];
        let first = rows[entry + 1..]
            .iter()
            .find(|row| row.efc != handed.efc)
            .expect("an update");
        let kept = handed.proportional_gain * handed.ti_filtered;
        let step = first.efc - handed.efc;
        assert!(
            ((step - kept) / kept).abs() < NEAR,
            "{}: pll_normal's first update moves u by {step} counts, K·f is {kept}",
            receiver.image
        );
    }
}
