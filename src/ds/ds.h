
// ds.h
//
// Generic data structures.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _DS_H_
#define _DS_H_

#include <stddef.h>

typedef struct list_node_s {
  void *value;
  struct list_node_s *next;
  struct list_node_s *prev;
} list_node_t;

typedef struct list_s {
  list_node_t *head;
  list_node_t *tail;
  size_t size;
} list_t;

typedef struct tree_node_s {
  void *value;
  list_t *children;
  struct tree_node_s *parent;
} tree_node_t;

void list_destroy(list_t *);
void list_push_back(list_t *, void *);
void list_pop_back(list_t *);
void list_push_front(list_t *, void *);
void list_pop_front(list_t *);
void list_insert_after(list_t *, list_node_t *, void *);
void list_insert_before(list_t *, list_node_t *, void *);
void list_remove(list_t *, list_node_t *);

tree_node_t *tree_init(void *);
void tree_destroy(tree_node_t *);

#define list_foreach(i, l)                                  \
  for (list_node_t *i = (l)->head; i != NULL; i = i->next)

#endif /* _DS_H_ */
