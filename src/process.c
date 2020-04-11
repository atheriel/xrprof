#include <stdio.h>      /* for fprintf */

#include "process.h"

#ifdef __linux
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>

int proc_create(phandle *out, void *data) {
  pid_t *pid = (pid_t *) data;
  *out = *pid;
  if (ptrace(PTRACE_SEIZE, *out, NULL, NULL)) {
    perror("fatal: Failed to attach to remote process");
    return -1;
  }
  return 0;
}

int proc_suspend(phandle pid) {
  if (ptrace(PTRACE_INTERRUPT, pid, NULL, NULL)) {
    perror("fatal: Failed to interrupt remote process");
    return -1;
  }

  int wstatus;
  if (waitpid(pid, &wstatus, 0) < 0) {
    perror("fatal: Failed to obtain remote process status information");
    return -1;
  }
  if (WIFEXITED(wstatus)) {
    fprintf(stderr, "Process %d finished.\n", pid);
    return -2;
  } else if (WIFSTOPPED(wstatus) && WSTOPSIG(wstatus) == SIGCHLD) {
    /* Try again. */
    ptrace(PTRACE_CONT, pid, NULL, NULL);
    return proc_suspend(pid);
  } else if (WIFSTOPPED(wstatus) && WSTOPSIG(wstatus) != SIGTRAP) {
    fprintf(stderr, "fatal: Unexpected stop signal in remote process: %d.\n",
            WSTOPSIG(wstatus));
    return -1;
  } else if (!WIFSTOPPED(wstatus)) {
    fprintf(stderr, "fatal: Unexpected remote process status: %d.\n",
            WSTOPSIG(wstatus));
    return -1;
  }

  return 0;
}

int proc_resume(phandle pid) {
  if (ptrace(PTRACE_CONT, pid, NULL, NULL)) {
    perror("fatal: Failed to continue remote process");
    return -1;
  }
  return 0;
}

int proc_destroy(phandle pid) {
  /* We don't actually care if this succeeds or not. */
  ptrace(PTRACE_DETACH, pid, NULL, NULL);
  return 0;
}
#else
#error "No support for non-Linux platforms."
#endif
