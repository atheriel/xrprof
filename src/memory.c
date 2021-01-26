#ifdef __linux
#define _GNU_SOURCE  /* for process_vm_readv */
#endif

#include <stdio.h>   /* for fprintf, perror, stderr */

#include "memory.h"
#include "rdefs.h"

#ifdef __linux
#include <sys/uio.h> /* for iovec, process_vm_readv */

/* No-op on Linux. */
int phandle_init(phandle *out, void *data) {
  pid_t pid = *((pid_t *) data);
  *out = pid;
  return 0;
}

ssize_t copy_address(phandle pid, void *addr, void *data, size_t len) {
  struct iovec local[1];
  local[0].iov_base = data;
  local[0].iov_len = len;

  struct iovec remote[1];
  remote[0].iov_base = addr;
  remote[0].iov_len = len;

  ssize_t bytes = process_vm_readv(pid, local, 1, remote, 1, 0);
  if (bytes < 0) {
    perror("error: Failed to read memory in the remote process");
  } else if (bytes < len) {
    fprintf(stderr, "error: Partial read of memory in remote process.\n");
  }
  return bytes;
}
#elif defined(__WIN32)
#include <windows.h> /* for ReadProcessMemory, GetLastError */

ssize_t copy_address(phandle pid, void *addr, void *data, size_t len) {
  if (!ReadProcessMemory(pid, addr, data, len, NULL)) {
    fprintf(stderr, "error: Failed to read memory in the remote process: %ld.\n",
            GetLastError());
    return -1;
  }
  return len;
}
#elif defined(__MACH__) // macOS support.
ssize_t copy_address(phandle task, void *addr, void *data, size_t len)
{
    fprintf(stderr, "error: macOS is not yet supported.\n");
    return -1;
}
#else
#error "No support for this platform."
#endif

int copy_context(phandle pid, void *addr, RCNTXT *data) {
  if (!addr) {
    return -1;
  }

  size_t len = sizeof(RCNTXT);
  ssize_t bytes = copy_address(pid, addr, data, len);
  if (bytes < len) {
    return -2;
  }

  return 0;
}

int copy_sexp(phandle pid, void *addr, SEXP data) {
  if (!addr) {
    return -1;
  }

  size_t len = sizeof(SEXPREC);
  ssize_t bytes = copy_address(pid, addr, data, len);
  if (bytes < len) {
    return -2;
  }

  return 0;
}

int copy_char(phandle pid, void *addr, char *data, size_t max_len) {
  if (!addr) {
    return -1;
  }
  SEXPREC_ALIGN vec;
  size_t len;
  ssize_t bytes;
  void *str_addr = STDVEC_DATAPTR(addr);

  /* We need to do this is two passes. First, we read the VECSEXP data to get
     the length of the data, and then we use that length and the data pointer
     address to read the actual character array. */

  len = sizeof(SEXPREC_ALIGN);
  bytes = copy_address(pid, addr, &vec, len);
  if (bytes < len) {
    return -2;
  }

  len = vec.s.vecsxp.length + 1 > max_len ? max_len : vec.s.vecsxp.length + 1;

  data[len] = '\0';
  bytes = copy_address(pid, str_addr, data, len);
  if (bytes < len) {
    return -2;
  }

  return 0;
}
