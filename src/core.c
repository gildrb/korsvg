#include "internal.h"

#include <stdarg.h>
#include <stdio.h>

int32_t hermeneus_parser_set_error(char *error, size_t capacity,
                                    const char *format, ...) {
  va_list arguments;

  if (error != NULL && capacity > 0) {
    va_start(arguments, format);
    vsnprintf(error, capacity, format, arguments);
    va_end(arguments);
  }
  return 0;
}

int32_t hermeneus_parser_checked_multiply(size_t left, size_t right,
                                          size_t *result) {
  if (left != 0 && right > SIZE_MAX / left) {
    return 0;
  }
  *result = left * right;
  return 1;
}
