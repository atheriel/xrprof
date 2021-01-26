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
#elif defined(__WIN32)
#include <unistd.h>  /* for pid_t */
#include <windows.h>
#include <winternl.h>

/* The internal APIs that everyone seems to use from ntdll:
   https://stackoverflow.com/questions/11010165/how-to-suspend-resume-a-process-in-windows/11010508#11010508
   https://github.com/benfred/remoteprocess/blob/cdbf4aa23f48b48f949da3dadfc5878ab6e94f53/src/windows/mod.rs#L43 */
LONG NtSuspendProcess(IN HANDLE ProcessHandle);
LONG NtResumeProcess(IN HANDLE ProcessHandle);

int proc_create(phandle *out, void *data) {
  pid_t pid = *((pid_t *) data);
  *out = OpenProcess(PROCESS_VM_READ | PROCESS_SUSPEND_RESUME |
                     PROCESS_QUERY_INFORMATION, FALSE, pid);
  if (!*out) {
    fprintf(stderr, "error: Failed to open process %I64d: %ld.\n", pid,
            GetLastError());
    return -1;
  }
  return 0;
}

int proc_suspend(phandle pid) {
  NTSTATUS ret = NtSuspendProcess(pid);
  if (ret == 0XC000010A) {
    fprintf(stderr, "Process finished.\n");
    return -2;
  }
  if (ret == 0XC0000002) {
    /* Running under Wine. */
    fprintf(stderr, "warning: Process cannot be suspended/resumed (%#lX).\n",
            ret);
    return 0;
  }
  if (ret != 0) {
    fprintf(stderr, "error: Failed to suspend process: %ld (%#lX).\n",
            RtlNtStatusToDosError(ret), ret);
    return -1;
  }
  return 0;
}

int proc_resume(phandle pid) {
  NTSTATUS ret = NtResumeProcess(pid);
  if (ret == 0XC0000002) {
    /* Running under Wine. */
    fprintf(stderr, "warning: Process cannot be suspended/resumed (%#lX).\n",
            ret);
    return 0;
  }
  if (ret != 0) {
    fprintf(stderr, "error: Failed to resume process: %ld (%#lX).\n",
            RtlNtStatusToDosError(ret), ret);
    return -1;
  }
  return 0;
}

int proc_destroy(phandle pid) {
  BOOL ret = CloseHandle(pid);
  if (ret == FALSE) {
    fprintf(stderr, "Failed to close process handle: %ld.\n", GetLastError());
    return -1;
  }
  return 0;
}
#elif defined(__MACH__) // macOS support.
int proc_create(phandle *out, void *data)
{
  pid_t *pid = (pid_t *) data;
  *out = *pid;
  return 0;
}

int proc_suspend(phandle pid)
{
  fprintf(stderr, "warning: Processes will not be suspended/resumed on macOS.\n");
  return 0;
}

int proc_resume(phandle pid)
{
  fprintf(stderr, "warning: Processes will not be suspended/resumed on macOS.\n");
  return 0;
}

int proc_destroy(phandle pid)
{
  return 0;
}
#else
#error "No support for this platform."
#endif
