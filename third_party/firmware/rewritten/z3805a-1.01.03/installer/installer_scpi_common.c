/* The Z3805A 3543B's installer/installer_scpi_common.c, the Z3805A
 * 1.01.03's own beside common/'s, whose installer is the 3543B's byte for
 * byte (docs/firmware/z3805a-1.01.03.md, "The export and the checks"): of
 * its functions, those common/ does not hold, the 3543B's own
 * rewritten/z3805a-3543b/installer/installer_scpi_common.c's.  Its
 * comments' installer addresses are the 3543B's, which are this image's; a
 * primary address is moved to this image's.  Each keeps its generated
 * prototype. */

/* The installer's handlers of the SCPI common commands, *CLS and *IDN?
 * (`applications/scpi/handlers/common`; scpi_common.c has the
 * primary's), on the installer's status model and identification.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* installer/installer_scpi_common.c's functions that are this image's own: the
 * rest are common/installer/installer_scpi_common.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* *IDN?: the maker, model, serial number and the installer's name, Peru,
 * with the hardware suffix, in installer_idn_response, as counted text
 * (installer::0x107bc4); docs/firmware/restart.md, "The installer". */
int32_t installer_scpi_query_common_IDN(void* parameters, void* response,
                                        scpi_call* call) {
  scpi_token_string* identification = response;

  installer_sprintf(installer_idn_response, "HEWLETT-PACKARD,%s,%s,%s-%c",
                    installer_idn_model, installer_idn_serial, "Peru",
                    installer_idn_suffix);
  identification->text = installer_idn_response;
  identification->length = scpi_engine_length(identification->text);
  return 0;
}
