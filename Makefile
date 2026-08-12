CC ?= cc
AR ?= ar
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?= -lm
PREFIX ?= /usr/local
ARCHETYPON_DIR ?= ../archetypon

ARCHETYPON_HEADER := $(ARCHETYPON_DIR)/archetypon.h
ARCHETYPON_LIBRARY := $(ARCHETYPON_DIR)/libarchetypon.a
ARCHETYPON_SOURCES := $(ARCHETYPON_HEADER) $(wildcard $(ARCHETYPON_DIR)/src/*.c) \
                      $(wildcard $(ARCHETYPON_DIR)/src/*.h)
OBJECTS := build/main.o

.PHONY: all clean install test

all: $(ARCHETYPON_LIBRARY) libhermeneus.a

libhermeneus.a: $(OBJECTS)
	$(RM) "$@"
	$(AR) rcs "$@" $(OBJECTS)

$(ARCHETYPON_LIBRARY): $(ARCHETYPON_SOURCES)
	$(MAKE) -C "$(ARCHETYPON_DIR)" libarchetypon.a

build/main.o: main.c hermeneus.h $(ARCHETYPON_HEADER)
	mkdir -p build
	$(CC) $(CPPFLAGS) -I"$(ARCHETYPON_DIR)" $(CFLAGS) -c "$<" -o "$@"

build/test: tests/test.c hermeneus.h libhermeneus.a $(ARCHETYPON_LIBRARY)
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test.c libhermeneus.a \
		$(ARCHETYPON_LIBRARY) $(LDFLAGS) $(LDLIBS) -o "$@"

test: build/test
	./tests/test.sh

install: all
	install -d "$(DESTDIR)$(PREFIX)/include"
	install -m 644 hermeneus.h "$(DESTDIR)$(PREFIX)/include/hermeneus.h"
	install -d "$(DESTDIR)$(PREFIX)/lib"
	install -m 644 libhermeneus.a "$(DESTDIR)$(PREFIX)/lib/libhermeneus.a"

clean:
	rm -rf build libhermeneus.a
