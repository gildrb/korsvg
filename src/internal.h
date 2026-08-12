#ifndef HERMENEUS_PARSER_INTERNAL_H
#define HERMENEUS_PARSER_INTERNAL_H

#include "parser.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float f32;
typedef double f64;

typedef HermeneusParserImage Image;

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

int32_t hermeneus_parser_set_error(char *error, size_t capacity,
                                    const char *format, ...);
int32_t hermeneus_parser_checked_multiply(size_t left, size_t right,
                                          size_t *result);

#define set_error hermeneus_parser_set_error
#define checked_multiply hermeneus_parser_checked_multiply

#endif
