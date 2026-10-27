/* pllc_request and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A SCPI request to the pllc task: the 24-byte message pllc_request_a
 * to pllc_request_e build in their frames and send pllc_exchange, which
 * pll_scpi_receive receives into 24 bytes and get_setup_vars copies
 * into pll_scpi_param to pll_scpi_values.  The kernel copies +0x04 to
 * +0x17 (kernel_copy_message). */
typedef struct pllc_request {
  uint32_t field_00;     /* +0x00: not set; the kernel does not copy it */
  uint32_t field_04;     /* +0x04: cleared; as gps_request_entry's */
  void* param;           /* +0x08: the descriptor, a scpi_numeric_param,
                            scpi_setting_param or scpi_action_param;
                            pll_scpi_param */
  uint8_t index;         /* +0x0c: pll_scpi_index */
  request_source source; /* +0x0d: pll_scpi_source; for 0 the SCPI task is
                            signaled when it is done */
  uint8_t field_0e;      /* +0x0e: not set */
  uint8_t setting;       /* +0x0f: a setting's new value, pll_scpi_setting;
                            pllc_request_a's alone */
  uint32_t values[2];    /* +0x10: by the numeric descriptor's is_integer: 0
                            the value in values[1], 1 in values[0], 3 both, 2
                            values[0] the address of index + 1 longs */
} pllc_request;
