#ifndef XRPROF_MEMORY_H
#define XRPROF_MEMORY_H

#include <unistd.h> /* for pid_t */
#include "rdefs.h"  /* for RCNTXT, SEXP */

size_t copy_address(pid_t pid, void *addr, void *data, size_t len);
int copy_context(pid_t pid, void *addr, RCNTXT *data);
int copy_sexp(pid_t pid, void *addr, SEXP data);
int copy_char(pid_t pid, void *addr, char *data, size_t max_len);

#endif /* XRPROF_MEMORY_H */
