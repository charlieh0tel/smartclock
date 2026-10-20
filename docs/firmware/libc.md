# The C library

Every image links its compiler's C library as one block, at the top of
the ROM beside the floating-point library (`softfloat.md`): in the
Z3805A 3543B from `0x5ec9e` to about `0x61900`, the order a C library
lays its routines out.  The routines below were named by reading them,
and those marked *run* were then confirmed by running them:
`tools/firmware/libc_check.py` calls each in a Unicorn 68k emulator on
known inputs, edge cases among them, and compares the result with what
the C routine of that name gives.

    uv run tools/firmware/libc_check.py third_party/firmware/z3805a-3543b.bin

`strlen`, `strcpy` and `strcmp` are not in the block; the compiler
expands them in line.

Arguments are passed on the stack, first argument lowest, in 4-byte
slots for `int`, pointers and `long` and 8 for `double`; the result
comes back in D0, or D0:D1 for a double or for `div`'s quotient and
remainder.

## Z3805A 3543B

| Address | Routine | How known |
| ------- | ------- | --------- |
| `0x5ec9e` | `abs` | run |
| `0x5ef02` | `labs` | run; the same code as `abs`, so which is which is by library order |
| `0x5ecfa` | `atoi` | run; skips spaces and a sign by the table at `0x5ed99` |
| `0x5ed98` | `ctype` table | read: one byte a character, indexed from `0x5ed99` so that EOF, -1, has a slot; bit 2 marks a digit, bit 3 a space |
| `0x5eea2` | `div` | run; quotient in D0, remainder in D1 |
| `0x5eed2` | `ldiv` | run; the same code as `div` |
| `0x5ef12` | `memcmp` | run |
| `0x5ef48` | `memcpy` | run |
| `0x5ef66` | `memset` | run |
| `0x5f0cc` | `qsort` | run; stores the comparator at `0x103940` and the element size at `0x103944` for `0x5efc4`, which sorts a range by them with the swaps `0x5ef86` and `0x5efa0` |
| `0x5f102` | `rand` | run; the ANSI C example generator, seed times 1103515245 plus 12345, bits 30 to 16 returned; the seed is at `0x103936` |
| `0x5f128` | `strcat` | run |
| `0x5f140` | `strchr` | run; finds the NUL too, as C requires |
| `0x5f15a` | `strncmp` | run |
| `0x5f190` | `strncpy` | run; pads with NULs, and does not terminate a string it truncates |
| `0x5f1be` | `toupper` | run |
| `0x5fb0c` | the formatting engine | read: a format string, its arguments and a routine that takes what it writes |
| `0x60896` | `sprintf` | run; the engine with `0x608c4`, which copies into the buffer, then a NUL |
| `0x608fe` | `vsprintf` | read: `sprintf` with the arguments as a pointer |
| `0x1de96` | `printf` | read: `vsprintf` into a 160-byte buffer on the stack, then `0x1de76` writes it out |
| `0x6098a` | `frexp` | run |
| `0x60d94` | `modf` | run |
| `0x60a10` | `log` | run; `softfloat.md`'s natural log; sets `0x10393a` to 34, ERANGE, for log of zero, so that cell is `errno` |
| `0x60f5e` | `pow` | run; `softfloat.md`'s pow |
| `0x6191c` | `sqrt` | run; `softfloat.md`'s square root |
| `0x5ecf0` | runs the `atexit` functions | read: calls the functions in the table at `0x1038b4`, last first, counting down `0x103934` |
| `0x5e93c` | raises a floating-point exception | read: every float routine calls it on an invalid operand, an overflow or a division by zero; it sets the cause in the status word at `0x1038b0` and traps if the control word at `0x1038b2` enables it (`softfloat.md`) |

The conversions in `softfloat.md` that it gives in pairs, two routines
to a line, differ by sign, which the emulator confirms: of each pair,
one negates a negative operand first and the other does not.

| Address | Routine |
| ------- | ------- |
| `0x5e168` | `int` to float |
| `0x5e20e` | `unsigned` to float |
| `0x5dfbc` | float to `int` |
| `0x5e04e` | float to `unsigned` |
| `0x5e08e` | `int` to double |
| `0x5e108` | `unsigned` to double |
| `0x5ddde` | double to `int` |
| `0x5de9e` | double to `unsigned` |
