R_HEADERS = /usr/include/R/
# R_HEADERS = ../R-3.5.2/src/include

CFLAGS := -O2 -Wall -fPIC -g
LIBS = -ldl
INCLUDES = -I$(R_HEADERS)

all: rtrace

rtrace: rtrace.c rtrace.h
	$(CC) $(CFLAGS) $(LIBS) $(INCLUDES) $< -o $@

clean:
	rm -f rtrace

.PHONY: all clean
