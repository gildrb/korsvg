CC ?= cc
CXX ?= c++
AR ?= ar
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?= -lm
PREFIX ?= /usr/local
ARCHETYPON_DIR ?= ../archetypon

ARCHETYPON_HEADER := $(ARCHETYPON_DIR)/archetypon.h
ARCHETYPON_SOURCES := $(wildcard $(ARCHETYPON_DIR)/src/*.c)
ARCHETYPON_OBJECTS := $(patsubst $(ARCHETYPON_DIR)/src/%.c,build/archetypon/%.o,$(ARCHETYPON_SOURCES))
ARCHETYPON_LIBRARY := build/libarchetypon.a
OBJECTS := build/main.o
FUZZ_CC ?= clang
FUZZ_CFLAGS ?= -std=c11 -O1 -g -fno-omit-frame-pointer \
	-fsanitize=fuzzer,address,undefined

.PHONY: all clean fixtures fuzz install test

all: $(ARCHETYPON_LIBRARY) libkorsvg.a

libkorsvg.a: $(OBJECTS)
	$(RM) "$@"
	$(AR) rcs "$@" $(OBJECTS)

$(ARCHETYPON_LIBRARY): $(ARCHETYPON_OBJECTS)
	$(RM) "$@"
	$(AR) rcs "$@" $(ARCHETYPON_OBJECTS)

build/archetypon/%.o: $(ARCHETYPON_DIR)/src/%.c $(ARCHETYPON_HEADER) \
                       $(ARCHETYPON_DIR)/src/internal.h
	mkdir -p build/archetypon
	$(CC) $(CPPFLAGS) -I"$(ARCHETYPON_DIR)" $(CFLAGS) -c "$<" -o "$@"

build/main.o: main.c korsvg.h $(ARCHETYPON_HEADER)
	mkdir -p build
	$(CC) $(CPPFLAGS) -I"$(ARCHETYPON_DIR)" $(CFLAGS) -c "$<" -o "$@"

build/test: tests/test.c korsvg.h libkorsvg.a $(ARCHETYPON_LIBRARY)
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test.c libkorsvg.a \
		$(ARCHETYPON_LIBRARY) $(LDFLAGS) $(LDLIBS) -o "$@"

build/test_cpp: tests/test_cpp.cpp korsvg.h libkorsvg.a $(ARCHETYPON_LIBRARY)
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/test_cpp.cpp libkorsvg.a \
		$(ARCHETYPON_LIBRARY) $(LDFLAGS) $(LDLIBS) -o "$@"

build/regression: tests/regression.c korsvg.h libkorsvg.a $(ARCHETYPON_LIBRARY)
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/regression.c libkorsvg.a \
		$(ARCHETYPON_LIBRARY) $(LDFLAGS) $(LDLIBS) -o "$@"

fixtures:
	./tests/generate_fixtures.sh

fuzz: build/fuzz

build/fuzz: tests/fuzz.c main.c korsvg.h $(ARCHETYPON_HEADER) \
             $(ARCHETYPON_SOURCES)
	mkdir -p build
	$(FUZZ_CC) $(CPPFLAGS) -I"$(ARCHETYPON_DIR)" $(FUZZ_CFLAGS) \
		tests/fuzz.c main.c $(ARCHETYPON_SOURCES) $(LDFLAGS) $(LDLIBS) \
		-o "$@"

build/korsvg.pc: Makefile
	mkdir -p build
	printf '%s\n' \
		'prefix=$(PREFIX)' \
		'exec_prefix=$${prefix}' \
		'libdir=$${exec_prefix}/lib' \
		'includedir=$${prefix}/include' \
		'' \
		'Name: KorSVG' \
		'Description: Portable C SVG document and RGBA rendering API' \
		'Version: 1.0.0' \
		'Libs: -L$${libdir} -lkorsvg -larchetypon -lm' \
		'Cflags: -I$${includedir}' > "$@"

test: build/test build/test_cpp build/regression
	./tests/test.sh

install: all build/korsvg.pc
	install -d "$(DESTDIR)$(PREFIX)/include"
	install -m 644 korsvg.h "$(DESTDIR)$(PREFIX)/include/korsvg.h"
	install -m 644 "$(ARCHETYPON_HEADER)" \
		"$(DESTDIR)$(PREFIX)/include/archetypon.h"
	install -d "$(DESTDIR)$(PREFIX)/lib"
	install -m 644 libkorsvg.a "$(DESTDIR)$(PREFIX)/lib/libkorsvg.a"
	install -m 644 "$(ARCHETYPON_LIBRARY)" \
		"$(DESTDIR)$(PREFIX)/lib/libarchetypon.a"
	install -d "$(DESTDIR)$(PREFIX)/lib/pkgconfig"
	install -m 644 build/korsvg.pc \
		"$(DESTDIR)$(PREFIX)/lib/pkgconfig/korsvg.pc"

clean:
	rm -rf build libkorsvg.a
