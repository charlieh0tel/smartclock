/* klok_event_class and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A klok_entry's event class, which klok_find looks entries up by:
 * the leap second's (leap_event_delete, leap_event_delete) and the lifetime
 * record's hourly save. */
typedef enum __attribute__((packed)) klok_event_class {
  KLOK_EVENT_LEAP = 1,
  KLOK_EVENT_LIFETIME = 2,
} klok_event_class;
