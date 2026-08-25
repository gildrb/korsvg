#ifndef KORSVG_H
#define KORSVG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t KorSVGTypeID;
typedef const void *KorSVGOptionsRef;
typedef struct KorSVGData *KorSVGDataRef;
typedef struct KorSVGURL *KorSVGURLRef;
typedef struct KorSVGDocument *KorSVGDocumentRef;
typedef struct KorSVGContext *KorSVGContextRef;

typedef struct {
	double width;
	double height;
} KorSVGSize;

KorSVGDataRef KorSVGDataCreate(const void *bytes, size_t length);
KorSVGDataRef KorSVGDataCreateMutable(void);
KorSVGDataRef KorSVGDataRetain(KorSVGDataRef data);
void KorSVGDataRelease(KorSVGDataRef data);
const uint8_t *KorSVGDataGetBytes(KorSVGDataRef data);
size_t KorSVGDataGetLength(KorSVGDataRef data);
int32_t KorSVGDataIsMutable(KorSVGDataRef data);

KorSVGURLRef KorSVGURLCreate(const char *path);
KorSVGURLRef KorSVGURLRetain(KorSVGURLRef url);
void KorSVGURLRelease(KorSVGURLRef url);
const char *KorSVGURLGetPath(KorSVGURLRef url);

KorSVGContextRef KorSVGContextCreate(int32_t width, int32_t height);
void KorSVGContextRelease(KorSVGContextRef context);
int32_t KorSVGContextSetViewport(KorSVGContextRef context, int32_t x, int32_t y,
				 int32_t width, int32_t height);
int32_t KorSVGContextClear(KorSVGContextRef context, uint8_t red, uint8_t green,
			    uint8_t blue, uint8_t alpha);
uint8_t *KorSVGContextGetData(KorSVGContextRef context);
size_t KorSVGContextGetStride(KorSVGContextRef context);
int32_t KorSVGContextGetWidth(KorSVGContextRef context);
int32_t KorSVGContextGetHeight(KorSVGContextRef context);

const char *KorSVGGetLastError(void);

KorSVGTypeID KorSVGDocumentGetTypeID(void);
KorSVGDocumentRef KorSVGDocumentCreateFromData(KorSVGDataRef data,
					     KorSVGOptionsRef options);
KorSVGDocumentRef KorSVGDocumentCreateFromURL(KorSVGURLRef url,
					    KorSVGOptionsRef options);
KorSVGDocumentRef KorSVGDocumentRetain(KorSVGDocumentRef document);
void KorSVGDocumentRelease(KorSVGDocumentRef document);
KorSVGSize KorSVGDocumentGetCanvasSize(KorSVGDocumentRef document);
int32_t KorSVGContextDrawDocument(KorSVGContextRef context,
			         KorSVGDocumentRef document);
int32_t KorSVGDocumentWriteToData(KorSVGDocumentRef document, KorSVGDataRef data,
				 KorSVGOptionsRef options);
int32_t KorSVGDocumentWriteToURL(KorSVGDocumentRef document, KorSVGURLRef url,
				KorSVGOptionsRef options);

void KorSVGDocumentSetPlanCacheLimit(KorSVGDocumentRef document, size_t bytes);
size_t KorSVGDocumentGetPlanCacheCost(KorSVGDocumentRef document);

#ifdef __cplusplus
}
#endif

#endif
