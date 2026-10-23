//! `dump-scpi`: every command path a firmware image's SCPI parser
//! knows, read out of its own tables (docs/scpi/README.md).
//!
//! Pointers in the image are absolute and it is not relocated, so a
//! stored pointer is a file offset as it stands.  Nothing here knows an
//! address: the tables are found by their shape, and an image whose
//! tables are not found exactly once is refused rather than guessed at.

use std::cmp::Reverse;
use std::collections::BTreeMap;
use std::collections::BTreeSet;
use std::path::PathBuf;

use anyhow::Context as _;
use anyhow::Result;
use anyhow::bail;
use anyhow::ensure;

use crate::image;
use crate::image::Image;

mod c;
mod models;
mod records;

/// Where a node keeps the pointer to its keyword pair.
const KEYWORD_SLOT: u32 = 0;
/// Where a node keeps the pointer to its child list, zero for a leaf.
const CHILDREN_SLOT: u32 = 4;
/// Where a node keeps its setter, zero if it has none.
const SETTER_SLOT: u32 = 8;
/// Where a node keeps its query handler, zero if it has none.
const QUERY_SLOT: u32 = 18;

/// Where a child list keeps how many children follow.
const COUNT_SLOT: u32 = 2;
/// Where a child list's pointers to its children start.
const CHILDREN_START: u32 = 6;
/// No child list in any image holds this many; a larger count is not
/// a child list.
const CHILDREN_LIMIT: u16 = 80;

/// The one character besides letters and digits a keyword may hold:
/// the HP 53131A-family counters, whose firmware shares this parser,
/// list a common command `_TRG` beside `*TRG`.
const KEYWORD_PUNCTUATION: u8 = b'_';

/// The longest half of a keyword pair in any image.
const KEYWORD_LIMIT: usize = 12;

/// A handler that does nothing but refuse, as these images compile one:
/// `move.l (12,sp),-(sp)`, `move.b #n,-(sp)`, `jsr` to the routine
/// every such handler calls, `addq.l #6,sp`, `rts`.  `None` stands for
/// the byte `n` and the four of the routine's address, which vary.
const REFUSAL: [Option<u8>; 18] = [
    Some(0x2f),
    Some(0x2f),
    Some(0x00),
    Some(0x0c),
    Some(0x1f),
    Some(0x3c),
    Some(0x00),
    None,
    Some(0x4e),
    Some(0xb9),
    None,
    None,
    None,
    None,
    Some(0x5c),
    Some(0x8f),
    Some(0x4e),
    Some(0x75),
];

/// Where in a `REFUSAL` stub the routine's address is.
const REFUSAL_TARGET: u32 = 10;
/// `rts`.
const RTS: u16 = 0x4e75;
/// `jsr` to an absolute long address.
const JSR_ABSOLUTE: u16 = 0x4eb9;
/// `jsr` relative to the program counter.
const JSR_RELATIVE: u16 = 0x4eba;
/// How far `calls` looks for the end of a routine.
const CALLS_LIMIT: u32 = 64;

/// A keyword the common-command list holds and the main tree does not,
/// which tells the two root lists apart.
const COMMON_MARKER: &str = "IDN";

