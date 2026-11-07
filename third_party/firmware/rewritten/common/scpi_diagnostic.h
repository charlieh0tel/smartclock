/* What the SCPI :DIAGnostic tree's files share (scpi_diagnostic.c and
 * each image's own scpi_diagnostic.c): the response items and
 * parameters, the logs' path word and a log's entries.  The exported C includes
 * the hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* A response's long, scpi_format_long's and _unsigned_long's 6 bytes. */
typedef struct scpi_diagnostic_long {
  int32_t value;   /* +0x00 */
  uint16_t unused; /* +0x04 */
} scpi_diagnostic_long;

/* A response's float, scpi_format_float's 6 bytes. */
typedef struct scpi_diagnostic_float {
  float value;     /* +0x00 */
  uint16_t unused; /* +0x04 */
} scpi_diagnostic_float;

/* A parameter of a word, a boolean's or a short's: its kind, the
 * value. */
typedef struct scpi_diagnostic_word {
  int16_t kind;  /* +0x00 */
  int16_t value; /* +0x02 */
} scpi_diagnostic_word;

/* A numeric setting's parameter as scpi_numeric_set and _get take it:
 * its kind, then a float's bits, a long or a bound, MAXimum or
 * MINimum. */
typedef struct scpi_diagnostic_numeric {
  int16_t kind; /* +0x00 */
  union {
    uint32_t real_bits; /* +0x02 */
    int32_t integer;    /* +0x02 */
    int16_t bound;      /* +0x02 */
  };
} scpi_diagnostic_numeric;

/* A parameter after one path word: the word, a numeric suffix's or a
 * subtree's, then the parameter. */
typedef struct scpi_diagnostic_indexed_word {
  int16_t index;              /* +0x00 */
  scpi_diagnostic_word given; /* +0x02 */
} scpi_diagnostic_indexed_word;

/* The :LOG and :SLOG subtrees' path word: the event log, or the
 * reset-cause log at 0x4001c0. */
enum {
  SCPI_DIAGNOSTIC_EVENT_LOG = 0,
  SCPI_DIAGNOSTIC_RESET_LOG = 1,
};

/* A log's entries, the event log's or the reset-cause log's. */
static int16_t scpi_diagnostic_log_entries(int16_t log) {
  if (log == SCPI_DIAGNOSTIC_EVENT_LOG) {
    return log_entry_total(0);
  }
  return log_count(0);
}
