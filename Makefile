CFLAGS += -O2 -Wall -fPIC -g
LIBS := -ldl

R_HEADERS ?= $(shell Rscript -e "cat(R.home('include'))")
CFLAGS += -I$(R_HEADERS)

BIN := rtrace
OBJ := cursor.o \
  locate.o \
  memory.o \
  rtrace.o

all: $(BIN)

clean:
	$(RM) $(BIN) $(OBJ)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

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

.PHONY: all clean test
