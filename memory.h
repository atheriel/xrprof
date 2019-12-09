#ifndef RTRACE_MEMORY_H
#define RTRACE_MEMORY_H

#include <unistd.h> /* for pid_t */
#include "rdefs.h"  /* for RCNTXT, SEXP */

void copy_context(pid_t pid, void *addr, RCNTXT **data);
void copy_sexp(pid_t pid, void *addr, SEXP *data);
void copy_char(pid_t pid, void *addr, char **data);

#endif /* RTRACE_MEMORY_H */
