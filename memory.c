#define _GNU_SOURCE  /* for process_vm_readv */

#include <stdlib.h>  /* for realloc */
#include <sys/uio.h> /* for iovec, process_vm_readv */

#include "rdefs.h"

void copy_context(pid_t pid, void *addr, RCNTXT **data) {
  if (!addr) { /* Makes loops easier. */
    goto fail;
  }

  size_t len = sizeof(RCNTXT);
  *data = (RCNTXT *) realloc(*data, len);

  struct iovec local[1];
  local[0].iov_base = *data;
  local[0].iov_len = len;

  struct iovec remote[1];
  remote[0].iov_base = addr;
  remote[0].iov_len = len;

  size_t bytes = process_vm_readv(pid, local, 1, remote, 1, 0);
  if (bytes < 0) {
    perror("process_vm_readv");
    goto fail;
  } else if (bytes < len) {
    fprintf(stderr, "partial read of RCNTXT data\n");
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

  struct iovec local[1];
  local[0].iov_base = *data;
  local[0].iov_len = len;

  struct iovec remote[1];
  remote[0].iov_base = addr;
  remote[0].iov_len = len;

  size_t bytes = process_vm_readv(pid, local, 1, remote, 1, 0);
  if (bytes < 0) {
    perror("process_vm_readv");
    goto fail;
  } else if (bytes < len) {
    fprintf(stderr, "partial read of SEXP data\n");
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
  struct iovec local[1], remote[1];
  void *str_addr = STDVEC_DATAPTR(addr);

  /* We need to do this is two passes. First, we read the VECSEXP data to get
     the length of the data, and then we use that length and the data pointer
     address to read the actual character array. */

  len = sizeof(SEXPREC_ALIGN);
  SEXPREC_ALIGN *vec = (SEXPREC_ALIGN *) malloc(len);

  local[0].iov_base = vec;
  local[0].iov_len = len;
  remote[0].iov_base = addr;
  remote[0].iov_len = len;

  bytes = process_vm_readv(pid, local, 1, remote, 1, 0);
  if (bytes < 0) {
    perror("process_vm_readv");
    goto fail;
  } else if (bytes < len) {
    fprintf(stderr, "partial read of VECSEXP data\n");
    goto fail;
  }

  len = vec->s.vecsxp.length;
  free(vec);

  *data = realloc(*data, len + 1);
  (*data)[len] = '\0';

  local[0].iov_base = *data;
  local[0].iov_len = len;
  remote[0].iov_base = str_addr;
  remote[0].iov_len = len;

  bytes = process_vm_readv(pid, local, 1, remote, 1, 0);
  if (bytes < 0) {
    perror("process_vm_readv");
    goto fail;
  } else if (bytes < len) {
    fprintf(stderr, "partial read of CHAR data\n");
    goto fail;
  }

  return;

 fail:
  free(*data);
  data = NULL;
  return;
}
