CC ?= cc
AR ?= ar
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?= -lm
PREFIX ?= /usr/local

OBJECTS := build/main.o build/src/core.o build/src/image.o build/src/svg.o

.PHONY: all clean install test

all: libhermeneus.a

libhermeneus.a: $(OBJECTS)
	$(AR) rcs "$@" $(OBJECTS)

build/main.o: main.c hermeneus.h src/parser.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/src/%.o: src/%.c src/parser.h src/internal.h
	mkdir -p build/src
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/test: tests/test.c hermeneus.h libhermeneus.a
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test.c libhermeneus.a $(LDFLAGS) $(LDLIBS) -o "$@"

test: build/test
	./tests/test.sh

install: libhermeneus.a
	install -d "$(DESTDIR)$(PREFIX)/include"
	install -m 644 hermeneus.h "$(DESTDIR)$(PREFIX)/include/hermeneus.h"
	install -d "$(DESTDIR)$(PREFIX)/lib"
	install -m 644 libhermeneus.a "$(DESTDIR)$(PREFIX)/lib/libhermeneus.a"

clean:
	rm -rf build libhermeneus.a
