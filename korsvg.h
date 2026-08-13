#ifndef KORSVG_H
#define KORSVG_H

#include <stddef.h>
#include <stdint.h>


typedef uint64_t CFTypeID;
typedef const void *CFDictionaryRef;
typedef struct KorSVGData *CFDataRef;
typedef struct KorSVGURL *CFURLRef;
typedef struct CGSVGDocument *CGSVGDocumentRef;
typedef struct KorSVGContext *CGContextRef;

typedef struct {
  double width;
  double height;
} CGSize;

CFDataRef KorSVGDataCreate(const void *bytes, size_t length);
CFDataRef KorSVGDataCreateMutable(void);
CFDataRef KorSVGDataRetain(CFDataRef data);
void KorSVGDataRelease(CFDataRef data);
const uint8_t *KorSVGDataGetBytes(CFDataRef data);
size_t KorSVGDataGetLength(CFDataRef data);
int32_t KorSVGDataIsMutable(CFDataRef data);

CFURLRef KorSVGURLCreate(const char *path);
CFURLRef KorSVGURLRetain(CFURLRef url);
void KorSVGURLRelease(CFURLRef url);
const char *KorSVGURLGetPath(CFURLRef url);

CGContextRef KorSVGContextCreate(int32_t width, int32_t height);
void KorSVGContextRelease(CGContextRef context);
int32_t KorSVGContextSetViewport(CGContextRef context, int32_t x, int32_t y,
                                    int32_t width, int32_t height);
int32_t KorSVGContextClear(CGContextRef context, uint8_t red, uint8_t green,
                              uint8_t blue, uint8_t alpha);
uint8_t *KorSVGContextGetData(CGContextRef context);
size_t KorSVGContextGetStride(CGContextRef context);
int32_t KorSVGContextGetWidth(CGContextRef context);
int32_t KorSVGContextGetHeight(CGContextRef context);

const char *KorSVGGetLastError(void);

CFTypeID CGSVGDocumentGetTypeID(void);
CGSVGDocumentRef CGSVGDocumentCreateFromData(CFDataRef data,
                                             CFDictionaryRef options);
CGSVGDocumentRef CGSVGDocumentCreateFromURL(CFURLRef url,
                                            CFDictionaryRef options);
CGSVGDocumentRef CGSVGDocumentRetain(CGSVGDocumentRef document);
void CGSVGDocumentRelease(CGSVGDocumentRef document);
CGSize CGSVGDocumentGetCanvasSize(CGSVGDocumentRef document);
void CGContextDrawSVGDocument(CGContextRef context,
                              CGSVGDocumentRef document);
int32_t CGSVGDocumentWriteToData(CGSVGDocumentRef document, CFDataRef data,
                                 CFDictionaryRef options);
int32_t CGSVGDocumentWriteToURL(CGSVGDocumentRef document, CFURLRef url,
                                CFDictionaryRef options);


#endif
