/* The MC68331's general-purpose timer as the loop and the kernel's
 * vectors use it directly, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `devices/timers`).  The exported C includes this file after its own,
 * so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype.  The registers are mc68331.h's. */
