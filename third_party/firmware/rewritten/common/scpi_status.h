/* What the SCPI :STATus tree's files share (scpi_status.c and each
 * image's own scpi_status.c): the register queries' parameters.  The exported C
 * includes the hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* A register's query's parameters: its index, the path's word. */
typedef struct scpi_status_query {
  int16_t index; /* +0x00 */
} scpi_status_query;

/* :OPERation:HARDware's query's parameters: :OPERation's word, then its
 * own index. */
typedef struct scpi_status_hardware_query {
  int16_t operation; /* +0x00 */
  int16_t index;     /* +0x02 */
} scpi_status_hardware_query;
