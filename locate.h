#ifndef RTRACE_LOCATE_H
#define RTRACE_LOCATE_H

#include <stdint.h> /* for uintptr_t */
#include <unistd.h> /* for pid_t */

typedef struct libR_globals_s {
  uintptr_t context_addr;
  uintptr_t doublecolon;
  uintptr_t triplecolon;
  uintptr_t dollar;
  uintptr_t bracket;
} * libR_globals;

libR_globals locate_libR_globals(pid_t pid);

#endif /* RTRACE_LOCATE_H */
