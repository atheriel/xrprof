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

  /* Find the symbols and addresses we need. */

  libR_globals globals = locate_libR_globals(pid);

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

    long context_ptr = ptrace(PTRACE_PEEKTEXT, pid, globals->context_addr, NULL);
    if (context_ptr < 0) {
      perror("Error in ptrace PEEKTEXT");
      code++;
      goto done;
    }

    RCNTXT *cptr = NULL;
    SEXP call = NULL, fun = NULL;
    char *name = NULL;
    int depth = 0;

    for (copy_context(pid, (void *) context_ptr, &cptr); cptr;
         copy_context(pid, (void *) cptr->nextcontext, &cptr)) {

      if (depth > MAX_STACK_DEPTH) {
        fprintf(stderr, "Warning: Exceeded max stack depth (%d)\n",
                MAX_STACK_DEPTH);
        break;
      }

      /* printf("  %p: call=%p,callflag=%d,nextcontext=%p\n", (void *) addr, */
      /*        (void *) cptr->call, cptr->callflag, (void *) cptr->nextcontext); */

      /* We're at the top level. */
      if (cptr->callflag == CTXT_TOPLEVEL) {
        if (depth > 0) printf("\n");
        break;
      }
      printf("\"");

      copy_sexp(pid, (void *) cptr->call, &call);
      if (!call) {
        fprintf(stderr, "could not read call\n");
        code++;
        goto done;
      }

      /* Adapted from R's eval.c code for Rprof. */

      if (cptr->callflag & (CTXT_FUNCTION | CTXT_BUILTIN) &&
          TYPEOF(call) == LANGSXP) {
        copy_sexp(pid, (void *) CAR(call), &fun);
        if (!fun) {
          fprintf(stderr, "lang item has no CAR\n");
          code++;
          goto done;
        }
        if (TYPEOF(fun) == SYMSXP) {
          copy_char(pid, (void *) PRINTNAME(fun), &name);
          printf("%s", name);
        } else if (TYPEOF(fun) == LANGSXP) {
          SEXP cdr = NULL, lhs = NULL, rhs = NULL;
          char *lname = NULL, *rname = NULL;
          copy_sexp(pid, (void *) CDR(fun), &cdr);
          copy_sexp(pid, (void *) CAR(cdr), &lhs);
          copy_sexp(pid, (void *) CDR(cdr), &cdr);
          copy_sexp(pid, (void *) CAR(cdr), &rhs);
          if ((uintptr_t) CAR(fun) == globals->doublecolon &&
              TYPEOF(lhs) == SYMSXP && TYPEOF(rhs) == SYMSXP) {
            copy_char(pid, (void *) PRINTNAME(lhs), &lname);
            copy_char(pid, (void *) PRINTNAME(rhs), &rname);
            printf("%s::%s", lname, rname);
          } else if ((uintptr_t) CAR(fun) == globals->triplecolon &&
              TYPEOF(lhs) == SYMSXP && TYPEOF(rhs) == SYMSXP) {
            copy_char(pid, (void *) PRINTNAME(lhs), &lname);
            copy_char(pid, (void *) PRINTNAME(rhs), &rname);
            printf("%s:::%s", lname, rname);
          } else if ((uintptr_t) CAR(fun) == globals->dollar &&
              TYPEOF(lhs) == SYMSXP && TYPEOF(rhs) == SYMSXP) {
            copy_char(pid, (void *) PRINTNAME(lhs), &lname);
            copy_char(pid, (void *) PRINTNAME(rhs), &rname);
            printf("%s$%s", lname, rname);
          } else {
            fprintf(stderr, "CAR(fun)=%p; lhs=%p; rhs=%p\n",
                    (void *) CAR(fun), (void *) lhs, (void *) rhs);
            printf("<Unimplemented>");
          }
        } else {
          printf("<Anonymous>");
          fprintf(stderr, "TYPEOF(fun): %d\n", TYPEOF(fun));
        }
      } else {
        printf("<Unknown>");
        fprintf(stderr, "TYPEOF(call)=%d; callflag=%d\n", TYPEOF(call),
                cptr->callflag);
      }

      printf("\" ");
      depth++;
    }

    free(cptr);
    free(call);
    free(fun);
    free(name);

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
