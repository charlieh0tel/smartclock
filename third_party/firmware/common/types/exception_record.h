/* exception_record and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The last exception, which the exception report prints: "Exception:
 * %s, vector %04X, PC = %08X, SR = %04X, addr = %08X". */
typedef struct exception_record {
  uint8_t pending;
  char name[5];
  uint16_t sr;
  uint32_t pc;
  uint16_t vector;
  uint32_t address; /* printed when the vector's format is not 0 */
} exception_record;
