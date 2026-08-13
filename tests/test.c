#include "../korsvg.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message) {
  fprintf(stderr, "korsvg test failed: %s\n", message);
  return 1;
}

static int expect(int condition, const char *message) {
  return condition ? 0 : fail(message);
}

static int expect_rejected_svg(const char *svg, size_t length,
                               const char *message) {
  CFDataRef data = KorSVGDataCreate(svg, length);
  CGSVGDocumentRef document;

  if (data == NULL) {
    return fail(KorSVGGetLastError());
  }
  document = CGSVGDocumentCreateFromData(data, NULL);
  KorSVGDataRelease(data);
  if (document != NULL) {
    CGSVGDocumentRelease(document);
    return fail(message);
  }
  if (KorSVGGetLastError()[0] == 0) {
    return fail("rejected SVG did not report an error");
  }
  return 0;
}

static const uint8_t *pixel(CGContextRef context, int32_t x, int32_t y) {
  return KorSVGContextGetData(context) +
         (size_t)y * KorSVGContextGetStride(context) + (size_t)x * 4;
}

int main(int argument_count, char **arguments) {
  static const char svg[] =
      "<svg viewBox=\"0 0 40 20\"><rect width=\"20\" height=\"20\" "
      "fill=\"#ff0000\"/><rect x=\"20\" width=\"20\" height=\"20\" "
      "fill=\"#0000ff\"/></svg>";
  static const char invalid[] = "<svg></svg>";
  static const char unsupported[] =
      "<svg viewBox=\"0 0 10 10\"><text>x</text></svg>";
  static const char nonfinite_color[] =
      "<svg viewBox=\"0 0 1 1\"><rect width=\"1\" height=\"1\" "
      "fill=\"rgb(nan,0,0)\"/></svg>";
  static const char huge_geometry[] =
      "<svg viewBox=\"0 0 1 1\"><rect y=\"1e308\" width=\"1\" "
      "height=\"1\"/></svg>";
  static const char huge_circle[] =
      "<svg viewBox=\"0 0 1 1\"><circle r=\"1e308\"/></svg>";
  static const char huge_arc[] =
      "<svg viewBox=\"0 0 1 1\"><path "
      "d=\"M0 0 A1e308 1e308 0 0 1 1 1 Z\"/></svg>";
  static const char group_opacity[] =
      "<svg viewBox=\"0 0 1 1\"><g opacity=\"0.5\"><rect width=\"1\" "
      "height=\"1\"/></g></svg>";
  static const char trailing_element[] =
      "<svg viewBox=\"0 0 1 1\"></svg><rect width=\"1\" height=\"1\"/>";
  static const char mismatched_close[] =
      "<svg viewBox=\"0 0 1 1\"><g></svg></g>";
  static const char unsupported_join[] =
      "<svg viewBox=\"0 0 10 10\"><polyline points=\"1,9 5,1 9,9\" "
      "fill=\"none\" stroke=\"black\" stroke-linejoin=\"miter\"/></svg>";
  char path[4096];
  CFDataRef source = NULL;
  CFDataRef serialized = NULL;
  CFDataRef bad = NULL;
  CFURLRef url = NULL;
  CGSVGDocumentRef document = NULL;
  CGSVGDocumentRef loaded = NULL;
  CGContextRef context = NULL;
  CGSize size;
  const uint8_t *sample;
  int status = 1;

  if (argument_count != 2) {
    return fail("temporary directory argument is missing");
  }
  if (snprintf(path, sizeof(path), "%s/roundtrip.svg", arguments[1]) < 0) {
    return fail("temporary path creation failed");
  }
  source = KorSVGDataCreate(svg, sizeof(svg) - 1);
  if (expect(source != NULL, KorSVGGetLastError()) ||
      expect(!KorSVGDataIsMutable(source), "source data must be immutable")) {
    goto cleanup;
  }
  document = CGSVGDocumentCreateFromData(source, NULL);
  KorSVGDataRelease(source);
  source = NULL;
  if (expect(document != NULL, KorSVGGetLastError()) ||
      expect(CGSVGDocumentGetTypeID() != 0, "document type ID is zero") ||
      expect(CGSVGDocumentRetain(document) == document,
             "document retain changed the reference")) {
    goto cleanup;
  }
  CGSVGDocumentRelease(document);
  size = CGSVGDocumentGetCanvasSize(document);
  if (expect(size.width == 40.0 && size.height == 20.0,
             "canvas size is wrong")) {
    goto cleanup;
  }
  context = KorSVGContextCreate(100, 60);
  if (expect(context != NULL, KorSVGGetLastError()) ||
      expect(KorSVGContextGetWidth(context) == 100 &&
                 KorSVGContextGetHeight(context) == 60,
             "context dimensions are wrong") ||
      expect(KorSVGContextClear(context, 0, 0, 0, 0),
             KorSVGGetLastError()) ||
      expect(KorSVGContextSetViewport(context, 10, 10, 80, 40),
             KorSVGGetLastError())) {
    goto cleanup;
  }
  CGContextDrawSVGDocument(context, document);
  if (expect(KorSVGGetLastError()[0] == 0, KorSVGGetLastError())) {
    goto cleanup;
  }
  sample = pixel(context, 20, 30);
  if (expect(sample[0] == 255 && sample[1] == 0 && sample[2] == 0 &&
                 sample[3] == 255,
             "left viewport pixel is not red")) {
    goto cleanup;
  }
  sample = pixel(context, 70, 30);
  if (expect(sample[0] == 0 && sample[1] == 0 && sample[2] == 255 &&
                 sample[3] == 255,
             "right viewport pixel is not blue")) {
    goto cleanup;
  }
  sample = pixel(context, 5, 5);
  if (expect(sample[3] == 0, "draw escaped the viewport") ||
      expect(!KorSVGContextSetViewport(context, 90, 0, 20, 20),
             "invalid viewport was accepted") ||
      expect(KorSVGGetLastError()[0] != 0,
             "invalid viewport did not report an error")) {
    goto cleanup;
  }
  serialized = KorSVGDataCreateMutable();
  if (expect(serialized != NULL, KorSVGGetLastError()) ||
      expect(CGSVGDocumentWriteToData(document, serialized, NULL),
             KorSVGGetLastError()) ||
      expect(KorSVGDataGetLength(serialized) == sizeof(svg) - 1,
             "serialized length is wrong") ||
      expect(memcmp(KorSVGDataGetBytes(serialized), svg, sizeof(svg) - 1) ==
                 0,
             "serialized bytes changed")) {
    goto cleanup;
  }
  source = KorSVGDataCreate(svg, sizeof(svg) - 1);
  if (expect(!CGSVGDocumentWriteToData(document, source, NULL),
             "immutable destination accepted serialization") ||
      expect(KorSVGGetLastError()[0] != 0,
             "immutable destination did not report an error")) {
    goto cleanup;
  }
  url = KorSVGURLCreate(path);
  if (expect(url != NULL, KorSVGGetLastError()) ||
      expect(KorSVGURLRetain(url) == url,
             "URL retain changed the reference")) {
    goto cleanup;
  }
  KorSVGURLRelease(url);
  if (expect(CGSVGDocumentWriteToURL(document, url, NULL),
             KorSVGGetLastError())) {
    goto cleanup;
  }
  loaded = CGSVGDocumentCreateFromURL(url, NULL);
  if (expect(loaded != NULL, KorSVGGetLastError())) {
    goto cleanup;
  }
  size = CGSVGDocumentGetCanvasSize(loaded);
  if (expect(size.width == 40.0 && size.height == 20.0,
             "URL document canvas size is wrong")) {
    goto cleanup;
  }
  bad = KorSVGDataCreate(invalid, sizeof(invalid) - 1);
  if (expect(bad != NULL, KorSVGGetLastError()) ||
      expect(CGSVGDocumentCreateFromData(bad, NULL) == NULL,
             "invalid SVG created a document") ||
      expect(KorSVGGetLastError()[0] != 0,
             "invalid SVG did not report an error")) {
    goto cleanup;
  }
  KorSVGDataRelease(bad);
  bad = KorSVGDataCreate(unsupported, sizeof(unsupported) - 1);
  if (expect(bad != NULL, KorSVGGetLastError()) ||
      expect(CGSVGDocumentCreateFromData(bad, NULL) == NULL,
             "unsupported SVG created a document") ||
      expect(KorSVGGetLastError()[0] != 0,
             "unsupported SVG did not report an error")) {
    goto cleanup;
  }
  KorSVGDataRelease(bad);
  bad = KorSVGDataCreate(nonfinite_color, sizeof(nonfinite_color) - 1);
  if (expect(bad != NULL, KorSVGGetLastError()) ||
      expect(CGSVGDocumentCreateFromData(bad, NULL) == NULL,
             "non-finite color created a document") ||
      expect(KorSVGGetLastError()[0] != 0,
             "non-finite color did not report an error")) {
    goto cleanup;
  }
  KorSVGDataRelease(bad);
  bad = KorSVGDataCreate(huge_geometry, sizeof(huge_geometry) - 1);
  if (expect(bad != NULL, KorSVGGetLastError())) {
    goto cleanup;
  }
  if (expect(CGSVGDocumentCreateFromData(bad, NULL) == NULL,
             "huge geometry created a document") ||
      expect(KorSVGGetLastError()[0] != 0,
             "huge geometry did not report an error")) {
    goto cleanup;
  }
  size = CGSVGDocumentGetCanvasSize(NULL);
  if (expect(size.width == 0 && size.height == 0,
             "null document returned a canvas") ||
      expect(KorSVGGetLastError()[0] != 0,
             "null document did not report an error")) {
    goto cleanup;
  }
  if (expect_rejected_svg(huge_circle, sizeof(huge_circle) - 1,
                          "huge circle created a document") ||
      expect_rejected_svg(huge_arc, sizeof(huge_arc) - 1,
                          "huge arc created a document") ||
      expect_rejected_svg(group_opacity, sizeof(group_opacity) - 1,
                          "group opacity created a document") ||
      expect_rejected_svg(trailing_element, sizeof(trailing_element) - 1,
                          "trailing element created a document") ||
      expect_rejected_svg(mismatched_close, sizeof(mismatched_close) - 1,
                          "mismatched close created a document") ||
      expect_rejected_svg(unsupported_join, sizeof(unsupported_join) - 1,
                          "unsupported join created a document")) {
    goto cleanup;
  }
  status = 0;

cleanup:
  KorSVGDataRelease(source);
  KorSVGDataRelease(serialized);
  KorSVGDataRelease(bad);
  KorSVGURLRelease(url);
  CGSVGDocumentRelease(loaded);
  CGSVGDocumentRelease(document);
  KorSVGContextRelease(context);
  return status;
}
