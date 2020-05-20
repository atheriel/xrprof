#include <stdlib.h>     /* for malloc, free */
#include <stdio.h>      /* for fprintf */

#include "cursor.h"
#include "rdefs.h"
#include "locate.h"
#include "memory.h"

struct xrprof_cursor {
  void *rcxt_ptr;
  RCNTXT *cptr;
  struct libR_globals globals;
  phandle pid;
  int depth;
};

struct xrprof_cursor *xrprof_create(phandle pid) {
  /* Find the symbols and addresses we need. */
  struct libR_globals globals;
  if (locate_libR_globals(pid, &globals) < 0) return NULL;

  struct xrprof_cursor *out = malloc(sizeof(struct xrprof_cursor));
  out->rcxt_ptr = NULL;
  out->cptr = malloc(sizeof(RCNTXT));
  out->pid = pid;
  out->globals = globals;
  out->depth = 0;

  return out;
}

void xrprof_destroy(struct xrprof_cursor *cursor) {
  if (!cursor) {
    return;
  }
  if (cursor->cptr) {
    free(cursor->cptr);
  }
  return free(cursor);
}

#define MAX_SYM_LEN 128

int dump_sexp(struct xrprof_cursor *cursor, SEXP sexp, char *buff, size_t len) {
  int ret;
  switch (TYPEOF(sexp)) {
  case 0:
    ret = snprintf(buff, len, "<nil>");
    return ret >= len ? -ret : 0;
  case 3:
    ret = snprintf(buff, len, "<closure>");
    return ret >= len ? -ret : 0;
  case 4:
    ret = snprintf(buff, len, "<env>");
    return ret >= len ? -ret : 0;
  case 5:
    ret = snprintf(buff, len, "<promise>");
    return ret >= len ? -ret : 0;
  case 1: {
    char name[MAX_SYM_LEN];
    ret = copy_char(cursor->pid, (void *) PRINTNAME(sexp), name, MAX_SYM_LEN);
    if (ret < 0) {
      ret = snprintf(buff, len, "<trunc>");
    } else {
      ret = snprintf(buff, len, "%s", name);
    }
    return ret >= len ? -ret : 0;
  }
  case 2:
    /* fallthrough */
  case 6: {
    const char *type = TYPEOF(sexp) == 2 ? "pairlist" : "lang";
    size_t sublen = len - 8; /* Space for "(,<nil>)". */
    SEXPREC car, cdr;
    char carbuff[sublen], cdrbuff[sublen];

    ret = copy_sexp(cursor->pid, (void *) CAR(sexp), &car);
    if (ret < 0) {
      fprintf(stderr, "error: Unexpected R structure: current %s item has no CAR.\n", type);
      return ret;
    }
    ret = dump_sexp(cursor, &car, carbuff, sublen);
    if (ret >= sublen) {
      ret = snprintf(buff, len, "<%s:trunc>", type);
      return ret >= len ? -ret : 0;
    }

    ret = copy_sexp(cursor->pid, (void *) CDR(sexp), &cdr);
    if (ret < 0) {
      fprintf(stderr, "error: Unexpected R structure: current %s item has no CDR.\n", type);
      return ret;
    }
    ret = dump_sexp(cursor, &cdr, cdrbuff, sublen);
    if (ret >= sublen) {
      ret = snprintf(buff, len, "<%s:trunc>", type);
      return ret >= len ? -ret : 0;
    }

    ret = snprintf(buff, len, "(%s, %s)", carbuff, cdrbuff);
    return ret >= len ? -ret : 0;
  }
  default:
    ret = snprintf(buff, len, "<sexptype:%d>", TYPEOF(sexp));
    return ret < len ? -ret : 0;
  }
}

