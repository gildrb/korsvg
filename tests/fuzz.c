#include "../korsvg.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *bytes, size_t length)
{
	KorSVGDataRef data;
	KorSVGDocumentRef document;
	KorSVGContextRef context;
	int32_t width = length > 0 ? 1 + bytes[0] % 64 : 1;
	int32_t height = length > 1 ? 1 + bytes[1] % 64 : 1;

	data = KorSVGDataCreate(bytes, length);
	if (!data)
		return 0;
	document = KorSVGDocumentCreateFromData(data, NULL);
	if (document) {
		context = KorSVGContextCreate(width, height);
		if (context) {
			(void)KorSVGContextClear(context, length > 0 ? bytes[0] : 0,
				length > 1 ? bytes[1] : 0,
				length > 2 ? bytes[2] : 0, 0);
			(void)KorSVGContextDrawDocument(context, document);
			KorSVGContextRelease(context);
		}
		KorSVGDocumentRelease(document);
	}
	KorSVGDataRelease(data);
	return 0;
}
