#ifndef XRPROF_MEMORY_H
#define XRPROF_MEMORY_H

#include "process.h" /* for phandle */
#include "rdefs.h"  /* for RCNTXT, SEXP */

size_t copy_address(phandle pid, void *addr, void *data, size_t len);
int copy_context(phandle pid, void *addr, RCNTXT *data);
int copy_sexp(phandle pid, void *addr, SEXP data);
int copy_char(phandle pid, void *addr, char *data, size_t max_len);

#endif /* XRPROF_MEMORY_H */
