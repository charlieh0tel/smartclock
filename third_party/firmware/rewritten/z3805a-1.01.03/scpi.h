/* From the 58503B 1.01.04's rewritten/scpi.h and the Z3815A 4010's
 * rewritten/scpi.h, copied for the Z3805A 1.01.03: what its copied functions
 * use.  Its comments' addresses are the Z3805A 1.01.03's, but where they name
 * another image. */

/* The port layer's numbers, the two cases each of its switches has
 * (0x2a510 to 0x2a638): the SCI, and DUART channel B, the SCPI port and
 * the console. */
static const int16_t SCPI_PORT_SCI = 1;

static const int16_t SCPI_PORT_DUART_B = 2;
