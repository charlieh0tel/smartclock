/* The SCPI :STATus tree (docs/scpi/README.md): its handlers, written by
 * hand in place of the decompiler's (docs/firmware/README.md, "Hand-
 * written C"; docs/firmware/modules.md,
 * `applications/scpi/handlers/status`).  097-z3801-01, table 4-2, lists
 * the commands.  A register's handlers are shared by :OPERation,
 * :QUEStionable, :POWerup and :HOLDover, which the path's word tells
 * apart, and by :OPERation:HARDware, whose word follows :OPERation's.
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* One register's entry in the image's table at 0x2f79e, as status.c's
 * status_register_layout reads it; the setters check a value against
 * its bounds.  22 bytes. */
typedef struct scpi_status_layout {
  uint8_t parent_index;  /* +0x00 */
  uint8_t unused;        /* +0x01 */
  uint16_t parent_bit;   /* +0x02 */
  uint16_t maximum;      /* +0x04: the greatest enable or filter */
  uint16_t minimum;      /* +0x06 */
  uint16_t defined_bits; /* +0x08 */
  uint32_t* enable;      /* +0x0a */
  uint32_t* positive;    /* +0x0e */
  uint32_t* negative;    /* +0x12 */
} scpi_status_layout;

_Static_assert(sizeof(scpi_status_layout) == 22, "scpi_status_layout");

/* The table, the image's, by register index (0x2a5c6, 0x2a5d6). */
static const scpi_status_layout* const SCPI_STATUS_LAYOUTS =
    (const scpi_status_layout*)0x2f79e;

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

/* A register's query's parameters: its index, the path's word. */
typedef struct scpi_status_query {
  int16_t index; /* +0x00 */
} scpi_status_query;

/* A register's setter's parameters: its index and the value. */
typedef struct scpi_status_setting {
  int16_t index;             /* +0x00 */
  scpi_status_number number; /* +0x02 */
} scpi_status_setting;

/* :OPERation:HARDware's query's parameters: :OPERation's word, then its
 * own index. */
typedef struct scpi_status_hardware_query {
  int16_t operation; /* +0x00 */
  int16_t index;     /* +0x02 */
} scpi_status_hardware_query;

/* :OPERation:HARDware's setter's parameters. */
typedef struct scpi_status_hardware_setting {
  int16_t operation;         /* +0x00 */
  int16_t index;             /* +0x02 */
  scpi_status_number number; /* +0x04 */
} scpi_status_hardware_setting;

/* :QUEStionable's :USER setters' parameters: the register's word, then
 * the choice, :CONDition's CLEar 0 or SET 1 (0x53c92), :EVENt's NTR 0 or
 * PTR 1 (0x53b88). */
typedef struct scpi_status_user {
  int16_t index;  /* +0x00 */
  int16_t kind;   /* +0x02 */
  int16_t choice; /* +0x04 */
} scpi_status_user;

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
static bool scpi_status_in_range(int16_t index, int16_t value) {
  const scpi_status_layout* layout = &SCPI_STATUS_LAYOUTS[index];

  return scpi_check_range(layout->minimum, value, layout->maximum,
                          return_zero()) != 0;
}

/* Reports a device-specific error where a register's word read back
 * differs from nv_settings'. */
static void scpi_status_verify(uint16_t word, uint32_t saved) {
  if (word != saved) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, return_zero());
  }
}

/* Sets a register's enable, through pllc, as nv_settings keeps it. */
static void scpi_status_enable_set(int16_t index,
                                   const scpi_status_number* number,
                                   int16_t entry_d2) {
  int16_t value = scpi_status_value(number, entry_d2);

  if (scpi_status_in_range(index, value)) {
    pllc_request_c(&scpi_param_STATus_ENABle, index, value, 0, return_zero());
    status_register_set_enable(status_object, index,
                               nv_settings.status_enable[index]);
  }
}

/* A register's enable, checked against nv_settings'. */
static void scpi_status_enable_query(int16_t index, uint16_t* enable) {
  status_register_get_enable(status_object, index, enable);
  scpi_status_verify(*enable, nv_settings.status_enable[index]);
}

/* Sets a register's positive transition filter. */
static void scpi_status_positive_set(int16_t index,
                                     const scpi_status_number* number,
                                     int16_t entry_d2) {
  int16_t value = scpi_status_value(number, entry_d2);
  int16_t filter = index - SCPI_STATUS_FIRST_FILTERED;

  if (scpi_status_in_range(index, value)) {
    pllc_request_c(&scpi_param_STATus_PTRansition, filter, value, 0,
                   return_zero());
    status_register_set_ptr(status_object, index,
                            nv_settings.status_positive_transition[filter]);
  }
}

/* A register's positive transition filter, checked against
 * nv_settings'. */
static void scpi_status_positive_query(int16_t index, uint16_t* filter) {
  status_register_get_ptr(status_object, index, filter);
  scpi_status_verify(
      *filter,
      nv_settings
          .status_positive_transition[index - SCPI_STATUS_FIRST_FILTERED]);
}

