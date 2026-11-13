/* The image's own of what the EEPROM's records share, the rest of
 * which is common/records.h's: the tag and the firmware revision; the
 * model is model_default_text's (0x43e76). */

/* The tag the NV register's and the identification record's writers
 * store and their readers compare, nv_tag_text's "M1" (0x43e7d). */
static const char RECORDS_TAG[] = "M1";

/* The firmware revision the NV register's must match, revision_text's
 * "3752" (0x23080), which *IDN? answers too. */
static const char RECORDS_FIRMWARE_REVISION[] = "3752";
