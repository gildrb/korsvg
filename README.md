# KorSVG

A portable C document API for parsing, retaining, measuring, drawing, and serializing SVG data through the reusable Archetypon library.

## Build

```sh
git clone https://github.com/gildrb/archetypon
git clone https://github.com/gildrb/korsvg
cd korsvg
make
make test
```

The default build expects both repositories under the same parent directory. Set `ARCHETYPON_DIR=/path/to/archetypon` when they are elsewhere. KorSVG builds Archetypon when its library is missing or stale.

The build produces `libkorsvg.a`. It contains the document adapter only; consumers link it with `libarchetypon.a` and the system math runtime:

```sh
cc app.c -I. -I../archetypon libkorsvg.a \
  ../archetypon/libarchetypon.a -lm
```

## Document surface

| Function | Contract |
| --- | --- |
| `CGSVGDocumentCreateFromData` | Copies immutable SVG bytes, verifies canvas geometry, and parses the complete supported document |
| `CGSVGDocumentCreateFromURL` | Reads at most 32 MiB from a regular file and delegates to data creation |
| `CGSVGDocumentRetain` / `CGSVGDocumentRelease` | Manage an atomic document reference count |
| `CGSVGDocumentGetCanvasSize` | Returns the root `viewBox` size, or root width and height when no `viewBox` exists |
| `CGContextDrawSVGDocument` | Renders into the active RGBA viewport and source-over composites the result |
| `CGSVGDocumentWriteToData` | Appends the original source bytes to mutable data |
| `CGSVGDocumentWriteToURL` | Writes the original source bytes to a file |
| `CGSVGDocumentGetTypeID` | Returns the stable KorSVG document type identifier |

`CFDataRef`, `CFURLRef`, `CGContextRef`, `CGSVGDocumentRef`, `CFDictionaryRef`, `CFTypeID`, and `CGSize` have local C definitions. No platform headers or dynamic symbol lookup are required. Options are accepted for call-shape compatibility and currently have no policy effect.

## Drawing

```c
#include "korsvg.h"

CFDataRef data = KorSVGDataCreate(svg, svg_length);
CGSVGDocumentRef document = CGSVGDocumentCreateFromData(data, NULL);
CGSize size = CGSVGDocumentGetCanvasSize(document);
CGContextRef context = KorSVGContextCreate(512, 512);

KorSVGContextSetViewport(context, 0, 0, 512, 512);
CGContextDrawSVGDocument(context, document);

uint8_t *rgba = KorSVGContextGetData(context);
size_t stride = KorSVGContextGetStride(context);

KorSVGContextRelease(context);
CGSVGDocumentRelease(document);
KorSVGDataRelease(data);
```

The context is straight-alpha RGBA with an explicit stride. Its default viewport is the full image. `KorSVGContextClear` initializes all pixels, and `KorSVGContextSetViewport` bounds the next and subsequent draws. Diagnostics are thread-local and available through `KorSVGGetLastError`.

## Parsing boundary

Document creation performs a one-pixel probe render. This forces unsupported rendering elements and properties to fail at creation rather than survive as deferred draw-time failures. The retained source remains byte-for-byte stable for serialization.

Supported SVG geometry, paths, transforms, solid paints, element opacity, fill rules, solid strokes, and round stroke joins are provided by Archetypon. Container opacity, dashed strokes, miter joins, and bevel joins are rejected. Gradients, text, external resources, filters, masks, patterns, clipping paths, CSS stylesheets, and nested viewports remain outside the renderer.

## Verification

`make test` proves:

1. data creation and immutable ownership;
2. document creation, type identity, retain/release, and canvas geometry;
3. viewport rendering, RGBA pixels, compositing bounds, and diagnostics;
4. byte-exact data and file serialization plus URL reloading;
5. rejection of malformed structure, non-finite geometry, unsupported effects, null inputs, and immutable-destination operations.

The test checks that the compatibility symbols are defined by `libkorsvg.a`, while the renderer symbols remain undefined Archetypon dependencies. `cc`, `clang`, ASan, and UBSan builds are supported through conventional `CC`, `CFLAGS`, and `LDFLAGS` overrides.

## Files

```text
korsvg/
  korsvg.h       public opaque document, data, URL, and RGBA context API
  main.c            ownership, I/O, document calls, rendering, compositing
  tests/test.c      API, lifetime, pixel, round-trip, and rejection proof
  tests/test.sh     isolated execution and exported-symbol proof
  Makefile          KorSVG build plus Archetypon dependency linkage
  LICENSE           MIT terms
```
