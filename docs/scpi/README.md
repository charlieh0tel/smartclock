# SCPI trees

Every command path each firmware image's parser knows, one file per
image in `third_party/firmware/`, as `smartclock-cli dump-scpi IMAGE`
prints it.  A test regenerates each and fails if it differs from the
file here, so the files and the images cannot drift apart.

`dump-scpi --handlers IMAGE` follows each path with its query
handler's and its setter's addresses, `-` for one it lacks, to name
them in a disassembler (`docs/firmware/README.md`, "Decompiling with
Ghidra").  A test checks two against the handlers the firmware notes
found by reading the code.

A path here means the parser knows it, not that a receiver answers it.
Nothing here was sent to a receiver.

[`models.html`](https://htmlpreview.github.io/?https://github.com/charlieh0tel/smartclockmon/blob/main/docs/scpi/models.html)
is the same table as a page that filters it -- by text, by whether a
manual lists a path, whether the images differ, whether it has a
handler, and by image; `dump-scpi --models --html` writes it, and the
same test checks it.

[`models.md`](models.md) sets the trees side by side, path by path,
with the manuals that list each path and whether a receiver has
answered it; `smartclock-cli dump-scpi --models` writes it from the
images, the `manual-*.txt` lists beside it and the command table, and
a test checks it as it does the trees.  `make docs` regenerates all of
them.  It also lists the paths in no manual whose node has the same
handlers as a documented one, and the stubs: paths whose every handler
only refuses, which a receiver answers with -113, "Undefined header"
(`undocumented.md`).

Each `manual-*.txt` is the command paths a manual writes from the
root, one `LANGUAGE PATH` a line, PRIMARY or INSTALL, with a comment
saying how they were drawn from it.  A model counts against its own
manual: 097-59551-02 for the 58503A, 097-58503-13 for the 58503B,
097-z3801-01 for the Z3801A.  The Z3805A has none of its own and is
counted against the Z3801A's while its tree is the same, with a
footnote saying so; none here is the Z3815A's or Z3816A's.

## Format

One path a line, sorted without regard to case.  Common commands start
`*`, the rest `:`.  `?` marks a node with a query handler, `(set)` one
with a setter.  A node with neither is a branch the parser walks
through, or a keyword with no handler of its own (`:DIAGnostic:SLOG`,
which a 58503A answers as a query nonetheless).

`:SOURce` is an optional header (`z3801a.md`): a path under it
also answers without it, though only the form with it is listed.

## How it is read

Pointers in the image are absolute and it is not relocated, so a
stored pointer is a file offset as it stands.

A **node** is a record:

    +0   u32   pointer to the keyword pair
    +4   u32   pointer to the child list, zero for a leaf
    +8   u32   setter, zero if none
    +18  u32   query handler, zero if none

A **keyword pair** is the short form, a NUL, the rest of the long form
and a NUL, both upper case: `SYST\0EM\0` is `SYSTem`.  A keyword may
also hold digits and `_`; none of these images uses `_`, but the HP
53131A-family counters, whose firmware shares this parser, list a
common command `_TRG`, and `dump-scpi` reads their trees too.

A **child list** is:

    +0   u16        an id
    +2   u16        how many children
    +4   u16        flags
    +6   u32 x n    pointers to the child nodes

The parser keeps two pointers side by side: one to the common-command
list (`*CLS` to `*WAI`) and one to the main tree.  `dump-scpi` finds
that pair by shape -- two child lists, the first holding `IDN` -- and
refuses an image where it is not found exactly once.  It follows every
parent's pointer, since several parents share one list (`SER`, `SER1`,
`SERIAL` and the rest under `:DIAGnostic` and `:SYSTem:COMMunicate`),
and refuses a children slot holding anything but zero or a child list,
and a handler slot holding anything but zero or an even address in the
image.

| Image | Paths | Query | Set | Root pair at |
| ----- | ----- | ----- | --- | ------------ |
| [Z3801A 3543](z3801a-3543.txt) | 595 | 414 | 303 | `0x5cfae` |
| [Z3805A 3543B](z3805a-3543b.txt) | 595 | 414 | 303 | `0x5d014` |
| [Z3816A 4001](z3816a-4001.txt) | 902 | 638 | 507 | `0x64ae8` |
| [58503A 3633](58503a-3633.txt) | 914 | 639 | 519 | `0x60d48` |
| [58503A 3704](58503a-3704.txt) | 931 | 646 | 533 | `0x61218` |
| [Z3815A 4010](z3815a-4010.txt) | 965 | 680 | 552 | `0x6cd16` |
| [58503B 1.01.04](58503b-1.01.04.txt) | 831 | 525 | 477 | `0x68316` |

## As C

`dump-scpi --c IMAGE` prints a tree as C data, every record its roots
reach, each a `const` object of the type
`tools/ghidra/types/smartclock.h` declares, for
`third_party/firmware/rewritten/` (PLAN.md, "Decided 2026-10-22");
`--names` prints a label and type row for each, in the names-file
format, for `SmartClockSetup.java`'s `names=`.  `--installer` reads the
installer's tree instead, unpacked into RAM from the boot code's
records as `tools/firmware/decompiled_check.py` unpacks it: the one
stream of copy records below `0x20000`, the flash the installer lives
in (`docs/firmware/restart.md`).  `--names-file` takes a names file:
its name for an address replaces the one made, a routine is named only
by it or, for a handler, by the rule below, and with `--names` an
address it names gets no row, so that its own row, and its type, stand.

`make scpi-tree` writes the Z3805A 3543B's,
`third_party/firmware/rewritten/z3805a-3543b/scpi_tree_data.c` through
clang-format and `tools/ghidra/names/z3805a-3543b.scpi-tree.tsv`, and a
test fails while either is stale, the C compared without its white
space.  The exporter includes it with the hand-written C and drops the
`.set` of each global it defines, so that the firmware's C, the
parser's routines among them, reads the tree it defines, not ROM's
(`docs/firmware/README.md`, "Hand-written C"); the function check
proves them on it.  `make check-scpi-tree` compiles it alone with the
Makefile's m68k flags and compares every object with the image: each
byte, and each pointer by the address of what it names, the records'
from the rows, the routines' from Ghidra's names in the decompiled C's
`.conventions.jsonl`, so that a handler named otherwise than Ghidra
names it differs; and it fails an object but a string at an odd
address, and a list's children that do not follow the list.  All 2055
of the Z3805A's, its 1933 records and its 122 lists' children, are the
image's.

The C defines a record's pointer to another record with a cast to the
field's type, since the records are `const` and smartclock.h's fields
are not: the exported C, which compiles it, has Ghidra's types, which
keep no `const`.  A converter and a formatter are cast too, their
prototypes not their field's own; a handler is not, every one of them
taking `scpi_handler_function`'s parameters, as `SmartClockSetup.java`
types them.  A child list's children are an array of their own,
`NAME_children`, which follows the list, as in the image: Ghidra
exports a flexible array member as one of no elements, which C cannot
initialize.  And each record but a string is defined with
`SCPI_TREE_DATA_EVEN`, at an even address as the image's are, since
smartclock.h's types are packed and a word or a long at an odd address
is misaligned (MC68331UM, 4.4.4).

Beyond the node and the child list above, the records are, at the
offsets smartclock.h gives:

- A node is `0x32` bytes, `scpi_node`: its keyword pair, its child
  list, a setter and a query entry of 10 bytes each, `scpi_handler` --
  the routine, the first parameter and the bytes the parameters take
  -- the query's response items and their bytes, the word it adds to
  the path, -1 for none, and 14 bytes `unused`.  Every node of every
  image is followed by those 14 zero bytes before the next record,
  whatever its kind (the Z3805A's 459: 305 item lists, 153 nodes and
  the tree pair), so they are the node's; no routine reads them (no
  displacement from `0x24` to `0x31` off a node in the parser,
  `0x25d00` to `0x2a200`, and no reference to one in Ghidra), and
  `dump-scpi` refuses a node where one is not zero.
