#ifdef __linux
#define _GNU_SOURCE  /* for process_vm_readv */
#include <stdio.h>   /* for fprintf, perror, stderr */
#include <stdlib.h>  /* for realloc */
#include <sys/uio.h> /* for iovec, process_vm_readv */

size_t copy_address(pid_t pid, void *addr, void *data, size_t len) {
  struct iovec local[1];
  local[0].iov_base = data;
  local[0].iov_len = len;

  struct iovec remote[1];
  remote[0].iov_base = addr;
  remote[0].iov_len = len;

  size_t bytes = process_vm_readv(pid, local, 1, remote, 1, 0);
  if (bytes < 0) {
    perror("error: Failed to read memory in the remote process");
  } else if (bytes < len) {
    fprintf(stderr, "error: Partial read of memory in remote process.\n");
  }
  return bytes;
}
#else
#error "No support for non-Linux platforms."
#endif

#include "rdefs.h"

void copy_context(pid_t pid, void *addr, RCNTXT **data) {
  if (!addr) { /* Makes loops easier. */
    goto fail;
  }

  size_t len = sizeof(RCNTXT);
  *data = (RCNTXT *) realloc(*data, len);
  size_t bytes = copy_address(pid, addr, *data, len);
  if (bytes < len) {
    goto fail;
  }
  return;

 fail:
  free(*data);
  data = NULL;
  return;
}

void copy_sexp(pid_t pid, void *addr, SEXP *data) {
  if (!addr) { /* Makes loops easier. */
    goto fail;
  }

  size_t len = sizeof(SEXPREC);
  *data = (SEXP) realloc(*data, len);
  size_t bytes = copy_address(pid, addr, *data, len);
  if (bytes < len) {
    goto fail;
  }
  return;

 fail:
  free(*data);
  data = NULL;
  return;
}

void copy_char(pid_t pid, void *addr, char **data) {
  if (!addr) { /* Makes loops easier. */
    goto fail;
  }
  size_t len, bytes;
  void *str_addr = STDVEC_DATAPTR(addr);

  /* We need to do this is two passes. First, we read the VECSEXP data to get
     the length of the data, and then we use that length and the data pointer
     address to read the actual character array. */

  len = sizeof(SEXPREC_ALIGN);
  SEXPREC_ALIGN *vec = (SEXPREC_ALIGN *) malloc(len);
  bytes = copy_address(pid, addr, vec, len);
  if (bytes < len) {
    goto fail;
  }

  len = vec->s.vecsxp.length;
  free(vec);

  *data = realloc(*data, len + 1);
  (*data)[len] = '\0';
  bytes = copy_address(pid, str_addr, *data, len);
  if (bytes < len) {
    goto fail;
  }

  return;

 fail:
  free(*data);
  data = NULL;
  return;
}
