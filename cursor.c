#include <stdlib.h>     /* for malloc, free */
#include <sys/ptrace.h> /* for ptrace */

#include "cursor.h"
#include "rtrace.h"

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
