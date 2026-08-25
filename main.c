#define _POSIX_C_SOURCE 200809L

#include "korsvg.h"

#include <archetypon.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef int32_t s32;
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

#define KORSVG_MAX_SOURCE (32u * 1024u * 1024u)
#define KORSVG_MAX_PATH 4096u
#define KORSVG_MAX_PIXELS 16777216u
#define KORSVG_PLAN_CACHE_LIMIT (32u * 1024u * 1024u)

struct KorSVGData {
	atomic_uint references;
	u8 *bytes;
	size_t length;
	size_t capacity;
	s32 writable;
};

struct KorSVGURL {
	atomic_uint references;
	char *path;
};

struct KorSVGContext {
	s32 width;
	s32 height;
	size_t stride;
	u8 *pixels;
	s32 viewport_x;
	s32 viewport_y;
	s32 viewport_width;
	s32 viewport_height;
};

struct plan_cache_entry {
	s32 width;
	s32 height;
	u64 stamp;
	struct archetypon_svg_plan *plan;
};

struct KorSVGDocument {
	atomic_uint references;
	KorSVGSize canvas_size;
	struct archetypon_svg_document *scene;
	pthread_mutex_t cache_mutex;
	pthread_mutex_t build_mutex;
	struct plan_cache_entry cache[2];
	size_t cache_cost;
	size_t cache_limit;
	u64 cache_clock;
};

static _Thread_local char korsvg_error[256];

static s32 set_error(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(korsvg_error, sizeof(korsvg_error), format, arguments);
	va_end(arguments);
	return 0;
}

static void clear_error(void)
{
	korsvg_error[0] = 0;
}

static s32 checked_multiply(size_t left, size_t right, size_t *result)
{
	if (left != 0 && right > SIZE_MAX / left)
		return 0;
	*result = left * right;
	return 1;
}

static s32 retain_reference(atomic_uint *references)
{
	unsigned int previous;

	previous = atomic_fetch_add_explicit(references, 1,
					     memory_order_relaxed);

	if (previous == UINT_MAX) {
		atomic_fetch_sub_explicit(references, 1, memory_order_relaxed);
		return set_error("reference count overflow");
	}
	return 1;
}

static KorSVGDataRef data_allocate(s32 writable)
{
	KorSVGDataRef data = (KorSVGDataRef)calloc(1, sizeof(*data));

	if (!data) {
		set_error("out of memory creating data");
		return NULL;
	}
	atomic_init(&data->references, 1);
	data->writable = writable;
	return data;
}

static s32 data_append(KorSVGDataRef data, const u8 *bytes, size_t length)
{
	size_t needed;
	size_t capacity;
	u8 *resized;

	if (!data || !data->writable)
		return set_error("destination data is not mutable");
	if (length == 0)
		return 1;
	if (!bytes || length > SIZE_MAX - data->length)
		return set_error("invalid data append");
	needed = data->length + length;
	if (needed > data->capacity) {
		capacity = data->capacity == 0 ? 256 : data->capacity;
		while (capacity < needed) {
			if (capacity > SIZE_MAX / 2) {
				capacity = needed;
				break;
			}
			capacity *= 2;
		}
		resized = (u8 *)realloc(data->bytes, capacity);
		if (!resized)
			return set_error("out of memory growing data");
		data->bytes = resized;
		data->capacity = capacity;
	}
	memcpy(data->bytes + data->length, bytes, length);
	data->length = needed;
	return 1;
}

KorSVGDataRef KorSVGDataCreate(const void *bytes, size_t length)
{
	KorSVGDataRef data;

	clear_error();
	if (length != 0 && !bytes) {
		set_error("data bytes are missing");
		return NULL;
	}
	data = data_allocate(0);
	if (!data)
		return NULL;
	if (length != 0) {
		data->bytes = (u8 *)malloc(length);
		if (!data->bytes) {
			free(data);
			set_error("out of memory copying data");
			return NULL;
		}
		memcpy(data->bytes, bytes, length);
		data->length = length;
		data->capacity = length;
	}
	return data;
}

KorSVGDataRef KorSVGDataCreateMutable(void)
{
	clear_error();
	return data_allocate(1);
}

