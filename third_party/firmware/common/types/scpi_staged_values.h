/* scpi_staged_values and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The SCPI task's response areas, end to end from scpi_response_texts: a record
 * for each source return_zero gives, of which there are two, as
 * robin_blocks and robin_arrays hold.  A query writes its response into
 * its source's record and answers its address; a setter of several
 * values stages them in its source's scpi_staged_values and sends pllc
 * their address. */
/* A query's text. */
typedef char scpi_response_text[0x11a];
/* A satellite list, or :ROBin:POINts?' counts. */
typedef int16_t scpi_response_list[32];
/* :ROBin:MEMory:OVERflow:COUNt?'s counts, an input's each. */
typedef double robin_overflow_counts[3];
/* A setter's values, the pllc_request's values[0], its index the last. */
typedef int32_t scpi_staged_values[3];
/* :ROBin? and :ROBin:TSTamp?'s points. */
typedef int16_t robin_point_record[80];
