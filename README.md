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

The default build expects both repositories under the same parent directory. Set `ARCHETYPON_DIR=/path/to/archetypon` when they are elsewhere. KorSVG compiles the Archetypon sources into a project-local dependency archive and does not modify the Archetypon checkout.

The build produces `libkorsvg.a`. It contains the document adapter only; consumers link it with `libarchetypon.a` and the system math runtime:

```sh
cc app.c -I. libkorsvg.a build/libarchetypon.a -pthread -lm
```

Install both static libraries, the public headers, and `korsvg.pc` with:

```sh
make install PREFIX=/usr/local
cc app.c $(pkg-config --cflags --libs korsvg)
```

`DESTDIR` is supported for staged packaging. The pkg-config link flags propagate the Archetypon, pthread, and math dependencies.

## Document surface

| Function | Contract |
| --- | --- |
| `KorSVGDocumentCreateFromData` | Copies immutable SVG bytes, verifies canvas geometry, and parses the complete supported document |
| `KorSVGDocumentCreateFromURL` | Opens one regular-file descriptor, reads at most 32 MiB from it, and delegates to data creation |
| `KorSVGDocumentRetain` / `KorSVGDocumentRelease` | Manage an atomic document reference count |
| `KorSVGDocumentGetCanvasSize` | Returns the root `viewBox` size, or root width and height when no `viewBox` exists |
| `KorSVGContextDrawDocument` | Renders and composites into the active RGBA viewport; returns nonzero on success and zero on failure |
| `KorSVGDocumentWriteToData` | Appends the original source bytes to mutable data |
| `KorSVGDocumentWriteToURL` | Atomically replaces a file with the original source bytes after a complete temporary-file write |
| `KorSVGDocumentGetTypeID` | Returns the stable KorSVG document type identifier |

`KorSVGDataRef`, `KorSVGURLRef`, `KorSVGContextRef`, `KorSVGDocumentRef`, `KorSVGOptionsRef`, `KorSVGTypeID`, and `KorSVGSize` are KorSVG-owned types. This is an independent portable API, not a Core Foundation or Core Graphics compatibility layer. No platform headers or dynamic symbol lookup are required. Options are reserved for future KorSVG policy and currently have no effect. The header can be included from C or C++.

## Drawing

```c
#include "korsvg.h"

KorSVGDataRef data = KorSVGDataCreate(svg, svg_length);
KorSVGDocumentRef document = KorSVGDocumentCreateFromData(data, NULL);
KorSVGSize size = KorSVGDocumentGetCanvasSize(document);
KorSVGContextRef context = KorSVGContextCreate(512, 512);

KorSVGContextSetViewport(context, 0, 0, 512, 512);
if (!KorSVGContextDrawDocument(context, document)) {
        /* KorSVGGetLastError() describes the failure. */
}

uint8_t *rgba = KorSVGContextGetData(context);
size_t stride = KorSVGContextGetStride(context);

KorSVGContextRelease(context);
KorSVGDocumentRelease(document);
KorSVGDataRelease(data);
```

The context is straight-alpha RGBA with an explicit stride. Its default viewport is the full image. `KorSVGContextClear` initializes all pixels, and `KorSVGContextSetViewport` bounds the next and subsequent draws. Diagnostics are thread-local and available through `KorSVGGetLastError`.

## Retained plans

Each document owns one parsed Archetypon document. Its immutable source copy is
also used for exact serialization, so KorSVG does not retain a duplicate. Draws
use a mutex-protected two-entry LRU of immutable size-specific plans. The combined
cache cost is limited to 32 MiB. A plan larger than that limit is used for the
current draw but is not cached. `KorSVGDocumentSetPlanCacheLimit` lets an app lower
or raise the per-document bound; changing it clears existing entries.
`KorSVGDocumentGetPlanCacheCost` reports current retained plan bytes.

The cache mutex only protects lookup and publication. A separate build mutex
single-flights cold plans, preventing duplicate peak allocations. Cached draws
and compositing do not hold either mutex. Plans have atomic references, so eviction
cannot invalidate an in-progress draw.

