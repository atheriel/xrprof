#include <errno.h>   /* for errno */
#include <limits.h>  /* for LONG_MIN, LONG_MAX */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>  /* for uintptr_t */
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>    /* for timespec */

#include "cursor.h"

#define MAX_STACK_DEPTH 100
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
        fprintf(stderr, "fatal: Cannot accept negative pids as input.\n");
        return 1;
      }
      break;
    case 'F':
      freq = strtol(optarg, NULL, 10);
      if (freq <= 0) {
        freq = DEFAULT_FREQ;
        fprintf(stderr, "warning: Invalid frequency argument, falling back on the default %d.\n",
                freq);
      } else if (freq > MAX_FREQ) {
        freq = MAX_FREQ;
        fprintf(stderr, "warning: Frequency cannot exceed %d, using that instead.\n",
                freq);
      }
      break;
    case 'd':
      duration = strtof(optarg, NULL);
      if (errno != 0 && duration == 0) {
        perror("warning: Failed to decode duration argument");
      }
      if (duration <= 0) {
        duration = DEFAULT_DURATION;
        fprintf(stderr, "warning: Invalid duration argument, failling back on the default %.0f.\n",
                duration);
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
    perror("fatal: Failed to attach to remote process");
    return 1;
  }

  struct xrprof_cursor *cursor = xrprof_create(pid);
  if (!cursor) {
    fprintf(stderr, "fatal: Failed to initialize R stack cursor.\n");
    code++;
    goto done;
  }

  /* Stop the tracee and read the R stack information. */

  // Allow the user to stop the tracing with Ctrl-C.
  signal(SIGINT, handle_sigint);
  float elapsed = 0;

  // Write the Rprof.out header.
  printf("sample.interval=%d\n", 1000000 / freq);

  while (should_trace && elapsed <= duration) {
    if (ptrace(PTRACE_INTERRUPT, pid, NULL, NULL)) {
      perror("fatal: Failed to interrupt remote process");
      code++;
      goto done;
    }
    int wstatus;
    if (waitpid(pid, &wstatus, 0) < 0) {
      perror("fatal: Failed to obtain remote process status information");
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
      fprintf(stderr, "fatal: Unexpected stop signal in remote process: %d.\n",
              WSTOPSIG(wstatus));
      code++;
      goto done;
    } else if (!WIFSTOPPED(wstatus)) {
      fprintf(stderr, "fatal: Unexpected remote process status: %d.\n",
              WSTOPSIG(wstatus));
      code++;
      goto done;
    }

    int ret;
    char rsym[256];
    if ((ret = xrprof_init(cursor)) < 0) {
      code++;
      fprintf(stderr, "fatal: Failed to initialize R stack cursor: %d.\n", ret);
      goto done;
    }

    do {
      rsym[0] = '\0';
      if ((ret = xrprof_get_fun_name(cursor, rsym, sizeof(rsym))) < 0) {
        code++;
        goto done;
      } else if (ret == 0) {
        printf("\"<TopLevel>\" ");
      } else {
        printf("\"%s\" ", rsym);
      }
    } while ((ret = xrprof_step(cursor)) > 0);

    if (ret < 0) {
      code++;
      fprintf(stderr, "fatal: Failed to step R stack cursor: %d.\n", ret);
      goto done;
    }
    printf("\n");

    if (ptrace(PTRACE_CONT, pid, NULL, NULL)) {
      perror("fatal: Failed to continue remote process");
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
  xrprof_destroy(cursor);

  return code;
}
