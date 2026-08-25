#define _POSIX_C_SOURCE 200809L

#include "../korsvg.h"
#include <archetypon.h>

#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

void archetypon_svg_counters_reset(void);
size_t archetypon_svg_document_compile_count(void);
size_t archetypon_svg_plan_compile_count(void);

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

struct test_fixture {
	char path[4096];
	KorSVGDataRef source;
	KorSVGDataRef serialized;
	KorSVGDataRef bad;
	KorSVGURLRef url;
	KorSVGDocumentRef document;
	KorSVGDocumentRef loaded;
	KorSVGContextRef context;
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

static const uint8_t *pixel(KorSVGContextRef context, int32_t x, int32_t y)
{
	return KorSVGContextGetData(context) +
	       (size_t)y * KorSVGContextGetStride(context) + (size_t)x * 4;
}

static int expect_rejected_svg(const char *source, size_t length,
			       const char *message)
{
	KorSVGDataRef data = KorSVGDataCreate(source, length);
	KorSVGDocumentRef document;

	if (!data)
		return fail(KorSVGGetLastError());
	document = KorSVGDocumentCreateFromData(data, NULL);
	KorSVGDataRelease(data);
	if (document) {
		KorSVGDocumentRelease(document);
		return fail(message);
	}
	if (KorSVGGetLastError()[0] == 0)
		return fail("rejected SVG did not report an error");
	return 0;
}

static int test_data_and_document(struct test_fixture *fixture)
{
	KorSVGSize size;

	fixture->source = KorSVGDataCreate(svg, sizeof(svg) - 1);
	if (expect(fixture->source != NULL, KorSVGGetLastError()) ||
	    expect(!KorSVGDataIsMutable(fixture->source),
		   "source data must be immutable"))
		return 1;
	fixture->document = KorSVGDocumentCreateFromData(fixture->source, NULL);
	KorSVGDataRelease(fixture->source);
	fixture->source = NULL;
	if (expect(fixture->document != NULL, KorSVGGetLastError()) ||
	    expect(KorSVGDocumentGetTypeID() != 0, "document type ID is zero") ||
	    expect(KorSVGDocumentRetain(fixture->document) == fixture->document,
		   "document retain changed the reference"))
		return 1;
	KorSVGDocumentRelease(fixture->document);
	size = KorSVGDocumentGetCanvasSize(fixture->document);
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
	if (expect(KorSVGContextDrawDocument(fixture->context,
					 fixture->document),
		   KorSVGGetLastError()) ||
	    expect(KorSVGGetLastError()[0] == 0, KorSVGGetLastError()))
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
		   "invalid viewport was accepted") ||
	    expect(KorSVGGetLastError()[0] != 0,
		   "invalid viewport did not report an error") ||
	    expect(!KorSVGContextDrawDocument(NULL, fixture->document),
		   "draw with a null context reported success"))
		return 1;
	return expect(KorSVGGetLastError()[0] != 0,
		      "failed draw did not report an error");
}

