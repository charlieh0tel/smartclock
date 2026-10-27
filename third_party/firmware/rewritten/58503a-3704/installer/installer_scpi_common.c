/* installer/installer_scpi_common.c's functions that are this image's own: the
 * rest are common/installer/installer_scpi_common.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* *IDN?: the maker, model, serial number and the installer's name, USA,
 * with the hardware suffix, in installer_idn_response, as counted text
 * (installer::0x107c98); docs/firmware/restart.md, "The installer". */
int32_t installer_scpi_query_common_IDN(void* parameters, void* response,
                                        scpi_call* call) {
  scpi_token_string* identification = response;

  installer_sprintf(installer_idn_response, "HEWLETT-PACKARD,%s,%s,%s-%c",
                    installer_idn_model, installer_idn_serial, "USA",
                    installer_idn_suffix);
  identification->text = installer_idn_response;
  identification->length = scpi_engine_length(identification->text);
  return 0;
}
