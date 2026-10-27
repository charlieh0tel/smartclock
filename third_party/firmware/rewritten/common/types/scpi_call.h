/* A SCPI call, its path and what its handler returns, as the images'
 * smartclock.h declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C". Where a comment cites code, it is the
 * Z3805A 3543B's, which the 58503A's copy of the comment came from. */

/* A header's path, scpi_interpret's 20 bytes (scpi_interpret+0x88): the list
 * the next keyword is looked up in, and the words the nodes on the way and
 * their numeric suffixes add, the handler's first parameters. */
typedef struct scpi_path {
  scpi_child_list* list; /* +0x00 */
  int16_t count;         /* +0x04 */
  uint16_t words[7];     /* +0x06 */
} scpi_path;

struct status_model;
/* scpi_call's responding: what a message's queries have responded so
 * far, which scpi_interpret clears at each message's start
 * (scpi_interpret+0xac): nothing, so that the first response is preceded by the
 * output port's begin signal (scpi_interpret+0x180 to scpi_interpret+0x198); a
 * response, so that the next is preceded by a ';' (scpi_interpret+0x19e); an
 * indefinite response, which scpi_format_response reports as -90
 * (scpi_interpret+0x1d0 to scpi_interpret+0x1d6), after which every query is
 * reported as -15 (scpi_interpret+0x168 to scpi_interpret+0x178). */
typedef enum __attribute__((packed)) scpi_call_responding {
  SCPI_CALL_NO_RESPONSE = 0,
  SCPI_CALL_RESPONDING = 1,
  SCPI_CALL_INDEFINITE = 2,
} scpi_call_responding;

/* scpi_parse_message_unit's own results that are not status
 * conditions, beside the conditions it returns (scpi_engine.h). */
typedef enum __attribute__((packed)) scpi_message_unit_result {
  SCPI_STATUS_NO_PARAMETERS = -100, /* an empty message unit */
  SCPI_STATUS_SKIPPED_END = -101,   /* SCPI_PORT_SKIPPED_END first */
} scpi_message_unit_result;

/* What an SCPI handler returns, beyond a status condition, for
 * scpi_interpret and its installer copy to act on (scpi_interpret+0x10a to
 * scpi_interpret+0x13a): call the common list's _TRG node, the user-defined
 * trigger, in its place; set the header path back to the root; end the session,
 * which :SYSTem:LANGuage's "PFORTH" and "INSTALL" return
 * (scpi_set_SYSTem_LANGuage) and the installer's "PRIMARY"
 * (installer_scpi_set_SYSTem_LANGuage).  No handler of this image returns the
 * first two.  It should type the handlers' results beside
 * status_condition_code. */
typedef enum __attribute__((packed)) scpi_handler_action {
  SCPI_HANDLER_CALL_TRIGGER = -62,
  SCPI_HANDLER_RESET_PATH = -61,
  SCPI_HANDLER_SESSION_END = -60,
} scpi_handler_action;

/* The call record scpi_interpret builds for a handler, on its stack at
 * -0x78(a6) (docs/firmware/README.md, "Hand-written C"; scpi_interpret+0x4c to
 * scpi_interpret+0x6e), which every handler is given. */
typedef struct scpi_call {
  scpi_node* node;   /* +0x00 */
  uint16_t query;    /* +0x04: 0 the setter, 1 the query, the handler's index */
  scpi_arena* arena; /* +0x06 */
  uint8_t* parameters; /* +0x0a: each parameter's kind word, then its value */
  uint8_t* response;   /* +0x0e: the query's, each item's value at its place */
  struct scpi_port* input;     /* +0x12 */
  struct scpi_port* output;    /* +0x16 */
  struct status_model* status; /* +0x1a */
  uint32_t
      result; /* +0x1e: scpi_interpret's result, -1 when its port fails
                 (scpi_interpret+0x2f0), else unset (scpi_interpret+0x354) */
  scpi_child_list*
      main_tree; /* +0x22: scpi_interpret's (scpi_interpret+0x5e) */
  scpi_child_list*
      common_list; /* +0x26: scpi_interpret's (scpi_interpret+0x64) */
  scpi_call_responding responding; /* +0x2a */
  uint8_t field_2b[9];             /* +0x2b: unset */
  uint16_t field_34; /* +0x34: cleared (scpi_interpret+0x6a), which
                        scpi_interpret does not read */
  uint32_t
      last_argument; /* +0x36: scpi_interpret's last (scpi_interpret+0x6e) */
} scpi_call;
