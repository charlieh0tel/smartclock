/* From the Z3815A 4010's rewritten/installer/installer_scpi_common.c, the
 * 58503B 1.01.04's own beside common/'s: of its functions, those the 58503B's
 * code carries identical or relocated (docs/firmware/58503b-1.01.04.md, "The
 * link list").  Its comments' addresses are the 58503B's, but where they name
 * another image.  Each keeps its generated prototype. */

/* *IDN?: the maker, model, serial number and the installer's name with
 * the hardware suffix, in installer_idn_response, as counted text
 * (installer::0x107dea); docs/firmware/restart.md, "The installer".  The
 * name is "Vatican" (installer::0x100cda), where the 58503A's is "USA". */
int32_t installer_scpi_query_common_IDN(void* parameters, void* response,
                                        scpi_call* call) {
  scpi_token_string* identification = response;

  installer_sprintf(installer_idn_response, "HEWLETT-PACKARD,%s,%s,%s-%c",
                    installer_idn_model, installer_idn_serial, "Vatican",
                    installer_idn_suffix);
  identification->text = installer_idn_response;
  identification->length = scpi_engine_length(identification->text);
  return 0;
}
