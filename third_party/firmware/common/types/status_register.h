/* A status register, as the images' smartclock.h declare them (each includes
 * this header where they stand): docs/firmware/rewrite.md, "Shared C". Where a
 * comment cites code, it is the Z3805A 3543B's, which the 58503A's copy of the
 * comment came from. */

/* One status register of the SCPI status tree.  16 bytes. */
typedef struct status_register {
  uint16_t condition; /* +0x00 */
  uint16_t
      negative_transition; /* +0x02: falling conditions that become events */
  uint16_t positive_transition;   /* +0x04: rising ones */
  uint16_t enable;                /* +0x06: events reported to the parent */
  uint16_t event;                 /* +0x08: latched, cleared as read */
  struct status_register* parent; /* +0x0a: the register it reports into */
  int16_t parent_bit;             /* +0x0e: at this bit */
} status_register;