/// What the SCPI walk reads from an image beyond words and longs.
impl Image<'_> {
    /// The routine the handler at `at` passes its refusal to, if the
    /// handler is the `REFUSAL` stub.
    fn refusal_target(&self, at: u32) -> Option<u32> {
        let start = usize::try_from(at).ok()?;
        let code = self.0.get(start..start + REFUSAL.len())?;
        code.iter()
            .zip(REFUSAL)
            .all(|(byte, wanted)| wanted.is_none_or(|wanted| *byte == wanted))
            .then(|| self.u32(at + REFUSAL_TARGET))
            .flatten()
    }

    /// The routines the code at `at` calls, in order, up to its first
    /// `rts`, and how many bytes that is; `None` if no `rts` comes
    /// within `CALLS_LIMIT` bytes.  Calls are `jsr` to an absolute long
    /// address or relative to the program counter.
    fn calls(&self, at: u32) -> Option<(Vec<u32>, u32)> {
        let mut calls = Vec::new();
        let mut offset = 0;
        while offset < CALLS_LIMIT {
            match self.u16(at + offset)? {
                RTS => return Some((calls, offset + 2)),
                JSR_ABSOLUTE => {
                    calls.push(self.u32(at + offset + 2)?);
                    offset += 6;
                }
                JSR_RELATIVE => {
                    let base = at + offset + 2;
                    let displacement = i16::from_be_bytes(self.u16(base)?.to_be_bytes());
                    calls.push(base.checked_add_signed(i32::from(displacement))?);
                    offset += 4;
                }
                _ => offset += 2,
            }
        }
        None
    }

    /// The keyword pair at `at`: the short form, a NUL, the rest of the
    /// long form and a NUL, both upper case, digits or
    /// `KEYWORD_PUNCTUATION`, as they stand.
    fn keyword_pair(&self, at: u32) -> Option<(String, String)> {
        let half = |from: usize, least: usize| {
            let bytes = self.0.get(from..)?;
            let length = bytes
                .iter()
                .take(KEYWORD_LIMIT + 1)
                .position(|&byte| byte == 0)?;
            let text = &bytes[..length];
            (length >= least
                && text.iter().all(|&byte| {
                    byte.is_ascii_uppercase()
                        || byte.is_ascii_digit()
                        || byte == KEYWORD_PUNCTUATION
                }))
            .then(|| String::from_utf8_lossy(text).into_owned())
        };
        let at = usize::try_from(at).ok()?;
        let short = half(at, 1)?;
        let rest = half(at + short.len() + 1, 0)?;
        Some((short, rest))
    }

    /// The node at `at`, if its keyword slot points at a keyword pair.
    fn node(&self, at: u32) -> Option<Node> {
        let (short, rest) = self.keyword_pair(self.u32(at + KEYWORD_SLOT)?)?;
        Some(Node {
            short,
            rest,
            children: self.u32(at + CHILDREN_SLOT)?,
            setter: self.u32(at + SETTER_SLOT)?,
            query: self.u32(at + QUERY_SLOT)?,
        })
    }

    /// The nodes the child list at `at` points to, if it is one: a
    /// count in range and that many pointers, each to a node.
    fn children(&self, at: u32) -> Option<Vec<(u32, Node)>> {
        let count = self.u16(at.checked_add(COUNT_SLOT)?)?;
        if count == 0 || count >= CHILDREN_LIMIT {
            return None;
        }
        (0..u32::from(count))
            .map(|index| {
                let pointer = self.u32(at + CHILDREN_START + 4 * index)?;
                Some((pointer, self.node(pointer)?))
            })
            .collect()
    }
}

/// One node of the tree, as its record holds it.
struct Node {
    /// Its keyword pair's short form, as it stands.
    short: String,
    /// Its keyword pair's rest of the long form, upper case as it
    /// stands; empty in a whole keyword.
    rest: String,
    /// Its child list, zero if it is a leaf.
    children: u32,
    /// Its setter, zero if it has none.
    setter: u32,
    /// Its query handler, zero if it has none.
    query: u32,
}

impl Node {
    /// Its keyword, long form, the rest lower case, as the manuals
    /// write it.
    fn keyword(&self) -> String {
        self.short.clone() + &self.rest.to_ascii_lowercase()
    }

    /// Its keyword as its stem and the numeric suffix it ends in, if it
    /// is a whole keyword: one with an empty rest.
    fn whole(&self) -> Option<(&str, &str)> {
        self.rest.is_empty().then(|| {
            let stem = self.short.trim_end_matches(|c: char| c.is_ascii_digit());
            (stem, &self.short[stem.len()..])
        })
    }

    /// The keyword `self` and `long` spell together, if they read as
    /// one keyword's short and long forms (docs/scpi/README.md, "Alias
    /// nodes"): both whole, with the same numeric suffix or none,
    /// `self`'s stem a strict prefix of `long`'s.  The short form is
    /// upper case, the rest of the long form lower, the suffix after:
    /// `CALCulate1`.
    fn alias_keyword(&self, long: &Node) -> Option<String> {
        let (short_stem, short_suffix) = self.whole()?;
        let (long_stem, long_suffix) = long.whole()?;
        let rest = long_stem.strip_prefix(short_stem)?;
        (!rest.is_empty() && short_suffix == long_suffix)
            .then(|| format!("{short_stem}{}{short_suffix}", rest.to_ascii_lowercase()))
    }
}

/// Where a tree's two root lists are: the common commands, `*IDN?` and
/// its kind, and the main tree.  The parser keeps a pointer to each,
/// side by side.
#[derive(Debug, Clone, Copy)]
struct Roots {
    /// Where the pair of pointers is.
    pair: u32,
    /// The common commands' list.
    common: u32,
    /// The main tree's root list.
    main: u32,
}

