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

RCNTXT * get_context(pid_t pid, void *addr) {
  if (!addr) return NULL; /* Makes loops easier. */

  size_t len = sizeof(RCNTXT);
  RCNTXT *data = (RCNTXT *) malloc(len);

  struct iovec local[1];
  local[0].iov_base = data;
  local[0].iov_len = len;

  struct iovec remote[1];
  remote[0].iov_base = addr;
  remote[0].iov_len = len;

  size_t bytes = process_vm_readv(pid, local, 1, remote, 1, 0);
  if (bytes < 0) {
    perror("process_vm_readv");
    free(data);
    data = NULL;
  } else if (bytes < len) {
    fprintf(stderr, "partial read of RCNTXT data\n");
    free(data);
    data = NULL;
  }

  return data;
}

SEXP get_sexp(pid_t pid, void *addr) {
  if (!addr) return NULL; /* Makes loops easier. */

  size_t len = sizeof(SEXPREC);
  SEXP data = (SEXP) malloc(len);

  struct iovec local[1];
  local[0].iov_base = data;
  local[0].iov_len = len;

  struct iovec remote[1];
  remote[0].iov_base = addr;
  remote[0].iov_len = len;

  size_t bytes = process_vm_readv(pid, local, 1, remote, 1, 0);
  if (bytes < 0) {
    perror("process_vm_readv");
    free(data);
    data = NULL;
  } else if (bytes < len) {
    fprintf(stderr, "partial read of SEXP data\n");
    free(data);
    data = NULL;
  }

  return data;
}

int main(int argc, char **argv) {
  pid_t pid;
  if (argc != 2) {
    printf("Usage: %s PID\n", argv[0]);
    return 0;
  }
  pid = strtol(argv[1], NULL, 10);
  if (errno != 0) {
    perror("strtoul");
    return 1;
  } else if (pid < 0) {
    fprintf(stderr, "cannot accept negative pids\n");
    return 1;
  }

  int code = 0;

  if (ptrace(PTRACE_ATTACH, pid, NULL, NULL)) {
    perror("ptrace ATTACH");
    return 1;
  }
  if (waitpid(pid, 0, WSTOPPED) < 0) {
    perror("waitpid");
    code++;
    goto done;
  }

  /* Do something. */

  char *path = NULL;
  uintptr_t addr;
  if (find_libR(pid, &path, &addr) < 0) {
    fprintf(stderr, "could not locate libR.so in process memory");
    code++;
    goto done;
  }

  printf("Found %s at %p in pid %d.\n", path, (void *) addr, pid);
  void *handle = dlopen(path, RTLD_LAZY);
  if (!handle) {
    fprintf(stderr, "%s\n", dlerror());
    code++;
    goto done;
  }

  uintptr_t local_addr;
  if (find_libR(getpid(), &path, &local_addr) < 0) {
    fprintf(stderr, "could not locate libR.so in process memory");
    code++;
    goto done;
  }
  printf("Found %s at %p in pid %d.\n", path, (void *) local_addr, getpid());

  void* context_addr = dlsym(handle, "R_GlobalContext");
  if (!context_addr) {
    fprintf(stderr, "%s\n", dlerror());
    code++;
    goto done;
  }
  printf("Found R_GlobalContext at %p in pid %d.\n", context_addr, getpid());
  
  ptrdiff_t context_offset = (uintptr_t) context_addr - local_addr;

  long context_ptr = ptrace(PTRACE_PEEKTEXT, pid, addr + context_offset, NULL);
  if (context_ptr < 0) {
    perror("ptrace PEEKTEXT");
    code++;
    goto done;
  }
  printf("R_GlobalContext contains %p in pid %d.\n", (void *) context_ptr, pid);

  RCNTXT *cptr;
  int failsafe = 0;
  addr = (uintptr_t) context_ptr;
  printf("Stack:\n");
  char stackbuff[1024];
  stackbuff[0] = '\0';

  for (cptr = get_context(pid, (void *) context_ptr); cptr;
       cptr = get_context(pid, (void *) cptr->nextcontext)) {

    if (failsafe > MAX_STACK_DEPTH) {
      fprintf(stderr, "exceeded max stack depth (%d)\n", MAX_STACK_DEPTH);
      code++;
      goto done;
    }
    failsafe++;

    printf("  %p: call=%p,callflag=%d,nextcontext=%p\n", (void *) addr,
           (void *) cptr->call, cptr->callflag, (void *) cptr->nextcontext);

    /* We're at the top level. */
    if (cptr->callflag == CTXT_TOPLEVEL) {
      strcat(stackbuff, " 1");
    } else if (failsafe > 1) {
      strcat(stackbuff, ";");
    }

    SEXP call = get_sexp(pid, (void *) cptr->call);
    if (!call) {
      fprintf(stderr, "could not read call\n");
      code++;
      goto done;
    }

    if (cptr->callflag & (CTXT_FUNCTION | CTXT_BUILTIN) &&
        TYPEOF(call) == LANGSXP) {
      SEXP fun = get_sexp(pid, (void *) CAR(call));
      if (!fun) {
        fprintf(stderr, "lang item has no CAR\n");
        code++;
        goto done;
      }
      if (TYPEOF(fun) == SYMSXP) {
        SEXP name = get_sexp(pid, (void *) PRINTNAME(fun));
        printf("    function: %s (%ld %ld)\n", CHAR(name), strlen(CHAR(name)), STDVEC_LENGTH(name));
        strncat(stackbuff, CHAR(name), STDVEC_LENGTH(name));
      } else {
        printf("    TYPEOF(CAR(call)): %d\n", TYPEOF(fun));
      }
    } else {
      printf("    TYPEOF(call)=%d\n", TYPEOF(call));
    }

    addr = (uintptr_t) cptr->nextcontext;
  }

  printf("\nFlamegraph format:\n%s\n", stackbuff);

 done:
  if (ptrace(PTRACE_DETACH, pid, NULL, NULL)) {
    perror("ptrace DETACH");
    code++;
  }

  return code;
}
