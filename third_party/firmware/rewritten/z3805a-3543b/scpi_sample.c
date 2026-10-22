/* The SCPI :SAMPle tree (docs/scpi/README.md): its handlers, and the
 * hooks and actions pllc runs for its settings, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/sample`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.
 *
 * :SAMPle is a test tree, one node for each kind of parameter, and no
 * guide documents it.  A handler's parameters are the words its path
 * adds (an ARRay node's index), then each parameter's kind word and its
 * value; a number's value is scpi_engine_long_parameter's. */

/* The settings no name is exported for (the handlers' references). */
static scpi_setting_param* const SCPI_SAMPLE_BOOLEAN =
    (scpi_setting_param*)0x33454;
static scpi_setting_param* const SCPI_SAMPLE_BARR =
    (scpi_setting_param*)0x33472;
static scpi_numeric_param* const SCPI_SAMPLE_IMULTIPLE =
    (scpi_numeric_param*)0x335be;

/* :SAMPle:DARRay's three doubles, after scpi_value_SAMPle_DOUBle; no
 * name is exported for them (0x2e6a6). */
static double* const SCPI_SAMPLE_DARR_VALUES = (double*)0x101e30;

/* A boolean or choice parameter: its kind word, then its value. */
typedef struct scpi_sample_word_parameter {
  int16_t kind;   /* +0x00 */
  uint16_t value; /* +0x02 */
} scpi_sample_word_parameter;

/* An ARRay node's parameters: its index, the path's word, then its
 * value. */
typedef struct scpi_sample_indexed_word {
  int16_t index;                    /* +0x00 */
  scpi_sample_word_parameter given; /* +0x02 */
} scpi_sample_indexed_word;

typedef struct scpi_sample_indexed_number {
  int16_t index;                     /* +0x00 */
  scpi_engine_long_parameter number; /* +0x02 */
} scpi_sample_indexed_number;

/* A number in a list of them: its value, then the unit word its
 * converter stores after it. */
typedef struct scpi_sample_listed_number {
  scpi_engine_long_parameter number; /* +0x00 */
  int16_t unit;                      /* +0x06 */
} scpi_sample_listed_number;

/* A long in a response, and the resolution word its formatter reads
 * after it, which these handlers leave as it is. */
typedef struct scpi_sample_long_response {
  int32_t value;      /* +0x00 */
  int16_t resolution; /* +0x04 */
} scpi_sample_long_response;

_Static_assert(sizeof(scpi_sample_listed_number) == 8,
               "scpi_sample_listed_number");
_Static_assert(sizeof(scpi_sample_long_response) == 6,
               "scpi_sample_long_response");

/* :SAMPle:IMULtiple's count of numbers. */
static const int32_t SCPI_SAMPLE_IMULTIPLE_COUNT = 4;

/* :SAMPle:ACTion: requests the action the number names (0x2e306). */
int32_t scpi_set_SAMPle_ACTion(void* parameters, void* response, void* call) {
  scpi_engine_long_parameter* given = parameters;
  uint8_t source = return_zero();

  pllc_request_b(&scpi_action_SAMPle_ACTion, (uint8_t)given->value, source);
  return 0;
}

/* :SAMPle:BOOLean: sets the boolean (0x2e334). */
int32_t scpi_set_SAMPle_BOOLean(void* parameters, void* response, void* call) {
  scpi_sample_word_parameter* given = parameters;
  uint8_t source = return_zero();

  pllc_request_a(SCPI_SAMPLE_BOOLEAN, 0, (uint8_t)given->value, source);
  return 0;
}

/* :SAMPle:BOOLean? (0x2e364). */
int32_t scpi_query_SAMPle_BOOLean(void* parameters, void* response,
                                  void* call) {
  int16_t* state = response;

  *state = (int8_t)scpi_value_SAMPle_BOOLean;
  return 0;
}

/* :SAMPle:BARRay: sets one of the booleans (0x2e376). */
int32_t scpi_set_SAMPle_BARR(void* parameters, void* response, void* call) {
  scpi_sample_indexed_word* given = parameters;
  uint8_t source = return_zero();

  pllc_request_a(SCPI_SAMPLE_BARR, (uint8_t)given->index,
                 (uint8_t)given->given.value, source);
  return 0;
}

/* :SAMPle:BARRay? (0x2e3a8). */
int32_t scpi_query_SAMPle_BARR(void* parameters, void* response, void* call) {
  int16_t* index = parameters;
  int16_t* state = response;

  *state = (int8_t)scpi_value_SAMPle_BARR[*index];
  return 0;
}

/* A toggle's choice, T0 or T1, its value; another answers nothing. */
static void scpi_sample_toggle_answer(int8_t toggle, uint16_t* choice) {
  if (toggle == 0 || toggle == 1) {
    *choice = toggle;
  }
}

/* :SAMPle:TOGGle? (0x2e412). */
int32_t scpi_query_SAMPle_TOGGle(void* parameters, void* response, void* call) {
  scpi_sample_toggle_answer((int8_t)scpi_value_SAMPle_TOGGle, response);
  return 0;
}

/* :SAMPle:TARRay? (0x2e498). */
int32_t scpi_query_SAMPle_TARR(void* parameters, void* response, void* call) {
  int16_t* index = parameters;

  scpi_sample_toggle_answer((int8_t)scpi_value_SAMPle_TARR[*index], response);
  return 0;
}

/* :SAMPle:INTeger: sets the integer, or its minimum or maximum
 * (0x2e4d0). */
