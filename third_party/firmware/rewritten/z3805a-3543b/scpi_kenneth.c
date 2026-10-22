/* The SCPI :KENneth tree (docs/scpi/README.md): its handlers, written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"; docs/firmware/modules.md,
 * `applications/scpi/handlers/kenneth`).  The exported C includes this
 * file after its own, so the types and prototypes it declares are in
 * scope; each function here keeps its generated prototype.  :KENneth is
 * the holdover time uncertainty's (docs/scpi/undocumented.md,
 * ":KENneth"), the Z3801A's :ROSCillator:HOLDover:TUNCertainty
 * (097-z3801-01, table 4-2, page 4-9). */

/* :KENneth:PREDicted?'s response: the time error a day's holdover is
 * expected to bring, and whether in holdover (its node's items at
 * 0x569e4). */
typedef struct scpi_kenneth_predicted {
  scpi_float_resolution error; /* +0x00 */
  int16_t holdover;            /* +0x06 */
} scpi_kenneth_predicted;

/* :KENneth:PREDicted?: from startup PLL on; -230, Data corrupt or stale,
 * before (0x2fdaa).  The image tests the result's low byte (0x2fde0). */
int32_t scpi_query_KENneth_PREDicted(void* parameters, void* response,
                                     void* call) {
  scpi_kenneth_predicted* predicted = response;
  uint8_t resolution;
  uint8_t holdover;
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  uint8_t known =
      kenneth_predicted_get(&predicted->error.value, &resolution, &holdover);

  set_interrupt_mask(sr);
  if (known == 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  /* A power of ten, signed (0x2fde8). */
  predicted->error.resolution = (int8_t)resolution;
  predicted->holdover = holdover;
  return 0;
}

/* A long numeric parameter as the parsers leave it in the call's buffer:
 * its kind, then a number's value or MAX's or MIN's value in the node's
 * list, then the unit (scpi_parse_long, scpi_parse_choice). */
typedef struct scpi_kenneth_numeric {
  int16_t kind; /* +0x00: SCPI_PARAMETER_DECIMAL, _CHARACTER, or SCPI_NONE
                   for a query's left out */
  union {
    int32_t value;   /* +0x02: a number's */
    int16_t keyword; /* +0x02: MAX 0, MIN 1 (0x53d40) */
  };
  int16_t unit; /* +0x06 */
} scpi_kenneth_numeric;

/* :KENneth:PREDicted:DURation: the number, or MAX or MIN, to pllc
 * (0x2fe16). */
int32_t scpi_set_KENneth_PREDicted_DURation(void* parameters, void* response,
                                            void* call) {
  scpi_kenneth_numeric* duration = parameters;

  scpi_numeric_set(&scpi_param_KENneth_PREDicted_DURation, 0, duration->kind,
                   duration->keyword, 0, duration->value, return_zero());
  return 0;
}

/* :KENneth:PREDicted:DURation?: the duration, or its MAX or MIN, as a
 * long (0x2fe50). */
int32_t scpi_query_KENneth_PREDicted_DURation(void* parameters, void* response,
                                              void* call) {
  scpi_kenneth_numeric* bound = parameters;
  int32_t* duration = response;

  *duration = float_to_integer(scpi_numeric_get(
      &scpi_param_KENneth_PREDicted_DURation, 0, bound->kind, bound->keyword));
  return 0;
}

/* :KENneth:PRESent?: the time error in holdover or its recovery; -221,
 * Settings conflict, else (0x2fe7c).  The image tests the result's low
 * byte (0x2feac). */
int32_t scpi_query_KENneth_PRESent(void* parameters, void* response,
                                   void* call) {
  scpi_float_resolution* error = response;
  uint8_t resolution;
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  uint8_t in_holdover = kenneth_present_get(&error->value, &resolution);

  set_interrupt_mask(sr);
  if (in_holdover == 0) {
    return scpi_query_failed(STATUS_SETTINGS_CONFLICT, call);
  }
  /* A power of ten, signed. */
  error->resolution = (int8_t)resolution;
  return 0;
}
