
// ds.c
//
// Generic data structures.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include "../common/stdint.h"
#include "kheap.h"
#include "util.h"
#include "log.h"
#include "ds.h"

// We assume everything has been allocated with kmalloc.

static void list_destroy_nodes(list_node_t *head)
{
  while (head) {
    list_node_t *next = head->next;
    kfree(head->value);
    kfree(head);
    head = next;
  }
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
  else list->tail = new_node;
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
  else list->head = new_node;
  ++(list->size);
}

void list_remove(list_t *list, list_node_t *node, uint8_t destroy)
{
  if (node->next) node->next->prev = node->prev;
  else list->tail = node->prev;
  if (node->prev) node->prev->next = node->next;
  else list->head = node->next;
  node->next = NULL;
  node->prev = NULL;
  if (destroy) list_destroy_nodes(node);
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

static void heap_grow(heap_t *hp)
{
  size_t new_cap = hp->capacity ? hp->capacity * 2 : 1;
  heap_node_t *new_nodes = kmalloc(new_cap * sizeof(heap_node_t));
  u_memcpy(new_nodes, hp->nodes, hp->size * sizeof(heap_node_t));
  kfree(hp->nodes);
  hp->nodes = new_nodes;
  hp->capacity = new_cap;
}

static void heap_swap_nodes(heap_t *hp, size_t a, size_t b)
{
  heap_node_t n = hp->nodes[a];
  hp->nodes[a] = hp->nodes[b];
  hp->nodes[b] = n;
}

static void heap_heapify_up(heap_t *hp, size_t idx)
{
  if (idx == 0) return;
  size_t parent = (idx - 1) >> 1;
  if (hp->nodes[idx].key < hp->nodes[parent].key) {
    heap_swap_nodes(hp, parent, idx);
    heap_heapify_up(hp, parent);
  }
}

static void heap_heapify_down(heap_t *hp, size_t idx)
{
  if (idx == hp->size - 1) return;
  size_t left = (idx << 1) + 1;
  size_t right = left + 1;
  size_t min_child = right;
  if (right >= hp->size || hp->nodes[right].key > hp->nodes[left].key)
    min_child = left;

  if (hp->nodes[min_child].key < hp->nodes[idx].key) {
    heap_swap_nodes(hp, min_child, idx);
    heap_heapify_down(hp, min_child);
  }
}

void heap_push(heap_t *hp, uint64_t key, void *value)
{
  heap_node_t node = { .key = key, .value = value };
  if (hp->nodes == NULL || hp->size == hp->capacity)
    heap_grow(hp);

  hp->nodes[hp->size] = node;
  hp->size++;
  heap_heapify_up(hp, hp->size - 1);
}

heap_node_t heap_pop(heap_t *hp)
{
  if (hp->size == 0)
    return (heap_node_t){ .key = 0xDEADDEAD, .value = NULL };

  heap_node_t popped = hp->nodes[0];
  hp->nodes[0] = hp->nodes[hp->size - 1];
  hp->size--;
  heap_heapify_down(hp, 0);

  return popped;
}

heap_node_t *heap_peek(heap_t *hp)
{ return hp->nodes; }

void heap_destroy(heap_t *hp)
{ kfree(hp->nodes); }
