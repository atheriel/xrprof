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
#include <time.h>    /* for timespec */

#include "rtrace.h"

#define MAX_STACK_DEPTH 100
#define MAX_LIBR_PATH_LEN 128
#define DEFAULT_FREQ 1
#define MAX_FREQ 1000
#define DEFAULT_DURATION 3600 // One hour.

static volatile int should_trace = 1;

void handle_sigint(int _sig) {
  should_trace = 0;
}

struct rstack_cursor {
  void *rcxt_ptr;
  RCNTXT *cptr;
  libR_globals globals;
  pid_t pid;
  int depth;
};

struct rstack_cursor *rstack_create(pid_t pid) {
  /* Find the symbols and addresses we need. */
  libR_globals globals = locate_libR_globals(pid);

  struct rstack_cursor *out = malloc(sizeof(struct rstack_cursor));
  out->rcxt_ptr = NULL;
  out->cptr = NULL;
  out->pid = pid;
  out->globals = globals;
  out->depth = 0;

  return out;
}

void rstack_destroy(struct rstack_cursor *cursor) {
  /* Clear any existing RCXT data. */
  if (cursor->cptr) {
    free(cursor->cptr);
    cursor->cptr = NULL;
  }
  free(cursor);
  return;
}

int rstack_get_fun_name(struct rstack_cursor *cursor, char *buff, size_t len) {
  SEXP call = NULL, fun = NULL;
  char *name = NULL;
  size_t written;

  if (!cursor || !cursor->cptr) {
    return -1;
  }

  /* We're at the top level. */
  if (cursor->cptr->callflag == CTXT_TOPLEVEL) {
    return 0;
  }

  copy_sexp(cursor->pid, (void *) cursor->cptr->call, &call);
  if (!call) {
    fprintf(stderr, "could not read call\n");
    return -1;
  }

  /* Adapted from R's eval.c code for Rprof. */

  if (cursor->cptr->callflag & (CTXT_FUNCTION | CTXT_BUILTIN | CTXT_CCODE) &&
      TYPEOF(call) == LANGSXP) {
    copy_sexp(cursor->pid, (void *) CAR(call), &fun);
    if (!fun) {
      fprintf(stderr, "lang item has no CAR\n");
      return -1;
    }
    if (TYPEOF(fun) == SYMSXP) {
      copy_char(cursor->pid, (void *) PRINTNAME(fun), &name);
      written = snprintf(buff, len, "%s", name);
    } else if (TYPEOF(fun) == LANGSXP) {
      SEXP cdr = NULL, lhs = NULL, rhs = NULL;
      char *lname = NULL, *rname = NULL;
      copy_sexp(cursor->pid, (void *) CDR(fun), &cdr);
      copy_sexp(cursor->pid, (void *) CAR(cdr), &lhs);
      copy_sexp(cursor->pid, (void *) CDR(cdr), &cdr);
      copy_sexp(cursor->pid, (void *) CAR(cdr), &rhs);
      if ((uintptr_t) CAR(fun) == cursor->globals->doublecolon &&
          TYPEOF(lhs) == SYMSXP && TYPEOF(rhs) == SYMSXP) {
        copy_char(cursor->pid, (void *) PRINTNAME(lhs), &lname);
        copy_char(cursor->pid, (void *) PRINTNAME(rhs), &rname);
        written = snprintf(buff, len, "%s::%s", lname, rname);
      } else if ((uintptr_t) CAR(fun) == cursor->globals->triplecolon &&
                 TYPEOF(lhs) == SYMSXP && TYPEOF(rhs) == SYMSXP) {
        copy_char(cursor->pid, (void *) PRINTNAME(lhs), &lname);
        copy_char(cursor->pid, (void *) PRINTNAME(rhs), &rname);
        written = snprintf(buff, len, "%s:::%s", lname, rname);
      } else if ((uintptr_t) CAR(fun) == cursor->globals->dollar &&
                 TYPEOF(lhs) == SYMSXP && TYPEOF(rhs) == SYMSXP) {
        copy_char(cursor->pid, (void *) PRINTNAME(lhs), &lname);
        copy_char(cursor->pid, (void *) PRINTNAME(rhs), &rname);
        written = snprintf(buff, len, "%s$%s", lname, rname);
      } else {
        /* fprintf(stderr, "CAR(fun)=%p; lhs=%p; rhs=%p\n", */
        /*         (void *) CAR(fun), (void *) lhs, (void *) rhs); */
        written = snprintf(buff, len, "<Unimplemented>");
      }
    } else {
      fprintf(stderr, "TYPEOF(fun): %d\n", TYPEOF(fun));
      written = snprintf(buff, len, "<Anonymous>");
    }
  } else {
    /* fprintf(stderr, "TYPEOF(call)=%d; callflag=%d\n", TYPEOF(call), */
    /*         cptr->callflag); */
    written = snprintf(buff, len, "<Unknown>");
  }

  free(call);
  free(fun);
  free(name);

  /* Function name may be too long for the buffer. */
  if (written >= len) {
    return -2;
  }

  return 1;
}

int rstack_init(struct rstack_cursor *cursor) {
  long context_ptr = ptrace(PTRACE_PEEKTEXT, cursor->pid, cursor->globals->context_addr, NULL);
  if (context_ptr < 0) {
    perror("Error in ptrace PEEKTEXT");
    return -1;
  }

  cursor->rcxt_ptr = (void *) context_ptr;
  cursor->depth = 0;

  /* Clear any existing RCXT data. */
  if (cursor->cptr) {
    free(cursor->cptr);
    cursor->cptr = NULL;
  }

  copy_context(cursor->pid, (void *) context_ptr, &cursor->cptr);
  if (!cursor->cptr) {
    return -1;
  }

  return 0;
}

