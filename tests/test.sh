#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' 0 HUP INT TERM

"$root/build/test" "$temporary"

symbols='CGSVGDocumentCreateFromData CGSVGDocumentRetain CGSVGDocumentRelease CGSVGDocumentGetCanvasSize CGContextDrawSVGDocument CGSVGDocumentWriteToData'
for symbol in $symbols; do
	nm -g --defined-only "$root/libhermeneus.a" | grep -q " $symbol$"
done

printf 'hermeneus tests passed\n'
