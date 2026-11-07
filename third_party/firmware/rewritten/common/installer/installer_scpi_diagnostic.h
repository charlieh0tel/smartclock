/* What the installer's :DIAGnostic handlers share with each image's own
 * (installer_scpi_diagnostic.c): the flash's writable range and pairs,
 * its command words and unlock cycles, and :DOWNload's sizes and tries.  The
 * exported C includes the hand-written headers after its own declarations and
 * before the hand-written .c files, so every one of them can use it.
 *
 * The flash is two pairs of AM29F010s, word-interleaved, one at 0x0 and
 * one at interpolator_calibration_init (docs/firmware/restart.md, "The
 * installer"): a word written at a pair's byte address twice a part's address
 * reaches both parts of the pair at once, so that each command word holds the
 * command in both bytes.  The installer writes from boot_memcpy+0xf462 to
 * 0x7ffff and never below, where it and the boot code live.  An image whose
 * flash is another part, the Z3815A 4010's, gives its own range and commands
 * in its own file, sharing only the sizes and tries. */

/* The writable range, its end not in it. */
static const uint32_t INSTALLER_SCPI_DIAGNOSTIC_FLASH_START = 0x10000;

static const uint32_t INSTALLER_SCPI_DIAGNOSTIC_FLASH_END = 0x80000;

/* The second pair's base; the first's is 0. */
static const uint32_t INSTALLER_SCPI_DIAGNOSTIC_PAIR_2 = 0x40000;

/* A pair's two unlock addresses, the parts' 0x5555 and 0x2aaa. */
static const uint32_t INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_FIRST = 0x5555 * 2;

static const uint32_t INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_SECOND = 0x2aaa * 2;

/* The command words, each a part's command in both bytes: the unlock
 * cycles' data, then the commands the third cycle gives. */
static const uint16_t INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_FIRST_DATA = 0xaaaa;

static const uint16_t INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_SECOND_DATA = 0x5555;

static const uint16_t INSTALLER_SCPI_DIAGNOSTIC_ERASE_SETUP = 0x8080;

static const uint16_t INSTALLER_SCPI_DIAGNOSTIC_SECTOR_ERASE = 0x3030;

static const uint16_t INSTALLER_SCPI_DIAGNOSTIC_PROGRAM = 0xa0a0;

static const uint16_t INSTALLER_SCPI_DIAGNOSTIC_READ_RESET = 0xf0f0;

/* The tries each operation makes before it reports failure. */
static const int16_t INSTALLER_SCPI_DIAGNOSTIC_TRIES = 5;

/* A word of the flash at an address.  In line: :ERASe?'s blank check
 * reads each word of the flash through it, half a million calls of its
 * own, which the function check runs past its step limit. */
static inline volatile uint16_t* installer_scpi_diagnostic_word(
    uint32_t address) {
  return (volatile uint16_t*)address;
}

/* The two unlock cycles to a pair. */
static void installer_scpi_diagnostic_unlock(uint32_t pair) {
  *installer_scpi_diagnostic_word(pair +
                                  INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_FIRST) =
      INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_FIRST_DATA;
  *installer_scpi_diagnostic_word(pair +
                                  INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_SECOND) =
      INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_SECOND_DATA;
}

/* A command to a pair: the unlock cycles, then the command at the first
 * unlock address. */
static void installer_scpi_diagnostic_command(uint32_t pair, uint16_t command) {
  installer_scpi_diagnostic_unlock(pair);
  *installer_scpi_diagnostic_word(
      pair + INSTALLER_SCPI_DIAGNOSTIC_UNLOCK_FIRST) = command;
}

/* :DOWNload's record text's room in the image's frame, then its bytes':
 * 256 words, each a pair of hex digits' value; arrays' sizes, so
 * #define. */
#define INSTALLER_SCPI_DIAGNOSTIC_TEXT_SIZE 0x200

#define INSTALLER_SCPI_DIAGNOSTIC_BYTES_SIZE 0x100

/* A byte's two hex digits and a NUL, as strtol reads them. */
#define INSTALLER_SCPI_DIAGNOSTIC_PAIR_SIZE 3
