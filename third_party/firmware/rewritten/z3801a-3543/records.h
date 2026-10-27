/* The image's own of what the EEPROM's records share, the rest of
 * which is common/records.h's: the tag and the firmware revision. */

/* The tag the NV register's and the identification record's writers
 * store, and the firmware revision the NV register's must match. */
static const char RECORDS_TAG[] = "Q";

/* The firmware revision the NV register's tag carries, which *IDN?
 * answers too. */
static const char RECORDS_FIRMWARE_REVISION[] = "3543";
