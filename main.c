#include "korsvg.h"

#include <archetypon.h>

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef int32_t s32;
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

#define KORSVG_MAX_SOURCE (32u * 1024u * 1024u)
#define KORSVG_MAX_PATH 4096u
#define KORSVG_MAX_PIXELS 16777216u

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

struct CGSVGDocument {
	atomic_uint references;
	u8 *source;
	size_t source_length;
	CGSize canvas_size;
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

static CFDataRef data_allocate(s32 writable)
{
	CFDataRef data = (CFDataRef)calloc(1, sizeof(*data));

	if (!data) {
		set_error("out of memory creating data");
		return NULL;
	}
	atomic_init(&data->references, 1);
	data->writable = writable;
	return data;
}

static s32 data_append(CFDataRef data, const u8 *bytes, size_t length)
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

CFDataRef KorSVGDataCreate(const void *bytes, size_t length)
{
	CFDataRef data;

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

CFDataRef KorSVGDataCreateMutable(void)
{
	clear_error();
	return data_allocate(1);
}

CFDataRef KorSVGDataRetain(CFDataRef data)
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

void KorSVGDataRelease(CFDataRef data)
{
	if (!data)
		return;
	if (atomic_fetch_sub_explicit(&data->references, 1,
				      memory_order_acq_rel) != 1)
		return;
	free(data->bytes);
	free(data);
}

const u8 *KorSVGDataGetBytes(CFDataRef data)
{
	return data ? data->bytes : NULL;
}

size_t KorSVGDataGetLength(CFDataRef data)
{
	return data ? data->length : 0;
}

s32 KorSVGDataIsMutable(CFDataRef data)
{
	return data && data->writable;
}

CFURLRef KorSVGURLCreate(const char *path)
{
	size_t length;
	CFURLRef url;

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
	url = (CFURLRef)calloc(1, sizeof(*url));
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

CFURLRef KorSVGURLRetain(CFURLRef url)
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

void KorSVGURLRelease(CFURLRef url)
{
	if (!url)
		return;
	if (atomic_fetch_sub_explicit(&url->references, 1,
				      memory_order_acq_rel) != 1)
		return;
	free(url->path);
	free(url);
}

const char *KorSVGURLGetPath(CFURLRef url)
{
	return url ? url->path : NULL;
}

CGContextRef KorSVGContextCreate(s32 width, s32 height)
{
	CGContextRef context;
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
	context = (CGContextRef)calloc(1, sizeof(*context));
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

void KorSVGContextRelease(CGContextRef context)
{
	if (context) {
		free(context->pixels);
		free(context);
	}
}

s32 KorSVGContextSetViewport(CGContextRef context, s32 x, s32 y, s32 width,
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

s32 KorSVGContextClear(CGContextRef context, u8 red, u8 green, u8 blue,
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

u8 *KorSVGContextGetData(CGContextRef context)
{
	return context ? context->pixels : NULL;
}

size_t KorSVGContextGetStride(CGContextRef context)
{
	return context ? context->stride : 0;
}

s32 KorSVGContextGetWidth(CGContextRef context)
{
	return context ? context->width : 0;
}

s32 KorSVGContextGetHeight(CGContextRef context)
{
	return context ? context->height : 0;
}

const char *KorSVGGetLastError(void)
{
	return korsvg_error;
}

CFTypeID CGSVGDocumentGetTypeID(void)
{
	return UINT64_C(0x4347535647444f43);
}

static s32 validate_svg_data(CFDataRef data, double *width, double *height)
{
	struct archetypon_image probe = { 0 };
	char error[256] = { 0 };

	if (!data || !data->bytes || data->length == 0 ||
	    data->length > KORSVG_MAX_SOURCE)
		return set_error("SVG data is invalid");
	if (memchr(data->bytes, 0, data->length))
		return set_error("SVG data contains a null byte");
	if (archetypon_svg_canvas_size((const char *)data->bytes, data->length,
				       width, height, error, sizeof(error)) ||
	    !isfinite(*width) || !isfinite(*height) || *width <= 0 ||
	    *height <= 0)
		return set_error("%s", error[0] == 0 ?
				"SVG canvas is invalid" : error);
	if (archetypon_svg_render((const char *)data->bytes, data->length, 1, 1,
				  &probe, error, sizeof(error))) {
		archetypon_image_free(&probe);
		return set_error("%s",
				error[0] == 0 ? "SVG parse failed" : error);
	}
	archetypon_image_free(&probe);
	return 1;
}

CGSVGDocumentRef CGSVGDocumentCreateFromData(CFDataRef data,
					     CFDictionaryRef options)
{
	CGSVGDocumentRef document;
	double width;
	double height;

	(void)options;
	clear_error();
	if (!validate_svg_data(data, &width, &height))
		return NULL;
	document = (CGSVGDocumentRef)calloc(1, sizeof(*document));
	if (!document) {
		set_error("out of memory creating SVG document");
		return NULL;
	}
	document->source = (u8 *)malloc(data->length);
	if (!document->source) {
		free(document);
		set_error("out of memory copying SVG document");
		return NULL;
	}
	memcpy(document->source, data->bytes, data->length);
	document->source_length = data->length;
	document->canvas_size = (CGSize){width, height};
	atomic_init(&document->references, 1);
	return document;
}

static CFDataRef read_svg_url(CFURLRef url)
{
	struct stat status;
	FILE *file;
	u8 *bytes;
	size_t length;
	CFDataRef data;

	if (!url || !url->path || stat(url->path, &status) != 0 ||
	    !S_ISREG(status.st_mode) || status.st_size <= 0 ||
	    (u64)status.st_size > KORSVG_MAX_SOURCE) {
		set_error("SVG URL is not a readable file");
		return NULL;
	}
	length = (size_t)status.st_size;
	bytes = (u8 *)malloc(length);
	if (!bytes) {
		set_error("out of memory reading SVG URL");
		return NULL;
	}
	file = fopen(url->path, "rb");
	if (!file) {
		free(bytes);
		set_error("cannot open SVG URL: %s", strerror(errno));
		return NULL;
	}
	if (fread(bytes, 1, length, file) != length) {
		fclose(file);
		free(bytes);
		set_error("cannot read SVG URL");
		return NULL;
	}
	if (fclose(file) != 0) {
		free(bytes);
		set_error("cannot close SVG URL");
		return NULL;
	}
	data = KorSVGDataCreate(bytes, length);
	free(bytes);
	return data;
}

CGSVGDocumentRef CGSVGDocumentCreateFromURL(CFURLRef url,
					     CFDictionaryRef options)
{
	CFDataRef data;
	CGSVGDocumentRef document;

	clear_error();
	data = read_svg_url(url);
	if (!data)
		return NULL;
	document = CGSVGDocumentCreateFromData(data, options);
	KorSVGDataRelease(data);
	return document;
}

CGSVGDocumentRef CGSVGDocumentRetain(CGSVGDocumentRef document)
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

void CGSVGDocumentRelease(CGSVGDocumentRef document)
{
	if (!document)
		return;
	if (atomic_fetch_sub_explicit(&document->references, 1,
				      memory_order_acq_rel) != 1)
		return;
	free(document->source);
	free(document);
}

CGSize CGSVGDocumentGetCanvasSize(CGSVGDocumentRef document)
{
	clear_error();
	if (!document) {
		set_error("SVG document is missing");
		return (CGSize){ 0, 0 };
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

void CGContextDrawSVGDocument(CGContextRef context,
			      CGSVGDocumentRef document)
{
	struct archetypon_image image = { 0 };
	char error[256] = { 0 };
	s32 y;

	clear_error();
	if (!context || !document) {
		set_error("context and SVG document are required");
		return;
	}
	if (archetypon_svg_render((const char *)document->source,
				   document->source_length,
				   context->viewport_width,
				   context->viewport_height, &image, error,
				   sizeof(error))) {
		archetypon_image_free(&image);
		set_error("%s", error[0] == 0 ? "SVG draw failed" : error);
		return;
	}
	for (y = 0; y < image.height; y++) {
		s32 x;
		u8 *destination = context->pixels;
		const u8 *source = image.pixels + (size_t)y * image.width * 4;

		destination +=
			(size_t)(context->viewport_y + y) * context->stride;
		destination += (size_t)context->viewport_x * 4;
		for (x = 0; x < image.width; x++) {
			composite_pixel(destination + (size_t)x * 4,
					source + (size_t)x * 4);
		}
	}
	archetypon_image_free(&image);
}

s32 CGSVGDocumentWriteToData(CGSVGDocumentRef document, CFDataRef data,
			     CFDictionaryRef options)
{
	(void)options;
	clear_error();
	if (!document)
		return set_error("SVG document is missing");
	return data_append(data, document->source, document->source_length);
}

s32 CGSVGDocumentWriteToURL(CGSVGDocumentRef document, CFURLRef url,
			    CFDictionaryRef options)
{
	FILE *file;
	s32 success;

	(void)options;
	clear_error();
	if (!document || !url || !url->path)
		return set_error("SVG document and URL are required");
	file = fopen(url->path, "wb");
	if (!file) {
		return set_error("cannot open SVG URL for writing: %s",
				strerror(errno));
	}
	success = fwrite(document->source, 1, document->source_length, file) ==
		  document->source_length;
	if (fclose(file) != 0)
		success = 0;
	if (!success)
		return set_error("cannot write SVG URL");
	return 1;
}