/// The one pair of root lists at or above `from`, an even address.
fn roots(image: &Image, from: u32) -> Result<Roots> {
    let common = |list: u32| {
        image.children(list).is_some_and(|children| {
            children
                .iter()
                .any(|(_, node)| node.keyword() == COMMON_MARKER)
        })
    };
    let found: Vec<Roots> = (from..)
        .step_by(2)
        .map_while(|at| {
            Some(Roots {
                pair: at,
                common: image.u32(at)?,
                main: image.u32(at + 4)?,
            })
        })
        .filter(|roots| {
            common(roots.common) && !common(roots.main) && image.children(roots.main).is_some()
        })
        .collect();
    match found.as_slice() {
        [one] => Ok(*one),
        [] => bail!("no pair of root lists found: not a SmartClock image, or not one this reads"),
        many => bail!(
            "{} candidate pairs of root lists found; refusing to guess",
            many.len()
        ),
    }
}

/// One path of the tree, and the node it reaches.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct Entry {
    /// The path, `*` or `:` first, keywords long form.
    pub(crate) path: String,
    /// Where the node is in the image; a node under a list several
    /// parents share is reached by several paths.
    pub(crate) node: u32,
    /// The node's query handler, if it has one.
    pub(crate) query: Option<u32>,
    /// The node's setter, if it has one.
    pub(crate) setter: Option<u32>,
    /// Whether it has a handler and every one it has only refuses: a
    /// path the parser knows and the firmware does not implement.
    pub(crate) refuses: bool,
}

impl Entry {
    /// The path with `?` where the node has a query handler and
    /// ` (set)` where it has a setter.
    fn line(&self) -> String {
        format!(
            "{}{}{}",
            self.path,
            if self.query.is_some() { "?" } else { "" },
            if self.setter.is_some() { " (set)" } else { "" }
        )
    }

    /// The path, then its query handler's and its setter's addresses,
    /// `-` for one it lacks: input for naming the handlers in a
    /// disassembler.
    fn handler_line(&self) -> String {
        let address =
            |handler: Option<u32>| handler.map_or("-".to_owned(), |at| format!("{at:#x}"));
        format!(
            "{} {} {}",
            self.path,
            address(self.query),
            address(self.setter)
        )
    }
}

/// What a walk does with alias nodes (docs/scpi/README.md, "Alias
/// nodes").
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Aliases {
    /// Keep each node of a pair, as the image holds them.
    Keep,
    /// Merge a pair into one node, its keyword in mixed case.
    Merge,
}

/// What a walk of a tree finds.
#[derive(Debug, Default)]
struct Listing {
    /// Every path, the node it reaches and its handlers.
    entries: Vec<Entry>,
    /// Each pair of nodes whose keywords read as one keyword's short
    /// and long forms but which lead to different subtrees or
    /// handlers, so are listed apart: a line for a reader.
    notes: Vec<String>,
}

/// A walk of the tree: the image, what it does with alias nodes, where
/// it is, and what it has found.
struct Walk<'a, 'b> {
    /// The image the tree is in.
    image: &'a Image<'b>,
    /// What it does with alias nodes.
    aliases: Aliases,
    /// The lists the walk is inside, to refuse a cycle.
    above: Vec<u32>,
    /// What it has found.
    found: Listing,
}

/// A path relative to a subtree, and its node's query handler and
/// setter: what two subtrees must share to be equal.
type Shape = (String, Option<u32>, Option<u32>);

impl Walk<'_, '_> {
    /// A walk of `image` that has found nothing yet, inside `above`.
    fn new<'a, 'b>(image: &'a Image<'b>, aliases: Aliases, above: Vec<u32>) -> Walk<'a, 'b> {
        Walk {
            image,
            aliases,
            above,
            found: Listing::default(),
        }
    }

