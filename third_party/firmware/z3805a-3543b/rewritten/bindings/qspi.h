/* The QSPI device's entries and its raw transfer, as C calls them: each
 * a wrapper under the routine's own name and prototype that binds its
 * arguments to the routine's registers and makes the jsr in one asm
 * statement, naming as changed each register the routine changes
 * (softfloat.h).  qspi_control and qspi_open set D7, no error, and
 * change nothing else (0x208fa, 0x208fe).  qspi_transfer_raw and
 * qspi_device_write call qspi_acquire and qspi_start, C that keeps D2
 * to D7 and A2 to A6 (their link and saves at 0x20874 and 0x2081e) and
 * changes D0, D1, A0 and A1 as its callees may; qspi_transfer_raw
 * keeps D0 and A1 across qspi_acquire only (0x20a62), so that each
 * changes D0, D1, A0 and A1, and the QSPI's state.  Each waits for the
 * transfer's interrupt to clear its flag (0x20aa6, 0x20866).  No C calls
 * them: the kernel's I/O dispatch does, through the I/O table (0x51744
 * to 0x5175c), and the QSPI routines' assembly. */

/* The device's control: nothing (0x208fa). */
__attribute__((always_inline)) static inline int32_t qspi_control(void) {
  register int32_t d7 __asm__("d7");

  __asm__ volatile("jsr qspi_control" : "=r"(d7) : : "cc", "memory");
  return d7;
}

/* The device's open and close: nothing (0x208fe). */
__attribute__((always_inline)) static inline int32_t qspi_open(void) {
  register int32_t d7 __asm__("d7");

  __asm__ volatile("jsr qspi_open" : "=r"(d7) : : "cc", "memory");
  return d7;
}

/* Takes the QSPI and runs a transfer of count words for device, D0, its
 * buffers' pointers at A1, and waits for it (0x20a62). */
__attribute__((always_inline)) static inline void qspi_transfer_raw(
    char device, void* buffers, int16_t count) {
  register int32_t d0 __asm__("d0") = device;
  register void* a1 __asm__("a1") = buffers;
  register int32_t d2 __asm__("d2") = count;

  __asm__ volatile("jsr qspi_transfer_raw"
                   : "+r"(d0), "+r"(a1)
                   : "r"(d2)
                   : "d1", "a0", "cc", "memory");
}

/* The device's write: takes the QSPI and sends count words from buffers,
 * A0, to device, D0, and waits for it (0x20ab0). */
__attribute__((always_inline)) static inline void qspi_device_write(
    char device, void* buffers, int16_t count) {
  register int32_t d0 __asm__("d0") = device;
  register void* a0 __asm__("a0") = buffers;
  register int32_t d2 __asm__("d2") = count;

  __asm__ volatile("jsr qspi_device_write"
                   : "+r"(d0), "+r"(a0)
                   : "r"(d2)
                   : "d1", "a1", "cc", "memory");
}
