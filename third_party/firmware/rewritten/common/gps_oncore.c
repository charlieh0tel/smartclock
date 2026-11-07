/* The functions of gps_oncore.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/gps_oncore.c describes the module and holds the rest,
 * among them gps_receive, which counts in gps_counts, laid out
 * otherwise in the 58503A.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* The XOR of the bytes from start up to end. */
uint8_t oncore_checksum(uint8_t* start, uint8_t* end) {
  uint8_t sum = 0;

  for (; start < end; start++) {
    sum ^= *start;
  }
  return sum;
}

/* A request's argument by its index. */
int32_t long_array_get(int32_t* array, uint8_t index) { return array[index]; }

/* Sets a request's argument by its index. */
void long_array_set(int32_t value, int32_t* array, uint8_t index) {
  array[index] = value;
}

/* The argument encoders a message's descriptor names, which
 * gps_request_build calls with the arguments, an index and where the
 * command's bytes go, and which return where the next goes. */

/* An argument's low byte. */
uint8_t* long_array_get_byte(int32_t* array, uint8_t index, uint8_t* out) {
  *out = (uint8_t)long_array_get(array, index);
  return out + 1;
}

/* An argument's low word, high byte first. */
uint8_t* long_array_get_word(int32_t* array, uint8_t index, uint8_t* out) {
  uint32_t value = (uint32_t)long_array_get(array, index);

  out[0] = (uint8_t)(value >> 8);
  out[1] = (uint8_t)value;
  return out + 2;
}

/* An argument, high byte first. */
uint8_t* long_array_get_long(int32_t* array, uint8_t index, uint8_t* out) {
  uint32_t value = (uint32_t)long_array_get(array, index);

  out[0] = (uint8_t)(value >> 24);
  out[1] = (uint8_t)(value >> 16);
  out[2] = (uint8_t)(value >> 8);
  out[3] = (uint8_t)value;
  return out + 4;
}

/* A zero byte, then an argument, high byte first. */
uint8_t* long_array_get_long_prefixed(int32_t* array, uint8_t index,
                                      uint8_t* out) {
  *out = 0;
  return long_array_get_long(array, index, out + 1);
}

/* The decoders a message's descriptor names, which gps_response_decode
 * calls with the cursor in the response and their record, and which
 * return the cursor past what they read. */

/* Copies a byte, if there is somewhere to put it. */
uint8_t* byte_copy_optional(uint8_t* source, uint8_t* destination) {
  if (destination != NULL) {
    *destination = *source;
  }
  return source + 1;
}

/* Copies a word. */
uint16_t* word_copy_advance(uint16_t* source, uint16_t* destination) {
  *destination = *source;
  return source + 1;
}

/* Folds a value into the average of count values before it. */
void running_average_update(int32_t value, double* average, uint32_t count) {
  double total = double_multiply_pointer(unsigned_to_double(count), average);

  total = double_add(integer_to_double(value), total);
  *average = double_divide_reversed(unsigned_to_double(count + 1), total);
}