    /// Every path the tree under `list` holds, prefixed by `prefix`.
    fn list(&mut self, list: u32, prefix: &str) -> Result<()> {
        ensure!(
            !self.above.contains(&list),
            "the tree loops back to the child list at {list:#x}"
        );
        let children = self
            .image
            .children(list)
            .with_context(|| format!("no child list at {list:#x}"))?;
        self.above.push(list);
        let merges = match self.aliases {
            Aliases::Keep => BTreeMap::new(),
            Aliases::Merge => self.merges(&children, prefix)?,
        };
        let merged: BTreeSet<usize> = merges.values().map(|(long, _)| *long).collect();
        for (index, (at, node)) in children.iter().enumerate() {
            if merged.contains(&index) {
                continue;
            }
            let keyword = merges
                .get(&index)
                .map_or_else(|| node.keyword(), |(_, keyword)| keyword.clone());
            let path = format!("{prefix}{keyword}");
            for (slot, handler) in [("setter", node.setter), ("query", node.query)] {
                ensure!(
                    handler == 0 || (handler % 2 == 0 && self.image.holds(handler)),
                    "{path} (node at {at:#x}) has {slot} {handler:#x}, neither zero nor code"
                );
            }
            if node.children != 0 {
                self.list(node.children, &format!("{path}:"))?;
            }
            self.found.entries.push(Entry {
                path,
                node: *at,
                query: (node.query != 0).then_some(node.query),
                setter: (node.setter != 0).then_some(node.setter),
                refuses: false,
            });
        }
        self.above.pop();
        Ok(())
    }

    /// The alias pairs among `children`, under `prefix`, by the short
    /// form's index: the long form's index and the keyword they spell
    /// together.  A node takes part in one pair at most, the longest
    /// long form first and for it the longest short form.  A pair that
    /// differs, of nodes in no pair, is noted.
    fn merges(
        &mut self,
        children: &[(u32, Node)],
        prefix: &str,
    ) -> Result<BTreeMap<usize, (usize, String)>> {
        let mut pairs: Vec<(usize, usize, String)> = Vec::new();
        for (short, (_, short_node)) in children.iter().enumerate() {
            for (long, (_, long_node)) in children.iter().enumerate() {
                if let Some(keyword) = short_node.alias_keyword(long_node) {
                    pairs.push((short, long, keyword));
                }
            }
        }
        pairs.sort_by_key(|(short, long, _)| {
            Reverse((
                children[*long].1.short.len(),
                children[*short].1.short.len(),
            ))
        });
        let mut merges = BTreeMap::new();
        let mut taken = BTreeSet::new();
        let mut differing = Vec::new();
        for (short, long, keyword) in pairs {
            match self.difference(&children[short].1, &children[long].1)? {
                None if !taken.contains(&short) && !taken.contains(&long) => {
                    taken.extend([short, long]);
                    merges.insert(short, (long, keyword));
                }
                None => {}
                Some(what) => differing.push((short, long, what)),
            }
        }
        for (short, long, what) in differing {
            if !taken.contains(&short) && !taken.contains(&long) {
                let (short_at, short_node) = &children[short];
                let (long_at, long_node) = &children[long];
                self.found.notes.push(format!(
                    "{prefix}{} (node at {short_at:#x}) and {prefix}{} (node at {long_at:#x}) \
                     read as one keyword but differ in their {what}; listed apart",
                    short_node.keyword(),
                    long_node.keyword()
                ));
            }
        }
        Ok(merges)
    }

    /// What `short` and `long` differ in, `None` if they have the same
    /// handlers and lead to the same subtree, by one child list or by
    /// equal ones.
    fn difference(&self, short: &Node, long: &Node) -> Result<Option<&'static str>> {
        if (short.setter, short.query) != (long.setter, long.query) {
            return Ok(Some("handlers"));
        }
        if short.children == long.children {
            return Ok(None);
        }
        if short.children == 0 || long.children == 0 {
            return Ok(Some("children"));
        }
        let shape = |list: u32| -> Result<Vec<Shape>> {
            let mut walk = Walk::new(self.image, self.aliases, self.above.clone());
            walk.list(list, "")?;
            Ok(walk
                .found
                .entries
                .into_iter()
                .map(|entry| (entry.path, entry.query, entry.setter))
                .collect())
        };
        Ok((shape(short.children)? != shape(long.children)?).then_some("children"))
    }
}

/// Mark each entry whose every handler only refuses.
///
/// The image's refusal routine is the one its `REFUSAL` stubs call, if
/// they all call one.  A handler refuses if it is such a stub, or if it
/// is no longer than the routine and makes exactly the routine's calls
/// in the same order: the routine written out in place, as some
/// setters are.
fn mark_refusals(image: &Image, entries: &mut [Entry]) {
    let handlers = || {
        entries
            .iter()
            .flat_map(|entry| [entry.query, entry.setter])
            .flatten()
    };
    let targets: BTreeSet<u32> = handlers()
        .filter_map(|handler| image.refusal_target(handler))
        .collect();
    let routine = match targets.into_iter().collect::<Vec<_>>().as_slice() {
        [one] => image.calls(*one),
        _ => None,
    };
    let refuses = |handler: u32| {
        image.refusal_target(handler).is_some()
            || routine.as_ref().is_some_and(|(calls, length)| {
                image
                    .calls(handler)
                    .is_some_and(|(made, made_length)| made == *calls && made_length <= *length)
            })
    };
    for entry in entries {
        entry.refuses = (entry.query.is_some() || entry.setter.is_some())
            && [entry.query, entry.setter]
                .into_iter()
                .flatten()
                .all(refuses);
    }
}

