/* The C library's function types and the shapes of printf and
 * console_write, as the images' smartclock.h declare them (each
 * includes this header): docs/firmware/rewrite.md, "Shared C". */

/* printf's shape, for the routines that take one to print with. */
typedef int printf_function(char* format, ...);

/* console_write's shape, for the routines that take one to write a
 * string with: log_print and status_screen. */
typedef unsigned long write_function(char* s);

/* qsort's comparator. */
typedef int qsort_compare_function(void* a, void* b);

/* A function atexit registers and call_atexit_functions calls, the
 * last first (atexit_functions, 32 of them). */
typedef void atexit_function(void);
