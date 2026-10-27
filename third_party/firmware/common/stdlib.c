/* The C library's general utilities, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `platform/stdlib`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

/* |j|. */
int32_t abs(int32_t j) { return j < 0 ? -j : j; }

/* ctype_table, each character's ctype bits indexed by the
 * character plus one, so that EOF, -1, has a slot, is the image's: atoi
 * indexes it by a sign-extended character (atoi+0x10), which reads the
 * 128 bytes before it, its own code, for 0x80 to 0xfe. */

/* A decimal integer after any spaces and a sign.  It counts
 * down from zero, so that the most negative int parses. */
int32_t atoi(char* s) {
  bool negative = false;
  int32_t c = *s;

  if ((ctype_table[c + 1] & LIBC_CTYPE_DIGIT) == 0) {
    while ((ctype_table[c + 1] & LIBC_CTYPE_SPACE) != 0) {
      c = *++s;
    }
    if (c == '-') {
      negative = true;
      c = *++s;
    } else if (c == '+') {
      c = *++s;
    }
    if ((ctype_table[c + 1] & LIBC_CTYPE_DIGIT) == 0) {
      return 0;
    }
  }
  int32_t value = '0' - c;

  for (c = *++s; (ctype_table[c + 1] & LIBC_CTYPE_DIGIT) != 0; c = *++s) {
    value = value * 10 + ('0' - c);
  }
  return negative ? value : -value;
}

/* The quotient in the high word, D0, and the remainder in the low, D1. */
uint64_t div(int32_t numerator, int32_t denominator) {
  int32_t quotient = numerator / denominator;
  int32_t remainder = numerator - denominator * quotient;

  return (uint64_t)quotient << CPU_LONG_BITS | (uint32_t)remainder;
}

/* The same, ldiv. */
int64_t ldiv(int32_t numerator, int32_t denominator) {
  return div(numerator, denominator);
}

/* |j|, labs. */
int32_t labs(int32_t j) { return abs(j); }

/* Swaps two of qsort's elements. */
void qsort_swap(uint8_t* a, uint8_t* b) {
  uint32_t count = qsort_size;

  do {
    uint8_t byte = *a;

    *a++ = *b;
    *b++ = byte;
  } while (--count != 0);
}

/* Rotates three of qsort's elements: a's to b, b's to c, c's to a. */
void qsort_rotate(uint8_t* a, uint8_t* b, uint8_t* c) {
  uint32_t count = qsort_size;

  do {
    uint8_t byte = *a;

    *a++ = *c;
    *c++ = *b;
    *b++ = byte;
  } while (--count != 0);
}

/* Sorts the elements from first up to end by qsort_compare, a
 * three-way partition about the middle element, then the smaller side
 * by recursion and the larger by iteration.  While it
 * partitions, [first, low) is less than the pivot, [equal_low,
 * equal_high] equal to it and (high, end) greater; [low, equal_low)
 * and (equal_high, high] are yet to be compared. */
void qsort_range(uint8_t* first, uint8_t* end) {
  uint32_t size = qsort_size;

  while ((uint32_t)(end - first) > size) {
    uint8_t* low = first;
    uint8_t* equal_low = first + size * ((uint32_t)(end - first) / (size * 2));
    uint8_t* equal_high = equal_low;
    uint8_t* high = end - size;

    for (;;) {
      if (low < equal_low) {
        int32_t order = qsort_compare(low, equal_low);

        if (order == 0) {
          equal_low -= size;
          qsort_swap(low, equal_low);
          continue;
        }
        if (order < 0) {
          low += size;
          continue;
        }
      }
      /* *low, if below equal_low, belongs above the pivot. */
      bool exchanged = false;

      while (!exchanged && equal_high < high) {
        int32_t order = qsort_compare(equal_high, high);

        if (order == 0) {
          equal_high += size;
          qsort_swap(equal_high, high);
        } else if (order < 0) {
          high -= size;
        } else if (low != equal_low) {
          qsort_swap(low, high);
          high -= size;
          low += size;
          exchanged = true;
        } else {
          equal_high += size;
          qsort_rotate(low, equal_high, high);
          low = equal_low = equal_low + size;
        }
      }
      if (exchanged) {
        continue;
      }
      if (low == equal_low) {
        break;
      }
      equal_low -= size;
      qsort_rotate(high, equal_low, low);
      equal_high -= size;
      high = equal_high;
    }
    if (equal_low - first < end - equal_high) {
      qsort_range(first, equal_low);
      first = equal_high + size;
    } else {
      qsort_range(equal_high + size, end);
      end = equal_low;
    }
  }
}

