//! The records of an SCPI command tree, every one its roots reach,
//! read by the layouts `tools/ghidra/types/smartclock.h` declares and
//! named by the command path that first reaches them, for `--c` and
//! `--names` (docs/scpi/README.md, "As C").
//!
//! A record that does not hold together as its layout says -- a
//! pointer outside the image, a list that does not end, a string that
//! is not a keyword -- refuses the image rather than being guessed at.

use std::collections::BTreeMap;
use std::collections::BTreeSet;

use anyhow::Result;
use anyhow::bail;
use anyhow::ensure;

use super::CHILDREN_SLOT;
use super::CHILDREN_START;
use super::COUNT_SLOT;
use super::Entry;
use super::KEYWORD_SLOT;
use super::QUERY_SLOT;
use super::Roots;
use super::SETTER_SLOT;
use super::roots;
use super::tree_entries;
use crate::image::Image;

/// A node's bytes (`scpi_node`), its unused bytes included.
const NODE_SIZE: u32 = 0x32;
/// Where a node's unused bytes start, which are zero in every image.
const NODE_UNUSED_SLOT: u32 = 0x24;
/// Where a node keeps its response items, zero for none.
const RESPONSE_ITEMS_SLOT: u32 = 0x1c;
/// Where a node keeps the bytes its query's response takes.
const RESPONSE_SIZE_SLOT: u32 = 0x20;
/// Where a node keeps the word it adds to the path.
const PATH_WORD_SLOT: u32 = 0x22;
/// Where a handler entry keeps its parameter list, after its routine.
const HANDLER_PARAMETERS: u32 = 4;
/// Where a handler entry keeps the bytes its parameters take.
const HANDLER_PARAMETERS_SIZE: u32 = 8;

/// Where a child list keeps its id.
const ID_SLOT: u32 = 0;
/// Where a child list keeps the child a header may leave out.
const DEFAULT_CHILD_SLOT: u32 = 4;

/// A parameter's bytes (`scpi_parameter`): the next, then its kinds.
const PARAMETER_SIZE: u32 = 8;
/// A tree pair's or a choice list's bytes: two pointers.
const POINTER_PAIR_SIZE: u32 = 8;
/// Where a parameter keeps its kinds.
const PARAMETER_KINDS_SLOT: u32 = 4;

/// Where a parameter's kinds keep whether it may be left out.
const OPTIONAL_SLOT: u32 = 2;
/// Where they keep the units a number may take.
const UNITS_SLOT: u32 = 4;
/// Where they keep the keywords a choice may take.
const CHOICES_SLOT: u32 = 8;
/// Where their converters start, one for each parameter kind.
const CONVERTERS_SLOT: u32 = 0xc;
/// How many converters: `SCPI_PARAMETER_DECIMAL` to `_EXPRESSION`
/// (rewritten/z3805a-3543b/scpi.h), which the records' stride confirms.
const PARAMETER_KINDS: usize = 6;
/// A parameter's kinds' bytes (`scpi_parameter_kinds`).
const KINDS_SIZE: u32 = CONVERTERS_SLOT + 4 * PARAMETER_KINDS as u32;

/// A unit's bytes (`scpi_unit`): its name, then its exponent.
const UNIT_SIZE: u32 = 6;
/// Where a unit keeps its exponent.
const UNIT_EXPONENT_SLOT: u32 = 4;
/// No unit table in any image holds this many; one that runs on is
/// not a unit table.
const UNITS_LIMIT: usize = 16;
/// The longest unit name in any image.
const UNIT_NAME_LIMIT: usize = 8;

/// Where a choice list keeps the values its keywords give.
const VALUES_SLOT: u32 = 4;
/// No choice list in any image holds this many keywords.
const CHOICES_LIMIT: usize = 64;

/// A response item's bytes (`scpi_response_item`).
const ITEM_SIZE: u32 = 10;
/// Where an item keeps the bytes its value takes.
const ITEM_SIZE_SLOT: u32 = 4;
/// Where an item keeps its formatter's argument.
const ITEM_ARGUMENT_SLOT: u32 = 6;
/// No node in any image answers with this many items.
const ITEMS_LIMIT: usize = 32;

/// The start of RAM, where the boot code unpacks the installer
/// (docs/firmware/restart.md, "The installer").
const RAM: u32 = 0x10_0000;
/// The RAM an installer may take, as `tools/firmware/decompiled_check.py`
/// allows it.
const INSTALLER_LIMIT: u32 = 0x1_0000;
/// What starts the boot code's records of the installer
/// (docs/firmware/restart.md, "Forced installer entry with an unusable
/// primary").
const RECORDS_START: u8 = b'S';
/// A record: a count, a RAM destination and that many bytes.
const RECORD_COPY: u8 = b'C';
/// What ends the records.
const RECORDS_END: u8 = b'E';
/// A record's count and destination.
const RECORD_HEADER: u32 = 8;
/// The end of the flash below the writable range at its highest, where
/// the installer and its records live and nothing writes
/// (docs/firmware/restart.md, "The installer"); a stream of records
/// above it is the primary's bytes that happen to read as one.
const INSTALLER_FLASH_END: u32 = 0x2_0000;

