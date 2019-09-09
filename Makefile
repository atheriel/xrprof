R_HEADERS := $(shell Rscript -e "cat(R.home('include'))")

CFLAGS := -O2 -Wall -fPIC -g
LIBS = -ldl
INCLUDES = -I$(R_HEADERS)

all: rtrace

rtrace.o: rtrace.c rtrace.h
	$(CC) -c $(CFLAGS) $(INCLUDES) $< -o $@

locate.o: locate.c locate.h
	$(CC) -c $(CFLAGS) $(INCLUDES) $< -o $@

memory.o: memory.c rtrace.h
	$(CC) -c $(CFLAGS) $(INCLUDES) $< -o $@

rtrace: rtrace.o locate.o memory.o
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LIBS)

clean:
	rm -f rtrace

.PHONY: all clean