KorSVGDataRef KorSVGDataRetain(KorSVGDataRef data)
{
	clear_error();
	if (!data) {
		set_error("cannot retain null data");
		return NULL;
	}
	if (!retain_reference(&data->references))
		return NULL;
	return data;
}

void KorSVGDataRelease(KorSVGDataRef data)
{
	if (!data)
		return;
	if (atomic_fetch_sub_explicit(&data->references, 1,
				      memory_order_acq_rel) != 1)
		return;
	free(data->bytes);
	free(data);
}

const u8 *KorSVGDataGetBytes(KorSVGDataRef data)
{
	return data ? data->bytes : NULL;
}

size_t KorSVGDataGetLength(KorSVGDataRef data)
{
	return data ? data->length : 0;
}

s32 KorSVGDataIsMutable(KorSVGDataRef data)
{
	return data && data->writable;
}

KorSVGURLRef KorSVGURLCreate(const char *path)
{
	size_t length;
	KorSVGURLRef url;

	clear_error();
	if (!path) {
		set_error("URL path is missing");
		return NULL;
	}
	length = strlen(path);
	if (length == 0 || length > KORSVG_MAX_PATH) {
		set_error("URL path length is invalid");
		return NULL;
	}
	url = (KorSVGURLRef)calloc(1, sizeof(*url));
	if (!url) {
		set_error("out of memory creating URL");
		return NULL;
	}
	url->path = (char *)malloc(length + 1);
	if (!url->path) {
		free(url);
		set_error("out of memory copying URL");
		return NULL;
	}
	memcpy(url->path, path, length + 1);
	atomic_init(&url->references, 1);
	return url;
}

KorSVGURLRef KorSVGURLRetain(KorSVGURLRef url)
{
	clear_error();
	if (!url) {
		set_error("cannot retain null URL");
		return NULL;
	}
	if (!retain_reference(&url->references))
		return NULL;
	return url;
}

void KorSVGURLRelease(KorSVGURLRef url)
{
	if (!url)
		return;
	if (atomic_fetch_sub_explicit(&url->references, 1,
				      memory_order_acq_rel) != 1)
		return;
	free(url->path);
	free(url);
}

const char *KorSVGURLGetPath(KorSVGURLRef url)
{
	return url ? url->path : NULL;
}

KorSVGContextRef KorSVGContextCreate(s32 width, s32 height)
{
	KorSVGContextRef context;
	size_t pixels;
	size_t bytes;

	clear_error();
	if (width <= 0 || height <= 0 ||
	    !checked_multiply((size_t)width, (size_t)height, &pixels) ||
	    pixels > KORSVG_MAX_PIXELS ||
	    !checked_multiply(pixels, 4, &bytes)) {
		set_error("context dimensions are invalid");
		return NULL;
	}
	context = (KorSVGContextRef)calloc(1, sizeof(*context));
	if (!context) {
		set_error("out of memory creating context");
		return NULL;
	}
	context->pixels = (u8 *)calloc(bytes, 1);
	if (!context->pixels) {
		free(context);
		set_error("out of memory creating context pixels");
		return NULL;
	}
	context->width = width;
	context->height = height;
	context->stride = (size_t)width * 4;
	context->viewport_width = width;
	context->viewport_height = height;
	return context;
}

void KorSVGContextRelease(KorSVGContextRef context)
{
	if (context) {
		free(context->pixels);
		free(context);
	}
}

s32 KorSVGContextSetViewport(KorSVGContextRef context, s32 x, s32 y, s32 width,
			     s32 height)
{
	clear_error();
	if (!context || x < 0 || y < 0 || width <= 0 || height <= 0 ||
	    x > context->width - width || y > context->height - height)
		return set_error("context viewport is invalid");
	context->viewport_x = x;
	context->viewport_y = y;
	context->viewport_width = width;
	context->viewport_height = height;
	return 1;
}

s32 KorSVGContextClear(KorSVGContextRef context, u8 red, u8 green, u8 blue,
		       u8 alpha)
{
	size_t pixels;
	size_t index;

	clear_error();
	if (!context)
		return set_error("context is missing");
	pixels = (size_t)context->width * (size_t)context->height;
	for (index = 0; index < pixels; index++) {
		context->pixels[index * 4] = red;
		context->pixels[index * 4 + 1] = green;
		context->pixels[index * 4 + 2] = blue;
		context->pixels[index * 4 + 3] = alpha;
	}
	return 1;
}

