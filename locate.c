#include <dlfcn.h>      /* for dlopen, dlsym */
#include <stddef.h>     /* for ptrdiff_t */
#include <stdio.h>      /* for fprintf */
#include <stdlib.h>     /* for malloc */
#include <string.h>     /* for strstr, strndup */
#include <sys/ptrace.h> /* for ptrace */
#include <sys/wait.h>   /* for waitpid */

#include "locate.h"

#define MAX_LIBR_PATH_LEN 128

static int find_libR(pid_t pid, char **path, uintptr_t *addr) {
  char maps_file[32];
  snprintf(maps_file, sizeof(maps_file), "/proc/%d/maps", pid);
  FILE *file = fopen(maps_file, "r");
  if (!file) {
    perror("fopen");
    return -1;
  }

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
  return 0;
}

static ptrdiff_t sym_offset(void * dlhandle, uintptr_t start, const char *sym) {
  void* addr = dlsym(dlhandle, sym);
  if (!addr) {
    return 0; // This would never make sense, anyway.
  }

  return (ptrdiff_t) (addr - start);
}

static uintptr_t sym_value(void * dlhandle, pid_t pid, uintptr_t local, uintptr_t remote, const char *sym) {
  ptrdiff_t offset = sym_offset(dlhandle, local, sym);
  if (!offset) {
    fprintf(stderr, "%s\n", dlerror());
    return -1;
  }
  long value = ptrace(PTRACE_PEEKTEXT, pid, remote + offset, NULL);
  /* fprintf(stderr, "sym=%s; offset=%p; addr=%p; value=%p\n", */
  /*         sym, (void *) offset, (void *) remote + offset, (void *) value); */
  if (value < 0) {
    perror("ptrace PEEKTEXT");
    return 0;
  }

  return (uintptr_t) value;
}

libR_globals locate_libR_globals(pid_t pid) {
  /* Open the same libR.so in the tracer so we can determine the symbol offsets
     to read memory at in the tracee. */

  char *path = NULL;
  uintptr_t remote;
  if (find_libR(pid, &path, &remote) < 0) {
    fprintf(stderr, "could not locate libR.so in process memory");
    return NULL;
  }

  /* if (verbose) fprintf(stderr, "Found %s at %p in pid %d.\n", path, */
  /*                      (void *) addr, pid); */

  void *dlhandle = dlopen(path, RTLD_LAZY);
  if (!dlhandle) {
    fprintf(stderr, "%s\n", dlerror());
    return NULL;
  }
  free(path);

  uintptr_t local;
  if (find_libR(getpid(), &path, &local) < 0) {
    fprintf(stderr, "could not locate libR.so in process memory");
    return NULL;
  }

  /* if (verbose) fprintf(stderr, "Found %s at %p locally.\n", path, */
  /*                      (void *) local_addr); */
  free(path);

  /* The R_GlobalContext value will change, so we only want the address to read
     the value from. */

  ptrdiff_t context_offset = sym_offset(dlhandle, local, "R_GlobalContext");
  if (!context_offset) {
    fprintf(stderr, "%s\n", dlerror());
    return NULL;
  }

  /* For many global symbols, the values will never change, so we can just keep
     track of them directly. */

  if (ptrace(PTRACE_INTERRUPT, pid, NULL, NULL)) {
    perror("ptrace INTERRUPT");
    return NULL;
  }
  int wstatus;
  if (waitpid(pid, &wstatus, 0) < 0) {
    perror("waitpid");
    return NULL;
  }

  libR_globals ret = (libR_globals) malloc(sizeof(struct libR_globals_s));
  ret->context_addr = remote + context_offset;
  ret->doublecolon = sym_value(dlhandle, pid, local, remote, "R_DoubleColonSymbol");
  ret->triplecolon = sym_value(dlhandle, pid, local, remote, "R_TripleColonSymbol");
  ret->dollar = sym_value(dlhandle, pid, local, remote, "R_DollarSymbol");
  ret->bracket = sym_value(dlhandle, pid, local, remote, "R_BracketSymbol");
  if (!ret->doublecolon || !ret->triplecolon || !ret->dollar ||
      !ret->bracket) {
    free(ret);
    ret = NULL;
  }

  /* We can close the library and continue the process now. */

  dlclose(dlhandle);
  if (ptrace(PTRACE_CONT, pid, NULL, NULL)) {
    perror("ptrace CONT");
    free(ret);
    ret = NULL;
  }

  return ret;
}
