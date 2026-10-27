/* task_debug_flag and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* Proposed enums: a rewrite that needs a record field's or a global's
 * coded values adds their enum here, packed and with its evidence, and
 * reports the fields and prototypes it should type.  The lead moves each
 * to its subject below, types them, and empties this section on
 * merging. */
/* A task's debug flag's index in dmes, in the order of the
 * console's dmes_ words and task_dmessage's table; the ninth no word
 * names, though system_init and dmes_all set it with the rest. */
typedef enum __attribute__((packed)) task_debug_flag {
  DMES_GPSM = 0,
  DMES_HMON = 1,
  DMES_KLOK = 2,
  DMES_PLLP = 3,
  DMES_ROOT = 4,
  DMES_SCPI = 5,
  DMES_SPOO = 6,
  DMES_CURV = 7,
  DMES_UNNAMED = 8,
} task_debug_flag;
