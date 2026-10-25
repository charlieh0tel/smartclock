# GCC's libgcc source, for the CPU32

The files `tools/firmware/cpu32_libgcc.py` builds the rebuilt image's
libgcc from: the CPU32's, with soft float, the members m68k-elf's gcc
builds for its CPU32 multilib, which Ubuntu's m68k-linux-gnu toolchain
lacks (`docs/firmware/README.md`, "Hand-written C").  They are GCC's,
not this project's (`third_party/NOTICE`), and only those the build
reads.

**Version.**  GCC 15.2.0, from the release tarball
`https://ftp.gnu.org/gnu/gcc/gcc-15.2.0/gcc-15.2.0.tar.xz`, sha256
`438fd996826b0c82485a29da03a72d71d6e3541a83ec702df4271f6fe025d24e`
(also the tarball Ubuntu's `gcc-15-source` 15.2.0-16ubuntu1 installs),
unmodified, at the tarball's own paths.  `gcc/BASE-VER` gives the
version; the build warns when the m68k gcc's is another.

| File | Bytes | Read for |
| ---- | ----: | -------- |
| `libgcc/libgcc2.c` | 74813 | the 64-bit integer routines and float conversions |
| `libgcc/libgcc2.h` | 17632 | `libgcc2.c`'s types and names |
| `libgcc/config/m68k/lb1sf68.S` | 102069 | the soft-float arithmetic and comparisons, 32-bit multiply and divide |
| `libgcc/config/m68k/fpgnulib.c` | 14408 | the soft-float conversions `lb1sf68.S` lacks |
| `include/longlong.h` | 61631 | `libgcc2.c`'s double-word arithmetic |
| `include/ansidecl.h` | 11777 | included by `hashtab.h` |
| `include/filenames.h` | 3524 | included by `tsystem.h` |
| `include/hashtab.h` | 7398 | included by `filenames.h` |
| `gcc/coretypes.h` | 15831 | included by `libgcc2.c` |
| `gcc/tsystem.h` | 3980 | included by `libgcc2.c` |
| `gcc/BASE-VER` | 7 | the version |
| `COPYING3` | 35147 | the GNU General Public License, version 3 |
| `COPYING.RUNTIME` | 3324 | the GCC Runtime Library Exception, version 3.1 |
| `COPYING.LIB` | 26527 | the GNU Lesser General Public License, version 2.1, `longlong.h`'s |

**Written for this project**, in `generated/`, in place of what GCC's
build generates, so that no GCC build is needed:

| File | In place of |
| ---- | ----------- |
| `tconfig.h` | gcc/configure's, without `auto-host.h` |
| `tm.h` | the target's, of which `libgcc2.c` reads `UNITS_PER_WORD` (4, from `gcc/config/m68k/m68k.h`) and `MIN_UNITS_PER_WORD`; `-fbuilding-libgcc` predefines the rest it reads |
| `libgcc_tm.h` | libgcc's configure's, empty for m68k |
| `auto-target.h` | libgcc's configure's, none of whose definitions the routines read |
| `xfgnulib.c` | `config/m68k/t-floatlib`'s, `fpgnulib.c` after `#define EXTFLOAT`, included rather than copied |

Each member builds to the same instructions from these files as from
the whole tarball with the same `generated/` headers.  To
move to another GCC, replace the files above from its tarball, keep
`generated/` in step with its `libgcc/Makefile.in` and
`config/m68k/t-floatlib`, and update this file.
