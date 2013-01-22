#ifndef BUILTINS_H
#define BUILTINS_H

struct object_t;
struct environment_t;

struct object_t
eval(struct environment_t *environment, struct object_t *object);

struct object_t
print(struct environment_t *environment, struct object_t *object);

struct object_t
cons(struct environment_t *environment, struct object_t *object);

struct object_t
car(struct environment_t *environment, struct object_t *object);

struct object_t
cdr(struct environment_t *environment, struct object_t *object);

struct object_t
define(struct environment_t *environment, struct object_t *object);

struct object_t
set(struct environment_t *environment, struct object_t *object);

struct object_t
quote(struct environment_t *environment, struct object_t *object);

struct object_t
lambda(struct environment_t *environment, struct object_t *object);

struct object_t
disassemble(struct environment_t *environment, struct object_t *object);

struct object_t
apply(struct environment_t *environment, struct object_t *object);

struct object_t
vector(struct environment_t *environment, struct object_t *object);

#endif

