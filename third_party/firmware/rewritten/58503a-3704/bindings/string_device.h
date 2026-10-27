/* The pForth console's string device's entries, as C calls them: each a
 * wrapper under the routine's own name and prototype that binds its
 * argument to its register and makes the jsr in one asm statement,
 * naming as changed each register the routine changes (softfloat.h).
 * Each sets D7, no error, and changes nothing else; init and control
 * write string_device_cursor (0x1b0e4 to 0x1b0fc).  No C calls them:
 * the kernel's I/O dispatch does, through the I/O table (0x54468 to
 * 0x54486; docs/firmware/console.md). */

/* Nothing in, D7, no error, the result. */
#define STRING_DEVICE_CALL(routine)                  \
  ({                                                 \
    register int32_t string_device_d7 __asm__("d7"); \
                                                     \
    __asm__ volatile("jsr " routine                  \
                     : "=r"(string_device_d7)        \
                     :                               \
                     : "cc", "memory");              \
    string_device_d7;                                \
  })

/* The device's open and close: nothing (0x1b0e4). */
__attribute__((always_inline)) static inline int32_t string_device_open(void) {
  return STRING_DEVICE_CALL("string_device_open");
}

/* The device's init: clears the string (0x1b0e8). */
__attribute__((always_inline)) static inline int32_t string_device_init(void) {
  return STRING_DEVICE_CALL("string_device_init");
}

/* The device's control: the string console_read_from_string reads,
 * D2 (0x1b0f2). */
__attribute__((always_inline)) static inline int32_t string_device_control(
    char* string) {
  register char* d2 __asm__("d2") = string;
  register int32_t d7 __asm__("d7");

  __asm__ volatile("jsr string_device_control"
                   : "=r"(d7)
                   : "r"(d2)
                   : "cc", "memory");
  return d7;
}
