#include "../korsvg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sample {
	int32_t x;
	int32_t y;
	uint8_t rgba[4];
};

struct regression {
	const char *name;
	double width;
	double height;
	const struct sample *samples;
	size_t sample_count;
};

static const struct sample geometry_samples[] = {
	{ 16, 16, { 255, 0, 0, 255 } },
	{ 48, 16, { 0, 255, 0, 255 } },
	{ 16, 48, { 0, 0, 255, 255 } },
	{ 48, 52, { 255, 255, 0, 255 } },
};
static const struct sample transform_samples[] = {
	{ 16, 16, { 255, 0, 0, 255 } },
	{ 48, 16, { 0, 255, 0, 255 } },
	{ 12, 44, { 0, 0, 255, 255 } },
	{ 28, 8, { 0, 0, 0, 0 } },
};
static const struct sample style_samples[] = {
	{ 16, 16, { 255, 0, 255, 255 } },
	{ 8, 4, { 0, 255, 255, 255 } },
	{ 44, 8, { 255, 165, 0, 255 } },
	{ 48, 16, { 0, 0, 0, 0 } },
	{ 12, 44, { 255, 255, 255, 255 } },
};
static const struct sample aspect_samples[] = {
	{ 32, 8, { 0, 0, 0, 0 } },
	{ 2, 32, { 0, 255, 0, 255 } },
	{ 32, 32, { 128, 0, 128, 255 } },
	{ 32, 56, { 0, 0, 0, 0 } },
};
static const struct sample alpha_samples[] = {
	{ 16, 16, { 255, 0, 0, 128 } },
	{ 48, 16, { 85, 0, 170, 192 } },
};

static const struct regression regressions[] = {
	{ "geometry.svg", 32, 32, geometry_samples,
	  sizeof(geometry_samples) / sizeof(geometry_samples[0]) },
	{ "transforms.svg", 32, 32, transform_samples,
	  sizeof(transform_samples) / sizeof(transform_samples[0]) },
	{ "styles.svg", 32, 32, style_samples,
	  sizeof(style_samples) / sizeof(style_samples[0]) },
	{ "aspect.svg", 20, 10, aspect_samples,
	  sizeof(aspect_samples) / sizeof(aspect_samples[0]) },
	{ "alpha.svg", 16, 16, alpha_samples,
	  sizeof(alpha_samples) / sizeof(alpha_samples[0]) },
};

static int run_regression(const char *directory,
			  const struct regression *regression)
{
	char path[4096];
	uint8_t *source = NULL;
	long length;
	FILE *file = NULL;
	KorSVGDataRef data = NULL;
	KorSVGDocumentRef document = NULL;
	KorSVGContextRef context = NULL;
	KorSVGSize size;
	int status = 1;

	if (snprintf(path, sizeof(path), "%s/%s", directory, regression->name) >=
	    (int)sizeof(path)) {
		fprintf(stderr, "regression fixture path is too long\n");
		goto cleanup;
	}
	file = fopen(path, "rb");
	if (!file || fseek(file, 0, SEEK_END) != 0 ||
	    (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
		fprintf(stderr, "could not open regression fixture %s\n", path);
		goto cleanup;
	}
	source = malloc((size_t)length);
	if ((!source && length != 0) ||
	    fread(source, 1, (size_t)length, file) != (size_t)length) {
		fprintf(stderr, "could not read regression fixture %s\n", path);
		goto cleanup;
	}
	if (fclose(file) != 0) {
		file = NULL;
		fprintf(stderr, "could not close regression fixture %s\n", path);
		goto cleanup;
	}
	file = NULL;
	data = KorSVGDataCreate(source, (size_t)length);
	document = data ? KorSVGDocumentCreateFromData(data, NULL) : NULL;
	if (!document) {
		fprintf(stderr, "%s: %s\n", regression->name,
			KorSVGGetLastError());
		goto cleanup;
	}
	size = KorSVGDocumentGetCanvasSize(document);
	if (size.width != regression->width || size.height != regression->height) {
		fprintf(stderr, "%s: canvas size changed\n", regression->name);
		goto cleanup;
	}
	context = KorSVGContextCreate(64, 64);
	if (!context || !KorSVGContextClear(context, 0, 0, 0, 0) ||
	    !KorSVGContextDrawDocument(context, document)) {
		fprintf(stderr, "%s: %s\n", regression->name,
			KorSVGGetLastError());
		goto cleanup;
	}
	for (size_t i = 0; i < regression->sample_count; ++i) {
		const struct sample *sample = &regression->samples[i];
		const uint8_t *actual = KorSVGContextGetData(context) +
			(size_t)sample->y * KorSVGContextGetStride(context) +
			(size_t)sample->x * 4;
		if (memcmp(actual, sample->rgba, 4) != 0) {
			fprintf(stderr,
				"%s: pixel %d,%d changed: got %u,%u,%u,%u\n",
				regression->name, sample->x, sample->y, actual[0],
				actual[1], actual[2], actual[3]);
			goto cleanup;
		}
	}
	status = 0;

cleanup:
	if (file)
		fclose(file);
	free(source);
	KorSVGContextRelease(context);
	KorSVGDocumentRelease(document);
	KorSVGDataRelease(data);
	return status;
}

int main(int argument_count, char **arguments)
{
	if (argument_count != 2) {
		fprintf(stderr, "regression fixture directory is missing\n");
		return 1;
	}
	for (size_t i = 0; i < sizeof(regressions) / sizeof(regressions[0]); ++i)
		if (run_regression(arguments[1], &regressions[i]))
			return 1;
	puts("korsvg regression fixtures passed");
	return 0;
}
