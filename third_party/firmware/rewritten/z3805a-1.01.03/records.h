/* The image's own of what the EEPROM's records share, the rest of
 * which is common/records.h's: the tag, the model and the firmware
 * revision. */

/* The tag the NV register's and the identification record's writers
 * store (0x34f6b), which :DIAGnostic:IDENtification:DEFault? answers as
 * the suffix. */
static const char RECORDS_TAG[] = "S";

/* The model *IDN? gives when the identification record holds none, and
 * :DIAGnostic:IDENtification:DEFault? answers (0x34f64). */
static const char RECORDS_MODEL[] = "Z3805A";

/* The firmware revision the NV register's record must carry, which *IDN?
 * answers too (0x13048). */
static const char RECORDS_FIRMWARE_REVISION[] = "1.01.03";
