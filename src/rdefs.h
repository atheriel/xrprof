#ifndef XRPROF_RDEFS_H
#define XRPROF_RDEFS_H

#include <stddef.h>

/*  Extracted from Rinternals.h and Defn.h. License header reproduced below.
 *
 *  R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995, 1996  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 1999-2017   The R Core Team.
 *
 *  This header file is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This file is part of R. R is distributed under the terms of the
 *  GNU General Public License, either Version 2, June 1991 or Version 3,
 *  June 2007. See doc/COPYRIGHTS for details of the copyright status of R.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 */

/* From Rinternals.h: */

typedef unsigned int SEXPTYPE;
#define SYMSXP 1
#define LANGSXP 6

typedef struct SEXPREC *SEXP;

struct sxpinfo_struct {
  SEXPTYPE type      :  5;
  /* We don't need any of the other fields at the moment. */
#ifdef R344_COMPAT
  unsigned int pad   : 27;
#else
  /* This header changed from 32 bits to 64 after R 3.4.4. */
  unsigned long pad  : 59;
#endif
};

/* TODO: Should be int on 32-bit platforms, if we support them. */
typedef ptrdiff_t R_xlen_t;
typedef int R_len_t;

struct vecsxp_struct {
#ifdef R344_COMPAT
  R_len_t length;
  R_len_t truelength;
#else
  /* Long vectors after R 3.4.4. */
  R_xlen_t length;
  R_xlen_t truelength;
#endif
};

struct symsxp_struct {
  struct SEXPREC *pname;
  struct SEXPREC *value;
  struct SEXPREC *internal;
};

struct listsxp_struct {
  struct SEXPREC *carval;
  struct SEXPREC *cdrval;
  struct SEXPREC *tagval;
};

#define SEXPREC_HEADER \
  struct sxpinfo_struct sxpinfo; \
  struct SEXPREC *attrib; \
  struct SEXPREC *gengc_next_node, *gengc_prev_node

typedef struct SEXPREC {
  SEXPREC_HEADER;
  union {
    /* We only need symbols and lists right now. */
    struct symsxp_struct symsxp;
    struct listsxp_struct listsxp;
  } u;
} SEXPREC;

typedef struct VECTOR_SEXPREC {
  SEXPREC_HEADER;
  struct vecsxp_struct vecsxp;
} VECTOR_SEXPREC, *VECSEXP;

typedef union { VECTOR_SEXPREC s; double align; } SEXPREC_ALIGN;

#define TYPEOF(x) ((x)->sxpinfo.type)
#define CAR(x) ((x)->u.listsxp.carval)
#define CDR(x) ((x)->u.listsxp.cdrval)
#define PRINTNAME(x) ((x)->u.symsxp.pname)
#define STDVEC_DATAPTR(x) ((void *) (((SEXPREC_ALIGN *) (x)) + 1))

/* From Defn.h: */

#undef BC_INT_STACK
#ifndef JMP_BUF
#include <setjmp.h>
#define JMP_BUF jmp_buf
#endif

typedef struct {
  int tag;
  union {
    int ival;
    double dval;
    SEXP sxpval;
  } u;
} R_bcstack_t;

/* Evaluation Context Structure */
typedef struct RCNTXT {
  struct RCNTXT *nextcontext;   /* The next context up the chain */
  int callflag;     /* The context "type" */
  JMP_BUF cjmpbuf;    /* C stack and register information */
  int cstacktop;    /* Top of the pointer protection stack */
  int evaldepth;          /* evaluation depth at inception */
  SEXP promargs;    /* Promises supplied to closure */
  SEXP callfun;     /* The closure called */
  SEXP sysparent;     /* environment the closure was called from */
  SEXP call;      /* The call that effected this context*/
  SEXP cloenv;    /* The environment */
  SEXP conexit;     /* Interpreted "on.exit" code */
  void (*cend)(void *);   /* C "on.exit" thunk */
  void *cenddata;     /* data for C "on.exit" thunk */
  void *vmax;             /* top of R_alloc stack */
  int intsusp;                /* interrupts are suspended */
  int gcenabled;    /* R_GCEnabled value */
  int bcintactive;            /* R_BCIntActive value */
  SEXP bcbody;                /* R_BCbody value */
  void* bcpc;                 /* R_BCpc value */
  SEXP handlerstack;          /* condition handler stack */
  SEXP restartstack;          /* stack of available restarts */
  struct RPRSTACK *prstack;   /* stack of pending promises */
  R_bcstack_t *nodestack;
#ifdef BC_INT_STACK
  IStackval *intstack;
#endif
  SEXP srcref;          /* The source line in effect */
  int browserfinish;          /* should browser finish this context without
                                 stopping */
  SEXP returnValue;           /* only set during on.exit calls */
  struct RCNTXT *jumptarget;  /* target for a continuing jump */
  int jumpmask;               /* associated LONGJMP argument */
} RCNTXT, *context;

enum {
      CTXT_TOPLEVEL = 0,
      CTXT_NEXT     = 1,
      CTXT_BREAK    = 2,
      CTXT_LOOP     = 3,
      CTXT_FUNCTION = 4,
      CTXT_CCODE    = 8,
      CTXT_RETURN   = 12,
      CTXT_BROWSER  = 16,
      CTXT_GENERIC  = 20,
      CTXT_RESTART  = 32,
      CTXT_BUILTIN  = 64,
      CTXT_UNWIND   = 128
};

#endif /* XRPROF_RDEFS_H */
