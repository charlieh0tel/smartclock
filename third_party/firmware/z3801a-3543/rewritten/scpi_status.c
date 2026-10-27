/* scpi_status.c's functions that are this image's own: the rest are
 * common/scpi_status.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The first register with transition filters, :OPERation
 * (status_register_index STATUS_OPERATION); nv_settings' filters start
 * with it. */
static const int16_t SCPI_STATUS_FIRST_FILTERED = 2;

/* A mask or filter value as the parser leaves it: its kind, then a
 * decimal number as a short or a non-decimal one (#H, #Q, #B) as a
 * long. */
typedef struct scpi_status_number {
  int16_t kind; /* +0x00: SCPI_PARAMETER_DECIMAL or _NON_DECIMAL */
  union {
    int16_t decimal;      /* +0x02 */
    uint32_t non_decimal; /* +0x02 */
  };
} scpi_status_number;

/* A register's setter's parameters: its index and the value. */
typedef struct scpi_status_setting {
  int16_t index;             /* +0x00 */
  scpi_status_number number; /* +0x02 */
} scpi_status_setting;

/* :OPERation:HARDware's setter's parameters. */
typedef struct scpi_status_hardware_setting {
  int16_t operation;         /* +0x00 */
  int16_t index;             /* +0x02 */
  scpi_status_number number; /* +0x04 */
} scpi_status_hardware_setting;

/* The value of a mask or filter, else the setter's entry D2. */
static int16_t scpi_status_value(const scpi_status_number* number,
                                 int16_t entry_d2) {
  if (number->kind == SCPI_PARAMETER_NON_DECIMAL) {
    return (int16_t)number->non_decimal;
  }
  if (number->kind == SCPI_PARAMETER_DECIMAL) {
    return number->decimal;
  }
  return entry_d2;
}

/* Whether a register's value is within its bounds, reporting -222 when
 * it is not. */
static bool scpi_status_in_range(int16_t index, int16_t value,
                                 scpi_port* input) {
  const status_register_layout* layout = &status_register_layouts[index];

  return scpi_check_range(layout->minimum, value, layout->maximum,
                          return_zero(input)) != 0;
}

/* Reports a device-specific error where a register's word read back
 * differs from nv_settings'. */
static void scpi_status_verify(uint16_t word, uint32_t saved,
                               scpi_port* input) {
  if (word != saved) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, return_zero(input));
  }
}

/* Sets a register's enable, through pllc, as nv_settings keeps it. */
static void scpi_status_enable_set(int16_t index,
                                   const scpi_status_number* number,
                                   int16_t entry_d2, scpi_port* input) {
  int16_t value = scpi_status_value(number, entry_d2);

  if (scpi_status_in_range(index, value, input)) {
    pllc_request_c(&scpi_param_STATus_ENABle, index, value, 0,
                   return_zero(input));
    status_register_set_enable(status_object, index,
                               nv_settings.status_enable[index]);
  }
}

/* A register's enable, checked against nv_settings'. */
static void scpi_status_enable_query(int16_t index, uint16_t* enable,
                                     scpi_port* input) {
  status_register_get_enable(status_object, index, enable);
  scpi_status_verify(*enable, nv_settings.status_enable[index], input);
}

/* Sets a register's positive transition filter. */
static void scpi_status_positive_set(int16_t index,
                                     const scpi_status_number* number,
                                     int16_t entry_d2, scpi_port* input) {
  int16_t value = scpi_status_value(number, entry_d2);
  int16_t filter = index - SCPI_STATUS_FIRST_FILTERED;

  if (scpi_status_in_range(index, value, input)) {
    pllc_request_c(&scpi_param_STATus_PTRansition, filter, value, 0,
                   return_zero(input));
    status_register_set_ptr(status_object, index,
                            nv_settings.status_positive_transition[filter]);
  }
}

/* A register's positive transition filter, checked against
 * nv_settings'. */
static void scpi_status_positive_query(int16_t index, uint16_t* filter,
                                       scpi_port* input) {
  status_register_get_ptr(status_object, index, filter);
  scpi_status_verify(
      *filter,
      nv_settings
          .status_positive_transition[index - SCPI_STATUS_FIRST_FILTERED],
      input);
}

/* Sets a register's negative transition filter. */
static void scpi_status_negative_set(int16_t index,
                                     const scpi_status_number* number,
                                     int16_t entry_d2, scpi_port* input) {
  int16_t value = scpi_status_value(number, entry_d2);
  int16_t filter = index - SCPI_STATUS_FIRST_FILTERED;

  if (scpi_status_in_range(index, value, input)) {
    pllc_request_c(&scpi_param_STATus_NTRansition, filter, value, 0,
                   return_zero(input));
    status_register_set_ntr(status_object, index,
                            nv_settings.status_negative_transition[filter]);
  }
}

/* A register's negative transition filter, checked against
 * nv_settings'. */
static void scpi_status_negative_query(int16_t index, uint16_t* filter,
                                       scpi_port* input) {
  status_register_get_ntr(status_object, index, filter);
  scpi_status_verify(
      *filter,
      nv_settings
          .status_negative_transition[index - SCPI_STATUS_FIRST_FILTERED],
      input);
}

