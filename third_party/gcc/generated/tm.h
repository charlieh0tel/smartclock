/* Written for this project in place of the tm.h GCC's build generates:
   of it, the one macro libgcc2.c reads that -fbuilding-libgcc does not
   predefine, gcc/config/m68k/m68k.h's UNITS_PER_WORD, and
   gcc/defaults.h's MIN_UNITS_PER_WORD from it
   (third_party/gcc/README.md).  */
#define UNITS_PER_WORD 4
#define MIN_UNITS_PER_WORD UNITS_PER_WORD
