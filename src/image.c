#include "internal.h"

void hermeneus_parser_image_free(Image *image) {
  if (image != NULL) {
    free(image->pixels);
    memset(image, 0, sizeof(*image));
  }
}