/* Sets a register's negative transition filter. */
static void scpi_status_negative_set(int16_t index,
                                     const scpi_status_number* number,
                                     int16_t entry_d2) {
  int16_t value = scpi_status_value(number, entry_d2);
  int16_t filter = index - SCPI_STATUS_FIRST_FILTERED;

  if (scpi_status_in_range(index, value)) {
    pllc_request_c(&scpi_param_STATus_NTRansition, filter, value, 0,
                   return_zero());
    status_register_set_ntr(status_object, index,
                            nv_settings.status_negative_transition[filter]);
  }
}

/* A register's negative transition filter, checked against
 * nv_settings'. */
static void scpi_status_negative_query(int16_t index, uint16_t* filter) {
  status_register_get_ntr(status_object, index, filter);
  scpi_status_verify(
      *filter,
      nv_settings
          .status_negative_transition[index - SCPI_STATUS_FIRST_FILTERED]);
}

/* :STATus:OPERation:CONDition? and its kind (0x2a4a0). */
int32_t scpi_query_STATus_OPERation_CONDition(void* parameters, void* response,
                                              void* call) {
  const scpi_status_query* given = parameters;

  status_read_condition(status_object, given->index, response);
  return 0;
}

/* :STATus:OPERation:EVENt? and its kind, cleared as read (0x2a480). */
int32_t scpi_query_STATus_OPERation_EVENt(void* parameters, void* response,
                                          void* call) {
  const scpi_status_query* given = parameters;

  status_read_event(status_object, given->index, response);
  return 0;
}

/* :STATus:OPERation:ENABle and its kind (0x2a580). */
int32_t scpi_set_STATus_OPERation_ENABle(void* parameters, void* response,
                                         void* call) {
  /* The caller's D2: the value for a kind neither decimal nor
   * non-decimal, which the parser never gives (0x2a588 to 0x2a5a2). */
  int16_t entry_d2 = (int16_t)CPU_CALLERS_REGISTER(int32_t, "d2");
  const scpi_status_setting* given = parameters;

  scpi_status_enable_set(given->index, &given->number, entry_d2);
  return 0;
}

/* :STATus:OPERation:ENABle? and its kind (0x2a63a). */
int32_t scpi_query_STATus_OPERation_ENABle(void* parameters, void* response,
                                           void* call) {
  const scpi_status_query* given = parameters;

  scpi_status_enable_query(given->index, response);
  return 0;
}

/* :STATus:OPERation:PTRansition and its kind (0x2a68e). */
int32_t scpi_set_STATus_OPERation_PTRansition(void* parameters, void* response,
                                              void* call) {
  /* The caller's D2: the value for a kind neither decimal nor
   * non-decimal, which the parser never gives (0x2a588 to 0x2a5a2). */
  int16_t entry_d2 = (int16_t)CPU_CALLERS_REGISTER(int32_t, "d2");
  const scpi_status_setting* given = parameters;

  scpi_status_positive_set(given->index, &given->number, entry_d2);
  return 0;
}

/* :STATus:OPERation:PTRansition? and its kind (0x2a74c). */
int32_t scpi_query_STATus_OPERation_PTRansition(void* parameters,
                                                void* response, void* call) {
  const scpi_status_query* given = parameters;

  scpi_status_positive_query(given->index, response);
  return 0;
}

/* :STATus:OPERation:NTRansition and its kind (0x2a7a0). */
int32_t scpi_set_STATus_OPERation_NTRansition(void* parameters, void* response,
                                              void* call) {
  /* The caller's D2: the value for a kind neither decimal nor
   * non-decimal, which the parser never gives (0x2a588 to 0x2a5a2). */
  int16_t entry_d2 = (int16_t)CPU_CALLERS_REGISTER(int32_t, "d2");
  const scpi_status_setting* given = parameters;

  scpi_status_negative_set(given->index, &given->number, entry_d2);
  return 0;
}

/* :STATus:OPERation:NTRansition? and its kind (0x2a85e). */
int32_t scpi_query_STATus_OPERation_NTRansition(void* parameters,
                                                void* response, void* call) {
  const scpi_status_query* given = parameters;

  scpi_status_negative_query(given->index, response);
  return 0;
}

/* :STATus:OPERation:HARDware:CONDition? (0x2a8d4). */
int32_t scpi_query_STATus_OPERation_HARDware_CONDition(void* parameters,
                                                       void* response,
                                                       void* call) {
  const scpi_status_hardware_query* given = parameters;

  status_read_condition(status_object, given->index, response);
  return 0;
}

/* :STATus:OPERation:HARDware:EVENt?, cleared as read (0x2a8b2). */
int32_t scpi_query_STATus_OPERation_HARDware_EVENt(void* parameters,
                                                   void* response, void* call) {
  const scpi_status_hardware_query* given = parameters;

  status_read_event(status_object, given->index, response);
  return 0;
}

