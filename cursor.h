#ifndef RTRACE_CURSOR_H
#define RTRACE_CURSOR_H

#include <unistd.h> /* for pid_t */

struct rstack_cursor;

struct rstack_cursor *rstack_create(pid_t pid);
void rstack_destroy(struct rstack_cursor *cursor);
int rstack_init(struct rstack_cursor *cursor);

int rstack_get_fun_name(struct rstack_cursor *cursor, char *buff, size_t len);
int rstack_step(struct rstack_cursor *cursor);

#endif /* RTRACE_CURSOR_H */
