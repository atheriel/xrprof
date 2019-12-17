R_HEADERS := $(shell Rscript -e "cat(R.home('include'))")

CFLAGS := -O2 -Wall -fPIC -g
LIBS = -ldl
INCLUDES = -I$(R_HEADERS)

all: rtrace

rtrace.o: rtrace.c cursor.h
	$(CC) -c $(CFLAGS) $(INCLUDES) $< -o $@

locate.o: locate.c locate.h
	$(CC) -c $(CFLAGS) $(INCLUDES) $< -o $@

memory.o: memory.c memory.h rdefs.h
	$(CC) -c $(CFLAGS) $(INCLUDES) $< -o $@

cursor.o: cursor.c cursor.h rdefs.h locate.h memory.h
	$(CC) -c $(CFLAGS) $(INCLUDES) $< -o $@

rtrace: rtrace.o locate.o memory.o cursor.o
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LIBS)

clean:
	rm -f rtrace

TEST_PROFILES := tests/sleep.out
test: $(TEST_PROFILES)

tests/%.out: tests/%.R
	sudo tests/harness.sh $<

.PHONY: all clean test
