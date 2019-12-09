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

struct prof_options {
  pid_t pid;
  int freq;
  float duration;
};

void prof_options_init(struct prof_options *opts) {
  opts->pid = -1;
  opts->freq = DEFAULT_FREQ;
  opts->duration = DEFAULT_DURATION;
}

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
  int verbose = 0;
  struct prof_options opts;
  prof_options_init(&opts);

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
      opts.pid = strtol(optarg, NULL, 10);
      if ((errno == ERANGE && (opts.pid == LONG_MAX || opts.pid == LONG_MIN)) ||
          (errno != 0 && opts.pid == 0)) {
        perror("strtol");
        return 1;
      }
      if (opts.pid < 0) {
        fprintf(stderr, "cannot accept negative pids\n");
        return 1;
      }
      break;
    case 'F':
      opts.freq = strtol(optarg, NULL, 10);
      if ((errno == ERANGE && (opts.freq == LONG_MAX || opts.freq == LONG_MIN)) ||
          (errno != 0 && opts.freq == 0)) {
        perror("strtol");
        return 1;
      }
      if (opts.freq < 0) {
        opts.freq = DEFAULT_FREQ;
        fprintf(stderr, "Invalid frequency, falling back on the default %d.\n",
                opts.freq);
      } else if (opts.freq > MAX_FREQ) {
        opts.freq = MAX_FREQ;
        fprintf(stderr, "Frequency cannot exceed %d, using that instead.\n",
                opts.freq);
      }
      break;
    case 'd':
      opts.duration = strtof(optarg, NULL);
      if (errno != 0 && opts.duration == 0) {
        perror("strtof");
        return 1;
      }
      if (opts.duration < 0) {
        opts.duration = DEFAULT_DURATION;
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
  if (opts.pid == -1) {
    usage(argv[0]);
    return 1;
  }

  struct timespec sleep_spec;
  sleep_spec.tv_sec = opts.freq == 1 ? 1 : 0;
  sleep_spec.tv_nsec = opts.freq == 1 ? 0 : 1000000000 / opts.freq;

  int code = 0;

  /* First, check that we can attach to the process. */

  if (ptrace(PTRACE_SEIZE, opts.pid, NULL, NULL)) {
    perror("Error in ptrace SEIZE");
    return 1;
  }

  struct rstack_cursor *cursor = rstack_create(opts.pid);


  /* Stop the tracee and read the R stack information. */

  // Allow the user to stop the tracing with Ctrl-C.
  signal(SIGINT, handle_sigint);
  float elapsed = 0;

  // Write the Rprof.out header.
  printf("sample.interval=%d\n", 1000000 / opts.freq);

  while (should_trace && elapsed <= opts.duration) {
    if (ptrace(PTRACE_INTERRUPT, opts.pid, NULL, NULL)) {
      perror("ptrace INTERRUPT");
      code++;
      goto done;
    }
    int wstatus;
    if (waitpid(opts.pid, &wstatus, 0) < 0) {
      perror("waitpid");
      code++;
      goto done;
    }
    if (WIFEXITED(wstatus)) {
      fprintf(stderr, "Process %d finished.\n", opts.pid);
      break;
    } else if (WIFSTOPPED(wstatus) && WSTOPSIG(wstatus) == SIGCHLD) {
      ptrace(PTRACE_CONT, opts.pid, NULL, NULL);
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

    if (ptrace(PTRACE_CONT, opts.pid, NULL, NULL)) {
      perror("ptrace CONT");
      code++;
      goto done;
    }
    if (nanosleep(&sleep_spec, NULL) < 0) {
      break; // Interupted.
    }
    elapsed = elapsed + 1.0 / opts.freq;
  }

 done:
  ptrace(PTRACE_DETACH, opts.pid, NULL, NULL);

  return code;
}
