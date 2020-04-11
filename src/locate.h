#ifndef XRPROF_LOCATE_H
#define XRPROF_LOCATE_H

#include <stdint.h> /* for uintptr_t */
#include "process.h"

struct libR_globals {
  uintptr_t context_addr;
  uintptr_t doublecolon;
  uintptr_t triplecolon;
  uintptr_t dollar;
  uintptr_t bracket;
};

int locate_libR_globals(phandle pid, struct libR_globals *out);

#endif /* XRPROF_LOCATE_H */
