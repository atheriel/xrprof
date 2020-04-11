#include <errno.h>   /* for errno */
#include <limits.h>  /* for LONG_MIN, LONG_MAX */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>  /* for uintptr_t */
#include <string.h>
#include <unistd.h>
#include <time.h>    /* for timespec */

#ifdef __linux
#define HAVE_LIBUNWIND
#include <libunwind-ptrace.h>
#endif

#include "cursor.h"
#include "process.h"

#define MAX_STACK_DEPTH 100
#define DEFAULT_FREQ 1
#define MAX_FREQ 1000
#define DEFAULT_DURATION 3600 // One hour.

static volatile int should_trace = 1;
int install_ctrl_c_handler();

#ifdef __unix
#include <signal.h>

void handle_sigint(int _sig) {
  should_trace = 0;
}

int install_ctrl_c_handler() {
  signal(SIGINT, handle_sigint);
  return 0;
}
#else
int install_ctrl_c_handler() {
  return 0;
}
#endif

void usage(const char *name) {
  // TODO: Add a long help message.
  printf("Usage: %s [-v] [-m] [-F <freq>] [-d <duration>] -p <pid>\n", name);
  return;
}

int main(int argc, char **argv) {
  pid_t pid = -1;
  int freq = DEFAULT_FREQ;
  float duration = DEFAULT_DURATION;
  int verbose = 0;
#ifdef HAVE_LIBUNWIND
  int mixed_mode = 0;
#endif

  int opt;
  while ((opt = getopt(argc, argv, "hvmF:d:p:")) != -1) {
    switch (opt) {
    case 'h':
      usage(argv[0]);
      return 0;
      break;
    case 'v':
      verbose++;
      break;
    case 'm':
#ifdef HAVE_LIBUNWIND
      mixed_mode = 1;
#else
      /* TODO: We should probably warn the user. */
#endif
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

  phandle proc;
  int code = 0;

  /* First, check that we can attach to the process. */

  if ((code = proc_create(&proc, (void *) &pid)) < 0) {
    return -code;
  }

  struct xrprof_cursor *cursor = xrprof_create(proc);
  if (!cursor) {
    fprintf(stderr, "fatal: Failed to initialize R stack cursor.\n");
    code++;
    goto done;
  }

#ifdef HAVE_LIBUNWIND
  unw_addr_space_t uw_as;
  void *uw_cxt;
  unw_cursor_t uw_cursor;
  if (mixed_mode) {
    uw_as = unw_create_addr_space(&_UPT_accessors, 0);
    unw_set_caching_policy(uw_as, UNW_CACHE_GLOBAL);
    uw_cxt = _UPT_create(proc);
  }
#endif

  /* Stop the tracee and read the R stack information. */

  // Allow the user to stop the tracing with Ctrl-C.
  if ((code = install_ctrl_c_handler()) < 0) {
    return -code;
  }

  float elapsed = 0;

  // Write the Rprof.out header.
  printf("sample.interval=%d\n", 1000000 / freq);

  while (should_trace && elapsed <= duration) {
    if ((code = proc_suspend(proc)) < 0) {
      if (code == -2) {
        code = 0;
        break;
      }
      code = -code;
      goto done;
    }

    int ret;
    char rsym[256];
    if ((ret = xrprof_init(cursor)) < 0) {
      code++;
      fprintf(stderr, "fatal: Failed to initialize R stack cursor: %d.\n", ret);
      goto done;
    }

#ifdef HAVE_LIBUNWIND
    if (mixed_mode && unw_init_remote(&uw_cursor, uw_as, uw_cxt) != 0) {
      perror("fatal: Failed to initialize libunwind cursor.");
      code++;
      goto done;
    } else if (mixed_mode) {
      char sym[256];
      unw_word_t offset, ip;
      unw_proc_info_t info;

      do {
        sym[0] = '\0';
        if ((ret = unw_get_proc_info(&uw_cursor, &info)) < 0) {
          code++;
          fprintf(stderr, "fatal: Failed to get proc info via libunwind: %d.\n",
                  ret);
          goto done;
        }

        if ((ret = unw_get_reg(&uw_cursor, UNW_REG_IP, &ip)) < 0) {
          code++;
          fprintf(stderr, "fatal: Failed to get IP register via libunwind: %d.\n",
                  ret);
          goto done;
        }

        if ((ret = unw_get_proc_name(&uw_cursor, sym, sizeof(sym), &offset)) < 0) {
          if (ret == -UNW_EUNSPEC || ret == -UNW_ENOINFO) {
            printf("\"<Native:0x%lx>\" ", ip);
            continue;
          } else if (ret != -UNW_ENOINFO) {
            code++;
            fprintf(stderr, "fatal: Failed to get proc symbol via libunwind: %d.\n",
                    ret);
            goto done;
          }
          /* Symbol is truncated but otherwise fine. */
        }

        /* We're not actually in the named procedure, but nearby.
           TODO: The printed address is wrong; it does not account for ASLR. */
        if (ip > info.end_ip) {
          printf("\"<Native:0x%lx>\" ", ip);
          continue;
        }

        /* TODO: Not sure what's going on here. */
        if (strncmp(sym, "do_Rprof", 8) == 0) {
          continue;
        }

        /* Look for the first eval call and jump into the R stack. */
        if (strncmp(sym, "Rf_eval", 7) == 0) {
          break;
        }

        /* Bail if we get to what looks like the REPL loop. */
        if (strncmp(sym, "Rf_ReplIteration", 16) == 0) {
          break;
        }

        printf("\"<Native:%s>\" ", sym);
      } while ((ret = unw_step(&uw_cursor)) > 0);

      if (ret < 0) {
        code++;
        fprintf(stderr, "fatal: Failed to step libunwind cursor: %d.\n", ret);
        goto done;
      }
    }
#endif

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

    if ((code = proc_resume(proc)) < 0) {
      code = -code;
      goto done;
    }
    if (nanosleep(&sleep_spec, NULL) < 0) {
      break; // Interupted.
    }
    elapsed = elapsed + 1.0 / freq;
  }

 done:
  proc_destroy(proc);
  xrprof_destroy(cursor);

  return code;
}
