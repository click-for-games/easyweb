/* easyweb - AST node helpers. */
#include "ew.h"

#include <stdlib.h>
#include <string.h>

Node* node_new(NodeKind kind, int line, int col) {
  Node* n = (Node*)ew_calloc(1, sizeof(Node));
  n->kind = kind;
  n->line = line;
  n->col = col;
  return n;
}

void node_free(Node* n) {
  if (!n) return;
  free(n->str);
  free(n->op);
  free(n->name);
  free(n->name2);
  node_free(n->left);
  node_free(n->right);
  node_free(n->third);
  node_free(n->body);
  node_free(n->init);
  node_free(n->cond);
  node_free(n->inc);
  node_free(n->elseBody);
  for (int i = 0; i < n->count; i++) node_free(n->items[i]);
  free(n->items);
  for (int i = 0; i < n->propcount; i++) {
    free(n->props[i].key);
    node_free(n->props[i].value);
  }
  free(n->props);
  free(n);
}

void node_push(Node* n, Node* child) {
  if (!n || !child) return;
  if (n->count == n->cap) {
    n->cap = n->cap ? n->cap * 2 : 4;
    n->items = (Node**)ew_realloc(n->items, sizeof(Node*) * (size_t)n->cap);
  }
  n->items[n->count++] = child;
}

void node_push_prop(Node* n, const char* key, Node* value) {
  if (!n) return;
  if (n->propcount == n->propcap) {
    n->propcap = n->propcap ? n->propcap * 2 : 4;
    n->props = (Prop*)ew_realloc(n->props, sizeof(Prop) * (size_t)n->propcap);
  }
  n->props[n->propcount].key = ew_strdup(key);
  n->props[n->propcount].value = value;
  n->propcount++;
}

Node* node_clone(Node* n) {
  if (!n) return NULL;
  Node* c = node_new(n->kind, n->line, n->col);
  c->num = n->num;
  c->bval = n->bval;
  c->line2 = n->line2;
  c->col2 = n->col2;
  c->str = n->str ? ew_strdup(n->str) : NULL;
  c->op = n->op ? ew_strdup(n->op) : NULL;
  c->name = n->name ? ew_strdup(n->name) : NULL;
  c->name2 = n->name2 ? ew_strdup(n->name2) : NULL;
  c->left = node_clone(n->left);
  c->right = node_clone(n->right);
  c->third = node_clone(n->third);
  c->body = node_clone(n->body);
  c->init = node_clone(n->init);
  c->cond = node_clone(n->cond);
  c->inc = node_clone(n->inc);
  c->elseBody = node_clone(n->elseBody);
  for (int i = 0; i < n->count; i++) node_push(c, node_clone(n->items[i]));
  for (int i = 0; i < n->propcount; i++) node_push_prop(c, n->props[i].key, node_clone(n->props[i].value));
  return c;
}