/// Which of an image's command trees.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) enum Tree {
    /// The primary firmware's.
    Primary,
    /// The installer's, as the boot code unpacks it into RAM.
    Installer,
}

impl Tree {
    /// What every name made for this tree starts with.
    fn prefix(self) -> &'static str {
        match self {
            Tree::Primary => "",
            Tree::Installer => "installer_",
        }
    }

    /// Where the address `at` is, as a names file writes it: an
    /// installer address in RAM is in the installer's own space, apart
    /// from the primary's RAM there.
    fn space(self, at: u32) -> Space {
        match self {
            Tree::Installer if at >= RAM => Space::Installer,
            _ => Space::Primary,
        }
    }
}

/// An address space a names file names addresses in.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub(super) enum Space {
    /// The image and the primary's RAM: a bare address.
    Primary,
    /// The installer's RAM: `installer::` and the address.
    Installer,
}

/// What a names file names, by address (tools/ghidra/names/).
#[derive(Debug, Default)]
pub(super) struct NamesFile {
    /// Each function row's name.
    functions: BTreeMap<(Space, u32), String>,
    /// Each label row's name, the first where there are several.
    labels: BTreeMap<(Space, u32), String>,
}

/// What an address in a names file is prefixed with in the installer's
/// space.
const INSTALLER_SPACE: &str = "installer::";

impl NamesFile {
    /// The function and label rows of a names file; its other rows,
    /// locals and calls, name no address.
    pub(super) fn parse(text: &str) -> Result<Self> {
        let mut names = Self::default();
        for line in text.lines() {
            if line.trim().is_empty() || line.starts_with('#') {
                continue;
            }
            let fields: Vec<&str> = line.split('\t').collect();
            let [address, kind, name, ..] = fields.as_slice() else {
                bail!("a row without an address, a kind and a name: {line}");
            };
            let table = match *kind {
                "function" => &mut names.functions,
                "label" => &mut names.labels,
                _ => continue,
            };
            let (space, hex) = match address.strip_prefix(INSTALLER_SPACE) {
                Some(hex) => (Space::Installer, hex),
                None => (Space::Primary, *address),
            };
            let Some(at) = hex
                .strip_prefix("0x")
                .and_then(|hex| u32::from_str_radix(hex, 16).ok())
            else {
                bail!("unparsable address: {line}");
            };
            table
                .entry((space, at))
                .or_insert_with(|| (*name).to_owned());
        }
        Ok(names)
    }

    /// The function the file names at `at`.
    fn function(&self, space: Space, at: u32) -> Option<&str> {
        self.functions.get(&(space, at)).map(String::as_str)
    }

    /// The file's name for `at`: its function's, else its label's.
    pub(super) fn any(&self, space: Space, at: u32) -> Option<&str> {
        self.function(space, at)
            .or_else(|| self.labels.get(&(space, at)).map(String::as_str))
    }
}

/// One handler entry of a node, as `scpi_handler` lays it out.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) struct Handler {
    /// Its routine, zero for none.
    pub(super) function: u32,
    /// Its first parameter, zero for none.
    pub(super) parameters: u32,
    /// The bytes its parameters take in the call's buffer.
    pub(super) parameters_size: i16,
}

/// One unit of a unit table, as `scpi_unit` lays it out.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) struct Unit {
    /// Its name, reversed; zero in the last.
    pub(super) name: u32,
    /// The power of ten it scales by.
    pub(super) exponent: i16,
}

/// One item of a response, as `scpi_response_item` lays it out.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) struct Item {
    /// Its formatter; zero in the last.
    pub(super) format: u32,
    /// The bytes its value takes.
    pub(super) size: i16,
    /// The formatter's argument: a choice list, or zero.
    pub(super) argument: u32,
}

