#define _GNU_SOURCE  /* for process_vm_readv */

#include <dlfcn.h>   /* for dlopen, dlsym */
#include <errno.h>   /* for errno */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>  /* for uintptr_t */
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h> /* for iovec, process_vm_readv */
#include <sys/wait.h>

#include "rtrace.h"

#define MAX_STACK_DEPTH 5
#define MAX_LIBR_PATH_LEN 128

int find_libR(pid_t pid, char **path, uintptr_t *addr) {
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

int main(int argc, char **argv) {
  pid_t pid = -1;
  int verbose = 0;

  int opt;
  while ((opt = getopt(argc, argv, "hvp:")) != -1) {
    switch (opt) {
    case 'h':
      // TODO: Add a long help message.
      printf("Usage: %s [-v] -p <pid>\n", argv[0]);
      return 0;
      break;
    case 'v':
      verbose++;
      break;
    case 'p':
      pid = strtol(optarg, NULL, 10);
      if ((errno == ERANGE && (pid == LONG_MAX || pid == LONG_MIN)) ||
          (errno != 0 && pid == 0)) {
        perror("strtol");
        return 1;
      }
      if (pid < 0) {
        fprintf(stderr, "cannot accept negative pids\n");
        return 1;
      }
      break;
    default: /* '?' */
      printf("Usage: %s [-v] -p <pid>\n", argv[0]);
      return 1;
      break;
    }
  }

  // A PID is required.
  if (pid == -1) {
    printf("Usage: %s [-v] -p <pid>\n", argv[0]);
    return 1;
  }

  int code = 0;

  /* First, check that we can attach to the process. */

  if (ptrace(PTRACE_ATTACH, pid, NULL, NULL)) {
    perror("ptrace ATTACH");
    return 1;
  }
  if (waitpid(pid, 0, WSTOPPED) < 0) {
    perror("waitpid");
    code++;
    goto done;
  }
  if (ptrace(PTRACE_CONT, pid, NULL, NULL)) {
    perror("ptrace CONT");
    code++;
    goto done;
  }

  /* Open the same libR.so in the tracer so we can determine the symbol offsets
     to read memory at in the tracee. */

  char *path = NULL;
  uintptr_t addr;
  if (find_libR(pid, &path, &addr) < 0) {
    fprintf(stderr, "could not locate libR.so in process memory");
    code++;
    goto done;
  }

  if (verbose) fprintf(stderr, "Found %s at %p in pid %d.\n", path,
                       (void *) addr, pid);

  void *handle = dlopen(path, RTLD_LAZY);
  if (!handle) {
    fprintf(stderr, "%s\n", dlerror());
    code++;
    goto done;
  }
  free(path);

  uintptr_t local_addr;
  if (find_libR(getpid(), &path, &local_addr) < 0) {
    fprintf(stderr, "could not locate libR.so in process memory");
    code++;
    goto done;
  }

  if (verbose) fprintf(stderr, "Found %s at %p locally.\n", path,
                       (void *) local_addr);
  free(path);

  void* context_addr = dlsym(handle, "R_GlobalContext");
  if (!context_addr) {
    fprintf(stderr, "%s\n", dlerror());
    code++;
    goto done;
  }
  ptrdiff_t context_offset = (uintptr_t) context_addr - local_addr;

  dlclose(handle);

  /* Stop the tracee and read the R stack information. */

  if (kill(pid, SIGSTOP) < 0) {
    perror("kill SIGSTOP");
    code++;
    goto done;
  }
  if (waitpid(pid, 0, WSTOPPED) < 0) {
    perror("waitpid");
    code++;
    goto done;
  }

  long context_ptr = ptrace(PTRACE_PEEKTEXT, pid, addr + context_offset, NULL);
  if (context_ptr < 0) {
    perror("ptrace PEEKTEXT");
    code++;
    goto done;
  }
  /* printf("R_GlobalContext contains %p in pid %d.\n", (void *) context_ptr, pid); */

  RCNTXT *cptr = NULL;
  SEXP call = NULL, fun = NULL, name = NULL;
  int depth = 0;
  addr = (uintptr_t) context_ptr;
  char stackbuff[1024];
  stackbuff[0] = '\0';

  printf("Format: bt\n\n");

  for (copy_context(pid, (void *) context_ptr, &cptr); cptr;
       copy_context(pid, (void *) cptr->nextcontext, &cptr)) {

    if (depth > MAX_STACK_DEPTH) {
      fprintf(stderr, "exceeded max stack depth (%d)\n", MAX_STACK_DEPTH);
      code++;
      goto done;
    }

    printf("  %p: call=%p,callflag=%d,nextcontext=%p\n", (void *) addr,
           (void *) cptr->call, cptr->callflag, (void *) cptr->nextcontext);

    /* We're at the top level. */
    if (cptr->callflag == CTXT_TOPLEVEL) {
      strcat(stackbuff, " 1");
      break; // Probably unnecessary.
    } else if (depth > 0) {
      strcat(stackbuff, ";");
    }

    copy_sexp(pid, (void *) cptr->call, &call);
    if (!call) {
      fprintf(stderr, "could not read call\n");
      code++;
      goto done;
    }

    if (cptr->callflag & (CTXT_FUNCTION | CTXT_BUILTIN) &&
        TYPEOF(call) == LANGSXP) {
      copy_sexp(pid, (void *) CAR(call), &fun);
      if (!fun) {
        fprintf(stderr, "lang item has no CAR\n");
        code++;
        goto done;
      }
      if (TYPEOF(fun) == SYMSXP) {
        copy_sexp(pid, (void *) PRINTNAME(fun), &name);
        printf("#%d %15p in %s ()\n", depth, (void *) addr, CHAR(name));
        strcat(stackbuff, CHAR(name));
      } else {
        printf("    TYPEOF(CAR(call)): %d\n", TYPEOF(fun));
      }
    } else {
      printf("    TYPEOF(call)=%d\n", TYPEOF(call));
    }

    addr = (uintptr_t) cptr->nextcontext;
    depth++;
  }

  printf("\nFormat: Flamegraph.pl\n\n%s\n", stackbuff);
  free(cptr);
  free(call);
  free(fun);
  free(name);

 done:
  if (ptrace(PTRACE_DETACH, pid, NULL, NULL)) {
    perror("ptrace DETACH");
    code++;
  }

  return code;
}
