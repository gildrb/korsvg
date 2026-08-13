#include "../korsvg.h"

#include <stdio.h>
#include <string.h>

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

struct test_fixture {
	char path[4096];
	CFDataRef source;
	CFDataRef serialized;
	CFDataRef bad;
	CFURLRef url;
	CGSVGDocumentRef document;
	CGSVGDocumentRef loaded;
	CGContextRef context;
};

static int fail(const char *message)
{
	fprintf(stderr, "korsvg test failed: %s\n", message);
	return 1;
}

static int expect(int condition, const char *message)
{
	return condition ? 0 : fail(message);
}

static const uint8_t *pixel(CGContextRef context, int32_t x, int32_t y)
{
	return KorSVGContextGetData(context) +
	       (size_t)y * KorSVGContextGetStride(context) + (size_t)x * 4;
}

static int expect_rejected_svg(const char *source, size_t length,
			       const char *message)
{
	CFDataRef data = KorSVGDataCreate(source, length);
	CGSVGDocumentRef document;

	if (!data)
		return fail(KorSVGGetLastError());
	document = CGSVGDocumentCreateFromData(data, NULL);
	KorSVGDataRelease(data);
	if (document) {
		CGSVGDocumentRelease(document);
		return fail(message);
	}
	if (KorSVGGetLastError()[0] == 0)
		return fail("rejected SVG did not report an error");
	return 0;
}

static int test_data_and_document(struct test_fixture *fixture)
{
	CGSize size;

	fixture->source = KorSVGDataCreate(svg, sizeof(svg) - 1);
	if (expect(fixture->source != NULL, KorSVGGetLastError()) ||
	    expect(!KorSVGDataIsMutable(fixture->source),
		   "source data must be immutable"))
		return 1;
	fixture->document = CGSVGDocumentCreateFromData(fixture->source, NULL);
	KorSVGDataRelease(fixture->source);
	fixture->source = NULL;
	if (expect(fixture->document != NULL, KorSVGGetLastError()) ||
	    expect(CGSVGDocumentGetTypeID() != 0, "document type ID is zero") ||
	    expect(CGSVGDocumentRetain(fixture->document) == fixture->document,
		   "document retain changed the reference"))
		return 1;
	CGSVGDocumentRelease(fixture->document);
	size = CGSVGDocumentGetCanvasSize(fixture->document);
	return expect(size.width == 40.0 && size.height == 20.0,
		      "canvas size is wrong");
}

static int test_rendering(struct test_fixture *fixture)
{
	const uint8_t *sample;

	fixture->context = KorSVGContextCreate(100, 60);
	if (expect(fixture->context != NULL, KorSVGGetLastError()) ||
	    expect(KorSVGContextGetWidth(fixture->context) == 100 &&
		   KorSVGContextGetHeight(fixture->context) == 60,
		   "context dimensions are wrong") ||
	    expect(KorSVGContextClear(fixture->context, 0, 0, 0, 0),
		   KorSVGGetLastError()) ||
	    expect(KorSVGContextSetViewport(fixture->context, 10, 10, 80, 40),
		   KorSVGGetLastError()))
		return 1;
	CGContextDrawSVGDocument(fixture->context, fixture->document);
	if (expect(KorSVGGetLastError()[0] == 0, KorSVGGetLastError()))
		return 1;
	sample = pixel(fixture->context, 20, 30);
	if (expect(sample[0] == 255 && sample[1] == 0 &&
		   sample[2] == 0 && sample[3] == 255,
		   "left viewport pixel is not red"))
		return 1;
	sample = pixel(fixture->context, 70, 30);
	if (expect(sample[0] == 0 && sample[1] == 0 &&
		   sample[2] == 255 && sample[3] == 255,
		   "right viewport pixel is not blue"))
		return 1;
	sample = pixel(fixture->context, 5, 5);
	if (expect(sample[3] == 0, "draw escaped the viewport"))
		return 1;
	if (expect(!KorSVGContextSetViewport(fixture->context, 90, 0, 20, 20),
		   "invalid viewport was accepted"))
		return 1;
	return expect(KorSVGGetLastError()[0] != 0,
		      "invalid viewport did not report an error");
}

