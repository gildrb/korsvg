#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' 0 HUP INT TERM

"$root/build/test" "$temporary"

symbols='CGSVGDocumentCreateFromData CGSVGDocumentRetain CGSVGDocumentRelease CGSVGDocumentGetCanvasSize CGContextDrawSVGDocument CGSVGDocumentWriteToData'
for symbol in $symbols; do
	nm -g --defined-only "$root/libkorsvg.a" | grep -q " $symbol$"
done

dependencies='archetypon_svg_canvas_size archetypon_svg_render archetypon_image_free'
for symbol in $dependencies; do
	nm -u "$root/libkorsvg.a" | grep -q " $symbol$"
done

if nm -g --defined-only "$root/libkorsvg.a" |
	grep -Eq ' (archetypon_|korsvg_parser_)'; then
	printf 'KorSVG contains copied parser symbols\n' >&2
	exit 1
fi

printf 'korsvg tests passed\n'
