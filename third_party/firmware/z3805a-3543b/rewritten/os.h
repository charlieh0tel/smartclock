/* What the kernel's binding shares with the installer's copy of it
 * (os.c, installer_os.c).  The exported C includes the hand-written
 * headers after its own declarations and before the hand-written .c
 * files, so every one of them can use it. */

/* The registers a call through the binding gives the kernel and takes
 * back (docs/firmware/psos.md, "Calls"): psos_call (0x16f1e), which
 * makes trap #3, and psos_io_call (0x16f36), trap #4, and the
 * installer's copies (installer::0x102a72, installer::0x102a8a), take a
 * service's or I/O function's arguments in D0 to D3, A0 and A1 and
 * leave its results in them; they return D7 and the condition codes.  A
 * register the service does not read is not loaded, and one it does
 * not change comes back as it was given. */
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
  /* D7 not 0 after the call, the call failed: Z clear, which the
   * image's stubs branch on, set from D7 by the binding's own last move
   * to it (bne at 0x16fda after 0x16f2c's move.w) or by their move of it
   * to D0 (bne at 0x171ea) */
  bool failed;
} os_registers;

/* A register's part in a service's call, its shape: NONE, the service
 * neither reads nor changes it, IN, it reads it, OUT, it changes it, and
 * IN_OUT, both; each expands to the register's declaration, its asm
 * operand among the outputs or the inputs, if any, and its value after
 * the call. */
#define OS_DECLARE_NONE(type, variable, name, value)
#define OS_DECLARE_IN(type, variable, name, value) \
  register type variable __asm__(name) = (value)
#define OS_DECLARE_OUT(type, variable, name, value) \
  register type variable __asm__(name)
#define OS_DECLARE_IN_OUT(type, variable, name, value) \
  register type variable __asm__(name) = (value)
#define OS_OUTPUT_NONE(variable)
#define OS_OUTPUT_IN(variable)
#define OS_OUTPUT_OUT(variable) , "=r"(variable)
#define OS_OUTPUT_IN_OUT(variable) , "+r"(variable)
#define OS_INPUT_NONE(variable)
#define OS_INPUT_IN(variable) , "r"(variable)
#define OS_INPUT_OUT(variable)
#define OS_INPUT_IN_OUT(variable)
#define OS_AFTER_NONE(variable, value) (value)
#define OS_AFTER_IN(variable, value) (value)
#define OS_AFTER_OUT(variable, value) (variable)
#define OS_AFTER_IN_OUT(variable, value) (variable)

/* A shape's outputs, after D7, and its inputs, after an immediate the
 * template does not use, which opens them, since the shape may leave
 * them empty. */
#define OS_OUTPUTS(shape_d0, shape_d1, shape_d2, shape_d3, shape_a0, shape_a1) \
  "+r"(os_d7)OS_OUTPUT_##shape_d0(os_d0) OS_OUTPUT_##shape_d1(os_d1)           \
      OS_OUTPUT_##shape_d2(os_d2) OS_OUTPUT_##shape_d3(os_d3)                  \
          OS_OUTPUT_##shape_a0(os_a0) OS_OUTPUT_##shape_a1(os_a1)
#define OS_INPUTS(shape_d0, shape_d1, shape_d2, shape_d3, shape_a0, shape_a1) \
  [unused] "i"(0)OS_INPUT_##shape_d0(os_d0) OS_INPUT_##shape_d1(os_d1)        \
      OS_INPUT_##shape_d2(os_d2) OS_INPUT_##shape_d3(os_d3)                   \
          OS_INPUT_##shape_a0(os_a0) OS_INPUT_##shape_a1(os_a1)

