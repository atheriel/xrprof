CFLAGS += -O2 -Wall -fPIC -g
LIBS := -ldl

R_HEADERS ?= $(shell Rscript -e "cat(R.home('include'))")
CFLAGS += -I$(R_HEADERS)

BIN := rtrace
BINOBJ := rtrace.o
OBJ := cursor.o \
  locate.o \
  memory.o
SHLIB := librtrace.so

all: $(BIN)

clean:
	$(RM) $(BIN) $(BINOBJ) $(OBJ) $(SHLIB)

$(BIN): $(OBJ) $(BINOBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

shlib: $(SHLIB)

$(SHLIB): $(OBJ)
	$(CC) $(LDFLAGS) -shared -o $@ $^

cursor.o: cursor.c cursor.h rdefs.h locate.h memory.h
	$(CC) $(CFLAGS) -c -o $@ $<

locate.o: locate.c locate.h
	$(CC) $(CFLAGS) -c -o $@ $<

memory.o: memory.c memory.h rdefs.h
	$(CC) $(CFLAGS) -c -o $@ $<

rtrace.o: rtrace.c cursor.h
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
	setcap cap_sys_ptrace=eip $(DESTDIR)$(bindir)/$(BIN)

install-shlib:
	$(INSTALL) -d $(DESTDIR)$(libdir)
	$(INSTALL) -T -m 0644 $(SHLIB) $(DESTDIR)$(libdir)/$(SHLIB)

.PHONY: all clean test install
