/* scpi_output.c's functions that are this image's own: the rest are
 * common/scpi_output.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :OUTPut:STATe's done hook (0x4bec0). */
void output_state_done(void) { output_state_program(nv_settings.output_state); }

/* :OUTPut:ACTive: the output made active through 0x34056, or -221
 * Settings conflict where it refuses (0x3fd16). */
int32_t scpi_set_OUTPut_ACTive(void* parameters, void* response,
                               scpi_call* call) {
  if (output_active_request() != 0) {
    status_report_from(STATUS_SETTINGS_CONFLICT,
                       scpi_input_source(call->input));
  }
  return 0;
}

/* :OUTPut:INACtive: the output made inactive through 0x34088, or -221
 * Settings conflict where it refuses (0x3fd40). */
int32_t scpi_set_OUTPut_INACtive(void* parameters, void* response,
                                 scpi_call* call) {
  if (output_inactive_request() != 0) {
    status_report_from(STATUS_SETTINGS_CONFLICT,
                       scpi_input_source(call->input));
  }
  return 0;
}

/* :OUTPut:ACTive:DISable, through 0x34036 (0x3fd6a). */
int32_t scpi_set_OUTPut_ACTive_DISable(void* parameters, void* response,
                                       scpi_call* call) {
  output_active_disable();
  return 0;
}

/* :OUTPut:ACTive:ENABle, through 0x34046 (0x3fd74). */
int32_t scpi_set_OUTPut_ACTive_ENABle(void* parameters, void* response,
                                      scpi_call* call) {
  output_active_enable();
  return 0;
}

/* :OUTPut:ACTive:HOLDover:DURation:THReshold, through its descriptor, as
 * :ROSCillator:HOLDover:DURation:MEASurement:THReshold (0x3fdd6). */
int32_t scpi_set_OUTPut_ACTive_HOLDover_DURation_THReshold(void* parameters,
                                                           void* response,
                                                           scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  request_source source = scpi_input_source(call->input);

  scpi_numeric_set(&scpi_param_OUTPut_ACTive_HOLDover_DURation_THReshold, 0,
                   given->kind, given->bound, 0, given->value, source);
  return 0;
}

/* :OUTPut:ACTive:HOLDover:DURation:THReshold?, or its minimum or maximum
 * (0x3fe10). */
int32_t scpi_query_OUTPut_ACTive_HOLDover_DURation_THReshold(void* parameters,
                                                             void* response,
                                                             scpi_call* call) {
  scpi_engine_long_parameter* given = parameters;
  int32_t* seconds = response;
  float value =
      scpi_numeric_get(&scpi_param_OUTPut_ACTive_HOLDover_DURation_THReshold, 0,
                       given->kind, given->bound);

  *seconds = float_to_integer(value);
  return 0;
}

/* :OUTPut:INACtive?: why the output is inactive, its inactive cause as
 * the response's keyword, NONE for none or a cause it has no keyword
 * for (0x3fe3c). */
int32_t scpi_query_OUTPut_INACtive(void* parameters, void* response,
                                   scpi_call* call) {
  int16_t* choice = response;

  switch (output_inactive_cause_get()) {
    case OUTPUT_INACTIVE_HARD:
      *choice = OUTPUT_INACTIVE_CHOICE_HARD;
      break;
    case OUTPUT_INACTIVE_HOLD:
      *choice = OUTPUT_INACTIVE_CHOICE_HOLD;
      break;
    case OUTPUT_INACTIVE_MANUAL:
      *choice = OUTPUT_INACTIVE_CHOICE_MANUAL;
      break;
    case OUTPUT_INACTIVE_EXTERNAL:
      *choice = OUTPUT_INACTIVE_CHOICE_EXTERNAL;
      break;
    case OUTPUT_INACTIVE_EXTRACTOR:
      *choice = OUTPUT_INACTIVE_CHOICE_EXTRACTOR;
      break;
    default:
      *choice = OUTPUT_INACTIVE_CHOICE_NONE;
      break;
  }
  return 0;
}

/* :OUTPut:STATe?: the output's state as the response's keyword, UNLOCK
 * for any other (0x3fe92). */
int32_t scpi_query_OUTPut_STATe(void* parameters, void* response,
                                scpi_call* call) {
  int16_t* choice = response;

  if (output_state_get() == OUTPUT_ACTIVE) {
    *choice = OUTPUT_STATE_CHOICE_ACTIVE;
  } else if (output_state_get() == OUTPUT_STANDBY) {
    *choice = OUTPUT_STATE_CHOICE_STANDBY;
  } else if (output_state_get() == OUTPUT_BLOCKED) {
    *choice = OUTPUT_STATE_CHOICE_BLOCK;
  } else if (output_state_get() == OUTPUT_ALARM) {
    *choice = OUTPUT_STATE_CHOICE_ALARM;
  } else {
    *choice = OUTPUT_STATE_CHOICE_UNLOCK;
  }
  return 0;
}

/* :OUTPut:PIN1:DELay, refused. */
int32_t scpi_set_OUTPut_PIN1_DELay(void* parameters, void* response,
                                   scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :OUTPut:PIN1:DELay?, which fails as the four-letter stubs do, the
 * call passed for the response (scpi_unsupported.c). */
int32_t scpi_query_OUTPut_PIN1_DELay(void* parameters, void* response,
                                     scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :OUTPut:PIN1:DELay:ALIGnment, which reports an undefined header and
 * succeeds. */
int32_t scpi_set_OUTPut_PIN1_DELay_ALIGnment(void* parameters, void* response,
                                             scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  return 0;
}

/* :OUTPut:PIN1:DELay:ALIGnment?, failing as :DELay? does. */
int32_t scpi_query_OUTPut_PIN1_DELay_ALIGnment(void* parameters, void* response,
                                               scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :OUTPut:PIN1:FREQuency, refused. */
int32_t scpi_set_OUTPut_PIN1_FREQuency(void* parameters, void* response,
                                       scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :OUTPut:PIN1:FREQuency?, failing as :DELay? does. */
int32_t scpi_query_OUTPut_PIN1_FREQuency(void* parameters, void* response,
                                         scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}