/* Calls routine, the binding's psos_call or psos_io_call, the
 * primary's or the installer's, with the service or function number in
 * D7 and the registers arguments gives, in one asm statement, so that
 * what it returns stays in the caller's registers as the image's stubs
 * keep it, and whether it failed is D7's being not 0, as the stubs
 * read Z, set from D7 (os_registers.failed): a flag of its own, read
 * with sne, would take a data register the stubs leave alone, D2 or D3
 * where the service keeps both, which gcc would then save (psos_create_x
 * at 0x171d2 saves D2 and D7 alone).  The
 * call loads D0 to D3, A0 and A1 as the service's shape says, its
 * parts d0 to a1, names as changed those the service changes, D7 and
 * the condition codes, and keeps the rest, so that gcc keeps a value
 * across a service that leaves its register as it was, as the image's
 * stubs do, which save only D7 and what they use of D2 and D3
 * (psos_get_time at 0x17374, psos_service_21 at 0x17202). */
#define OS_BINDING_CALL(routine, number, arguments, shape_d0, shape_d1,   \
                        shape_d2, shape_d3, shape_a0, shape_a1)           \
  ({                                                                      \
    const os_registers os_arguments = (arguments);                        \
    OS_DECLARE_##shape_d0(uint32_t, os_d0, "d0", os_arguments.d0);        \
    OS_DECLARE_##shape_d1(uint32_t, os_d1, "d1", os_arguments.d1);        \
    OS_DECLARE_##shape_d2(uint32_t, os_d2, "d2", os_arguments.d2);        \
    OS_DECLARE_##shape_d3(uint32_t, os_d3, "d3", os_arguments.d3);        \
    OS_DECLARE_##shape_a0(void*, os_a0, "a0", os_arguments.a0);           \
    OS_DECLARE_##shape_a1(void*, os_a1, "a1", os_arguments.a1);           \
    register uint32_t os_d7 __asm__("d7") = (number);                     \
                                                                          \
    __asm__ volatile("jsr " routine                                       \
                     : OS_OUTPUTS(shape_d0, shape_d1, shape_d2, shape_d3, \
                                  shape_a0, shape_a1)                     \
                     : OS_INPUTS(shape_d0, shape_d1, shape_d2, shape_d3,  \
                                 shape_a0, shape_a1)                      \
                     : "cc", "memory");                                   \
    (os_registers){OS_AFTER_##shape_d0(os_d0, os_arguments.d0),           \
                   OS_AFTER_##shape_d1(os_d1, os_arguments.d1),           \
                   OS_AFTER_##shape_d2(os_d2, os_arguments.d2),           \
                   OS_AFTER_##shape_d3(os_d3, os_arguments.d3),           \
                   OS_AFTER_##shape_a0(os_a0, os_arguments.a0),           \
                   OS_AFTER_##shape_a1(os_a1, os_arguments.a1),           \
                   os_d7,                                                 \
                   os_d7 != 0};                                           \
  })

/* Each service's shape, X(routine, service, D0, D1, D2, D3, A0, A1).
 * What a service reads is what the harness's model of the kernel
 * records (tools/firmware/harness.py, KERNEL_SERVICES, from the stubs'
 * loads and the handlers' reads); what it changes is read from its
 * handler, the table at 0x14b6e's, and its calls: the trap's entry keeps
 * D6, A5 and A6 around it (0x14b26, 0x14b62), and a task switch keeps
 * the rest.  So A0 changes in each service that checks a task, an
 * exchange or a segment by its id, whose check gives the running task's
 * record for 0 (0x15b68); D0 in each that may wait, whose wait counts
 * the ticks down in it (0x15be2), create's and allocate's for a region's
 * memory (0x15124); D1 in request_x, set from an interrupt (0x156e8);
 * mode's D1 is complemented and complemented back (0x158e4, 0x158ea), and
 * create partition keeps every register it uses (0x15a10, 0x15a4a), its
 * partition's id the area in A0. */