int dump_context(struct xrprof_cursor *cursor, char *buff, size_t len) {
  SEXPREC promargs, callfun, sysparent, call, cloenv;
  int ret;
  ret = copy_sexp(cursor->pid, (void *) cursor->cptr->promargs, &promargs);
  if (ret < 0) {
    fprintf(stderr, "error: Could not read SEXP for current promargs.\n");
    return ret;
  }
  ret = copy_sexp(cursor->pid, (void *) cursor->cptr->callfun, &callfun);
  if (ret < 0) {
    fprintf(stderr, "error: Could not read SEXP for current callfun.\n");
    return ret;
  }
  ret = copy_sexp(cursor->pid, (void *) cursor->cptr->sysparent, &sysparent);
  if (ret < 0) {
    fprintf(stderr, "error: Could not read SEXP for current sysparent.\n");
    return ret;
  }
  ret = copy_sexp(cursor->pid, (void *) cursor->cptr->call, &call);
  if (ret < 0) {
    fprintf(stderr, "error: Could not read SEXP for current call.\n");
    return ret;
  }
  ret = copy_sexp(cursor->pid, (void *) cursor->cptr->cloenv, &cloenv);
  if (ret < 0) {
    fprintf(stderr, "error: Could not read SEXP for current cloenv.\n");
    return ret;
  }

  const char *fmt = "\
context {\n\
  nextcontext: %p\n\
  callflag: %d\n\
  cstacktop: %d\n\
  evaldepth: %d\n\
  promargs: %p %s\n\
  callfun: %p %s\n\
  sysparent: %p %s\n\
  call: %p %s\n\
  cloenv: %p %s\n\
  intsusp: %d\n\
  gcenabled: %d\n\
  bcintactive: %d\n\
}";

  char dbuff[5][128];
  dump_sexp(cursor, &promargs, dbuff[0], 128);
  dump_sexp(cursor, &callfun, dbuff[1], 128);
  dump_sexp(cursor, &sysparent, dbuff[2], 128);
  dump_sexp(cursor, &call, dbuff[3], 128);
  dump_sexp(cursor, &cloenv, dbuff[4], 128);

  snprintf(buff, len, fmt,
           (void *) cursor->cptr->nextcontext,
           cursor->cptr->callflag,
           cursor->cptr->cstacktop,
           cursor->cptr->evaldepth,
           (void *) cursor->cptr->promargs, dbuff[0],
           (void *) cursor->cptr->callfun, dbuff[1],
           (void *) cursor->cptr->sysparent, dbuff[2],
           (void *) cursor->cptr->call, dbuff[3],
           (void *) cursor->cptr->cloenv, dbuff[4],
           cursor->cptr->intsusp,
           cursor->cptr->gcenabled,
           cursor->cptr->bcintactive);

  return 0;
}

