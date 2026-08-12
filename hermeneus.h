#ifndef HERMENEUS_H
#define HERMENEUS_H

#include <stddef.h>
#include <stdint.h>


typedef uint64_t CFTypeID;
typedef const void *CFDictionaryRef;
typedef struct HermeneusData *CFDataRef;
typedef struct HermeneusURL *CFURLRef;
typedef struct CGSVGDocument *CGSVGDocumentRef;
typedef struct HermeneusContext *CGContextRef;

typedef struct {
  double width;
  double height;
} CGSize;

CFDataRef HermeneusDataCreate(const void *bytes, size_t length);
CFDataRef HermeneusDataCreateMutable(void);
CFDataRef HermeneusDataRetain(CFDataRef data);
void HermeneusDataRelease(CFDataRef data);
const uint8_t *HermeneusDataGetBytes(CFDataRef data);
size_t HermeneusDataGetLength(CFDataRef data);
int32_t HermeneusDataIsMutable(CFDataRef data);

CFURLRef HermeneusURLCreate(const char *path);
CFURLRef HermeneusURLRetain(CFURLRef url);
void HermeneusURLRelease(CFURLRef url);
const char *HermeneusURLGetPath(CFURLRef url);

CGContextRef HermeneusContextCreate(int32_t width, int32_t height);
void HermeneusContextRelease(CGContextRef context);
int32_t HermeneusContextSetViewport(CGContextRef context, int32_t x, int32_t y,
                                    int32_t width, int32_t height);
int32_t HermeneusContextClear(CGContextRef context, uint8_t red, uint8_t green,
                              uint8_t blue, uint8_t alpha);
uint8_t *HermeneusContextGetData(CGContextRef context);
size_t HermeneusContextGetStride(CGContextRef context);
int32_t HermeneusContextGetWidth(CGContextRef context);
int32_t HermeneusContextGetHeight(CGContextRef context);

const char *HermeneusGetLastError(void);

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