#define OS_SERVICES(X, routine)                                                \
  X(routine, PSOS_SERVICE_VERSION, NONE, NONE, NONE, NONE, NONE, NONE)         \
  X(routine, PSOS_SERVICE_SPAWN, IN_OUT, IN, IN, IN, IN_OUT, NONE)             \
  X(routine, PSOS_SERVICE_START, NONE, NONE, NONE, NONE, IN_OUT, IN)           \
  X(routine, PSOS_SERVICE_IDENT, IN_OUT, NONE, NONE, NONE, OUT, NONE)          \
  X(routine, PSOS_SERVICE_DELETE, NONE, NONE, NONE, NONE, IN, NONE)            \
  X(routine, PSOS_SERVICE_SUSPEND, NONE, NONE, NONE, NONE, IN_OUT, NONE)       \
  X(routine, PSOS_SERVICE_RESUME, NONE, NONE, NONE, NONE, IN_OUT, NONE)        \
  X(routine, PSOS_SERVICE_PRIORITY, IN_OUT, NONE, NONE, NONE, IN_OUT, NONE)    \
  X(routine, PSOS_SERVICE_ALLOCATE_SEGMENT, IN_OUT, IN, IN, IN, OUT, NONE)     \
  X(routine, PSOS_SERVICE_FREE_SEGMENT, NONE, NONE, NONE, NONE, IN, NONE)      \
  X(routine, PSOS_SERVICE_ASSIGN_SEGMENT, NONE, NONE, NONE, NONE, IN_OUT, IN)  \
  X(routine, PSOS_SERVICE_RECLAIM_SEGMENT, NONE, NONE, NONE, NONE, IN_OUT, IN) \
  X(routine, PSOS_SERVICE_BROADCAST_X, IN_OUT, OUT, NONE, NONE, IN, IN)        \
  X(routine, PSOS_SERVICE_JAM_X, NONE, NONE, NONE, NONE, IN, IN)               \
  X(routine, PSOS_SERVICE_CREATE_X, IN, IN, IN, NONE, OUT, NONE)               \
  X(routine, PSOS_SERVICE_ATTACH_X, IN, OUT, OUT, NONE, OUT, NONE)             \
  X(routine, PSOS_SERVICE_DELETE_X, NONE, NONE, NONE, NONE, IN, NONE)          \
  X(routine, PSOS_SERVICE_SEND_X, NONE, NONE, NONE, NONE, IN, IN)              \
  X(routine, PSOS_SERVICE_REQUEST_X, IN_OUT, IN_OUT, NONE, NONE, IN, IN)       \
  X(routine, PSOS_SERVICE_SIGNAL_V, NONE, IN, NONE, NONE, IN_OUT, NONE)        \
  X(routine, PSOS_SERVICE_WAIT_V, IN_OUT, IN_OUT, IN, NONE, NONE, NONE)        \
  X(routine, PSOS_SERVICE_GET_V, OUT, IN_OUT, IN, NONE, NONE, NONE)            \
  X(routine, PSOS_SERVICE_PAUSE, IN_OUT, NONE, NONE, NONE, NONE, NONE)         \
  X(routine, PSOS_SERVICE_ANNOUNCE_TICK, NONE, NONE, NONE, NONE, NONE, NONE)   \
  X(routine, PSOS_SERVICE_GET_TIME, OUT, OUT, OUT, NONE, NONE, NONE)           \
  X(routine, PSOS_SERVICE_SET_TIME, IN, IN, IN, NONE, NONE, NONE)              \
  X(routine, PSOS_SERVICE_MODE, IN_OUT, IN, NONE, NONE, NONE, NONE)            \
  X(routine, PSOS_SERVICE_CREATE_PARTITION, IN, IN, NONE, NONE, IN, NONE)      \
  X(routine, PSOS_SERVICE_DELETE_PARTITION, IN, NONE, NONE, NONE, IN, NONE)    \
  X(routine, PSOS_SERVICE_GET_BLOCK, IN, NONE, NONE, NONE, IN, OUT)            \
  X(routine, PSOS_SERVICE_RETURN_BLOCK, NONE, NONE, NONE, NONE, IN, IN)

/* Each I/O function's shape, the union of its four devices' (the table
 * at 0x5171a): each takes the device in D0, open a long in D1, read the
 * buffer in A1 and the length in D2, write the buffer in A0 and the
 * length in D2, control two longs in D2 and D3; init, open and close
 * change nothing but D7 (0x1d93e, 0x20902, 0x1af84, 0x1fdb6 and their
 * neighbors); read, write and control change D0, D1, A0 and A1, through
 * the C routines DUART channel B's and the SCI's call (drtb_read's
 * drtb_gets, 0x1dde2), read the length in D2 and its end in A1, write
 * the length and the end in A0. */