int xrprof_get_fun_name(struct xrprof_cursor *cursor, char *buff, size_t len) {
  SEXPREC call, fun, cdr, lhs, rhs;
  char lname[MAX_SYM_LEN], rname[MAX_SYM_LEN];
  size_t written;

  if (!cursor || !cursor->cptr) {
    return -1;
  }

  /* We're at the top level. */
  if (cursor->cptr->callflag == CTXT_TOPLEVEL) {
    return 0;
  }

  int ret = copy_sexp(cursor->pid, (void *) cursor->cptr->call, &call);
  if (ret < 0) {
    fprintf(stderr, "error: Could not read SEXP for current call.\n");
    return ret;
  }

  /* Adapted from R's eval.c code for Rprof. */

  if (cursor->cptr->callflag & (CTXT_FUNCTION | CTXT_BUILTIN | CTXT_CCODE) &&
      TYPEOF(&call) == LANGSXP) {
    ret = copy_sexp(cursor->pid, (void *) CAR(&call), &fun);
    if (ret < 0) {
      fprintf(stderr, "error: Unexpected R structure: current call lang item has no CAR.\n");
      return ret;
    }
    if (TYPEOF(&fun) == SYMSXP) {
      copy_char(cursor->pid, (void *) PRINTNAME(&fun), rname, MAX_SYM_LEN);
      written = snprintf(buff, len, "%s", rname);
    } else if (TYPEOF(&fun) == LANGSXP) {
      copy_sexp(cursor->pid, (void *) CDR(&fun), &cdr);
      copy_sexp(cursor->pid, (void *) CAR(&cdr), &lhs);
      copy_sexp(cursor->pid, (void *) CDR(&cdr), &cdr);
      copy_sexp(cursor->pid, (void *) CAR(&cdr), &rhs);
      if ((uintptr_t) CAR(&fun) == cursor->globals.doublecolon &&
          TYPEOF(&lhs) == SYMSXP && TYPEOF(&rhs) == SYMSXP) {
        copy_char(cursor->pid, (void *) PRINTNAME(&lhs), lname, MAX_SYM_LEN);
        copy_char(cursor->pid, (void *) PRINTNAME(&rhs), rname, MAX_SYM_LEN);
        written = snprintf(buff, len, "%s::%s", lname, rname);
      } else if ((uintptr_t) CAR(&fun) == cursor->globals.triplecolon &&
                 TYPEOF(&lhs) == SYMSXP && TYPEOF(&rhs) == SYMSXP) {
        copy_char(cursor->pid, (void *) PRINTNAME(&lhs), lname, MAX_SYM_LEN);
        copy_char(cursor->pid, (void *) PRINTNAME(&rhs), rname, MAX_SYM_LEN);
        written = snprintf(buff, len, "%s:::%s", lname, rname);
      } else if ((uintptr_t) CAR(&fun) == cursor->globals.dollar &&
                 TYPEOF(&lhs) == SYMSXP && TYPEOF(&rhs) == SYMSXP) {
        copy_char(cursor->pid, (void *) PRINTNAME(&lhs), lname, MAX_SYM_LEN);
        copy_char(cursor->pid, (void *) PRINTNAME(&rhs), rname, MAX_SYM_LEN);
        written = snprintf(buff, len, "%s$%s", lname, rname);
      } else {
        /* fprintf(stderr, "CAR(fun)=%p; lhs=%p; rhs=%p\n", */
        /*         (void *) CAR(fun), (void *) lhs, (void *) rhs); */
        written = snprintf(buff, len, "<Unimplemented>");
      }
    } else {
      written = snprintf(buff, len, "<Anonymous>");
    }
  } else {
    char dbuff[512];
    dump_context(cursor, dbuff, 512);
    fprintf(stderr, "info: unknown %s\n", dbuff);
    written = snprintf(buff, len, "<Unknown>");
  }

  /* Function name may be too long for the buffer. */
  if (written >= len) {
    return -2;
  }

  return 1;
}

int xrprof_init(struct xrprof_cursor *cursor) {
  uintptr_t context_ptr;
  size_t bytes = copy_address(cursor->pid, (void *)cursor->globals.context_addr,
                              &context_ptr, sizeof(uintptr_t));
  if (bytes < sizeof(uintptr_t)) {
    /* copy_address() will have already printed an error. */
    return -1;
  }

  cursor->rcxt_ptr = (void *) context_ptr;
  cursor->depth = 0;

  int ret = copy_context(cursor->pid, (void *) context_ptr, cursor->cptr);
  if (ret < 0) {
    return ret;
  }

  return 0;
}

int xrprof_step(struct xrprof_cursor *cursor) {
  if (!cursor || !cursor->cptr) {
    return -1;
  }

  /* We're at the top level. */
  if (cursor->cptr->callflag == CTXT_TOPLEVEL) {
    return 0;
  }

  cursor->rcxt_ptr = cursor->cptr->nextcontext;
  cursor->depth++;

  copy_context(cursor->pid, cursor->rcxt_ptr, cursor->cptr);
  if (!cursor->cptr) {
    return -2;
  }

  return cursor->depth;
}