int32_t scpi_set_SAMPle_INTeger(void* parameters, void* response, void* call) {
  scpi_engine_long_parameter* given = parameters;
  uint8_t source = return_zero();

  scpi_numeric_set(&scpi_param_SAMPle_INTeger, 0, given->kind, given->bound, 0,
                   given->value, source);
  return 0;
}

/* :SAMPle:INTeger? (0x2e50a). */
int32_t scpi_query_SAMPle_INTeger(void* parameters, void* response,
                                  void* call) {
  int32_t* value = response;

  *value = scpi_value_SAMPle_INTeger;
  return 0;
}

/* :SAMPle:IARRay: sets one of the integers (0x2e518). */
int32_t scpi_set_SAMPle_IARR(void* parameters, void* response, void* call) {
  scpi_sample_indexed_number* given = parameters;
  uint8_t source = return_zero();

  scpi_numeric_set(&scpi_param_SAMPle_IARR, given->index, given->number.kind,
                   given->number.bound, 0, given->number.value, source);
  return 0;
}

/* :SAMPle:IARRay? (0x2e556).  The export declares the first of its four
 * longs. */
int32_t scpi_query_SAMPle_IARR(void* parameters, void* response, void* call) {
  int16_t* index = parameters;
  int32_t* values = &scpi_value_SAMPle_IARR;
  int32_t* value = response;

  *value = values[*index];
  return 0;
}

/* :SAMPle:IMULtiple: sets the four integers at once, by their last
 * index, through a copy of them whose address the request carries as
 * its value (0x2e56c). */
int32_t scpi_set_SAMPle_IMULtiple(void* parameters, void* response,
                                  void* call) {
  scpi_sample_listed_number* given = parameters;

  for (int32_t i = 0; i < SCPI_SAMPLE_IMULTIPLE_COUNT; i++) {
    scpi_values_SAMPle_IMULtiple[i] = given[i].number.value;
  }
  uint8_t source = return_zero();

  pllc_request_d(SCPI_SAMPLE_IMULTIPLE, SCPI_SAMPLE_IMULTIPLE_COUNT - 1,
                 (uint32_t)scpi_values_SAMPle_IMULtiple, source);
  return 0;
}

/* :SAMPle:IMULtiple? (0x2e5ba). */
int32_t scpi_query_SAMPle_IMULtiple(void* parameters, void* response,
                                    void* call) {
  scpi_sample_long_response* values = response;

  for (int32_t i = 0; i < SCPI_SAMPLE_IMULTIPLE_COUNT; i++) {
    values[i].value = scpi_value_SAMPle_IMULtiple[i];
  }
  return 0;
}

/* :SAMPle:FLOat: sets the real, or its minimum or maximum; the value's
 * long is the float's bits (0x2e5dc). */
int32_t scpi_set_SAMPle_FLOat(void* parameters, void* response, void* call) {
  scpi_engine_long_parameter* given = parameters;
  uint8_t source = return_zero();

  scpi_numeric_set(&scpi_param_SAMPle_FLOat, 0, given->kind, given->bound,
                   (uint32_t)given->value, 0, source);
  return 0;
}

/* :SAMPle:FLOat? (0x2e616). */
int32_t scpi_query_SAMPle_FLOat(void* parameters, void* response, void* call) {
  float* value = response;

  *value = scpi_value_SAMPle_FLOat;
  return 0;
}

/* :SAMPle:FARRay: sets one of the reals; the value's long is the
 * float's bits (0x2e624). */
int32_t scpi_set_SAMPle_FARR(void* parameters, void* response, void* call) {
  scpi_sample_indexed_number* given = parameters;
  uint8_t source = return_zero();

  scpi_numeric_set(&scpi_param_SAMPle_FARR, given->index, given->number.kind,
                   given->number.bound, (uint32_t)given->number.value, 0,
                   source);
  return 0;
}

/* :SAMPle:FARRay?: one of the reals, or its minimum or maximum
 * (0x2e662). */
int32_t scpi_query_SAMPle_FARR(void* parameters, void* response, void* call) {
  scpi_sample_indexed_number* given = parameters;
  float* value = response;

  *value = scpi_numeric_get(&scpi_param_SAMPle_FARR, given->index,
                            given->number.kind, given->number.bound);
  return 0;
}

/* :SAMPle:DOUBle: does nothing (0x2e68c). */
int32_t scpi_set_SAMPle_DOUBle(void* parameters, void* response, void* call) {
  return 0;
}

/* :SAMPle:DOUBle? (0x2e690). */
int32_t scpi_query_SAMPle_DOUBle(void* parameters, void* response, void* call) {
  double* value = response;

  *value = scpi_value_SAMPle_DOUBle;
  return 0;
}

/* :SAMPle:DARRay: does nothing (0x2e6a2). */
int32_t scpi_set_SAMPle_DARR(void* parameters, void* response, void* call) {
  return 0;
}

/* :SAMPle:DARRay? (0x2e6a6). */
int32_t scpi_query_SAMPle_DARR(void* parameters, void* response, void* call) {
  int16_t* index = parameters;
  double* value = response;

  *value = SCPI_SAMPLE_DARR_VALUES[*index];
  return 0;
}

/* :SAMPle's action: prints which of three it was (0x43426). */
void sample_action_print(int32_t index, int32_t source) {
  switch ((int8_t)index) {
    case 1:
      task_debug_message("sample action 1\r\n");
      break;
    case 2:
      task_debug_message("sample action 2\r\n");
      break;
    case 3:
      task_debug_message("sample action 3\r\n");
      break;
    default:
      break;
  }
}
