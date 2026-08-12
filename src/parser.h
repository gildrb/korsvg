#ifndef HERMENEUS_PARSER_H
#define HERMENEUS_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  int32_t width;
  int32_t height;
  uint8_t *pixels;
} HermeneusParserImage;

void hermeneus_parser_image_free(HermeneusParserImage *image);
int32_t hermeneus_parser_svg_canvas_size(const char *source, size_t length,
                                          double *width, double *height,
                                          char *error, size_t error_capacity);
int32_t hermeneus_parser_svg_render(const char *source, size_t length,
                                     int32_t output_width,
                                     int32_t output_height,
                                     HermeneusParserImage *image, char *error,
                                     size_t error_capacity);

#endif