int rstack_step(struct rstack_cursor *cursor) {
  if (!cursor || !cursor->cptr) {
    return -1;
  }

  /* We're at the top level. */
  if (cursor->cptr->callflag == CTXT_TOPLEVEL) {
    return 0;
  }

  cursor->rcxt_ptr = cursor->cptr->nextcontext;
  cursor->depth++;
  free(cursor->cptr);
  cursor->cptr = NULL;

  copy_context(cursor->pid, cursor->rcxt_ptr, &cursor->cptr);
  if (!cursor->cptr) {
    return -2;
  }

  return cursor->depth;
}

void usage(const char *name) {
  // TODO: Add a long help message.
  printf("Usage: %s [-v] [-F <freq>] [-d <duration>] -p <pid>\n", name);
  return;
}

int main(int argc, char **argv) {
  pid_t pid = -1;
  int freq = DEFAULT_FREQ;
  float duration = DEFAULT_DURATION;
  int verbose = 0;

  int opt;
  while ((opt = getopt(argc, argv, "hvF:d:p:")) != -1) {
    switch (opt) {
    case 'h':
      usage(argv[0]);
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
    case 'F':
      freq = strtol(optarg, NULL, 10);
      if ((errno == ERANGE && (freq == LONG_MAX || freq == LONG_MIN)) ||
          (errno != 0 && freq == 0)) {
        perror("strtol");
        return 1;
      }
      if (freq < 0) {
        freq = DEFAULT_FREQ;
        fprintf(stderr, "Invalid frequency, falling back on the default %d.\n",
                freq);
      } else if (freq > MAX_FREQ) {
        freq = MAX_FREQ;
        fprintf(stderr, "Frequency cannot exceed %d, using that instead.\n",
                freq);
      }
      break;
    case 'd':
      duration = strtof(optarg, NULL);
      if (errno != 0 && duration == 0) {
        perror("strtof");
        return 1;
      }
      if (duration < 0) {
        duration = DEFAULT_DURATION;
        fprintf(stderr, "Invalid duration, ignoring.\n");
      }
      break;
    default: /* '?' */
      usage(argv[0]);
      return 1;
      break;
    }
  }

  // A PID is required.
  if (pid == -1) {
    usage(argv[0]);
    return 1;
  }

  struct timespec sleep_spec;
  sleep_spec.tv_sec = freq == 1 ? 1 : 0;
  sleep_spec.tv_nsec = freq == 1 ? 0 : 1000000000 / freq;

  int code = 0;

  /* First, check that we can attach to the process. */

  if (ptrace(PTRACE_SEIZE, pid, NULL, NULL)) {
    perror("Error in ptrace SEIZE");
    return 1;
  }

  struct rstack_cursor *cursor = rstack_create(pid);


  /* Stop the tracee and read the R stack information. */

  // Allow the user to stop the tracing with Ctrl-C.
  signal(SIGINT, handle_sigint);
  float elapsed = 0;

  // Write the Rprof.out header.
  printf("sample.interval=%d\n", 1000000 / freq);

  while (should_trace && elapsed <= duration) {
    if (ptrace(PTRACE_INTERRUPT, pid, NULL, NULL)) {
      perror("ptrace INTERRUPT");
      code++;
      goto done;
    }
    int wstatus;
    if (waitpid(pid, &wstatus, 0) < 0) {
      perror("waitpid");
      code++;
      goto done;
    }
    if (WIFEXITED(wstatus)) {
      fprintf(stderr, "Process %d finished.\n", pid);
      break;
    } else if (WIFSTOPPED(wstatus) && WSTOPSIG(wstatus) == SIGCHLD) {
      ptrace(PTRACE_CONT, pid, NULL, NULL);
      continue;
    } else if (WIFSTOPPED(wstatus) && WSTOPSIG(wstatus) != SIGTRAP) {
      fprintf(stderr, "Unexpected stop signal: %d\n", WSTOPSIG(wstatus));
      code++;
      goto done;
    } else if (!WIFSTOPPED(wstatus)) {
      fprintf(stderr, "Unexpected waitpid status: %d\n", WSTOPSIG(wstatus));
      code++;
      goto done;
    }

    int ret;
    char rsym[256];
    if ((ret = rstack_init(cursor)) < 0) {
      code++;
      fprintf(stderr, "Failed to init R cursor: %d.\n", ret);
      goto done;
    }

    do {
      rsym[0] = '\0';
      if ((ret = rstack_get_fun_name(cursor, rsym, sizeof(rsym))) < 0) {
        code++;
        goto done;
      } else if (ret == 0) {
        printf("\"<TopLevel>\" ");
      } else {
        printf("\"%s\" ", rsym);
      }
    } while ((ret = rstack_step(cursor)) > 0);

    if (ret < 0) {
      code++;
      fprintf(stderr, "Failed to step R cursor: %d.\n", ret);
      goto done;
    }
    printf("\n");

    if (ptrace(PTRACE_CONT, pid, NULL, NULL)) {
      perror("ptrace CONT");
      code++;
      goto done;
    }
    if (nanosleep(&sleep_spec, NULL) < 0) {
      break; // Interupted.
    }
    elapsed = elapsed + 1.0 / freq;
  }

 done:
  ptrace(PTRACE_DETACH, pid, NULL, NULL);

  return code;
}