/* :STATus:OPERation:HARDware:ENABle (0x2a8f6). */
int32_t scpi_set_STATus_OPERation_HARDware_ENABle(void* parameters,
                                                  void* response, void* call) {
  /* The caller's D2: the value for a kind neither decimal nor
   * non-decimal, which the parser never gives (0x2a588 to 0x2a5a2). */
  int16_t entry_d2 = (int16_t)CPU_CALLERS_REGISTER(int32_t, "d2");
  const scpi_status_hardware_setting* given = parameters;

  scpi_status_enable_set(given->index, &given->number, entry_d2);
  return 0;
}

/* :STATus:OPERation:HARDware:ENABle? (0x2a9b8). */
int32_t scpi_query_STATus_OPERation_HARDware_ENABle(void* parameters,
                                                    void* response,
                                                    void* call) {
  const scpi_status_hardware_query* given = parameters;

  scpi_status_enable_query(given->index, response);
  return 0;
}

/* :STATus:OPERation:HARDware:PTRansition (0x2aa10). */
int32_t scpi_set_STATus_OPERation_HARDware_PTRansition(void* parameters,
                                                       void* response,
                                                       void* call) {
  /* The caller's D2: the value for a kind neither decimal nor
   * non-decimal, which the parser never gives (0x2a588 to 0x2a5a2). */
  int16_t entry_d2 = (int16_t)CPU_CALLERS_REGISTER(int32_t, "d2");
  const scpi_status_hardware_setting* given = parameters;

  scpi_status_positive_set(given->index, &given->number, entry_d2);
  return 0;
}

/* :STATus:OPERation:HARDware:PTRansition? (0x2aad6). */
int32_t scpi_query_STATus_OPERation_HARDware_PTRansition(void* parameters,
                                                         void* response,
                                                         void* call) {
  const scpi_status_hardware_query* given = parameters;

  scpi_status_positive_query(given->index, response);
  return 0;
}

/* :STATus:OPERation:HARDware:NTRansition (0x2ab2e). */
int32_t scpi_set_STATus_OPERation_HARDware_NTRansition(void* parameters,
                                                       void* response,
                                                       void* call) {
  /* The caller's D2: the value for a kind neither decimal nor
   * non-decimal, which the parser never gives (0x2a588 to 0x2a5a2). */
  int16_t entry_d2 = (int16_t)CPU_CALLERS_REGISTER(int32_t, "d2");
  const scpi_status_hardware_setting* given = parameters;

  scpi_status_negative_set(given->index, &given->number, entry_d2);
  return 0;
}

/* :STATus:OPERation:HARDware:NTRansition? (0x2abf4). */
int32_t scpi_query_STATus_OPERation_HARDware_NTRansition(void* parameters,
                                                         void* response,
                                                         void* call) {
  const scpi_status_hardware_query* given = parameters;

  scpi_status_negative_query(given->index, response);
  return 0;
}

/* :STATus:QUEStionable:CONDition:USER: sets or clears the user
 * condition (0x2a4d2). */
int32_t scpi_set_STATus_QUEStionable_CONDition_USER(void* parameters,
                                                    void* response,
                                                    void* call) {
  const scpi_status_user* given = parameters;

  pllc_request_a(&scpi_param_STATus_QUEStionable_USER, 0, given->choice,
                 return_zero());
  return 0;
}

/* :STATus:QUEStionable:EVENt:USER: makes the user condition's
 * transition, PTR clear then set, NTR set then clear (0x2a502). */
int32_t scpi_set_STATus_QUEStionable_EVENt_USER(void* parameters,
                                                void* response, void* call) {
  /* The choice's values (0x53b88). */
  static const int16_t POSITIVE_TRANSITION = 1;
  const scpi_status_user* given = parameters;
  bool rising = given->choice == POSITIVE_TRANSITION;

  pllc_request_a(&scpi_param_STATus_QUEStionable_USER, 0, !rising,
                 return_zero());
  pllc_request_a(&scpi_param_STATus_QUEStionable_USER, 0, rising,
                 return_zero());
  return 0;
}

/* :STATus:PRESet: SCPI's preset enables and filters, by pllc's
 * status_preset_done (0x2ac4c). */
int32_t scpi_set_STATus_PRESet(void* parameters, void* response, void* call) {
  static const uint8_t FORM_SCPI = 0; /* status.c's STATUS_PRESET_SCPI */

  pllc_request_b(&scpi_action_STATus_PRESet, FORM_SCPI, return_zero());
  return 0;
}

/* :STATus:PRESet:ALARm: the enables that light the Alarm LED on the
 * factory-default conditions (0x2ac72). */
int32_t scpi_set_STATus_PRESet_ALARm(void* parameters, void* response,
                                     void* call) {
  static const uint8_t FORM_ALARM = 1; /* status.c's STATUS_PRESET_ALARM */

  pllc_request_b(&scpi_action_STATus_PRESet, FORM_ALARM, return_zero());
  return 0;
}
