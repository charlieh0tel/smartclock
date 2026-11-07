/* What the SCPI :SOURce:PULSe tree's files share (scpi_source_pulse.c
 * and each image's own scpi_source_pulse.c): a date's or time's fields.  The
 * exported C includes the hand-written headers after its own declarations and
 * before the hand-written .c files, so every one of them can use it. */

/* A date's or time of day's three fields, as a setter's short parameters
 * or a query's short response items: each a short and its unit word,
 * after a parameter's kind word (scpi_parse_short, scpi_format_short). */
#define SCPI_SOURCE_PULSE_FIELDS 3