- A parameter, `scpi_parameter`, is the next one and its kinds; the
  kinds, `scpi_parameter_kinds`, `0x24` bytes, the value's size, whether
  it may be left out, its unit table, its choice list and six
  converters, `converters[6]`, one for each parameter kind,
  `SCPI_PARAMETER_DECIMAL` to `_EXPRESSION`, by which the parser
  indexes them (`scpi_parameter_match`, `0x278f4`).  The kinds lie
  `0x24` bytes apart, as six make them.
- A unit table, `scpi_unit`, 6 bytes each, runs to a zero name; a
  unit's name is its letters reversed.
- A choice list, `scpi_choices`, points at its keyword pairs' pointers,
  to a NULL, and at a word for each, or NULL for its index.
- A response item list, `scpi_response_item`, 10 bytes each, runs to a
  zero formatter; its third field, `choices`, the formatter's third
  argument, is the node's choice list for `scpi_format_choice`, the one
  formatter that reads it, and NULL for every other.

`dump-scpi` refuses an image where any of these does not hold
together: a pointer neither zero nor in the image (even, but for a
string's), a list that does not end within the most any image holds, a
keyword pair or a unit's name that is not one, a record read as two
kinds, or two records of one name.  No two records overlap in any
image.  `--c` also refuses while any routine has no name: without a
names file, the converters and formatters of every image; with the
Z3805A's, none of the primary's, but the installer's six.

| Image | Tree | Records | Nodes | Child lists | Keyword pairs | Parameters | Kinds | Item lists | Choice lists | Unit tables | Routines |
| ----- | ---- | ------- | ----- | ----------- | ------------- | ---------- | ----- | ---------- | ------------ | ----------- | -------- |
| `z3801a-3543` | primary | 1933 | 459 | 122 | 308 | 485 | 189 | 305 | 22 | 4 | 295 |
| `z3801a-3543` | installer | 34 | 9 | 4 | 9 | 3 | 3 | 5 | 0 | 0 | 15 |
| `z3805a-3543b` | primary | 1933 | 459 | 122 | 308 | 485 | 189 | 305 | 22 | 4 | 295 |
| `z3805a-3543b` | installer | 34 | 9 | 4 | 9 | 3 | 3 | 5 | 0 | 0 | 15 |
| `z3816a-4001` | primary | 2015 | 496 | 141 | 297 | 488 | 198 | 324 | 24 | 5 | 313 |
| `z3816a-4001` | installer | 34 | 9 | 4 | 9 | 3 | 3 | 5 | 0 | 0 | 15 |
| `58503a-3633` | primary | 2039 | 508 | 141 | 308 | 488 | 198 | 325 | 24 | 5 | 313 |
| `58503a-3633` | installer | 34 | 9 | 4 | 9 | 3 | 3 | 5 | 0 | 0 | 15 |
| `58503a-3704` | primary | 2095 | 525 | 144 | 320 | 496 | 205 | 332 | 25 | 5 | 327 |
| `58503a-3704` | installer | 34 | 9 | 4 | 9 | 3 | 3 | 5 | 0 | 0 | 15 |
| `z3815a-4010` | primary | 2257 | 559 | 145 | 363 | 534 | 206 | 366 | 30 | 5 | 364 |
| `z3815a-4010` | installer | 34 | 9 | 4 | 9 | 3 | 3 | 5 | 0 | 0 | 15 |
| `58503b-1.01.04` | primary | 1970 | 513 | 144 | 317 | 450 | 174 | 299 | 27 | 3 | 309 |
| `58503b-1.01.04` | installer | 34 | 9 | 4 | 9 | 3 | 3 | 5 | 0 | 0 | 15 |

Records counts every kind: those in the columns, the tree pair, and a
choice list's keywords and values and a unit's name.  A test checks
each row.

Names, with `installer_` before each for the installer's tree; `ID` is
a path as `SmartClockSetup.java` spells it in a handler's name, a
common command's `*` as `common_`, the main tree's leading `:` dropped
and every other `:` an `_` (`*IDN` is `common_IDN`, `:SYSTem:LANGuage`
is `SYSTem_LANGuage`):

| Record | Name |
| ------ | ---- |
| tree pair | `scpi_trees` |
| root lists | `scpi_list_common`, `scpi_list_main` |
| node | `scpi_node_ID` |
| child list of a node | `scpi_list_ID` |
| keyword pair, a node's or a choice's | `scpi_keyword_` and its spelling, `scpi_keyword_LANGuage` |
| parameter N of the setter or the query | `scpi_parameter_set_ID_N`, `scpi_parameter_query_ID_N` |
| its kinds, unit table and choice list | `scpi_kinds_`, `scpi_units_`, `scpi_choices_` and the same stem |
| a choice list's keywords and values | `scpi_choice_keywords_`, `scpi_choice_values_` and its stem |
| a unit's name | `scpi_unit_` and the unit, `scpi_unit_MHZ` |
| a query's response items | `scpi_items_ID` |
| the choice list of response item N | `scpi_choices_response_ID_N` |
| a handler | `scpi_query_ID`, `scpi_set_ID` |

A record reached by several paths, as the lists `SER`, `SER1` and the
rest share, is named for the first in this tree's order, a case-blind
sort; a keyword pair the image shares between nodes, as it shares one
`COUNt` among them all, is one record.  A handler is named as `Setup`
names it from `dump-scpi --handlers`: the names file's function there,
else the first path, query before setter, whose node holds it, which
Ghidra's names for all 271 of the Z3805A's confirm.  The installer's
handlers are no Ghidra functions; their names are the same rule's.

`--names` adds a row a record's type leaves out, since a flexible
array member types none of its elements: at `+6` of every child list,
the main tree's and the common commands' too, `NAME_children`,
`scpi_node *[n]`, the array the C defines there.  Every child list,
`scpi_main_tree` among them, is a `scpi_child_list`.

## Parameters and responses

Each handler converts its parameters and writes its response through a
small library beside the parser (Z3805A 3543B, `0x25d70` to `0x26a06`).
A numeric parameter arrives parsed: a sign word, then its digits' values
from `+2`, the exponent at `+0x12` and its units suffix at `+0x14`.
`scpi_apply_suffix` matches the suffix against the node's unit table,
allowing a two-character multiplier before the unit; then
`scpi_number_to_short`, `_unsigned_short`, `_long`, `_unsigned_long`
or `_double` converts it, the unsigned ones taking the magnitude and
dropping the sign, each answering -11 out of range.  `scpi_parse_boolean`
takes `ON` and `OFF`, `scpi_parse_boolean_number` a number, and
`scpi_parse_choice` a keyword from the node's list.
`scpi_parse_text` (`0x25d4a`), the string converter of 14 parameters'
kinds, takes a quoted string's text pointer, and answers -21, "Invalid
string data", for one holding a NUL, its length not the text's;
`scpi_parse_string` takes the pointer and the length.

A response goes to a writer whose method table puts a character (at
`+0x14`) or a counted string (`+0x18`).  `scpi_format_long` and its kind
write integers with their sign, `scpi_format_string` a quoted string
with each `"` doubled, `scpi_format_block` a definite-length block, and
the arrays their elements separated by commas.  Every real goes through
`scpi_real_to_text`, which writes NR3, `+1.23456E+002`, to so many
significant digits and no digit below a resolution, a power of ten:
-308 keeps every digit, 0 rounds to units.

Under both is a decimal engine of its own (`0x24e18` to `0x2553a`),
taking its arguments in registers.  `scpi_number_to_bcd` packs a parsed
number's 16 digits two to a byte, and `scpi_bcd_to_double` makes them a
64-bit integer, normalizes it and has `scpi_scale_by_power_of_ten`
multiply it by 10 to the exponent less 16, from tables of powers of ten
(`0x256ae`, single steps from 10^-64 to 10^64, and `0x25b36`, steps of
10^64).  `scpi_double_to_decimal` goes the other way, estimating the
exponent from the binary one by log10(2), as `0x4d10` / 65536, scaling
into [0.1, 1) and taking two digits more than asked, to round; it sets
Z on failure, which is all `scpi_real_to_text` reads of its result, and
`scpi_round_decimal` then rounds the record to the digits wanted.  Both
multiply by `scpi_double_multiply`, the engine's own IEEE multiply.
`scpi_decimal_to_text_unused` (`0x24d88`), which would write such a
record as NR3 text, is called by nothing: `scpi_real_to_text` writes its
own.
The two jumps `SmartClockSetup.java` reports as unrecognized switch
shapes, at `0x24ff2` and `0x25140`, are in this engine: each enters an
unrolled run of shifts part way, to shift by a count, and every
instruction it can reach is already disassembled.

The converters, `scpi_real_to_text` and the engine are run on known values by
`tools/firmware/app_check.py`:

    uv run tools/firmware/app_check.py third_party/firmware/z3805a-3543b.bin

## Between images

Counted by path, ignoring handlers.

- **Z3801A 3543 and Z3805A 3543B** have the same tree, line for line.
- **58503A 3633 to 3704** adds 17 and removes none: `:DIAGnostic:TCODe`
  and its error and status masks, `:DIAGnostic:ROSCillator:EFControl:
  DATA` and `:MODE`, `:SOURce:PTIMe:UTC`, and the four-letter tokens
  `DACP`, `EFER`, `ESSD`, `ESSN`, `GDOP`, `RSTG` and `TMD1`.
- **Z3801A 3543 against 58503A 3633**: 534 in common.  Only in the
  Z3801A's: `:LED:TMHValid` and `:SAMPle` with its 48 array and
  value words.  Only in the 58503A's: `:ALARm`, `:SYSTem:PON`, the
  `:DIAGnostic:REFerence`, `:TMODe`, `:TSET` and `:TVALid` branches,
  `:OUTPut:ACTive`, more of `:SYSTem:COMMunicate` and
  `:STATus:OPERation`, and the tokens `ANT1`, `AZEL`, `LEAP`, `MANI`,
  `MATThew`, `PAVG`, `PMD1`, `POS1`, `SIGQ`, `TIMD`, `TIME`, `UNSL`.
- **Z3815A 4010** is nearest the 58503A 3704: it has every path of
  3704 but the five under `:GARY`, and adds 39, 21 of them under
  `:DIAGnostic:ROSCillator`, with `:DIAGnostic:ADC`, `:CALibration`,
  `:TEST`, `:LED:NGPS`, `:LED:STANdby`, `:OUTPut:HPOWer` and
  `:OUTPut:PRIMary`.  Against the Z3816A 4001 it adds 68 and removes
  the same five.
- **58503B 1.01.04** against the 58503A 3704 removes 168 and adds 68.
  Gone: the `R...`/`W...` tokens, `CALA`, `CEQU`, `FMHO`, `IPSU`,
  `:GARY`'s children, the `:OUTPut:PINn` branches, the `SER*` ports
  under `:DIAGnostic`, and much of `:SOURce`, `:STATus` and
  `:SYSTem:COMMunicate`.  New: `:DIAGnostic:DOWNload`, `:ERASe` and
  `:DCOMplete` in the primary's own tree, `:SYSTem:SRESet`,
  `:STATus:AACKnowledge`, `:DIAGnostic:FAIL`, `:DIAGnostic:ROSCillator:
  LTIMe`, `:TEMP` and `CHOE`.  40 of the 68 are among the Z3815A's
  additions too: `:DIAGnostic:ROSCillator:PTESt` and its thresholds,
  `:DIAGnostic:ADC`, `:CALibration`, `:LED:NGPS`, `:OUTPut:HPOWer` and
  `:SOURce:PTIMe:TDATe` among them.

## Against the command table

Each `commands.toml` entry, looked for in the images of its models,
with `:SOURce` optional:

| Command table entries | Image | Found |
| --------------------- | ----- | ----- |
| 58503A/B | 58503A 3633 | 119 of 130 |
| 58503A/B | 58503A 3704 | 120 of 130 |
| 58503A/B | Z3815A 4010 | 120 of 130 |
| 58503A/B | 58503B 1.01.04 | 121 of 130 |
| Z3801A | Z3801A 3543 | 78 of 82 |
| Z3801A | Z3816A 4001 | 79 of 82 |

Missing from every image checked against the 58503A/B entries: the three
`:SYNChronization:HOLDover:DURation:THReshold` entries, whose node is
`:ROSCillator:HOLDover:DURation:MEASurement:THReshold`, and the six
`:SYSTem:COMMunicate:SERial1:...` entries, whose settings sit under
`:SER:RECeive:`; a 58503A answers both table spellings
(`58503a.md`).  `:DIAGnostic:ERASe` is missing from all but the
58503B, and `:DIAGnostic:ROSCillator:EFControl:DATA` from 3633 only.

Missing from the images checked against the Z3801A entries: the two
`:ROSCillator:HOLDover:DURation:THReshold` entries, whose node has
`MEASurement` between (`z3801a.md`), `:DIAGnostic:ERASe`, and in
3543 `:SYSTem:PON`.
