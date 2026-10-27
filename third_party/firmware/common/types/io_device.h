/* The kernel's I/O devices, as the images' smartclock.h declare them
 * (each includes this header where they stand): docs/firmware/rewrite.md,
 * "Shared C". */

/* A kernel I/O device, by kernel_dispatch_io's major number in
 * the high byte; the QSPI's minor, a qspi_device, goes in the low. */
typedef enum io_device {
  IO_DEVICE_DUART_B = 0x000,
  IO_DEVICE_QSPI = 0x100,
  IO_DEVICE_CONSOLE_STRING = 0x200,
  IO_DEVICE_SCI = 0x300,
} io_device;