u8 *KorSVGContextGetData(KorSVGContextRef context)
{
	return context ? context->pixels : NULL;
}

size_t KorSVGContextGetStride(KorSVGContextRef context)
{
	return context ? context->stride : 0;
}

s32 KorSVGContextGetWidth(KorSVGContextRef context)
{
	return context ? context->width : 0;
}

s32 KorSVGContextGetHeight(KorSVGContextRef context)
{
	return context ? context->height : 0;
}

const char *KorSVGGetLastError(void)
{
	return korsvg_error;
}

KorSVGTypeID KorSVGDocumentGetTypeID(void)
{
	return UINT64_C(0x4347535647444f43);
}

static struct archetypon_svg_document *parse_svg_data(
	KorSVGDataRef data, double *width, double *height)
{
	struct archetypon_svg_document *scene;
	struct archetypon_svg_plan *probe;
	char error[256] = { 0 };

	if (!data || !data->bytes || data->length == 0 ||
	    data->length > KORSVG_MAX_SOURCE) {
		set_error("SVG data is invalid");
		return NULL;
	}
	if (memchr(data->bytes, 0, data->length)) {
		set_error("SVG data contains a null byte");
		return NULL;
	}
	scene = archetypon_svg_document_create((const char *)data->bytes,
					       data->length, error,
					       sizeof(error));
	if (!scene || archetypon_svg_document_canvas_size(scene, width, height) ||
	    !isfinite(*width) || !isfinite(*height) || *width <= 0 ||
	    *height <= 0) {
		archetypon_svg_document_free(scene);
		set_error("%s", error[0] ? error : "SVG canvas is invalid");
		return NULL;
	}
	probe = archetypon_svg_plan_create(scene, 1, 1, error, sizeof(error));
	if (!probe) {
		archetypon_svg_document_free(scene);
		set_error("%s", error[0] ? error : "SVG parse failed");
		return NULL;
	}
	archetypon_svg_plan_release(probe);
	return scene;
}

KorSVGDocumentRef KorSVGDocumentCreateFromData(KorSVGDataRef data,
					     KorSVGOptionsRef options)
{
	KorSVGDocumentRef document;
	double width;
	double height;
	struct archetypon_svg_document *scene;

	(void)options;
	clear_error();

	scene = parse_svg_data(data, &width, &height);
	if (!scene)
		return NULL;
	document = (KorSVGDocumentRef)calloc(1, sizeof(*document));
	if (!document) {
		archetypon_svg_document_free(scene);
		set_error("out of memory creating SVG document");
		return NULL;
	}
	if (pthread_mutex_init(&document->cache_mutex, NULL) != 0) {
		archetypon_svg_document_free(scene);
		free(document);
		set_error("cannot initialize SVG plan cache");
		return NULL;
	}
	if (pthread_mutex_init(&document->build_mutex, NULL) != 0) {
		pthread_mutex_destroy(&document->cache_mutex);
		archetypon_svg_document_free(scene);
		free(document);
		set_error("cannot initialize SVG plan builder");
		return NULL;
	}
	document->scene = scene;
	document->cache_limit = KORSVG_PLAN_CACHE_LIMIT;
	document->canvas_size = (KorSVGSize){width, height};
	atomic_init(&document->references, 1);
	return document;
}

