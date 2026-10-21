/* The signals the firmware's drivers send their tasks: each a bit of
 * the events signal_v ORs into a task's pending word, which wait_v
 * waits on and get_v clears (docs/firmware/psos.md, "Inside the
 * kernel").  The names are the firmware's own, from the messages it
 * gives when a service fails, each quoted beside its value.  The bits
 * are a task's, so one bit can mean different things to different
 * tasks -- 0x800 is SIGINTR to the console's and SIG_SOQA to the GPS
 * task, 0x200 the SCI's output queue and the interpolator's reading --
 * and each name lists, after "in:", the functions of the driver its
 * message comes from, where it means that.  The exporter copies these
 * #defines into the C (defines=) and names a signal a call to
 * signal_v, wait_v or get_v passes only in those functions. */

#define SIG_SIQ_SCI 0x0040  /* "wait_v SIG_SIQ_SCI error in getc"; in: sci_getc */
#define SIG_SOQ_SCI 0x0200  /* "wait_v SIG_SOQ_SCI error in _sci_puts"; in: sci_puts */
#define SIG_QSPI    0x0400  /* "wait_v SIG_QSPI"; in: qspi_acquire qspi_release qspi_isr */
#define SIGINTR     0x0800  /* "DUARTB isr signaling SIGINTR"; in: duartb_isr installer_duartb_isr sci_gets */
#define SIG_SOQA    0x0800  /* "wait_v SIG_SOQA error in drta_send_message"; in: drta_send_message duarta_tx_interrupt */
#define SIG_SOQ     0x1000  /* "wait_v SIG_SOQ error in _drtb_puts"; in: drtb_puts duartb_transmit_interrupt installer_duartb_transmit_interrupt */
#define SIG_SIQ     0x4000  /* "wait_v SIG_SIQ error in getc"; in: drtb_getc installer_drtb_getc drta_get_byte duarta_isr duartb_isr installer_duartb_isr */
