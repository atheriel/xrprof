#ifndef XRPROF_PROCESS_H
#define XRPROF_PROCESS_H

#ifdef __WIN32
typedef void * phandle;
#else
#include <unistd.h>  /* for pid_t */
typedef pid_t phandle;
#endif

int proc_create(phandle *out, void *data);
int proc_suspend(phandle pid);
int proc_resume(phandle pid);
int proc_destroy(phandle pid);

#endif /* XRPROF_PROCESS_H */