/// What a record holds, field by field, as its type in smartclock.h
/// lays it out.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(super) enum Body {
    /// `scpi_tree_pair`: the two root lists.
    TreePair {
        /// The common commands' list.
        common: u32,
        /// The main tree's root list.
        main: u32,
    },
    /// `scpi_child_list`.
    ChildList {
        /// Its id.
        id: u16,
        /// The child a header may leave out, -1 for none.
        default_child: i16,
        /// Its nodes; the count is how many.
        children: Vec<u32>,
    },
    /// `scpi_node`.
    Node {
        /// Its keyword pair.
        keywords: u32,
        /// Its child list, zero for a leaf.
        children: u32,
        /// Its setter, then its query.
        handlers: [Handler; 2],
        /// Its query's response items, zero for none.
        response_items: u32,
        /// The bytes its query's response takes.
        response_size: i16,
        /// The word it adds to the path, -1 for none.
        path_word: i16,
    },
    /// A keyword pair, a `char` array: the short form, a NUL, the rest
    /// of the long form and a NUL.
    Keyword {
        /// The short form.
        short: String,
        /// The rest of the long form, upper case as it stands.
        rest: String,
    },
    /// `scpi_parameter`.
    Parameter {
        /// The next parameter, zero after the last.
        next: u32,
        /// Its kinds.
        kinds: u32,
    },
    /// `scpi_parameter_kinds`, with its converters.
    Kinds {
        /// The value's bytes, after its kind word.
        size: i16,
        /// Nonzero when it may be left out.
        optional: i16,
        /// The units a number may take, zero for none.
        units: u32,
        /// The keywords a choice may take, zero for none.
        choices: u32,
        /// Each parameter kind's converter, zero for one it does not
        /// take.
        converters: [u32; PARAMETER_KINDS],
    },
    /// A `scpi_unit` array, its zero-named last entry included.
    Units(Vec<Unit>),
    /// A unit's name, a NUL-terminated `char` array, reversed.
    UnitName(String),
    /// `scpi_choices`.
    Choices {
        /// Its keyword pairs' pointers.
        keywords: u32,
        /// The value each keyword gives, zero for its index.
        values: u32,
    },
    /// A choice list's `char *` array: each keyword pair, then NULL.
    ChoiceKeywords(Vec<u32>),
    /// A choice list's `int16_t` values, one for each keyword.
    ChoiceValues(Vec<i16>),
    /// A `scpi_response_item` array, its formatter-less last item
    /// included.
    Items(Vec<Item>),
}

impl Body {
    /// Its bytes.
    pub(super) fn size(&self) -> u32 {
        let count = |n: usize| u32::try_from(n).expect("a record's count is small");
        match self {
            Body::TreePair { .. } | Body::Choices { .. } => POINTER_PAIR_SIZE,
            Body::Parameter { .. } => PARAMETER_SIZE,
            Body::ChildList { children, .. } => CHILDREN_START + 4 * count(children.len()),
            Body::Node { .. } => NODE_SIZE,
            Body::Keyword { short, rest } => count(short.len() + rest.len() + 2),
            Body::Kinds { .. } => KINDS_SIZE,
            Body::Units(units) => UNIT_SIZE * count(units.len()),
            Body::UnitName(name) => count(name.len() + 1),
            Body::ChoiceKeywords(keywords) => 4 * count(keywords.len() + 1),
            Body::ChoiceValues(values) => 2 * count(values.len()),
            Body::Items(items) => ITEM_SIZE * count(items.len()),
        }
    }

    /// Whether it is an array in C, named without `&` to point at it.
    pub(super) fn is_array(&self) -> bool {
        matches!(
            self,
            Body::Keyword { .. }
                | Body::Units(_)
                | Body::UnitName(_)
                | Body::ChoiceKeywords(_)
                | Body::ChoiceValues(_)
                | Body::Items(_)
        )
    }

    /// What kind of record it is, for a message.
    fn kind(&self) -> &'static str {
        match self {
            Body::TreePair { .. } => "a tree pair",
            Body::ChildList { .. } => "a child list",
            Body::Node { .. } => "a node",
            Body::Keyword { .. } => "a keyword pair",
            Body::Parameter { .. } => "a parameter",
            Body::Kinds { .. } => "a parameter's kinds",
            Body::Units(_) => "a unit table",
            Body::UnitName(_) => "a unit's name",
            Body::Choices { .. } => "a choice list",
            Body::ChoiceKeywords(_) => "a choice list's keywords",
            Body::ChoiceValues(_) => "a choice list's values",
            Body::Items(_) => "a response item list",
        }
    }
}

/// One record: what it holds, its name, and what it is, for a reader.
#[derive(Debug)]
pub(super) struct Record {
    /// Its fields.
    pub(super) body: Body,
    /// Its name, the names file's or one made from the path.
    pub(super) name: String,
    /// What it is: the path and the part of it.
    pub(super) note: String,
}

/// What a routine the records point at is for.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) enum Role {
    /// A node's setter or query.
    Handler,
    /// A parameter kind's converter.
    Converter,
    /// A response item's formatter.
    Formatter,
}

/// A routine the records point at, outside the tree.
#[derive(Debug)]
pub(super) struct Routine {
    /// What it is for.
    pub(super) role: Role,
    /// Its name, if it has one: a handler's is the names file's, or
    /// SmartClockSetup.java's from the path; the others' only the names
    /// file's.
    pub(super) name: Option<String>,
    /// The first record that points at it, for a message.
    pub(super) user: String,
}