/// Every path the image's parser knows, each alias node kept, the
/// common commands as `*` and the rest as `:`, ordered as a case-blind
/// sort of the paths.
fn entries(bytes: &[u8]) -> Result<Vec<Entry>> {
    Ok(listing(bytes, Aliases::Keep)?.entries)
}

/// Every path the image's parser knows, with alias nodes as `aliases`
/// says, the common commands as `*` and the rest as `:`, ordered as a
/// case-blind sort of the paths, and the walk's notes.
fn listing(bytes: &[u8], aliases: Aliases) -> Result<Listing> {
    let image = Image(bytes);
    let mut listing = tree_listing(&image, roots(&image, 0)?, aliases)?;
    mark_refusals(&image, &mut listing.entries);
    Ok(listing)
}

/// Every path the tree under `roots` holds, each alias node kept,
/// ordered as a case-blind sort of the paths, none marked as refusing.
fn tree_entries(image: &Image, roots: Roots) -> Result<Vec<Entry>> {
    Ok(tree_listing(image, roots, Aliases::Keep)?.entries)
}

/// Every path the tree under `roots` holds, with alias nodes as
/// `aliases` says, ordered as a case-blind sort of the paths, none
/// marked as refusing, and the walk's notes.
fn tree_listing(image: &Image, roots: Roots, aliases: Aliases) -> Result<Listing> {
    let mut walk = Walk::new(image, aliases, Vec::new());
    walk.list(roots.common, "*")?;
    walk.list(roots.main, ":")?;
    let mut listing = walk.found;
    listing
        .entries
        .sort_by_cached_key(|entry| entry.path.to_ascii_lowercase());
    listing
        .entries
        .dedup_by(|later, earlier| later.path == earlier.path);
    Ok(listing)
}

/// `dump-scpi`'s arguments.
#[derive(Debug, clap::Args)]
pub(crate) struct DumpScpiArgs {
    /// Full 512 KiB images, as `read-flash` writes or `join-chips`
    /// makes: one, or with `--models` any number.
    #[arg(required = true)]
    images: Vec<PathBuf>,
    /// Print the trees side by side against the manuals and the
    /// command table, as Markdown (docs/scpi/models.md).
    #[arg(long)]
    models: bool,
    /// With `--models`, print a page that filters the table instead
    /// of Markdown (docs/scpi/models.html).
    #[arg(long, requires = "models")]
    html: bool,
    /// Follow each path with its query handler's and its setter's
    /// addresses, `-` for one it lacks, to name them in a
    /// disassembler (tools/ghidra/).
    #[arg(long, conflicts_with = "models")]
    handlers: bool,
    /// Print every record of the tree as C data, for an image folder's
    /// `rewritten/` (docs/scpi/README.md, "As C").
    #[arg(long, conflicts_with_all = ["models", "handlers", "names"])]
    c: bool,
    /// Print a label and type row for every record of the tree, in the
    /// names-file format, for tools/ghidra/SmartClockSetup.java.
    #[arg(long, conflicts_with_all = ["models", "handlers"])]
    names: bool,
    /// With `--c` or `--names`, a names file
    /// (`third_party/firmware/IMAGE/names.tsv`) whose name for an
    /// address replaces the one made from the path, and names the
    /// routines the tree points at; with `--names`, rows for the
    /// addresses it names are left out.
    #[arg(long)]
    names_file: Option<PathBuf>,
    /// With `--c` or `--names`, the installer's tree, which the boot
    /// code unpacks into RAM, in place of the primary's.
    #[arg(long)]
    installer: bool,
}

