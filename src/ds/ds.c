
// ds.c
//
// Generic data structures.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include <kheap/kheap.h>
#include <util/util.h>
#include "ds.h"

// We assume everything has been allocated with kmalloc.

static void list_destroy_nodes(list_node_t *head)
{
  if (head == NULL) return;
  list_destroy_nodes(head->next);
  kfree(head->value);
  kfree(head);
}

void list_destroy(list_t *list)
{
  list_destroy_nodes(list->head);
  kfree(list);
}

void list_push_back(list_t *list, void *value)
{
  list_node_t *new_tail = kmalloc(sizeof(list_node_t));
  u_memset(new_tail, 0, sizeof(list_node_t));
  new_tail->value = value;
  new_tail->prev = list->tail;
  if (list->tail != NULL) list->tail->next = new_tail;
  list->tail = new_tail;
  if (list->head == NULL) list->head = new_tail;
  ++(list->size);
}

void list_pop_back(list_t *list)
{
  if (list->tail == NULL) return;
  list_node_t *old_tail = list->tail;
  list->tail = old_tail->prev;
  if (list->tail) list->tail->next = NULL;
  if (list->head == old_tail) list->head = old_tail->next;
  list_destroy_nodes(old_tail);
  --(list->size);
}

void list_push_front(list_t *list, void *value)
{
  list_node_t *new_head = kmalloc(sizeof(list_node_t));
  u_memset(new_head, 0, sizeof(list_node_t));
  new_head->value = value;
  new_head->next = list->head;
  if (list->head != NULL) list->head->prev = new_head;
  list->head = new_head;
  if (list->tail == NULL) list->tail = new_head;
  ++(list->size);
}

void list_pop_front(list_t *list)
{
  if (list->head == NULL) return;
  list_node_t *old_head = list->head;
  list->head = old_head->next;
  if (list->head) list->head->prev = NULL;
  old_head->next = NULL;
  if (list->tail == old_head) list->tail = old_head->prev;
  list_destroy_nodes(old_head);
  --(list->size);
}

void list_insert_after(list_t *list, list_node_t *node, void *value)
{
  list_node_t *new_node = kmalloc(sizeof(list_node_t));
  u_memset(new_node, 0, sizeof(list_node_t));
  new_node->prev = node;
  new_node->next = node->next;
  new_node->value = value;
  node->next = new_node;
  if (new_node->next) new_node->next->prev = new_node;
  ++(list->size);
}

void list_insert_before(list_t *list, list_node_t *node, void *value)
{
  list_node_t *new_node = kmalloc(sizeof(list_node_t));
  u_memset(new_node, 0, sizeof(list_node_t));
  new_node->next = node;
  new_node->prev = node->prev;
  new_node->value = value;
  node->prev = new_node;
  if (new_node->prev) new_node->prev->next = new_node;
  ++(list->size);
}

void list_remove(list_t *list, list_node_t *node)
{
  if (node->next) node->next->prev = node->prev;
  if (node->prev) node->prev->next = node->next;
  node->next = NULL;
  node->prev = NULL;
  list_destroy_nodes(node);
  --(list->size);
}

tree_node_t *tree_init(void *value)
{
  tree_node_t *root = kmalloc(sizeof(tree_node_t));
  u_memset(root, 0, sizeof(list_node_t));
  list_t *children = kmalloc(sizeof(list_t));
  u_memset(children, 0, sizeof(list_t));
  root->value = value;
  root->children = children;
  return root;
}

void tree_insert(tree_node_t *parent, tree_node_t *child)
{
  list_push_front(parent->children, child);
  child->parent = parent;
}

void tree_destroy(tree_node_t *root)
{
  if (root == NULL) return;
  list_foreach(child, root->children)
    tree_destroy(child->value);
  list_destroy(root->children);
  kfree(root->value);
  if (root->parent == NULL) kfree(root);
}
