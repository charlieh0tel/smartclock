/* The SCPI :FORMat and :SENSe trees (docs/scpi/README.md): their
 * handlers, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `applications/scpi/handlers/sense_format`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype.  Both are
 * the 59551A's time tagging (097-59551-02, "Reading Time Stamps" and
 * "Defining the Time-stamped Edge"), as :ROBin is (scpi_robin.c):
 * :FORMat:DATA chooses the :ROBin queries' format, and
 * :SENSe:TST1:EDGE is refused. */

/* A choice parameter's kind word and value, its keyword's index in the
 * node's list. */
typedef struct scpi_sense_format_choice {
  int16_t kind;   /* +0x00 */
  int16_t choice; /* +0x02 */
} scpi_sense_format_choice;

/* :FORMat:DATA's keywords, by their index in its list (0x53b1c). */
enum {
  SCPI_SENSE_FORMAT_ASCII = 0,
  SCPI_SENSE_FORMAT_INTEGER = 1,
};

/* :FORMat:DATA?: ASCii, comma-separated shorts, or INTeger, a block
 * (0x2eeb8).  Another format leaves the response as it is. */
int32_t scpi_query_FORMat_DATA(void* parameters, void* response, void* call) {
  int16_t* choice = response;

  switch (nv_settings.format_data) {
    case ROBIN_SHORTS:
      *choice = SCPI_SENSE_FORMAT_ASCII;
      break;
    case ROBIN_BLOCK:
      *choice = SCPI_SENSE_FORMAT_INTEGER;
      break;
  }
  return 0;
}

/* :SENSe:TST1:EDGE, RISing or FALLing, which reports an undefined header
 * and succeeds (0x2f2ae). */
int32_t scpi_set_SENSe_TST1_EDGE(void* parameters, void* response, void* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, return_zero());
  return 0;
}

/* :SENSe:TST1:EDGE?, which fails as the four-letter stubs do, the call
 * passed for the response (scpi_unsupported.c) (0x2f2ce). */
int32_t scpi_query_SENSe_TST1_EDGE(void* parameters, void* response,
                                   void* call) {
  scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  return 0;
}
