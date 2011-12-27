#ifndef READ_H
#define READ_H

struct object_t;
struct environment_t;

struct object_t *
read(struct environment_t *environment, struct object_t *object);

#endif