/// Every record of one tree, and the routines they point at.
#[derive(Debug)]
pub(super) struct Records {
    /// Which tree.
    pub(super) tree: Tree,
    /// The tree pair, where the walk starts.
    pub(super) pair: u32,
    /// Each record by its address.
    pub(super) records: BTreeMap<u32, Record>,
    /// Each routine by its address.
    pub(super) routines: BTreeMap<u32, Routine>,
}

/// A command path as SmartClockSetup.java's scpi= argument spells it in
/// a name: a common command's `*` as `common_`, the main tree's leading
/// `:` dropped, and every other `:` an `_`.
fn identifier(path: &str) -> String {
    match path.strip_prefix('*') {
        Some(rest) => format!("common_{rest}"),
        None => path.trim_start_matches(':').to_owned(),
    }
    .replace(':', "_")
}

/// The memory a tree is read from, and where its roots are looked for:
/// the image itself, or with the installer unpacked into RAM above it.
fn memory(bytes: &[u8], tree: Tree) -> Result<(Vec<u8>, u32)> {
    match tree {
        Tree::Primary => Ok((bytes.to_vec(), 0)),
        Tree::Installer => {
            let image = Image(bytes);
            let streams: Vec<Vec<(u32, &[u8])>> = image
                .find(&[RECORDS_START, RECORD_COPY])
                .into_iter()
                .filter_map(|at| installer_records(&image, at + 1))
                .collect();
            let [records] = streams.as_slice() else {
                bail!(
                    "{} streams of installer records found, not one; refusing to guess",
                    streams.len()
                );
            };
            let ram = usize::try_from(RAM)?;
            let mut memory = bytes.to_vec();
            ensure!(memory.len() <= ram, "the image runs into RAM");
            memory.resize(ram + usize::try_from(INSTALLER_LIMIT)?, 0);
            for (destination, data) in records {
                let at = usize::try_from(*destination)?;
                memory[at..at + data.len()].copy_from_slice(data);
            }
            Ok((memory, RAM))
        }
    }
}

/// The boot code's records from `at`, each a RAM destination and its
/// bytes, if they are records to their end: each a copy into the
/// installer's RAM, then the end, below `INSTALLER_FLASH_END`.
fn installer_records<'a>(image: &Image<'a>, at: u32) -> Option<Vec<(u32, &'a [u8])>> {
    let mut records = Vec::new();
    let mut at = at;
    loop {
        match image.u8(at)? {
            RECORD_COPY => {
                let count = image.u32(at + 1)?;
                let destination = image.u32(at + 5)?;
                let start = at.checked_add(1 + RECORD_HEADER)?;
                let end = start.checked_add(count)?;
                if destination < RAM || destination.checked_add(count)? > RAM + INSTALLER_LIMIT {
                    return None;
                }
                let data = image
                    .0
                    .get(usize::try_from(start).ok()?..usize::try_from(end).ok()?)?;
                records.push((destination, data));
                at = end;
            }
            RECORDS_END if !records.is_empty() && at < INSTALLER_FLASH_END => return Some(records),
            _ => return None,
        }
    }
}

/// The reader's state: the memory, the names it may take, and what it
/// has read so far.
struct Reader<'a> {
    /// The memory the tree is in.
    image: Image<'a>,
    /// Which tree.
    tree: Tree,
    /// A names file's names, which replace made ones.
    names: Option<&'a NamesFile>,
    /// Each record read so far.
    records: BTreeMap<u32, Record>,
    /// Each routine pointed at so far.
    routines: BTreeMap<u32, Routine>,
}

impl Records {
    /// Every record of the image's `tree`, named for the first path, in
    /// dump-scpi's order, that reaches it, unless `names` names its
    /// address.
    pub(super) fn read(bytes: &[u8], tree: Tree, names: Option<&NamesFile>) -> Result<Self> {
        let (memory, from) = memory(bytes, tree)?;
        let image = Image(&memory);
        let roots = roots(&image, from)?;
        let entries = tree_entries(&image, roots)?;
        let mut reader = Reader {
            image: Image(&memory),
            tree,
            names,
            records: BTreeMap::new(),
            routines: BTreeMap::new(),
        };
        reader.roots(roots)?;
        for entry in &entries {
            reader.node(entry)?;
        }
        let Reader {
            records, routines, ..
        } = reader;
        let records = Self {
            tree,
            pair: roots.pair,
            records,
            routines,
        };
        records.check_names()?;
        Ok(records)
    }

