/* What the kernel's binding shares with the installer's copy of it
 * (os.c, installer_os.c).  The exported C includes the hand-written
 * headers after its own declarations and before the hand-written .c
 * files, so every one of them can use it. */

/* The registers a call through the binding gives the kernel and takes
 * back (docs/firmware/psos.md, "Calls"): psos_call (0x16f1e), which
 * makes trap #3, and psos_io_call (0x16f36), trap #4, and the
 * installer's copies (installer::0x102a72, installer::0x102a8a), take a
 * service's or I/O function's arguments in D0 to D3, A0 and A1 and
 * leave its results in them, a register it does not return as it was;
 * they return D7 and the condition codes.  A register the stubs do not
 * load is passed as 0. */
typedef struct os_registers {
  uint32_t d0;
  uint32_t d1;
  uint32_t d2;
  uint32_t d3;
  void* a0;
  void* a1;
  /* D7 after the call: 0, or the service or I/O function and its error
   * (0x16f26 to 0x16f2c, 0x16f42 to 0x16f46) */
  uint32_t error;
  /* Z clear after the call, which the image's stubs branch on (bne at
   * 0x16fda): the call failed, D7 not 0 */
  bool failed;
} os_registers;

/* Calls routine, the binding's psos_call or psos_io_call, the
 * primary's or the installer's, with the service or function number in
 * D7 and the registers arguments gives, in one asm statement, so that
 * what it returns stays in the caller's registers as the image's stubs
 * keep it, and Z is read with sne before anything sets it again.  The
 * call changes D0 to D3, A0, A1, D7 and the condition codes and keeps the
 * rest: the binding's contract, which the image's stubs rely on, saving
 * only D7 and what they use of D2 and D3 (psos_get_time at 0x17374,
 * psos_service_21 at 0x17202), and the harness's model of the kernel,
 * which sets only those registers (harness.KERNEL_SERVICES); the
 * kernel's own entry keeps D6, A5 and A6 for its handlers (0x14b26,
 * 0x14b62). */
#define OS_BINDING_CALL(routine, number, arguments)                        \
  ({                                                                       \
    const os_registers os_arguments = (arguments);                         \
    register uint32_t os_d0 __asm__("d0") = os_arguments.d0;               \
    register uint32_t os_d1 __asm__("d1") = os_arguments.d1;               \
    register uint32_t os_d2 __asm__("d2") = os_arguments.d2;               \
    register uint32_t os_d3 __asm__("d3") = os_arguments.d3;               \
    register void* os_a0 __asm__("a0") = os_arguments.a0;                  \
    register void* os_a1 __asm__("a1") = os_arguments.a1;                  \
    register uint32_t os_d7 __asm__("d7") = (number);                      \
    uint8_t os_failed;                                                     \
                                                                           \
    __asm__ volatile("jsr " routine "\n\tsne %[failed]"                    \
                     : "+r"(os_d0), "+r"(os_d1), "+r"(os_d2), "+r"(os_d3), \
                       "+r"(os_a0), "+r"(os_a1),                           \
                       "+r"(os_d7), [failed] "=d"(os_failed)               \
                     :                                                     \
                     : "cc", "memory");                                    \
    (os_registers){os_d0, os_d1, os_d2, os_d3,                             \
                   os_a0, os_a1, os_d7, os_failed != 0};                   \
  })

__attribute__((always_inline)) static inline os_registers os_psos_call(
    psos_service service, os_registers arguments) {
  return OS_BINDING_CALL("psos_call", service, arguments);
}

__attribute__((always_inline)) static inline os_registers os_psos_io_call(
    psos_io_function function, os_registers arguments) {
  return OS_BINDING_CALL("psos_io_call", function, arguments);
}

__attribute__((always_inline)) static inline os_registers
os_installer_psos_call(psos_service service, os_registers arguments) {
  return OS_BINDING_CALL("installer_psos_call", service, arguments);
}

__attribute__((always_inline)) static inline os_registers
os_installer_psos_io_call(psos_io_function function, os_registers arguments) {
  return OS_BINDING_CALL("installer_psos_io_call", function, arguments);
}

/* The name a stub was given, its first argument, packed into a long:
 * psos_pack_name (0x16f50), or the installer's (installer::0x102aa4),
 * reads it at 8(A6), its caller's frame, and changes D0, A0 and the
 * condition codes.  Called in line in the stub, as the image's stubs call
 * it (bsr at 0x16fd0), so that A6 is the stub's frame. */
#define OS_PACK_NAME(routine)                                                  \
  ({                                                                           \
    register uint32_t os_name __asm__("d0");                                   \
    __asm__ volatile("jsr " routine : "=r"(os_name) : : "a0", "cc", "memory"); \
    os_name;                                                                   \
  })

/* A task's supervisor and user stack sizes, in words, as spawn takes
 * them in D3: a word each, the supervisor's high (psos.md, service 1). */
static uint32_t os_stack_sizes(uint32_t supervisor, uint32_t user) {
  static const uint32_t WORD_BITS = 16;
  static const uint32_t WORD_MASK = 0xffff;

  return supervisor << WORD_BITS | (user & WORD_MASK);
}

/* What exception_hook_per_task holds while set, and what
 * exception_hook_set returns when a hook was set before (0x118b6,
 * 0x118d8). */
static const uint8_t OS_SWITCH_ON = 0xff;

/* mutex_owner's and its installer copy's body: the task holding a
 * mutex. */
static uint32_t os_mutex_owner(void* lock) { return ((mutex*)lock)->owner; }

/* busy_wait's and its installer copy's body: spins 729 empty loops for
 * each count, the count a word once it is not 0. */
static void os_busy_wait(uint32_t count) {
  /* busy_wait's empty loops for each count (0x13432). */
  static const uint16_t BUSY_WAIT_SPINS = 0x2d9;

  if (count == 0) {
    return;
  }
  uint16_t remaining = count;

  do {
    for (uint16_t spin = BUSY_WAIT_SPINS; spin != 0; spin--) {
      __asm__ volatile("");
    }
    remaining--;
  } while (remaining != 0);
}
