#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' 0 HUP INT TERM

"$root/build/test" "$temporary"
"$root/build/test_cpp"
"$root/build/regression" "$root/tests/fixtures"

defined_symbols=$(nm -g "$root/libkorsvg.a" |
	awk '$0 !~ /(^|[[:space:]])U[[:space:]]/ { print $NF }' |
	sed 's/^_//')
undefined_symbols=$(nm -u "$root/libkorsvg.a" |
	awk '{ print $NF }' | sed 's/^_//')

symbols='KorSVGDocumentCreateFromData KorSVGDocumentRetain KorSVGDocumentRelease KorSVGDocumentGetCanvasSize KorSVGContextDrawDocument KorSVGDocumentWriteToData'
for symbol in $symbols; do
	printf '%s\n' "$defined_symbols" | grep -qx "$symbol"
done

dependencies='archetypon_svg_canvas_size archetypon_svg_render archetypon_image_free'
for symbol in $dependencies; do
	printf '%s\n' "$undefined_symbols" | grep -qx "$symbol"
done

if printf '%s\n' "$defined_symbols" | grep -Eq '^(archetypon_|korsvg_parser_)'; then
	printf 'KorSVG contains copied parser symbols\n' >&2
	exit 1
fi

make -s -C "$root" install DESTDIR="$temporary/stage" PREFIX=/usr
cat > "$temporary/installed.c" <<'EOF'
#include <korsvg.h>

int main(void)
{
	return KorSVGDocumentGetTypeID() == 0;
}
EOF
${CC:-cc} -std=c11 -I"$temporary/stage/usr/include" \
	"$temporary/installed.c" -L"$temporary/stage/usr/lib" \
	-lkorsvg -larchetypon -lm ${LDFLAGS:-} -o "$temporary/installed"
"$temporary/installed"
grep -q -- '-lkorsvg -larchetypon -lm' \
	"$temporary/stage/usr/lib/pkgconfig/korsvg.pc"

printf 'korsvg tests passed\n'
