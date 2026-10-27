/* status_register_mask and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* status_register_mask: which of a status register's masks
 * status_preset_register sets, by its jump table at
 * status_preset_register+0x38; any other value sets none
 * (status_preset_register+0x2a).  status_enable_restore passes the enable
 * (status_enable_restore+0x1e), status_ptr_restore and status_ntr_restore a
 * filtered register's transition filters, by its index less 2
 * (status_ptr_restore+0x1e, status_ntr_restore+0x1e). */
typedef enum __attribute__((packed)) status_register_mask {
  STATUS_MASK_ENABLE = 0,
  STATUS_MASK_POSITIVE_TRANSITION = 1,
  STATUS_MASK_NEGATIVE_TRANSITION = 2,
} status_register_mask;
