#ifndef XRPROF_MEMORY_H
#define XRPROF_MEMORY_H

#include <unistd.h> /* for pid_t */
#include "rdefs.h"  /* for RCNTXT, SEXP */

size_t copy_address(pid_t pid, void *addr, void *data, size_t len);
void copy_context(pid_t pid, void *addr, RCNTXT **data);
void copy_sexp(pid_t pid, void *addr, SEXP *data);
void copy_char(pid_t pid, void *addr, char **data);

#endif /* XRPROF_MEMORY_H */