The Archetypon document stores the compiled XML hierarchy, inherited styles,
visibility, and transforms. A new-size plan still parses numeric shape/path
geometry, flattens curves, rasterizes, and stores completed RGBA pixels. Cache
hits repeat none of that work.

## Concurrency

Data created with `KorSVGDataCreate`, URLs, and documents are immutable after creation. Separate threads may read them concurrently. Their retain and release operations use atomic reference counts, but each thread must own a reference for the full duration of its use. The final release must not overlap any use.

Mutable data and contexts require exclusive external synchronization. This includes reads through pointers returned by `KorSVGDataGetBytes` and `KorSVGContextGetData`. Calls on one thread replace that thread's last-error text; callers should inspect it before making another KorSVG call on the same thread.

URL loading validates and reads the descriptor returned by one `open` call, so path replacement cannot redirect an in-progress read. URL writing uses a temporary file in the destination directory and renames it only after the full write succeeds. Concurrent successful writers to the same path each publish a complete document, but which write wins is unspecified.

## Parsing boundary

Document creation performs a one-pixel probe render. This forces unsupported rendering elements and properties to fail at creation rather than survive as deferred draw-time failures. The retained source remains byte-for-byte stable for serialization.

Archetypon provides geometry, paths, transforms, solid and linear-gradient paints, inherited presentation styles, embedded simple CSS selectors, group and element opacity, fill and clip rules, dashed strokes with round/miter/bevel joins, clipping paths, and luminance or alpha masks. Retained group commands isolate container opacity, clipping, and masking before KorSVG caches the completed size-specific plan.

Unsupported features fail during document creation rather than rendering partially. These currently include text, images, external resources, radial gradients, filters, patterns, nested viewports, complex CSS selectors, nested clip/mask content, and non-pad gradient spread modes.

## Verification

`make test` proves:

1. data creation and immutable ownership;
2. document creation, type identity, retain/release, and canvas geometry;
3. viewport rendering, RGBA pixels, compositing bounds, and diagnostics;
4. byte-exact data and file serialization plus URL reloading;
5. rejection of malformed structure, non-finite geometry, unsupported effects, null inputs, and immutable-destination operations.

The test checks that the public symbols are defined by `libkorsvg.a`, while the renderer symbols remain undefined Archetypon dependencies. It also checks C++ linkage, a staged installed consumer, and generated render fixtures for geometry, transforms, paint and stroke styles, aspect-ratio mapping, and alpha compositing. Symbol checks work with GNU and Darwin `nm` output. Regenerate the committed fixtures with `make fixtures`.

GitHub Actions runs the C and C++ tests with GCC and Clang on Linux and with Clang on macOS. Separate jobs run ASan, UBSan, and Linux ThreadSanitizer, build the fuzz target, and verify that generated fixtures are current.

## Fuzzing

Clang with libFuzzer support can build the document create/draw harness:

```sh
make fuzz
./build/fuzz tests/fixtures -max_len=1048576 -timeout=10
```

The harness passes arbitrary bytes through document creation and, for accepted documents, draws into a context whose width and height are each bounded to 64 pixels. The `-max_len` example also bounds individual inputs. Sanitized Archetypon and KorSVG sources are compiled directly into the harness with ASan and UBSan.

## Files

```text
korsvg/
  korsvg.h          public opaque document, data, URL, and RGBA context API
  main.c            ownership, I/O, document calls, rendering, compositing
  tests/test.c      API, lifetime, pixel, round-trip, and rejection proof
  tests/test_cpp.cpp C++ header and linkage proof
  tests/regression.c generated SVG pixel and canvas regression proof
  tests/fuzz.c      bounded libFuzzer document create/draw harness
  tests/fixtures/   generated representative SVG corpus
  tests/test.sh     execution, install, and portable exported-symbol proof
  Makefile          KorSVG build plus Archetypon dependency linkage
  LICENSE           MIT terms
```
