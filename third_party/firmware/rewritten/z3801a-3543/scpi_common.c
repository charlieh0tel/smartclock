/* scpi_common.c's functions that are this image's own: the rest are
 * common/scpi_common.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The greatest *ESE or *SRE mask. */
static const int32_t SCPI_COMMON_MASK_MAXIMUM = 0xff;

/* Sets a register's enable, through pllc, from a mask within 0 to
 * 0xff, else reports -222. */
static void scpi_common_enable_set(int16_t index,
                                   const scpi_word_parameter* mask,
                                   scpi_port* input) {
  if (scpi_check_range(0, mask->value, SCPI_COMMON_MASK_MAXIMUM,
                       return_zero(input)) != 0) {
    pllc_request_c(&scpi_param_STATus_ENABle, index, mask->value, 0,
                   return_zero(input));
    status_register_set_enable(status_object, index,
                               nv_settings.status_enable[index]);
  }
}

/* A register's enable, reporting a device-specific error where it
 * differs from nv_settings'. */
static void scpi_common_enable_query(int16_t index, int16_t* enable,
                                     scpi_port* input) {
  status_register_get_enable(status_object, index, (uint16_t*)enable);
  if (nv_settings.status_enable[index] != *enable) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, return_zero(input));
  }
}

/* *ESE: the Standard Event register's enable (0x290a6). */
int32_t scpi_set_common_ESE(void* parameters, void* response, scpi_call* call) {
  scpi_common_enable_set(SCPI_COMMON_STANDARD_EVENT, parameters, call->input);
  return 0;
}

/* *ESE? (0x2912c). */
int32_t scpi_query_common_ESE(void* parameters, void* response,
                              scpi_call* call) {
  scpi_common_enable_query(SCPI_COMMON_STANDARD_EVENT, response, call->input);
  return 0;
}

/* *IDN?: the maker, model, serial number and firmware revision with the
 * hardware suffix, in the source's text buffer, as counted text
 * (0x2918e). */
int32_t scpi_query_common_IDN(void* parameters, void* response,
                              scpi_call* call) {
  scpi_token_string* identification = response;
  char* text = scpi_response_texts[(uint8_t)return_zero(call->input)];

  sprintf(text, "HEWLETT-PACKARD,%s,%s,%s-%c", idn_model, idn_serial,
          RECORDS_FIRMWARE_REVISION, idn_suffix);
  identification->text = text;
  identification->length = scpi_engine_length(text);
  return 0;
}

/* *SRE: the service request enable (0x29230). */
int32_t scpi_set_common_SRE(void* parameters, void* response, scpi_call* call) {
  scpi_common_enable_set(SCPI_COMMON_STATUS_BYTE, parameters, call->input);
  return 0;
}

/* *SRE? (0x292b2). */
int32_t scpi_query_common_SRE(void* parameters, void* response,
                              scpi_call* call) {
  scpi_common_enable_query(SCPI_COMMON_STATUS_BYTE, response, call->input);
  return 0;
}

/* *TST?: runs the self-tests, test 0, through pllc; their result, 0
 * for passed (0x29310). */
int32_t scpi_query_common_TST(void* parameters, void* response,
                              scpi_call* call) {
  static const uint8_t ALL_TESTS = 0;
  uint32_t* result = response;

  pllc_request_b(&scpi_action_self_test, ALL_TESTS, return_zero(call->input));
  *result = selftest.result;
  return 0;
}
