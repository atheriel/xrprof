#include <dlfcn.h>      /* for dlopen, dlsym */
#include <stddef.h>     /* for ptrdiff_t */
#include <stdio.h>      /* for fprintf */
#include <stdlib.h>     /* for malloc */
#include <string.h>     /* for strstr, strndup */

#include "locate.h"
#include "memory.h"

#define MAX_LIBR_PATH_LEN 128

static int find_libR(pid_t pid, char **path, uintptr_t *addr) {
  char maps_file[32];
  snprintf(maps_file, sizeof(maps_file), "/proc/%d/maps", pid);
  FILE *file = fopen(maps_file, "r");
  if (!file) {
    char msg[51]; // 19 for the message + 32 for the buffer above.
    snprintf(msg, 51, "error: Cannot open %s", maps_file);
    perror(msg);
    return -1;
  }
  *path = NULL;

  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), file)) {
    if (strstr(buffer, "libR.so")) {
      /* Extract the address. */
      *addr = (uintptr_t) strtoul(buffer, NULL, 16);

      /* Extract the path, minus the trailing '\n'. */
      *path = strndup(strstr(buffer, "/"), MAX_LIBR_PATH_LEN);
      char *linebreak = strstr(*path, "\n");
      if (linebreak) {
        *linebreak = '\0';
      }

      break;
    }
  }

  fclose(file);

  /* Either (1) this R program does not use libR.so, or (2) it's not actually an
     R program. */
  if (!*path) {
    return -1;
  }
  return 0;
}

static ptrdiff_t sym_offset(void * dlhandle, uintptr_t start, const char *sym) {
  void* addr = dlsym(dlhandle, sym);
  if (!addr) {
    fprintf(stderr, "error: Failed to locate symbol %s: %s.\n", sym, dlerror());
    return 0; // This would never make sense, anyway.
  }

  return (ptrdiff_t) (addr - start);
}

static uintptr_t sym_value(void * dlhandle, pid_t pid, uintptr_t local, uintptr_t remote, const char *sym) {
  ptrdiff_t offset = sym_offset(dlhandle, local, sym);
  if (!offset) {
    /* sym_offset() will print its own error message. */
    return 0;
  }
  uintptr_t value;
  size_t bytes = copy_address(pid, (void *)remote + offset, &value,
                              sizeof(uintptr_t));
  /* fprintf(stderr, "sym=%s; offset=%p; addr=%p; value=%p\n", */
  /*         sym, (void *) offset, (void *) remote + offset, (void *) value); */
  if (bytes < sizeof(uintptr_t)) {
    /* copy_address() will have already printed an error. */
    return 0;
  }

  return value;
}

int locate_libR_globals(pid_t pid, libR_globals *globals) {
  /* Open the same libR.so in the tracer so we can determine the symbol offsets
     to read memory at in the tracee. */

  char *path = NULL;
  uintptr_t remote;
  if (find_libR(pid, &path, &remote) < 0) {
    fprintf(stderr, "error: Could not locate libR.so in process %d's memory. Are you sure it is an R program?\n",
            pid);
    return -1;
  }

  /* if (verbose) fprintf(stderr, "Found %s at %p in pid %d.\n", path, */
  /*                      (void *) addr, pid); */

  void *dlhandle = dlopen(path, RTLD_LAZY);
  if (!dlhandle) {
    fprintf(stderr, "error: Failed to dlopen() libR.so: %s\n", dlerror());
    return -1;
  }
  free(path);

  uintptr_t local;
  if (find_libR(getpid(), &path, &local) < 0) {
    fprintf(stderr, "error: Failed to load libR.so into local memory.\n");
    return -1;
  }

  /* if (verbose) fprintf(stderr, "Found %s at %p locally.\n", path, */
  /*                      (void *) local_addr); */
  free(path);

  /* The R_GlobalContext value will change, so we only want the address to read
     the value from. */

  ptrdiff_t context_offset = sym_offset(dlhandle, local, "R_GlobalContext");
  if (!context_offset) {
    /* sym_offset() will print its own error message. */
    return -1;
  }

  /* For many global symbols, the values will never change, so we can just keep
     track of them directly. */

  libR_globals ret = (libR_globals) malloc(sizeof(struct libR_globals_s));
  ret->context_addr = remote + context_offset;
  ret->doublecolon = sym_value(dlhandle, pid, local, remote, "R_DoubleColonSymbol");
  ret->triplecolon = sym_value(dlhandle, pid, local, remote, "R_TripleColonSymbol");
  ret->dollar = sym_value(dlhandle, pid, local, remote, "R_DollarSymbol");
  ret->bracket = sym_value(dlhandle, pid, local, remote, "R_BracketSymbol");
  if (!ret->doublecolon || !ret->triplecolon || !ret->dollar ||
      !ret->bracket) {
    fprintf(stderr, "error: Failed to locate required R global variables.\n");
    free(ret);
    ret = NULL;
  }

  /* We can close the library and continue the process now. */

  dlclose(dlhandle);

  if (!ret) {
    return -1;
  }

  *globals = ret;
  return 0;
}
