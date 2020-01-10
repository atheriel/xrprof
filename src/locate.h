#ifndef XRPROF_LOCATE_H
#define XRPROF_LOCATE_H

#include <stdint.h> /* for uintptr_t */
#include <unistd.h> /* for pid_t */

typedef struct libR_globals_s {
  uintptr_t context_addr;
  uintptr_t doublecolon;
  uintptr_t triplecolon;
  uintptr_t dollar;
  uintptr_t bracket;
} * libR_globals;

int locate_libR_globals(pid_t pid, libR_globals *globals);

#endif /* XRPROF_LOCATE_H */
