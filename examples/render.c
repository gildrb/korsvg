#include "../hermeneus.h"

#include <stdio.h>

int main(void) {
  static const char svg[] =
      "<svg viewBox=\"0 0 32 32\"><circle cx=\"16\" cy=\"16\" r=\"12\" "
      "fill=\"#7c3aed\"/></svg>";
  CFDataRef data = HermeneusDataCreate(svg, sizeof(svg) - 1);
  CGSVGDocumentRef document;
  CGContextRef context;
  const uint8_t *center;

  if (data == NULL) {
    return 1;
  }
  document = CGSVGDocumentCreateFromData(data, NULL);
  if (document == NULL) {
    HermeneusDataRelease(data);
    return 1;
  }
  context = HermeneusContextCreate(64, 64);
  if (context == NULL) {
    CGSVGDocumentRelease(document);
    HermeneusDataRelease(data);
    return 1;
  }
  CGContextDrawSVGDocument(context, document);
  center = HermeneusContextGetData(context) +
           32 * HermeneusContextGetStride(context) + 32 * 4;
  printf("%u %u %u %u\n", center[0], center[1], center[2], center[3]);
  HermeneusContextRelease(context);
  CGSVGDocumentRelease(document);
  HermeneusDataRelease(data);
  return 0;
}