static int test_serialization(struct test_fixture *fixture)
{
	KorSVGSize size;

	fixture->serialized = KorSVGDataCreateMutable();
	if (expect(fixture->serialized != NULL, KorSVGGetLastError()) ||
	    expect(KorSVGDocumentWriteToData(fixture->document,
					    fixture->serialized, NULL),
		   KorSVGGetLastError()) ||
	    expect(KorSVGDataGetLength(fixture->serialized) == sizeof(svg) - 1,
		   "serialized length is wrong") ||
	    expect(memcmp(KorSVGDataGetBytes(fixture->serialized), svg,
			  sizeof(svg) - 1) == 0, "serialized bytes changed"))
		return 1;
	fixture->source = KorSVGDataCreate(svg, sizeof(svg) - 1);
	if (expect(!KorSVGDocumentWriteToData(fixture->document, fixture->source,
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
	if (expect(KorSVGDocumentWriteToURL(fixture->document, fixture->url,
					   NULL), KorSVGGetLastError()))
		return 1;
	fixture->loaded = KorSVGDocumentCreateFromURL(fixture->url, NULL);
	if (expect(fixture->loaded != NULL, KorSVGGetLastError()))
		return 1;
	size = KorSVGDocumentGetCanvasSize(fixture->loaded);
	return expect(size.width == 40.0 && size.height == 20.0,
		      "URL document canvas size is wrong");
}

static int test_safe_url_io(struct test_fixture *fixture,
			    const char *temporary_directory)
{
	static const char original[] = "original";
	char oversized_path[4096];
	char fifo_path[4096];
	char contents[sizeof(original)] = { 0 };
	struct rlimit old_limit;
	struct rlimit write_limit;
	struct stat destination_status;
	void (*old_signal)(int);
	KorSVGURLRef url = NULL;
	int descriptor = -1;
	int write_status;
	int restore_status;
	ssize_t count;

	if (snprintf(oversized_path, sizeof(oversized_path), "%s/oversized.svg",
		     temporary_directory) >= (int)sizeof(oversized_path) ||
	    snprintf(fifo_path, sizeof(fifo_path), "%s/source.fifo",
		     temporary_directory) >= (int)sizeof(fifo_path))
		return fail("safe I/O test path is too long");
	descriptor = open(oversized_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (descriptor < 0)
		return fail("could not create oversized URL source");
	if (ftruncate(descriptor,
		      (off_t)(32u * 1024u * 1024u + 1u)) != 0) {
		close(descriptor);
		return fail("could not size oversized URL source");
	}
	if (close(descriptor) != 0)
		return fail("could not close oversized URL source");
	descriptor = -1;
	url = KorSVGURLCreate(oversized_path);
	if (expect(url != NULL, KorSVGGetLastError()) ||
	    expect(!KorSVGDocumentCreateFromURL(url, NULL),
		   "oversized URL source was accepted") ||
	    expect(KorSVGGetLastError()[0] != 0,
		   "oversized URL source did not report an error")) {
		KorSVGURLRelease(url);
		return 1;
	}
	KorSVGURLRelease(url);
	url = NULL;
	if (mkfifo(fifo_path, 0600) != 0)
		return fail("could not create FIFO URL source");
	url = KorSVGURLCreate(fifo_path);
	if (expect(url != NULL, KorSVGGetLastError()) ||
	    expect(!KorSVGDocumentCreateFromURL(url, NULL),
		   "non-regular URL source was accepted") ||
	    expect(KorSVGGetLastError()[0] != 0,
		   "non-regular URL source did not report an error")) {
		KorSVGURLRelease(url);
		return 1;
	}
	KorSVGURLRelease(url);

	descriptor = open(fixture->path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (descriptor < 0)
		return fail("could not prepare atomic write destination");
	count = write(descriptor, original, sizeof(original) - 1);
	if (close(descriptor) != 0 ||
	    count != (ssize_t)(sizeof(original) - 1))
		return fail("could not prepare atomic write destination");
	descriptor = -1;
	if (getrlimit(RLIMIT_FSIZE, &old_limit) != 0)
		return fail("could not inspect file size limit");
	write_limit = old_limit;
	write_limit.rlim_cur = 1;
	old_signal = signal(SIGXFSZ, SIG_IGN);
	if (old_signal == SIG_ERR)
		return fail("could not ignore file size signal");
	if (setrlimit(RLIMIT_FSIZE, &write_limit) != 0) {
		(void)signal(SIGXFSZ, old_signal);
		return fail("could not constrain atomic write");
	}
	write_status = KorSVGDocumentWriteToURL(fixture->document, fixture->url,
						 NULL);
	restore_status = setrlimit(RLIMIT_FSIZE, &old_limit);
	(void)signal(SIGXFSZ, old_signal);
	if (restore_status != 0)
		return fail("could not restore file size limit");
	if (expect(!write_status, "constrained URL write reported success") ||
	    expect(KorSVGGetLastError()[0] != 0,
		   "failed URL write did not report an error"))
		return 1;
	descriptor = open(fixture->path, O_RDONLY);
	if (descriptor < 0)
		return fail("atomic write destination disappeared");
	count = read(descriptor, contents, sizeof(contents));
	if (close(descriptor) != 0)
		return fail("could not close atomic write destination");
	if (expect(count == (ssize_t)(sizeof(original) - 1) &&
		   memcmp(contents, original, sizeof(original) - 1) == 0,
		   "failed URL write changed existing data"))
		return 1;
	if (chmod(fixture->path, 0644) != 0 ||
	    !KorSVGDocumentWriteToURL(fixture->document, fixture->url, NULL) ||
	    stat(fixture->path, &destination_status) != 0)
		return fail("could not verify atomic write permissions");
	return expect((destination_status.st_mode & 0777) == 0644,
		      "atomic URL write changed destination permissions");
}

static int test_rejections(struct test_fixture *fixture)
{
	KorSVGSize size;

	fixture->bad = KorSVGDataCreate(invalid, sizeof(invalid) - 1);
	if (expect(fixture->bad != NULL, KorSVGGetLastError()) ||
	    expect(!KorSVGDocumentCreateFromData(fixture->bad, NULL),
		   "invalid SVG created a document") ||
	    expect(KorSVGGetLastError()[0] != 0,
		   "invalid SVG did not report an error"))
		return 1;
	KorSVGDataRelease(fixture->bad);
	fixture->bad = KorSVGDataCreate(unsupported, sizeof(unsupported) - 1);
	if (expect(fixture->bad != NULL, KorSVGGetLastError()) ||
	    expect(!KorSVGDocumentCreateFromData(fixture->bad, NULL),
		   "unsupported SVG created a document") ||
	    expect(KorSVGGetLastError()[0] != 0,
		   "unsupported SVG did not report an error"))
		return 1;
	KorSVGDataRelease(fixture->bad);
	fixture->bad =
		KorSVGDataCreate(nonfinite_color, sizeof(nonfinite_color) - 1);
	if (expect(fixture->bad != NULL, KorSVGGetLastError()) ||
	    expect(!KorSVGDocumentCreateFromData(fixture->bad, NULL),
		   "non-finite color created a document") ||
	    expect(KorSVGGetLastError()[0] != 0,
		   "non-finite color did not report an error"))
		return 1;
	KorSVGDataRelease(fixture->bad);
	fixture->bad =
		KorSVGDataCreate(huge_geometry, sizeof(huge_geometry) - 1);
	if (expect(fixture->bad != NULL, KorSVGGetLastError()) ||
	    expect(!KorSVGDocumentCreateFromData(fixture->bad, NULL),
		   "huge geometry created a document") ||
	    expect(KorSVGGetLastError()[0] != 0,
		   "huge geometry did not report an error"))
		return 1;
	size = KorSVGDocumentGetCanvasSize(NULL);
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
				   "mismatched close created a document");
}

struct draw_thread {
	KorSVGDocumentRef document;
	int status;
};

static void *draw_in_thread(void *opaque)
{
	struct draw_thread *thread = opaque;
	KorSVGContextRef context = KorSVGContextCreate(64, 32);

	thread->status = context &&
		KorSVGContextDrawDocument(context, thread->document);
	KorSVGContextRelease(context);
	return NULL;
}

static void *change_cache_limit(void *opaque)
{
	KorSVGDocumentRef document = opaque;
	size_t index;

	for (index = 0; index < 200; index++)
		KorSVGDocumentSetPlanCacheLimit(document,
			index % 2 ? 32u * 1024u * 1024u : 0);
	return NULL;
}

static int test_retained_plans(void)
{
	KorSVGDataRef data = KorSVGDataCreate(svg, sizeof(svg) - 1);
	KorSVGDocumentRef document = NULL;
	KorSVGContextRef a = NULL;
	KorSVGContextRef b = NULL;
	KorSVGContextRef c = NULL;
	struct draw_thread threads[8];
	pthread_t ids[8];
	pthread_t limit_thread;
	size_t after_threads;
	size_t created = 0;
	size_t i;
	int status = 1;

	if (!data)
		goto cleanup;
	archetypon_svg_counters_reset();
	document = KorSVGDocumentCreateFromData(data, NULL);
	if (!document || archetypon_svg_document_compile_count() != 1) {
		fail("SVG document was not compiled exactly once");
		goto cleanup;
	}
	KorSVGDataRelease(data);
	data = NULL;
	a = KorSVGContextCreate(40, 20);
	b = KorSVGContextCreate(80, 40);
	c = KorSVGContextCreate(60, 30);
	if (!a || !b || !c)
		goto cleanup;
	archetypon_svg_counters_reset();
	if (!KorSVGContextDrawDocument(a, document) ||
	    archetypon_svg_plan_compile_count() != 1 ||
	    !KorSVGContextDrawDocument(a, document) ||
	    archetypon_svg_plan_compile_count() != 1 ||
	    !KorSVGContextDrawDocument(b, document) ||
	    archetypon_svg_plan_compile_count() != 2 ||
	    !KorSVGContextDrawDocument(a, document) ||
	    archetypon_svg_plan_compile_count() != 2 ||
	    !KorSVGContextDrawDocument(c, document) ||
	    archetypon_svg_plan_compile_count() != 3 ||
	    !KorSVGContextDrawDocument(b, document) ||
	    archetypon_svg_plan_compile_count() != 4) {
		fail("retained plan LRU behavior is wrong");
		goto cleanup;
	}
	KorSVGDocumentSetPlanCacheLimit(document, 1024);
	archetypon_svg_counters_reset();
	if (!KorSVGContextDrawDocument(a, document) ||
	    !KorSVGContextDrawDocument(a, document) ||
	    archetypon_svg_plan_compile_count() != 2 ||
	    KorSVGDocumentGetPlanCacheCost(document) != 0) {
		fail("oversized plans were cached past the byte bound");
		goto cleanup;
	}
	KorSVGDocumentRelease(document);
	document = KorSVGDocumentCreateFromData(
		data = KorSVGDataCreate(svg, sizeof(svg) - 1), NULL);
	KorSVGDataRelease(data);
	data = NULL;
	if (!document)
		goto cleanup;
	archetypon_svg_counters_reset();
	for (created = 0; created < 8; created++) {
		threads[created].document = document;
		threads[created].status = 0;
		if (pthread_create(&ids[created], NULL, draw_in_thread,
				   &threads[created]) != 0)
			break;
	}
	for (i = 0; i < created; i++) {
		if (pthread_join(ids[i], NULL) != 0 || !threads[i].status) {
			fail("concurrent retained draw failed");
			goto cleanup;
		}
	}
	if (created != 8) {
		fail("could not create concurrent draw threads");
		goto cleanup;
	}
	after_threads = archetypon_svg_plan_compile_count();
	if (after_threads != 1 || !KorSVGContextSetViewport(b, 0, 0, 64, 32) ||
	    !KorSVGContextDrawDocument(b, document) ||
	    archetypon_svg_plan_compile_count() != after_threads) {
		fail("concurrent plan publication was not reused");
		goto cleanup;
	}
	if (pthread_create(&limit_thread, NULL, change_cache_limit, document) != 0) {
		fail("could not create cache-limit thread");
		goto cleanup;
	}
	for (i = 0; i < 200; i++) {
		if (!KorSVGContextDrawDocument(b, document)) {
			fail("draw failed while cache limit changed");
			break;
		}
	}
	if (pthread_join(limit_thread, NULL) != 0 || i != 200)
		goto cleanup;
	status = 0;
cleanup:
	KorSVGContextRelease(a);
	KorSVGContextRelease(b);
	KorSVGContextRelease(c);
	KorSVGDocumentRelease(document);
	KorSVGDataRelease(data);
	return status;
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
	if (test_safe_url_io(&fixture, arguments[1]))
		goto cleanup;
	if (test_rejections(&fixture))
		goto cleanup;
	if (test_retained_plans())
		goto cleanup;
	status = 0;

cleanup:
	KorSVGDataRelease(fixture.source);
	KorSVGDataRelease(fixture.serialized);
	KorSVGDataRelease(fixture.bad);
	KorSVGURLRelease(fixture.url);
	KorSVGDocumentRelease(fixture.loaded);
	KorSVGDocumentRelease(fixture.document);
	KorSVGContextRelease(fixture.context);
	return status;
}
