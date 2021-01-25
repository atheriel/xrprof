VERSION = 0.3.0

CFLAGS = -O2 -Wall -fPIC -mno-ms-bitfields -g
LIBS = -lelf -lunwind-ptrace -lunwind-generic

BIN = xrprof
BINOBJ = src/xrprof.o
OBJ = src/cursor.o \
  src/locate.o \
  src/memory.o \
  src/process.o
SHLIB = libxrprof.so

all: $(BIN)

clean:
	$(RM) $(BIN) $(BINOBJ) $(OBJ) $(SHLIB)
	cd tests && $(MAKE) clean

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

src/process.o: src/process.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/xrprof.o: src/xrprof.c src/cursor.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(BIN)
	cd tests && $(MAKE) "BIN=../$(BIN)"

# Mostly compatible with https://www.gnu.org/prep/standards/html_node/Makefile-Conventions.html
INSTALL = install
prefix ?= /usr/local
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
DISTDIR = $(PACKAGE)-$(VERSION)

dist:
	$(INSTALL) -d $(DISTDIR)
	$(RM) -r $(DISTDIR)/*
	$(INSTALL) -d $(DISTDIR)/src
	cp src/*.c src/*.h $(DISTDIR)/src
	$(INSTALL) -d $(DISTDIR)/docs
	cp docs/* $(DISTDIR)/docs
	cp Makefile README.md NEWS.md $(DISTDIR)/
	tar -czf $(DISTDIR).tar.gz $(DISTDIR)
	$(RM) -r $(DISTDIR)
	sha256sum $(DISTDIR).tar.gz > $(DISTDIR).tar.gz.sha256

distclean:
	$(RM) $(BIN) $(BINOBJ) $(OBJ) $(SHLIB)

.PHONY: all clean test install dist distclean