static int test_serialization(struct test_fixture *fixture)
{
	CGSize size;

	fixture->serialized = KorSVGDataCreateMutable();
	if (expect(fixture->serialized != NULL, KorSVGGetLastError()) ||
	    expect(CGSVGDocumentWriteToData(fixture->document,
					    fixture->serialized, NULL),
		   KorSVGGetLastError()) ||
	    expect(KorSVGDataGetLength(fixture->serialized) == sizeof(svg) - 1,
		   "serialized length is wrong") ||
	    expect(memcmp(KorSVGDataGetBytes(fixture->serialized), svg,
			  sizeof(svg) - 1) == 0, "serialized bytes changed"))
		return 1;
	fixture->source = KorSVGDataCreate(svg, sizeof(svg) - 1);
	if (expect(!CGSVGDocumentWriteToData(fixture->document, fixture->source,
					     NULL),
		   "immutable destination accepted serialization") ||
	    expect(KorSVGGetLastError()[0] != 0,
		   "immutable destination did not report an error"))
		return 1;
	fixture->url = KorSVGURLCreate(fixture->path);
	if (expect(fixture->url != NULL, KorSVGGetLastError()) ||
	    expect(KorSVGURLRetain(fixture->url) == fixture->url,
		   "URL retain changed the reference"))
		return 1;
	KorSVGURLRelease(fixture->url);
	if (expect(CGSVGDocumentWriteToURL(fixture->document, fixture->url,
					   NULL), KorSVGGetLastError()))
		return 1;
	fixture->loaded = CGSVGDocumentCreateFromURL(fixture->url, NULL);
	if (expect(fixture->loaded != NULL, KorSVGGetLastError()))
		return 1;
	size = CGSVGDocumentGetCanvasSize(fixture->loaded);
	return expect(size.width == 40.0 && size.height == 20.0,
		      "URL document canvas size is wrong");
}

static int test_rejections(struct test_fixture *fixture)
{
	CGSize size;

	fixture->bad = KorSVGDataCreate(invalid, sizeof(invalid) - 1);
	if (expect(fixture->bad != NULL, KorSVGGetLastError()) ||
	    expect(!CGSVGDocumentCreateFromData(fixture->bad, NULL),
		   "invalid SVG created a document") ||
	    expect(KorSVGGetLastError()[0] != 0,
		   "invalid SVG did not report an error"))
		return 1;
	KorSVGDataRelease(fixture->bad);
	fixture->bad = KorSVGDataCreate(unsupported, sizeof(unsupported) - 1);
	if (expect(fixture->bad != NULL, KorSVGGetLastError()) ||
	    expect(!CGSVGDocumentCreateFromData(fixture->bad, NULL),
		   "unsupported SVG created a document") ||
	    expect(KorSVGGetLastError()[0] != 0,
		   "unsupported SVG did not report an error"))
		return 1;
	KorSVGDataRelease(fixture->bad);
	fixture->bad =
		KorSVGDataCreate(nonfinite_color, sizeof(nonfinite_color) - 1);
	if (expect(fixture->bad != NULL, KorSVGGetLastError()) ||
	    expect(!CGSVGDocumentCreateFromData(fixture->bad, NULL),
		   "non-finite color created a document") ||
	    expect(KorSVGGetLastError()[0] != 0,
		   "non-finite color did not report an error"))
		return 1;
	KorSVGDataRelease(fixture->bad);
	fixture->bad =
		KorSVGDataCreate(huge_geometry, sizeof(huge_geometry) - 1);
	if (expect(fixture->bad != NULL, KorSVGGetLastError()) ||
	    expect(!CGSVGDocumentCreateFromData(fixture->bad, NULL),
		   "huge geometry created a document") ||
	    expect(KorSVGGetLastError()[0] != 0,
		   "huge geometry did not report an error"))
		return 1;
	size = CGSVGDocumentGetCanvasSize(NULL);
	if (expect(size.width == 0 && size.height == 0,
		   "null document returned a canvas") ||
	    expect(KorSVGGetLastError()[0] != 0,
		   "null document did not report an error"))
		return 1;
	return expect_rejected_svg(huge_circle, sizeof(huge_circle) - 1,
				   "huge circle created a document") ||
	       expect_rejected_svg(huge_arc, sizeof(huge_arc) - 1,
				   "huge arc created a document") ||
	       expect_rejected_svg(group_opacity, sizeof(group_opacity) - 1,
				   "group opacity created a document") ||
	       expect_rejected_svg(trailing_element,
				   sizeof(trailing_element) - 1,
				   "trailing element created a document") ||
	       expect_rejected_svg(mismatched_close,
				   sizeof(mismatched_close) - 1,
				   "mismatched close created a document") ||
	       expect_rejected_svg(unsupported_join,
				   sizeof(unsupported_join) - 1,
				   "unsupported join created a document");
}

int main(int argument_count, char **arguments)
{
	struct test_fixture fixture = { 0 };
	int status = 1;

	if (argument_count != 2)
		return fail("temporary directory argument is missing");
	if (snprintf(fixture.path, sizeof(fixture.path), "%s/roundtrip.svg",
		     arguments[1]) < 0)
		return fail("temporary path creation failed");
	if (test_data_and_document(&fixture))
		goto cleanup;
	if (test_rendering(&fixture))
		goto cleanup;
	if (test_serialization(&fixture))
		goto cleanup;
	if (test_rejections(&fixture))
		goto cleanup;
	status = 0;

cleanup:
	KorSVGDataRelease(fixture.source);
	KorSVGDataRelease(fixture.serialized);
	KorSVGDataRelease(fixture.bad);
	KorSVGURLRelease(fixture.url);
	CGSVGDocumentRelease(fixture.loaded);
	CGSVGDocumentRelease(fixture.document);
	KorSVGContextRelease(fixture.context);
	return status;
}
