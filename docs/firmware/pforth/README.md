# pForth words

Every word each firmware image's pForth console knows, one file per
image in `third_party/firmware/images/`, as `smartclock-cli dump-pforth IMAGE`
prints it, and [`models.md`](models.md) setting them side by side,
as `dump-pforth --models` writes it.  A test regenerates each and fails
if it differs from the file here; `make docs` rewrites them all.
Beside each, `IMAGE-words.md` tables the same words with their flags
and stack effects ("The word tables", below).
`../console.md` is the console itself: getting in and out, reading
memory, and what particular words do.

A word here is one the image's tables hold, not one tried on a
receiver.  The console defines a few more when it starts (`ps`,
`mem_rep`, `s_rep`; `../console.md`, "Getting to it"), which no table
in the image holds.

## The word tables

`IMAGE-words.md`, one per image, tables each word with its header's
and code's addresses, its flags and inline size, its stack effect and,
where one is known, what it does:
[Z3801A 3543](z3801a-3543-words.md),
[Z3805A 3543B](z3805a-3543b-words.md),
[Z3815A 4010](z3815a-4010-words.md),
[Z3816A 4001](z3816a-4001-words.md),
[58503A 3633](58503a-3633-words.md),
[58503A 3704](58503a-3704-words.md),
[58503B 1.01.04](58503b-1.01.04-words.md).
`tools/firmware/pforth_words.py` writes them from the images, with no
Ghidra; `make pforth-words` rewrites them and `make
check-pforth-words` fails when one and its image disagree.  It needs
python3's `capstone` module.

It finds the kernel's dictionary by its shape, not by a name: the one
header whose code pushes the header's own address (`move.l
#HEADER,-(a6)`, `rts`) is `user`'s, the latest, and the links run from
it to a null link.  A header is `pforth_word_header` (`../structures.md`,
"The console's dictionary"): link, code, inline size, flags (1
compile-only, 2 hidden, 4 immediate, 8 inline), and the name, ended by a
NUL and padded to even with `0xff`.  The diagnostic words are the one
run of `diag_word_entry` records ended by a null code whose address the
image holds.  An image where either is not found once, or whose links
leave the image or loop, is refused.

A kernel word's stack effect, cells taken -- cells left, is read from
its code: an inline word's inline-size bytes, any other's routine, on
every path and into every routine it calls, counting A6's pops, pushes
and reaches (A6 is the data stack, `../console.md`, "The kernel's
registers").  For a compiling word it is what the word itself does
while compiling, its control tags included.  It is `?`, with the
reason, where the returning paths disagree, or A6 is lost, or a call or
jump goes through a register, except as the three rules below allow.
A path that resets the stacks, as `abort` does, does not return and
does not count; a trap is taken to leave A6 alone.

- **The sentinel rule** reads the printing words.  `.`, `u.` and `u.x`
  push a zero byte, push a digit byte a loop at a time, and then pop
  and print bytes until the zero comes back (`move.b (a6)+,ea`, `bne`).
  A zero byte pushed is taken as a sentinel, a loop pushing bytes above
  it leaves A6 unknown but above it, and the popping loop leaves A6
  known again one byte past the sentinel, so the two loops cancel and
  the effect is the rest of the code's: `1 -- 0`.  Routines called in
  between are followed into, so the popping loop may be a routine of
  its own, as it is.  It rests on no byte pushed above the sentinel
  being zero: an immediate is checked, and a data register's byte is
  taken to be nonzero, which the effect notes ("pushed bytes taken as
  nonzero"), as does any word that calls one so read (`.s`, `ver`).
  The digits are '0' + d or 'A' - 10 + d, d the remainder by `base`,
  nonzero for a `base` up to 208; `u.x`'s d is a nibble.  Anything else
  done to A6 above a sentinel loses A6.
- **The index rule**: a read or write at `d(a6,Xn)` reaches as deep as
  the index register says, which is not followed, but does not move
  A6.  The effect is shown with n more cells taken and left: `pick`'s
  `1+n -- 1+n`, `.s`'s `n -- n`.
- **The split rule**: a word whose returns disagree, whose code up to
  its first conditional branch runs straight, and whose branch is
  `beq`, `bne`, `bmi` or `bpl` on a cell's value, or that value less or
  plus an immediate, is walked down each side of the branch alone.
  Where each returns with one A6, both effects are given, the second
  with the condition that takes the branch, of the cell's value at
  entry, cell 1 the top: `?dup`'s `1 -- 2 | 1 (1 when cell 1 is 0)`,
  `fill`'s `3 -- 0 | 1 (1 when cell 2 - 1 is negative)`, which is its
  count of zero or less leaving the address (`../quirks.md`).

A
diagnostic word's effect is its table entry's argument and result
counts, which `pforth_define_word` compiles a wrapper from: it moves
that many cells to the C stack, calls the routine, and pushes D0 for
one result and D1 too for two.  The descriptions are the names file's
notes, in the Z3805A 3543B's table only, and what `../console.md` says.

In every image 147 of the 154 kernel effects are read, `abort` does
not return and 6 are `?`, no rule covering them: `interpret`, the
outer interpreter, runs each word it finds through a register;
`execute` and `watch` call a word through a register, so their effect
is that word's; `halt` jumps through the hook cell; `number` and `0x`
end by compiling the number as a literal when `state` is set and
pushing it when not, a branch on a variable, not a cell, and their
first branches are on the text, not a cell, so the split rule does not
apply.  Read against their standard effects,
`dup`, `drop`, `swap`, `over`, `rot`, `+`, `-`, `@`, `!`, `.` and
`emit` agree in every image.

## The word lists' format

One word a line: its table, its name and its code's address, sorted by
table and then by name.

## How it is read

**The kernel** is the interpreter's dictionary, a list of entries each
linking to the one before:

    +0   u32   the previous entry, zero for the first
    +4   u32   the word's code
    +8   u16   two fields not read here
    +10  u16
    +12        the name, NUL-terminated

`dump-pforth` finds the entry named `halt`, which every image has,
follows the links back to the first entry, and forward to the last by
the entry within 64 bytes that links to the one before.  It refuses an
image where `halt` is not found once, where the links loop, or where
two entries claim the same predecessor.

**The diagnostic words** are the firmware's own, a table of 38-byte
records:

    +0   u32       the word's code
    +4   28 bytes  the name, NUL-padded
    +32  6 bytes   not read here

It finds the record named `loop_time`, which every image has, and takes
records before and after it for as long as each has an even code
address inside the image and a padded name.

Every image has 154 kernel words, the same names; the diagnostic words
number 78 to 89 and are where the images differ.  `models.md` lists the
words every image has in one run and tables only the rest.
