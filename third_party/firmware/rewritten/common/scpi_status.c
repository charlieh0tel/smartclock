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

/* :QUEStionable's :USER setters' parameters: the register's word, then
 * the choice, :CONDition's CLEar 0 or SET 1
 * (scpi_choices_set_STATus_QUEStionable_CONDition_USER_1), :EVENt's NTR 0 or
 * PTR 1 (scpi_choices_set_STATus_QUEStionable_EVENt_USER_1). */
typedef struct scpi_status_user {
  int16_t index;  /* +0x00 */
  int16_t kind;   /* +0x02 */
  int16_t choice; /* +0x04 */
} scpi_status_user;

/* :STATus:OPERation:CONDition? and its kind. */
int32_t scpi_query_STATus_OPERation_CONDition(void* parameters, void* response,
                                              scpi_call* call) {
  const scpi_status_query* given = parameters;

  status_read_condition(status_object, given->index, response);
  return 0;
}

/* :STATus:OPERation:EVENt? and its kind, cleared as read. */
int32_t scpi_query_STATus_OPERation_EVENt(void* parameters, void* response,
                                          scpi_call* call) {
  const scpi_status_query* given = parameters;

  status_read_event(status_object, given->index, response);
  return 0;
}

/* :STATus:OPERation:HARDware:CONDition?. */
int32_t scpi_query_STATus_OPERation_HARDware_CONDition(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  const scpi_status_hardware_query* given = parameters;

  status_read_condition(status_object, given->index, response);
  return 0;
}

/* :STATus:OPERation:HARDware:EVENt?, cleared as read. */
int32_t scpi_query_STATus_OPERation_HARDware_EVENt(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  const scpi_status_hardware_query* given = parameters;

  status_read_event(status_object, given->index, response);
  return 0;
}

/* :STATus:QUEStionable:CONDition:USER: sets or clears the user
 * condition. */
int32_t scpi_set_STATus_QUEStionable_CONDition_USER(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  const scpi_status_user* given = parameters;

  pllc_request_a(&scpi_param_STATus_QUEStionable_USER, 0, given->choice,
                 return_zero(call->input));
  return 0;
}

/* :STATus:QUEStionable:EVENt:USER: makes the user condition's
 * transition, PTR clear then set, NTR set then clear. */
int32_t scpi_set_STATus_QUEStionable_EVENt_USER(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  /* The choice's values (scpi_choices_set_STATus_QUEStionable_EVENt_USER_1). */
  static const int16_t POSITIVE_TRANSITION = 1;
  const scpi_status_user* given = parameters;
  bool rising = given->choice == POSITIVE_TRANSITION;

  pllc_request_a(&scpi_param_STATus_QUEStionable_USER, 0, !rising,
                 return_zero(call->input));
  pllc_request_a(&scpi_param_STATus_QUEStionable_USER, 0, rising,
                 return_zero(call->input));
  return 0;
}

/* :STATus:PRESet: SCPI's preset enables and filters, by pllc's
 * status_preset_done. */
int32_t scpi_set_STATus_PRESet(void* parameters, void* response,
                               scpi_call* call) {
  pllc_request_b(&scpi_action_STATus_PRESet, STATUS_PRESET_SCPI,
                 return_zero(call->input));
  return 0;
}

/* :STATus:PRESet:ALARm: the enables that light the Alarm LED on the
 * factory-default conditions. */
int32_t scpi_set_STATus_PRESet_ALARm(void* parameters, void* response,
                                     scpi_call* call) {
  pllc_request_b(&scpi_action_STATus_PRESet, STATUS_PRESET_ALARM,
                 return_zero(call->input));
  return 0;
}
