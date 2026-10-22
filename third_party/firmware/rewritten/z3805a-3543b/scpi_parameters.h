/* What the SCPI parameters' matching shares with the installer's copy
 * of it (scpi_parameters.c, installer_scpi_parameters.c): its bodies,
 * which both copies' routines call.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* The parameter buffer's word at offset, a parameter's kind (0x27936).
 * The buffer holds each parameter's kind word, then its value, at any
 * offset its values' sizes give. */
static void scpi_parameters_mark(scpi_call* call, int16_t offset,
                                 int16_t kind) {
  *(int16_t*)(call->parameters + offset) = kind;
}

/* scpi_parameter_match's and its installer copy's body: finds the
 * node's next parameter that takes a token of the kind, marking the
 * optional ones it passes left out, and marks it with the kind; offset
 * is the next parameter's in the call's buffer. */
static int32_t scpi_parameters_match(scpi_parameter** next, int32_t kind,
                                     scpi_call* call, int16_t* offset) {
  /* The condition for a parameter of each kind where none is allowed:
   * the image's table at 0x279ca, scpi_parameter_kind_errors; the
   * installer's at installer::0x105678 holds the same. */
  static const int16_t KIND_ERRORS[] = {
      [SCPI_PARAMETER_DECIMAL] = STATUS_SCPI_NUMERIC_NOT_ALLOWED,
      [SCPI_PARAMETER_STRING] = STATUS_SCPI_STRING_NOT_ALLOWED,
      [SCPI_PARAMETER_CHARACTER] = STATUS_SCPI_CHARACTER_NOT_ALLOWED,
      [SCPI_PARAMETER_BLOCK] = STATUS_SCPI_BLOCK_NOT_ALLOWED,
      [SCPI_PARAMETER_NON_DECIMAL] = STATUS_SCPI_NUMERIC_NOT_ALLOWED,
      [SCPI_PARAMETER_EXPRESSION] = STATUS_SCPI_EXPRESSION_NOT_ALLOWED,
  };
  int16_t token = kind;

  for (scpi_parameter* entry = *next;; entry = *next = entry->next) {
    if (entry == 0) {
      return STATUS_SCPI_EXTRA_PARAMETER;
    }
    if (entry->kinds->converters[token] != 0) {
      break;
    }
    if (entry->kinds->optional == 0) {
      return KIND_ERRORS[token];
    }
    scpi_parameters_mark(call, *offset, SCPI_NONE);
    *offset += entry->kinds->size + sizeof(int16_t);
  }
  scpi_parameters_mark(call, *offset, token);
  *offset += sizeof(int16_t);
  return 0;
}

/* scpi_parameters_finish's and its installer copy's body: marks the
 * node's parameters left over left out, unless one is required. */
static uint32_t scpi_parameters_left_out(scpi_parameter** next, scpi_call* call,
                                         int16_t* offset) {
  for (; *next != 0; *next = (*next)->next) {
    if ((*next)->kinds->optional == 0) {
      return STATUS_SCPI_MISSING_PARAMETER;
    }
    scpi_parameters_mark(call, *offset, SCPI_NONE);
    *offset += (*next)->kinds->size + sizeof(int16_t);
  }
  return 0;
}
