/* What the SCPI :ROBin tree's files share (scpi_robin.c and each image's
 * own scpi_robin.c): a handler's parameters.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* A :ROBin handler's parameters: a data set's name, and :TSTamp?'s
 * entry. */
typedef struct scpi_robin_parameters {
  int16_t name_kind;  /* +0x00: SCPI_NONE when the name is left out */
  char* name;         /* +0x02 */
  int16_t entry_kind; /* +0x06 */
  int16_t entry;      /* +0x08: 1 to 256 */
} scpi_robin_parameters;