#define OS_IO_FUNCTIONS(X, routine)                                 \
  X(routine, PSOS_IO_INIT, IN, NONE, NONE, NONE, NONE, NONE)        \
  X(routine, PSOS_IO_OPEN, IN, IN, NONE, NONE, NONE, NONE)          \
  X(routine, PSOS_IO_CLOSE, IN, NONE, NONE, NONE, NONE, NONE)       \
  X(routine, PSOS_IO_READ, IN_OUT, OUT, IN_OUT, NONE, OUT, IN_OUT)  \
  X(routine, PSOS_IO_WRITE, IN_OUT, OUT, IN_OUT, NONE, IN_OUT, OUT) \
  X(routine, PSOS_IO_CONTROL, IN_OUT, OUT, IN, IN, OUT, OUT)

/* A case of a binding's switch: the call in its service's shape. */
#define OS_SERVICE_CASE(routine, service, shape_d0, shape_d1, shape_d2,     \
                        shape_d3, shape_a0, shape_a1)                       \
  case service:                                                             \
    return OS_BINDING_CALL(routine, service, arguments, shape_d0, shape_d1, \
                           shape_d2, shape_d3, shape_a0, shape_a1);

/* Each binding calls the service its caller names, always a constant,
 * in that service's shape: the switch is folded to the one call. */
__attribute__((always_inline)) static inline os_registers os_psos_call(
    psos_service service, os_registers arguments) {
  switch (service) { OS_SERVICES(OS_SERVICE_CASE, "psos_call") }
  __builtin_unreachable();
}

__attribute__((always_inline)) static inline os_registers os_psos_io_call(
    psos_io_function function, os_registers arguments) {
  switch (function) { OS_IO_FUNCTIONS(OS_SERVICE_CASE, "psos_io_call") }
  __builtin_unreachable();
}

__attribute__((always_inline)) static inline os_registers
os_installer_psos_call(psos_service service, os_registers arguments) {
  switch (service) { OS_SERVICES(OS_SERVICE_CASE, "installer_psos_call") }
  __builtin_unreachable();
}

__attribute__((always_inline)) static inline os_registers
os_installer_psos_io_call(psos_io_function function, os_registers arguments) {
  switch (function) {
    OS_IO_FUNCTIONS(OS_SERVICE_CASE, "installer_psos_io_call")
  }
  __builtin_unreachable();
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
  return supervisor << CPU_WORD_BITS | (user & CPU_WORD_MASK);
}

/* The kernel's ticks a second, and the longest pause sleep_seconds
 * takes, 2^21 seconds. */
static const float OS_TICKS_PER_SECOND = 1024;

static const float OS_SLEEP_LONGEST = 1 << 21;

/* The spool task's stack sizes in words (supervisor and user), its
 * exchange's create_x arguments, which psos.md leaves unnamed, and the
 * ticks it waits for a message before it prints the deferred ones
 * instead (structures.md, "Others"); spoo_task_create's and
 * installer_spoo_task_create's (0x20c96). */
static const uint32_t SPOOL_SUPERVISOR_STACK = 0x600;

static const uint32_t SPOOL_USER_STACK = 0x10;

static const uint32_t SPOOL_EXCHANGE_A = 2;

static const uint32_t SPOOL_EXCHANGE_B = 5;

static const uint32_t SPOOL_WAIT_TICKS = 0x400;

/* The bytes between a task's supervisor stack's top and its user stack,
 * which stack_measure skips (0x1321a). */
static const uint32_t OS_USER_STACK_GAP = 4;

/* A service's result's low byte, the kernel's error (psos_error). */
static const uint32_t OS_ERROR_BITS = 0xff;

/* The task a service's task argument of 0 is: the caller's own
 * (docs/firmware/psos.md, service 10; console_halt_hook deletes its own
 * task so, 0x12f50). */
static const uint32_t OS_TASK_SELF = 0;

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
