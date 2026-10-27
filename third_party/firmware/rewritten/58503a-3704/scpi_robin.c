/* scpi_robin.c's functions that are this image's own: the rest are
 * common/scpi_robin.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :ROBin?, :SENSe:DATA? "TSTamp 1": an input's points, in the
 * :FORMat:DATA format (0x2eb8a). */
int32_t scpi_query_ROBin(void* parameters, void* response, scpi_call* call) {
  scpi_robin_parameters* given = parameters;
  uint32_t kind;
  uint32_t second;
  int32_t error = robin_parse(given->name, &kind, &second);
  request_source source = return_zero(call->input);
  int16_t* points = robin_points[source];
  uint16_t count;

  if (error != 0) {
    return scpi_query_failed(STATUS_ILLEGAL_PARAMETER_VALUE, call);
  }
  robin_point_list((uint16_t*)points, &count, kind);
  robin_store(response, points, count, nv_settings.format_data, source);
  return 0;
}

/* :ROBin:MEMory:SAVE? (0x2ed7e). */
int32_t scpi_query_ROBin_MEMory_SAVE(void* parameters, void* response,
                                     scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(nv_settings.memory_save);
  return 0;
}

/* :ROBin:TSTamp?, an input's entry'th time stamp, its ten values in the
 * :FORMat:DATA format (0x2ee40).  The tenth is what the frame held, which
 * robin_timestamp_values leaves unset and the image copies. */
int32_t scpi_query_ROBin_TSTamp(void* parameters, void* response,
                                scpi_call* call) {
  static const int16_t LAST_ENTRY = 256;
  static const uint8_t TIMESTAMP_VALUES = 10;
  /* The value whose low byte robin_timestamp_values leaves in D0. */
  static const uint8_t TESTED_VALUE = 7;
  scpi_robin_parameters* given = parameters;
  uint32_t kind;
  uint32_t second;
  int32_t error = robin_parse(given->name, &kind, &second);
  request_source source = return_zero(call->input);
  int16_t* points = robin_points[source];
  int16_t values[TIMESTAMP_VALUES];

  if (error != 0) {
    return scpi_query_failed(STATUS_ILLEGAL_PARAMETER_VALUE, call);
  }
  if (given->entry < 1 || given->entry > LAST_ENTRY) {
    return scpi_query_failed(STATUS_DATA_OUT_OF_RANGE, call);
  }
  robin_timestamp_values(kind, given->entry, values);
  if ((uint8_t)values[TESTED_VALUE] == 0) {
    return scpi_query_failed(STATUS_DATA_OUT_OF_RANGE, call);
  }
  for (uint8_t index = 0; index < TIMESTAMP_VALUES; index++) {
    points[index] = values[index];
  }
  robin_store(response, points, TIMESTAMP_VALUES, nv_settings.format_data,
              source);
  return 0;
}