    /// Refuse two addresses of one name.
    fn check_names(&self) -> Result<()> {
        let mut seen: BTreeMap<&str, u32> = BTreeMap::new();
        let named = self
            .records
            .iter()
            .map(|(at, record)| (*at, record.name.as_str()))
            .chain(
                self.routines
                    .iter()
                    .filter_map(|(at, routine)| Some((*at, routine.name.as_deref()?))),
            );
        for (at, name) in named {
            if let Some(other) = seen.insert(name, at) {
                bail!("{name} names both {other:#x} and {at:#x}; refusing to guess which");
            }
        }
        Ok(())
    }

    /// The routines nothing names, for `--c` to refuse.
    pub(super) fn unnamed(&self) -> Vec<(u32, &Routine)> {
        self.routines
            .iter()
            .filter(|(_, routine)| routine.name.is_none())
            .map(|(at, routine)| (*at, routine))
            .collect()
    }

    /// How many records of each kind, by `Body::kind`'s description.
    #[cfg(test)]
    pub(super) fn counts(&self) -> BTreeMap<&'static str, usize> {
        let mut counts = BTreeMap::new();
        for record in self.records.values() {
            *counts.entry(record.body.kind()).or_insert(0) += 1;
        }
        counts
    }

    /// The address-space prefix a names file writes before `at`.
    pub(super) fn space(&self, at: u32) -> Space {
        self.tree.space(at)
    }
}