/// Print one image's paths, one a line, with `handlers` each followed
/// by its handlers' addresses; with `models`, the trees of all of them
/// side by side, as Markdown or with `html` as a page; with `c` or
/// `names`, the tree's records as C or as names-file rows.
pub(crate) fn run(args: &DumpScpiArgs) -> Result<()> {
    ensure!(
        args.c || args.names || (args.names_file.is_none() && !args.installer),
        "--names-file and --installer go with --c or --names"
    );
    if args.models {
        let trees = image::load_all(&args.images, entries)?;
        if args.html {
            print!("{}", models::html(&trees));
        } else {
            print!("{}", models::markdown(&trees));
        }
        return Ok(());
    }
    let [image] = args.images.as_slice() else {
        bail!("give one image, or --models and any number");
    };
    if args.c || args.names {
        let names = args
            .names_file
            .as_deref()
            .map(|path| {
                std::fs::read_to_string(path)
                    .with_context(|| format!("reading {}", path.display()))
                    .and_then(|text| records::NamesFile::parse(&text))
                    .with_context(|| format!("reading {}", path.display()))
            })
            .transpose()?;
        let tree = if args.installer {
            records::Tree::Installer
        } else {
            records::Tree::Primary
        };
        let source = c::Source {
            image: image::name(image)?,
            names_file: args
                .names_file
                .as_deref()
                .map(image::file_name)
                .transpose()?,
        };
        let records = image::load(image, |bytes| {
            records::Records::read(bytes, tree, names.as_ref())
        })?;
        if args.c {
            print!("{}", c::c(&records, &source)?);
        } else {
            print!("{}", c::names(&records, &source, names.as_ref()));
        }
        return Ok(());
    }
    if args.handlers {
        for entry in image::load(image, entries)? {
            println!("{}", entry.handler_line());
        }
        return Ok(());
    }
    let listing = image::load(image, |bytes| listing(bytes, Aliases::Merge))?;
    for entry in &listing.entries {
        println!("{}", entry.line());
    }
    for note in &listing.notes {
        eprintln!("{note}");
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use anyhow::Result;
    use smartclock::scpi::spells;

    use super::Aliases;
    use super::Entry;
    use super::entries;
    use super::listing;
    use super::records::Records;
    use super::records::Tree;

    /// The tree as `dump-scpi` prints it, a line a path.
    fn dump(bytes: &[u8]) -> Result<Vec<String>> {
        Ok(listing(bytes, Aliases::Merge)?
            .entries
            .iter()
            .map(Entry::line)
            .collect())
    }

    /// Each image in `third_party/firmware/`, by the name its tree has
    /// in `docs/scpi/`.
    pub(super) const IMAGES: [&str; 7] = [
        "58503a-3633",
        "58503a-3704",
        "58503b-1.01.04",
        "z3801a-3543",
        "z3805a-3543b",
        "z3815a-4010",
        "z3816a-4001",
    ];

    #[test]
    fn every_image_gives_the_tree_its_document_holds() {
        let root = format!("{}/../..", env!("CARGO_MANIFEST_DIR"));
        for name in IMAGES {
            let image = std::fs::read(format!("{root}/third_party/firmware/{name}.bin"))
                .expect("the image is in the repository");
            let held = std::fs::read_to_string(format!("{root}/docs/scpi/{name}.txt"))
                .expect("the tree is in the repository");
            let read = dump(&image).expect("the image has a tree");
            assert_eq!(read, held.lines().collect::<Vec<_>>(), "{name}");
        }
    }

    /// The handlers docs/firmware/interval.md, "In every image", found
    /// by reading the Z3805A 3543B's code, are the ones the tree gives.
    #[test]
    fn handler_lines_give_the_handlers_the_firmware_notes_found() {
        let root = format!("{}/../..", env!("CARGO_MANIFEST_DIR"));
        let image = std::fs::read(format!("{root}/third_party/firmware/z3805a-3543b.bin"))
            .expect("the image is in the repository");
        let lines: Vec<String> = entries(&image)
            .expect("the image has a tree")
            .iter()
            .map(Entry::handler_line)
            .collect();
        for wanted in [
            ":SOURce:SYNChronization:TINTerval 0x300b2 -",
            ":DIAGnostic:PTIMe:TINTerval 0x2b896 -",
        ] {
            assert!(lines.iter().any(|line| line == wanted), "{wanted}");
        }
    }

    /// The image folder's handlers.txt, SmartClockSetup.java's `scpi=`,
    /// is what `make handlers` writes from the image.
    #[test]
    fn the_handlers_file_is_what_the_image_gives() {
        let root = format!("{}/../..", env!("CARGO_MANIFEST_DIR"));
        let image = std::fs::read(format!("{root}/third_party/firmware/z3805a-3543b.bin"))
            .expect("the image is in the repository");
        let held = std::fs::read_to_string(format!(
            "{root}/third_party/firmware/z3805a-3543b/handlers.txt"
        ))
        .expect("the handlers file is in the repository");
        let lines: Vec<String> = entries(&image)
            .expect("the image has a tree")
            .iter()
            .map(Entry::handler_line)
            .collect();
        assert_eq!(
            lines,
            held.lines().collect::<Vec<_>>(),
            "handlers.txt is stale; run make handlers"
        );
    }

    #[test]
    fn the_models_document_is_what_the_images_give() {
        let root = format!("{}/../..", env!("CARGO_MANIFEST_DIR"));
        let trees: Vec<(String, Vec<Entry>)> = IMAGES
            .iter()
            .map(|name| {
                let image = std::fs::read(format!("{root}/third_party/firmware/{name}.bin"))
                    .expect("the image is in the repository");
                (
                    (*name).to_owned(),
                    entries(&image).expect("the image has a tree"),
                )
            })
            .collect();
        let held = std::fs::read_to_string(format!("{root}/docs/scpi/models.md"))
            .expect("the document is in the repository");
        assert_eq!(
            super::models::markdown(&trees).lines().collect::<Vec<_>>(),
            held.lines().collect::<Vec<_>>(),
            "docs/scpi/models.md is stale; run make docs"
        );
        let page = std::fs::read_to_string(format!("{root}/docs/scpi/models.html"))
            .expect("the page is in the repository");
        assert_eq!(
            super::models::html(&trees).lines().collect::<Vec<_>>(),
            page.lines().collect::<Vec<_>>(),
            "docs/scpi/models.html is stale; run make docs"
        );
    }

    #[test]
    fn every_path_the_undocumented_commands_name_is_in_a_tree() {
        let root = format!("{}/../..", env!("CARGO_MANIFEST_DIR"));
        let trees: Vec<Vec<Entry>> = IMAGES
            .iter()
            .map(|name| {
                let image = std::fs::read(format!("{root}/third_party/firmware/{name}.bin"))
                    .expect("the image is in the repository");
                entries(&image).expect("the image has a tree")
            })
            .collect();
        let held = std::fs::read_to_string(format!("{root}/docs/scpi/undocumented.md"))
            .expect("the document is in the repository");
        let headings: Vec<&str> = held
            .lines()
            .filter_map(|line| line.strip_prefix("### `"))
            .filter_map(|rest| rest.split('`').next())
            .collect();
        assert!(!headings.is_empty(), "no path headings found");
        for heading in headings {
            assert!(
                trees
                    .iter()
                    .flatten()
                    .any(|entry| spells(heading, &entry.path)),
                "{heading} is in no image's tree"
            );
        }
    }

    /// A synthetic image: a root pair at 0 naming `lists[0]` as the
    /// common commands' list and `lists[1]` as the main tree's, each
    /// list the indexes in `nodes` of the nodes it holds, and each node
    /// its keyword pair and the index in `lists` of its child list, if
    /// it has one.  Every node has one query handler, and is laid out
    /// far enough from the next for `Records` to read it.
    fn synthetic(nodes: &[(&[u8], Option<u32>)], lists: &[&[u32]]) -> Vec<u8> {
        const KEYWORDS: u32 = 0x100;
        const KEYWORD_STRIDE: u32 = 0x10;
        const NODES: u32 = 0x400;
        const NODE_STRIDE: u32 = 0x40;
        const LISTS: u32 = 0xc00;
        const LIST_STRIDE: u32 = 0x40;
        const HANDLER: u32 = 0x1800;
        const NO_DEFAULT_CHILD: [u8; 2] = [0xff, 0xff];
        let list_at = |index: u32| LISTS + index * LIST_STRIDE;
        let mut image = vec![0u8; 0x2000];
        let mut put = |at: u32, bytes: &[u8]| {
            let at = usize::try_from(at).expect("in range");
            image[at..at + bytes.len()].copy_from_slice(bytes);
        };
        put(0, &list_at(0).to_be_bytes());
        put(4, &list_at(1).to_be_bytes());
        for (index, (pair, children)) in (0..).zip(nodes) {
            let keyword = KEYWORDS + index * KEYWORD_STRIDE;
            let node = NODES + index * NODE_STRIDE;
            put(keyword, pair);
            put(node + super::KEYWORD_SLOT, &keyword.to_be_bytes());
            if let Some(children) = children {
                put(
                    node + super::CHILDREN_SLOT,
                    &list_at(*children).to_be_bytes(),
                );
            }
            put(node + super::QUERY_SLOT, &HANDLER.to_be_bytes());
        }
        for (index, members) in (0..).zip(lists) {
            let list = list_at(index);
            put(
                list + super::COUNT_SLOT,
                &u16::try_from(members.len()).expect("small").to_be_bytes(),
            );
            put(list + super::COUNT_SLOT + 2, &NO_DEFAULT_CHILD);
            for (slot, node) in (0..).zip(*members) {
                put(
                    list + super::CHILDREN_START + 4 * slot,
                    &(NODES + node * NODE_STRIDE).to_be_bytes(),
                );
            }
        }
        image
    }

    /// A minimal image: a common list of `IDN` and `_TRG` and a main
    /// list of `SYSTem`.
    fn tiny_image() -> Vec<u8> {
        synthetic(
            &[
                (b"IDN\0\0", None),
                (b"_TRG\0\0", None),
                (b"SYST\0EM\0", None),
            ],
            &[&[0, 1], &[2]],
        )
    }

    #[test]
    fn a_keyword_may_hold_an_underscore() {
        assert_eq!(
            dump(&tiny_image()).expect("the image has a tree"),
            ["*_TRG?", "*IDN?", ":SYSTem?"]
        );
    }

    /// An image whose main tree holds a keyword pair, `SYSTem`, and
    /// pairs of whole keywords, each one keyword's short and long form
    /// as two nodes: `CALC` and `CALCULATE` with one child list, and so
    /// `CALC1` and `CALCULATE1`; `MEAS` and `MEASURE` with equal ones;
    /// `INP` and `INPUT` with different ones.
    fn alias_image() -> Vec<u8> {
        synthetic(
            &[
                (b"IDN\0\0", None),
                (b"SYST\0EM\0", Some(2)),
                (b"CALC\0\0", Some(3)),
                (b"CALCULATE\0\0", Some(3)),
                (b"INP\0\0", Some(4)),
                (b"INPUT\0\0", Some(5)),
                (b"MEAS\0\0", Some(6)),
                (b"MEASURE\0\0", Some(7)),
                (b"STAT\0E\0", None),
                (b"MATH\0\0", None),
                (b"COUP\0LING\0", None),
                (b"IMP\0EDANCE\0", None),
                (b"VOLT\0AGE\0", None),
                (b"CALC1\0\0", Some(3)),
                (b"CALCULATE1\0\0", Some(3)),
            ],
            &[
                &[0],
                &[1, 2, 3, 4, 5, 6, 7, 13, 14],
                &[8],
                &[9],
                &[10],
                &[11],
                &[12],
                &[12],
            ],
        )
    }

    #[test]
    fn alias_nodes_are_listed_as_one_keyword_when_they_lead_to_one_subtree() {
        let image = alias_image();
        let listing = listing(&image, Aliases::Merge).expect("the image has a tree");
        let lines: Vec<String> = listing.entries.iter().map(Entry::line).collect();
        assert_eq!(
            lines,
            [
                "*IDN?",
                ":CALCulate?",
                ":CALCulate1?",
                ":CALCulate1:MATH?",
                ":CALCulate:MATH?",
                ":INP?",
                ":INP:COUPling?",
                ":INPUT?",
                ":INPUT:IMPedance?",
                ":MEASure?",
                ":MEASure:VOLTage?",
                ":SYSTem?",
                ":SYSTem:STATe?",
            ]
        );
        assert_eq!(
            listing.notes,
            [
                ":INP (node at 0x500) and :INPUT (node at 0x540) read as one keyword \
              but differ in their children; listed apart"
            ]
        );
    }

    #[test]
    fn alias_nodes_are_kept_for_the_handlers_and_the_records() {
        let image = alias_image();
        let paths: Vec<String> = entries(&image)
            .expect("the image has a tree")
            .into_iter()
            .map(|entry| entry.path)
            .collect();
        for path in [":CALC:MATH", ":CALCULATE:MATH", ":MEAS", ":MEASURE"] {
            assert!(paths.iter().any(|held| held == path), "{path}");
        }
        let records = Records::read(&image, Tree::Primary, None).expect("the image has records");
        assert_eq!(records.counts().get("a node"), Some(&15));
    }

    #[test]
    fn an_image_without_a_tree_is_refused() {
        assert!(dump(&[0; 0x1000]).is_err());
    }
}