static KorSVGDataRef read_svg_url(KorSVGURLRef url)
{
	struct stat status;
	u8 extra;
	u8 *bytes;
	size_t capacity;
	size_t length = 0;
	ssize_t count;
	int descriptor;
	int error;
	KorSVGDataRef data;

	if (!url || !url->path) {
		set_error("SVG URL is missing");
		return NULL;
	}
	descriptor = open(url->path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (descriptor < 0) {
		set_error("cannot open SVG URL: %s", strerror(errno));
		return NULL;
	}
	if (fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
	    status.st_size <= 0 || (u64)status.st_size > KORSVG_MAX_SOURCE) {
		close(descriptor);
		set_error("SVG URL is not a readable regular file");
		return NULL;
	}
	capacity = (size_t)status.st_size;
	bytes = (u8 *)malloc(capacity);
	if (!bytes) {
		close(descriptor);
		set_error("out of memory reading SVG URL");
		return NULL;
	}
	while (length < capacity) {
		count = read(descriptor, bytes + length, capacity - length);
		if (count > 0) {
			length += (size_t)count;
			continue;
		}
		if (count == 0)
			break;
		if (errno != EINTR) {
			error = errno;
			close(descriptor);
			free(bytes);
			set_error("cannot read SVG URL: %s", strerror(error));
			return NULL;
		}
	}
	do {
		count = read(descriptor, &extra, 1);
	} while (count < 0 && errno == EINTR);
	if (count != 0 || length != capacity) {
		close(descriptor);
		free(bytes);
		set_error(count > 0 || length != capacity ?
			  "SVG URL changed while being read" :
			  "cannot complete SVG URL read");
		return NULL;
	}
	if (close(descriptor) != 0) {
		free(bytes);
		set_error("cannot close SVG URL: %s", strerror(errno));
		return NULL;
	}
	data = KorSVGDataCreate(bytes, length);
	free(bytes);
	return data;
}

KorSVGDocumentRef KorSVGDocumentCreateFromURL(KorSVGURLRef url,
					     KorSVGOptionsRef options)
{
	KorSVGDataRef data;
	KorSVGDocumentRef document;

	clear_error();
	data = read_svg_url(url);
	if (!data)
		return NULL;
	document = KorSVGDocumentCreateFromData(data, options);
	KorSVGDataRelease(data);
	return document;
}

KorSVGDocumentRef KorSVGDocumentRetain(KorSVGDocumentRef document)
{
	clear_error();
	if (!document) {
		set_error("cannot retain a null SVG document");
		return NULL;
	}
	if (!retain_reference(&document->references))
		return NULL;
	return document;
}

void KorSVGDocumentRelease(KorSVGDocumentRef document)
{
	if (!document)
		return;
	if (atomic_fetch_sub_explicit(&document->references, 1,
				      memory_order_acq_rel) != 1)
		return;
	archetypon_svg_plan_release(document->cache[0].plan);
	archetypon_svg_plan_release(document->cache[1].plan);
	pthread_mutex_destroy(&document->build_mutex);
	pthread_mutex_destroy(&document->cache_mutex);
	archetypon_svg_document_free(document->scene);
	free(document);
}

KorSVGSize KorSVGDocumentGetCanvasSize(KorSVGDocumentRef document)
{
	clear_error();
	if (!document) {
		set_error("SVG document is missing");
		return (KorSVGSize){ 0, 0 };
	}
	return document->canvas_size;
}

static void composite_pixel(u8 *destination, const u8 *source)
{
	u32 source_alpha = source[3];
	u32 destination_alpha = destination[3];
	u32 inverse_alpha;
	u32 output_alpha;
	size_t channel;

	if (source_alpha == 0)
		return;
	if (source_alpha == 255 || destination_alpha == 0) {
		memcpy(destination, source, 4);
		return;
	}
	inverse_alpha = 255 - source_alpha;
	output_alpha = source_alpha +
		       (destination_alpha * inverse_alpha + 127) / 255;
	for (channel = 0; channel < 3; channel++) {
		u32 premultiplied;

		premultiplied = source[channel] * source_alpha +
				(destination[channel] * destination_alpha *
				 inverse_alpha + 127) / 255;
		destination[channel] = (u8)((premultiplied + output_alpha / 2) /
					    output_alpha);
	}
	destination[3] = (u8)output_alpha;
}

static struct archetypon_svg_plan *cached_document_plan(
	KorSVGDocumentRef document, s32 width, s32 height)
{
	struct archetypon_svg_plan *plan = NULL;
	size_t index;

	pthread_mutex_lock(&document->cache_mutex);
	for (index = 0; index < 2; index++) {
		if (document->cache[index].plan &&
		    document->cache[index].width == width &&
		    document->cache[index].height == height) {
			document->cache[index].stamp = ++document->cache_clock;
			plan = archetypon_svg_plan_retain(
				document->cache[index].plan);
			break;
		}
	}
	pthread_mutex_unlock(&document->cache_mutex);
	return plan;
}

static struct archetypon_svg_plan *document_plan(KorSVGDocumentRef document,
						  s32 width, s32 height,
						  char *error,
						  size_t error_capacity)
{
	struct archetypon_svg_plan *built;
	struct archetypon_svg_plan *discard[2] = { NULL, NULL };
	size_t discard_count = 0;
	size_t cost;
	size_t index;

	built = cached_document_plan(document, width, height);
	if (built)
		return built;
	pthread_mutex_lock(&document->build_mutex);
	built = cached_document_plan(document, width, height);
	if (built) {
		pthread_mutex_unlock(&document->build_mutex);
		return built;
	}
	built = archetypon_svg_plan_create(document->scene, width, height, error,
					   error_capacity);
	if (!built) {
		pthread_mutex_unlock(&document->build_mutex);
		return NULL;
	}
	cost = archetypon_svg_plan_cost(built);
	pthread_mutex_lock(&document->cache_mutex);
	if (cost > document->cache_limit) {
		pthread_mutex_unlock(&document->cache_mutex);
		pthread_mutex_unlock(&document->build_mutex);
		return built;
	}
	while (document->cache_cost + cost > document->cache_limit) {
		size_t victim = !document->cache[0].plan ? 1 :
			(!document->cache[1].plan ? 0 :
			 (document->cache[0].stamp <= document->cache[1].stamp ?
			  0 : 1));

		if (!document->cache[victim].plan)
			break;
		document->cache_cost -= archetypon_svg_plan_cost(
			document->cache[victim].plan);
		discard[discard_count++] = document->cache[victim].plan;
		memset(&document->cache[victim], 0,
		       sizeof(document->cache[victim]));
	}
	index = !document->cache[0].plan ? 0 :
		(!document->cache[1].plan ? 1 :
		 (document->cache[0].stamp <= document->cache[1].stamp ? 0 : 1));
	if (document->cache[index].plan) {
		document->cache_cost -= archetypon_svg_plan_cost(
			document->cache[index].plan);
		discard[discard_count++] = document->cache[index].plan;
	}
	document->cache[index].width = width;
	document->cache[index].height = height;
	document->cache[index].stamp = ++document->cache_clock;
	document->cache[index].plan = built;
	document->cache_cost += cost;
	if (!archetypon_svg_plan_retain(built)) {
		document->cache_cost -= cost;
		memset(&document->cache[index], 0,
		       sizeof(document->cache[index]));
		pthread_mutex_unlock(&document->cache_mutex);
		pthread_mutex_unlock(&document->build_mutex);
		for (index = 0; index < discard_count; index++)
			archetypon_svg_plan_release(discard[index]);
		archetypon_svg_plan_release(built);
		snprintf(error, error_capacity,
			 "SVG plan reference count overflow");
		return NULL;
	}
	pthread_mutex_unlock(&document->cache_mutex);
	pthread_mutex_unlock(&document->build_mutex);
	for (index = 0; index < discard_count; index++)
		archetypon_svg_plan_release(discard[index]);
	return built;
}

s32 KorSVGContextDrawDocument(KorSVGContextRef context,
			      KorSVGDocumentRef document)
{
	struct archetypon_svg_plan *plan;
	const u8 *pixels;
	char error[256] = { 0 };
	s32 width;
	s32 height;
	s32 y;

	clear_error();
	if (!context || !document)
		return set_error("context and SVG document are required");
	plan = document_plan(document, context->viewport_width,
			     context->viewport_height, error, sizeof(error));
	if (!plan)
		return set_error("%s", error[0] ? error : "SVG draw failed");
	width = archetypon_svg_plan_width(plan);
	height = archetypon_svg_plan_height(plan);
	pixels = archetypon_svg_plan_pixels(plan);
	for (y = 0; y < height; y++) {
		s32 x;
		u8 *destination = context->pixels;
		const u8 *source = pixels + (size_t)y * width * 4;

		destination +=
			(size_t)(context->viewport_y + y) * context->stride;
		destination += (size_t)context->viewport_x * 4;
		for (x = 0; x < width; x++)
			composite_pixel(destination + (size_t)x * 4,
					source + (size_t)x * 4);
	}
	archetypon_svg_plan_release(plan);
	return 1;
}

void KorSVGDocumentSetPlanCacheLimit(KorSVGDocumentRef document,
					       size_t bytes)
{
	struct archetypon_svg_plan *plans[2];

	if (!document)
		return;
	pthread_mutex_lock(&document->cache_mutex);
	plans[0] = document->cache[0].plan;
	plans[1] = document->cache[1].plan;
	memset(document->cache, 0, sizeof(document->cache));
	document->cache_cost = 0;
	document->cache_limit = bytes;
	pthread_mutex_unlock(&document->cache_mutex);
	archetypon_svg_plan_release(plans[0]);
	archetypon_svg_plan_release(plans[1]);
}

size_t KorSVGDocumentGetPlanCacheCost(KorSVGDocumentRef document)
{
	size_t cost;

	if (!document)
		return 0;
	pthread_mutex_lock(&document->cache_mutex);
	cost = document->cache_cost;
	pthread_mutex_unlock(&document->cache_mutex);
	return cost;
}

s32 KorSVGDocumentWriteToData(KorSVGDocumentRef document, KorSVGDataRef data,
			     KorSVGOptionsRef options)
{
	(void)options;
	clear_error();
	if (!document)
		return set_error("SVG document is missing");
	return data_append(data,
		archetypon_svg_document_source(document->scene),
		archetypon_svg_document_source_length(document->scene));
}

s32 KorSVGDocumentWriteToURL(KorSVGDocumentRef document, KorSVGURLRef url,
			    KorSVGOptionsRef options)
{
	static const char suffix[] = ".korsvg.XXXXXX";
	struct stat destination_status;
	const u8 *source;
	char *temporary;
	size_t path_length;
	size_t source_length;
	size_t written = 0;
	ssize_t count;
	s32 success;
	int descriptor;
	int error = 0;

	(void)options;
	clear_error();
	if (!document || !url || !url->path)
		return set_error("SVG document and URL are required");
	source = archetypon_svg_document_source(document->scene);
	source_length = archetypon_svg_document_source_length(document->scene);
	path_length = strlen(url->path);
	if (path_length > SIZE_MAX - sizeof(suffix))
		return set_error("SVG URL path is too long");
	temporary = (char *)malloc(path_length + sizeof(suffix));
	if (!temporary)
		return set_error("out of memory creating SVG URL path");
	memcpy(temporary, url->path, path_length);
	memcpy(temporary + path_length, suffix, sizeof(suffix));
	descriptor = mkstemp(temporary);
	if (descriptor < 0) {
		set_error("cannot create temporary SVG URL: %s", strerror(errno));
		free(temporary);
		return 0;
	}
	if (stat(url->path, &destination_status) == 0 &&
	    S_ISREG(destination_status.st_mode) &&
	    fchmod(descriptor, destination_status.st_mode & 0777) != 0) {
		set_error("cannot preserve SVG URL permissions: %s",
			  strerror(errno));
		close(descriptor);
		unlink(temporary);
		free(temporary);
		return 0;
	}
	while (written < source_length) {
		count = write(descriptor, source + written,
			      source_length - written);
		if (count > 0) {
			written += (size_t)count;
			continue;
		}
		if (count < 0 && errno == EINTR)
			continue;
		set_error("cannot write temporary SVG URL: %s",
			  count < 0 ? strerror(errno) : "short write");
		close(descriptor);
		unlink(temporary);
		free(temporary);
		return 0;
	}
	success = fsync(descriptor) == 0;
	if (!success)
		error = errno;
	if (close(descriptor) != 0) {
		success = 0;
		error = errno;
	}
	if (!success) {
		set_error("cannot finish temporary SVG URL: %s", strerror(error));
		unlink(temporary);
		free(temporary);
		return 0;
	}
	if (rename(temporary, url->path) != 0) {
		set_error("cannot replace SVG URL: %s", strerror(errno));
		unlink(temporary);
		free(temporary);
		return 0;
	}
	free(temporary);
	return 1;
}