impl Reader<'_> {
    /// The name for `at`: the names file's, else `made` with the tree's
    /// prefix.
    fn name(&self, at: u32, made: &str) -> String {
        self.names
            .and_then(|names| names.any(self.tree.space(at), at))
            .map_or_else(|| format!("{}{made}", self.tree.prefix()), str::to_owned)
    }

    /// Whether `at` is already read, as the same kind of record as
    /// `kind` names; another kind is refused.
    fn seen(&self, at: u32, kind: &str) -> Result<bool> {
        match self.records.get(&at) {
            Some(record) if record.body.kind() == kind => Ok(true),
            Some(record) => bail!(
                "{at:#x} is read both as {} ({}) and as {kind}",
                record.body.kind(),
                record.name
            ),
            None => Ok(false),
        }
    }

    /// Keep the record at `at`.
    fn add(&mut self, at: u32, body: Body, made: &str, note: String) -> Result<()> {
        ensure!(
            self.image.holds(at) && self.image.holds(at + body.size() - 1),
            "{} at {at:#x} runs past the image",
            body.kind()
        );
        if !self.seen(at, body.kind())? {
            let name = self.name(at, made);
            self.records.insert(at, Record { body, name, note });
        }
        Ok(())
    }

    /// The routine at `at`, which `user` points at as `role`; a handler
    /// is named `made`, with the tree's prefix, unless the names file
    /// names a function there.
    fn routine(&mut self, at: u32, role: Role, made: Option<&str>, user: &str) -> Result<()> {
        ensure!(
            at.is_multiple_of(2) && self.image.holds(at),
            "{user} points at {at:#x} as code, which is not"
        );
        if self.routines.contains_key(&at) {
            return Ok(());
        }
        let space = self.tree.space(at);
        let name = match role {
            Role::Handler => self
                .names
                .and_then(|names| names.function(space, at))
                .map(str::to_owned)
                .or_else(|| made.map(|made| format!("{}{made}", self.tree.prefix()))),
            Role::Converter | Role::Formatter => self
                .names
                .and_then(|names| names.any(space, at))
                .map(str::to_owned),
        };
        self.routines.insert(
            at,
            Routine {
                role,
                name,
                user: user.to_owned(),
            },
        );
        Ok(())
    }

    /// An even address in the image, or refuse: what `field` holds.
    fn pointer(&self, at: u32, field: &str) -> Result<u32> {
        let target = self
            .image
            .u32(at)
            .ok_or_else(|| anyhow::anyhow!("{field} at {at:#x} runs past the image"))?;
        ensure!(
            target == 0 || (target.is_multiple_of(2) && self.image.holds(target)),
            "{field} at {at:#x} holds {target:#x}, neither zero nor an even address in the image"
        );
        Ok(target)
    }

    /// A string's address in the image, or zero, or refuse: what
    /// `field` holds.  A string may start at any byte.
    fn string_pointer(&self, at: u32, field: &str) -> Result<u32> {
        let target = self
            .image
            .u32(at)
            .ok_or_else(|| anyhow::anyhow!("{field} at {at:#x} runs past the image"))?;
        ensure!(
            target == 0 || self.image.holds(target),
            "{field} at {at:#x} holds {target:#x}, neither zero nor an address in the image"
        );
        Ok(target)
    }

    /// The signed word at `at`.
    fn word(&self, at: u32) -> Result<i16> {
        let word = self
            .image
            .u16(at)
            .ok_or_else(|| anyhow::anyhow!("a word at {at:#x} runs past the image"))?;
        Ok(i16::from_be_bytes(word.to_be_bytes()))
    }

    /// The tree pair and its two root lists.
    fn roots(&mut self, roots: Roots) -> Result<()> {
        self.add(
            roots.pair,
            Body::TreePair {
                common: roots.common,
                main: roots.main,
            },
            "scpi_trees",
            "the common commands' list and the main tree's".to_owned(),
        )?;
        self.list(roots.common, "scpi_list_common", "the common commands")?;
        self.list(roots.main, "scpi_list_main", "the main tree's root")
    }

    /// The child list at `at`.
    fn list(&mut self, at: u32, made: &str, whose: &str) -> Result<()> {
        if self.seen(at, "a child list")? {
            return Ok(());
        }
        let children = self
            .image
            .children(at)
            .ok_or_else(|| anyhow::anyhow!("no child list at {at:#x}"))?;
        let id = self
            .image
            .u16(at + ID_SLOT)
            .ok_or_else(|| anyhow::anyhow!("no child list at {at:#x}"))?;
        let count = self.word(at + COUNT_SLOT)?;
        ensure!(
            usize::try_from(count).ok() == Some(children.len()),
            "the child list at {at:#x} counts {count}, not its {} children",
            children.len()
        );
        let body = Body::ChildList {
            id,
            default_child: self.word(at + DEFAULT_CHILD_SLOT)?,
            children: children.into_iter().map(|(node, _)| node).collect(),
        };
        self.add(at, body, made, format!("the child list of {whose}"))
    }

    /// The node `entry` reaches and every record it holds, unless an
    /// earlier path reached it.
    fn node(&mut self, entry: &Entry) -> Result<()> {
        let at = entry.node;
        if self.seen(at, "a node")? {
            return Ok(());
        }
        let path = &entry.path;
        let id = identifier(path);
        for unused in NODE_UNUSED_SLOT..NODE_SIZE {
            let byte = self
                .image
                .u8(at + unused)
                .ok_or_else(|| anyhow::anyhow!("the node at {at:#x} runs past the image"))?;
            ensure!(
                byte == 0,
                "the node at {at:#x} holds {byte:#x} at +{unused:#x}, where every node holds zero"
            );
        }
        let keywords = self.string_pointer(at + KEYWORD_SLOT, "a node's keyword pair")?;
        let children = self.pointer(at + CHILDREN_SLOT, "a node's child list")?;
        let response_items = self.pointer(at + RESPONSE_ITEMS_SLOT, "a node's response items")?;
        let mut handlers = [Handler {
            function: 0,
            parameters: 0,
            parameters_size: 0,
        }; 2];
        for (handler, (slot, role)) in handlers
            .iter_mut()
            .zip([(SETTER_SLOT, "set"), (QUERY_SLOT, "query")])
        {
            *handler = Handler {
                function: self.pointer(at + slot, "a handler's routine")?,
                parameters: self
                    .pointer(at + slot + HANDLER_PARAMETERS, "a handler's parameters")?,
                parameters_size: self.word(at + slot + HANDLER_PARAMETERS_SIZE)?,
            };
            if handler.function != 0 {
                self.routine(
                    handler.function,
                    Role::Handler,
                    Some(&format!("scpi_{role}_{id}")),
                    path,
                )?;
            }
            self.parameters(handler.parameters, &format!("{role}_{id}"), path, role)?;
        }
        self.keyword(keywords, &format!("the keyword of {path}"))?;
        if children != 0 {
            self.list(children, &format!("scpi_list_{id}"), path)?;
        }
        if response_items != 0 {
            self.items(response_items, &id, path)?;
        }
        let body = Body::Node {
            keywords,
            children,
            handlers,
            response_items,
            response_size: self.word(at + RESPONSE_SIZE_SLOT)?,
            path_word: self.word(at + PATH_WORD_SLOT)?,
        };
        self.add(
            at,
            body,
            &format!("scpi_node_{id}"),
            format!("the node of {path}"),
        )
    }

    /// The parameter list from `first`, of `path`'s `role` handler, its
    /// parameters named `stem` and their place, from 1.
    fn parameters(&mut self, first: u32, stem: &str, path: &str, role: &str) -> Result<()> {
        let mut visited = BTreeSet::new();
        let mut at = first;
        for place in 1.. {
            if at == 0 || self.seen(at, "a parameter")? {
                return Ok(());
            }
            ensure!(
                visited.insert(at),
                "the parameter list from {first:#x} loops back to {at:#x}"
            );
            let next = self.pointer(at, "a parameter's next")?;
            let kinds = self.pointer(at + PARAMETER_KINDS_SLOT, "a parameter's kinds")?;
            ensure!(kinds != 0, "the parameter at {at:#x} has no kinds");
            let part = format!("parameter {place} of {path}'s {role} handler");
            self.kinds(kinds, &format!("{stem}_{place}"), &part)?;
            self.add(
                at,
                Body::Parameter { next, kinds },
                &format!("scpi_parameter_{stem}_{place}"),
                part,
            )?;
            at = next;
        }
        Ok(())
    }

    /// A parameter's kinds at `at`, named `stem`, of `part`.
    fn kinds(&mut self, at: u32, stem: &str, part: &str) -> Result<()> {
        if self.seen(at, "a parameter's kinds")? {
            return Ok(());
        }
        let mut converters = [0; PARAMETER_KINDS];
        for (slot, converter) in (0..).zip(converters.iter_mut()) {
            *converter = self.pointer(at + CONVERTERS_SLOT + 4 * slot, "a converter")?;
            if *converter != 0 {
                self.routine(*converter, Role::Converter, None, part)?;
            }
        }
        let units = self.pointer(at + UNITS_SLOT, "a parameter's units")?;
        if units != 0 {
            self.units(units, stem, part)?;
        }
        let choices = self.pointer(at + CHOICES_SLOT, "a parameter's choices")?;
        if choices != 0 {
            self.choices(choices, stem, part)?;
        }
        let body = Body::Kinds {
            size: self.word(at)?,
            optional: self.word(at + OPTIONAL_SLOT)?,
            units,
            choices,
            converters,
        };
        self.add(
            at,
            body,
            &format!("scpi_kinds_{stem}"),
            format!("the kinds of {part}"),
        )
    }

    /// A unit table at `at`, named `stem`, of `part`.
    fn units(&mut self, at: u32, stem: &str, part: &str) -> Result<()> {
        if self.seen(at, "a unit table")? {
            return Ok(());
        }
        let mut units = Vec::new();
        for place in 0..UNITS_LIMIT {
            let entry = at + UNIT_SIZE * u32::try_from(place)?;
            let unit = Unit {
                name: self.string_pointer(entry, "a unit's name")?,
                exponent: self.word(entry + UNIT_EXPONENT_SLOT)?,
            };
            units.push(unit);
            if unit.name == 0 {
                self.add(
                    at,
                    Body::Units(units),
                    &format!("scpi_units_{stem}"),
                    format!("the units of {part}"),
                )?;
                return Ok(());
            }
            let name = self.text(unit.name).ok_or_else(|| {
                anyhow::anyhow!(
                    "the unit at {entry:#x} has no name in capitals at {:#x}",
                    unit.name
                )
            })?;
            let spelled: String = name.chars().rev().collect();
            self.add(
                unit.name,
                Body::UnitName(name),
                &format!("scpi_unit_{spelled}"),
                format!("the name of the unit {spelled}, of {part} first"),
            )?;
        }
        bail!("the unit table at {at:#x} does not end within {UNITS_LIMIT} units")
    }

    /// The keyword pair at `at`, named for its spelling, which is
    /// `note`, where it is first reached.
    fn keyword(&mut self, at: u32, note: &str) -> Result<()> {
        let (short, rest) = self
            .image
            .keyword_pair(at)
            .ok_or_else(|| anyhow::anyhow!("no keyword pair at {at:#x}, for {note}"))?;
        let spelled = format!("{short}{}", rest.to_ascii_lowercase());
        self.add(
            at,
            Body::Keyword { short, rest },
            &format!("scpi_keyword_{spelled}"),
            format!("the keyword pair {spelled}, {note} first"),
        )
    }

    /// The NUL-terminated name at `at`: capitals or digits, no longer
    /// than any unit's.
    fn text(&self, at: u32) -> Option<String> {
        let bytes = self.image.0.get(usize::try_from(at).ok()?..)?;
        let length = bytes
            .iter()
            .take(UNIT_NAME_LIMIT + 1)
            .position(|&byte| byte == 0)?;
        let text = &bytes[..length];
        (length > 0
            && text
                .iter()
                .all(|byte| byte.is_ascii_uppercase() || byte.is_ascii_digit()))
        .then(|| String::from_utf8_lossy(text).into_owned())
    }

    /// A choice list at `at`, named `stem`, of `part`.
    fn choices(&mut self, at: u32, stem: &str, part: &str) -> Result<()> {
        if self.seen(at, "a choice list")? {
            return Ok(());
        }
        let keywords = self.pointer(at, "a choice list's keywords")?;
        let values = self.pointer(at + VALUES_SLOT, "a choice list's values")?;
        ensure!(keywords != 0, "the choice list at {at:#x} has no keywords");
        let mut pairs = Vec::new();
        for place in 0..=CHOICES_LIMIT {
            let pair =
                self.string_pointer(keywords + 4 * u32::try_from(place)?, "a choice's keyword")?;
            if pair == 0 {
                break;
            }
            ensure!(
                place < CHOICES_LIMIT,
                "the choice list at {at:#x} does not end within {CHOICES_LIMIT} keywords"
            );
            self.keyword(pair, &format!("a choice of {part}"))?;
            pairs.push(pair);
        }
        if values != 0 {
            let values_body = Body::ChoiceValues(
                (0..pairs.len())
                    .map(|place| self.word(values + 2 * u32::try_from(place)?))
                    .collect::<Result<_>>()?,
            );
            self.add(
                values,
                values_body,
                &format!("scpi_choice_values_{stem}"),
                format!("the values of the choices of {part}"),
            )?;
        }
        self.add(
            keywords,
            Body::ChoiceKeywords(pairs),
            &format!("scpi_choice_keywords_{stem}"),
            format!("the keywords of the choices of {part}"),
        )?;
        self.add(
            at,
            Body::Choices { keywords, values },
            &format!("scpi_choices_{stem}"),
            format!("the choices of {part}"),
        )
    }

    /// The response items at `at` of the node of `path`, named `id`.
    fn items(&mut self, at: u32, id: &str, path: &str) -> Result<()> {
        if self.seen(at, "a response item list")? {
            return Ok(());
        }
        let mut items = Vec::new();
        for place in 1..=ITEMS_LIMIT {
            let entry = at + ITEM_SIZE * u32::try_from(place - 1)?;
            let format = self.pointer(entry, "a response item's formatter")?;
            let size = self.word(entry + ITEM_SIZE_SLOT)?;
            let argument = self
                .image
                .u32(entry + ITEM_ARGUMENT_SLOT)
                .ok_or_else(|| anyhow::anyhow!("the item at {entry:#x} runs past the image"))?;
            items.push(Item {
                format,
                size,
                argument,
            });
            if format == 0 {
                self.add(
                    at,
                    Body::Items(items),
                    &format!("scpi_items_{id}"),
                    format!("the response items of {path}?"),
                )?;
                return Ok(());
            }
            let part = format!("response item {place} of {path}?");
            self.routine(format, Role::Formatter, None, &part)?;
            if argument != 0 {
                self.pointer(entry + ITEM_ARGUMENT_SLOT, "a response item's choices")?;
                self.choices(argument, &format!("response_{id}_{place}"), &part)?;
            }
        }
        bail!("the response items at {at:#x} do not end within {ITEMS_LIMIT}")
    }
}

