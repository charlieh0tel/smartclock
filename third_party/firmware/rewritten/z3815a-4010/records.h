/* The image's own of what the EEPROM's records share, the rest of
 * which is common/records.h's: the tag and the firmware revision. */

/* The tag the NV register's and the identification record's writers
 * store, and the firmware revision the NV register's must match
 * (identification_record_write, 0x47676; nv_save, 0x4701e). */
static const char RECORDS_TAG[] = "S3";

/* The firmware revision the NV register's tag carries, which *IDN?
 * answers too. */
static const char RECORDS_FIRMWARE_REVISION[] = "U-4010.0";