/* :STATus:OPERation:ENABle and its kind (0x29444). */
int32_t scpi_set_STATus_OPERation_ENABle(void* parameters, void* response,
                                         scpi_call* call) {
  /* The caller's D2: the value for a kind neither decimal nor
   * non-decimal, which the parser never gives (0x2944c to 0x29466). */
  int16_t entry_d2 = (int16_t)CPU_CALLERS_REGISTER(int32_t, "d2");
  const scpi_status_setting* given = parameters;

  scpi_status_enable_set(given->index, &given->number, entry_d2, call->input);
  return 0;
}

/* :STATus:OPERation:ENABle? and its kind (0x294fe). */
int32_t scpi_query_STATus_OPERation_ENABle(void* parameters, void* response,
                                           scpi_call* call) {
  const scpi_status_query* given = parameters;

  scpi_status_enable_query(given->index, response, call->input);
  return 0;
}

/* :STATus:OPERation:PTRansition and its kind (0x29552). */
int32_t scpi_set_STATus_OPERation_PTRansition(void* parameters, void* response,
                                              scpi_call* call) {
  /* The caller's D2: the value for a kind neither decimal nor
   * non-decimal, which the parser never gives (0x2944c to 0x29466). */
  int16_t entry_d2 = (int16_t)CPU_CALLERS_REGISTER(int32_t, "d2");
  const scpi_status_setting* given = parameters;

  scpi_status_positive_set(given->index, &given->number, entry_d2, call->input);
  return 0;
}

/* :STATus:OPERation:PTRansition? and its kind (0x29610). */
int32_t scpi_query_STATus_OPERation_PTRansition(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  const scpi_status_query* given = parameters;

  scpi_status_positive_query(given->index, response, call->input);
  return 0;
}

/* :STATus:OPERation:NTRansition and its kind (0x29664). */
int32_t scpi_set_STATus_OPERation_NTRansition(void* parameters, void* response,
                                              scpi_call* call) {
  /* The caller's D2: the value for a kind neither decimal nor
   * non-decimal, which the parser never gives (0x2944c to 0x29466). */
  int16_t entry_d2 = (int16_t)CPU_CALLERS_REGISTER(int32_t, "d2");
  const scpi_status_setting* given = parameters;

  scpi_status_negative_set(given->index, &given->number, entry_d2, call->input);
  return 0;
}

/* :STATus:OPERation:NTRansition? and its kind (0x29722). */
int32_t scpi_query_STATus_OPERation_NTRansition(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  const scpi_status_query* given = parameters;

  scpi_status_negative_query(given->index, response, call->input);
  return 0;
}

/* :STATus:OPERation:HARDware:ENABle (0x297ba). */
int32_t scpi_set_STATus_OPERation_HARDware_ENABle(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  /* The caller's D2: the value for a kind neither decimal nor
   * non-decimal, which the parser never gives (0x2944c to 0x29466). */
  int16_t entry_d2 = (int16_t)CPU_CALLERS_REGISTER(int32_t, "d2");
  const scpi_status_hardware_setting* given = parameters;

  scpi_status_enable_set(given->index, &given->number, entry_d2, call->input);
  return 0;
}

/* :STATus:OPERation:HARDware:ENABle? (0x2987c). */
int32_t scpi_query_STATus_OPERation_HARDware_ENABle(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  const scpi_status_hardware_query* given = parameters;

  scpi_status_enable_query(given->index, response, call->input);
  return 0;
}

/* :STATus:OPERation:HARDware:PTRansition (0x298d4). */
int32_t scpi_set_STATus_OPERation_HARDware_PTRansition(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  /* The caller's D2: the value for a kind neither decimal nor
   * non-decimal, which the parser never gives (0x2944c to 0x29466). */
  int16_t entry_d2 = (int16_t)CPU_CALLERS_REGISTER(int32_t, "d2");
  const scpi_status_hardware_setting* given = parameters;

  scpi_status_positive_set(given->index, &given->number, entry_d2, call->input);
  return 0;
}

/* :STATus:OPERation:HARDware:PTRansition? (0x2999a). */
int32_t scpi_query_STATus_OPERation_HARDware_PTRansition(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const scpi_status_hardware_query* given = parameters;

  scpi_status_positive_query(given->index, response, call->input);
  return 0;
}

/* :STATus:OPERation:HARDware:NTRansition (0x299f2). */
int32_t scpi_set_STATus_OPERation_HARDware_NTRansition(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  /* The caller's D2: the value for a kind neither decimal nor
   * non-decimal, which the parser never gives (0x2944c to 0x29466). */
  int16_t entry_d2 = (int16_t)CPU_CALLERS_REGISTER(int32_t, "d2");
  const scpi_status_hardware_setting* given = parameters;

  scpi_status_negative_set(given->index, &given->number, entry_d2, call->input);
  return 0;
}

/* :STATus:OPERation:HARDware:NTRansition? (0x29ab8). */
int32_t scpi_query_STATus_OPERation_HARDware_NTRansition(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const scpi_status_hardware_query* given = parameters;

  scpi_status_negative_query(given->index, response, call->input);
  return 0;
}