#[cfg(test)]
mod tests {
    use super::Records;
    use super::Tree;
    use super::identifier;
    use crate::dump_scpi::tests::IMAGES;

    /// The kinds of record docs/scpi/README.md's table counts, in its
    /// columns' order, by `Body::kind`'s description.
    const COLUMNS: [&str; 8] = [
        "a node",
        "a child list",
        "a keyword pair",
        "a parameter",
        "a parameter's kinds",
        "a response item list",
        "a choice list",
        "a unit table",
    ];

    /// docs/scpi/README.md's row for one image's tree: its records in
    /// all, those of each of `COLUMNS`, and the routines they point at.
    fn row(image: &str, tree: Tree, records: &Records) -> String {
        let counts = records.counts();
        let columns: Vec<String> = COLUMNS
            .iter()
            .map(|kind| counts.get(kind).copied().unwrap_or(0).to_string())
            .collect();
        format!(
            "| `{image}` | {} | {} | {} | {} |",
            match tree {
                Tree::Primary => "primary",
                Tree::Installer => "installer",
            },
            records.records.len(),
            columns.join(" | "),
            records.routines.len()
        )
    }

    /// Every image's two trees read, no record overlapping another, and
    /// counted as docs/scpi/README.md, "As C", tabulates them.
    #[test]
    fn every_image_gives_the_records_its_table_counts() {
        let root = format!("{}/../..", env!("CARGO_MANIFEST_DIR"));
        let held = std::fs::read_to_string(format!("{root}/docs/scpi/README.md"))
            .expect("the document is in the repository");
        for name in IMAGES {
            let image = std::fs::read(format!("{root}/third_party/firmware/{name}.bin"))
                .expect("the image is in the repository");
            for tree in [Tree::Primary, Tree::Installer] {
                let records = Records::read(&image, tree, None).expect("the image has the tree");
                let mut end = 0;
                for (at, record) in &records.records {
                    assert!(
                        *at >= end,
                        "{name} {tree:?}: {} overlaps the record before",
                        record.name
                    );
                    end = at + record.body.size();
                }
                let row = row(name, tree, &records);
                assert!(
                    held.lines().any(|line| line == row),
                    "not in the table: {row}"
                );
            }
        }
    }

    #[test]
    fn a_path_is_spelled_as_setup_names_its_handlers() {
        assert_eq!(identifier("*IDN"), "common_IDN");
        assert_eq!(identifier(":SYSTem:LANGuage"), "SYSTem_LANGuage");
    }
}
