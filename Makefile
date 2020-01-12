CFLAGS += -O2 -Wall -fPIC -g
LIBS := -ldl

R_HEADERS ?= $(shell Rscript -e "cat(R.home('include'))")
CFLAGS += -I$(R_HEADERS)

BIN := xrprof
BINOBJ := src/xrprof.o
OBJ := src/cursor.o \
  src/locate.o \
  src/memory.o
SHLIB := libxrprof.so

all: $(BIN)

clean:
	$(RM) $(BIN) $(BINOBJ) $(OBJ) $(SHLIB)

$(BIN): $(OBJ) $(BINOBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

shlib: $(SHLIB)

$(SHLIB): $(OBJ)
	$(CC) $(LDFLAGS) -shared -o $@ $^

src/cursor.o: src/cursor.c src/cursor.h src/rdefs.h src/locate.h src/memory.h
	$(CC) $(CFLAGS) -c -o $@ $<

src/locate.o: src/locate.c src/locate.h src/memory.h
	$(CC) $(CFLAGS) -c -o $@ $<

src/memory.o: src/memory.c src/memory.h src/rdefs.h
	$(CC) $(CFLAGS) -c -o $@ $<

src/xrprof.o: src/xrprof.c src/cursor.h
	$(CC) $(CFLAGS) -c -o $@ $<

TEST_PROFILES := tests/sleep.out
test: $(TEST_PROFILES)

tests/%.out: tests/%.R
	sudo tests/harness.sh $<

# Mostly compatible with https://www.gnu.org/prep/standards/html_node/Makefile-Conventions.html
INSTALL = install
prefix = /usr/local
bindir = $(prefix)/bin
datadir = $(prefix)/share
includedir = $(prefix)/include
libdir = $(prefix)/lib

install:
	$(INSTALL) -d $(DESTDIR)$(bindir)
	$(INSTALL) -T -m 0755 $(BIN) $(DESTDIR)$(bindir)/$(BIN)
	$(INSTALL) -d $(DESTDIR)$(datadir)/man/man1
	$(INSTALL) -T -m 0755 docs/$(BIN).1 $(DESTDIR)$(datadir)/man/man1/$(BIN).1
	setcap cap_sys_ptrace=eip $(DESTDIR)$(bindir)/$(BIN) || exit 0

install-shlib:
	$(INSTALL) -d $(DESTDIR)$(libdir)
	$(INSTALL) -T -m 0644 $(SHLIB) $(DESTDIR)$(libdir)/$(SHLIB)

PACKAGE = $(BIN)
VERSION := $(shell git describe --tags --always | sed 's/^v//g')
DISTDIR = $(PACKAGE)-$(VERSION)

dist:
	$(INSTALL) -d $(DISTDIR)
	$(RM) -r $(DISTDIR)/*
	$(INSTALL) -d $(DISTDIR)/src
	cp src/*.c src/*.h $(DISTDIR)/src
	$(INSTALL) -d $(DISTDIR)/docs
	cp docs/* $(DISTDIR)/docs
	cp Makefile README.md $(DISTDIR)/
	tar -czf $(DISTDIR).tar.gz $(DISTDIR)
	$(RM) -r $(DISTDIR)
	sha256sum $(DISTDIR).tar.gz > $(DISTDIR).tar.gz.sha256

distclean: clean

.PHONY: all clean test install dist distclean