/* Sorts nmemb elements of size bytes by compar, leaving compar and
 * size where qsort_range reads them. */
void qsort(void* base, uint32_t nmemb, uint32_t size,
           qsort_compare_function* compar) {
  qsort_compare = compar;
  qsort_size = size;
  qsort_range(base, (uint8_t*)base + size * nmemb);
}

/* A pseudo-random number in 0 to 32767. */
int32_t rand(void) {
  /* rand's generator, the ANSI C example's. */
  static const uint32_t RAND_MULTIPLIER = 1103515245;
  static const uint32_t RAND_INCREMENT = 12345;
  static const uint32_t RAND_SHIFT = 16;
  static const uint32_t RAND_MASK = 0x7fff;

  rand_seed = rand_seed * RAND_MULTIPLIER + RAND_INCREMENT;
  return rand_seed >> RAND_SHIFT & RAND_MASK;
}

/* Registers a function for call_atexit_functions: 1 for NULL or a full
 * table, else 0; nothing calls it. */
int32_t atexit(atexit_function* function) {
  static const uint16_t FUNCTIONS_MOST =
      sizeof atexit_functions / sizeof atexit_functions[0];

  if (function == NULL || atexit_count == FUNCTIONS_MOST) {
    return 1;
  }
  atexit_functions[(int16_t)atexit_count++] = function;
  return 0;
}

/* Calls the functions atexit registered, the last first; the count is
 * signed. */
void call_atexit_functions(void) {
  while ((int16_t)atexit_count > 0) {
    atexit_count--;
    atexit_functions[(int16_t)atexit_count]();
  }
}

/* Builds "Prog end, returned N" in program_end_message and stops with
 * fatal_error_with_code, whose code is return_address, program_exit's;
 * never returns.  The digits are built backward from a stop
 * byte at the buffer's end, where they stay. */
void program_end_report(uint32_t value, uint32_t return_address) {
  uint8_t* digit = &program_end_message[LIBC_PROGRAM_END_DIGITS_END];
  uint32_t magnitude = value;

  program_end_message[0] = 0;
  for (const char* text = "Prog end, returned "; *text != '\0'; text++) {
    libc_message_append(program_end_message, *text);
  }
  *digit = LIBC_PROGRAM_END_DIGITS_STOP;
  if ((int32_t)value < 0) {
    magnitude = -value;
    libc_message_append(program_end_message, '-');
  }
  do {
    *--digit = magnitude % 10;
    magnitude /= 10;
  } while (magnitude != 0);
  for (; *digit != LIBC_PROGRAM_END_DIGITS_STOP; digit++) {
    libc_message_append(program_end_message, '0' + *digit);
  }
  fatal_error_with_code((char*)program_end_message,
                        (uint32_t)__builtin_return_address(0), value,
                        return_address);
  for (;;) {
  }
}

/* Runs the atexit functions and reports the exit value with its own
 * return address; never returns. */
void program_exit(uint32_t value) {
  call_atexit_functions();
  program_end_report(value, (uint32_t)__builtin_return_address(0));
  for (;;) {
  }
}

/* Saves its caller's registers, frame, return and stack, and through
 * trap #1 the status register, in a buffer; returns 0, and again the
 * value longjmp gives. */
uint32_t setjmp(uint32_t* buffer) {
  libc_setjmp_save();
  return 0;
}

/* Restores the registers setjmp saved in a buffer, and through trap #1
 * its status register, and returns to setjmp's caller with value in D0. */
uint64_t longjmp(uint32_t* buffer, uint32_t value) { libc_longjmp_restore(); }
