/* selftest_test and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C". */

/* A self-test, in selftest_table and selftest_list: run is 1 to run the
 * test, as selftest_execute's second call and selftest_run_all_counted
 * pass it, and 0 otherwise. */
typedef void selftest_test(char_slot run);
/* An entry of the self-test tables. */
typedef selftest_test* selftest_entry;
